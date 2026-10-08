#pragma once

#include <QImage>
#include <QPointer>
#include <QTimer>
#include <QWidget>

namespace mf {

class Document;
class Tool;

class CanvasView : public QWidget {
    Q_OBJECT
public:
    explicit CanvasView(Document* doc, QWidget* parent = nullptr);

    void setTool(Tool* tool);
    double zoom() const { return m_zoom; }
    void setZoom(double zoom, QPointF anchorWidget = QPointF(-1, -1));
    void zoomIn();
    void zoomOut();
    void fitToWindow();
    void actualSize();
    void setBrushSize(int size) { m_brushSize = size; update(); }

    QTransform canvasToView() const;
    QPointF viewToCanvas(QPointF p) const;

signals:
    void zoomChanged(double zoom);
    void cursorMoved(QPoint canvasPos, bool inside);
    void filesDropped(const QStringList& paths);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void leaveEvent(QEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    void onContentChanged(const QRect& r);
    void onStructureChanged();
    void rebuildCache();
    void clampPan();
    bool panning() const;

    Document* m_doc;
    Tool* m_tool = nullptr;
    QImage m_cache;
    QPixmap m_checker;
    double m_zoom = 1.0;
    QPointF m_origin; // widget position of the canvas's top-left
    bool m_spaceDown = false;
    bool m_panDrag = false;
    QPointF m_panStart;
    QPointF m_originStart;
    QPointF m_mouse = QPointF(-1, -1);
    bool m_toolDown = false;
    bool m_fitPending = true;
    int m_brushSize = 0;
    int m_antsOffset = 0;
    QTimer m_antsTimer;
    QSize m_lastDocSize;
};

} // namespace mf
