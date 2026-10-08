#include "videoui/PreviewEngine.h"

#include "videoui/FrameRenderer.h"

#include <QMetaObject>

namespace mf {

namespace {
constexpr size_t kQueueSize = 6;
}

PreviewEngine::PreviewEngine(QObject* parent)
    : QObject(parent)
{
    m_tick.setInterval(8);
    m_tick.setTimerType(Qt::PreciseTimer);
    connect(&m_tick, &QTimer::timeout, this, &PreviewEngine::tick);
    m_worker = std::thread([this] { workerLoop(); });
}

PreviewEngine::~PreviewEngine()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_quit = true;
    }
    m_cv.notify_all();
    if (m_worker.joinable())
        m_worker.join();
}

void PreviewEngine::requestFrame(const TimelineState& st, double t, QSize size)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_hasRequest = true;
        m_reqState = st;
        m_reqT = t;
        m_reqSize = size;
    }
    m_cv.notify_all();
}

void PreviewEngine::play(const TimelineState& st, double from, QSize size)
{
    stop();
    const double dur = st.duration();
    if (dur <= 0)
        return;
    if (from >= dur - 1e-3)
        from = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_playState = st;
        m_playSize = size;
        m_nextT = from;
        m_queue.clear();
        ++m_gen;
        m_producing = true;
    }
    m_clockAtomic = from;
    m_playFrom = from;
    m_playEnd = dur;
    m_playing = true;
    m_elapsed.start();
    m_tick.start();
    m_cv.notify_all();
}

void PreviewEngine::stop()
{
    if (!m_playing)
        return;
    m_playing = false;
    m_tick.stop();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_producing = false;
        ++m_gen;
        m_queue.clear();
    }
    m_cv.notify_all();
}

double PreviewEngine::clock() const
{
    return m_playing ? m_playFrom + m_elapsed.nsecsElapsed() / 1e9 : m_clockAtomic.load();
}

void PreviewEngine::tick()
{
    if (!m_playing)
        return;
    const double now = clock();
    m_clockAtomic = now;
    Frame best;
    bool have = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        while (!m_queue.empty() && m_queue.front().t <= now) {
            best = std::move(m_queue.front());
            have = true;
            m_queue.pop_front();
        }
    }
    m_cv.notify_all();
    if (have)
        emit frameReady(best.image, best.t);
    emit positionChanged(std::min(now, m_playEnd));
    if (now >= m_playEnd) {
        stop();
        emit playbackFinished();
    }
}

void PreviewEngine::workerLoop()
{
    FrameRenderer renderer;
    std::unique_lock<std::mutex> lock(m_mutex);
    while (!m_quit) {
        m_cv.wait(lock, [this] {
            return m_quit || m_hasRequest || (m_producing && m_queue.size() < kQueueSize);
        });
        if (m_quit)
            break;
        if (m_hasRequest && !m_producing) {
            const TimelineState st = m_reqState;
            const double t = m_reqT;
            const QSize size = m_reqSize;
            m_hasRequest = false;
            lock.unlock();
            const QImage img = renderer.render(st, t, size);
            QMetaObject::invokeMethod(this, [this, img, t] { emit frameReady(img, t); }, Qt::QueuedConnection);
            lock.lock();
            continue;
        }
        if (m_producing && m_queue.size() < kQueueSize) {
            const quint64 gen = m_gen;
            const double fps = std::max(1.0, m_playState.fps);
            double t = m_nextT;
            // Fell behind the clock: skip ahead instead of showing stale frames.
            const double clockNow = m_clockAtomic.load();
            if (t < clockNow - 2.0 / fps)
                t = clockNow + 1.0 / fps;
            if (t >= m_playState.duration()) {
                m_producing = false;
                continue;
            }
            m_nextT = t + 1.0 / fps;
            const TimelineState st = m_playState;
            const QSize size = m_playSize;
            lock.unlock();
            QImage img = renderer.render(st, t, size);
            lock.lock();
            if (gen == m_gen && m_producing)
                m_queue.push_back(Frame{std::move(img), t, gen});
            continue;
        }
        m_hasRequest = false; // request arrived during playback: playback frames win
    }
}

} // namespace mf
