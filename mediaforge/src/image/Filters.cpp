#include "image/Filters.h"

#include "common/Parallel.h"

#include <QColor>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace mf::filters {

namespace {

constexpr QImage::Format kFmt = QImage::Format_ARGB32_Premultiplied;

inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
inline int clampTo(int v, int hi) { return v < 0 ? 0 : (v > hi ? hi : v); }

QImage prepared(const QImage& src)
{
    return src.format() == kFmt ? src.copy() : src.convertToFormat(kFmt);
}

// Calls fn(QRgb* row, int y) for every row of img in parallel.
template <typename Fn>
void forEachRow(QImage& img, Fn fn)
{
    const int w = img.width();
    parallelFor(img.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
            fn(reinterpret_cast<QRgb*>(img.scanLine(y)), y, w);
    });
}

std::vector<int> boxesForGauss(double sigma, int n)
{
    const double wIdeal = std::sqrt(12.0 * sigma * sigma / n + 1.0);
    int wl = int(std::floor(wIdeal));
    if (wl % 2 == 0)
        --wl;
    const int wu = wl + 2;
    const double mIdeal = (12.0 * sigma * sigma - n * wl * wl - 4.0 * n * wl - 3.0 * n) / (-4.0 * wl - 4.0);
    const int m = int(std::lround(mIdeal));
    std::vector<int> sizes;
    for (int i = 0; i < n; ++i)
        sizes.push_back(i < m ? wl : wu);
    return sizes;
}

// Running-sum box blur over a strided line of premultiplied pixels.
void boxLine(const QRgb* in, QRgb* out, int len, int radius)
{
    if (radius <= 0 || len <= 1) {
        std::copy(in, in + len, out);
        return;
    }
    const int d = 2 * radius + 1;
    int sa = 0, sr = 0, sg = 0, sb = 0;
    auto add = [&](QRgb p, int k) {
        sa += k * int(qAlpha(p));
        sr += k * int(qRed(p));
        sg += k * int(qGreen(p));
        sb += k * int(qBlue(p));
    };
    add(in[0], radius + 1);
    for (int i = 1; i <= radius; ++i)
        add(in[std::min(i, len - 1)], 1);
    for (int x = 0; x < len; ++x) {
        out[x] = qRgba((sr + d / 2) / d, (sg + d / 2) / d, (sb + d / 2) / d, (sa + d / 2) / d);
        add(in[std::min(x + radius + 1, len - 1)], 1);
        add(in[std::max(x - radius, 0)], -1);
    }
}

template <typename PixelFn>
QImage mapUnpremultiplied(const QImage& src, PixelFn fn)
{
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    forEachRow(img, [&](QRgb* row, int, int w) {
        for (int x = 0; x < w; ++x)
            row[x] = fn(row[x]);
    });
    return img.convertToFormat(kFmt);
}

} // namespace

QImage gaussianBlur(const QImage& src, double radius)
{
    QImage img = prepared(src);
    if (radius < 0.3 || img.isNull())
        return img;
    const int w = img.width();
    const int h = img.height();
    const auto boxes = boxesForGauss(radius, 3);

    for (int size : boxes) {
        const int r = (size - 1) / 2;
        parallelFor(h, [&](int y0, int y1) {
            std::vector<QRgb> tmp(w);
            for (int y = y0; y < y1; ++y) {
                QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
                boxLine(row, tmp.data(), w, r);
                std::copy(tmp.begin(), tmp.end(), row);
            }
        });
        parallelFor(w, [&](int x0, int x1) {
            std::vector<QRgb> col(h), tmp(h);
            for (int x = x0; x < x1; ++x) {
                for (int y = 0; y < h; ++y)
                    col[y] = reinterpret_cast<const QRgb*>(img.constScanLine(y))[x];
                boxLine(col.data(), tmp.data(), h, r);
                for (int y = 0; y < h; ++y)
                    reinterpret_cast<QRgb*>(img.scanLine(y))[x] = tmp[y];
            }
        }, 8);
    }
    return img;
}

QImage sharpen(const QImage& src, double amount, double radius)
{
    const QImage base = src.convertToFormat(QImage::Format_ARGB32);
    const QImage blurred = gaussianBlur(src, radius).convertToFormat(QImage::Format_ARGB32);
    QImage out(base.size(), QImage::Format_ARGB32);
    parallelFor(base.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const QRgb* a = reinterpret_cast<const QRgb*>(base.constScanLine(y));
            const QRgb* b = reinterpret_cast<const QRgb*>(blurred.constScanLine(y));
            QRgb* o = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < base.width(); ++x) {
                auto ch = [&](int c, int bc) { return clamp255(int(std::lround(c + amount * (c - bc)))); };
                o[x] = qRgba(ch(qRed(a[x]), qRed(b[x])), ch(qGreen(a[x]), qGreen(b[x])), ch(qBlue(a[x]), qBlue(b[x])),
                             qAlpha(a[x]));
            }
        }
    });
    return out.convertToFormat(kFmt);
}

