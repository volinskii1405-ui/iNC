#include "image/FloodFill.h"

#include <cstdlib>
#include <vector>

namespace mf {

namespace {
inline bool similar(QRgb a, QRgb b, int tol)
{
    return std::abs(qRed(a) - qRed(b)) <= tol && std::abs(qGreen(a) - qGreen(b)) <= tol &&
           std::abs(qBlue(a) - qBlue(b)) <= tol && std::abs(qAlpha(a) - qAlpha(b)) <= tol;
}
} // namespace

QImage floodFillMask(const QImage& src, QPoint seed, int tolerance, bool contiguous)
{
    const QImage img = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage mask(img.size(), QImage::Format_Grayscale8);
    mask.fill(0);
    if (!img.rect().contains(seed))
        return mask;
    const int w = img.width(), h = img.height();
    auto px = [&](int x, int y) { return reinterpret_cast<const QRgb*>(img.constScanLine(y))[x]; };
    const QRgb target = px(seed.x(), seed.y());

    if (!contiguous) {
        for (int y = 0; y < h; ++y) {
            uchar* m = mask.scanLine(y);
            for (int x = 0; x < w; ++x)
                if (similar(px(x, y), target, tolerance))
                    m[x] = 255;
        }
        return mask;
    }

    // Scanline fill: each stack entry is a seed; we expand it into a horizontal span.
    std::vector<QPoint> stack;
    stack.push_back(seed);
    auto marked = [&](int x, int y) { return mask.constScanLine(y)[x] != 0; };
    auto matches = [&](int x, int y) { return !marked(x, y) && similar(px(x, y), target, tolerance); };
    while (!stack.empty()) {
        const QPoint p = stack.back();
        stack.pop_back();
        int y = p.y();
        if (!matches(p.x(), y))
            continue;
        int x0 = p.x(), x1 = p.x();
        while (x0 > 0 && matches(x0 - 1, y))
            --x0;
        while (x1 < w - 1 && matches(x1 + 1, y))
            ++x1;
        uchar* m = mask.scanLine(y);
        for (int x = x0; x <= x1; ++x)
            m[x] = 255;
        for (int ny : {y - 1, y + 1}) {
            if (ny < 0 || ny >= h)
                continue;
            bool inSpan = false;
            for (int x = x0; x <= x1; ++x) {
                const bool ok = matches(x, ny);
                if (ok && !inSpan)
                    stack.push_back(QPoint(x, ny));
                inSpan = ok;
            }
        }
    }
    return mask;
}

} // namespace mf
