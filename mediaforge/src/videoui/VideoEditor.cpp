#include "videoui/VideoEditor.h"

#include "app/Busy.h"
#include "app/Theme.h"
#include "image/ImageIO.h"
#include "media/ExportBuilder.h"
#include "media/FFmpegJob.h"
#include "videoui/ClipProperties.h"
#include "videoui/ExportDialog.h"
#include "videoui/FrameRenderer.h"
#include "videoui/MediaCache.h"
#include "videoui/PreviewEngine.h"
#include "videoui/TimelineAudio.h"
#include "videoui/TimelineWidget.h"

#include <QDropEvent>
#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QProgressDialog>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QUndoView>
#include <QVBoxLayout>

#include <cmath>
#include <memory>

namespace mf {

// ============================================================ PreviewWidget

PreviewWidget::PreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAcceptDrops(true);
    setMinimumSize(240, 135);
}

void PreviewWidget::setFrame(const QImage& img)
{
    m_frame = img;
    update();
}

void PreviewWidget::setAspect(QSize res)
{
    if (!res.isEmpty() && res != m_aspect) {
        m_aspect = res;
        update();
        emit resized();
    }
}

QRect PreviewWidget::frameRect() const
{
    const QSize s = m_aspect.scaled(size() - QSize(8, 8), Qt::KeepAspectRatio);
    return QRect((width() - s.width()) / 2, (height() - s.height()) / 2, s.width(), s.height());
}

QSize PreviewWidget::renderSize() const
{
    const QSize s = frameRect().size() * devicePixelRatioF();
    const QSize capped = s.width() > m_aspect.width() ? m_aspect : s;
    return QSize(std::max(2, capped.width()), std::max(2, capped.height()));
}

void PreviewWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(18, 18, 20));
    const QRect r = frameRect();
    p.fillRect(r, Qt::black);
    if (!m_frame.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(r, m_frame);
    }
}

void PreviewWidget::resizeEvent(QResizeEvent*)
{
    emit resized();
}

void PreviewWidget::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void PreviewWidget::dropEvent(QDropEvent* e)
{
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile())
            paths << u.toLocalFile();
    emit filesDropped(paths);
}

// ============================================================ VideoEditor

namespace {
QString clock(double t, bool ms = true)
{
    const qint64 total = qint64(std::llround(t * 1000));
    const QString base = QStringLiteral("%1:%2:%3")
                             .arg(total / 3600000, 2, 10, QLatin1Char('0'))
                             .arg((total / 60000) % 60, 2, 10, QLatin1Char('0'))
                             .arg((total / 1000) % 60, 2, 10, QLatin1Char('0'));
    return ms ? base + QStringLiteral(".%1").arg(total % 1000, 3, 10, QLatin1Char('0')) : base;
}
} // namespace

VideoEditor::VideoEditor(QWidget* parent)
    : Workspace(parent)
{
    m_tl = new Timeline(this);
    m_cache = new MediaCache(this);
    m_engine = new PreviewEngine(this);
    m_audio = new TimelineAudio([this] { return m_engine->isPlaying() ? m_engine->clock() : m_playhead; }, this);
    buildActions();
    buildLayout();
    buildMenus();

    connect(m_tl, &Timeline::changed, this, &VideoEditor::onTimelineChanged);
    connect(m_tl->undoStack(), &QUndoStack::cleanChanged, this, &Workspace::titleChanged);
    connect(m_engine, &PreviewEngine::frameReady, this, [this](const QImage& img, double) { m_preview->setFrame(img); });
    connect(m_engine, &PreviewEngine::positionChanged, this, [this](double t) {
        m_playhead = t;
        m_timeline->setPlayhead(t, true);
        updateTimeLabel();
    });
    connect(m_engine, &PreviewEngine::playbackFinished, this, [this] { stopPlayback(); });
    connect(m_audio, &TimelineAudio::status, this, [this](const QString& s) { statusBar()->showMessage(s, 5000); });
    connect(m_timeline, &TimelineWidget::seekRequested, this, &VideoEditor::seek);
    connect(m_timeline, &TimelineWidget::selectionChanged, this, [this] {
        m_props->showClip(m_timeline->selectedTrack(), m_timeline->selectedId());
        updateActions();
    });
    connect(m_timeline, &TimelineWidget::filesDropped, this,
            [this](const QStringList& paths, Track track, double t) { importFiles(paths, track, t); });
    connect(m_timeline, &TimelineWidget::contextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu;
        menu.addActions({m_a["split"], m_a["delete"]});
        QMenu* speed = menu.addMenu(QStringLiteral("Скорость"));
        for (double v : {0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0})
            speed->addAction(QStringLiteral("%1×").arg(v), this, [this, v] { setSpeedOfSelected(v); });
        speed->setEnabled(m_timeline->hasSelection());
        menu.addActions({m_a["muteClip"], m_a["moveLeft"], m_a["moveRight"], m_a["clipAudio"]});
        menu.addSeparator();
        menu.addActions({m_a["frameToImage"], m_a["exportFrame"]});
        menu.exec(pos);
    });
    connect(m_preview, &PreviewWidget::resized, this, &VideoEditor::requestFrame);
    connect(m_preview, &PreviewWidget::filesDropped, this, [this](const QStringList& p) { importFiles(p, std::nullopt, -1); });
    onTimelineChanged();
}

