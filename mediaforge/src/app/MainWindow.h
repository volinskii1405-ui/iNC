#pragma once

#include <QMainWindow>

class QTabWidget;

namespace mf {

class ImageEditor;
class VideoEditor;
class AudioEditor;
class Workspace;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

    void openFiles(const QStringList& paths);
    void setWorkspace(int index);
    int workspaceIndex() const;
    ImageEditor* imageEditor() const { return m_image; }
    VideoEditor* videoEditor() const { return m_video; }
    AudioEditor* audioEditor() const { return m_audio; }

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void rebuildMenus();
    void updateTitle();
    void showShortcuts();
    void showAbout();
    Workspace* current() const;

    QTabWidget* m_tabs;
    ImageEditor* m_image;
    VideoEditor* m_video;
    AudioEditor* m_audio;
    QList<QMenu*> m_common;
};

} // namespace mf
