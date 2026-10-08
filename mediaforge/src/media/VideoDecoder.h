#pragma once

#include "media/MediaInfo.h"

#include <QImage>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace mf {

// Decodes frames of one file. Not thread-safe: each thread owns its decoders.
// Sequential requests (playback) decode forward without seeking; jumps seek
// to the previous keyframe and decode up to the requested time.
class VideoDecoder {
public:
    VideoDecoder() = default;
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool open(const QString& path, QString* error, int threads = 0);
    void close();
    bool isOpen() const { return m_dec != nullptr; }
    const MediaInfo& info() const { return m_info; }

    // Frame displayed at time t (seconds from the start of the file), scaled
    // to fit inside box keeping aspect ratio; an empty box means full size.
    QImage frameAt(double t, QSize box = QSize(), bool highQuality = false);
    double lastFrameTime() const { return m_curPts; }

private:
    bool seekTo(double t);
    bool decodeInto(AVFrame* frame);
    double framePts(const AVFrame* frame) const;
    QImage convert(const AVFrame* frame, QSize box, bool highQuality);

    MediaInfo m_info;
    AVFormatContext* m_fmt = nullptr;
    AVCodecContext* m_dec = nullptr;
    AVFrame* m_cur = nullptr;
    AVFrame* m_pending = nullptr;
    AVPacket* m_pkt = nullptr;
    SwsContext* m_sws = nullptr;
    int m_stream = -1;
    double m_timeBase = 0.0;
    double m_startOffset = 0.0;
    double m_curPts = -1.0;
    double m_pendingPts = 0.0;
    bool m_hasCur = false;
    bool m_hasPending = false;
    bool m_eofSent = false;
    bool m_eof = false;
};

} // namespace mf
