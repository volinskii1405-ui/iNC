#include "imageui/Tools.h"

#include "image/Document.h"
#include "image/Filters.h"
#include "image/FloodFill.h"
#include "image/Transform.h"

#include <QKeyEvent>
#include <QPainterPath>
#include <QRadialGradient>

#include <cmath>

namespace mf {

void ToolSettings::setForeground(const QColor& c)
{
    if (!c.isValid() || c == foreground)
        return;
    foreground = c;
    emit colorsChanged();
}

void ToolSettings::setBackground(const QColor& c)
{
    if (!c.isValid() || c == background)
        return;
    background = c;
    emit colorsChanged();
}

void ToolSettings::swapColors()
{
    std::swap(foreground, background);
    emit colorsChanged();
}

void ToolSettings::resetColors()
{
    foreground = Qt::black;
    background = Qt::white;
    emit colorsChanged();
}

Document* Tool::doc() const
{
    return m_ctx->doc;
}

bool Tool::activeLayerEditable()
{
    if (doc()->isEmpty())
        return false;
    if (!doc()->layer(doc()->activeIndex()).visible) {
        m_ctx->status(QStringLiteral("Активный слой скрыт — включите его видимость, чтобы рисовать."));
        return false;
    }
    return true;
}

namespace {

QImage maskToAlpha(const QImage& mask)
{
    QImage a(mask.size(), QImage::Format_Alpha8);
    for (int y = 0; y < mask.height(); ++y)
        memcpy(a.scanLine(y), mask.constScanLine(y), size_t(mask.width()));
    return a;
}

QPen antsPen(qreal width = 1.0)
{
    QPen p(Qt::white, width, Qt::DashLine);
    p.setCosmetic(true);
    return p;
}

// ---------------------------------------------------------------- brush / eraser
class BrushTool : public Tool {
public:
    BrushTool(ToolContext* c, bool erase) : Tool(c), m_erase(erase) {}
    ToolId id() const override { return m_erase ? ToolId::Eraser : ToolId::Brush; }
    bool showsBrushOutline() const override { return true; }

    void press(const ToolEvent& e) override
    {
        if (e.button != Qt::LeftButton || !activeLayerEditable())
            return;
        m_active = true;
        doc()->beginPixelEdit(doc()->activeIndex());
        m_orig = doc()->editOriginal();
        m_stroke = QImage(doc()->size(), kLayerFormat);
        m_stroke.fill(Qt::transparent);
        m_dirty = QRect();
        m_last = e.pos;
        m_carry = 0.0;
        compose(dab(e.pos));
    }

    void move(const ToolEvent& e) override
    {
        if (!m_active)
            return;
        const double spacing = std::max(1.0, settings()->brushSize * 0.12);
        QLineF seg(m_last, e.pos);
        double len = seg.length();
        QRect changed;
        double t = spacing - m_carry;
        while (t <= len) {
            const QPointF p = seg.pointAt(t / len);
            changed |= dab(p);
            t += spacing;
        }
        m_carry = len - (t - spacing);
        if (len > 0)
            m_last = e.pos;
        if (!changed.isEmpty())
            compose(changed);
    }

    void release(const ToolEvent&) override
    {
        if (!m_active)
            return;
        m_active = false;
        doc()->endPixelEdit(m_erase ? QStringLiteral("Ластик") : QStringLiteral("Кисть"), m_dirty);
        m_stroke = QImage();
        m_orig = QImage();
    }

    void cancel() override
    {
        if (m_active) {
            m_active = false;
            doc()->cancelPixelEdit();
        }
    }

private:
    QRect dab(QPointF p)
    {
        const double r = std::max(0.5, settings()->brushSize / 2.0);
        QPainter g(&m_stroke);
        g.setRenderHint(QPainter::Antialiasing);
        if (doc()->hasSelection())
            g.setClipPath(doc()->selection());
        QColor c = m_erase ? QColor(Qt::black) : settings()->foreground;
        c.setAlpha(255);
        if (settings()->hardness >= 100 || r < 2.0) {
            g.setPen(Qt::NoPen);
            g.setBrush(c);
        } else {
            QRadialGradient grad(p, r);
            const double h = std::clamp(settings()->hardness / 100.0, 0.0, 0.99);
            QColor transparent = c;
            transparent.setAlpha(0);
            grad.setColorAt(0.0, c);
            grad.setColorAt(h, c);
            grad.setColorAt(1.0, transparent);
            g.setPen(Qt::NoPen);
            g.setBrush(grad);
        }
        g.drawEllipse(p, r, r);
        const QRect rr = QRectF(p.x() - r - 2, p.y() - r - 2, 2 * r + 4, 2 * r + 4).toAlignedRect();
        return rr.intersected(doc()->rect());
    }