QImage addNoise(const QImage& src, int amount, bool monochrome, quint32 seed)
{
    const int spread = int(std::lround(amount * 1.275));
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    forEachRow(img, [&](QRgb* row, int y, int w) {
        std::mt19937 rng(seed * 2654435761u + quint32(y));
        std::uniform_int_distribution<int> dist(-spread, spread);
        for (int x = 0; x < w; ++x) {
            const QRgb p = row[x];
            if (qAlpha(p) == 0)
                continue;
            const int n = dist(rng);
            const int nr = n;
            const int ng = monochrome ? n : dist(rng);
            const int nb = monochrome ? n : dist(rng);
            row[x] = qRgba(clamp255(qRed(p) + nr), clamp255(qGreen(p) + ng), clamp255(qBlue(p) + nb), qAlpha(p));
        }
    });
    return img.convertToFormat(kFmt);
}

QImage grayscale(const QImage& src)
{
    QImage img = prepared(src);
    forEachRow(img, [](QRgb* row, int, int w) {
        for (int x = 0; x < w; ++x) {
            const QRgb p = row[x];
            const int v = (299 * qRed(p) + 587 * qGreen(p) + 114 * qBlue(p) + 500) / 1000;
            row[x] = qRgba(v, v, v, qAlpha(p));
        }
    });
    return img;
}

QImage sepia(const QImage& src)
{
    QImage img = prepared(src);
    forEachRow(img, [](QRgb* row, int, int w) {
        for (int x = 0; x < w; ++x) {
            const QRgb p = row[x];
            const int r = qRed(p), g = qGreen(p), b = qBlue(p), a = qAlpha(p);
            // Premultiplied values stay valid as long as each channel <= alpha.
            const int nr = clampTo(int(0.393 * r + 0.769 * g + 0.189 * b + 0.5), a);
            const int ng = clampTo(int(0.349 * r + 0.686 * g + 0.168 * b + 0.5), a);
            const int nb = clampTo(int(0.272 * r + 0.534 * g + 0.131 * b + 0.5), a);
            row[x] = qRgba(nr, ng, nb, a);
        }
    });
    return img;
}

QImage invert(const QImage& src)
{
    QImage img = prepared(src);
    forEachRow(img, [](QRgb* row, int, int w) {
        for (int x = 0; x < w; ++x) {
            const QRgb p = row[x];
            const int a = qAlpha(p);
            row[x] = qRgba(a - qRed(p), a - qGreen(p), a - qBlue(p), a);
        }
    });
    return img;
}

QImage pixelate(const QImage& src, int cell)
{
    QImage img = prepared(src);
    cell = std::max(1, cell);
    if (cell == 1)
        return img;
    const int w = img.width(), h = img.height();
    const int rows = (h + cell - 1) / cell;
    parallelFor(rows, [&](int r0, int r1) {
        for (int cy = r0; cy < r1; ++cy) {
            const int y0 = cy * cell, y1 = std::min(h, y0 + cell);
            for (int x0 = 0; x0 < w; x0 += cell) {
                const int x1 = std::min(w, x0 + cell);
                qint64 sa = 0, sr = 0, sg = 0, sb = 0;
                for (int y = y0; y < y1; ++y) {
                    const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
                    for (int x = x0; x < x1; ++x) {
                        sa += qAlpha(row[x]);
                        sr += qRed(row[x]);
                        sg += qGreen(row[x]);
                        sb += qBlue(row[x]);
                    }
                }
                const qint64 n = qint64(y1 - y0) * (x1 - x0);
                const QRgb avg = qRgba(int(sr / n), int(sg / n), int(sb / n), int(sa / n));
                for (int y = y0; y < y1; ++y) {
                    QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
                    std::fill(row + x0, row + x1, avg);
                }
            }
        }
    }, 1);
    return img;
}

Lut identityLut()
{
    Lut l;
    for (int i = 0; i < 256; ++i)
        l[i] = quint8(i);
    return l;
}

Lut composeLut(const Lut& first, const Lut& second)
{
    Lut l;
    for (int i = 0; i < 256; ++i)
        l[i] = second[first[i]];
    return l;
}

QImage applyLuts(const QImage& src, const Lut& r, const Lut& g, const Lut& b)
{
    return mapUnpremultiplied(src, [&](QRgb p) { return qRgba(r[qRed(p)], g[qGreen(p)], b[qBlue(p)], qAlpha(p)); });
}

