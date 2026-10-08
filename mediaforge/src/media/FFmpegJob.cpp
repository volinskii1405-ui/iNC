#include "media/FFmpegJob.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QStandardPaths>

namespace mf {

FFmpegJob::FFmpegJob(QObject* parent)
    : QObject(parent)
{
    connect(&m_proc, &QProcess::readyReadStandardOutput, this, &FFmpegJob::onStdout);
    connect(&m_proc, &QProcess::readyReadStandardError, this, [this] {
        m_stderr += m_proc.readAllStandardError();
        if (m_stderr.size() > 64 * 1024)
            m_stderr = m_stderr.right(32 * 1024);
    });
    connect(&m_proc, &QProcess::finished, this, &FFmpegJob::onFinished);
    connect(&m_proc, &QProcess::errorOccurred, this, &FFmpegJob::onError);
}

FFmpegJob::~FFmpegJob()
{
    if (isRunning()) {
        m_proc.kill();
        m_proc.waitForFinished(3000);
    }
}

QString FFmpegJob::ffmpegPath()
{
    const QString env = qEnvironmentVariable("MEDIAFORGE_FFMPEG");
    if (!env.isEmpty() && QFileInfo(env).isExecutable())
        return env;
    const QString local = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ffmpeg"));
    if (QFileInfo(local).isExecutable())
        return local;
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

bool FFmpegJob::ffmpegAvailable(QString* error)
{
    if (!ffmpegPath().isEmpty())
        return true;
    if (error)
        *error = QStringLiteral("Не найден исполняемый файл ffmpeg. Он должен лежать рядом с программой "
                                "(папка bin) — переустановите архив или задайте путь в MEDIAFORGE_FFMPEG.");
    return false;
}

bool FFmpegJob::hasEncoder(const QString& name)
{
    static QMutex mutex;
    static QString cache;
    static bool loaded = false;
    QMutexLocker lock(&mutex);
    if (!loaded) {
        loaded = true;
        const QString exe = ffmpegPath();
        if (!exe.isEmpty()) {
            QProcess p;
            p.start(exe, {"-hide_banner", "-encoders"});
            if (p.waitForFinished(10000))
                cache = QString::fromUtf8(p.readAllStandardOutput());
        }
    }
    return cache.contains(QLatin1Char(' ') + name + QLatin1Char(' '));
}

void FFmpegJob::start(const QStringList& args, double duration, const QString& outputToRemove)
{
    m_duration = duration;
    m_output = outputToRemove;
    m_cancelled = false;
    m_done = false;
    m_stdoutBuf.clear();
    m_stderr.clear();
    QString err;
    if (!ffmpegAvailable(&err)) {
        // Report asynchronously so callers can connect after start().
        QMetaObject::invokeMethod(this, [this, err] { finish(false, err); }, Qt::QueuedConnection);
        return;
    }
    m_proc.start(ffmpegPath(), args);
}

void FFmpegJob::cancel()
{
    if (!isRunning())
        return;
    m_cancelled = true;
    m_proc.kill();
}

void FFmpegJob::onStdout()
{
    if (m_done)
        return;
    m_stdoutBuf += m_proc.readAllStandardOutput();
    int nl;
    while ((nl = m_stdoutBuf.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuf.left(nl).trimmed();
        m_stdoutBuf.remove(0, nl + 1);
        if (line.startsWith("out_time_us=") || line.startsWith("out_time_ms=")) {
            bool ok = false;
            const qint64 us = line.mid(line.indexOf('=') + 1).toLongLong(&ok);
            if (ok && m_duration > 0)
                emit progress(qBound(0.0, us / 1e6 / m_duration, 1.0));
        } else if (line == "progress=end") {
            emit progress(1.0);
        }
    }
}

void FFmpegJob::onFinished(int exitCode, QProcess::ExitStatus status)
{
    m_stderr += m_proc.readAllStandardError();
    if (m_cancelled) {
        finish(false, QStringLiteral("Операция отменена."));
        return;
    }
    if (status != QProcess::NormalExit) {
        finish(false, QStringLiteral("Процесс ffmpeg аварийно завершился.\n%1")
                          .arg(friendlyFFmpegError(QString::fromUtf8(m_stderr), exitCode)));
        return;
    }
    if (exitCode != 0) {
        finish(false, friendlyFFmpegError(QString::fromUtf8(m_stderr), exitCode));
        return;
    }
    finish(true, QString());
}

void FFmpegJob::onError(QProcess::ProcessError err)
{
    if (err == QProcess::FailedToStart)
        finish(false, QStringLiteral("Не удалось запустить ffmpeg (%1): %2").arg(ffmpegPath(), m_proc.errorString()));
}

void FFmpegJob::finish(bool ok, const QString& error)
{
    if (m_done)
        return;
    m_done = true;
    // Late stdout from the finished process must not produce progress after "finished".
    disconnect(&m_proc, &QProcess::readyReadStandardOutput, this, &FFmpegJob::onStdout);
    if (!ok && !m_output.isEmpty()) {
        QFileInfo fi(m_output);
        if (fi.isDir())
            QDir(m_output).removeRecursively();
        else if (fi.exists())
            QFile::remove(m_output);
    }
    emit finished(ok, error);
}

bool FFmpegJob::runBlocking(const QStringList& args, double duration, QString* error,
                            const std::function<void(double)>& onProgress)
{
    FFmpegJob job;
    QEventLoop loop;
    bool result = false;
    QObject::connect(&job, &FFmpegJob::progress, [&](double p) {
        if (onProgress)
            onProgress(p);
    });
    QObject::connect(&job, &FFmpegJob::finished, [&](bool ok, const QString& err) {
        result = ok;
        if (error)
            *error = err;
        loop.quit();
    });
    job.start(args, duration);
    loop.exec();
    return result;
}

QString friendlyFFmpegError(const QString& stderrText, int exitCode)
{
    const QString s = stderrText;
    auto has = [&](const char* needle) { return s.contains(QLatin1String(needle), Qt::CaseInsensitive); };
    QString hint;
    if (has("No space left on device"))
        hint = QStringLiteral("Недостаточно места на диске. Освободите место или выберите другую папку.");
    else if (has("Permission denied"))
        hint = QStringLiteral("Нет прав на запись или чтение файла. Выберите другую папку.");
    else if (has("Unknown encoder") || has("Encoder not found") || has("encoder not found"))
        hint = QStringLiteral("В сборке ffmpeg нет нужного кодека.");
    else if (has("Decoder") && has("not found"))
        hint = QStringLiteral("Нет декодера для одного из входных файлов (неподдерживаемый кодек).");
    else if (has("Invalid data found when processing input") || has("moov atom not found"))
        hint = QStringLiteral("Один из входных файлов повреждён или не является медиафайлом.");
    else if (has("No such file or directory"))
        hint = QStringLiteral("Файл или папка не найдены (возможно, исходник переместили или удалили).");
    else if (has("Stream specifier") || has("matches no streams"))
        hint = QStringLiteral("В файле нет нужной дорожки (например, звука).");
    QStringList lines = s.split('\n', Qt::SkipEmptyParts);
    while (lines.size() > 6)
        lines.removeFirst();
    const QString tail = lines.join('\n').trimmed();
    QString msg = hint.isEmpty() ? QStringLiteral("ffmpeg завершился с ошибкой (код %1).").arg(exitCode) : hint;
    if (!tail.isEmpty())
        msg += QStringLiteral("\n\nПодробности:\n") + tail;
    return msg;
}

} // namespace mf
