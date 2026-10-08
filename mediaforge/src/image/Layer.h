#pragma once

#include "image/BlendMode.h"

#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainterPath>
#include <QPoint>
#include <QString>
#include <QVector>

#include <optional>

namespace mf {

// Text layers stay editable until someone paints over them.
struct TextInfo {
    QString text;
    QFont font;
    QColor color = Qt::black;
    QPoint pos;

    bool operator==(const TextInfo& o) const
    {
        return text == o.text && font == o.font && color == o.color && pos == o.pos;
    }
};

struct Layer {
    QString name;
    QImage image; // Format_ARGB32_Premultiplied, always canvas-sized
    bool visible = true;
    qreal opacity = 1.0;
    BlendMode mode = BlendMode::Normal;
    std::optional<TextInfo> text;
};

// Everything that structural undo commands snapshot. QImage is implicitly
// shared, so a snapshot only costs memory for layers that change afterwards.
struct DocState {
    QSize size;
    QVector<Layer> layers;
    int active = 0;
    QPainterPath selection; // empty = nothing selected (operations apply everywhere)
};

constexpr QImage::Format kLayerFormat = QImage::Format_ARGB32_Premultiplied;

QImage makeLayerImage(QSize size, QColor fill = Qt::transparent);
QImage renderTextImage(const TextInfo& info, QSize canvas);
QRect textBounds(const TextInfo& info);

} // namespace mf