VideoEditor::~VideoEditor()
{
    m_tl->undoStack()->disconnect(this);
    m_engine->stop();
    m_audio->stop();
}

QAction* VideoEditor::act(const QString& text, const QString& iconName, const QKeySequence& key, std::function<void()> fn)
{
    auto* a = new QAction(text, this);
    if (!iconName.isEmpty())
        a->setIcon(icon(iconName));
    if (!key.isEmpty())
        a->setShortcut(key);
    connect(a, &QAction::triggered, this, [fn] { fn(); });
    return a;
}

void VideoEditor::buildActions()
{
    auto& a = m_a;
    a["new"] = act(QStringLiteral("Новый проект"), "new", QKeySequence::New, [this] { newProject(); });
    a["open"] = act(QStringLiteral("Открыть проект…"), "open", QKeySequence::Open, [this] { openProject(); });
    a["save"] = act(QStringLiteral("Сохранить проект"), "save", QKeySequence::Save, [this] { saveProject(false); });
    a["saveAs"] = act(QStringLiteral("Сохранить проект как…"), QString(), QKeySequence(QStringLiteral("Ctrl+Shift+S")),
                      [this] { saveProject(true); });
    a["import"] = act(QStringLiteral("Импорт медиафайлов…"), "import", QKeySequence(QStringLiteral("Ctrl+I")), [this] { importDialog(); });
    a["export"] = act(QStringLiteral("Экспорт видео (MP4/WEBM)…"), "export", QKeySequence(QStringLiteral("Ctrl+E")), [this] { exportVideo(); });
    a["exportFrame"] = act(QStringLiteral("Экспорт текущего кадра…"), "frame", QKeySequence(QStringLiteral("Ctrl+Shift+E")),
                           [this] { exportFrame(); });
    a["exportFrames"] = act(QStringLiteral("Экспорт кадров диапазона в PNG…"), QString(), QKeySequence(), [this] { exportFrameSequence(); });
    a["frameToImage"] = act(QStringLiteral("Открыть кадр в редакторе изображений"), "image", QKeySequence(QStringLiteral("Ctrl+Shift+F")),
                            [this] { frameToImageEditor(); });

    a["undo"] = m_tl->undoStack()->createUndoAction(this, QStringLiteral("Отменить"));
    a["undo"]->setShortcut(QKeySequence::Undo);
    a["undo"]->setIcon(icon("undo"));
    a["redo"] = m_tl->undoStack()->createRedoAction(this, QStringLiteral("Повторить"));
    a["redo"]->setShortcuts({QKeySequence(QStringLiteral("Ctrl+Shift+Z")), QKeySequence(QStringLiteral("Ctrl+Y"))});
    a["redo"]->setIcon(icon("redo"));
    a["split"] = act(QStringLiteral("Разрезать в позиции указателя"), "split", QKeySequence(QStringLiteral("S")), [this] { splitAtPlayhead(); });
    a["delete"] = act(QStringLiteral("Удалить клип"), "delete", QKeySequence::Delete, [this] { deleteSelected(); });
    a["markIn"] = act(QStringLiteral("Отметить начало фрагмента (In)"), "marker-in", QKeySequence(QStringLiteral("I")), [this] { setMark(true); });
    a["markOut"] = act(QStringLiteral("Отметить конец фрагмента (Out)"), "marker-out", QKeySequence(QStringLiteral("O")), [this] { setMark(false); });
    a["clearMarks"] = act(QStringLiteral("Снять отметки In/Out"), QString(), QKeySequence(QStringLiteral("Alt+X")), [this] {
        m_in = m_out = -1;
        m_timeline->setRange(-1, -1);
        updateActions();
    });
    a["cutRange"] = act(QStringLiteral("Вырезать фрагмент In–Out"), "scissors", QKeySequence(QStringLiteral("Shift+Delete")), [this] {
        if (m_in >= 0 && m_out > m_in) {
            m_tl->removeRange(m_in, m_out);
            seek(m_in);
            m_in = m_out = -1;
            m_timeline->setRange(-1, -1);
            updateActions();
        }
    });
    a["trimToRange"] = act(QStringLiteral("Оставить только фрагмент In–Out"), "trim", QKeySequence(), [this] {
        if (m_in >= 0 && m_out > m_in) {
            const double in = m_in, out = m_out;
            m_tl->undoStack()->beginMacro(QStringLiteral("Обрезка по фрагменту"));
            m_tl->removeRange(out, m_tl->state().duration() + 1);
            m_tl->removeRange(0, in);
            m_tl->undoStack()->endMacro();
            m_in = m_out = -1;
            m_timeline->setRange(-1, -1);
            seek(0);
            updateActions();
        }
    });
    a["moveLeft"] = act(QStringLiteral("Сдвинуть клип раньше"), "left", QKeySequence(QStringLiteral("Alt+Left")), [this] { moveSelected(-1); });
    a["moveRight"] = act(QStringLiteral("Сдвинуть клип позже"), "right", QKeySequence(QStringLiteral("Alt+Right")), [this] { moveSelected(1); });
    a["muteClip"] = act(QStringLiteral("Звук клипа вкл./выкл."), "mute", QKeySequence(QStringLiteral("M")), [this] { toggleMuteSelected(); });
    a["muteOriginal"] = act(QStringLiteral("Заглушить исходный звук видео"), "mute", QKeySequence(), [this] {
        m_tl->setMuteOriginal(!m_tl->state().muteOriginal);
    });
    a["muteOriginal"]->setCheckable(true);
    a["clipAudio"] = act(QStringLiteral("Открыть звук клипа в аудиоредакторе"), "audio", QKeySequence(), [this] { openSelectedAudio(); });
    a["addImageClip"] = act(QStringLiteral("Добавить изображение как клип…"), "image", QKeySequence(), [this] {
        const QStringList p = QFileDialog::getOpenFileNames(this, QStringLiteral("Изображения как клипы"), QString(), imageio::openFilter());
        importFiles(p, Track::Main, -1);
    });
    a["addOverlay"] = act(QStringLiteral("Добавить изображение поверх видео (оверлей)…"), "image", QKeySequence(QStringLiteral("Ctrl+Shift+O")), [this] {
        const QStringList p = QFileDialog::getOpenFileNames(this, QStringLiteral("Изображения-оверлеи"), QString(), imageio::openFilter());
        importFiles(p, Track::Overlay, m_playhead);
    });
    a["addAudio"] = act(QStringLiteral("Добавить аудиодорожку…"), "audio", QKeySequence(QStringLiteral("Ctrl+Shift+A")), [this] {
        const QStringList p = QFileDialog::getOpenFileNames(this, QStringLiteral("Аудиофайлы"), QString(), audioOpenFilter());
        importFiles(p, Track::Audio, m_playhead);
    });

    a["play"] = act(QStringLiteral("Воспроизведение / пауза"), "play", QKeySequence(QStringLiteral("Space")), [this] { togglePlay(); });
    a["home"] = act(QStringLiteral("В начало"), "to-start", QKeySequence(QStringLiteral("Home")), [this] { seek(0); });
    a["end"] = act(QStringLiteral("В конец"), "to-end", QKeySequence(QStringLiteral("End")), [this] { seek(m_tl->state().duration()); });
    a["prev"] = act(QStringLiteral("Кадр назад"), "prev-frame", QKeySequence(QStringLiteral("Left")),
                    [this] { step(-1.0 / std::max(1.0, m_tl->state().fps)); });
    a["next"] = act(QStringLiteral("Кадр вперёд"), "next-frame", QKeySequence(QStringLiteral("Right")),
                    [this] { step(1.0 / std::max(1.0, m_tl->state().fps)); });
    a["back1"] = act(QStringLiteral("Назад на 1 с"), QString(), QKeySequence(QStringLiteral("Shift+Left")), [this] { step(-1); });
    a["fwd1"] = act(QStringLiteral("Вперёд на 1 с"), QString(), QKeySequence(QStringLiteral("Shift+Right")), [this] { step(1); });
    a["zoomIn"] = act(QStringLiteral("Увеличить таймлайн"), "zoom-in", QKeySequence(QStringLiteral("Ctrl+=")), [this] { m_timeline->zoomIn(); });
    a["zoomOut"] = act(QStringLiteral("Уменьшить таймлайн"), "zoom-out", QKeySequence(QStringLiteral("Ctrl+-")), [this] { m_timeline->zoomOut(); });
    a["zoomFit"] = act(QStringLiteral("Весь таймлайн"), "fit", QKeySequence(QStringLiteral("Ctrl+0")), [this] { m_timeline->zoomToFit(); });
}

