#pragma once

#include <QSize>
#include <QString>
#include <QStringList>

namespace mf {

struct MediaInfo {
    QString path;
    double duration = 0.0; // seconds
    bool hasVideo = false;
    bool hasAudio = false;
    bool isImage = false;
    int width = 0;  // display size, rotation and pixel aspect applied
    int height = 0;
    double fps = 0.0;
    int rotation = 0; // clockwise degrees the decoder applies to frames
    QString videoCodec;
    QString audioCodec;
    QString container;
    int sampleRate = 0;
    int channels = 0;

    QSize size() const { return QSize(width, height); }
};

void initFFmpegLogging();

// Reads stream information with libavformat. Images are handled through Qt.
bool probeMedia(const QString& path, MediaInfo* info, QString* error);
bool probeImage(const QString& path, MediaInfo* info, QString* error);
bool probeAny(const QString& path, MediaInfo* info, QString* error);

bool isImageFile(const QString& path);
QStringList videoSuffixes();
QStringList audioSuffixes();
QString mediaOpenFilter();
QString audioOpenFilter();

// Turns an AVERROR code into a message for the user.
QString avErrorText(int err);

} // namespace mf
