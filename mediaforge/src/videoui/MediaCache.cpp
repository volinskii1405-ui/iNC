#include "videoui/MediaCache.h"

#include "image/ImageIO.h"
#include "media/Audio.h"
#include "media/VideoDecoder.h"

#include <QImageReader>
#include <QMetaObject>

#include <cmath>
#include <memory>
#include <unordered_map>

namespace mf {

MediaCache::MediaCache(QObject* parent)
    : QObject(parent)
{
    // Coalesce many finished jobs into one repaint.
    m_notify.setSingleShot(true);
    m_notify.setInterval(50);
    connect(&m_notify, &QTimer::timeout, this, &MediaCache::updated);
    m_worker = std::thread([this] { workerLoop(); });
}

MediaCache::~MediaCache()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_quit = true;
        m_jobs.clear();
    }
    m_cv.notify_all();
    if (m_worker.joinable())
        m_worker.join();
}

QImage MediaCache::thumbnail(const QString& path, double t, int height, bool still)
{
    // Quantize so that nearby requests share a decode.
    const double q = still ? 0.0 : std::round(t * 4.0) / 4.0;
    const QString key = QStringLiteral("%1|%2|%3").arg(path).arg(q, 0, 'f', 2).arg(height);
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_thumbs.find(key);
    if (it != m_thumbs.end())
        return it.value();
    if (!m_pending.contains(key)) {
        m_pending.insert(key);
        m_jobs.push_front(Job{key, path, q, height, still, false});
        if (m_jobs.size() > 300) {
            m_pending.remove(m_jobs.back().key);
            m_jobs.pop_back();
        }
        m_cv.notify_one();
    }
    return QImage();
}

QVector<float> MediaCache::peaks(const QString& path)
{
    const QString key = QStringLiteral("peaks|") + path;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_peaks.find(path);
    if (it != m_peaks.end())
        return it.value();
    if (!m_pending.contains(key)) {
        m_pending.insert(key);
        m_jobs.push_back(Job{key, path, 0, 0, false, true});
        m_cv.notify_one();
    }
    return {};
}

void MediaCache::workerLoop()
{
    std::unordered_map<std::string, std::unique_ptr<VideoDecoder>> decoders;
    std::unique_lock<std::mutex> lock(m_mutex);
    while (true) {
        m_cv.wait(lock, [this] { return m_quit || !m_jobs.empty(); });
        if (m_quit)
            return;
        const Job job = m_jobs.front();
        m_jobs.pop_front();
        lock.unlock();

        QImage thumb;
        QVector<float> peaks;
        if (job.peaks) {
            PcmBuffer pcm;
            QString err;
            if (decodeAudio(job.path, &pcm, &err, {}, 8000, 1))
                peaks = computePeaks(pcm, 8000 / kPeaksPerSecond);
            if (peaks.isEmpty())
                peaks = {0.f, 0.f}; // mark as done so we don't retry forever
        } else if (job.still) {
            QImageReader reader(job.path);
            reader.setAutoTransform(true);
            const QSize s = reader.size();
            if (s.isValid() && s.height() > 0)
                reader.setScaledSize(QSize(std::max(1, s.width() * job.height / s.height()), job.height));
            thumb = reader.read();
        } else {
            auto& dec = decoders[job.path.toStdString()];
            if (!dec) {
                dec = std::make_unique<VideoDecoder>();
                QString err;
                if (!dec->open(job.path, &err, 2))
                    dec.reset();
            }
            if (dec)
                thumb = dec->frameAt(job.t, QSize(job.height * 4, job.height));
            if (decoders.size() > 8)
                decoders.clear();
        }

        lock.lock();
        if (job.peaks)
            m_peaks.insert(job.path, peaks);
        else
            m_thumbs.insert(job.key, thumb.isNull() ? QImage(1, 1, QImage::Format_RGB32) : thumb);
        m_pending.remove(job.key);
        if (m_thumbs.size() > 3000)
            m_thumbs.clear();
        QMetaObject::invokeMethod(&m_notify, qOverload<>(&QTimer::start), Qt::QueuedConnection);
    }
}

} // namespace mf
