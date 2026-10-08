#include "videoui/TimelineWidget.h"

#include "app/Theme.h"
#include "videoui/MediaCache.h"

#include <QDropEvent>
#include <QContextMenuEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace mf {

namespace {
constexpr int kRulerH = 26;
constexpr int kMainH = 72;
constexpr int kOverlayLaneH = 34;
constexpr int kAudioLaneH = 48;
constexpr int kScrollH = 14;
constexpr double kEdgePx = 6.0;

QString timeLabel(double t, double step)
{
    const int total = int(std::floor(t + 1e-6));
    const int m = total / 60, s = total % 60;
    if (step < 1.0)
        return QStringLiteral("%1:%2.%3").arg(m).arg(s, 2, 10, QLatin1Char('0')).arg(int(std::lround((t - total) * 10)) % 10);
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QColor kindColor(ClipKind k)
{
    switch (k) {
    case ClipKind::Video: return QColor(52, 88, 140);
    case ClipKind::Image: return QColor(98, 74, 140);
    case ClipKind::Overlay: return QColor(140, 100, 40);
    case ClipKind::Audio: return QColor(44, 110, 76);
    }
    return Qt::gray;
}
} // namespace

TimelineWidget::TimelineWidget(Timeline* tl, MediaCache* cache, QWidget* parent)
    : QWidget(parent), m_tl(tl), m_cache(cache)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setAcceptDrops(true);
    m_hbar = new QScrollBar(Qt::Horizontal, this);
    connect(m_hbar, &QScrollBar::valueChanged, this, [this](int v) {
        m_scroll = v / m_pps;
        update();
    });
    connect(m_tl, &Timeline::changed, this, [this] {
        if (!m_userZoomed && m_drag == Drag::None)
            zoomToFit();
        updateScrollBar();
        if (m_selId && m_tl->state().indexOfId(m_selTrack, m_selId) < 0)
            clearSelection();
        update();
    });
    connect(m_cache, &MediaCache::updated, this, qOverload<>(&QWidget::update));
}

QSize TimelineWidget::minimumSizeHint() const
{
    return QSize(400, kRulerH + kMainH + kOverlayLaneH + kAudioLaneH + kScrollH + 8);
}

QVector<int> TimelineWidget::lanesFor(const QVector<Clip>& clips, int* count) const
{
    // Greedy lane assignment so overlapping clips sit on separate lanes.
    QVector<int> order(clips.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return clips[a].start < clips[b].start; });
    QVector<double> laneEnd;
    QVector<int> lane(clips.size(), 0);
    for (int i : order) {
        int l = 0;
        while (l < laneEnd.size() && laneEnd[l] > clips[i].start + 1e-6)
            ++l;
        if (l == laneEnd.size())
            laneEnd.push_back(0);
        laneEnd[l] = clips[i].end();
        lane[i] = l;
    }
    *count = std::max(1, int(laneEnd.size()));
    return lane;
}

QVector<TimelineWidget::Row> TimelineWidget::rows() const
{
    const TimelineState& s = m_tl->state();
    int ov = 1, au = 1;
    lanesFor(s.overlays, &ov);
    lanesFor(s.audio, &au);
    QVector<Row> r;
    int y = kRulerH;
    r.push_back({Track::Main, y, kMainH, 1, kMainH});
    y += kMainH;
    r.push_back({Track::Overlay, y, ov * kOverlayLaneH, ov, kOverlayLaneH});
    y += ov * kOverlayLaneH;
    r.push_back({Track::Audio, y, au * kAudioLaneH, au, kAudioLaneH});
    return r;
}

int TimelineWidget::contentBottom() const
{
    const auto r = rows();
    return r.last().y + r.last().height;
}

double TimelineWidget::xToTime(double x) const
{
    return m_scroll + (x - trackLeft()) / m_pps;
}

double TimelineWidget::timeToX(double t) const
{
    return trackLeft() + (t - m_scroll) * m_pps;
}

QRectF TimelineWidget::clipRect(Track t, int index) const
{
    const TimelineState& s = m_tl->state();
    const auto rs = rows();
    const Row& row = rs[t == Track::Main ? 0 : (t == Track::Overlay ? 1 : 2)];
    const Clip& c = s.track(t)[index];
    const double start = t == Track::Main ? s.mainStart(index) : c.start;
    int lane = 0;
    if (t != Track::Main) {
        int n = 0;
        lane = lanesFor(s.track(t), &n)[index];
    }
    const double x0 = timeToX(start), x1 = timeToX(start + c.length());
    return QRectF(x0, row.y + lane * row.laneHeight + 2, std::max(2.0, x1 - x0), row.laneHeight - 4);
}