void VideoEditor::buildMenus()
{
    auto& a = m_a;
    auto* file = new QMenu(QStringLiteral("&Файл"), this);
    file->addActions({a["new"], a["open"], a["save"], a["saveAs"]});
    file->addSeparator();
    file->addActions({a["import"], a["addImageClip"], a["addOverlay"], a["addAudio"]});
    file->addSeparator();
    file->addActions({a["export"], a["exportFrame"], a["exportFrames"], a["frameToImage"]});
    auto* edit = new QMenu(QStringLiteral("&Правка"), this);
    edit->addActions({a["undo"], a["redo"]});
    edit->addSeparator();
    edit->addActions({a["split"], a["delete"], a["moveLeft"], a["moveRight"]});
    edit->addSeparator();
    edit->addActions({a["markIn"], a["markOut"], a["clearMarks"], a["cutRange"], a["trimToRange"]});
    auto* clip = new QMenu(QStringLiteral("&Клип"), this);
    QMenu* speed = clip->addMenu(icon("speed"), QStringLiteral("Скорость"));
    for (double v : {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0})
        speed->addAction(QStringLiteral("%1×").arg(v), this, [this, v] { setSpeedOfSelected(v); });
    clip->addActions({a["muteClip"], a["muteOriginal"], a["clipAudio"]});
    auto* play = new QMenu(QStringLiteral("&Воспроизведение"), this);
    play->addActions({a["play"], a["home"], a["end"], a["prev"], a["next"], a["back1"], a["fwd1"]});
    auto* view = new QMenu(QStringLiteral("В&ид"), this);
    view->addActions({a["zoomIn"], a["zoomOut"], a["zoomFit"]});
    m_menus = {file, edit, clip, play, view};
}

