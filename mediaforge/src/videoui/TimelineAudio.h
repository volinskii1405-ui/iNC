#pragma once

#include "audioui/PcmPlayer.h"
#include "media/Timeline.h"

#include <QObject>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>

namespace mf {

class FFmpegJob;

// Preview sound for the video timeline. The mix is rendered by the same
// ffmpeg filter graph as the export (speed, fades, volume, extra tracks),
// so what you hear while editing matches the exported file.
class TimelineAudio : public QObject {
    Q_OBJECT
public:
    explicit TimelineAudio(std::function<double()> clock, QObject* parent = nullptr);
    ~TimelineAudio() override;

    void invalidate(const TimelineState& st);
    void play(double from);
    void stop();
    bool isReady() const { return !m_file.isEmpty(); }

signals:
    void status(const QString& message);

private:
    void render();
    void startPlayer(double from);

    std::function<double()> m_clock;
    QTemporaryDir m_dir;
    PcmPlayer m_player;
    QTimer m_debounce;
    TimelineState m_state;
    QPointer<FFmpegJob> m_job;
    QString m_file;
    QString m_rendering;
    int m_counter = 0;
    bool m_wantPlay = false;
    bool m_dirty = false;
};

} // namespace mf