std::optional<Track> TimelineWidget::trackAtY(int y) const
{
    for (const Row& r : rows())
        if (y >= r.y && y < r.y + r.height)
            return r.track;
    return std::nullopt;
}

std::optional<TimelineWidget::Hit> TimelineWidget::hitTest(QPointF p) const
{
    const auto track = trackAtY(int(p.y()));
    if (!track || p.x() < trackLeft())
        return std::nullopt;
    const auto& clips = m_tl->state().track(*track);
    for (int i = clips.size() - 1; i >= 0; --i) {
        const QRectF r = clipRect(*track, i);
        if (!r.contains(p))
            continue;
        Part part = Part::Body;
        if (r.width() > 3 * kEdgePx) {
            if (p.x() - r.left() <= kEdgePx)
                part = Part::LeftEdge;
            else if (r.right() - p.x() <= kEdgePx)
                part = Part::RightEdge;
        }
        return Hit{*track, i, part};
    }
    return std::nullopt;
}

void TimelineWidget::setPlayhead(double t, bool follow)
{
    if (std::abs(t - m_playhead) < 1e-9)
        return;
    m_playhead = t;
    if (follow) {
        const double x = timeToX(t);
        const int w = width() - trackLeft();
        if (x > width() - 20 || x < trackLeft())
            m_hbar->setValue(int(std::max(0.0, (t - 0.1 * w / m_pps) * m_pps)));
    }
    update();
}

void TimelineWidget::setRange(double in, double out)
{
    m_in = in;
    m_out = out;
    update();
}

void TimelineWidget::select(Track track, quint64 id)
{
    m_selTrack = track;
    m_selId = id;
    update();
    emit selectionChanged();
}

void TimelineWidget::clearSelection()
{
    if (!m_selId)
        return;
    m_selId = 0;
    update();
    emit selectionChanged();
}

int TimelineWidget::selectedIndex() const
{
    return m_selId ? m_tl->state().indexOfId(m_selTrack, m_selId) : -1;
}

void TimelineWidget::updateScrollBar()
{
    const double content = (m_tl->state().duration() + 5.0) * m_pps;
    const int visible = std::max(1, width() - trackLeft());
    m_hbar->setRange(0, std::max(0, int(content) - visible));
    m_hbar->setPageStep(visible);
    m_hbar->setSingleStep(std::max(1, visible / 20));
    m_hbar->setValue(int(m_scroll * m_pps));
}

void TimelineWidget::zoomAround(double factor, double x)
{
    const double t = xToTime(x);
    m_pps = std::clamp(m_pps * factor, 2.0, 2000.0);
    m_userZoomed = true;
    m_scroll = std::max(0.0, t - (x - trackLeft()) / m_pps);
    updateScrollBar();
    update();
}

void TimelineWidget::zoomIn()
{
    zoomAround(1.5, timeToX(m_playhead));
}

void TimelineWidget::zoomOut()
{
    zoomAround(1 / 1.5, timeToX(m_playhead));
}

void TimelineWidget::zoomToFit()
{
    const double dur = std::max(5.0, m_tl->state().duration() * 1.04);
    m_pps = std::clamp((width() - trackLeft() - 10) / dur, 2.0, 2000.0);
    m_scroll = 0;
    m_userZoomed = false;
    updateScrollBar();
    update();
}

void TimelineWidget::resizeEvent(QResizeEvent*)
{
    m_hbar->setGeometry(trackLeft(), height() - kScrollH, width() - trackLeft(), kScrollH);
    if (!m_userZoomed)
        zoomToFit();
    else
        updateScrollBar();
}

double TimelineWidget::snap(double t, quint64 ignoreId) const
{
    const TimelineState& s = m_tl->state();
    QVector<double> points{0.0, m_playhead};
    double acc = 0;
    for (const Clip& c : s.main) {
        acc += c.length();
        points << acc;
    }
    for (const auto* v : {&s.overlays, &s.audio})
        for (const Clip& c : *v)
            if (c.id != ignoreId)
                points << c.start << c.end();
    const double tol = 8.0 / m_pps;
    double best = t, bestD = tol;
    for (double p : points)
        if (std::abs(p - t) < bestD) {
            bestD = std::abs(p - t);
            best = p;
        }
    return best;
}

// ------------------------------------------------------------------ painting