    void compose(const QRect& r)
    {
        if (r.isEmpty())
            return;
        m_dirty |= r;
        QPainter p(&doc()->editImage());
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(r.topLeft(), m_orig, r);
        p.setCompositionMode(m_erase ? QPainter::CompositionMode_DestinationOut : QPainter::CompositionMode_SourceOver);
        p.setOpacity(settings()->opacity / 100.0);
        p.drawImage(r.topLeft(), m_stroke, r);
        p.end();
        doc()->notifyEdit(r);
    }

    bool m_erase;
    bool m_active = false;
    QImage m_orig;
    QImage m_stroke;
    QRect m_dirty;
    QPointF m_last;
    double m_carry = 0.0;
};

// ---------------------------------------------------------------- fill
class FillTool : public Tool {
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Fill; }
    void press(const ToolEvent& e) override
    {
        if (e.button != Qt::LeftButton || !activeLayerEditable())
            return;
        const QPoint pt = e.pos.toPoint();
        if (!doc()->rect().contains(pt))
            return;
        const int layer = doc()->activeIndex();
        const QImage src = settings()->sampleMerged ? doc()->composite() : doc()->layer(layer).image;
        QImage mask = floodFillMask(src, pt, settings()->tolerance, settings()->contiguous);
        if (doc()->hasSelection()) {
            const QImage sel = doc()->selectionMask();
            for (int y = 0; y < mask.height(); ++y) {
                uchar* m = mask.scanLine(y);
                const uchar* s = sel.constScanLine(y);
                for (int x = 0; x < mask.width(); ++x)
                    m[x] = uchar((int(m[x]) * s[x]) / 255);
            }
        }
        const QImage out = filters::fillWithMask(doc()->layer(layer).image, mask, settings()->foreground,
                                                 settings()->opacity / 100.0);
        doc()->applyLayerImage(layer, out, QStringLiteral("Заливка"));
    }
};

// ---------------------------------------------------------------- eyedropper
class PickerTool : public Tool {
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Picker; }
    void press(const ToolEvent& e) override { pick(e, e.button); }
    void move(const ToolEvent& e) override
    {
        if (e.buttons & Qt::LeftButton)
            pick(e, Qt::LeftButton);
        else if (e.buttons & Qt::RightButton)
            pick(e, Qt::RightButton);
    }

private:
    void pick(const ToolEvent& e, Qt::MouseButton b)
    {
        const QPoint pt = e.pos.toPoint();
        const QColor c = settings()->sampleMerged ? doc()->sampleComposite(pt)
                                                  : doc()->sampleLayer(doc()->activeIndex(), pt);
        if (!c.isValid())
            return;
        QColor opaque = c;
        opaque.setAlpha(255);
        if (b == Qt::RightButton)
            settings()->setBackground(opaque);
        else
            settings()->setForeground(opaque);
        m_ctx->status(QStringLiteral("Цвет: %1").arg(opaque.name()));
    }
};

// ---------------------------------------------------------------- selections
class SelectTool : public Tool {
public:
    SelectTool(ToolContext* c, ToolId kind) : Tool(c), m_kind(kind) {}
    ToolId id() const override { return m_kind; }