QImage brightnessContrast(const QImage& src, int brightness, int contrast)
{
    const double B = brightness * 1.27;
    const double C = std::clamp(contrast * 2.55, -254.0, 254.0);
    const double f = 259.0 * (C + 255.0) / (255.0 * (259.0 - C));
    Lut l;
    for (int i = 0; i < 256; ++i)
        l[i] = quint8(clamp255(int(std::lround(f * (i + B - 128.0) + 128.0))));
    return applyLuts(src, l, l, l);
}

namespace {

void rgbToHsl(double r, double g, double b, double& h, double& s, double& l)
{
    const double mx = std::max({r, g, b});
    const double mn = std::min({r, g, b});
    l = (mx + mn) / 2.0;
    if (mx - mn < 1e-9) {
        h = s = 0.0;
        return;
    }
    const double d = mx - mn;
    s = l > 0.5 ? d / (2.0 - mx - mn) : d / (mx + mn);
    if (mx == r)
        h = (g - b) / d + (g < b ? 6.0 : 0.0);
    else if (mx == g)
        h = (b - r) / d + 2.0;
    else
        h = (r - g) / d + 4.0;
    h /= 6.0;
}

double hueToRgb(double p, double q, double t)
{
    if (t < 0)
        t += 1;
    if (t > 1)
        t -= 1;
    if (t < 1.0 / 6)
        return p + (q - p) * 6 * t;
    if (t < 0.5)
        return q;
    if (t < 2.0 / 3)
        return p + (q - p) * (2.0 / 3 - t) * 6;
    return p;
}

void hslToRgb(double h, double s, double l, double& r, double& g, double& b)
{
    if (s < 1e-9) {
        r = g = b = l;
        return;
    }
    const double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
    const double p = 2 * l - q;
    r = hueToRgb(p, q, h + 1.0 / 3);
    g = hueToRgb(p, q, h);
    b = hueToRgb(p, q, h - 1.0 / 3);
}

} // namespace

QImage hueSaturation(const QImage& src, int hue, int saturation, int lightness)
{
    const double dh = hue / 360.0;
    const double ds = saturation / 100.0;
    const double dl = lightness / 100.0;
    return mapUnpremultiplied(src, [&](QRgb p) {
        if (qAlpha(p) == 0)
            return p;
        double h, s, l;
        rgbToHsl(qRed(p) / 255.0, qGreen(p) / 255.0, qBlue(p) / 255.0, h, s, l);
        h = std::fmod(h + dh + 1.0, 1.0);
        s = ds < 0 ? s * (1.0 + ds) : s + (1.0 - s) * ds * s; // keep greys grey
        l = dl < 0 ? l * (1.0 + dl) : l + (1.0 - l) * dl;
        double r, g, b;
        hslToRgb(h, std::clamp(s, 0.0, 1.0), std::clamp(l, 0.0, 1.0), r, g, b);
        return qRgba(clamp255(int(std::lround(r * 255))), clamp255(int(std::lround(g * 255))),
                     clamp255(int(std::lround(b * 255))), qAlpha(p));
    });
}

Lut levelsLut(const Levels& lv)
{
    Lut l;
    const double ib = lv.inBlack, iw = std::max(lv.inBlack + 1, lv.inWhite);
    const double gamma = std::clamp(lv.gamma, 0.01, 10.0);
    for (int i = 0; i < 256; ++i) {
        double v = std::clamp((i - ib) / (iw - ib), 0.0, 1.0);
        v = std::pow(v, 1.0 / gamma);
        l[i] = quint8(clamp255(int(std::lround(lv.outBlack + v * (lv.outWhite - lv.outBlack)))));
    }
    return l;
}

QImage levels(const QImage& src, const Levels& lv, Channel channel)
{
    const Lut l = levelsLut(lv);
    const Lut id = identityLut();
    switch (channel) {
    case Channel::Red: return applyLuts(src, l, id, id);
    case Channel::Green: return applyLuts(src, id, l, id);
    case Channel::Blue: return applyLuts(src, id, id, l);
    case Channel::RGB: break;
    }
    return applyLuts(src, l, l, l);
}

