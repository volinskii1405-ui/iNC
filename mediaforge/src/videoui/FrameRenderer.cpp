#include "videoui/FrameRenderer.h"

#include "image/ImageIO.h"
#include "media/VideoDecoder.h"

#include <QPainter>

#include <cmath>
#include <unordered_set>

namespace mf {

FrameRenderer::FrameRenderer() = default;
FrameRenderer::~FrameRenderer() = default;

VideoDecoder* FrameRenderer::decoder(const QString& path)
{
    const std::string key = path.toStdString();
    auto it = m_decoders.find(key);
    if (it != m_decoders.end())
        return it->second.get();
    auto dec = std::make_unique<VideoDecoder>();
    QString err;
    if (!dec->open(path, &err))
        dec.reset();
    VideoDecoder* raw = dec.get();
    m_decoders[key] = std::move(dec);
    return raw;
}

QImage FrameRenderer::still(const QString& path, QSize box)
{
    const QString key = QStringLiteral("%1|%2x%3").arg(path).arg(box.width()).arg(box.height());
    auto it = m_scaled.find(key);
    if (it != m_scaled.end())
        return it.value();
    if (!m_stills.contains(path)) {
        const auto res = imageio::loadImage(path);
        m_stills.insert(path, res.image);
    }
    const QImage src = m_stills.value(path);
    if (src.isNull())
        return QImage();
    const QImage scaled = src.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (m_scaled.size() > 64)
        m_scaled.clear();
    m_scaled.insert(key, scaled);
    return scaled;
}

QImage FrameRenderer::render(const TimelineState& st, double t, QSize outSize, bool highQuality)
{
    QImage frame(outSize, QImage::Format_RGB32);
    frame.fill(Qt::black);
    QPainter p(&frame);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    double clipStart = 0.0;
    const int idx = st.mainIndexAt(t, &clipStart);
    if (idx >= 0) {
        const Clip& c = st.main[idx];
        QImage img;
        if (c.kind == ClipKind::Image) {
            img = still(c.path, outSize);
        } else if (VideoDecoder* dec = decoder(c.path)) {
            img = dec->frameAt(c.sourceTime(t - clipStart), outSize, highQuality);
        }
        if (!img.isNull())
            p.drawImage((outSize.width() - img.width()) / 2, (outSize.height() - img.height()) / 2, img);
    }
    for (const Clip& o : st.overlays) {
        if (t < o.start || t >= o.end())
            continue;
        const int w = std::max(2, int(std::lround(o.width * outSize.width())));
        const QImage img = still(o.path, QSize(w, 100000));
        if (img.isNull())
            continue;
        p.setOpacity(o.opacity);
        p.drawImage(QPoint(int(std::lround(o.x * outSize.width())), int(std::lround(o.y * outSize.height()))), img);
        p.setOpacity(1.0);
    }
    return frame;
}

void FrameRenderer::dropUnused(const TimelineState& st)
{
    std::unordered_set<std::string> used;
    for (const Clip& c : st.main)
        used.insert(c.path.toStdString());
    for (auto it = m_decoders.begin(); it != m_decoders.end();) {
        if (!used.count(it->first))
            it = m_decoders.erase(it);
        else
            ++it;
    }
}

} // namespace mf
