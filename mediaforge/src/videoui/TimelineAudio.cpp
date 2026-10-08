#include "videoui/TimelineAudio.h"

#include "media/ExportBuilder.h"
#include "media/FFmpegJob.h"

#include <QFile>

namespace mf {

TimelineAudio::TimelineAudio(std::function<double()> clock, QObject* parent)
    : QObject(parent), m_clock(std::move(clock))
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(500);
    connect(&m_debounce, &QTimer::timeout, this, &TimelineAudio::render);
}

TimelineAudio::~TimelineAudio()
{
    m_player.stop();
    if (m_job)
        m_job->cancel();
}

void TimelineAudio::invalidate(const TimelineState& st)
{
    m_state = st;
    m_dirty = true;
    m_debounce.start();
}

void TimelineAudio::render()
{
    if (m_job) {
        // Let the running render finish; its completion triggers another pass.
        return;
    }
    m_dirty = false;
    const double dur = m_state.duration();
    if (dur < kMinClipLength) {
        m_file.clear();
        return;
    }
    VideoExportSettings s;
    s.format = VideoFormat::AudioRaw;
    m_rendering = m_dir.filePath(QStringLiteral("mix_%1.pcm").arg(m_counter++ % 2));
    s.output = m_rendering;
    const ExportPlan plan = buildVideoExport(m_state, s, m_dir.path());
    if (!plan.ok())
        return;
    auto* job = new FFmpegJob(this);
    m_job = job;
    connect(job, &FFmpegJob::finished, this, [this, job](bool ok, const QString& err) {
        job->deleteLater();
        m_job = nullptr;
        if (ok) {
            const bool wasPlaying = m_player.isActive() || m_wantPlay;
            m_file = m_rendering;
            if (wasPlaying && m_wantPlay)
                startPlayer(m_clock());
        } else {
            emit status(QStringLiteral("Не удалось подготовить звук для просмотра: %1").arg(err.section('\n', 0, 0)));
        }
        if (m_dirty)
            m_debounce.start();
    });
    job->start(plan.args, plan.duration, m_rendering);
}

void TimelineAudio::play(double from)
{
    m_wantPlay = true;
    if (m_debounce.isActive()) {
        m_debounce.stop();
        render();
    }
    if (!m_file.isEmpty())
        startPlayer(from);
    else if (m_job)
        emit status(QStringLiteral("Звук готовится…"));
}

void TimelineAudio::startPlayer(double from)
{
    auto* f = new QFile(m_file);
    if (!f->open(QIODevice::ReadOnly)) {
        delete f;
        return;
    }
    const qint64 bytesPerSecond = 48000 * 2 * 2;
    qint64 offset = qint64(from * bytesPerSecond);
    offset -= offset % 4;
    f->seek(std::min(offset, f->size()));
    if (!m_player.start(f, 48000, 2))
        emit status(m_player.lastError());
}

void TimelineAudio::stop()
{
    m_wantPlay = false;
    m_player.stop();
}

} // namespace mf
