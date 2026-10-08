#include "TestUtil.h"

#include "media/Audio.h"
#include "media/ExportBuilder.h"
#include "media/FFmpegJob.h"
#include "media/MediaInfo.h"
#include "media/Timeline.h"
#include "media/VideoDecoder.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <cmath>

using namespace mf;
using namespace testutil;

namespace {

QTemporaryDir* g_tmp = nullptr;

bool generate(const QStringList& args, double duration)
{
    QString err;
    const bool ok = FFmpegJob::runBlocking(commonFFmpegArgs() + args, duration, &err);
    if (!ok)
        qWarning() << "ffmpeg failed:" << err;
    return ok;
}

double probeDuration(const QString& path)
{
    MediaInfo mi;
    QString err;
    if (!probeMedia(path, &mi, &err))
        return -1;
    return mi.duration;
}

// Dominant frequency estimate from zero crossings of the left channel.
double toneFrequency(const PcmBuffer& pcm, double from, double to)
{
    const qint64 a = pcm.frameAt(from), b = pcm.frameAt(to);
    int crossings = 0;
    for (qint64 i = a + 1; i < b; ++i) {
        const float p = pcm.data[(i - 1) * pcm.channels], c = pcm.data[i * pcm.channels];
        if ((p < 0) != (c < 0))
            ++crossings;
    }
    return crossings / 2.0 / (to - from);
}

bool runExport(const TimelineState& st, VideoExportSettings s, QString* err)
{
    QTemporaryDir work;
    const ExportPlan plan = buildVideoExport(st, s, work.path());
    if (!plan.ok()) {
        *err = plan.error;
        return false;
    }
    return FFmpegJob::runBlocking(plan.args, plan.duration, err);
}

Clip clipFor(Timeline& tl, const QString& path, ClipKind kind)
{
    MediaInfo mi;
    QString err;
    if (!probeAny(path, &mi, &err))
        qWarning() << err;
    return tl.makeClip(path, mi, kind);
}

} // namespace

void MediaCoreTest::initTestCase()
{
    initFFmpegLogging();
    if (qEnvironmentVariableIsEmpty("MEDIAFORGE_FFMPEG"))
        qputenv("MEDIAFORGE_FFMPEG", MF_TEST_FFMPEG);
    QVERIFY2(FFmpegJob::ffmpegAvailable(), "ffmpeg binary not found");
    QVERIFY(FFmpegJob::hasEncoder(QStringLiteral("libx264")));

    g_tmp = new QTemporaryDir();
    m_dir = g_tmp->path();
    m_video = m_dir + "/clip.mp4";
    m_video2 = m_dir + "/noaudio.mkv";
    m_audio = m_dir + "/tone.mp3";
    m_image = m_dir + "/red.png";

    QElapsedTimer timer;
    timer.start();
    QVERIFY(generate({"-f", "lavfi", "-i", "testsrc2=size=640x360:rate=25:duration=5", "-f", "lavfi", "-i",
                      "sine=frequency=440:sample_rate=44100:duration=5", "-c:v", "libx264", "-preset", "ultrafast",
                      "-pix_fmt", "yuv420p", "-c:a", "aac", "-ac", "2", "-shortest", m_video},
                     5));
    QVERIFY(generate({"-f", "lavfi", "-i", "testsrc=size=320x240:rate=30:duration=3", "-c:v", "libx264", "-preset",
                      "ultrafast", "-pix_fmt", "yuv420p", m_video2},
                     3));
    QVERIFY(generate({"-f", "lavfi", "-i", "sine=frequency=880:sample_rate=48000:duration=4", "-c:a", "libmp3lame",
                      "-b:a", "128k", m_audio},
                     4));
    QImage img(200, 100, QImage::Format_ARGB32);
    img.fill(QColor(255, 0, 0));
    QVERIFY(img.save(m_image));
    qInfo() << "test media generated in" << timer.elapsed() << "ms";
}

