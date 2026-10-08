#include "media/ExportBuilder.h"

#include "image/ImageIO.h"
#include "media/Audio.h"

#include <QDir>
#include <QFileInfo>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace mf {

QString num(double v)
{
    QString s = QString::number(v, 'f', 6);
    while (s.contains('.') && (s.endsWith('0') || s.endsWith('.')))
        s.chop(1);
    return s.isEmpty() || s == "-" ? QStringLiteral("0") : s;
}

QStringList commonFFmpegArgs()
{
    return {"-hide_banner", "-nostdin", "-y", "-loglevel", "error", "-progress", "pipe:1", "-nostats"};
}

namespace {

bool renderStill(const QString& src, QSize frame, const QString& dst, QString* error)
{
    const auto loaded = imageio::loadImage(src);
    if (!loaded.error.isEmpty()) {
        *error = QStringLiteral("%1: %2").arg(QFileInfo(src).fileName(), loaded.error);
        return false;
    }
    QImage canvas(frame, QImage::Format_RGB32);
    canvas.fill(Qt::black);
    const QImage scaled = loaded.image.scaled(frame, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter p(&canvas);
    p.drawImage((frame.width() - scaled.width()) / 2, (frame.height() - scaled.height()) / 2, scaled);
    p.end();
    if (!canvas.save(dst, "PNG")) {
        *error = QStringLiteral("Не удалось записать временный файл %1 (нет места или прав?)").arg(dst);
        return false;
    }
    return true;
}

bool renderOverlay(const Clip& c, QSize frame, const QString& dst, QPoint* pos, QString* error)
{
    const auto loaded = imageio::loadImage(c.path);
    if (!loaded.error.isEmpty()) {
        *error = QStringLiteral("%1: %2").arg(QFileInfo(c.path).fileName(), loaded.error);
        return false;
    }
    const int w = std::max(2, int(std::lround(c.width * frame.width())));
    const int h = std::max(2, int(std::lround(double(w) * loaded.image.height() / std::max(1, loaded.image.width()))));
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setOpacity(c.opacity);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRect(0, 0, w, h), loaded.image);
    p.end();
    if (!out.save(dst, "PNG")) {
        *error = QStringLiteral("Не удалось записать временный файл %1 (нет места или прав?)").arg(dst);
        return false;
    }
    *pos = QPoint(int(std::lround(c.x * frame.width())), int(std::lround(c.y * frame.height())));
    return true;
}

QString audioFilters(const Clip& c, double len)
{
    QStringList f;
    f << "asetpts=PTS-STARTPTS";
    if (std::abs(c.speed - 1.0) > 1e-9)
        f << atempoChain(c.speed);
    if (std::abs(c.volume - 1.0) > 1e-9)
        f << "volume=" + num(c.volume);
    f << "aresample=48000"
      << "aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo"
      << "apad"
      << "atrim=duration=" + num(len);
    if (c.fadeIn > 1e-6)
        f << "afade=t=in:st=0:d=" + num(std::min(c.fadeIn, len));
    if (c.fadeOut > 1e-6)
        f << "afade=t=out:st=" + num(std::max(0.0, len - c.fadeOut)) + ":d=" + num(std::min(c.fadeOut, len));
    f << "asetpts=PTS-STARTPTS";
    return f.join(',');
}

QString silence(double len)
{
    return "anullsrc=r=48000:cl=stereo,atrim=duration=" + num(len) +
           ",aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo,asetpts=PTS-STARTPTS";
}

} // namespace

