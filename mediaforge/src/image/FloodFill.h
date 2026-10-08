#pragma once

#include <QImage>

namespace mf {

// Returns a Grayscale8 mask (0/255) of pixels similar to the one at seed.
// tolerance is the max per-channel difference (0..255).
QImage floodFillMask(const QImage& image, QPoint seed, int tolerance, bool contiguous);

} // namespace mf
