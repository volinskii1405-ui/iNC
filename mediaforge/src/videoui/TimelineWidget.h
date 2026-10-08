#pragma once

#include "media/Timeline.h"

#include <QWidget>

#include <optional>

class QScrollBar;

namespace mf {

class MediaCache;

class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    TimelineWidget(Timeline* tl, MediaCache* cache, QWidget* parent = nullptr);

    void setPlayhead(double t, bool follow = false);
    double playhead() const { return m_playhead; }
    void setRange(double in, double out);
    void select(Track track, quint64 id);
    void clearSelection();
    bool hasSelection() const { return m_selId != 0; }
    Track selectedTrack() const { return m_selTrack; }
    quint64 selectedId() const { return m_selId; }
    int selectedIndex() const;

    void zoomIn();
    void zoomOut();
    void zoomToFit();
    QSize minimumSizeHint() const override;
    QRectF clipRect(Track t, int index) const; // widget coordinates
    double timeToX(double t) const;

signals:
    void seekRequested(double t);
    void selectionChanged();
    void contextMenuRequested(const QPoint& globalPos);
    void filesDropped(const QStringList& paths, Track track, double t);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    enum class Part { Body, LeftEdge, RightEdge };
    struct Hit {
        Track track;
        int index;
        Part part;
    };
    struct Row {
        Track track;
        int y;
        int height;
        int lanes;
        int laneHeight;
    };

    QVector<Row> rows() const;
    QVector<int> lanesFor(const QVector<Clip>& clips, int* count) const;
    std::optional<Hit> hitTest(QPointF p) const;
    double xToTime(double x) const;
    int trackLeft() const { return 96; }
    int contentBottom() const;
    void updateScrollBar();
    double snap(double t, quint64 ignoreId) const;
    void zoomAround(double factor, double x);
    void paintRuler(QPainter& p);
    void paintMainClip(QPainter& p, const Clip& c, const QRectF& r);
    void paintOverlayClip(QPainter& p, const Clip& c, const QRectF& r);
    void paintAudioClip(QPainter& p, const Clip& c, const QRectF& r);
    std::optional<Track> trackAtY(int y) const;

    Timeline* m_tl;
    MediaCache* m_cache;
    QScrollBar* m_hbar;
    double m_pps = 60.0;  // pixels per second
    double m_scroll = 0.0; // seconds at the left edge
    double m_playhead = 0.0;
    double m_in = -1, m_out = -1;
    Track m_selTrack = Track::Main;
    quint64 m_selId = 0;

    enum class Drag { None, Scrub, Move, TrimLeft, TrimRight };
    Drag m_drag = Drag::None;
    TimelineState m_before;
    Track m_dragTrack = Track::Main;
    quint64 m_dragId = 0;
    int m_dragIndex = -1;
    Clip m_dragClip;
    double m_dragT0 = 0;
    int m_insertIndex = -1;
    bool m_dragChanged = false;
    bool m_userZoomed = false;
};

} // namespace mf