void MediaCoreTest::probe()
{
    MediaInfo mi;
    QString err;
    QVERIFY2(probeMedia(m_video, &mi, &err), qPrintable(err));
    QVERIFY(std::abs(mi.duration - 5.0) < 0.15);
    QCOMPARE(mi.width, 640);
    QCOMPARE(mi.height, 360);
    QVERIFY(std::abs(mi.fps - 25.0) < 0.01);
    QVERIFY(mi.hasAudio);
    QCOMPARE(mi.videoCodec, QStringLiteral("h264"));

    QVERIFY2(probeMedia(m_video2, &mi, &err), qPrintable(err));
    QVERIFY(!mi.hasAudio);
    QVERIFY(std::abs(mi.fps - 30.0) < 0.01);

    QVERIFY2(probeMedia(m_audio, &mi, &err), qPrintable(err));
    QVERIFY(!mi.hasVideo);
    QVERIFY(std::abs(mi.duration - 4.0) < 0.15);

    QVERIFY2(probeAny(m_image, &mi, &err), qPrintable(err));
    QVERIFY(mi.isImage);
    QCOMPARE(mi.size(), QSize(200, 100));
}

void MediaCoreTest::probeBroken()
{
    MediaInfo mi;
    QString err;
    QVERIFY(!probeMedia(m_dir + "/missing.mp4", &mi, &err));
    QVERIFY(!err.isEmpty());

    const QString junk = m_dir + "/junk.mp4";
    QFile f(junk);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QByteArray bytes(50000, '\0');
    for (int i = 0; i < bytes.size(); ++i)
        bytes[i] = char((i * 7919) % 251);
    f.write(bytes);
    f.close();
    err.clear();
    QVERIFY(!probeMedia(junk, &mi, &err));
    QVERIFY(!err.isEmpty());

    // A truncated file must not crash the decoder.
    QFile src(m_video);
    QVERIFY(src.open(QIODevice::ReadOnly));
    const QByteArray head = src.read(src.size() / 3);
    src.close();
    const QString cut = m_dir + "/cut.mp4";
    QFile c(cut);
    QVERIFY(c.open(QIODevice::WriteOnly));
    c.write(head);
    c.close();
    VideoDecoder dec;
    if (dec.open(cut, &err)) {
        dec.frameAt(4.5);
        dec.frameAt(0.5);
    }
    PcmBuffer pcm;
    decodeAudio(cut, &pcm, &err);
}

void MediaCoreTest::videoDecoder()
{
    VideoDecoder dec;
    QString err;
    QVERIFY2(dec.open(m_video, &err), qPrintable(err));
    const QImage f0 = dec.frameAt(0.0);
    QCOMPARE(f0.size(), QSize(640, 360));
    const QImage f2 = dec.frameAt(2.0);
    QVERIFY(std::abs(dec.lastFrameTime() - 2.0) < 0.001);
    QVERIFY(!imagesEqual(f0, f2));
    // Sequential playback-style access.
    for (int i = 0; i < 25; ++i) {
        const QImage f = dec.frameAt(2.0 + i * 0.04);
        QVERIFY(!f.isNull());
        QVERIFY(std::abs(dec.lastFrameTime() - (2.0 + i * 0.04)) < 0.021);
    }
    // Seeking back gives the same frame as before.
    const QImage again = dec.frameAt(2.0);
    QVERIFY(imagesEqual(again, f2));
    const QImage between = dec.frameAt(2.03); // shows the frame at 2.00 until 2.04
    QVERIFY(std::abs(dec.lastFrameTime() - 2.0) < 0.001);
    QVERIFY(imagesEqual(between, f2));
    const QImage boxed = dec.frameAt(1.0, QSize(320, 320));
    QCOMPARE(boxed.size(), QSize(320, 180));
    // Past the end returns the last frame instead of nothing.
    QVERIFY(!dec.frameAt(30.0).isNull());
}

