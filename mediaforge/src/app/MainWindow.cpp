#include "app/MainWindow.h"

#include "app/Theme.h"
#include "audioui/AudioEditor.h"
#include "image/ImageIO.h"
#include "imageui/ImageEditor.h"
#include "media/FFmpegJob.h"
#include "media/MediaInfo.h"
#include "videoui/VideoEditor.h"

#include <QAction>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QMenuBar>
#include <QMessageBox>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

extern "C" {
#include <libavutil/avutil.h>
}

namespace mf {

MainWindow::MainWindow()
{
    setWindowIcon(icon(QStringLiteral("image")));
    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_tabs->setIconSize(QSize(18, 18));
    m_image = new ImageEditor;
    m_video = new VideoEditor;
    m_audio = new AudioEditor;
    m_tabs->addTab(m_image, icon(QStringLiteral("image")), QStringLiteral("Изображение"));
    m_tabs->addTab(m_video, icon(QStringLiteral("video")), QStringLiteral("Видео"));
    m_tabs->addTab(m_audio, icon(QStringLiteral("audio")), QStringLiteral("Аудио"));
    setCentralWidget(m_tabs);

    auto* window = new QMenu(QStringLiteral("&Окно"), this);
    const QStringList names = {QStringLiteral("Изображение"), QStringLiteral("Видео"), QStringLiteral("Аудио")};
    for (int i = 0; i < 3; ++i) {
        QAction* a = window->addAction(names[i], this, [this, i] { setWorkspace(i); });
        a->setShortcut(QKeySequence(QStringLiteral("Alt+%1").arg(i + 1)));
    }
    window->addSeparator();
    QAction* fs = window->addAction(QStringLiteral("Полноэкранный режим"), this, [this] {
        isFullScreen() ? showNormal() : showFullScreen();
    });
    fs->setShortcut(QKeySequence(QStringLiteral("F11")));
    QAction* quit = window->addAction(QStringLiteral("Выход"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);
    auto* help = new QMenu(QStringLiteral("&Справка"), this);
    help->addAction(QStringLiteral("Горячие клавиши…"), QKeySequence(QStringLiteral("F1")), this, &MainWindow::showShortcuts);
    help->addAction(QStringLiteral("О программе"), this, &MainWindow::showAbout);
    m_common = {window, help};

    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        rebuildMenus();
        updateTitle();
    });
    for (Workspace* w : {static_cast<Workspace*>(m_image), static_cast<Workspace*>(m_video), static_cast<Workspace*>(m_audio)})
        connect(w, &Workspace::titleChanged, this, &MainWindow::updateTitle);

    connect(m_video, &VideoEditor::sendFrameToImageEditor, this, [this](const QImage& img, const QString& name) {
        setWorkspace(0);
        m_image->openImage(img, name);
    });
    connect(m_video, &VideoEditor::openInAudioEditor, this, [this](const QString& path) {
        setWorkspace(2);
        m_audio->openFiles({path});
    });
    connect(m_audio, &AudioEditor::sendToVideo, this, [this](const QString& path, bool replaceOriginal) {
        setWorkspace(1);
        m_video->addAudioTrack(path, replaceOriginal);
    });

    rebuildMenus();
    updateTitle();
    resize(1440, 900);
}

MainWindow::~MainWindow()
{
    // Undo stacks emit cleanChanged while being destroyed; don't let that reach a half-destroyed window.
    for (Workspace* w : {static_cast<Workspace*>(m_image), static_cast<Workspace*>(m_video), static_cast<Workspace*>(m_audio)})
        disconnect(w, nullptr, this, nullptr);
    disconnect(m_tabs, nullptr, this, nullptr);
}

Workspace* MainWindow::current() const
{
    return static_cast<Workspace*>(m_tabs->currentWidget());
}

int MainWindow::workspaceIndex() const
{
    return m_tabs->currentIndex();
}

void MainWindow::setWorkspace(int index)
{
    m_tabs->setCurrentIndex(index);
}

void MainWindow::rebuildMenus()
{
    // Only the active workspace's actions are reachable, so shortcuts never act on a hidden tab.
    menuBar()->clear();
    for (QMenu* m : current()->menus())
        menuBar()->addMenu(m);
    for (QMenu* m : m_common)
        menuBar()->addMenu(m);
}

