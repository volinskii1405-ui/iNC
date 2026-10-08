#pragma once

#include <QString>
#include <QVector>

#include <functional>

namespace mf {

// Interleaved float PCM.
struct PcmBuffer {
    int rate = 48000;
    int channels = 2;
    QVector<float> data;

    qint64 frames() const { return channels > 0 ? data.size() / channels : 0; }
    double duration() const { return rate > 0 ? double(frames()) / rate : 0.0; }
    qint64 frameAt(double seconds) const;
    bool isEmpty() const { return data.isEmpty(); }
};

// progress(fraction) returns false to cancel.
using ProgressFn = std::function<bool(double)>;

bool decodeAudio(const QString& path, PcmBuffer* out, QString* error, const ProgressFn& progress = {},
                 int rate = 48000, int channels = 2);

// Min/max pairs per bucket of `framesPerBucket` frames (channels mixed down).
QVector<float> computePeaks(const PcmBuffer& pcm, int framesPerBucket);

namespace audioops {

// All ranges are [a, b) in frames and clamped to the buffer.
PcmBuffer slice(const PcmBuffer& pcm, qint64 a, qint64 b);
void removeRange(PcmBuffer& pcm, qint64 a, qint64 b);
void insert(PcmBuffer& pcm, qint64 at, const PcmBuffer& other);
void silence(PcmBuffer& pcm, qint64 a, qint64 b);
void gain(PcmBuffer& pcm, qint64 a, qint64 b, float factor);
void fadeIn(PcmBuffer& pcm, qint64 a, qint64 b);
void fadeOut(PcmBuffer& pcm, qint64 a, qint64 b);
float peak(const PcmBuffer& pcm, qint64 a, qint64 b);
// Changes tempo without changing pitch (libavfilter atempo). speed 0.25..4.
bool changeSpeed(const PcmBuffer& in, double speed, PcmBuffer* out, QString* error);

} // namespace audioops

bool writeWav(const PcmBuffer& pcm, const QString& path, QString* error);
QString atempoChain(double speed); // e.g. "atempo=2.0,atempo=1.5"

} // namespace mf
