#include "media/MediaInfo.h"

#include <QFileInfo>
#include <QImageReader>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/error.h>
}

#include <cmath>

namespace mf {

void initFFmpegLogging()
{
    // Errors are reported through return codes; libav's own console output is noise for users.
    av_log_set_level(qEnvironmentVariableIsSet("MEDIAFORGE_AV_DEBUG") ? AV_LOG_INFO : AV_LOG_QUIET);
}

QString avErrorText(int err)
{
    if (err == AVERROR(ENOENT))
        return QStringLiteral("Файл не найден.");
    if (err == AVERROR(EACCES))
        return QStringLiteral("Нет прав на чтение файла.");
    if (err == AVERROR_INVALIDDATA)
        return QStringLiteral("Файл повреждён или имеет неизвестный формат.");
    if (err == AVERROR_DECODER_NOT_FOUND)
        return QStringLiteral("Нет декодера для кодека этого файла.");
    if (err == AVERROR_DEMUXER_NOT_FOUND)
        return QStringLiteral("Формат контейнера не поддерживается.");
    if (err == AVERROR_STREAM_NOT_FOUND)
        return QStringLiteral("В файле нет подходящих дорожек.");
    if (err == AVERROR(ENOMEM))
        return QStringLiteral("Недостаточно памяти.");
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return QStringLiteral("Ошибка FFmpeg: %1").arg(QString::fromUtf8(buf));
}

QStringList videoSuffixes()
{
    return {"mp4", "mkv", "avi", "mov", "webm", "m4v", "mpg", "mpeg", "ts", "flv", "wmv", "3gp", "ogv"};
}

QStringList audioSuffixes()
{
    return {"mp3", "wav", "flac", "ogg", "oga", "opus", "aac", "m4a", "wma", "aif", "aiff"};
}

bool isImageFile(const QString& path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    for (const char* f : {"png", "jpg", "jpeg", "webp", "bmp", "tif", "tiff", "gif"})
        if (s == QLatin1String(f))
            return true;
    return false;
}

static QString globs(const QStringList& suffixes)
{
    QStringList out;
    for (const QString& s : suffixes)
        out << "*." + s << "*." + s.toUpper();
    return out.join(' ');
}

QString mediaOpenFilter()
{
    const QStringList images{"png", "jpg", "jpeg", "webp", "bmp", "tif", "tiff"};
    return QStringLiteral("Все медиа (%1 %2 %3);;Видео (%1);;Аудио (%2);;Изображения (%3);;Все файлы (*)")
        .arg(globs(videoSuffixes()), globs(audioSuffixes()), globs(images));
}

QString audioOpenFilter()
{
    return QStringLiteral("Аудио и видео (%1 %2);;Аудио (%1);;Видео (%2);;Все файлы (*)")
        .arg(globs(audioSuffixes()), globs(videoSuffixes()));
}

bool probeImage(const QString& path, MediaInfo* info, QString* error)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize sz = reader.size();
    if (!reader.canRead() || !sz.isValid()) {
        if (error)
            *error = QStringLiteral("Не удалось прочитать изображение: %1").arg(reader.errorString());
        return false;
    }
    MediaInfo mi;
    mi.path = path;
    mi.isImage = true;
    mi.hasVideo = true;
    mi.width = sz.width();
    mi.height = sz.height();
    mi.container = QString::fromLatin1(reader.format());
    *info = mi;
    return true;
}

bool probeMedia(const QString& path, MediaInfo* info, QString* error)
{
    auto fail = [error](const QString& msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (!QFileInfo::exists(path))
        return fail(QStringLiteral("Файл не найден: %1").arg(path));

    AVFormatContext* fmt = nullptr;
    int r = avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr);
    if (r < 0)
        return fail(QStringLiteral("Не удалось открыть «%1»: %2").arg(QFileInfo(path).fileName(), avErrorText(r)));
    r = avformat_find_stream_info(fmt, nullptr);
    if (r < 0) {
        avformat_close_input(&fmt);
        return fail(QStringLiteral("Не удалось прочитать дорожки «%1»: %2").arg(QFileInfo(path).fileName(), avErrorText(r)));
    }

    MediaInfo mi;
    mi.path = path;
    mi.container = QString::fromLatin1(fmt->iformat->name);
    if (fmt->duration != AV_NOPTS_VALUE)
        mi.duration = fmt->duration / double(AV_TIME_BASE);

    const int vi = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vi >= 0 && !(fmt->streams[vi]->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
        AVStream* st = fmt->streams[vi];
        const AVCodecParameters* par = st->codecpar;
        mi.hasVideo = true;
        mi.videoCodec = QString::fromLatin1(avcodec_get_name(par->codec_id));
        double w = par->width, h = par->height;
        const AVRational sar = av_guess_sample_aspect_ratio(fmt, st, nullptr);
        if (sar.num > 0 && sar.den > 0)
            w = w * sar.num / sar.den;
        const AVPacketSideData* sd =
            av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
        if (sd) {
            double rot = -av_display_rotation_get(reinterpret_cast<const int32_t*>(sd->data));
            rot = std::fmod(std::round(rot) + 360.0, 360.0);
            mi.rotation = int(rot);
        }
        if (mi.rotation == 90 || mi.rotation == 270)
            std::swap(w, h);
        mi.width = int(std::lround(w));
        mi.height = int(std::lround(h));
        AVRational fr = st->avg_frame_rate.num > 0 ? st->avg_frame_rate : st->r_frame_rate;
        mi.fps = fr.den > 0 ? av_q2d(fr) : 0.0;
        if (mi.fps <= 0 || mi.fps > 1000)
            mi.fps = 30.0;
        if (mi.duration <= 0 && st->duration != AV_NOPTS_VALUE)
            mi.duration = st->duration * av_q2d(st->time_base);
    }
    const int ai = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (ai >= 0) {
        const AVCodecParameters* par = fmt->streams[ai]->codecpar;
        mi.hasAudio = true;
        mi.audioCodec = QString::fromLatin1(avcodec_get_name(par->codec_id));
        mi.sampleRate = par->sample_rate;
        mi.channels = par->ch_layout.nb_channels;
        if (mi.duration <= 0 && fmt->streams[ai]->duration != AV_NOPTS_VALUE)
            mi.duration = fmt->streams[ai]->duration * av_q2d(fmt->streams[ai]->time_base);
    }
    avformat_close_input(&fmt);

    if (!mi.hasVideo && !mi.hasAudio)
        return fail(QStringLiteral("В файле «%1» нет ни видео-, ни аудиодорожки.").arg(QFileInfo(path).fileName()));
    if (mi.duration <= 0)
        return fail(QStringLiteral("Не удалось определить длительность «%1» — файл повреждён или не завершён.")
                        .arg(QFileInfo(path).fileName()));
    *info = mi;
    return true;
}

bool probeAny(const QString& path, MediaInfo* info, QString* error)
{
    if (isImageFile(path))
        return probeImage(path, info, error);
    return probeMedia(path, info, error);
}

} // namespace mf
