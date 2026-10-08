#include "media/VideoDecoder.h"

#include <QFileInfo>
#include <QTransform>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>

namespace mf {

VideoDecoder::~VideoDecoder()
{
    close();
}

void VideoDecoder::close()
{
    if (m_sws)
        sws_freeContext(m_sws);
    m_sws = nullptr;
    av_frame_free(&m_cur);
    av_frame_free(&m_pending);
    av_packet_free(&m_pkt);
    avcodec_free_context(&m_dec);
    avformat_close_input(&m_fmt);
    m_stream = -1;
    m_hasCur = m_hasPending = false;
    m_curPts = -1.0;
}

bool VideoDecoder::open(const QString& path, QString* error, int threads)
{
    close();
    auto fail = [&](const QString& msg) {
        close();
        if (error)
            *error = msg;
        return false;
    };
    if (!probeMedia(path, &m_info, error))
        return false;
    if (!m_info.hasVideo)
        return fail(QStringLiteral("В файле «%1» нет видеодорожки.").arg(QFileInfo(path).fileName()));

    int r = avformat_open_input(&m_fmt, path.toUtf8().constData(), nullptr, nullptr);
    if (r < 0)
        return fail(avErrorText(r));
    if ((r = avformat_find_stream_info(m_fmt, nullptr)) < 0)
        return fail(avErrorText(r));
    const AVCodec* codec = nullptr;
    m_stream = av_find_best_stream(m_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (m_stream < 0 || !codec)
        return fail(QStringLiteral("Нет декодера для видео «%1» (кодек %2).")
                        .arg(QFileInfo(path).fileName(), m_info.videoCodec));
    AVStream* st = m_fmt->streams[m_stream];
    m_dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_dec, st->codecpar);
    m_dec->thread_count = threads;
    m_dec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    if ((r = avcodec_open2(m_dec, codec, nullptr)) < 0)
        return fail(QStringLiteral("Не удалось открыть декодер %1: %2").arg(m_info.videoCodec, avErrorText(r)));
    m_timeBase = av_q2d(st->time_base);
    // Times are relative to the container start, the same origin ffmpeg's -ss uses.
    m_startOffset = m_fmt->start_time != AV_NOPTS_VALUE ? m_fmt->start_time / double(AV_TIME_BASE) : 0.0;
    m_cur = av_frame_alloc();
    m_pending = av_frame_alloc();
    m_pkt = av_packet_alloc();
    m_eofSent = m_eof = false;
    return true;
}

double VideoDecoder::framePts(const AVFrame* f) const
{
    int64_t ts = f->best_effort_timestamp;
    if (ts == AV_NOPTS_VALUE)
        ts = f->pts;
    if (ts == AV_NOPTS_VALUE)
        return m_hasCur ? m_curPts + 1.0 / std::max(1.0, m_info.fps) : 0.0;
    return ts * m_timeBase - m_startOffset;
}

bool VideoDecoder::seekTo(double t)
{
    const int64_t ts = int64_t(std::floor((std::max(0.0, t) + m_startOffset) / m_timeBase));
    if (av_seek_frame(m_fmt, m_stream, ts, AVSEEK_FLAG_BACKWARD) < 0)
        av_seek_frame(m_fmt, -1, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_dec);
    m_hasCur = m_hasPending = false;
    m_eofSent = m_eof = false;
    m_curPts = -1.0;
    return true;
}

bool VideoDecoder::decodeInto(AVFrame* frame)
{
    for (;;) {
        int r = avcodec_receive_frame(m_dec, frame);
        if (r == 0)
            return true;
        if (r == AVERROR_EOF) {
            m_eof = true;
            return false;
        }
        if (r != AVERROR(EAGAIN))
            return false;
        if (m_eofSent) {
            m_eof = true;
            return false;
        }
        r = av_read_frame(m_fmt, m_pkt);
        if (r < 0) {
            avcodec_send_packet(m_dec, nullptr);
            m_eofSent = true;
            continue;
        }
        if (m_pkt->stream_index == m_stream)
            avcodec_send_packet(m_dec, m_pkt); // corrupt packets are skipped; decoding continues
        av_packet_unref(m_pkt);
    }
}

QImage VideoDecoder::frameAt(double t, QSize box, bool highQuality)
{
    if (!isOpen())
        return QImage();
    const double frameDur = 1.0 / std::max(1.0, m_info.fps);
    const double eps = std::min(1e-3, frameDur / 4);
    // Backwards or far ahead: seeking is cheaper than decoding through.
    if (!m_hasCur || t < m_curPts - eps || t > m_curPts + 3.0)
        seekTo(t);

    for (;;) {
        if (!m_hasPending) {
            if (m_eof || !decodeInto(m_pending))
                break;
            m_hasPending = true;
            m_pendingPts = framePts(m_pending);
        }
        if (!m_hasCur || m_pendingPts <= t + eps) {
            std::swap(m_cur, m_pending);
            av_frame_unref(m_pending);
            m_hasCur = true;
            m_curPts = m_pendingPts;
            m_hasPending = false;
            continue;
        }
        break;
    }
    if (!m_hasCur)
        return QImage();
    return convert(m_cur, box, highQuality);
}

QImage VideoDecoder::convert(const AVFrame* f, QSize box, bool highQuality)
{
    const bool swap = m_info.rotation == 90 || m_info.rotation == 270;
    // Display size before rotation (pixel aspect applied).
    double dw = swap ? m_info.height : m_info.width;
    double dh = swap ? m_info.width : m_info.height;
    if (dw <= 0 || dh <= 0) {
        dw = f->width;
        dh = f->height;
    }
    int ow = int(std::lround(dw)), oh = int(std::lround(dh));
    if (box.isValid() && !box.isEmpty()) {
        const QSize b = swap ? box.transposed() : box;
        const double s = std::min(b.width() / dw, b.height() / dh);
        ow = std::max(1, int(std::lround(dw * s)));
        oh = std::max(1, int(std::lround(dh * s)));
    }
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(AVPixelFormat(f->format));
    const bool alpha = desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA);
    m_sws = sws_getCachedContext(m_sws, f->width, f->height, AVPixelFormat(f->format), ow, oh, AV_PIX_FMT_RGB32,
                                 highQuality ? SWS_BICUBIC : SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_sws)
        return QImage();
    QImage img(ow, oh, alpha ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    uint8_t* dst[4] = {img.bits(), nullptr, nullptr, nullptr};
    int dstStride[4] = {int(img.bytesPerLine()), 0, 0, 0};
    sws_scale(m_sws, f->data, f->linesize, 0, f->height, dst, dstStride);
    if (m_info.rotation != 0)
        img = img.transformed(QTransform().rotate(m_info.rotation));
    return img;
}

} // namespace mf