void VideoEditor::buildLayout()
{
    auto* top = new QToolBar(QStringLiteral("Видео"), this);
    top->setObjectName("videoToolbar");
    top->setMovable(false);
    top->setIconSize(QSize(20, 20));
    top->addActions({m_a["import"], m_a["save"], m_a["export"]});
    top->addSeparator();
    top->addActions({m_a["undo"], m_a["redo"]});
    top->addSeparator();
    top->addActions({m_a["split"], m_a["delete"], m_a["markIn"], m_a["markOut"], m_a["cutRange"], m_a["trimToRange"]});
    top->addSeparator();
    top->addActions({m_a["addOverlay"], m_a["addAudio"], m_a["muteOriginal"]});
    top->addSeparator();
    top->addActions({m_a["exportFrame"], m_a["frameToImage"]});
    addToolBar(Qt::TopToolBarArea, top);

    m_preview = new PreviewWidget;
    auto* previewPanel = new QWidget;
    auto* pv = new QVBoxLayout(previewPanel);
    pv->setContentsMargins(0, 0, 0, 0);
    pv->setSpacing(2);
    pv->addWidget(m_preview, 1);
    auto* transport = new QWidget;
    auto* tl = new QHBoxLayout(transport);
    tl->setContentsMargins(6, 2, 6, 2);
    auto tbtn = [&](QAction* a) {
        auto* b = new QToolButton;
        b->setDefaultAction(a);
        b->setAutoRaise(true);
        b->setIconSize(QSize(22, 22));
        tl->addWidget(b);
        return b;
    };
    tl->addStretch();
    tbtn(m_a["home"]);
    tbtn(m_a["prev"]);
    m_playButton = tbtn(m_a["play"]);
    tbtn(m_a["next"]);
    tbtn(m_a["end"]);
    m_timeLabel = new QLabel;
    QFont mono(QStringLiteral("DejaVu Sans Mono"));
    mono.setStyleHint(QFont::Monospace);
    m_timeLabel->setFont(mono);
    tl->addSpacing(12);
    tl->addWidget(m_timeLabel);
    tl->addStretch();
    pv->addWidget(transport);

    m_timeline = new TimelineWidget(m_tl, m_cache);
    auto* timelinePanel = new QWidget;
    auto* tv = new QVBoxLayout(timelinePanel);
    tv->setContentsMargins(0, 0, 0, 0);
    tv->setSpacing(0);
    auto* ttb = new QToolBar;
    ttb->setIconSize(QSize(16, 16));
    ttb->addActions({m_a["zoomOut"], m_a["zoomIn"], m_a["zoomFit"]});
    auto* hint = new QLabel(QStringLiteral("  Перетаскивайте клипы и их края. S — разрезать, I/O — отметить фрагмент, "
                                           "Shift+Del — вырезать фрагмент, Ctrl+колесо — масштаб"));
    hint->setForegroundRole(QPalette::PlaceholderText);
    ttb->addWidget(hint);
    tv->addWidget(ttb);
    tv->addWidget(m_timeline, 1);

    auto* split = new QSplitter(Qt::Vertical);
    split->addWidget(previewPanel);
    split->addWidget(timelinePanel);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    setCentralWidget(split);

    m_bin = new QListWidget;
    m_bin->setIconSize(QSize(20, 20));
    m_bin->setToolTip(QStringLiteral("Двойной щелчок — добавить на таймлайн"));
    auto* binPanel = new QWidget;
    auto* bl = new QVBoxLayout(binPanel);
    bl->setContentsMargins(4, 4, 4, 4);
    bl->addWidget(m_bin, 1);
    auto* binButtons = new QHBoxLayout;
    auto* addMain = new QPushButton(QStringLiteral("В видео"));
    auto* addOv = new QPushButton(QStringLiteral("Оверлей"));
    auto* addAu = new QPushButton(QStringLiteral("В аудио"));
    addMain->setToolTip(QStringLiteral("Добавить в конец основной дорожки"));
    addOv->setToolTip(QStringLiteral("Изображение поверх видео в позиции указателя"));
    addAu->setToolTip(QStringLiteral("Звук (в т.ч. из видео) на аудиодорожку в позиции указателя"));
    binButtons->addWidget(addMain);
    binButtons->addWidget(addOv);
    binButtons->addWidget(addAu);
    bl->addLayout(binButtons);
    auto* importBtn = new QPushButton(icon("import"), QStringLiteral("Импорт…"));
    bl->addWidget(importBtn);
    connect(importBtn, &QPushButton::clicked, this, &VideoEditor::importDialog);
    connect(addMain, &QPushButton::clicked, this, [this] { addFromBin(Track::Main); });
    connect(addOv, &QPushButton::clicked, this, [this] { addFromBin(Track::Overlay); });
    connect(addAu, &QPushButton::clicked, this, [this] { addFromBin(Track::Audio); });
    connect(m_bin, &QListWidget::itemDoubleClicked, this, [this] {
        const QString path = m_bin->currentItem()->data(Qt::UserRole).toString();
        const MediaInfo mi = m_binInfo.value(path);
        addFromBin(mi.hasVideo ? Track::Main : Track::Audio);
    });
    auto* binDock = new QDockWidget(QStringLiteral("Медиафайлы"), this);
    binDock->setObjectName("binDock");
    binDock->setWidget(binPanel);
    binDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::LeftDockWidgetArea, binDock);

    m_props = new ClipProperties(m_tl);
    auto* propDock = new QDockWidget(QStringLiteral("Свойства"), this);
    propDock->setObjectName("propsDock");
    propDock->setWidget(m_props);
    propDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, propDock);
    auto* history = new QUndoView(m_tl->undoStack());
    history->setEmptyLabel(QStringLiteral("Исходное состояние"));
    auto* histDock = new QDockWidget(QStringLiteral("История"), this);
    histDock->setObjectName("videoHistory");
    histDock->setWidget(history);
    histDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, histDock);
    tabifyDockWidget(propDock, histDock);
    propDock->raise();
    resizeDocks({binDock, propDock}, {230, 290}, Qt::Horizontal);
}

