#pragma once

#include "media/Timeline.h"

#include <QHash>
#include <QImage>

#include <memory>
#include <string>
#include <unordered_map>

namespace mf {

class VideoDecoder;

// Composes what the export would show at time t: the main-track frame
// letterboxed into the output size, plus active image overlays.
// One instance per thread (it owns decoders).
class FrameRenderer {
public:
    FrameRenderer();
    ~FrameRenderer();

    QImage render(const TimelineState& st, double t, QSize outSize, bool highQuality = false);
    void dropUnused(const TimelineState& st);

private:
    QImage still(const QString& path, QSize box);
    VideoDecoder* decoder(const QString& path);

    std::unordered_map<std::string, std::unique_ptr<VideoDecoder>> m_decoders;
    QHash<QString, QImage> m_stills;   // source images
    QHash<QString, QImage> m_scaled;   // per target size
};

} // namespace mf
