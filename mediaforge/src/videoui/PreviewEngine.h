#pragma once

#include "media/Timeline.h"

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QTimer>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace mf {

// Renders preview frames on a worker thread. Scrubbing requests are
// "latest wins"; during playback the worker renders ahead into a small queue
// and the UI thread shows whichever frame matches the playback clock.
class PreviewEngine : public QObject {
    Q_OBJECT
public:
    explicit PreviewEngine(QObject* parent = nullptr);
    ~PreviewEngine() override;

    void requestFrame(const TimelineState& st, double t, QSize size);
    void play(const TimelineState& st, double from, QSize size);
    void stop();
    bool isPlaying() const { return m_playing; }
    double clock() const;

signals:
    void frameReady(const QImage& frame, double t);
    void positionChanged(double t); // during playback
    void playbackFinished();

private:
    struct Frame {
        QImage image;
        double t;
        quint64 gen;
    };
    void workerLoop();
    void tick();

    std::thread m_worker;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_quit = false;

    // single-frame request
    bool m_hasRequest = false;
    TimelineState m_reqState;
    double m_reqT = 0;
    QSize m_reqSize;

    // playback
    std::atomic<bool> m_producing{false};
    std::atomic<quint64> m_gen{0};
    TimelineState m_playState;
    QSize m_playSize;
    double m_nextT = 0;
    std::deque<Frame> m_queue;
    std::atomic<double> m_clockAtomic{0.0};

    bool m_playing = false;
    double m_playFrom = 0;
    double m_playEnd = 0;
    QElapsedTimer m_elapsed;
    QTimer m_tick;
};

} // namespace mf