    void press(const ToolEvent& e) override
    {
        if (e.button != Qt::LeftButton)
            return;
        m_active = true;
        m_start = m_cur = e.pos;
        m_points = {e.pos};
        const bool shift = e.modifiers & Qt::ShiftModifier;
        const bool alt = e.modifiers & (Qt::AltModifier | Qt::ControlModifier);
        m_mode = shift && alt ? SelectMode::Intersect
                              : shift ? SelectMode::Add : alt ? SelectMode::Subtract : settings()->selectMode;
    }
    void move(const ToolEvent& e) override
    {
        if (!m_active)
            return;
        m_cur = e.pos;
        if (m_kind == ToolId::Lasso && QLineF(m_points.last(), e.pos).length() >= 1.5)
            m_points << e.pos;
        m_ctx->repaint();
    }
    void release(const ToolEvent& e) override
    {
        if (!m_active)
            return;
        m_active = false;
        m_cur = e.pos;
        QPainterPath shape = currentShape();
        const QRectF b = shape.boundingRect();
        const bool tiny = b.width() < 2 && b.height() < 2;
        QPainterPath result;
        const QPainterPath& sel = doc()->selection();
        if (tiny) {
            if (m_mode == SelectMode::Replace && doc()->hasSelection())
                doc()->setSelection(QPainterPath(), QStringLiteral("Снять выделение"));
            m_ctx->repaint();
            return;
        }
        QPainterPath canvas;
        canvas.addRect(QRectF(doc()->rect()));
        shape = shape.intersected(canvas);
        switch (m_mode) {
        case SelectMode::Replace: result = shape; break;
        case SelectMode::Add: result = doc()->hasSelection() ? sel.united(shape) : shape; break;
        case SelectMode::Subtract: result = sel.subtracted(shape); break;
        case SelectMode::Intersect: result = sel.intersected(shape); break;
        }
        doc()->setSelection(result.simplified(), QStringLiteral("Выделение"));
        m_ctx->repaint();
    }
    void paintOverlay(QPainter& p, const QTransform& toView, qreal) override
    {
        if (!m_active)
            return;
        const QPainterPath path = toView.map(currentShape());
        p.setPen(QPen(Qt::black, 1));
        p.drawPath(path);
        p.setPen(antsPen());
        p.drawPath(path);
    }
    void cancel() override { m_active = false; }

private:
    QPainterPath currentShape() const
    {
        QPainterPath path;
        const QRectF r = QRectF(m_start, m_cur).normalized();
        if (m_kind == ToolId::RectSelect)
            path.addRect(r);
        else if (m_kind == ToolId::EllipseSelect)
            path.addEllipse(r);
        else if (m_points.size() >= 3) {
            path.addPolygon(QPolygonF(m_points));
            path.closeSubpath();
        }
        return path;
    }

    ToolId m_kind;
    bool m_active = false;
    SelectMode m_mode = SelectMode::Replace;
    QPointF m_start, m_cur;
    QVector<QPointF> m_points;
};

// ---------------------------------------------------------------- move
class MoveTool : public Tool {
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Move; }
    QCursor cursor() const override { return Qt::SizeAllCursor; }

    void press(const ToolEvent& e) override
    {
        if (e.button != Qt::LeftButton || !begin())
            return;
        m_start = e.pos;
    }
    void move(const ToolEvent& e) override
    {
        if (!m_active)
            return;
        m_offset = (e.pos - m_start).toPoint();
        if (e.modifiers & Qt::ShiftModifier) // constrain to an axis
            (std::abs(m_offset.x()) > std::abs(m_offset.y()) ? m_offset.ry() : m_offset.rx()) = 0;
        render();
    }
    void release(const ToolEvent&) override { finish(); }
    bool key(QKeyEvent* ev) override
    {
        QPoint d;
        const int step = (ev->modifiers() & Qt::ShiftModifier) ? 10 : 1;
        switch (ev->key()) {
        case Qt::Key_Left: d = {-step, 0}; break;
        case Qt::Key_Right: d = {step, 0}; break;
        case Qt::Key_Up: d = {0, -step}; break;
        case Qt::Key_Down: d = {0, step}; break;
        default: return false;
        }
        if (m_active || !begin())
            return true;
        m_offset = d;
        render();
        finish();
        return true;
    }
    void paintOverlay(QPainter& p, const QTransform& toView, qreal) override
    {
        if (!m_active || !doc()->hasSelection())
            return;
        const QPainterPath path = toView.map(doc()->selection().translated(m_offset));
        p.setPen(QPen(Qt::black, 1));
        p.drawPath(path);
        p.setPen(antsPen());
        p.drawPath(path);
    }
    void cancel() override
    {
        if (m_active) {
            m_active = false;
            doc()->cancelPixelEdit();
        }
    }