void TimelineWidget::paintRuler(QPainter& p)
{
    const QRect ruler(trackLeft(), 0, width() - trackLeft(), kRulerH);
    p.fillRect(ruler, QColor(36, 36, 39));
    static const double steps[] = {0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1200};
    double step = steps[std::size(steps) - 1];
    for (double s : steps)
        if (s * m_pps >= 70) {
            step = s;
            break;
        }
    if (m_in >= 0 && m_out > m_in) {
        const QRectF r(timeToX(m_in), 0, (m_out - m_in) * m_pps, kRulerH);
        p.fillRect(r.intersected(QRectF(ruler)), QColor(61, 142, 232, 90));
    }
    p.setPen(theme::dimText());
    const double t0 = std::floor(m_scroll / step) * step;
    for (double t = t0; timeToX(t) < width(); t += step) {
        const double x = timeToX(t);
        if (x < trackLeft())
            continue;
        p.drawLine(QPointF(x, kRulerH - 9), QPointF(x, kRulerH));
        p.drawText(QPointF(x + 3, 13), timeLabel(t, step));
        for (int k = 1; k < 5; ++k) {
            const double xs = timeToX(t + step * k / 5);
            p.drawLine(QPointF(xs, kRulerH - 4), QPointF(xs, kRulerH));
        }
    }
}

void TimelineWidget::paintMainClip(QPainter& p, const Clip& c, const QRectF& r)
{
    p.fillRect(r, kindColor(c.kind));
    const double labelH = 16;
    const QRectF strip(r.left(), r.top() + labelH, r.width(), r.height() - labelH);
    const int thumbH = int(strip.height());
    const QSize res = m_tl->state().resolution;
    const double thumbW = std::max(24.0, thumbH * double(res.width()) / std::max(1, res.height()));
    p.save();
    p.setClipRect(strip.intersected(QRectF(trackLeft(), 0, width() - trackLeft(), height())));
    for (double x = r.left(); x < r.right(); x += thumbW) {
        if (x + thumbW < trackLeft() || x > width())
            continue;
        const double local = (x + thumbW / 2 - r.left()) / m_pps;
        const QImage th = m_cache->thumbnail(c.path, c.sourceTime(std::min(local, c.length())), thumbH, c.kind == ClipKind::Image);
        if (th.isNull() || th.width() <= 1)
            continue;
        const QRectF target(x, strip.top(), thumbW, strip.height());
        const QSizeF s = QSizeF(th.size()).scaled(target.size(), Qt::KeepAspectRatioByExpanding);
        const QRectF src((th.width() - th.width() * target.width() / s.width()) / 2,
                         (th.height() - th.height() * target.height() / s.height()) / 2,
                         th.width() * target.width() / s.width(), th.height() * target.height() / s.height());
        p.drawImage(target, th, src);
    }
    p.restore();
    QString label = QFileInfo(c.path).fileName();
    if (std::abs(c.speed - 1.0) > 1e-6)
        label = QStringLiteral("×%1  %2").arg(c.speed, 0, 'g', 3).arg(label);
    if (c.kind == ClipKind::Video && (c.muted || !c.info.hasAudio || m_tl->state().muteOriginal))
        label = QStringLiteral("[без звука]  ") + label;
    p.setPen(Qt::white);
    p.drawText(QRectF(r.left() + 4, r.top(), r.width() - 8, labelH), Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(label, Qt::ElideRight, int(r.width() - 8)));
}

void TimelineWidget::paintOverlayClip(QPainter& p, const Clip& c, const QRectF& r)
{
    p.fillRect(r, kindColor(c.kind));
    const int h = int(r.height()) - 4;
    const QImage th = m_cache->thumbnail(c.path, 0, h, true);
    double x = r.left() + 2;
    if (!th.isNull() && th.width() > 1 && r.width() > h) {
        p.drawImage(QPointF(x, r.top() + 2), th, QRectF(0, 0, std::min<double>(th.width(), r.width() - 4), th.height()));
        x += std::min<double>(th.width(), r.width() - 4) + 4;
    }
    p.setPen(Qt::white);
    p.drawText(QRectF(x, r.top(), r.right() - x - 2, r.height()), Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(QFileInfo(c.path).fileName(), Qt::ElideRight, int(r.right() - x - 2)));
}

