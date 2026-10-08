#pragma once

#include "image/Layer.h"

#include <QImage>
#include <QString>
#include <QStringList>

namespace mf::imageio {

struct LoadResult {
    QImage image;
    QString error; // empty on success
};

LoadResult loadImage(const QString& path);

// Format is picked from the file suffix. quality: 0..100 (JPG/WEBP).
// JPG and BMP have no alpha, so transparent areas are flattened onto white.
bool saveImage(const QImage& image, const QString& path, int quality, QString* error);

QStringList readableSuffixes();
QStringList writableSuffixes();
QString openFilter();   // for QFileDialog
QString exportFilter();

// Free-space check used before writing large files.
bool hasFreeSpace(const QString& path, qint64 bytesNeeded, QString* error);

} // namespace mf::imageio

namespace mf::project {

constexpr const char* kSuffix = "mfp";

bool save(const DocState& state, const QString& path, QString* error);
bool load(const QString& path, DocState* state, QString* error);

} // namespace mf::project
