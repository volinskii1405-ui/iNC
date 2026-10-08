#include "imageui/CanvasView.h"

#include "app/Theme.h"
#include "image/Document.h"
#include "imageui/Tools.h"

#include <QFileInfo>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace mf {

namespace {
constexpr double kMinZoom = 0.01;
constexpr double kMaxZoom = 64.0;
} // namespace

CanvasView::CanvasView(Document* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(200, 150);

    m_checker = QPixmap(16, 16);
    m_checker.fill(QColor(200, 200, 200));
    {
        QPainter p(&m_checker);
        p.fillRect(0, 0, 8, 8, QColor(150, 150, 150));
        p.fillRect(8, 8, 8, 8, QColor(150, 150, 150));
    }

    connect(m_doc, &Document::contentChanged, this, &CanvasView::onContentChanged);
    connect(m_doc, &Document::structureChanged, this, &CanvasView::onStructureChanged);
    connect(m_doc, &Document::selectionChanged, this, [this] {
        if (m_doc->hasSelection())
            m_antsTimer.start();
        else
            m_antsTimer.stop();
        update();
    });
    m_antsTimer.setInterval(150);
    connect(&m_antsTimer, &QTimer::timeout, this, [this] {
        m_antsOffset = (m_antsOffset + 1) % 8;
        update();
    });
    rebuildCache();
}

void CanvasView::setTool(Tool* tool)
{
    m_tool = tool;
    m_toolDown = false;
    setCursor(m_tool ? m_tool->cursor() : QCursor(Qt::ArrowCursor));
    update();
}

QTransform CanvasView::canvasToView() const
{
    return QTransform(m_zoom, 0, 0, m_zoom, m_origin.x(), m_origin.y());
}

QPointF CanvasView::viewToCanvas(QPointF p) const
{
    return (p - m_origin) / m_zoom;
}

void CanvasView::setZoom(double zoom, QPointF anchor)
{
    zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    if (anchor.x() < 0)
        anchor = QPointF(width() / 2.0, height() / 2.0);
    const QPointF canvasPt = viewToCanvas(anchor);
    m_zoom = zoom;
    m_origin = anchor - canvasPt * m_zoom;
    clampPan();
    emit zoomChanged(m_zoom);
    update();
}

void CanvasView::zoomIn()
{
    // Snap to a pleasant sequence of zoom levels.
    static const double steps[] = {0.01, 0.02, 0.03, 0.05, 0.0833, 0.125, 0.1667, 0.25, 0.3333, 0.5, 0.6667, 1, 1.5, 2,
                                   3, 4, 6, 8, 12, 16, 24, 32, 48, 64};
    for (double s : steps)
        if (s > m_zoom * 1.001) {
            setZoom(s);
            return;
        }
}

void CanvasView::zoomOut()
{
    static const double steps[] = {64, 48, 32, 24, 16, 12, 8, 6, 4, 3, 2, 1.5, 1, 0.6667, 0.5, 0.3333, 0.25, 0.1667,
                                   0.125, 0.0833, 0.05, 0.03, 0.02, 0.01};
    for (double s : steps)
        if (s < m_zoom * 0.999) {
            setZoom(s);
            return;
        }
}

void CanvasView::fitToWindow()
{
    const QSize s = m_doc->size();
    if (s.isEmpty() || width() < 10 || height() < 10) {
        m_fitPending = true;
        return;
    }
    m_fitPending = false;
    const double z = std::min((width() - 40.0) / s.width(), (height() - 40.0) / s.height());
    m_zoom = std::clamp(std::min(z, 1.0), kMinZoom, kMaxZoom);
    m_origin = QPointF((width() - s.width() * m_zoom) / 2.0, (height() - s.height() * m_zoom) / 2.0);
    emit zoomChanged(m_zoom);
    update();
}

void CanvasView::actualSize()
{
    setZoom(1.0);
}

void CanvasView::clampPan()
{
    // Keep at least part of the canvas on screen.
    const QSizeF s = QSizeF(m_doc->size()) * m_zoom;
    const double margin = 60;
    m_origin.setX(std::clamp(m_origin.x(), margin - s.width(), width() - margin));
    m_origin.setY(std::clamp(m_origin.y(), margin - s.height(), height() - margin));
}

void CanvasView::rebuildCache()
{
    if (m_doc->isEmpty()) {
        m_cache = QImage();
        return;
    }
    m_cache = makeLayerImage(m_doc->size());
    m_doc->compositeInto(m_cache, m_doc->rect());
}

void CanvasView::onContentChanged(const QRect& r)
{
    if (m_cache.size() != m_doc->size()) {
        rebuildCache();
        update();
        return;
    }
    m_doc->compositeInto(m_cache, r);
    update(canvasToView().mapRect(QRectF(r)).toAlignedRect().adjusted(-2, -2, 2, 2));
}

void CanvasView::onStructureChanged()
{
    const bool resized = m_doc->size() != m_lastDocSize;
    m_lastDocSize = m_doc->size();
    rebuildCache();
    if (resized)
        fitToWindow();
    update();
}

void CanvasView::resizeEvent(QResizeEvent*)
{
    if (m_fitPending)
        fitToWindow();
}

