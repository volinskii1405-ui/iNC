#pragma once

#include "media/MediaInfo.h"

#include <QObject>
#include <QSize>
#include <QUndoStack>
#include <QVector>

namespace mf {

enum class ClipKind { Video, Image, Audio, Overlay };
enum class Track { Main, Overlay, Audio };

struct Clip {
    quint64 id = 0;
    ClipKind kind = ClipKind::Video;
    QString path;
    MediaInfo info;
    double in = 0.0;  // source range in seconds; images use [0, duration)
    double out = 0.0;
    double speed = 1.0;
    double volume = 1.0;
    bool muted = false;
    double fadeIn = 0.0;
    double fadeOut = 0.0;
    double start = 0.0; // timeline position for overlay and audio clips
    // Overlay placement as fractions of the output frame.
    double x = 0.05;
    double y = 0.05;
    double width = 0.3;
    double opacity = 1.0;

    double length() const { return (out - in) / speed; }
    double end() const { return start + length(); }
    double sourceTime(double local) const { return in + local * speed; }
    bool carriesAudio() const { return kind == ClipKind::Audio || (kind == ClipKind::Video && info.hasAudio); }
    bool isStill() const { return kind == ClipKind::Image || kind == ClipKind::Overlay; }
};

struct TimelineState {
    QVector<Clip> main;     // video and image clips played back to back
    QVector<Clip> overlays; // images drawn over the main track
    QVector<Clip> audio;    // extra audio clips mixed in
    QSize resolution = QSize(1920, 1080);
    double fps = 30.0;
    bool formatFromFirstClip = true;
    bool muteOriginal = false;

    QVector<Clip>& track(Track t) { return t == Track::Main ? main : (t == Track::Overlay ? overlays : audio); }
    const QVector<Clip>& track(Track t) const
    {
        return t == Track::Main ? main : (t == Track::Overlay ? overlays : audio);
    }
    double mainLength() const;
    double duration() const;
    double mainStart(int index) const;
    int mainIndexAt(double t, double* clipStart = nullptr) const;
    int indexOfId(Track t, quint64 id) const;
};

constexpr double kMinClipLength = 0.04;
constexpr double kDefaultImageDuration = 3.0;

// Keeps only [a, b) of the timeline and shifts it to start at zero.
TimelineState subRange(const TimelineState& s, double a, double b);

class Timeline : public QObject {
    Q_OBJECT
public:
    explicit Timeline(QObject* parent = nullptr);

    const TimelineState& state() const { return m_state; }
    QUndoStack* undoStack() { return &m_undo; }

    void reset(const TimelineState& s = TimelineState());
    // Commits a modified copy of the state as one undo step.
    void commit(const TimelineState& s, const QString& text, int mergeId = -1);
    // Used by undo commands and by interactive drags (which commit at the end).
    void setStateSilently(const TimelineState& s);

    Clip makeClip(const QString& path, const MediaInfo& info, ClipKind kind);
    quint64 addClip(Track track, Clip clip, int index = -1);
    void removeClip(Track track, int index);
    void updateClip(Track track, int index, const Clip& clip, const QString& text, int mergeId = -1);
    void moveMainClip(int from, int to);
    bool split(Track track, int index, double t);
    void splitMainAt(double t);
    void removeRange(double a, double b);
    void setMuteOriginal(bool mute);
    void setFormat(QSize resolution, double fps);

signals:
    void changed();

private:
    void adoptFormat(TimelineState& s, const Clip& c);

    TimelineState m_state;
    QUndoStack m_undo;
    quint64 m_nextId = 1;
};

// Saves/loads the edit list as JSON (.mfv).
bool saveTimeline(const TimelineState& s, const QString& path, QString* error);
bool loadTimeline(const QString& path, TimelineState* s, QStringList* warnings, QString* error);

} // namespace mf