QString VideoEditor::documentTitle() const
{
    const QString name = m_projectPath.isEmpty() ? QStringLiteral("Новый видеопроект") : QFileInfo(m_projectPath).fileName();
    return m_tl->undoStack()->isClean() ? name : name + QStringLiteral(" *");
}

bool VideoEditor::confirmClose()
{
    if (m_tl->undoStack()->isClean() || m_tl->state().duration() <= 0)
        return true;
    const auto r = QMessageBox::question(this, QStringLiteral("Несохранённые изменения"),
                                         QStringLiteral("Видеопроект изменён. Сохранить его?"),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel)
        return false;
    if (r == QMessageBox::Save)
        return saveProject(false);
    return true;
}

void VideoEditor::openFiles(const QStringList& paths)
{
    QStringList media;
    for (const QString& p : paths) {
        if (QFileInfo(p).suffix().compare(QLatin1String("mfv"), Qt::CaseInsensitive) == 0)
            openProject(p);
        else
            media << p;
    }
    if (!media.isEmpty())
        importFiles(media, std::nullopt, -1);
}

void VideoEditor::importDialog()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Импорт медиафайлов"),
                                                            QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
                                                            mediaOpenFilter());
    importFiles(paths, std::nullopt, -1);
}

void VideoEditor::importFiles(const QStringList& paths, std::optional<Track> forceTrack, double at)
{
    if (paths.isEmpty())
        return;
    struct Probed {
        QString path;
        MediaInfo info;
        QString error;
    };
    const QVector<Probed> probed = runBusy<QVector<Probed>>(this, QStringLiteral("Чтение медиафайлов…"), [paths] {
        QVector<Probed> out;
        for (const QString& p : paths) {
            Probed pr{p, {}, {}};
            probeAny(p, &pr.info, &pr.error);
            out << pr;
        }
        return out;
    });
    QStringList errors;
    double audioAt = at >= 0 ? at : 0.0;
    if (at < 0)
        for (const Clip& c : m_tl->state().audio)
            audioAt = std::max(audioAt, c.end());
    m_tl->undoStack()->beginMacro(QStringLiteral("Импорт"));
    for (const Probed& pr : probed) {
        if (!pr.error.isEmpty()) {
            errors << QStringLiteral("%1: %2").arg(QFileInfo(pr.path).fileName(), pr.error);
            continue;
        }
        if (!m_binInfo.contains(pr.path)) {
            m_binInfo.insert(pr.path, pr.info);
            auto* item = new QListWidgetItem(icon(pr.info.isImage ? "image" : (pr.info.hasVideo ? "video" : "audio")),
                                             QFileInfo(pr.path).fileName());
            item->setData(Qt::UserRole, pr.path);
            item->setToolTip(QStringLiteral("%1\n%2").arg(pr.path,
                                                          pr.info.isImage ? QStringLiteral("Изображение %1×%2").arg(pr.info.width).arg(pr.info.height)
                                                                          : QStringLiteral("%1 с, %2").arg(pr.info.duration, 0, 'f', 2).arg(pr.info.hasVideo ? QStringLiteral("%1×%2").arg(pr.info.width).arg(pr.info.height) : QStringLiteral("только звук"))));
            m_bin->addItem(item);
        }
        Track track = pr.info.isImage ? Track::Main : (pr.info.hasVideo ? Track::Main : Track::Audio);
        if (forceTrack) {
            track = *forceTrack;
            if (track == Track::Overlay && !pr.info.isImage) {
                errors << QStringLiteral("%1: оверлеем может быть только изображение").arg(QFileInfo(pr.path).fileName());
                continue;
            }
            if (track == Track::Main && !pr.info.hasVideo) {
                track = Track::Audio;
            }
            if (track == Track::Audio && !pr.info.hasAudio) {
                errors << QStringLiteral("%1: в файле нет звука").arg(QFileInfo(pr.path).fileName());
                continue;
            }
        }
        ClipKind kind = track == Track::Overlay ? ClipKind::Overlay
                                                : (track == Track::Audio ? ClipKind::Audio
                                                                         : (pr.info.isImage ? ClipKind::Image : ClipKind::Video));
        Clip c = m_tl->makeClip(pr.path, pr.info, kind);
        if (track == Track::Audio) {
            c.start = audioAt;
            audioAt += c.length();
        } else if (track == Track::Overlay) {
            c.start = std::max(0.0, at);
        }
        const quint64 id = m_tl->addClip(track, c);
        m_timeline->select(track, id);
    }
    m_tl->undoStack()->endMacro();
    if (!errors.isEmpty())
        QMessageBox::warning(this, QStringLiteral("Некоторые файлы не добавлены"), errors.join('\n'));
}