void CanvasView::paintEvent(QPaintEvent* ev)
{
    QPainter p(this);
    p.fillRect(ev->rect(), theme::canvasBackdrop());
    if (m_cache.isNull())
        return;
    const QTransform t = canvasToView();
    const QRectF canvasView = t.mapRect(QRectF(m_doc->rect()));
    const QRectF visible = canvasView.intersected(QRectF(ev->rect()));
    if (!visible.isEmpty()) {
        p.save();
        p.setBrushOrigin(canvasView.topLeft());
        p.fillRect(visible, QBrush(m_checker));
        // Map the exposed area back to source pixels so only that part gets scaled.
        const QRectF src = t.inverted().mapRect(visible).intersected(QRectF(m_cache.rect()));
        const QRectF srcAligned = QRectF(src.toAlignedRect()).intersected(QRectF(m_cache.rect()));
        p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1.0);
        p.drawImage(t.mapRect(srcAligned), m_cache, srcAligned);
        p.restore();
    }
    p.setPen(QPen(QColor(0, 0, 0, 160), 1));
    p.drawRect(canvasView.adjusted(-1, -1, 0, 0));

    p.setRenderHint(QPainter::Antialiasing);
    if (m_doc->hasSelection()) {
        const QPainterPath path = t.map(m_doc->selection());
        p.setPen(QPen(Qt::black, 1));
        p.drawPath(path);
        QPen ants(Qt::white, 1, Qt::CustomDashLine);
        ants.setDashPattern({4, 4});
        ants.setDashOffset(m_antsOffset);
        p.setPen(ants);
        p.drawPath(path);
    }
    if (m_tool)
        m_tool->paintOverlay(p, t, m_zoom);
    if (m_tool && m_tool->showsBrushOutline() && m_mouse.x() >= 0 && !panning()) {
        const double r = std::max(1.0, m_brushSize * m_zoom / 2.0);
        p.setPen(QPen(QColor(0, 0, 0, 180), 1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(m_mouse, r + 1, r + 1);
        p.setPen(QPen(QColor(255, 255, 255, 200), 1));
        p.drawEllipse(m_mouse, r, r);
    }
}

bool CanvasView::panning() const
{
    return m_panDrag || m_spaceDown || (m_tool && m_tool->id() == ToolId::Hand);
}

static ToolEvent toolEvent(const CanvasView* v, QMouseEvent* e)
{
    ToolEvent te;
    te.pos = v->viewToCanvas(e->position());
    te.button = e->button();
    te.buttons = e->buttons();
    te.modifiers = e->modifiers();
    return te;
}

void CanvasView::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && panning())) {
        m_panDrag = true;
        m_panStart = e->position();
        m_originStart = m_origin;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (m_tool && !m_doc->isEmpty()) {
        m_toolDown = true;
        m_tool->press(toolEvent(this, e));
    }
}

void CanvasView::mouseMoveEvent(QMouseEvent* e)
{
    const QPointF prev = m_mouse;
    m_mouse = e->position();
    if (m_panDrag) {
        m_origin = m_originStart + (e->position() - m_panStart);
        clampPan();
        update();
        return;
    }
    const QPointF c = viewToCanvas(e->position());
    const QPoint px(int(std::floor(c.x())), int(std::floor(c.y())));
    emit cursorMoved(px, m_doc->rect().contains(px));
    if (m_tool && m_toolDown)
        m_tool->move(toolEvent(this, e));
    if (m_tool && m_tool->showsBrushOutline()) {
        const int r = int(m_brushSize * m_zoom / 2.0) + 4;
        update(QRect(prev.toPoint(), QSize()).adjusted(-r, -r, r, r));
        update(QRect(m_mouse.toPoint(), QSize()).adjusted(-r, -r, r, r));
    }
}

void CanvasView::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_panDrag && (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton)) {
        m_panDrag = false;
        setCursor(panning() ? QCursor(Qt::OpenHandCursor) : (m_tool ? m_tool->cursor() : QCursor()));
        return;
    }
    if (m_tool && m_toolDown) {
        m_toolDown = false;
        m_tool->release(toolEvent(this, e));
    }
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (m_tool && !panning())
        m_tool->doubleClick(toolEvent(this, e));
}

void CanvasView::wheelEvent(QWheelEvent* e)
{
    const QPoint d = e->angleDelta();
    if (e->modifiers() & Qt::ControlModifier) {
        const double factor = std::pow(1.0015, d.y());
        setZoom(m_zoom * factor, e->position());
    } else {
        if (e->modifiers() & Qt::ShiftModifier)
            m_origin.rx() += d.y() / 2.0;
        else {
            m_origin.rx() += d.x() / 2.0;
            m_origin.ry() += d.y() / 2.0;
        }
        clampPan();
        update();
    }
    e->accept();
}

void CanvasView::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        m_spaceDown = true;
        setCursor(Qt::OpenHandCursor);
        update();
        return;
    }
    if (m_tool && m_tool->key(e))
        return;
    QWidget::keyPressEvent(e);
}

void CanvasView::keyReleaseEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        m_spaceDown = false;
        setCursor(m_tool ? m_tool->cursor() : QCursor());
        update();
        return;
    }
    QWidget::keyReleaseEvent(e);
}

void CanvasView::leaveEvent(QEvent*)
{
    m_mouse = QPointF(-1, -1);
    emit cursorMoved(QPoint(), false);
    update();
}

void CanvasView::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void CanvasView::dropEvent(QDropEvent* e)
{
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile())
            paths << u.toLocalFile();
    if (!paths.isEmpty())
        emit filesDropped(paths);
}

} // namespace mf
