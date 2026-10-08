#include "audioui/AudioEditor.h"

#include "app/Busy.h"
#include "app/Theme.h"
#include "audioui/PcmPlayer.h"
#include "audioui/WaveformWidget.h"
#include "media/FFmpegJob.h"
#include "media/MediaInfo.h"

#include <QAction>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QToolBar>
#include <QUndoCommand>
#include <QUndoView>
#include <QVBoxLayout>

#include <atomic>
#include <cmath>

namespace mf {

namespace {

class AudioCommand : public QUndoCommand {
public:
    AudioCommand(AudioEditor* ed, PcmBuffer before, PcmBuffer after, const QString& text, QPair<qint64, qint64> selBefore,
                 QPair<qint64, qint64> selAfter)
        : QUndoCommand(text), m_ed(ed), m_before(std::move(before)), m_after(std::move(after)), m_selBefore(selBefore),
          m_selAfter(selAfter)
    {
    }
    void undo() override { m_ed->restore(m_before, m_selBefore.first, m_selBefore.second); }
    void redo() override
    {
        if (m_first) {
            m_first = false;
            return;
        }
        m_ed->restore(m_after, m_selAfter.first, m_selAfter.second);
    }

private:
    AudioEditor* m_ed;
    PcmBuffer m_before, m_after;
    QPair<qint64, qint64> m_selBefore, m_selAfter;
    bool m_first = true;
};

QString fmt(double t)
{
    const qint64 ms = qint64(std::llround(t * 1000));
    return QStringLiteral("%1:%2.%3")
        .arg(ms / 60000, 2, 10, QLatin1Char('0'))
        .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

QByteArray toInt16(const PcmBuffer& pcm, qint64 a, qint64 b)
{
    QByteArray out;
    const qint64 n = (b - a) * pcm.channels;
    out.resize(n * 2);
    auto* dst = reinterpret_cast<qint16*>(out.data());
    const float* src = pcm.data.constData() + a * pcm.channels;
    for (qint64 i = 0; i < n; ++i)
        dst[i] = qint16(std::lround(std::clamp(src[i], -1.f, 1.f) * 32767.f));
    return out;
}

} // namespace

AudioEditor::AudioEditor(QWidget* parent)
    : Workspace(parent)
{
    m_player = new PcmPlayer(this);
    m_wave = new WaveformWidget;
    m_wave->setBuffer(&m_pcm);
    setCentralWidget(m_wave);
    buildActions();
    buildLayout();
    m_playTimer.setInterval(25);
    connect(&m_playTimer, &QTimer::timeout, this, [this] {
        const qint64 f = m_playStart + qint64(m_playClock.nsecsElapsed() / 1e9 * m_pcm.rate);
        if (f >= m_playEnd) {
            stopPlayback();
            m_wave->setPlayhead(m_playEnd);
            return;
        }
        m_wave->setPlayhead(f, true);
    });
    connect(m_wave, &WaveformWidget::selectionChanged, this, [this] {
        updateInfo();
        updateActions();
    });
    connect(m_wave, &WaveformWidget::playheadMoved, this, [this] {
        if (m_playing)
            stopPlayback();
        updateInfo();
    });
    connect(m_wave, &WaveformWidget::filesDropped, this, &AudioEditor::openFiles);
    connect(&m_undo, &QUndoStack::cleanChanged, this, &Workspace::titleChanged);
    updateInfo();
    updateActions();
}

AudioEditor::~AudioEditor()
{
    m_undo.disconnect(this);
    m_player->stop();
}

QAction* AudioEditor::act(const QString& text, const QString& iconName, const QKeySequence& key, std::function<void()> fn)
{
    auto* a = new QAction(text, this);
    if (!iconName.isEmpty())
        a->setIcon(icon(iconName));
    if (!key.isEmpty())
        a->setShortcut(key);
    connect(a, &QAction::triggered, this, [fn] { fn(); });
    return a;
}

void AudioEditor::buildActions()
{
    auto& a = m_a;
    a["open"] = act(QStringLiteral("Открыть аудио или видео…"), "open", QKeySequence::Open, [this] { openDialog(); });
    a["extract"] = act(QStringLiteral("Извлечь звук из видео в файл…"), "video", QKeySequence(QStringLiteral("Ctrl+Shift+X")),
                       [this] { extractFromVideo(); });
    a["export"] = act(QStringLiteral("Экспорт аудио (MP3, WAV, FLAC, OGG, AAC)…"), "export", QKeySequence(QStringLiteral("Ctrl+E")),
                      [this] { exportAudio(); });
    a["toVideoAdd"] = act(QStringLiteral("Добавить в видеопроект как дорожку"), "video", QKeySequence(), [this] { sendTo(false); });
    a["toVideoReplace"] = act(QStringLiteral("Заменить звук в видеопроекте"), "video", QKeySequence(), [this] { sendTo(true); });

    a["undo"] = m_undo.createUndoAction(this, QStringLiteral("Отменить"));
    a["undo"]->setShortcut(QKeySequence::Undo);
    a["undo"]->setIcon(icon("undo"));
    a["redo"] = m_undo.createRedoAction(this, QStringLiteral("Повторить"));
    a["redo"]->setShortcuts({QKeySequence(QStringLiteral("Ctrl+Shift+Z")), QKeySequence(QStringLiteral("Ctrl+Y"))});
    a["redo"]->setIcon(icon("redo"));
    a["cut"] = act(QStringLiteral("Вырезать"), "scissors", QKeySequence::Cut, [this] { copy(true); });
    a["copy"] = act(QStringLiteral("Копировать"), QString(), QKeySequence::Copy, [this] { copy(false); });
    a["paste"] = act(QStringLiteral("Вставить в позицию указателя"), QString(), QKeySequence::Paste, [this] { paste(); });
    a["selectAll"] = act(QStringLiteral("Выделить всё"), QString(), QKeySequence::SelectAll, [this] { m_wave->setSelection(0, m_pcm.frames()); });
    a["deselect"] = act(QStringLiteral("Снять выделение"), QString(), QKeySequence(QStringLiteral("Ctrl+D")),
                        [this] { m_wave->setSelection(m_wave->playhead(), m_wave->playhead()); });

    a["trim"] = act(QStringLiteral("Обрезать по выделению"), "trim", QKeySequence(QStringLiteral("Ctrl+T")), [this] { trimToSelection(); });
    a["delete"] = act(QStringLiteral("Удалить выделенное"), "delete", QKeySequence::Delete, [this] { deleteSelection(); });
    a["silence"] = act(QStringLiteral("Заглушить (тишина)"), "silence", QKeySequence(QStringLiteral("Ctrl+L")), [this] { silenceSelection(); });
    a["volume"] = act(QStringLiteral("Громкость…"), "volume", QKeySequence(QStringLiteral("Ctrl+G")), [this] { volumeDialog(); });
    a["normalize"] = act(QStringLiteral("Нормализовать (−1 дБ)"), QString(), QKeySequence(), [this] { normalize(); });
    a["fadeIn"] = act(QStringLiteral("Плавное нарастание (fade in)…"), "fade-in", QKeySequence(), [this] { fade(true); });
    a["fadeOut"] = act(QStringLiteral("Плавное затухание (fade out)…"), "fade-out", QKeySequence(), [this] { fade(false); });
    a["speed"] = act(QStringLiteral("Скорость (без изменения высоты тона)…"), "speed", QKeySequence(QStringLiteral("Ctrl+R")),
                     [this] { speedDialog(); });

    a["play"] = act(QStringLiteral("Воспроизведение / пауза"), "play", QKeySequence(QStringLiteral("Space")), [this] { togglePlay(); });
    a["stop"] = act(QStringLiteral("Стоп"), "stop", QKeySequence(), [this] { stopPlayback(); });
    a["home"] = act(QStringLiteral("В начало"), "to-start", QKeySequence(QStringLiteral("Home")), [this] {
        stopPlayback();
        m_wave->setPlayhead(0, true);
        updateInfo();
    });
    a["end"] = act(QStringLiteral("В конец"), "to-end", QKeySequence(QStringLiteral("End")), [this] {
        stopPlayback();
        m_wave->setPlayhead(m_pcm.frames(), true);
        updateInfo();
    });
    a["zoomIn"] = act(QStringLiteral("Увеличить"), "zoom-in", QKeySequence(QStringLiteral("Ctrl+=")), [this] { m_wave->zoomIn(); });
    a["zoomOut"] = act(QStringLiteral("Уменьшить"), "zoom-out", QKeySequence(QStringLiteral("Ctrl+-")), [this] { m_wave->zoomOut(); });
    a["zoomFit"] = act(QStringLiteral("Весь файл"), "fit", QKeySequence(QStringLiteral("Ctrl+0")), [this] { m_wave->zoomToFit(); });
    a["zoomSel"] = act(QStringLiteral("Выделение во весь экран"), QString(), QKeySequence(QStringLiteral("Ctrl+Shift+0")),
                       [this] { m_wave->zoomToSelection(); });

    auto* file = new QMenu(QStringLiteral("&Файл"), this);
    file->addActions({a["open"], a["extract"]});
    file->addSeparator();
    file->addActions({a["export"], a["toVideoAdd"], a["toVideoReplace"]});
    auto* edit = new QMenu(QStringLiteral("&Правка"), this);
    edit->addActions({a["undo"], a["redo"]});
    edit->addSeparator();
    edit->addActions({a["cut"], a["copy"], a["paste"], a["selectAll"], a["deselect"]});
    auto* effects = new QMenu(QStringLiteral("&Эффекты"), this);
    effects->addActions({a["trim"], a["delete"], a["silence"]});
    effects->addSeparator();
    effects->addActions({a["volume"], a["normalize"], a["fadeIn"], a["fadeOut"], a["speed"]});
    auto* play = new QMenu(QStringLiteral("&Воспроизведение"), this);
    play->addActions({a["play"], a["stop"], a["home"], a["end"]});
    auto* view = new QMenu(QStringLiteral("В&ид"), this);
    view->addActions({a["zoomIn"], a["zoomOut"], a["zoomFit"], a["zoomSel"]});
    m_menus = {file, edit, effects, play, view};
}

void AudioEditor::buildLayout()
{
    auto* tb = new QToolBar(QStringLiteral("Аудио"), this);
    tb->setObjectName("audioToolbar");
    tb->setMovable(false);
    tb->setIconSize(QSize(20, 20));
    tb->addActions({m_a["open"], m_a["export"]});
    tb->addSeparator();
    tb->addActions({m_a["undo"], m_a["redo"]});
    tb->addSeparator();
    tb->addActions({m_a["home"], m_a["play"], m_a["stop"], m_a["end"]});
    tb->addSeparator();
    tb->addActions({m_a["trim"], m_a["cut"], m_a["delete"], m_a["silence"], m_a["volume"], m_a["fadeIn"], m_a["fadeOut"], m_a["speed"]});
    tb->addSeparator();
    tb->addActions({m_a["zoomIn"], m_a["zoomOut"], m_a["zoomFit"]});
    addToolBar(Qt::TopToolBarArea, tb);

    auto* sel = new QToolBar(QStringLiteral("Выделение"), this);
    sel->setObjectName("audioSelection");
    sel->setMovable(false);
    sel->addWidget(new QLabel(QStringLiteral(" Выделение с ")));
    m_selStartSpin = new QDoubleSpinBox;
    m_selEndSpin = new QDoubleSpinBox;
    for (QDoubleSpinBox* s : {m_selStartSpin, m_selEndSpin}) {
        s->setDecimals(3);
        s->setSuffix(QStringLiteral(" с"));
        s->setKeyboardTracking(false);
        s->setRange(0, 0);
        s->setSingleStep(0.1);
    }
    sel->addWidget(m_selStartSpin);
    sel->addWidget(new QLabel(QStringLiteral(" по ")));
    sel->addWidget(m_selEndSpin);
    sel->addSeparator();
    sel->addAction(m_a["toVideoAdd"]);
    sel->addAction(m_a["toVideoReplace"]);
    addToolBarBreak();
    addToolBar(Qt::TopToolBarArea, sel);
    auto apply = [this] {
        if (m_syncingSpins)
            return;
        m_wave->setSelection(m_pcm.frameAt(m_selStartSpin->value()), m_pcm.frameAt(m_selEndSpin->value()));
    };
    connect(m_selStartSpin, &QDoubleSpinBox::valueChanged, this, apply);
    connect(m_selEndSpin, &QDoubleSpinBox::valueChanged, this, apply);

    m_info = new QLabel;
    statusBar()->addPermanentWidget(m_info);

    auto* history = new QUndoView(&m_undo);
    history->setEmptyLabel(QStringLiteral("Исходный файл"));
    auto* dock = new QDockWidget(QStringLiteral("История"), this);
    dock->setObjectName("audioHistory");
    dock->setWidget(history);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    resizeDocks({dock}, {220}, Qt::Horizontal);
}

QString AudioEditor::documentTitle() const
{
    if (m_name.isEmpty())
        return QStringLiteral("Аудио");
    return m_undo.isClean() ? m_name : m_name + QStringLiteral(" *");
}

bool AudioEditor::confirmClose()
{
    if (m_undo.isClean() || m_pcm.isEmpty())
        return true;
    const auto r = QMessageBox::question(this, QStringLiteral("Несохранённые изменения"),
                                         QStringLiteral("Звук «%1» изменён и не экспортирован. Закрыть без сохранения?").arg(m_name),
                                         QMessageBox::Discard | QMessageBox::Cancel);
    return r == QMessageBox::Discard;
}

void AudioEditor::openFiles(const QStringList& paths)
{
    if (paths.isEmpty() || !confirmClose())
        return;
    loadFile(paths.first());
}

void AudioEditor::openDialog()
{
    if (!confirmClose())
        return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Открыть аудио или видео"),
                                                      QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
                                                      audioOpenFilter());
    if (!path.isEmpty())
        loadFile(path);
}

bool AudioEditor::loadFile(const QString& path)
{
    MediaInfo mi;
    QString err;
    if (!probeMedia(path, &mi, &err)) {
        QMessageBox::critical(this, QStringLiteral("Не удалось открыть файл"), err);
        return false;
    }
    if (!mi.hasAudio) {
        QMessageBox::critical(this, QStringLiteral("Нет звука"),
                              QStringLiteral("В файле «%1» нет звуковой дорожки.").arg(QFileInfo(path).fileName()));
        return false;
    }
    if (mi.duration > 3 * 3600) {
        QMessageBox::warning(this, QStringLiteral("Слишком длинный файл"),
                             QStringLiteral("Редактор держит звук в памяти; файлы длиннее 3 часов не поддерживаются. "
                                            "Используйте «Извлечь звук из видео в файл» или обрежьте файл в видеоредакторе."));
        return false;
    }
    stopPlayback();
    std::atomic<double> progress{0.0};
    std::atomic<bool> cancel{false};
    QProgressDialog dlg(QStringLiteral("Декодирование «%1»…").arg(QFileInfo(path).fileName()), QStringLiteral("Отмена"), 0, 100, this);
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumDuration(400);
    QTimer poll;
    connect(&poll, &QTimer::timeout, &dlg, [&] { dlg.setValue(int(progress * 100)); });
    connect(&dlg, &QProgressDialog::canceled, &dlg, [&] { cancel = true; });
    poll.start(50);
    PcmBuffer pcm;
    QFutureWatcher<bool> watcher;
    QEventLoop loop;
    connect(&watcher, &QFutureWatcher<bool>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([&] {
        return decodeAudio(path, &pcm, &err, [&](double p) {
            progress = p;
            return !cancel.load();
        });
    }));
    loop.exec();
    poll.stop();
    dlg.reset();
    if (!watcher.result()) {
        if (!cancel)
            QMessageBox::critical(this, QStringLiteral("Ошибка декодирования"), err);
        return false;
    }
    m_pcm = std::move(pcm);
    m_path = path;
    m_name = QFileInfo(path).fileName();
    m_undo.clear();
    // Each undo step keeps a full copy of the samples, so long files get fewer steps.
    const double bytes = std::max<double>(1.0, m_pcm.data.size() * sizeof(float));
    m_undo.setUndoLimit(std::clamp(int(1.5e9 / bytes), 10, 100));
    m_undo.setClean();
    m_wave->setSelection(0, 0);
    m_wave->setBuffer(&m_pcm);
    m_wave->setPlayhead(0);
    updateInfo();
    updateActions();
    emit titleChanged();
    statusBar()->showMessage(QStringLiteral("Загружено: %1 (%2, шагов отмены: %3)").arg(m_name, fmt(m_pcm.duration())).arg(m_undo.undoLimit()), 6000);
    return true;
}

void AudioEditor::restore(const PcmBuffer& pcm, qint64 selA, qint64 selB)
{
    stopPlayback();
    m_pcm = pcm;
    m_wave->bufferChanged();
    m_wave->setSelection(selA, selB);
    updateInfo();
    updateActions();
}

void AudioEditor::applyEdit(const PcmBuffer& result, const QString& text, qint64 selA, qint64 selB)
{
    stopPlayback();
    const PcmBuffer before = m_pcm;
    const QPair<qint64, qint64> selBefore(m_wave->selStart(), m_wave->selEnd());
    m_pcm = result;
    m_wave->bufferChanged();
    m_wave->setSelection(selA, selB);
    m_undo.push(new AudioCommand(this, before, m_pcm, text, selBefore, {selA, selB}));
    updateInfo();
    updateActions();
}

void AudioEditor::range(qint64* a, qint64* b, bool wholeIfEmpty) const
{
    if (m_wave->hasSelection()) {
        *a = m_wave->selStart();
        *b = m_wave->selEnd();
    } else if (wholeIfEmpty) {
        *a = 0;
        *b = m_pcm.frames();
    } else {
        *a = *b = 0;
    }
}

void AudioEditor::trimToSelection()
{
    if (!m_wave->hasSelection())
        return;
    const PcmBuffer r = audioops::slice(m_pcm, m_wave->selStart(), m_wave->selEnd());
    applyEdit(r, QStringLiteral("Обрезка"), 0, r.frames());
    m_wave->zoomToFit();
}

void AudioEditor::deleteSelection()
{
    if (!m_wave->hasSelection())
        return;
    PcmBuffer r = m_pcm;
    const qint64 a = m_wave->selStart();
    audioops::removeRange(r, a, m_wave->selEnd());
    applyEdit(r, QStringLiteral("Удаление фрагмента"), a, a);
    m_wave->setPlayhead(a);
}

void AudioEditor::silenceSelection()
{
    qint64 a, b;
    range(&a, &b);
    PcmBuffer r = m_pcm;
    audioops::silence(r, a, b);
    applyEdit(r, QStringLiteral("Тишина"), m_wave->selStart(), m_wave->selEnd());
}

void AudioEditor::volumeDialog()
{
    if (m_pcm.isEmpty())
        return;
    bool ok = false;
    const double db = QInputDialog::getDouble(this, QStringLiteral("Громкость"),
                                              m_wave->hasSelection() ? QStringLiteral("Усиление выделенного, дБ (−40…+24):")
                                                                     : QStringLiteral("Усиление всего файла, дБ (−40…+24):"),
                                              3.0, -40.0, 24.0, 1, &ok, Qt::WindowFlags(), 0.5);
    if (!ok)
        return;
    qint64 a, b;
    range(&a, &b);
    PcmBuffer r = m_pcm;
    audioops::gain(r, a, b, float(std::pow(10.0, db / 20.0)));
    applyEdit(r, QStringLiteral("Громкость %1%2 дБ").arg(db > 0 ? "+" : "").arg(db), m_wave->selStart(), m_wave->selEnd());
}

void AudioEditor::normalize()
{
    qint64 a, b;
    range(&a, &b);
    const float pk = audioops::peak(m_pcm, a, b);
    if (pk <= 1e-6f) {
        statusBar()->showMessage(QStringLiteral("Фрагмент беззвучный — нормализовать нечего"), 3000);
        return;
    }
    PcmBuffer r = m_pcm;
    audioops::gain(r, a, b, float(std::pow(10.0, -1.0 / 20.0)) / pk);
    applyEdit(r, QStringLiteral("Нормализация"), m_wave->selStart(), m_wave->selEnd());
}

void AudioEditor::fade(bool in)
{
    if (m_pcm.isEmpty())
        return;
    qint64 a, b;
    if (m_wave->hasSelection()) {
        range(&a, &b);
    } else {
        bool ok = false;
        const double sec = QInputDialog::getDouble(this, in ? QStringLiteral("Нарастание") : QStringLiteral("Затухание"),
                                                   in ? QStringLiteral("Длительность нарастания от начала, с:")
                                                      : QStringLiteral("Длительность затухания до конца, с:"),
                                                   2.0, 0.01, std::max(0.01, m_pcm.duration()), 2, &ok);
        if (!ok)
            return;
        const qint64 len = m_pcm.frameAt(sec);
        a = in ? 0 : m_pcm.frames() - len;
        b = in ? len : m_pcm.frames();
    }
    PcmBuffer r = m_pcm;
    if (in)
        audioops::fadeIn(r, a, b);
    else
        audioops::fadeOut(r, a, b);
    applyEdit(r, in ? QStringLiteral("Нарастание") : QStringLiteral("Затухание"), m_wave->selStart(), m_wave->selEnd());
}

void AudioEditor::speedDialog()
{
    if (m_pcm.isEmpty())
        return;
    bool ok = false;
    const double speed = QInputDialog::getDouble(this, QStringLiteral("Скорость"),
                                                 m_wave->hasSelection() ? QStringLiteral("Скорость выделенного (0.25–4), высота тона сохраняется:")
                                                                        : QStringLiteral("Скорость всего файла (0.25–4), высота тона сохраняется:"),
                                                 1.5, 0.25, 4.0, 2, &ok, Qt::WindowFlags(), 0.05);
    if (!ok || std::abs(speed - 1.0) < 1e-9)
        return;
    qint64 a, b;
    range(&a, &b);
    const PcmBuffer src = m_pcm;
    QString err;
    PcmBuffer part;
    const bool success = runBusy<bool>(this, QStringLiteral("Изменение скорости…"), [&] {
        return audioops::changeSpeed(audioops::slice(src, a, b), speed, &part, &err);
    });
    if (!success) {
        QMessageBox::critical(this, QStringLiteral("Скорость"), err);
        return;
    }
    PcmBuffer r = m_pcm;
    audioops::removeRange(r, a, b);
    audioops::insert(r, a, part);
    applyEdit(r, QStringLiteral("Скорость %1×").arg(speed), m_wave->hasSelection() ? a : 0,
              m_wave->hasSelection() ? a + part.frames() : 0);
}

void AudioEditor::copy(bool cut)
{
    if (!m_wave->hasSelection())
        return;
    m_clipboard = audioops::slice(m_pcm, m_wave->selStart(), m_wave->selEnd());
    if (cut)
        deleteSelection();
    statusBar()->showMessage(QStringLiteral("Скопировано %1").arg(fmt(m_clipboard.duration())), 3000);
}

void AudioEditor::paste()
{
    if (m_clipboard.isEmpty() || m_pcm.isEmpty())
        return;
    PcmBuffer r = m_pcm;
    const qint64 at = m_wave->hasSelection() ? m_wave->selStart() : m_wave->playhead();
    if (m_wave->hasSelection())
        audioops::removeRange(r, m_wave->selStart(), m_wave->selEnd());
    audioops::insert(r, at, m_clipboard);
    applyEdit(r, QStringLiteral("Вставка"), at, at + m_clipboard.frames());
}

void AudioEditor::togglePlay()
{
    if (m_playing) {
        stopPlayback();
        return;
    }
    if (m_pcm.isEmpty())
        return;
    qint64 a = m_wave->playhead(), b = m_pcm.frames();
    if (m_wave->hasSelection()) {
        a = m_wave->selStart();
        b = m_wave->selEnd();
    }
    if (a >= b - 1)
        a = 0;
    auto* buf = new QBuffer;
    buf->setData(toInt16(m_pcm, a, b));
    buf->open(QIODevice::ReadOnly);
    if (!m_player->start(buf, m_pcm.rate, m_pcm.channels))
        statusBar()->showMessage(m_player->lastError(), 5000);
    m_playStart = a;
    m_playEnd = b;
    m_playClock.start();
    m_playing = true;
    m_playTimer.start();
    m_a["play"]->setIcon(icon("pause"));
}

void AudioEditor::stopPlayback()
{
    if (!m_playing)
        return;
    m_playing = false;
    m_playTimer.stop();
    m_player->stop();
    m_a["play"]->setIcon(icon("play"));
}

void AudioEditor::updateInfo()
{
    m_syncingSpins = true;
    const double dur = m_pcm.duration();
    m_selStartSpin->setRange(0, dur);
    m_selEndSpin->setRange(0, dur);
    m_selStartSpin->setValue(double(m_wave->selStart()) / std::max(1, m_pcm.rate));
    m_selEndSpin->setValue(double(m_wave->selEnd()) / std::max(1, m_pcm.rate));
    m_syncingSpins = false;
    if (m_pcm.isEmpty()) {
        m_info->setText(QStringLiteral("Файл не открыт"));
        return;
    }
    QString text = QStringLiteral("%1 Гц, %2, длительность %3, указатель %4")
                       .arg(m_pcm.rate)
                       .arg(m_pcm.channels == 2 ? QStringLiteral("стерео") : QStringLiteral("моно"))
                       .arg(fmt(dur), fmt(double(m_wave->playhead()) / m_pcm.rate));
    if (m_wave->hasSelection())
        text += QStringLiteral(", выделено %1").arg(fmt(double(m_wave->selEnd() - m_wave->selStart()) / m_pcm.rate));
    m_info->setText(text);
}

void AudioEditor::updateActions()
{
    const bool has = !m_pcm.isEmpty();
    const bool sel = m_wave->hasSelection();
    for (const char* k : {"export", "toVideoAdd", "toVideoReplace", "silence", "volume", "normalize", "fadeIn", "fadeOut",
                          "speed", "play", "stop", "selectAll", "zoomIn", "zoomOut", "zoomFit"})
        m_a[k]->setEnabled(has);
    for (const char* k : {"trim", "delete", "cut", "copy", "zoomSel"})
        m_a[k]->setEnabled(sel);
    m_a["paste"]->setEnabled(has && !m_clipboard.isEmpty());
}

bool AudioEditor::askExportSettings(const QString& title, AudioExportSettings* s, bool allowSelection, bool* onlySelection)
{
    QDialog dlg(this);
    dlg.setWindowTitle(title);
    auto* form = new QFormLayout(&dlg);
    auto* format = new QComboBox;
    for (AudioFormat f : {AudioFormat::MP3, AudioFormat::WAV, AudioFormat::FLAC, AudioFormat::OGG, AudioFormat::AAC})
        format->addItem(QStringLiteral("%1 (.%2)").arg(audioFormatName(f), audioSuffix(f)), int(f));
    auto* bitrate = new QComboBox;
    for (int b : {96, 128, 160, 192, 256, 320})
        bitrate->addItem(QStringLiteral("%1 кбит/с").arg(b), b);
    bitrate->setCurrentIndex(3);
    auto* rate = new QComboBox;
    rate->addItem(QStringLiteral("Без изменений"), 0);
    for (int r : {48000, 44100, 32000, 22050})
        rate->addItem(QStringLiteral("%1 Гц").arg(r), r);
    auto* channels = new QComboBox;
    channels->addItem(QStringLiteral("Без изменений"), 0);
    channels->addItem(QStringLiteral("Стерео"), 2);
    channels->addItem(QStringLiteral("Моно"), 1);
    form->addRow(QStringLiteral("Формат:"), format);
    form->addRow(QStringLiteral("Битрейт:"), bitrate);
    form->addRow(QStringLiteral("Частота:"), rate);
    form->addRow(QStringLiteral("Каналы:"), channels);
    QCheckBox* onlySel = nullptr;
    if (allowSelection) {
        onlySel = new QCheckBox(QStringLiteral("Только выделенный фрагмент"));
        onlySel->setEnabled(m_wave->hasSelection());
        form->addRow(QString(), onlySel);
    }
    connect(format, &QComboBox::currentIndexChanged, &dlg, [=] {
        const auto f = AudioFormat(format->currentData().toInt());
        bitrate->setEnabled(f == AudioFormat::MP3 || f == AudioFormat::OGG || f == AudioFormat::AAC);
    });
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(box);
    if (dlg.exec() != QDialog::Accepted)
        return false;
    s->format = AudioFormat(format->currentData().toInt());
    s->bitrate = bitrate->currentData().toInt();
    s->sampleRate = rate->currentData().toInt();
    s->channels = channels->currentData().toInt();
    if (onlySelection)
        *onlySelection = onlySel && onlySel->isChecked();
    const QString suffix = audioSuffix(s->format);
    const QString filter = s->format == AudioFormat::AAC ? QStringLiteral("AAC (*.m4a *.aac)")
                                                         : QStringLiteral("%1 (*.%2)").arg(audioFormatName(s->format), suffix);
    const QString base = QFileInfo(m_name.isEmpty() ? QStringLiteral("audio") : m_name).completeBaseName();
    QString path = QFileDialog::getSaveFileName(this, title,
                                                QDir(QStandardPaths::writableLocation(QStandardPaths::MusicLocation)).filePath(base + "." + suffix),
                                                filter);
    if (path.isEmpty())
        return false;
    const QString sfx = QFileInfo(path).suffix().toLower();
    if (sfx.isEmpty() || (sfx != suffix && !(s->format == AudioFormat::AAC && sfx == "aac")))
        path += "." + suffix;
    s->output = path;
    return true;
}

void AudioEditor::runJob(const QString& title, const ExportPlan& plan, const QString& output)
{
    auto* job = new FFmpegJob(this);
    auto* dlg = new QProgressDialog(QStringLiteral("%1…").arg(title), QStringLiteral("Отмена"), 0, 1000, this);
    dlg->setWindowTitle(title);
    dlg->setWindowModality(Qt::WindowModal);
    dlg->setMinimumDuration(0);
    dlg->setAutoClose(false);
    dlg->setAutoReset(false);
    connect(job, &FFmpegJob::progress, dlg, [dlg](double p) { dlg->setValue(int(p * 1000)); });
    connect(dlg, &QProgressDialog::canceled, job, &FFmpegJob::cancel);
    connect(job, &FFmpegJob::finished, this, [this, job, dlg, output, title](bool ok, const QString& err) {
        dlg->close();
        dlg->deleteLater();
        job->deleteLater();
        if (ok) {
            m_undo.setClean();
            statusBar()->showMessage(QStringLiteral("Сохранено: %1").arg(output), 8000);
            QMessageBox::information(this, title, QStringLiteral("Файл сохранён:\n%1").arg(output));
        } else if (!err.contains(QStringLiteral("отменена"))) {
            QMessageBox::critical(this, QStringLiteral("Ошибка: %1").arg(title), err);
        }
    });
    job->start(plan.args, plan.duration, output);
}

void AudioEditor::exportAudio()
{
    if (m_pcm.isEmpty())
        return;
    stopPlayback();
    AudioExportSettings s;
    bool onlySel = false;
    if (!askExportSettings(QStringLiteral("Экспорт аудио"), &s, true, &onlySel))
        return;
    static QTemporaryDir tmp;
    const QString wav = tmp.filePath(QStringLiteral("export_source.wav"));
    const PcmBuffer part = onlySel ? audioops::slice(m_pcm, m_wave->selStart(), m_wave->selEnd()) : m_pcm;
    QString err;
    const bool ok = runBusy<bool>(this, QStringLiteral("Подготовка…"), [&] { return writeWav(part, wav, &err); });
    if (!ok) {
        QMessageBox::critical(this, QStringLiteral("Экспорт аудио"), err);
        return;
    }
    runJob(QStringLiteral("Экспорт аудио"), buildAudioEncode(wav, part.duration(), s), s.output);
}

void AudioEditor::extractFromVideo()
{
    const QString video = QFileDialog::getOpenFileName(this, QStringLiteral("Видео, из которого извлечь звук"),
                                                       QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
                                                       mediaOpenFilter());
    if (video.isEmpty())
        return;
    MediaInfo mi;
    QString err;
    if (!probeMedia(video, &mi, &err)) {
        QMessageBox::critical(this, QStringLiteral("Извлечение звука"), err);
        return;
    }
    if (!mi.hasAudio) {
        QMessageBox::critical(this, QStringLiteral("Извлечение звука"),
                              QStringLiteral("В файле «%1» нет звуковой дорожки.").arg(QFileInfo(video).fileName()));
        return;
    }
    const QString savedName = m_name;
    m_name = QFileInfo(video).fileName();
    AudioExportSettings s;
    const bool ok = askExportSettings(QStringLiteral("Извлечь звук из видео"), &s, false, nullptr);
    m_name = savedName;
    if (!ok)
        return;
    runJob(QStringLiteral("Извлечение звука"), buildAudioExtract(video, mi.duration, s), s.output);
}

void AudioEditor::sendTo(bool replace)
{
    if (m_pcm.isEmpty())
        return;
    stopPlayback();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/audio");
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QStringLiteral("%1_%2.wav")
                                                .arg(QFileInfo(m_name).completeBaseName(),
                                                     QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"))));
    QString err;
    const PcmBuffer pcm = m_pcm;
    const bool ok = runBusy<bool>(this, QStringLiteral("Подготовка звука…"), [&] { return writeWav(pcm, path, &err); });
    if (!ok) {
        QMessageBox::critical(this, QStringLiteral("Не удалось передать звук"), err);
        return;
    }
    emit sendToVideo(path, replace);
}

} // namespace mf