void VideoEditor::addFromBin(Track track)
{
    QListWidgetItem* item = m_bin->currentItem();
    if (!item) {
        statusBar()->showMessage(QStringLiteral("Выберите файл в списке медиафайлов"), 3000);
        return;
    }
    importFiles({item->data(Qt::UserRole).toString()}, track, track == Track::Main ? -1 : m_playhead);
}

void VideoEditor::addAudioTrack(const QString& path, bool replaceOriginal)
{
    m_tl->undoStack()->beginMacro(QStringLiteral("Добавление звука из аудиоредактора"));
    importFiles({path}, Track::Audio, 0.0);
    if (replaceOriginal)
        m_tl->setMuteOriginal(true);
    m_tl->undoStack()->endMacro();
    statusBar()->showMessage(replaceOriginal ? QStringLiteral("Звук заменён: исходная дорожка заглушена, новая добавлена")
                                             : QStringLiteral("Звук добавлен на аудиодорожку"),
                             5000);
}

void VideoEditor::newProject()
{
    if (!confirmClose())
        return;
    stopPlayback();
    m_tl->reset();
    m_projectPath.clear();
    m_in = m_out = -1;
    m_timeline->setRange(-1, -1);
    seek(0);
    emit titleChanged();
}

void VideoEditor::openProject(const QString& given)
{
    if (!confirmClose())
        return;
    QString path = given;
    if (path.isEmpty())
        path = QFileDialog::getOpenFileName(this, QStringLiteral("Открыть видеопроект"), QString(),
                                            QStringLiteral("Видеопроект MediaForge (*.mfv)"));
    if (path.isEmpty())
        return;
    TimelineState st;
    QStringList warnings;
    QString err;
    const bool ok = runBusy<bool>(this, QStringLiteral("Открытие проекта…"), [&] { return loadTimeline(path, &st, &warnings, &err); });
    if (!ok) {
        QMessageBox::critical(this, QStringLiteral("Не удалось открыть проект"), err);
        return;
    }
    stopPlayback();
    m_tl->reset(st);
    m_projectPath = path;
    seek(0);
    emit titleChanged();
    if (!warnings.isEmpty())
        QMessageBox::warning(this, QStringLiteral("Не все файлы найдены"), warnings.join('\n'));
}

bool VideoEditor::saveProject(bool saveAs)
{
    QString path = m_projectPath;
    if (path.isEmpty() || saveAs) {
        path = QFileDialog::getSaveFileName(this, QStringLiteral("Сохранить видеопроект"),
                                            QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).filePath("project.mfv"),
                                            QStringLiteral("Видеопроект MediaForge (*.mfv)"));
        if (path.isEmpty())
            return false;
        if (!path.endsWith(QLatin1String(".mfv"), Qt::CaseInsensitive))
            path += QStringLiteral(".mfv");
    }
    QString err;
    if (!saveTimeline(m_tl->state(), path, &err)) {
        QMessageBox::critical(this, QStringLiteral("Ошибка сохранения"), err);
        return false;
    }
    m_projectPath = path;
    m_tl->undoStack()->setClean();
    emit titleChanged();
    statusBar()->showMessage(QStringLiteral("Проект сохранён: %1").arg(path), 4000);
    return true;
}

void VideoEditor::runExport(const QString& title, const ExportPlan& plan, const QString& output, const QString& doneMessage)
{
    if (!plan.ok()) {
        QMessageBox::critical(this, title, plan.error);
        return;
    }
    auto* job = new FFmpegJob(this);
    auto* dlg = new QProgressDialog(QStringLiteral("%1…").arg(title), QStringLiteral("Отмена"), 0, 1000, this);
    dlg->setWindowTitle(title);
    dlg->setWindowModality(Qt::WindowModal);
    dlg->setMinimumDuration(0);
    dlg->setAutoClose(false);
    dlg->setAutoReset(false);
    dlg->setValue(0);
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    connect(job, &FFmpegJob::progress, dlg, [dlg, timer, title](double p) {
        dlg->setValue(int(p * 1000));
        const double el = timer->elapsed() / 1000.0;
        if (p > 0.02)
            dlg->setLabelText(QStringLiteral("%1… %2 %, осталось ~%3 с").arg(title).arg(int(p * 100)).arg(int(el / p - el)));
    });
    connect(dlg, &QProgressDialog::canceled, job, &FFmpegJob::cancel);
    connect(job, &FFmpegJob::finished, this, [this, job, dlg, timer, output, doneMessage, title](bool ok, const QString& err) {
        // The message boxes below spin a nested event loop; detach everything first.
        job->disconnect();
        dlg->disconnect();
        dlg->close();
        dlg->deleteLater();
        job->deleteLater();
        const double secs = timer->elapsed() / 1000.0;
        if (ok) {
            statusBar()->showMessage(QStringLiteral("%1 (%2 с)").arg(doneMessage).arg(secs, 0, 'f', 1), 8000);
            QMessageBox::information(this, title, QStringLiteral("%1\n%2").arg(doneMessage, output));
        } else if (!err.contains(QStringLiteral("отменена"))) {
            QMessageBox::critical(this, QStringLiteral("Ошибка: %1").arg(title), err);
        } else {
            statusBar()->showMessage(err, 4000);
        }
    });
    job->start(plan.args, plan.duration, output);
}