private:
    bool begin()
    {
        if (!activeLayerEditable())
            return false;
        m_active = true;
        m_offset = QPoint();
        doc()->beginPixelEdit(doc()->activeIndex());
        const QImage orig = doc()->editOriginal();
        if (doc()->hasSelection()) {
            const QImage mask = doc()->selectionMask();
            m_float = orig.copy();
            QPainter p(&m_float);
            p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            p.drawImage(0, 0, maskToAlpha(mask));
            p.end();
            m_base = filters::clearWithMask(orig, mask);
        } else {
            m_float = orig;
            m_base = makeLayerImage(orig.size());
        }
        return true;
    }
    void render()
    {
        QImage& img = doc()->editImage();
        img = m_base.copy();
        QPainter p(&img);
        p.drawImage(m_offset, m_float);
        p.end();
        doc()->notifyEdit(doc()->rect());
        m_ctx->repaint();
    }
    void finish()
    {
        if (!m_active)
            return;
        m_active = false;
        if (m_offset.isNull()) {
            doc()->cancelPixelEdit();
            return;
        }
        QUndoStack* undo = doc()->undoStack();
        if (doc()->hasSelection()) {
            const QPainterPath moved = doc()->selection().translated(m_offset);
            undo->beginMacro(QStringLiteral("Перемещение выделения"));
            doc()->endPixelEdit(QStringLiteral("Перемещение"), doc()->rect());
            doc()->setSelection(moved, QStringLiteral("Сдвиг выделения"));
            undo->endMacro();
        } else {
            doc()->endPixelEdit(QStringLiteral("Перемещение"), doc()->rect());
        }
        m_float = m_base = QImage();
    }

    bool m_active = false;
    QPointF m_start;
    QPoint m_offset;
    QImage m_float;
    QImage m_base;
};

// ---------------------------------------------------------------- crop
class CropTool : public Tool {
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Crop; }

    void press(const ToolEvent& e) override
    {
        if (e.button != Qt::LeftButton)
            return;
        m_dragging = true;
        if (!m_rect.isEmpty() && m_rect.contains(e.pos)) {
            m_moving = true;
            m_grab = e.pos - m_rect.topLeft();
        } else {
            m_moving = false;
            m_start = clampPt(e.pos);
            m_rect = QRectF(m_start, m_start);
        }
        m_ctx->status(QStringLiteral("Enter или двойной щелчок — кадрировать, Esc — отмена"));
    }
    void move(const ToolEvent& e) override
    {
        if (!m_dragging)
            return;
        if (m_moving) {
            QRectF r = m_rect;
            r.moveTopLeft(e.pos - m_grab);
            const QRectF b(doc()->rect());
            r.moveLeft(std::clamp(r.left(), b.left(), b.right() + 1 - r.width()));
            r.moveTop(std::clamp(r.top(), b.top(), b.bottom() + 1 - r.height()));
            m_rect = r;
        } else {
            m_rect = QRectF(m_start, clampPt(e.pos)).normalized();
        }
        m_ctx->repaint();
    }
    void release(const ToolEvent&) override { m_dragging = false; }
    void doubleClick(const ToolEvent&) override { apply(); }
    bool key(QKeyEvent* ev) override
    {
        if (ev->key() == Qt::Key_Return || ev->key() == Qt::Key_Enter) {
            apply();
            return true;
        }
        if (ev->key() == Qt::Key_Escape) {
            cancel();
            m_ctx->repaint();
            return true;
        }
        return false;
    }
    void cancel() override
    {
        m_rect = QRectF();
        m_dragging = false;
    }
    void paintOverlay(QPainter& p, const QTransform& toView, qreal) override
    {
        if (m_rect.isEmpty())
            return;
        const QRectF v = toView.mapRect(m_rect);
        QPainterPath outside;
        outside.addRect(toView.mapRect(QRectF(doc()->rect())));
        QPainterPath inside;
        inside.addRect(v);
        p.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 140));
        p.setPen(QPen(Qt::white, 1));
        p.drawRect(v);
        p.setPen(QPen(QColor(255, 255, 255, 90), 1));
        for (int i = 1; i < 3; ++i) {
            p.drawLine(QPointF(v.left() + v.width() * i / 3, v.top()), QPointF(v.left() + v.width() * i / 3, v.bottom()));
            p.drawLine(QPointF(v.left(), v.top() + v.height() * i / 3), QPointF(v.right(), v.top() + v.height() * i / 3));
        }
        const QRect r = m_rect.toRect();
        p.setPen(Qt::white);
        p.drawText(v.topLeft() + QPointF(4, -6), QStringLiteral("%1 × %2").arg(r.width()).arg(r.height()));
    }
    void apply()
    {
        const QRect r = m_rect.toRect().intersected(doc()->rect());
        if (r.width() < 1 || r.height() < 1)
            return;
        doc()->commitState(transform::crop(doc()->state(), r), QStringLiteral("Кадрирование"));
        cancel();
        m_ctx->repaint();
    }

