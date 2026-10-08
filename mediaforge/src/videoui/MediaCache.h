#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVector>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace mf {

// Thumbnails and waveforms for the timeline, produced on a background thread.
// Lookups never block: a miss queues work and `updated` fires when it's ready.
class MediaCache : public QObject {
    Q_OBJECT
public:
    explicit MediaCache(QObject* parent = nullptr);
    ~MediaCache() override;

    static constexpr int kPeaksPerSecond = 100;

    QImage thumbnail(const QString& path, double t, int height, bool still);
    // Min/max pairs, kPeaksPerSecond buckets per second; empty while loading.
    QVector<float> peaks(const QString& path);

signals:
    void updated();

private:
    struct Job {
        QString key;
        QString path;
        double t;
        int height;
        bool still;
        bool peaks;
    };
    void workerLoop();

    std::thread m_worker;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_quit = false;
    std::deque<Job> m_jobs;
    QSet<QString> m_pending;
    QHash<QString, QImage> m_thumbs;
    QHash<QString, QVector<float>> m_peaks;
    QTimer m_notify;
};

} // namespace mf