void VideoEditor::exportVideo()
{
    if (m_tl->state().duration() < kMinClipLength) {
        QMessageBox::information(this, QStringLiteral("Экспорт"), QStringLiteral("Таймлайн пуст — сначала добавьте клипы."));
        return;
    }
    stopPlayback();
    const QString base = m_projectPath.isEmpty() ? QStringLiteral("video") : QFileInfo(m_projectPath).completeBaseName();
    VideoExportDialog dlg(m_tl->state(), m_in, m_out, base, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const VideoExportSettings s = dlg.settings();
    QString err;
    const ExportPlan plan = runBusy<ExportPlan>(this, QStringLiteral("Подготовка…"),
                                                [&] { return buildVideoExport(m_tl->state(), s, m_exportTemp.path()); });
    runExport(QStringLiteral("Экспорт видео"), plan, s.output, QStringLiteral("Видео сохранено."));
}

QImage VideoEditor::renderFullFrame(double t)
{
    const TimelineState st = m_tl->state();
    return runBusy<QImage>(this, QStringLiteral("Подготовка кадра…"), [st, t] {
        FrameRenderer r;
        return r.render(st, t, st.resolution, true);
    });
}

void VideoEditor::exportFrame()
{
    if (m_tl->state().duration() < kMinClipLength)
        return;
    stopPlayback();
    const QImage img = renderFullFrame(m_playhead);
    const int ms = int(std::lround(m_playhead * 1000));
    const QString name = QStringLiteral("frame_%1m%2s%3.png").arg(ms / 60000, 2, 10, QLatin1Char('0')).arg((ms / 1000) % 60, 2, 10, QLatin1Char('0')).arg(ms % 1000, 3, 10, QLatin1Char('0'));
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Сохранить кадр"),
                                                QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath(name),
                                                imageio::exportFilter());
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".png");
    QString err;
    if (!imageio::saveImage(img, path, 95, &err)) {
        QMessageBox::critical(this, QStringLiteral("Ошибка"), err);
        return;
    }
    statusBar()->showMessage(QStringLiteral("Кадр сохранён: %1").arg(path), 5000);
}

void VideoEditor::exportFrameSequence()
{
    if (m_tl->state().duration() < kMinClipLength)
        return;
    stopPlayback();
    const bool range = m_in >= 0 && m_out > m_in;
    const double a = range ? m_in : 0.0, b = range ? m_out : m_tl->state().duration();
    const double frames = (b - a) * m_tl->state().fps;
    if (!range && frames > 600) {
        const auto r = QMessageBox::question(this, QStringLiteral("Экспорт кадров"),
                                             QStringLiteral("Диапазон In–Out не задан, будет сохранено около %1 кадров. Продолжить?")
                                                 .arg(int(frames)));
        if (r != QMessageBox::Yes)
            return;
    }
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Папка для кадров"),
                                                          QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    if (dir.isEmpty())
        return;
    VideoExportSettings s;
    s.format = VideoFormat::PngSequence;
    s.output = dir;
    s.rangeStart = a;
    s.rangeEnd = b;
    const ExportPlan plan = buildVideoExport(m_tl->state(), s, m_exportTemp.path());
    runExport(QStringLiteral("Экспорт кадров"), plan, QString(), QStringLiteral("Кадры сохранены в папку %1").arg(dir));
}

void VideoEditor::frameToImageEditor()
{
    if (m_tl->state().duration() < kMinClipLength)
        return;
    stopPlayback();
    const QImage img = renderFullFrame(m_playhead);
    emit sendFrameToImageEditor(img.convertToFormat(QImage::Format_ARGB32_Premultiplied),
                                QStringLiteral("Кадр %1").arg(clock(m_playhead)));
}

void VideoEditor::seek(double t)
{
    const double dur = m_tl->state().duration();
    m_playhead = std::clamp(t, 0.0, std::max(0.0, dur));
    if (m_engine->isPlaying()) {
        m_audio->stop();
        m_engine->play(m_tl->state(), m_playhead, m_preview->renderSize());
        m_audio->play(m_playhead);
    } else {
        requestFrame();
    }
    m_timeline->setPlayhead(m_playhead);
    updateTimeLabel();
}

void VideoEditor::requestFrame()
{
    m_preview->setAspect(m_tl->state().resolution);
    if (!m_engine->isPlaying())
        m_engine->requestFrame(m_tl->state(), m_playhead, m_preview->renderSize());
}

void VideoEditor::step(double seconds)
{
    if (m_engine->isPlaying())
        stopPlayback();
    seek(m_playhead + seconds);
}

