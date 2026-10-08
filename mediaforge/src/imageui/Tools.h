#pragma once

#include "image/Layer.h"

#include <QColor>
#include <QCursor>
#include <QFont>
#include <QObject>
#include <QPainter>
#include <QPointF>
#include <QTransform>

#include <functional>
#include <memory>

class QKeyEvent;

namespace mf {

class Document;

enum class ToolId { Move, RectSelect, EllipseSelect, Lasso, Crop, Brush, Eraser, Fill, Gradient, Text, Picker, Hand };
enum class SelectMode { Replace, Add, Subtract, Intersect };

class ToolSettings : public QObject {
    Q_OBJECT
public:
    int brushSize = 24;
    int hardness = 80;   // percent
    int opacity = 100;   // percent
    int tolerance = 32;
    bool contiguous = true;
    bool sampleMerged = false;
    SelectMode selectMode = SelectMode::Replace;
    bool radialGradient = false;
    bool gradientToTransparent = false;
    QColor foreground = Qt::black;
    QColor background = Qt::white;

    void setForeground(const QColor& c);
    void setBackground(const QColor& c);
    void swapColors();
    void resetColors();
    void notify() { emit changed(); }

signals:
    void changed();
    void colorsChanged();
};

struct ToolEvent {
    QPointF pos; // canvas coordinates
    Qt::MouseButton button = Qt::NoButton;
    Qt::MouseButtons buttons;
    Qt::KeyboardModifiers modifiers;
};

// What tools need from their surroundings.
struct ToolContext {
    Document* doc = nullptr;
    ToolSettings* settings = nullptr;
    std::function<void()> repaint;                        // overlay changed
    std::function<void(const QString&)> status;           // short message for the status bar
    std::function<void(QPoint, const std::optional<TextInfo>&, int)> editText; // pos, existing text, layer
};

class Tool {
public:
    explicit Tool(ToolContext* ctx) : m_ctx(ctx) {}
    virtual ~Tool() = default;
    virtual ToolId id() const = 0;
    virtual QCursor cursor() const { return Qt::CrossCursor; }
    virtual void press(const ToolEvent&) {}
    virtual void move(const ToolEvent&) {}
    virtual void release(const ToolEvent&) {}
    virtual void doubleClick(const ToolEvent&) {}
    virtual bool key(QKeyEvent*) { return false; }
    virtual void paintOverlay(QPainter&, const QTransform& toView, qreal zoom) { Q_UNUSED(toView) Q_UNUSED(zoom) }
    virtual void cancel() {}
    virtual bool showsBrushOutline() const { return false; }

protected:
    Document* doc() const;
    ToolSettings* settings() const { return m_ctx->settings; }
    bool activeLayerEditable();
    ToolContext* m_ctx;
};

std::unique_ptr<Tool> createTool(ToolId id, ToolContext* ctx);
QString toolName(ToolId id);
QString toolShortcut(ToolId id);
QString toolIcon(ToolId id);

} // namespace mf