void MediaCoreTest::audioDecoder()
{
    PcmBuffer pcm;
    QString err;
    QVERIFY2(decodeAudio(m_audio, &pcm, &err), qPrintable(err));
    QCOMPARE(pcm.rate, 48000);
    QCOMPARE(pcm.channels, 2);
    QVERIFY2(std::abs(pcm.duration() - 4.0) < 0.1, qPrintable(QString::number(pcm.duration())));
    QVERIFY(std::abs(toneFrequency(pcm, 1.0, 3.0) - 880.0) < 10.0);

    QVERIFY2(decodeAudio(m_video, &pcm, &err), qPrintable(err));
    QVERIFY(std::abs(pcm.duration() - 5.0) < 0.1);
    QVERIFY(std::abs(toneFrequency(pcm, 1.0, 3.0) - 440.0) < 10.0);

    const QVector<float> peaks = computePeaks(pcm, 480);
    QVERIFY(peaks.size() >= 2 * 499);

    err.clear();
    QVERIFY(!decodeAudio(m_video2, &pcm, &err));
    QVERIFY(!err.isEmpty());

    PcmBuffer mono;
    QVERIFY(decodeAudio(m_audio, &mono, &err, {}, 8000, 1));
    QCOMPARE(mono.channels, 1);
    QVERIFY(std::abs(mono.duration() - 4.0) < 0.1);
}

void MediaCoreTest::audioOps()
{
    PcmBuffer pcm;
    QString err;
    QVERIFY(decodeAudio(m_audio, &pcm, &err));
    const double d = pcm.duration();

    PcmBuffer part = audioops::slice(pcm, pcm.frameAt(1.0), pcm.frameAt(2.5));
    QVERIFY(std::abs(part.duration() - 1.5) < 1e-3);

    PcmBuffer cut = pcm;
    audioops::removeRange(cut, cut.frameAt(1.0), cut.frameAt(2.0));
    QVERIFY(std::abs(cut.duration() - (d - 1.0)) < 1e-3);

    PcmBuffer quiet = pcm;
    audioops::silence(quiet, 0, quiet.frameAt(1.0));
    QCOMPARE(audioops::peak(quiet, 0, quiet.frameAt(1.0)), 0.f);

    PcmBuffer loud = pcm;
    const float p0 = audioops::peak(pcm, pcm.frameAt(1), pcm.frameAt(2));
    audioops::gain(loud, 0, loud.frames(), 0.5f);
    QVERIFY(std::abs(audioops::peak(loud, loud.frameAt(1), loud.frameAt(2)) - p0 * 0.5f) < 1e-3);

    PcmBuffer faded = pcm;
    audioops::fadeIn(faded, 0, faded.frameAt(1.0));
    QVERIFY(audioops::peak(faded, 0, faded.frameAt(0.05)) < p0 * 0.1f);
    audioops::fadeOut(faded, faded.frameAt(d - 1.0), faded.frames());
    QVERIFY(audioops::peak(faded, faded.frameAt(d - 0.05), faded.frames()) < p0 * 0.1f);

    for (double speed : {0.25, 0.5, 2.0, 4.0}) {
        PcmBuffer out;
        QVERIFY2(audioops::changeSpeed(pcm, speed, &out, &err), qPrintable(err));
        const double expected = d / speed;
        QVERIFY2(std::abs(out.duration() - expected) < 0.1,
                 qPrintable(QStringLiteral("speed %1: %2 vs %3").arg(speed).arg(out.duration()).arg(expected)));
        // Tempo changes, pitch does not.
        QVERIFY2(std::abs(toneFrequency(out, out.duration() * 0.3, out.duration() * 0.7) - 880.0) < 20.0,
                 qPrintable(QString::number(speed)));
    }
    PcmBuffer bad;
    QVERIFY(!audioops::changeSpeed(pcm, 8.0, &bad, &err));

    const QString wav = m_dir + "/ops.wav";
    QVERIFY2(writeWav(part, wav, &err), qPrintable(err));
    PcmBuffer back;
    QVERIFY2(decodeAudio(wav, &back, &err), qPrintable(err));
    QVERIFY(std::abs(back.duration() - 1.5) < 0.01);

    QCOMPARE(atempoChain(0.25), QStringLiteral("atempo=0.5,atempo=0.5"));
    QCOMPARE(atempoChain(4.0), QStringLiteral("atempo=2,atempo=2"));
    QCOMPARE(atempoChain(1.5), QStringLiteral("atempo=1.5"));
}

