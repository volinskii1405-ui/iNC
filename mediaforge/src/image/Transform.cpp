#include "image/Transform.h"

#include <QPainter>
#include <QTransform>

#include <cmath>

namespace mf::transform {

namespace {

// Applies the same geometric mapping to every layer and to the selection.
DocState mapState(const DocState& s, QSize newSize, const QTransform& t, bool smooth)
{
    DocState out = s;
    out.size = newSize;
    for (Layer& l : out.layers) {
        QImage img = makeLayerImage(newSize);
        QPainter p(&img);
        p.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
        p.setRenderHint(QPainter::Antialiasing, smooth);
        p.setTransform(t);
        p.drawImage(0, 0, l.image);
        p.end();
        l.image = img;
        l.text.reset();
    }
    out.selection = t.map(s.selection);
    return out;
}

} // namespace

DocState scaleImage(const DocState& s, QSize newSize, bool smooth)
{
    if (newSize.isEmpty() || newSize == s.size)
        return s;
    DocState out = s;
    out.size = newSize;
    for (Layer& l : out.layers) {
        l.image = l.image.scaled(newSize, Qt::IgnoreAspectRatio, smooth ? Qt::SmoothTransformation : Qt::FastTransformation)
                      .convertToFormat(kLayerFormat);
        l.text.reset();
    }
    QTransform t;
    t.scale(double(newSize.width()) / s.size.width(), double(newSize.height()) / s.size.height());
    out.selection = t.map(s.selection);
    return out;
}

DocState resizeCanvas(const DocState& s, QSize newSize, QPoint offset, QColor fill)
{
    DocState out = s;
    out.size = newSize;
    for (int i = 0; i < out.layers.size(); ++i) {
        Layer& l = out.layers[i];
        // Only the bottom layer gets the background fill so upper layers keep transparency.
        QImage img = makeLayerImage(newSize, i == 0 ? fill : QColor(Qt::transparent));
        QPainter p(&img);
        p.setCompositionMode(i == 0 && fill.alpha() > 0 ? QPainter::CompositionMode_SourceOver
                                                         : QPainter::CompositionMode_Source);
        p.drawImage(offset, l.image);
        p.end();
        l.image = img;
        if (l.text)
            l.text->pos += offset;
    }
    out.selection = s.selection.translated(offset);
    return out;
}

DocState crop(const DocState& s, const QRect& rect)
{
    const QRect r = rect.normalized();
    if (r.isEmpty())
        return s;
    return resizeCanvas(s, r.size(), -r.topLeft());
}

DocState rotate(const DocState& s, double degrees)
{
    degrees = std::fmod(degrees, 360.0);
    if (degrees < 0)
        degrees += 360.0;
    if (std::abs(degrees) < 1e-9)
        return s;
    const int quarter = int(std::lround(degrees / 90.0)) % 4;
    const bool exact = std::abs(degrees - quarter * 90.0) < 1e-9;
    const double w = s.size.width(), h = s.size.height();
    QTransform rot;
    rot.rotate(degrees);
    const QRectF bounds = rot.mapRect(QRectF(0, 0, w, h));
    QSize newSize = exact ? ((quarter % 2) ? QSize(s.size.height(), s.size.width()) : s.size)
                          : QSize(int(std::ceil(bounds.width() - 1e-6)), int(std::ceil(bounds.height() - 1e-6)));
    QTransform t;
    t.translate(newSize.width() / 2.0, newSize.height() / 2.0);
    t.rotate(degrees);
    t.translate(-w / 2.0, -h / 2.0);
    return mapState(s, newSize, t, !exact);
}

DocState flip(const DocState& s, bool horizontal)
{
    QTransform t;
    if (horizontal)
        t = QTransform(-1, 0, 0, 1, s.size.width(), 0);
    else
        t = QTransform(1, 0, 0, -1, 0, s.size.height());
    DocState out = s;
    for (Layer& l : out.layers) {
        l.image = flipLayer(l.image, horizontal);
        l.text.reset();
    }
    out.selection = t.map(s.selection);
    return out;
}

QImage flipLayer(const QImage& layer, bool horizontal)
{
    return layer.mirrored(horizontal, !horizontal).convertToFormat(kLayerFormat);
}

QImage offsetLayer(const QImage& layer, QPoint offset)
{
    QImage img = makeLayerImage(layer.size());
    QPainter p(&img);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(offset, layer);
    return img;
}

QRect contentBounds(const QImage& layer)
{
    int x0 = layer.width(), y0 = layer.height(), x1 = -1, y1 = -1;
    for (int y = 0; y < layer.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(layer.constScanLine(y));
        for (int x = 0; x < layer.width(); ++x) {
            if (qAlpha(row[x])) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
        }
    }
    if (x1 < 0)
        return QRect();
    return QRect(QPoint(x0, y0), QPoint(x1, y1));
}

QImage transformLayer(const QImage& layer, double scaleX, double scaleY, double degrees)
{
    QRect bounds = contentBounds(layer);
    if (bounds.isEmpty())
        return layer;
    const QPointF c = QRectF(bounds).center();
    QTransform t;
    t.translate(c.x(), c.y());
    t.rotate(degrees);
    t.scale(scaleX, scaleY);
    t.translate(-c.x(), -c.y());
    QImage img = makeLayerImage(layer.size());
    QPainter p(&img);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::Antialiasing);
    p.setTransform(t);
    p.drawImage(0, 0, layer);
    return img;
}

QPoint anchorOffset(QSize oldSize, QSize newSize, int anchor)
{
    const int col = anchor % 3, row = anchor / 3;
    const int dx = newSize.width() - oldSize.width();
    const int dy = newSize.height() - oldSize.height();
    return QPoint(col == 0 ? 0 : (col == 1 ? dx / 2 : dx), row == 0 ? 0 : (row == 1 ? dy / 2 : dy));
}

} // namespace mf::transform
