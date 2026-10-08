#include "media/Audio.h"

#include "image/ImageIO.h"
#include "media/MediaInfo.h"

#include <QFileInfo>
#include <QSaveFile>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mf {

qint64 PcmBuffer::frameAt(double seconds) const
{
    return std::clamp<qint64>(qint64(std::llround(seconds * rate)), 0, frames());
}

QString atempoChain(double speed)
{
    speed = std::clamp(speed, 0.25, 4.0);
    QStringList parts;
    // Each atempo stage stays within 0.5..2 where its quality is best.
    while (speed > 2.0 + 1e-9) {
        parts << QStringLiteral("atempo=2");
        speed /= 2.0;
    }
    while (speed < 0.5 - 1e-9) {
        parts << QStringLiteral("atempo=0.5");
        speed /= 0.5;
    }
    if (std::abs(speed - 1.0) > 1e-9 || parts.isEmpty())
        parts << QStringLiteral("atempo=%1").arg(QString::number(speed, 'g', 10));
    return parts.join(',');
}

bool decodeAudio(const QString& path, PcmBuffer* out, QString* error, const ProgressFn& progress, int rate, int channels)
{
    AVFormatContext* fmt = nullptr;
    AVCodecContext* dec = nullptr;
    SwrContext* swr = nullptr;
    AVPacket* pkt = nullptr;
    AVFrame* frame = nullptr;
    auto cleanup = [&] {
        swr_free(&swr);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
    };
    auto fail = [&](const QString& msg) {
        cleanup();
        if (error)
            *error = msg;
        return false;
    };
    const QString name = QFileInfo(path).fileName();

    int r = avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr);
    if (r < 0)
        return fail(QStringLiteral("Не удалось открыть «%1»: %2").arg(name, avErrorText(r)));
    if ((r = avformat_find_stream_info(fmt, nullptr)) < 0)
        return fail(QStringLiteral("Не удалось прочитать «%1»: %2").arg(name, avErrorText(r)));
    const AVCodec* codec = nullptr;
    const int si = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (si < 0)
        return fail(QStringLiteral("В файле «%1» нет звуковой дорожки.").arg(name));
    if (!codec)
        return fail(QStringLiteral("Нет декодера для звука в «%1».").arg(name));
    AVStream* st = fmt->streams[si];
    dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, st->codecpar);
    if ((r = avcodec_open2(dec, codec, nullptr)) < 0)
        return fail(QStringLiteral("Не удалось открыть аудиодекодер: %1").arg(avErrorText(r)));
    if (dec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
        av_channel_layout_default(&dec->ch_layout, std::max(1, dec->ch_layout.nb_channels));

    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, channels);
    r = swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, rate, &dec->ch_layout, dec->sample_fmt,
                            dec->sample_rate, 0, nullptr);
    if (r < 0 || swr_init(swr) < 0)
        return fail(QStringLiteral("Не удалось настроить преобразование звука."));

    const double total = fmt->duration != AV_NOPTS_VALUE ? fmt->duration / double(AV_TIME_BASE) : 0.0;
    PcmBuffer pcm;
    pcm.rate = rate;
    pcm.channels = channels;
    if (total > 0)
        pcm.data.reserve(qsizetype(total * rate * channels * 1.02) + 4096);
    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    const double tb = av_q2d(st->time_base);
    double lastReport = 0;

    auto pushConverted = [&](const AVFrame* f) {
        const int inSamples = f ? f->nb_samples : 0;
        const int maxOut = swr_get_out_samples(swr, inSamples) + 64;
        if (maxOut <= 0)
            return;
        const qsizetype base = pcm.data.size();
        pcm.data.resize(base + qsizetype(maxOut) * channels);
        uint8_t* dst = reinterpret_cast<uint8_t*>(pcm.data.data() + base);
        const int got = swr_convert(swr, &dst, maxOut, f ? const_cast<const uint8_t**>(f->extended_data) : nullptr,
                                    inSamples);
        pcm.data.resize(base + qsizetype(std::max(0, got)) * channels);
    };

    bool sentEof = false;
    for (;;) {
        r = avcodec_receive_frame(dec, frame);
        if (r == 0) {
            pushConverted(frame);
            if (progress && total > 0 && frame->pts != AV_NOPTS_VALUE) {
                const double p = std::clamp(frame->pts * tb / total, 0.0, 1.0);
                if (p - lastReport > 0.01) {
                    lastReport = p;
                    if (!progress(p))
                        return fail(QStringLiteral("Отменено."));
                }
            }
            av_frame_unref(frame);
            continue;
        }
        if (r == AVERROR_EOF)
            break;
        if (r != AVERROR(EAGAIN))
            break; // decoder error: keep what we have
        if (sentEof)
            break;
        r = av_read_frame(fmt, pkt);
        if (r < 0) {
            avcodec_send_packet(dec, nullptr);
            sentEof = true;
            continue;
        }
        if (pkt->stream_index == si)
            avcodec_send_packet(dec, pkt);
        av_packet_unref(pkt);
    }
    pushConverted(nullptr); // flush resampler
    cleanup();
    if (pcm.isEmpty()) {
        if (error)
            *error = QStringLiteral("Не удалось декодировать звук из «%1»: файл повреждён или пуст.").arg(name);
        return false;
    }
    if (progress)
        progress(1.0);
    *out = std::move(pcm);
    return true;
}