void MediaCoreTest::timelineEditing()
{
    Timeline tl;
    tl.addClip(Track::Main, clipFor(tl, m_video, ClipKind::Video));
    QCOMPARE(tl.state().resolution, QSize(640, 360));
    QVERIFY(std::abs(tl.state().fps - 25.0) < 1e-6);
    tl.addClip(Track::Main, clipFor(tl, m_image, ClipKind::Image));
    tl.addClip(Track::Main, clipFor(tl, m_video2, ClipKind::Video));
    const double total = tl.state().duration();
    QVERIFY(std::abs(total - (5.0 + 3.0 + 3.0)) < 0.2);

    tl.splitMainAt(2.0);
    QCOMPARE(tl.state().main.size(), 4);
    QVERIFY(std::abs(tl.state().duration() - total) < 1e-9);
    QVERIFY(std::abs(tl.state().main[0].out - 2.0) < 1e-9);
    QVERIFY(std::abs(tl.state().main[1].in - 2.0) < 1e-9);

    // Image clip split keeps its total duration.
    tl.splitMainAt(6.0);
    QCOMPARE(tl.state().main.size(), 5);
    QVERIFY(std::abs(tl.state().duration() - total) < 1e-9);

    Clip a = clipFor(tl, m_audio, ClipKind::Audio);
    a.start = 6.5;
    tl.addClip(Track::Audio, a);
    tl.removeRange(1.0, 2.0);
    QVERIFY(std::abs(tl.state().mainLength() - (total - 1.0)) < 1e-6);
    QVERIFY(std::abs(tl.state().audio[0].start - 5.5) < 1e-9);

    tl.undoStack()->undo();
    QVERIFY(std::abs(tl.state().mainLength() - total) < 1e-6);
    tl.undoStack()->redo();

    // Speed changes the clip's timeline length.
    Clip c = tl.state().main[0];
    c.speed = 2.0;
    tl.updateClip(Track::Main, 0, c, QStringLiteral("speed"));
    QVERIFY(std::abs(tl.state().main[0].length() - 0.5) < 1e-9);

    tl.moveMainClip(0, 2);
    QVERIFY(std::abs(tl.state().main[2].speed - 2.0) < 1e-9);

    const TimelineState sub = subRange(tl.state(), 0.5, 3.0);
    QVERIFY(std::abs(sub.mainLength() - 2.5) < 1e-6);

    // An audio clip cut in the middle becomes two pieces.
    Timeline t2;
    Clip au = clipFor(t2, m_audio, ClipKind::Audio);
    au.start = 1.0;
    t2.addClip(Track::Audio, au);
    t2.removeRange(2.0, 3.0);
    QCOMPARE(t2.state().audio.size(), 2);
    QVERIFY(std::abs(t2.state().audio[0].length() - 1.0) < 1e-6);
    QVERIFY(std::abs(t2.state().audio[1].start - 2.0) < 1e-6);
    QVERIFY(std::abs(t2.state().audio[1].in - 2.0) < 1e-6);

    const QString proj = m_dir + "/project.mfv";
    QString err;
    QVERIFY2(saveTimeline(tl.state(), proj, &err), qPrintable(err));
    TimelineState loaded;
    QStringList warnings;
    QVERIFY2(loadTimeline(proj, &loaded, &warnings, &err), qPrintable(err));
    QVERIFY(warnings.isEmpty());
    QCOMPARE(loaded.main.size(), tl.state().main.size());
    QVERIFY(std::abs(loaded.duration() - tl.state().duration()) < 1e-6);
}

void MediaCoreTest::exportTrim()
{
    Timeline tl;
    Clip c = clipFor(tl, m_video, ClipKind::Video);
    c.in = 1.0;
    c.out = 3.0;
    tl.addClip(Track::Main, c);
    VideoExportSettings s;
    s.output = m_dir + "/trim.mp4";
    s.preset = "ultrafast";
    QString err;
    QVERIFY2(runExport(tl.state(), s, &err), qPrintable(err));
    MediaInfo mi;
    QVERIFY2(probeMedia(s.output, &mi, &err), qPrintable(err));
    QVERIFY2(std::abs(mi.duration - 2.0) < 0.1, qPrintable(QString::number(mi.duration)));
    QCOMPARE(mi.size(), QSize(640, 360));
    QVERIFY(mi.hasAudio);
    QCOMPARE(mi.videoCodec, QStringLiteral("h264"));

    // The first exported frame must be the source frame at 1.0 s.
    VideoDecoder src, out;
    QVERIFY(src.open(m_video, &err));
    QVERIFY(out.open(s.output, &err));
    QVERIFY(imagesEqual(src.frameAt(1.0), out.frameAt(0.0), 40));
}