void MainWindow::updateTitle()
{
    QString doc;
    if (current() == m_image)
        doc = m_image->documentTitle();
    else if (current() == m_video)
        doc = m_video->documentTitle();
    else
        doc = m_audio->documentTitle();
    setWindowTitle(doc.isEmpty() ? QStringLiteral("MediaForge") : QStringLiteral("%1 — MediaForge").arg(doc));
}

void MainWindow::openFiles(const QStringList& paths)
{
    QStringList images, videos, audios, projects;
    for (const QString& p : paths) {
        const QString s = QFileInfo(p).suffix().toLower();
        if (s == "mfp" || isImageFile(p))
            images << p;
        else if (s == "mfv" || videoSuffixes().contains(s))
            videos << p;
        else if (audioSuffixes().contains(s))
            audios << p;
        else
            videos << p; // let the video importer probe unknown files
    }
    if (!images.isEmpty()) {
        setWorkspace(0);
        m_image->openFiles(images);
    }
    if (!audios.isEmpty() && videos.isEmpty()) {
        setWorkspace(2);
        m_audio->openFiles(audios);
    }
    if (!videos.isEmpty()) {
        setWorkspace(1);
        m_video->openFiles(videos + audios);
    }
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    for (Workspace* w : {static_cast<Workspace*>(m_image), static_cast<Workspace*>(m_video), static_cast<Workspace*>(m_audio)}) {
        if (!w->confirmClose()) {
            e->ignore();
            return;
        }
    }
    e->accept();
}

void MainWindow::showShortcuts()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Горячие клавиши"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* tree = new QTreeWidget;
    tree->setColumnCount(2);
    tree->setHeaderLabels({QStringLiteral("Действие"), QStringLiteral("Клавиши")});
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    std::function<void(QMenu*, QTreeWidgetItem*)> addMenu = [&](QMenu* m, QTreeWidgetItem* parent) {
        for (QAction* a : m->actions()) {
            if (a->isSeparator())
                continue;
            if (a->menu()) {
                addMenu(a->menu(), parent);
                continue;
            }
            QStringList keys;
            for (const QKeySequence& k : a->shortcuts())
                keys << k.toString(QKeySequence::NativeText);
            if (keys.isEmpty())
                continue;
            auto* item = new QTreeWidgetItem(parent, {a->text().remove('&'), keys.join(QStringLiteral(", "))});
            Q_UNUSED(item)
        }
    };
    for (QMenu* m : current()->menus() + m_common) {
        auto* top = new QTreeWidgetItem(tree, {m->title().remove('&'), QString()});
        addMenu(m, top);
        if (top->childCount() == 0)
            delete top;
    }
    auto* extra = new QTreeWidgetItem(tree, {QStringLiteral("Мышь и клавиатура"), QString()});
    for (const auto& [what, key] : std::initializer_list<std::pair<const char*, const char*>>{
             {"Временная рука (холст)", "Пробел + перетаскивание"},
             {"Масштаб", "Ctrl + колесо мыши"},
             {"Прокрутка холста / таймлайна", "Колесо, Shift + колесо"},
             {"Добавить к выделению / вычесть", "Shift / Alt (или Ctrl)"},
             {"Перемещение на 1 / 10 px", "Стрелки / Shift + стрелки (инструмент «Перемещение»)"}})
        new QTreeWidgetItem(extra, {QString::fromUtf8(what), QString::fromUtf8(key)});
    tree->expandAll();
    lay->addWidget(tree);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(box);
    dlg.resize(560, 640);
    dlg.exec();
}

void MainWindow::showAbout()
{
    QString ffmpegState;
    QString err;
    if (FFmpegJob::ffmpegAvailable(&err))
        ffmpegState = QStringLiteral("ffmpeg: %1").arg(FFmpegJob::ffmpegPath());
    else
        ffmpegState = err;
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("О программе"));
    box.setIconPixmap(icon(QStringLiteral("image")).pixmap(64, 64));
    box.setTextFormat(Qt::PlainText);
    box.setText(QStringLiteral("MediaForge %1").arg(QStringLiteral(MF_VERSION)));
    box.setInformativeText(QStringLiteral("Редактор изображений, видео и звука для Linux.\n\n"
                                          "Qt %1, FFmpeg (libav) %2\n%3\n\n"
                                          "Сборка включает FFmpeg с libx264, libvpx, LAME, Vorbis и Opus "
                                          "и распространяется на условиях GPL.")
                               .arg(QString::fromLatin1(qVersion()), QString::fromLatin1(av_version_info()), ffmpegState));
    box.exec();
}

} // namespace mf
