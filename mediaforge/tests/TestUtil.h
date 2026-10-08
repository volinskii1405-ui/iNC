#pragma once

#include <QColor>
#include <QImage>
#include <QObject>
#include <QString>

#include <cstdlib>

namespace testutil {

inline QImage solid(int w, int h, QColor c)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(c);
    return img;
}

inline bool near(QRgb a, QRgb b, int tol)
{
    return std::abs(qRed(a) - qRed(b)) <= tol && std::abs(qGreen(a) - qGreen(b)) <= tol &&
           std::abs(qBlue(a) - qBlue(b)) <= tol && std::abs(qAlpha(a) - qAlpha(b)) <= tol;
}

inline int maskAt(const QImage& mask, int x, int y)
{
    return mask.constScanLine(y)[x];
}

inline QString rgbaStr(QRgb p)
{
    return QStringLiteral("(%1,%2,%3,%4)").arg(qRed(p)).arg(qGreen(p)).arg(qBlue(p)).arg(qAlpha(p));
}

inline bool imagesEqual(const QImage& a, const QImage& b, int tol = 0)
{
    if (a.size() != b.size())
        return false;
    const QImage x = a.convertToFormat(QImage::Format_ARGB32);
    const QImage y = b.convertToFormat(QImage::Format_ARGB32);
    for (int j = 0; j < x.height(); ++j)
        for (int i = 0; i < x.width(); ++i)
            if (!near(x.pixel(i, j), y.pixel(i, j), tol))
                return false;
    return true;
}

} // namespace testutil

class ImageCoreTest : public QObject {
    Q_OBJECT
private slots:
    void blendModes_data();
    void blendModes();
    void layerOpacityAndVisibility();
    void layerOperationsWithUndo();
    void undoDepth();
    void filters();
    void adjustments();
    void selectionMasking();
    void transforms();
    void floodFill();
    void textLayer();
    void projectRoundTrip();
    void projectCorrupted();
    void imageFormats();
    void brokenImage();
};

class MediaCoreTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void probe();
    void probeBroken();
    void videoDecoder();
    void audioDecoder();
    void audioOps();
    void timelineEditing();
    void exportTrim();
    void exportSpeed_data();
    void exportSpeed();
    void exportConcatImageOverlayAudio();
    void exportWebm();
    void exportFrames();
    void extractAudio_data();
    void extractAudio();
    void ffmpegErrors();

private:
    QString m_dir;
    QString m_video;
    QString m_video2;
    QString m_audio;
    QString m_image;
};