void MediaCoreTest::exportSpeed_data()
{
    QTest::addColumn<double>("speed");
    QTest::addColumn<double>("srcLen");
    QTest::newRow("0.25x") << 0.25 << 1.0;
    QTest::newRow("0.5x") << 0.5 << 1.5;
    QTest::newRow("2x") << 2.0 << 4.0;
    QTest::newRow("4x") << 4.0 << 4.0;
}

void MediaCoreTest::exportSpeed()
{
    QFETCH(double, speed);
    QFETCH(double, srcLen);
    Timeline tl;
    Clip c = clipFor(tl, m_video, ClipKind::Video);
    c.in = 0.5;
    c.out = 0.5 + srcLen;
    c.speed = speed;
    tl.addClip(Track::Main, c);
    VideoExportSettings s;
    s.output = m_dir + QStringLiteral("/speed_%1.mp4").arg(speed);
    s.preset = "ultrafast";
    QString err;
    QVERIFY2(runExport(tl.state(), s, &err), qPrintable(err));
    const double expected = srcLen / speed;
    MediaInfo mi;
    QVERIFY2(probeMedia(s.output, &mi, &err), qPrintable(err));
    QVERIFY2(std::abs(mi.duration - expected) < 0.12,
             qPrintable(QStringLiteral("%1 vs %2").arg(mi.duration).arg(expected)));
    PcmBuffer pcm;
    QVERIFY2(decodeAudio(s.output, &pcm, &err), qPrintable(err));
    QVERIFY2(std::abs(pcm.duration() - expected) < 0.12,
             qPrintable(QStringLiteral("audio %1 vs %2").arg(pcm.duration()).arg(expected)));
    const double f = toneFrequency(pcm, expected * 0.25, expected * 0.75);
    QVERIFY2(std::abs(f - 440.0) < 20.0, qPrintable(QStringLiteral("pitch %1 Hz").arg(f)));
    // Video frame count follows the new duration.
    VideoDecoder dec;
    QVERIFY(dec.open(s.output, &err));
    QVERIFY(!dec.frameAt(expected - 0.05).isNull());
}