QVector<float> computePeaks(const PcmBuffer& pcm, int framesPerBucket)
{
    QVector<float> peaks;
    framesPerBucket = std::max(1, framesPerBucket);
    const qint64 n = pcm.frames();
    const int ch = pcm.channels;
    peaks.reserve(int(2 * (n / framesPerBucket + 1)));
    for (qint64 b = 0; b < n; b += framesPerBucket) {
        float mn = 0.f, mx = 0.f;
        const qint64 e = std::min(n, b + framesPerBucket);
        for (qint64 i = b; i < e; ++i) {
            float v = 0.f;
            for (int c = 0; c < ch; ++c)
                v += pcm.data[i * ch + c];
            v /= ch;
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
        peaks << mn << mx;
    }
    return peaks;
}

namespace audioops {

namespace {
void clampRange(const PcmBuffer& pcm, qint64& a, qint64& b)
{
    a = std::clamp<qint64>(a, 0, pcm.frames());
    b = std::clamp<qint64>(b, 0, pcm.frames());
    if (b < a)
        std::swap(a, b);
}
} // namespace

PcmBuffer slice(const PcmBuffer& pcm, qint64 a, qint64 b)
{
    clampRange(pcm, a, b);
    PcmBuffer out;
    out.rate = pcm.rate;
    out.channels = pcm.channels;
    out.data = pcm.data.mid(a * pcm.channels, (b - a) * pcm.channels);
    return out;
}

void removeRange(PcmBuffer& pcm, qint64 a, qint64 b)
{
    clampRange(pcm, a, b);
    pcm.data.remove(a * pcm.channels, (b - a) * pcm.channels);
}

void insert(PcmBuffer& pcm, qint64 at, const PcmBuffer& other)
{
    at = std::clamp<qint64>(at, 0, pcm.frames());
    QVector<float> merged;
    merged.reserve(pcm.data.size() + other.data.size());
    merged.append(pcm.data.mid(0, at * pcm.channels));
    merged.append(other.data);
    merged.append(pcm.data.mid(at * pcm.channels));
    pcm.data = std::move(merged);
}

void silence(PcmBuffer& pcm, qint64 a, qint64 b)
{
    clampRange(pcm, a, b);
    std::fill(pcm.data.begin() + a * pcm.channels, pcm.data.begin() + b * pcm.channels, 0.f);
}

void gain(PcmBuffer& pcm, qint64 a, qint64 b, float factor)
{
    clampRange(pcm, a, b);
    for (qint64 i = a * pcm.channels; i < b * pcm.channels; ++i)
        pcm.data[i] = std::clamp(pcm.data[i] * factor, -1.f, 1.f);
}

void fadeIn(PcmBuffer& pcm, qint64 a, qint64 b)
{
    clampRange(pcm, a, b);
    const qint64 len = b - a;
    if (len <= 0)
        return;
    for (qint64 i = 0; i < len; ++i) {
        const float g = float(i) / float(len);
        for (int c = 0; c < pcm.channels; ++c)
            pcm.data[(a + i) * pcm.channels + c] *= g;
    }
}

void fadeOut(PcmBuffer& pcm, qint64 a, qint64 b)
{
    clampRange(pcm, a, b);
    const qint64 len = b - a;
    if (len <= 0)
        return;
    for (qint64 i = 0; i < len; ++i) {
        const float g = 1.f - float(i + 1) / float(len);
        for (int c = 0; c < pcm.channels; ++c)
            pcm.data[(a + i) * pcm.channels + c] *= g;
    }
}

float peak(const PcmBuffer& pcm, qint64 a, qint64 b)
{
    clampRange(pcm, a, b);
    float p = 0.f;
    for (qint64 i = a * pcm.channels; i < b * pcm.channels; ++i)
        p = std::max(p, std::abs(pcm.data[i]));
    return p;
}

bool changeSpeed(const PcmBuffer& in, double speed, PcmBuffer* out, QString* error)
{
    auto fail = [error](const QString& msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (speed < 0.25 - 1e-9 || speed > 4.0 + 1e-9)
        return fail(QStringLiteral("Скорость должна быть в диапазоне 0.25–4."));
    if (std::abs(speed - 1.0) < 1e-9) {
        *out = in;
        return true;
    }
    AVFilterGraph* graph = avfilter_graph_alloc();
    AVFilterContext* src = nullptr;
    AVFilterContext* sink = nullptr;
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* got = av_frame_alloc();
    auto cleanup = [&] {
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        av_frame_free(&frame);
        av_frame_free(&got);
        avfilter_graph_free(&graph);
    };
    const char* layoutName = in.channels == 1 ? "mono" : "stereo";
    const QByteArray srcArgs = QStringLiteral("time_base=1/%1:sample_rate=%1:sample_fmt=flt:channel_layout=%2")
                                   .arg(in.rate)
                                   .arg(QLatin1String(layoutName))
                                   .toLatin1();
    int r = avfilter_graph_create_filter(&src, avfilter_get_by_name("abuffer"), "in", srcArgs.constData(), nullptr, graph);
    if (r >= 0)
        r = avfilter_graph_create_filter(&sink, avfilter_get_by_name("abuffersink"), "out", nullptr, nullptr, graph);
    if (r < 0) {
        cleanup();
        return fail(QStringLiteral("Не удалось создать фильтр скорости: %1").arg(avErrorText(r)));
    }
    outputs->name = av_strdup("in");
    outputs->filter_ctx = src;
    outputs->pad_idx = 0;
    outputs->next = nullptr;
    inputs->name = av_strdup("out");
    inputs->filter_ctx = sink;
    inputs->pad_idx = 0;
    inputs->next = nullptr;
    const QByteArray desc =
        QStringLiteral("%1,aformat=sample_fmts=flt:channel_layouts=%2").arg(atempoChain(speed), QLatin1String(layoutName)).toLatin1();
    r = avfilter_graph_parse_ptr(graph, desc.constData(), &inputs, &outputs, nullptr);
    if (r >= 0)
        r = avfilter_graph_config(graph, nullptr);
    if (r < 0) {
        cleanup();
        return fail(QStringLiteral("Не удалось настроить фильтр скорости: %1").arg(avErrorText(r)));
    }

    PcmBuffer res;
    res.rate = in.rate;
    res.channels = in.channels;
    res.data.reserve(qsizetype(in.data.size() / speed) + 8192);
    auto drain = [&]() {
        for (;;) {
            const int rr = av_buffersink_get_frame(sink, got);
            if (rr < 0)
                return rr;
            const float* p = reinterpret_cast<const float*>(got->data[0]);
            const qsizetype n = qsizetype(got->nb_samples) * in.channels;
            const qsizetype base = res.data.size();
            res.data.resize(base + n);
            std::memcpy(res.data.data() + base, p, size_t(n) * sizeof(float));
            av_frame_unref(got);
        }
    };

    const qint64 total = in.frames();
    const int chunk = 4096;
    AVChannelLayout layout;
    av_channel_layout_default(&layout, in.channels);
    for (qint64 pos = 0; pos < total; pos += chunk) {
        const int n = int(std::min<qint64>(chunk, total - pos));
        frame->nb_samples = n;
        frame->format = AV_SAMPLE_FMT_FLT;
        frame->sample_rate = in.rate;
        av_channel_layout_copy(&frame->ch_layout, &layout);
        frame->pts = pos;
        if (av_frame_get_buffer(frame, 0) < 0) {
            cleanup();
            return fail(QStringLiteral("Недостаточно памяти."));
        }
        std::memcpy(frame->data[0], in.data.constData() + pos * in.channels, size_t(n) * in.channels * sizeof(float));
        r = av_buffersrc_add_frame(src, frame);
        av_frame_unref(frame);
        if (r < 0) {
            cleanup();
            return fail(QStringLiteral("Ошибка фильтра скорости: %1").arg(avErrorText(r)));
        }
        drain();
    }
    if (av_buffersrc_add_frame(src, nullptr) < 0) {
        cleanup();
        return fail(QStringLiteral("Ошибка завершения фильтра скорости."));
    }
    drain();
    cleanup();
    *out = std::move(res);
    return true;
}

} // namespace audioops

bool writeWav(const PcmBuffer& pcm, const QString& path, QString* error)
{
    auto fail = [error](const QString& msg) {
        if (error)
            *error = msg;
        return false;
    };
    const quint32 dataBytes = quint32(pcm.data.size() * sizeof(float));
    QString err;
    if (!imageio::hasFreeSpace(path, qint64(dataBytes) + 4096, &err))
        return fail(err);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("Не удалось создать файл: %1").arg(f.errorString()));
    auto u32 = [&](quint32 v) {
        const char b[4] = {char(v & 0xff), char((v >> 8) & 0xff), char((v >> 16) & 0xff), char((v >> 24) & 0xff)};
        f.write(b, 4);
    };
    auto u16 = [&](quint16 v) {
        const char b[2] = {char(v & 0xff), char((v >> 8) & 0xff)};
        f.write(b, 2);
    };
    f.write("RIFF", 4);
    u32(4 + 26 + 12 + 8 + dataBytes);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    u32(18);
    u16(3); // IEEE float
    u16(quint16(pcm.channels));
    u32(quint32(pcm.rate));
    u32(quint32(pcm.rate * pcm.channels * 4));
    u16(quint16(pcm.channels * 4));
    u16(32);
    u16(0);
    f.write("fact", 4);
    u32(4);
    u32(quint32(pcm.frames()));
    f.write("data", 4);
    u32(dataBytes);
    if (f.write(reinterpret_cast<const char*>(pcm.data.constData()), dataBytes) != qint64(dataBytes)) {
        const QString e = f.errorString();
        f.cancelWriting();
        return fail(QStringLiteral("Ошибка записи WAV: %1").arg(e));
    }
    if (!f.commit())
        return fail(QStringLiteral("Не удалось сохранить WAV: %1").arg(f.errorString()));
    return true;
}

} // namespace mf
