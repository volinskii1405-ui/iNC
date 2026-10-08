#pragma once

#include <QImage>
#include <QPointF>
#include <QVector>

#include <array>

// All functions take and return Format_ARGB32_Premultiplied images and never
// modify their input, so they are safe to run on worker threads.
namespace mf::filters {

QImage gaussianBlur(const QImage& src, double radius);
QImage sharpen(const QImage& src, double amount, double radius);
QImage addNoise(const QImage& src, int amount, bool monochrome, quint32 seed);
QImage grayscale(const QImage& src);
QImage sepia(const QImage& src);
QImage invert(const QImage& src);
QImage pixelate(const QImage& src, int cell);

QImage brightnessContrast(const QImage& src, int brightness, int contrast);
QImage hueSaturation(const QImage& src, int hue, int saturation, int lightness);

struct Levels {
    int inBlack = 0;
    int inWhite = 255;
    double gamma = 1.0;
    int outBlack = 0;
    int outWhite = 255;
};
enum class Channel { RGB, Red, Green, Blue };

using Lut = std::array<quint8, 256>;
Lut identityLut();
Lut levelsLut(const Levels& lv);
// Monotone cubic interpolation through control points (x, y in 0..255).
Lut curveLut(QVector<QPointF> points);
Lut composeLut(const Lut& first, const Lut& second);

QImage levels(const QImage& src, const Levels& lv, Channel channel);
QImage applyLuts(const QImage& src, const Lut& r, const Lut& g, const Lut& b);

// Mixes processed into original using an 8-bit mask (null mask = processed).
QImage blendWithMask(const QImage& original, const QImage& processed, const QImage& mask);
// Erases pixels under the mask (Delete on a selection).
QImage clearWithMask(const QImage& src, const QImage& mask);
// Paints a solid color under the mask with source-over.
QImage fillWithMask(const QImage& src, const QImage& mask, QColor color, qreal opacity);

} // namespace mf::filters