void MediaCoreTest::exportConcatImageOverlayAudio()
{
    Timeline tl;
    Clip v1 = clipFor(tl, m_video, ClipKind::Video);
    v1.out = 2.0;
    tl.addClip(Track::Main, v1);
    Clip img = clipFor(tl, m_image, ClipKind::Image);
    img.out = 2.0;
    tl.addClip(Track::Main, img);
    Clip v2 = clipFor(tl, m_video2, ClipKind::Video);
    v2.out = 1.5;
    tl.addClip(Track::Main, v2);
    Clip ov = clipFor(tl, m_image, ClipKind::Overlay);
    ov.start = 1.0;
    ov.out = 0.8; // visible 1.0 .. 1.8
    ov.x = 0.0;
    ov.y = 0.0;
    ov.width = 0.25;
    tl.addClip(Track::Overlay, ov);
    Clip au = clipFor(tl, m_audio, ClipKind::Audio);
    au.start = 4.0;
    au.fadeIn = 0.5;
    au.fadeOut = 0.5;
    tl.addClip(Track::Audio, au); // runs past the main track: adds a black tail
    tl.setMuteOriginal(false);
    const double expected = 4.0 + 4.0;

    VideoExportSettings s;
    s.output = m_dir + "/concat.mp4";
    s.preset = "ultrafast";
    s.size = QSize(320, 180);
    s.fps = 25;
    QString err;
    QVERIFY2(runExport(tl.state(), s, &err), qPrintable(err));
    MediaInfo mi;
    QVERIFY2(probeMedia(s.output, &mi, &err), qPrintable(err));
    QVERIFY2(std::abs(mi.duration - expected) < 0.15, qPrintable(QString::number(mi.duration)));
    QCOMPARE(mi.size(), QSize(320, 180));

    VideoDecoder dec;
    QVERIFY(dec.open(s.output, &err));
    auto isRed = [](QRgb p) { return qRed(p) > 180 && qGreen(p) < 80 && qBlue(p) < 80; };
    QImage f = dec.frameAt(1.4);
    QVERIFY2(isRed(f.pixel(20, 10)), "overlay visible at 1.4 s");
    f = dec.frameAt(0.5);
    QVERIFY2(!isRed(f.pixel(20, 10)), "overlay hidden at 0.5 s");
    f = dec.frameAt(3.0);
    QVERIFY2(isRed(f.pixel(160, 90)), "image clip at 3.0 s");
    QVERIFY2(!isRed(f.pixel(160, 3)), "image clip is letterboxed (2:1 image in a 16:9 frame)");
    f = dec.frameAt(7.0);
    QVERIFY2(qRed(f.pixel(160, 90)) < 30, "black tail after the main track");

    PcmBuffer pcm;
    QVERIFY(decodeAudio(s.output, &pcm, &err));
    QVERIFY(std::abs(pcm.duration() - expected) < 0.15);
    QVERIFY(std::abs(toneFrequency(pcm, 0.5, 1.5) - 440.0) < 20.0);  // original audio
    QVERIFY(audioops::peak(pcm, pcm.frameAt(2.2), pcm.frameAt(3.8)) < 0.01); // image + silent clip
    QVERIFY(std::abs(toneFrequency(pcm, 5.0, 7.0) - 880.0) < 20.0);  // added track
    QVERIFY(audioops::peak(pcm, pcm.frameAt(4.0), pcm.frameAt(4.05)) <
            audioops::peak(pcm, pcm.frameAt(5.0), pcm.frameAt(5.05)) * 0.3f); // fade in

    // Replacing the original audio: mute it, keep the added track only.
    tl.setMuteOriginal(true);
    s.output = m_dir + "/replaced.mp4";
    QVERIFY2(runExport(tl.state(), s, &err), qPrintable(err));
    QVERIFY(decodeAudio(s.output, &pcm, &err));
    QVERIFY(audioops::peak(pcm, pcm.frameAt(0.2), pcm.frameAt(1.8)) < 0.01);
    QVERIFY(std::abs(toneFrequency(pcm, 5.0, 7.0) - 880.0) < 20.0);
}

void MediaCoreTest::exportWebm()
{
    Timeline tl;
    Clip c = clipFor(tl, m_video, ClipKind::Video);
    c.out = 1.5;
    tl.addClip(Track::Main, c);
    VideoExportSettings s;
    s.format = VideoFormat::WEBM;
    s.output = m_dir + "/out.webm";
    s.crf = 40;
    s.size = QSize(320, 180);
    QString err;
    QVERIFY2(runExport(tl.state(), s, &err), qPrintable(err));
    MediaInfo mi;
    QVERIFY2(probeMedia(s.output, &mi, &err), qPrintable(err));
    QCOMPARE(mi.videoCodec, QStringLiteral("vp9"));
    QCOMPARE(mi.audioCodec, QStringLiteral("opus"));
    QVERIFY(std::abs(mi.duration - 1.5) < 0.12);
}

void MediaCoreTest::exportFrames()
{
    Timeline tl;
    tl.addClip(Track::Main, clipFor(tl, m_video, ClipKind::Video));
    VideoExportSettings s;
    s.format = VideoFormat::PngSequence;
    s.output = m_dir + "/frames";
    QDir().mkpath(s.output);
    s.rangeStart = 1.0;
    s.rangeEnd = 1.4;
    QString err;
    QVERIFY2(runExport(tl.state(), s, &err), qPrintable(err));
    const QStringList files = QDir(s.output).entryList({"*.png"}, QDir::Files);
    QCOMPARE(files.size(), 10);
    QImage first(QDir(s.output).filePath(files.first()));
    QCOMPARE(first.size(), QSize(640, 360));
}