ExportPlan buildVideoExport(const TimelineState& timeline, const VideoExportSettings& settings, const QString& tempDir)
{
    ExportPlan plan;
    const bool audioOnly = settings.format == VideoFormat::AudioRaw;
    const bool wantAudio = settings.format == VideoFormat::MP4 || settings.format == VideoFormat::WEBM || audioOnly;
    const bool wantVideo = !audioOnly;

    TimelineState st = timeline;
    double total = st.duration();
    if (settings.rangeEnd >= 0) {
        const double a = std::clamp(settings.rangeStart, 0.0, total);
        const double b = std::clamp(settings.rangeEnd, a, total);
        st = subRange(timeline, a, b);
        total = b - a;
    }
    if (total < kMinClipLength) {
        plan.error = QStringLiteral("Нечего экспортировать: таймлайн или выбранный диапазон пуст.");
        return plan;
    }
    plan.duration = total;

    QSize size = settings.size.isEmpty() ? st.resolution : settings.size;
    size = QSize(std::max(2, size.width() & ~1), std::max(2, size.height() & ~1));
    const double fps = settings.fps > 0 ? settings.fps : st.fps;
    const QString W = QString::number(size.width()), H = QString::number(size.height()), F = num(fps);
    const QString fit = QStringLiteral("scale=%1:%2:force_original_aspect_ratio=decrease:force_divisible_by=2:flags=bicubic,"
                                       "pad=%1:%2:(ow-iw)/2:(oh-ih)/2:color=black,setsar=1,format=yuv420p")
                            .arg(W, H);

    QStringList inputs;
    QStringList graph;
    int inputCount = 0;
    auto addInput = [&](const QStringList& opts, const QString& path) {
        inputs << opts << "-i" << path;
        return inputCount++;
    };
    QDir tmp(tempDir);
    int tmpCounter = 0;
    auto tempPng = [&]() { return tmp.filePath(QStringLiteral("still_%1.png").arg(tmpCounter++)); };

    QStringList concatPads;
    int seg = 0;
    double mainLen = 0.0;
    for (const Clip& c : st.main) {
        const double len = c.length();
        mainLen += len;
        const QString v = QStringLiteral("v%1").arg(seg), a = QStringLiteral("a%1").arg(seg);
        const bool clipAudio = wantAudio && c.kind == ClipKind::Video && c.info.hasAudio && !c.muted &&
                               !st.muteOriginal && c.volume > 1e-6;
        int idx = -1;
        if (c.kind == ClipKind::Video && (wantVideo ? true : clipAudio))
            idx = addInput({"-ss", num(c.in), "-t", num(c.out - c.in)}, c.path);

        if (wantVideo) {
            if (c.kind == ClipKind::Image) {
                const QString png = tempPng();
                if (!renderStill(c.path, size, png, &plan.error))
                    return plan;
                const int k = addInput({"-loop", "1", "-framerate", F, "-t", num(len + 1.0 / fps)}, png);
                graph << QStringLiteral("[%1:v:0]fps=%2,setsar=1,format=yuv420p,tpad=stop_mode=clone:stop_duration=%3,"
                                        "trim=duration=%3,setpts=PTS-STARTPTS[%4]")
                             .arg(k)
                             .arg(F, num(len), v);
            } else if (c.info.hasVideo) {
                graph << QStringLiteral("[%1:v:0]setpts=(PTS-STARTPTS)/%2,fps=%3,%4,tpad=stop_mode=clone:stop_duration=%5,"
                                        "trim=duration=%5,setpts=PTS-STARTPTS[%6]")
                             .arg(idx)
                             .arg(num(c.speed), F, fit, num(len), v);
            } else {
                graph << QStringLiteral("color=c=black:s=%1x%2:r=%3:d=%4,setsar=1,format=yuv420p[%5]")
                             .arg(W, H, F, num(len), v);
            }
        }
        if (wantAudio)
            graph << (clipAudio ? QStringLiteral("[%1:a:0]%2[%3]").arg(idx).arg(audioFilters(c, len), a)
                                : QStringLiteral("%1[%2]").arg(silence(len), a));
        concatPads << (wantVideo ? "[" + v + "]" : QString()) + (wantAudio ? "[" + a + "]" : QString());
        ++seg;
    }
    // Black (and silent) tail when overlays or audio run past the main track.
    if (total - mainLen > 1e-3) {
        const double gap = total - mainLen;
        const QString v = QStringLiteral("v%1").arg(seg), a = QStringLiteral("a%1").arg(seg);
        if (wantVideo)
            graph << QStringLiteral("color=c=black:s=%1x%2:r=%3:d=%4,setsar=1,format=yuv420p[%5]")
                         .arg(W, H, F, num(gap), v);
        if (wantAudio)
            graph << QStringLiteral("%1[%2]").arg(silence(gap), a);
        concatPads << (wantVideo ? "[" + v + "]" : QString()) + (wantAudio ? "[" + a + "]" : QString());
        ++seg;
    }
    graph << QStringLiteral("%1concat=n=%2:v=%3:a=%4%5%6")
                 .arg(concatPads.join(QString()))
                 .arg(seg)
                 .arg(wantVideo ? 1 : 0)
                 .arg(wantAudio ? 1 : 0)
                 .arg(wantVideo ? "[vcat]" : "", wantAudio ? "[acat]" : "");

    if (wantVideo) {
        QString cur = QStringLiteral("vcat");
        int o = 0;
        for (const Clip& c : st.overlays) {
            const QString png = tempPng();
            QPoint pos;
            if (!renderOverlay(c, size, png, &pos, &plan.error))
                return plan;
            const int k = addInput({}, png);
            const QString next = QStringLiteral("ov%1").arg(o++);
            graph << QStringLiteral("[%1][%2:v:0]overlay=x=%3:y=%4:enable='between(t,%5,%6)'[%7]")
                         .arg(cur)
                         .arg(k)
                         .arg(pos.x())
                         .arg(pos.y())
                         .arg(num(c.start), num(c.end()), next);
            cur = next;
        }
        graph << QStringLiteral("[%1]format=yuv420p[vout]").arg(cur);
    }

    if (wantAudio) {
        QStringList mixPads{"[acat]"};
        int x = 0;
        for (const Clip& c : st.audio) {
            if (c.muted || c.volume <= 1e-6)
                continue;
            const int k = addInput({"-ss", num(c.in), "-t", num(c.out - c.in)}, c.path);
            const QString pad = QStringLiteral("x%1").arg(x++);
            const qint64 delayMs = qint64(std::llround(c.start * 1000.0));
            graph << QStringLiteral("[%1:a:0]%2,adelay=delays=%3:all=1[%4]")
                         .arg(k)
                         .arg(audioFilters(c, c.length()))
                         .arg(delayMs)
                         .arg(pad);
            mixPads << "[" + pad + "]";
        }
        if (mixPads.size() > 1)
            graph << QStringLiteral("%1amix=inputs=%2:duration=first:dropout_transition=0:normalize=0[aout]")
                         .arg(mixPads.join(QString()))
                         .arg(mixPads.size());
        else
            graph << QStringLiteral("[acat]anull[aout]");
    }

    QStringList args = commonFFmpegArgs();
    args << inputs;
    args << "-filter_complex" << graph.join(';');
    switch (settings.format) {
    case VideoFormat::MP4:
        args << "-map" << "[vout]" << "-map" << "[aout]";
        args << "-c:v" << "libx264" << "-preset" << settings.preset << "-crf" << QString::number(std::clamp(settings.crf, 0, 51))
             << "-pix_fmt" << "yuv420p" << "-r" << F;
        args << "-c:a" << "aac" << "-b:a" << QStringLiteral("%1k").arg(settings.audioBitrate) << "-ar" << "48000";
        args << "-movflags" << "+faststart";
        break;
    case VideoFormat::WEBM:
        args << "-map" << "[vout]" << "-map" << "[aout]";
        args << "-c:v" << "libvpx-vp9" << "-crf" << QString::number(std::clamp(settings.crf, 0, 63)) << "-b:v" << "0"
             << "-deadline" << "good" << "-cpu-used" << "4" << "-row-mt" << "1" << "-pix_fmt" << "yuv420p" << "-r" << F;
        args << "-c:a" << "libopus" << "-b:a" << QStringLiteral("%1k").arg(std::min(settings.audioBitrate, 256)) << "-ar" << "48000";
        break;
    case VideoFormat::PngSequence:
        args << "-map" << "[vout]" << "-c:v" << "png" << "-pix_fmt" << "rgb24" << "-start_number" << "0" << "-f" << "image2";
        break;
    case VideoFormat::AudioRaw:
        args << "-map" << "[aout]" << "-c:a" << "pcm_s16le" << "-ar" << "48000" << "-ac" << "2" << "-f" << "s16le";
        break;
    }
    args << "-t" << num(total);
    if (settings.format == VideoFormat::PngSequence)
        args << QDir(settings.output).filePath(QStringLiteral("frame_%05d.png"));
    else
        args << settings.output;
    plan.args = args;
    return plan;
}

