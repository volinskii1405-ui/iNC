#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

#include <functional>

namespace mf {

// Runs the bundled ffmpeg binary asynchronously and reports progress parsed
// from "-progress pipe:1". On failure or cancel, the partial output is removed.
class FFmpegJob : public QObject {
    Q_OBJECT
public:
    explicit FFmpegJob(QObject* parent = nullptr);
    ~FFmpegJob() override;

    static QString ffmpegPath();
    static bool ffmpegAvailable(QString* error = nullptr);
    static bool hasEncoder(const QString& name);

    // outputToRemove: file or directory deleted if the job fails or is cancelled.
    void start(const QStringList& args, double duration, const QString& outputToRemove = QString());
    void cancel();
    bool isRunning() const { return m_proc.state() != QProcess::NotRunning; }

    // Convenience for tests and short jobs: runs a local event loop.
    static bool runBlocking(const QStringList& args, double duration, QString* error,
                            const std::function<void(double)>& onProgress = {});

signals:
    void progress(double fraction);
    void finished(bool ok, const QString& error);

private:
    void onStdout();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onError(QProcess::ProcessError err);
    void finish(bool ok, const QString& error);

    QProcess m_proc;
    QByteArray m_stdoutBuf;
    QByteArray m_stderr;
    double m_duration = 0.0;
    QString m_output;
    bool m_cancelled = false;
    bool m_done = false;
};

QString friendlyFFmpegError(const QString& stderrText, int exitCode);

} // namespace mf