void MediaCoreTest::extractAudio_data()
{
    QTest::addColumn<int>("format");
    QTest::addColumn<QString>("codec");
    QTest::newRow("mp3") << int(AudioFormat::MP3) << "mp3";
    QTest::newRow("wav") << int(AudioFormat::WAV) << "pcm_s16le";
    QTest::newRow("flac") << int(AudioFormat::FLAC) << "flac";
    QTest::newRow("ogg") << int(AudioFormat::OGG) << "vorbis";
    QTest::newRow("aac") << int(AudioFormat::AAC) << "aac";
}

void MediaCoreTest::extractAudio()
{
    QFETCH(int, format);
    QFETCH(QString, codec);
    AudioExportSettings s;
    s.format = AudioFormat(format);
    s.output = m_dir + "/extracted." + audioSuffix(s.format);
    const ExportPlan plan = buildAudioExtract(m_video, 5.0, s);
    QString err;
    double lastProgress = 0;
    QVERIFY2(FFmpegJob::runBlocking(plan.args, plan.duration, &err, [&](double p) { lastProgress = p; }),
             qPrintable(err));
    QVERIFY(lastProgress > 0.9);
    MediaInfo mi;
    QVERIFY2(probeMedia(s.output, &mi, &err), qPrintable(err));
    QVERIFY(!mi.hasVideo);
    QVERIFY(mi.hasAudio);
    QCOMPARE(mi.audioCodec, codec);
    QVERIFY2(std::abs(mi.duration - 5.0) < 0.15, qPrintable(QString::number(mi.duration)));

    // Encoding an edited WAV from the audio editor goes through the same path.
    PcmBuffer pcm;
    QVERIFY(decodeAudio(m_audio, &pcm, &err));
    const QString wav = m_dir + "/edit.wav";
    QVERIFY(writeWav(audioops::slice(pcm, 0, pcm.frameAt(2.0)), wav, &err));
    s.output = m_dir + "/encoded." + audioSuffix(s.format);
    const ExportPlan enc = buildAudioEncode(wav, 2.0, s);
    QVERIFY2(FFmpegJob::runBlocking(enc.args, enc.duration, &err), qPrintable(err));
    QVERIFY(std::abs(probeDuration(s.output) - 2.0) < 0.1);
}

void MediaCoreTest::ffmpegErrors()
{
    QString err;
    AudioExportSettings s;
    s.output = m_dir + "/never.mp3";
    QVERIFY(!FFmpegJob::runBlocking(buildAudioExtract(m_dir + "/missing.mp4", 1, s).args, 1, &err));
    QVERIFY(err.contains(QStringLiteral("не найден")));

    // A video without audio: asking for its audio fails with a clear message.
    err.clear();
    QVERIFY(!FFmpegJob::runBlocking(buildAudioExtract(m_video2, 3, s).args, 3, &err));
    QVERIFY(!err.isEmpty());
    QVERIFY(!QFile::exists(s.output) || QFileInfo(s.output).size() == 0);

    err.clear();
    s.output = "/nonexistent-dir/out.mp3";
    QVERIFY(!FFmpegJob::runBlocking(buildAudioExtract(m_video, 5, s).args, 5, &err));
    QVERIFY(!err.isEmpty());

    QCOMPARE(friendlyFFmpegError(QStringLiteral("av_interleaved_write_frame(): No space left on device"), 1).left(16),
             QStringLiteral("Недостаточно мес"));

    // Cancelling a long job removes the partial output.
    FFmpegJob job;
    const QString out = m_dir + "/cancel.mp4";
    QEventLoop loop;
    bool finished = false, ok = true;
    QString msg;
    QObject::connect(&job, &FFmpegJob::progress, [&](double p) {
        if (p > 0.0)
            job.cancel();
    });
    QObject::connect(&job, &FFmpegJob::finished, [&](bool o, const QString& e) {
        finished = true;
        ok = o;
        msg = e;
        loop.quit();
    });
    job.start(commonFFmpegArgs() + QStringList{"-f", "lavfi", "-i", "testsrc2=size=1280x720:rate=30:duration=120",
                                                "-c:v", "libx264", "-preset", "veryslow", out},
              120, out);
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();
    QVERIFY(finished);
    QVERIFY(!ok);
    QVERIFY(msg.contains(QStringLiteral("отменена")));
    QVERIFY(!QFile::exists(out));
}