QString audioSuffix(AudioFormat f)
{
    switch (f) {
    case AudioFormat::MP3: return QStringLiteral("mp3");
    case AudioFormat::WAV: return QStringLiteral("wav");
    case AudioFormat::FLAC: return QStringLiteral("flac");
    case AudioFormat::OGG: return QStringLiteral("ogg");
    case AudioFormat::AAC: return QStringLiteral("m4a");
    }
    return QStringLiteral("mp3");
}

QString audioFormatName(AudioFormat f)
{
    switch (f) {
    case AudioFormat::MP3: return QStringLiteral("MP3");
    case AudioFormat::WAV: return QStringLiteral("WAV");
    case AudioFormat::FLAC: return QStringLiteral("FLAC");
    case AudioFormat::OGG: return QStringLiteral("OGG Vorbis");
    case AudioFormat::AAC: return QStringLiteral("AAC");
    }
    return QString();
}

QStringList audioCodecArgs(const AudioExportSettings& s)
{
    QStringList a;
    const QString br = QStringLiteral("%1k").arg(std::clamp(s.bitrate, 32, 320));
    switch (s.format) {
    case AudioFormat::MP3: a << "-c:a" << "libmp3lame" << "-b:a" << br; break;
    case AudioFormat::WAV: a << "-c:a" << "pcm_s16le"; break;
    case AudioFormat::FLAC: a << "-c:a" << "flac"; break;
    case AudioFormat::OGG: a << "-c:a" << "libvorbis" << "-b:a" << br; break;
    case AudioFormat::AAC: a << "-c:a" << "aac" << "-b:a" << br; break;
    }
    if (s.sampleRate > 0)
        a << "-ar" << QString::number(s.sampleRate);
    if (s.channels > 0)
        a << "-ac" << QString::number(s.channels);
    return a;
}

ExportPlan buildAudioExtract(const QString& input, double duration, const AudioExportSettings& s)
{
    ExportPlan plan;
    plan.duration = duration;
    plan.args = commonFFmpegArgs();
    plan.args << "-i" << input << "-map" << "0:a:0" << "-vn" << "-sn" << "-dn";
    plan.args << audioCodecArgs(s) << s.output;
    return plan;
}

ExportPlan buildAudioEncode(const QString& wavInput, double duration, const AudioExportSettings& s)
{
    ExportPlan plan;
    plan.duration = duration;
    plan.args = commonFFmpegArgs();
    plan.args << "-i" << wavInput << "-map" << "0:a:0";
    plan.args << audioCodecArgs(s) << s.output;
    return plan;
}

} // namespace mf