void VideoEditor::togglePlay()
{
    if (m_engine->isPlaying()) {
        stopPlayback();
        return;
    }
    if (m_tl->state().duration() < kMinClipLength)
        return;
    if (m_playhead >= m_tl->state().duration() - 1e-3)
        m_playhead = 0;
    m_engine->play(m_tl->state(), m_playhead, m_preview->renderSize());
    m_audio->play(m_playhead);
    m_a["play"]->setIcon(icon("pause"));
}

void VideoEditor::stopPlayback()
{
    const bool was = m_engine->isPlaying();
    m_engine->stop();
    m_audio->stop();
    m_a["play"]->setIcon(icon("play"));
    if (was)
        requestFrame();
}

void VideoEditor::splitAtPlayhead()
{
    stopPlayback();
    if (m_timeline->hasSelection()) {
        const int idx = m_timeline->selectedIndex();
        if (idx >= 0 && m_tl->split(m_timeline->selectedTrack(), idx, m_playhead))
            return;
    }
    const int before = m_tl->state().main.size();
    m_tl->splitMainAt(m_playhead);
    if (m_tl->state().main.size() == before)
        statusBar()->showMessage(QStringLiteral("Указатель должен быть внутри клипа (не у края)"), 3000);
}

void VideoEditor::deleteSelected()
{
    const int idx = m_timeline->selectedIndex();
    if (idx < 0) {
        statusBar()->showMessage(QStringLiteral("Выберите клип на таймлайне"), 3000);
        return;
    }
    stopPlayback();
    m_tl->removeClip(m_timeline->selectedTrack(), idx);
}

void VideoEditor::setSpeedOfSelected(double speed)
{
    const int idx = m_timeline->selectedIndex();
    if (idx < 0)
        return;
    Clip c = m_tl->state().track(m_timeline->selectedTrack())[idx];
    if (c.isStill()) {
        statusBar()->showMessage(QStringLiteral("У изображения нет скорости — измените длительность"), 3000);
        return;
    }
    c.speed = speed;
    m_tl->updateClip(m_timeline->selectedTrack(), idx, c, QStringLiteral("Скорость %1×").arg(speed));
}

void VideoEditor::toggleMuteSelected()
{
    const int idx = m_timeline->selectedIndex();
    if (idx < 0)
        return;
    Clip c = m_tl->state().track(m_timeline->selectedTrack())[idx];
    if (!c.carriesAudio())
        return;
    c.muted = !c.muted;
    m_tl->updateClip(m_timeline->selectedTrack(), idx, c, c.muted ? QStringLiteral("Выключить звук") : QStringLiteral("Включить звук"));
}

void VideoEditor::openSelectedAudio()
{
    const int idx = m_timeline->selectedIndex();
    if (idx < 0)
        return;
    const Clip& c = m_tl->state().track(m_timeline->selectedTrack())[idx];
    if (!c.carriesAudio()) {
        statusBar()->showMessage(QStringLiteral("В этом клипе нет звука"), 3000);
        return;
    }
    emit openInAudioEditor(c.path);
}

void VideoEditor::moveSelected(int delta)
{
    const int idx = m_timeline->selectedIndex();
    if (idx < 0 || m_timeline->selectedTrack() != Track::Main)
        return;
    m_tl->moveMainClip(idx, std::clamp(idx + delta, 0, int(m_tl->state().main.size()) - 1));
}

void VideoEditor::setMark(bool in)
{
    if (in)
        m_in = m_playhead;
    else
        m_out = m_playhead;
    if (m_in >= 0 && m_out >= 0 && m_out < m_in)
        std::swap(m_in, m_out);
    m_timeline->setRange(m_in, m_out);
    updateTimeLabel();
    updateActions();
}

void VideoEditor::onTimelineChanged()
{
    if (m_engine->isPlaying())
        stopPlayback();
    const double dur = m_tl->state().duration();
    if (m_playhead > dur)
        m_playhead = dur;
    m_timeline->setPlayhead(m_playhead);
    m_a["muteOriginal"]->setChecked(m_tl->state().muteOriginal);
    m_audio->invalidate(m_tl->state());
    requestFrame();
    updateTimeLabel();
    updateActions();
}

void VideoEditor::updateTimeLabel()
{
    QString text = QStringLiteral("%1 / %2").arg(clock(m_playhead), clock(m_tl->state().duration()));
    if (m_in >= 0 || m_out >= 0)
        text += QStringLiteral("   In %1  Out %2")
                    .arg(m_in >= 0 ? clock(m_in) : QStringLiteral("—"), m_out >= 0 ? clock(m_out) : QStringLiteral("—"));
    m_timeLabel->setText(text);
}

void VideoEditor::updateActions()
{
    const bool has = m_tl->state().duration() > 0;
    const bool sel = m_timeline->hasSelection();
    const bool range = m_in >= 0 && m_out > m_in;
    for (const char* k : {"export", "exportFrame", "exportFrames", "frameToImage", "play", "split"})
        m_a[k]->setEnabled(has);
    for (const char* k : {"delete", "muteClip", "clipAudio"})
        m_a[k]->setEnabled(sel);
    m_a["moveLeft"]->setEnabled(sel && m_timeline->selectedTrack() == Track::Main);
    m_a["moveRight"]->setEnabled(sel && m_timeline->selectedTrack() == Track::Main);
    m_a["cutRange"]->setEnabled(range);
    m_a["trimToRange"]->setEnabled(range);
}

} // namespace mf