Lut curveLut(QVector<QPointF> pts)
{
    std::sort(pts.begin(), pts.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    // Drop duplicate x values; interpolation needs strictly increasing x.
    QVector<QPointF> p;
    for (const QPointF& q : pts) {
        if (!p.isEmpty() && q.x() - p.last().x() < 1e-6)
            p.last() = q;
        else
            p.push_back(q);
    }
    if (p.size() < 2)
        return identityLut();
    const int n = p.size();
    std::vector<double> dx(n - 1), slope(n - 1), m(n);
    for (int i = 0; i < n - 1; ++i) {
        dx[i] = p[i + 1].x() - p[i].x();
        slope[i] = (p[i + 1].y() - p[i].y()) / dx[i];
    }
    m[0] = slope[0];
    m[n - 1] = slope[n - 2];
    for (int i = 1; i < n - 1; ++i)
        m[i] = (slope[i - 1] * slope[i] <= 0) ? 0.0 : (slope[i - 1] + slope[i]) / 2.0;
    // Fritsch–Carlson limiter keeps the curve monotone between points.
    for (int i = 0; i < n - 1; ++i) {
        if (std::abs(slope[i]) < 1e-12) {
            m[i] = m[i + 1] = 0.0;
            continue;
        }
        const double a = m[i] / slope[i], b = m[i + 1] / slope[i];
        const double s = a * a + b * b;
        if (s > 9.0) {
            const double t = 3.0 / std::sqrt(s);
            m[i] = t * a * slope[i];
            m[i + 1] = t * b * slope[i];
        }
    }
    Lut l;
    for (int x = 0; x < 256; ++x) {
        double y;
        if (x <= p.first().x()) {
            y = p.first().y();
        } else if (x >= p.last().x()) {
            y = p.last().y();
        } else {
            int k = 0;
            while (k < n - 2 && x > p[k + 1].x())
                ++k;
            const double h = dx[k];
            const double t = (x - p[k].x()) / h;
            const double t2 = t * t, t3 = t2 * t;
            y = (2 * t3 - 3 * t2 + 1) * p[k].y() + (t3 - 2 * t2 + t) * h * m[k] + (-2 * t3 + 3 * t2) * p[k + 1].y() +
                (t3 - t2) * h * m[k + 1];
        }
        l[x] = quint8(clamp255(int(std::lround(y))));
    }
    return l;
}

QImage blendWithMask(const QImage& original, const QImage& processed, const QImage& mask)
{
    if (mask.isNull())
        return processed.convertToFormat(kFmt);
    QImage out = prepared(original);
    const QImage proc = processed.convertToFormat(kFmt);
    const QImage m = mask.convertToFormat(QImage::Format_Grayscale8);
    parallelFor(out.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            QRgb* o = reinterpret_cast<QRgb*>(out.scanLine(y));
            const QRgb* p = reinterpret_cast<const QRgb*>(proc.constScanLine(y));
            const uchar* mk = m.constScanLine(y);
            for (int x = 0; x < out.width(); ++x) {
                const int k = mk[x];
                if (k == 0)
                    continue;
                if (k == 255) {
                    o[x] = p[x];
                    continue;
                }
                auto mix = [k](int a, int b) { return a + ((b - a) * k + 127) / 255; };
                o[x] = qRgba(mix(qRed(o[x]), qRed(p[x])), mix(qGreen(o[x]), qGreen(p[x])),
                             mix(qBlue(o[x]), qBlue(p[x])), mix(qAlpha(o[x]), qAlpha(p[x])));
            }
        }
    });
    return out;
}

QImage clearWithMask(const QImage& src, const QImage& mask)
{
    QImage cleared(src.size(), kFmt);
    cleared.fill(Qt::transparent);
    return blendWithMask(src, cleared, mask);
}

QImage fillWithMask(const QImage& src, const QImage& mask, QColor color, qreal opacity)
{
    QImage out = prepared(src);
    const QImage m = mask.isNull() ? QImage() : mask.convertToFormat(QImage::Format_Grayscale8);
    const QRgb c = color.rgba();
    const int baseA = int(std::lround(qAlpha(c) * std::clamp(opacity, 0.0, 1.0)));
    parallelFor(out.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            QRgb* o = reinterpret_cast<QRgb*>(out.scanLine(y));
            const uchar* mk = m.isNull() ? nullptr : m.constScanLine(y);
            for (int x = 0; x < out.width(); ++x) {
                const int a = mk ? (baseA * mk[x] + 127) / 255 : baseA;
                if (a == 0)
                    continue;
                const int sr = (qRed(c) * a + 127) / 255, sg = (qGreen(c) * a + 127) / 255,
                          sb = (qBlue(c) * a + 127) / 255;
                const int inv = 255 - a;
                const QRgb d = o[x];
                o[x] = qRgba(sr + (qRed(d) * inv + 127) / 255, sg + (qGreen(d) * inv + 127) / 255,
                             sb + (qBlue(d) * inv + 127) / 255, a + (qAlpha(d) * inv + 127) / 255);
            }
        }
    });
    return out;
}

} // namespace mf::filters
