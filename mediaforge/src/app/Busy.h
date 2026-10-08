#pragma once

#include <QEventLoop>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QTimer>
#include <QtConcurrent>

#include <functional>

namespace mf {

// Runs fn on a worker thread while the event loop keeps the UI painting.
// A modal "please wait" dialog appears only if the job takes a while.
template <typename T>
T runBusy(QWidget* parent, const QString& label, std::function<T()> fn)
{
    QFutureWatcher<T> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<T>::finished, &loop, &QEventLoop::quit);
    QProgressDialog dlg(label, QString(), 0, 0, parent);
    dlg.setWindowModality(Qt::ApplicationModal);
    dlg.setMinimumDuration(0);
    dlg.setCancelButton(nullptr);
    dlg.setAutoClose(false);
    dlg.hide();
    QTimer showTimer;
    showTimer.setSingleShot(true);
    QObject::connect(&showTimer, &QTimer::timeout, &dlg, &QProgressDialog::show);
    showTimer.start(300);
    watcher.setFuture(QtConcurrent::run(fn));
    if (!watcher.isFinished())
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    showTimer.stop();
    dlg.hide();
    return watcher.result();
}

} // namespace mf