void TimelineWidget::paintAudioClip(QPainter& p, const Clip& c, const QRectF& r)
{
    p.fillRect(r, c.muted ? QColor(70, 70, 70) : kindColor(c.kind));
    const QVector<float> peaks = m_cache->peaks(c.path);
    const double mid = r.center().y();
    const double amp = (r.height() - 6) / 2.0;
    if (peaks.size() > 2) {
        p.setPen(QColor(150, 230, 180, c.muted ? 90 : 200));
        const int buckets = peaks.size() / 2;
        const int x0 = int(std::max(r.left(), double(trackLeft())));
        const int x1 = int(std::min(r.right(), double(width())));
        for (int x = x0; x < x1; ++x) {
            const double local = (x - r.left()) / m_pps;
            const double src0 = c.sourceTime(local), src1 = c.sourceTime(local + 1.0 / m_pps);
            int b0 = int(src0 * MediaCache::kPeaksPerSecond), b1 = std::max(b0 + 1, int(src1 * MediaCache::kPeaksPerSecond));
            float mn = 0, mx = 0;
            for (int b = std::max(0, b0); b < std::min(buckets, b1); ++b) {
                mn = std::min(mn, peaks[2 * b]);
                mx = std::max(mx, peaks[2 * b + 1]);
            }
            double g = c.volume;
            if (c.fadeIn > 0 && local < c.fadeIn)
                g *= local / c.fadeIn;
            if (c.fadeOut > 0 && local > c.length() - c.fadeOut)
                g *= std::max(0.0, (c.length() - local) / c.fadeOut);
            p.drawLine(QPointF(x, mid - std::min(1.0, mx * g) * amp), QPointF(x, mid - std::max(-1.0, mn * g) * amp));
        }
    }
    p.setPen(QColor(255, 255, 255, 120));
    if (c.fadeIn > 0)
        p.drawLine(QPointF(r.left(), r.bottom()), QPointF(r.left() + c.fadeIn * m_pps, r.top()));
    if (c.fadeOut > 0)
        p.drawLine(QPointF(r.right() - c.fadeOut * m_pps, r.top()), QPointF(r.right(), r.bottom()));
    QString label = QFileInfo(c.path).fileName();
    if (std::abs(c.speed - 1.0) > 1e-6)
        label = QStringLiteral("×%1  %2").arg(c.speed, 0, 'g', 3).arg(label);
    if (c.muted)
        label = QStringLiteral("[выкл.]  ") + label;
    p.setPen(Qt::white);
    p.drawText(QRectF(r.left() + 4, r.top() + 1, r.width() - 8, 14), Qt::AlignLeft | Qt::AlignTop,
               p.fontMetrics().elidedText(label, Qt::ElideRight, int(r.width() - 8)));
}

void TimelineWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(28, 28, 30));
    const TimelineState& s = m_tl->state();
    const auto rs = rows();

    for (const Row& r : rs)
        p.fillRect(QRect(trackLeft(), r.y, width() - trackLeft(), r.height),
                   r.track == Track::Overlay ? QColor(33, 33, 36) : QColor(38, 38, 41));

    p.save();
    p.setClipRect(QRect(trackLeft(), kRulerH, width() - trackLeft(), height() - kRulerH));
    for (Track t : {Track::Main, Track::Overlay, Track::Audio}) {
        const auto& clips = s.track(t);
        for (int i = 0; i < clips.size(); ++i) {
            const QRectF r = clipRect(t, i);
            if (r.right() < trackLeft() || r.left() > width())
                continue;
            if (t == Track::Main)
                paintMainClip(p, clips[i], r);
            else if (t == Track::Overlay)
                paintOverlayClip(p, clips[i], r);
            else
                paintAudioClip(p, clips[i], r);
            const bool sel = m_selId == clips[i].id && m_selTrack == t;
            p.setPen(sel ? QPen(QColor(255, 210, 80), 2) : QPen(QColor(0, 0, 0, 160), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
        }
    }
    if (m_drag == Drag::Move && m_dragTrack == Track::Main && m_insertIndex >= 0) {
        double x = 0;
        int k = 0;
        for (int i = 0; i < s.main.size(); ++i) {
            if (s.main[i].id == m_dragId)
                continue;
            if (k == m_insertIndex)
                break;
            x += s.main[i].length();
            ++k;
        }
        p.setPen(QPen(QColor(255, 210, 80), 3));
        p.drawLine(QPointF(timeToX(x), rs[0].y), QPointF(timeToX(x), rs[0].y + rs[0].height));
    }
    if (m_in >= 0 && m_out > m_in) {
        QPen dash(QColor(61, 142, 232), 1, Qt::DashLine);
        p.setPen(dash);
        p.drawLine(QPointF(timeToX(m_in), kRulerH), QPointF(timeToX(m_in), contentBottom()));
        p.drawLine(QPointF(timeToX(m_out), kRulerH), QPointF(timeToX(m_out), contentBottom()));
    }
    if (s.main.isEmpty() && s.audio.isEmpty() && s.overlays.isEmpty()) {
        p.setPen(theme::dimText());
        p.drawText(QRect(trackLeft(), kRulerH, width() - trackLeft(), kMainH), Qt::AlignCenter,
                   QStringLiteral("Перетащите сюда видео, изображения или звук, либо нажмите Ctrl+I"));
    }
    p.restore();

    paintRuler(p);

    // Track headers.
    p.fillRect(QRect(0, 0, trackLeft(), height()), QColor(43, 43, 46));
    p.setPen(theme::text());
    const QStringList names = {QStringLiteral("Видео"), QStringLiteral("Оверлеи"), QStringLiteral("Аудио")};
    const QStringList icons = {QStringLiteral("video"), QStringLiteral("image"), QStringLiteral("audio")};
    for (int i = 0; i < rs.size(); ++i) {
        const Row& r = rs[i];
        icon(icons[i]).paint(&p, QRect(6, r.y + 6, 18, 18));
        p.drawText(QRect(28, r.y + 4, trackLeft() - 30, 22), Qt::AlignLeft | Qt::AlignVCenter, names[i]);
        if (r.track == Track::Main && s.muteOriginal) {
            p.setPen(QColor(235, 120, 120));
            p.drawText(QRect(6, r.y + 28, trackLeft() - 8, 18), Qt::AlignLeft, QStringLiteral("звук выкл."));
            p.setPen(theme::text());
        }
        p.setPen(QColor(20, 20, 22));
        p.drawLine(0, r.y + r.height, width(), r.y + r.height);
        p.setPen(theme::text());
    }

    const double px = timeToX(m_playhead);
    if (px >= trackLeft()) {
        p.setPen(QPen(QColor(235, 70, 70), 1.5));
        p.drawLine(QPointF(px, 0), QPointF(px, contentBottom()));
        QPainterPath tri;
        tri.moveTo(px - 6, 0);
        tri.lineTo(px + 6, 0);
        tri.lineTo(px, 9);
        tri.closeSubpath();
        p.fillPath(tri, QColor(235, 70, 70));
    }
}

// ------------------------------------------------------------------ interaction

void TimelineWidget::mousePressEvent(QMouseEvent* e)
{
    const QPointF pos = e->position();
    if (e->button() == Qt::MiddleButton)
        return;
    if (pos.y() < kRulerH || !hitTest(pos)) {
        if (e->button() != Qt::LeftButton)
            return;
        if (pos.y() >= kRulerH)
            clearSelection();
        m_drag = Drag::Scrub;
        emit seekRequested(std::max(0.0, xToTime(std::max(pos.x(), double(trackLeft())))));
        return;
    }
    const Hit h = *hitTest(pos);
    const Clip& c = m_tl->state().track(h.track)[h.index];
    select(h.track, c.id);
    if (e->button() != Qt::LeftButton)
        return;
    m_before = m_tl->state();
    m_dragTrack = h.track;
    m_dragId = c.id;
    m_dragIndex = h.index;
    m_dragClip = c;
    m_dragT0 = xToTime(pos.x());
    m_dragChanged = false;
    m_insertIndex = -1;
    m_drag = h.part == Part::LeftEdge ? Drag::TrimLeft : (h.part == Part::RightEdge ? Drag::TrimRight : Drag::Move);
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* e)
{
    const QPointF pos = e->position();
    if (m_drag == Drag::None) {
        const auto h = hitTest(pos);
        setCursor(h && h->part != Part::Body ? Qt::SizeHorCursor : Qt::ArrowCursor);
        return;
    }
    if (m_drag == Drag::Scrub) {
        emit seekRequested(std::max(0.0, xToTime(std::max(pos.x(), double(trackLeft())))));
        return;
    }
    const double t = xToTime(pos.x());
    const double dt = t - m_dragT0;
    TimelineState s = m_before;
    const int idx = s.indexOfId(m_dragTrack, m_dragId);
    if (idx < 0)
        return;
    Clip c = m_dragClip;
    const Clip& c0 = m_dragClip;

    if (m_drag == Drag::Move) {
        if (m_dragTrack == Track::Main) {
            // Insertion slot among the other clips, by midpoints.
            double acc = 0;
            int slot = 0;
            for (const Clip& o : s.main) {
                if (o.id == m_dragId)
                    continue;
                if (t > acc + o.length() / 2)
                    ++slot;
                acc += o.length();
            }
            m_insertIndex = slot;
            update();
            return;
        }
        double ns = std::max(0.0, c0.start + dt);
        const double snappedStart = snap(ns, c0.id);
        const double snappedEnd = snap(ns + c0.length(), c0.id);
        if (std::abs(snappedStart - ns) > 1e-9)
            ns = snappedStart;
        else if (std::abs(snappedEnd - (ns + c0.length())) > 1e-9)
            ns = snappedEnd - c0.length();
        c.start = std::max(0.0, ns);
        // Moving vertically onto the other positioned track is not allowed (different kinds).
    } else {
        const bool left = m_drag == Drag::TrimLeft;
        const double minLen = kMinClipLength;
        if (c0.isStill()) {
            if (left) {
                double d = dt;
                if (m_dragTrack != Track::Main) {
                    d = snap(c0.start + dt, c0.id) - c0.start;
                    d = std::max(d, -c0.start);
                }
                d = std::min(d, c0.length() - minLen);
                c.out = c0.out - d;
                if (m_dragTrack != Track::Main)
                    c.start = c0.start + d;
            } else {
                double end = c0.end() + dt;
                if (m_dragTrack != Track::Main)
                    end = snap(end, c0.id);
                c.out = std::max(minLen, c0.out + (end - c0.end()));
            }
        } else {
            const double srcDur = c0.info.duration;
            if (left) {
                double d = dt;
                if (m_dragTrack != Track::Main)
                    d = snap(c0.start + dt, c0.id) - c0.start;
                double newIn = std::clamp(c0.in + d * c0.speed, 0.0, c0.out - minLen * c0.speed);
                if (m_dragTrack != Track::Main)
                    newIn = std::max(newIn, c0.in - c0.start * c0.speed);
                c.in = newIn;
                if (m_dragTrack != Track::Main)
                    c.start = c0.start + (newIn - c0.in) / c0.speed;
            } else {
                double d = dt;
                if (m_dragTrack != Track::Main)
                    d = snap(c0.end() + dt, c0.id) - c0.end();
                c.out = std::clamp(c0.out + d * c0.speed, c0.in + minLen * c0.speed, std::max(c0.in + minLen, srcDur));
            }
        }
        c.fadeIn = std::min(c.fadeIn, c.length());
        c.fadeOut = std::min(c.fadeOut, c.length());
    }
    s.track(m_dragTrack)[idx] = c;
    m_dragChanged = true;
    m_tl->setStateSilently(s);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent*)
{
    const Drag d = m_drag;
    m_drag = Drag::None;
    if (d == Drag::Move && m_dragTrack == Track::Main) {
        const int from = m_tl->state().indexOfId(Track::Main, m_dragId);
        const int to = m_insertIndex;
        m_insertIndex = -1;
        if (from >= 0 && to >= 0 && to != from)
            m_tl->moveMainClip(from, to);
        update();
        return;
    }
    if ((d == Drag::Move || d == Drag::TrimLeft || d == Drag::TrimRight) && m_dragChanged) {
        m_tl->commitFrom(m_before, d == Drag::Move ? QStringLiteral("Перемещение клипа") : QStringLiteral("Обрезка клипа"));
    }
    m_dragChanged = false;
}

void TimelineWidget::wheelEvent(QWheelEvent* e)
{
    const QPoint d = e->angleDelta();
    if (e->modifiers() & Qt::ControlModifier) {
        zoomAround(std::pow(1.0015, d.y()), e->position().x());
    } else {
        const int delta = d.x() != 0 ? d.x() : d.y();
        m_hbar->setValue(m_hbar->value() - delta);
    }
    e->accept();
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent* e)
{
    if (const auto h = hitTest(e->pos()))
        select(h->track, m_tl->state().track(h->track)[h->index].id);
    emit contextMenuRequested(e->globalPos());
}

void TimelineWidget::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void TimelineWidget::dropEvent(QDropEvent* e)
{
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile())
            paths << u.toLocalFile();
    const auto track = trackAtY(int(e->position().y()));
    if (!paths.isEmpty())
        emit filesDropped(paths, track.value_or(Track::Main), std::max(0.0, xToTime(e->position().x())));
}

} // namespace mf
