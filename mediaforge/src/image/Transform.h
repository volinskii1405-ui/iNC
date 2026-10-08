#pragma once

#include "image/Layer.h"

// Whole-document geometry operations. Each returns a new state that callers
// commit as one undo step.
namespace mf::transform {

DocState scaleImage(const DocState& s, QSize newSize, bool smooth = true);
// offset = where the old canvas's top-left lands on the new canvas.
DocState resizeCanvas(const DocState& s, QSize newSize, QPoint offset, QColor fill = Qt::transparent);
DocState crop(const DocState& s, const QRect& rect);
DocState rotate(const DocState& s, double degrees);
DocState flip(const DocState& s, bool horizontal);

// Single-layer operations; return a canvas-sized image.
QImage transformLayer(const QImage& layer, double scaleX, double scaleY, double degrees);
QImage flipLayer(const QImage& layer, bool horizontal);
QImage offsetLayer(const QImage& layer, QPoint offset);
QRect contentBounds(const QImage& layer); // bounding box of non-transparent pixels

QPoint anchorOffset(QSize oldSize, QSize newSize, int anchor); // anchor 0..8, row-major

} // namespace mf::transform