private:
    QPointF clampPt(QPointF p) const
    {
        return QPointF(std::clamp(p.x(), 0.0, double(doc()->size().width())),
                       std::clamp(p.y(), 0.0, double(doc()->size().height())));
    }
    QRectF m_rect;
    QPointF m_start, m_grab;
    bool m_dragging = false;
    bool m_moving = false;
};

// ---------------------------------------------------------------- text
class TextTool : public Tool {
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Text; }
    QCursor cursor() const override { return Qt::IBeamCursor; }
    void press(const ToolEvent& e) override
    {
        if (e.button != Qt::LeftButton || doc()->isEmpty())
            return;
        // Clicking an existing text layer edits it; elsewhere creates a new one.
        for (int i = doc()->layerCount() - 1; i >= 0; --i) {
            const Layer& l = doc()->layer(i);
            if (l.visible && l.text && textBounds(*l.text).contains(e.pos.toPoint())) {
                m_ctx->editText(l.text->pos, l.text, i);
                return;
            }
        }
        m_ctx->editText(e.pos.toPoint(), std::nullopt, -1);
    }
};

// ---------------------------------------------------------------- gradient
class GradientTool : public Tool {
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Gradient; }
    void press(const ToolEvent& e) override
    {
        if (e.button != Qt::LeftButton || !activeLayerEditable())
            return;
        m_active = true;
        m_start = m_end = e.pos;
        doc()->beginPixelEdit(doc()->activeIndex());
    }
    void move(const ToolEvent& e) override
    {
        if (!m_active)
            return;
        m_end = e.pos;
        render();
    }
    void release(const ToolEvent& e) override
    {
        if (!m_active)
            return;
        m_active = false;
        m_end = e.pos;
        if (QLineF(m_start, m_end).length() < 1.0) {
            doc()->cancelPixelEdit();
            return;
        }
        render();
        doc()->endPixelEdit(QStringLiteral("Градиент"), doc()->rect());
    }
    void paintOverlay(QPainter& p, const QTransform& toView, qreal) override
    {
        if (!m_active)
            return;
        p.setPen(QPen(Qt::white, 1.5));
        p.drawLine(toView.map(m_start), toView.map(m_end));
        p.setBrush(Qt::white);
        p.drawEllipse(toView.map(m_start), 3, 3);
        p.drawEllipse(toView.map(m_end), 3, 3);
    }
    void cancel() override
    {
        if (m_active) {
            m_active = false;
            doc()->cancelPixelEdit();
        }
    }

private:
    void render()
    {
        QImage& img = doc()->editImage();
        img = doc()->editOriginal().copy();
        const QColor a = settings()->foreground;
        QColor b = settings()->background;
        if (settings()->gradientToTransparent) {
            b = a;
            b.setAlpha(0);
        }
        QGradient* g;
        QLinearGradient lin(m_start, m_end);
        QRadialGradient rad(m_start, QLineF(m_start, m_end).length());
        g = settings()->radialGradient ? static_cast<QGradient*>(&rad) : static_cast<QGradient*>(&lin);
        g->setColorAt(0, a);
        g->setColorAt(1, b);
        QPainter p(&img);
        if (doc()->hasSelection())
            p.setClipPath(doc()->selection());
        p.setOpacity(settings()->opacity / 100.0);
        p.fillRect(img.rect(), *g);
        p.end();
        doc()->notifyEdit(doc()->rect());
        m_ctx->repaint();
    }
    bool m_active = false;
    QPointF m_start, m_end;
};

