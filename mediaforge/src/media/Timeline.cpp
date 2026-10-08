#include "media/Timeline.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUndoCommand>

#include <algorithm>
#include <cmath>

namespace mf {

double TimelineState::mainLength() const
{
    double t = 0.0;
    for (const Clip& c : main)
        t += c.length();
    return t;
}

double TimelineState::duration() const
{
    double d = mainLength();
    for (const Clip& c : overlays)
        d = std::max(d, c.end());
    for (const Clip& c : audio)
        d = std::max(d, c.end());
    return d;
}

double TimelineState::mainStart(int index) const
{
    double t = 0.0;
    for (int i = 0; i < index && i < main.size(); ++i)
        t += main[i].length();
    return t;
}

int TimelineState::mainIndexAt(double t, double* clipStart) const
{
    double s = 0.0;
    for (int i = 0; i < main.size(); ++i) {
        const double len = main[i].length();
        if (t >= s && t < s + len) {
            if (clipStart)
                *clipStart = s;
            return i;
        }
        s += len;
    }
    return -1;
}

int TimelineState::indexOfId(Track t, quint64 id) const
{
    const auto& v = track(t);
    for (int i = 0; i < v.size(); ++i)
        if (v[i].id == id)
            return i;
    return -1;
}

namespace {

// Portion [a, b) of the main track, as a new list of clips.
QVector<Clip> mainSlice(const QVector<Clip>& main, double a, double b)
{
    QVector<Clip> out;
    double s = 0.0;
    for (const Clip& c : main) {
        const double len = c.length();
        const double e = s + len;
        const double ov0 = std::max(a, s), ov1 = std::min(b, e);
        if (ov1 - ov0 > 1e-6) {
            Clip n = c;
            n.in = c.sourceTime(ov0 - s);
            n.out = c.sourceTime(ov1 - s);
            if (ov0 > s + 1e-6)
                n.fadeIn = 0.0;
            if (ov1 < e - 1e-6)
                n.fadeOut = 0.0;
            out.push_back(n);
        }
        s = e;
    }
    return out;
}

// Part of a positioned clip inside [a, b), shifted so that a becomes zero.
bool positionedSlice(const Clip& c, double a, double b, Clip* out)
{
    const double ov0 = std::max(a, c.start), ov1 = std::min(b, c.end());
    if (ov1 - ov0 <= 1e-6)
        return false;
    Clip n = c;
    n.in = c.sourceTime(ov0 - c.start);
    n.out = c.sourceTime(ov1 - c.start);
    n.start = ov0 - a;
    if (ov0 > c.start + 1e-6)
        n.fadeIn = 0.0;
    if (ov1 < c.end() - 1e-6)
        n.fadeOut = 0.0;
    *out = n;
    return true;
}

class TimelineCommand : public QUndoCommand {
public:
    TimelineCommand(Timeline* tl, TimelineState before, TimelineState after, const QString& text, int mergeId)
        : QUndoCommand(text), m_tl(tl), m_before(std::move(before)), m_after(std::move(after)), m_mergeId(mergeId)
    {
    }
    void undo() override { m_tl->setStateSilently(m_before); }
    void redo() override { m_tl->setStateSilently(m_after); }
    int id() const override { return m_mergeId; }
    bool mergeWith(const QUndoCommand* other) override
    {
        if (m_mergeId < 0 || other->id() != m_mergeId)
            return false;
        m_after = static_cast<const TimelineCommand*>(other)->m_after;
        return true;
    }

private:
    Timeline* m_tl;
    TimelineState m_before;
    TimelineState m_after;
    int m_mergeId;
};

} // namespace

TimelineState subRange(const TimelineState& s, double a, double b)
{
    TimelineState out = s;
    out.main = mainSlice(s.main, a, b);
    out.overlays.clear();
    out.audio.clear();
    Clip n;
    for (const Clip& c : s.overlays)
        if (positionedSlice(c, a, b, &n))
            out.overlays.push_back(n);
    for (const Clip& c : s.audio)
        if (positionedSlice(c, a, b, &n))
            out.audio.push_back(n);
    return out;
}

Timeline::Timeline(QObject* parent)
    : QObject(parent)
{
    m_undo.setUndoLimit(200);
}

void Timeline::reset(const TimelineState& s)
{
    m_undo.clear();
    m_state = s;
    for (Track t : {Track::Main, Track::Overlay, Track::Audio})
        for (Clip& c : m_state.track(t))
            if (c.id == 0 || c.id >= m_nextId)
                c.id = m_nextId++;
    m_undo.setClean();
    emit changed();
}

void Timeline::commit(const TimelineState& s, const QString& text, int mergeId)
{
    m_undo.push(new TimelineCommand(this, m_state, s, text, mergeId));
}

void Timeline::commitFrom(const TimelineState& before, const QString& text)
{
    m_undo.push(new TimelineCommand(this, before, m_state, text, -1));
}

void Timeline::setStateSilently(const TimelineState& s)
{
    m_state = s;
    emit changed();
}

Clip Timeline::makeClip(const QString& path, const MediaInfo& info, ClipKind kind)
{
    Clip c;
    c.kind = kind;
    c.path = path;
    c.info = info;
    c.in = 0.0;
    c.out = (kind == ClipKind::Image || kind == ClipKind::Overlay) ? kDefaultImageDuration
                                                                   : std::max(kMinClipLength, info.duration);
    return c;
}

void Timeline::adoptFormat(TimelineState& s, const Clip& c)
{
    if (!s.formatFromFirstClip || c.info.width <= 0 || c.info.height <= 0)
        return;
    s.resolution = QSize(c.info.width & ~1, c.info.height & ~1);
    if (c.kind == ClipKind::Video && c.info.fps > 0)
        s.fps = std::clamp(std::round(c.info.fps * 1000.0) / 1000.0, 1.0, 120.0);
    s.formatFromFirstClip = false;
}

quint64 Timeline::addClip(Track track, Clip clip, int index)
{
    TimelineState s = m_state;
    clip.id = m_nextId++;
    QVector<Clip>& v = s.track(track);
    if (track == Track::Main && s.main.isEmpty())
        adoptFormat(s, clip);
    if (index < 0 || index > v.size())
        v.push_back(clip);
    else
        v.insert(index, clip);
    commit(s, QStringLiteral("Добавление клипа"));
    return clip.id;
}

void Timeline::removeClip(Track track, int index)
{
    TimelineState s = m_state;
    QVector<Clip>& v = s.track(track);
    if (index < 0 || index >= v.size())
        return;
    v.removeAt(index);
    commit(s, QStringLiteral("Удаление клипа"));
}

void Timeline::updateClip(Track track, int index, const Clip& clip, const QString& text, int mergeId)
{
    TimelineState s = m_state;
    QVector<Clip>& v = s.track(track);
    if (index < 0 || index >= v.size())
        return;
    Clip c = clip;
    c.speed = std::clamp(c.speed, 0.25, 4.0);
    c.volume = std::clamp(c.volume, 0.0, 4.0);
    c.opacity = std::clamp(c.opacity, 0.0, 1.0);
    c.start = std::max(0.0, c.start);
    if (!c.isStill()) {
        c.in = std::clamp(c.in, 0.0, std::max(0.0, c.info.duration - kMinClipLength));
        c.out = std::clamp(c.out, c.in + kMinClipLength, std::max(c.in + kMinClipLength, c.info.duration));
    } else {
        c.in = 0.0;
        c.out = std::max(kMinClipLength, c.out);
    }
    c.fadeIn = std::clamp(c.fadeIn, 0.0, c.length());
    c.fadeOut = std::clamp(c.fadeOut, 0.0, c.length());
    v[index] = c;
    commit(s, text, mergeId);
}

void Timeline::moveMainClip(int from, int to)
{
    TimelineState s = m_state;
    if (from < 0 || from >= s.main.size() || to < 0 || to >= s.main.size() || from == to)
        return;
    s.main.move(from, to);
    commit(s, QStringLiteral("Перемещение клипа"));
}

bool Timeline::split(Track track, int index, double t)
{
    TimelineState s = m_state;
    QVector<Clip>& v = s.track(track);
    if (index < 0 || index >= v.size())
        return false;
    const Clip c = v[index];
    const double start = track == Track::Main ? s.mainStart(index) : c.start;
    const double local = t - start;
    if (local < kMinClipLength || local > c.length() - kMinClipLength)
        return false;
    Clip first = c, second = c;
    first.out = c.sourceTime(local);
    second.in = first.out;
    first.fadeOut = 0.0;
    second.fadeIn = 0.0;
    second.start = t;
    second.id = m_nextId++;
    if (c.isStill()) {
        // Stills have no source timing: both halves just get their own duration.
        first.in = second.in = 0.0;
        first.out = local;
        second.out = c.length() - local;
    }
    v[index] = first;
    v.insert(index + 1, second);
    commit(s, QStringLiteral("Разрезание клипа"));
    return true;
}

void Timeline::splitMainAt(double t)
{
    double clipStart = 0;
    const int i = m_state.mainIndexAt(t, &clipStart);
    if (i >= 0)
        split(Track::Main, i, t);
}

void Timeline::removeRange(double a, double b)
{
    if (b < a)
        std::swap(a, b);
    if (b - a < 1e-6)
        return;
    TimelineState s = m_state;
    const double cut = b - a;
    QVector<Clip> main = mainSlice(m_state.main, 0.0, a);
    for (const Clip& c : mainSlice(m_state.main, b, 1e12))
        main.push_back(c);
    // Pieces created by the cut need their own ids.
    QSet<quint64> seen;
    for (Clip& c : main) {
        if (seen.contains(c.id))
            c.id = m_nextId++;
        seen.insert(c.id);
    }
    s.main = main;

    for (Track t : {Track::Overlay, Track::Audio}) {
        QVector<Clip> out;
        for (const Clip& c : m_state.track(t)) {
            if (c.end() <= a + 1e-9) {
                out.push_back(c);
                continue;
            }
            if (c.start >= b - 1e-9) {
                Clip n = c;
                n.start -= cut;
                out.push_back(n);
                continue;
            }
            Clip piece;
            bool first = true;
            if (positionedSlice(c, c.start, a, &piece)) {
                piece.start = c.start;
                out.push_back(piece);
                first = false;
            }
            if (positionedSlice(c, b, c.end(), &piece)) {
                piece.start = a;
                if (!first)
                    piece.id = m_nextId++;
                out.push_back(piece);
            }
        }
        s.track(t) = out;
    }
    commit(s, QStringLiteral("Удаление фрагмента"));
}

void Timeline::setMuteOriginal(bool mute)
{
    if (m_state.muteOriginal == mute)
        return;
    TimelineState s = m_state;
    s.muteOriginal = mute;
    commit(s, mute ? QStringLiteral("Заглушить исходный звук") : QStringLiteral("Вернуть исходный звук"));
}

void Timeline::setFormat(QSize resolution, double fps)
{
    TimelineState s = m_state;
    s.resolution = QSize(std::max(2, resolution.width() & ~1), std::max(2, resolution.height() & ~1));
    s.fps = std::clamp(fps, 1.0, 120.0);
    s.formatFromFirstClip = false;
    commit(s, QStringLiteral("Параметры проекта"));
}

namespace {

const char* kindId(ClipKind k)
{
    switch (k) {
    case ClipKind::Video: return "video";
    case ClipKind::Image: return "image";
    case ClipKind::Audio: return "audio";
    case ClipKind::Overlay: return "overlay";
    }
    return "video";
}

ClipKind kindFromId(const QString& s)
{
    if (s == "image")
        return ClipKind::Image;
    if (s == "audio")
        return ClipKind::Audio;
    if (s == "overlay")
        return ClipKind::Overlay;
    return ClipKind::Video;
}

QJsonObject clipToJson(const Clip& c)
{
    QJsonObject o;
    o["kind"] = kindId(c.kind);
    o["path"] = c.path;
    o["in"] = c.in;
    o["out"] = c.out;
    o["speed"] = c.speed;
    o["volume"] = c.volume;
    o["muted"] = c.muted;
    o["fadeIn"] = c.fadeIn;
    o["fadeOut"] = c.fadeOut;
    o["start"] = c.start;
    o["x"] = c.x;
    o["y"] = c.y;
    o["width"] = c.width;
    o["opacity"] = c.opacity;
    return o;
}

} // namespace

bool saveTimeline(const TimelineState& s, const QString& path, QString* error)
{
    QJsonObject root;
    root["format"] = "mediaforge-timeline";
    root["version"] = 1;
    root["width"] = s.resolution.width();
    root["height"] = s.resolution.height();
    root["fps"] = s.fps;
    root["muteOriginal"] = s.muteOriginal;
    for (Track t : {Track::Main, Track::Overlay, Track::Audio}) {
        QJsonArray arr;
        for (const Clip& c : s.track(t))
            arr.append(clipToJson(c));
        root[t == Track::Main ? "main" : (t == Track::Overlay ? "overlays" : "audio")] = arr;
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Не удалось открыть файл: %1").arg(f.errorString());
        return false;
    }
    f.write(QJsonDocument(root).toJson());
    if (!f.commit()) {
        if (error)
            *error = QStringLiteral("Не удалось сохранить проект: %1").arg(f.errorString());
        return false;
    }
    return true;
}

bool loadTimeline(const QString& path, TimelineState* out, QStringList* warnings, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Не удалось открыть файл: %1").arg(f.errorString());
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    const QJsonObject root = doc.object();
    if (pe.error != QJsonParseError::NoError || root.value("format").toString() != "mediaforge-timeline") {
        if (error)
            *error = QStringLiteral("Файл не является проектом видео MediaForge или повреждён.");
        return false;
    }
    TimelineState s;
    s.resolution = QSize(root.value("width").toInt(1920), root.value("height").toInt(1080));
    s.fps = root.value("fps").toDouble(30.0);
    s.muteOriginal = root.value("muteOriginal").toBool();
    s.formatFromFirstClip = false;
    for (Track t : {Track::Main, Track::Overlay, Track::Audio}) {
        const QJsonArray arr = root.value(t == Track::Main ? "main" : (t == Track::Overlay ? "overlays" : "audio")).toArray();
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            Clip c;
            c.kind = kindFromId(o.value("kind").toString());
            c.path = o.value("path").toString();
            QString err;
            if (!probeAny(c.path, &c.info, &err)) {
                if (warnings)
                    *warnings << QStringLiteral("%1 — пропущен: %2").arg(QFileInfo(c.path).fileName(), err);
                continue;
            }
            c.in = o.value("in").toDouble();
            c.out = o.value("out").toDouble();
            c.speed = std::clamp(o.value("speed").toDouble(1.0), 0.25, 4.0);
            c.volume = o.value("volume").toDouble(1.0);
            c.muted = o.value("muted").toBool();
            c.fadeIn = o.value("fadeIn").toDouble();
            c.fadeOut = o.value("fadeOut").toDouble();
            c.start = o.value("start").toDouble();
            c.x = o.value("x").toDouble(0.05);
            c.y = o.value("y").toDouble(0.05);
            c.width = o.value("width").toDouble(0.3);
            c.opacity = o.value("opacity").toDouble(1.0);
            if (c.out - c.in < kMinClipLength)
                continue;
            s.track(t).push_back(c);
        }
    }
    *out = s;
    return true;
}

} // namespace mf
