#pragma once

#include <QList>
#include <QMainWindow>
#include <QMenu>

namespace mf {

// One tab of the main window (image, video, audio). Each workspace owns its
// menus; the main window shows only the active workspace's menus so that
// shortcuts like Ctrl+Z always act on what the user is looking at.
class Workspace : public QMainWindow {
    Q_OBJECT
public:
    explicit Workspace(QWidget* parent = nullptr) : QMainWindow(parent) { setWindowFlags(Qt::Widget); }
    virtual QList<QMenu*> menus() const = 0;
    // Returns false if the user cancelled closing (unsaved changes).
    virtual bool confirmClose() { return true; }
    virtual void openFiles(const QStringList& paths) = 0;

signals:
    void titleChanged();
};

} // namespace mf