class HandTool : public Tool {
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Hand; }
    QCursor cursor() const override { return Qt::OpenHandCursor; }
};

} // namespace

std::unique_ptr<Tool> createTool(ToolId id, ToolContext* ctx)
{
    switch (id) {
    case ToolId::Brush: return std::make_unique<BrushTool>(ctx, false);
    case ToolId::Eraser: return std::make_unique<BrushTool>(ctx, true);
    case ToolId::Fill: return std::make_unique<FillTool>(ctx);
    case ToolId::Picker: return std::make_unique<PickerTool>(ctx);
    case ToolId::RectSelect:
    case ToolId::EllipseSelect:
    case ToolId::Lasso: return std::make_unique<SelectTool>(ctx, id);
    case ToolId::Move: return std::make_unique<MoveTool>(ctx);
    case ToolId::Crop: return std::make_unique<CropTool>(ctx);
    case ToolId::Text: return std::make_unique<TextTool>(ctx);
    case ToolId::Gradient: return std::make_unique<GradientTool>(ctx);
    case ToolId::Hand: return std::make_unique<HandTool>(ctx);
    }
    return nullptr;
}

QString toolName(ToolId id)
{
    switch (id) {
    case ToolId::Move: return QStringLiteral("Перемещение");
    case ToolId::RectSelect: return QStringLiteral("Прямоугольное выделение");
    case ToolId::EllipseSelect: return QStringLiteral("Овальное выделение");
    case ToolId::Lasso: return QStringLiteral("Лассо");
    case ToolId::Crop: return QStringLiteral("Кадрирование");
    case ToolId::Brush: return QStringLiteral("Кисть");
    case ToolId::Eraser: return QStringLiteral("Ластик");
    case ToolId::Fill: return QStringLiteral("Заливка");
    case ToolId::Gradient: return QStringLiteral("Градиент");
    case ToolId::Text: return QStringLiteral("Текст");
    case ToolId::Picker: return QStringLiteral("Пипетка");
    case ToolId::Hand: return QStringLiteral("Рука");
    }
    return QString();
}

QString toolShortcut(ToolId id)
{
    switch (id) {
    case ToolId::Move: return QStringLiteral("V");
    case ToolId::RectSelect: return QStringLiteral("M");
    case ToolId::EllipseSelect: return QStringLiteral("Shift+M");
    case ToolId::Lasso: return QStringLiteral("L");
    case ToolId::Crop: return QStringLiteral("C");
    case ToolId::Brush: return QStringLiteral("B");
    case ToolId::Eraser: return QStringLiteral("E");
    case ToolId::Fill: return QStringLiteral("K");
    case ToolId::Gradient: return QStringLiteral("G");
    case ToolId::Text: return QStringLiteral("T");
    case ToolId::Picker: return QStringLiteral("I");
    case ToolId::Hand: return QStringLiteral("H");
    }
    return QString();
}

QString toolIcon(ToolId id)
{
    switch (id) {
    case ToolId::Move: return QStringLiteral("move");
    case ToolId::RectSelect: return QStringLiteral("rect-select");
    case ToolId::EllipseSelect: return QStringLiteral("ellipse-select");
    case ToolId::Lasso: return QStringLiteral("lasso");
    case ToolId::Crop: return QStringLiteral("crop");
    case ToolId::Brush: return QStringLiteral("brush");
    case ToolId::Eraser: return QStringLiteral("eraser");
    case ToolId::Fill: return QStringLiteral("bucket");
    case ToolId::Gradient: return QStringLiteral("gradient");
    case ToolId::Text: return QStringLiteral("text");
    case ToolId::Picker: return QStringLiteral("picker");
    case ToolId::Hand: return QStringLiteral("hand");
    }
    return QString();
}

} // namespace mf
