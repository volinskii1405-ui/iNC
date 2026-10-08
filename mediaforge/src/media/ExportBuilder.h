#pragma once

#include "media/Timeline.h"

#include <QSize>
#include <QString>
#include <QStringList>

namespace mf {

enum class VideoFormat { MP4, WEBM, PngSequence, AudioRaw };

struct VideoExportSettings {
    VideoFormat format = VideoFormat::MP4;
    QString output;      // file path, or a directory for PNG sequences
    QSize size;          // output resolution (made even); empty = timeline resolution
    double fps = 0.0;    // 0 = timeline fps
    int crf = 23;        // x264: 0..51, vp9: 0..63
    QString preset = QStringLiteral("medium");
    int audioBitrate = 192; // kbps
    double rangeStart = 0.0;
    double rangeEnd = -1.0; // <0 = whole timeline
};

struct ExportPlan {
    QStringList args;   // arguments for the ffmpeg binary
    double duration = 0.0;
    QString error;
    bool ok() const { return error.isEmpty(); }
};

// Builds a single ffmpeg invocation that renders the timeline: trims and speed
// per clip, concatenation, overlays and the audio mix. Image clips and overlays
// are pre-rendered by Qt into tempDir so every readable image works the same.
ExportPlan buildVideoExport(const TimelineState& timeline, const VideoExportSettings& settings, const QString& tempDir);

enum class AudioFormat { MP3, WAV, FLAC, OGG, AAC };

struct AudioExportSettings {
    AudioFormat format = AudioFormat::MP3;
    QString output;
    int bitrate = 192;   // kbps, lossy formats
    int sampleRate = 0;  // 0 = keep
    int channels = 0;    // 0 = keep
};

QString audioSuffix(AudioFormat f);
QString audioFormatName(AudioFormat f);
QStringList audioCodecArgs(const AudioExportSettings& s);
QStringList commonFFmpegArgs(); // -y, quiet logs, machine-readable progress

// Extracts the first audio stream of a media file directly with ffmpeg.
ExportPlan buildAudioExtract(const QString& input, double duration, const AudioExportSettings& s);
// Encodes a WAV (from the audio editor) into the chosen format.
ExportPlan buildAudioEncode(const QString& wavInput, double duration, const AudioExportSettings& s);

QString num(double v); // locale-independent decimal for ffmpeg arguments

} // namespace mf
