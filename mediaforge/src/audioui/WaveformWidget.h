#pragma once

#include "media/Audio.h"

#include <QWidget>

class QScrollBar;

namespace mf {

class WaveformWidget : public QWidget {
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget* parent = nullptr);

    void setBuffer(const PcmBuffer* pcm); // not owned; call bufferChanged() after edits
    void bufferChanged();
    void setPlayhead(qint64 frame, bool follow = false);
    qint64 playhead() const { return m_playhead; }
    void setSelection(qint64 a, qint64 b);
    qint64 selStart() const { return m_selA; }
    qint64 selEnd() const { return m_selB; }
    bool hasSelection() const { return m_selB > m_selA; }
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void zoomToSelection();

signals:
    void selectionChanged();
    void playheadMoved(qint64 frame);
    void filesDropped(const QStringList& paths);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    qint64 frameAtX(double x) const;
    double xOfFrame(qint64 f) const;
    void updateScrollBar();
    void rebuildPeaks();
    void zoomAround(double factor, double x);

    const PcmBuffer* m_pcm = nullptr;
    QVector<QVector<float>> m_peaks; // per channel, min/max per kBucket frames
    QScrollBar* m_bar;
    double m_fpp = 512.0; // frames per pixel
    qint64 m_offset = 0;  // first visible frame
    qint64 m_playhead = 0;
    qint64 m_selA = 0, m_selB = 0;
    qint64 m_anchor = 0;
    bool m_dragging = false;
};

} // namespace mf
