// Headless GUI smoke tests: drive the real widgets with synthetic mouse and
// keyboard events (run with QT_QPA_PLATFORM=offscreen or under xvfb).

#include "app/MainWindow.h"
#include "app/Theme.h"
#include "audioui/AudioEditor.h"
#include "audioui/WaveformWidget.h"
#include "image/Document.h"
#include "imageui/CanvasView.h"
#include "imageui/ImageEditor.h"
#include "media/Audio.h"
#include "media/ExportBuilder.h"
#include "media/FFmpegJob.h"
#include "media/MediaInfo.h"
#include "videoui/TimelineWidget.h"
#include "videoui/VideoEditor.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <cmath>

using namespace mf;

namespace {

void mouse(QWidget* w, QEvent::Type type, QPointF pos, Qt::MouseButton b, Qt::MouseButtons bs,
           Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent e(type, pos, w->mapToGlobal(pos), b, bs, mods);
    QApplication::sendEvent(w, &e);
}

// Drag on the canvas between two points given in image coordinates.
void dragCanvas(CanvasView* c, QPointF from, QPointF to, Qt::KeyboardModifiers mods = Qt::NoModifier, int steps = 12)
{
    const QTransform t = c->canvasToView();
    mouse(c, QEvent::MouseButtonPress, t.map(from), Qt::LeftButton, Qt::LeftButton, mods);
    for (int i = 1; i <= steps; ++i) {
        const QPointF p = from + (to - from) * (double(i) / steps);
        mouse(c, QEvent::MouseMove, t.map(p), Qt::NoButton, Qt::LeftButton, mods);
    }
    mouse(c, QEvent::MouseButtonRelease, t.map(to), Qt::LeftButton, Qt::NoButton, mods);
}

void clickCanvas(CanvasView* c, QPointF at, Qt::MouseButton b = Qt::LeftButton)
{
    const QPointF v = c->canvasToView().map(at);
    mouse(c, QEvent::MouseButtonPress, v, b, b);
    mouse(c, QEvent::MouseButtonRelease, v, b, Qt::NoButton);
}

QRgb px(Document* d, int layer, int x, int y)
{
    return qUnpremultiply(d->layer(layer).image.pixel(x, y));
}

bool near(QRgb a, QRgb b, int tol = 3)
{
    return std::abs(qRed(a) - qRed(b)) <= tol && std::abs(qGreen(a) - qGreen(b)) <= tol &&
           std::abs(qBlue(a) - qBlue(b)) <= tol && std::abs(qAlpha(a) - qAlpha(b)) <= tol;
}

// Accepts the next modal dialog after optionally adjusting it.
void acceptNextDialog(std::function<void(QDialog*)> tweak = {}, int delayMs = 400)
{
    QTimer::singleShot(delayMs, [tweak] {
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg)
            return;
        if (tweak)
            tweak(dlg);
        dlg->accept();
    });
}

QString g_media;

} // namespace

class GuiTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void imageTools();
    void imageDialogsAndLayers();
    void videoEditing();
    void audioEditing();
    void mainWindowRouting();

private:
    QTemporaryDir m_tmp;
    QString m_video, m_video2, m_audio, m_logo;
};

void GuiTest::initTestCase()
{
    if (qEnvironmentVariableIsEmpty("MEDIAFORGE_FFMPEG"))
        qputenv("MEDIAFORGE_FFMPEG", MF_TEST_FFMPEG);
    initFFmpegLogging();
    applyDarkTheme(*qApp);
    m_video = m_tmp.filePath("a.mp4");
    m_video2 = m_tmp.filePath("b.mkv");
    m_audio = m_tmp.filePath("tone.mp3");
    m_logo = m_tmp.filePath("logo.png");
    QString err;
    QVERIFY2(FFmpegJob::runBlocking(commonFFmpegArgs() + QStringList{"-f", "lavfi", "-i", "testsrc2=size=640x360:rate=25:duration=6",
                                                                     "-f", "lavfi", "-i", "sine=frequency=440:duration=6",
                                                                     "-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt",
                                                                     "yuv420p", "-c:a", "aac", "-shortest", m_video},
                                    6, &err),
             qPrintable(err));
    QVERIFY(FFmpegJob::runBlocking(commonFFmpegArgs() + QStringList{"-f", "lavfi", "-i", "testsrc=size=320x240:rate=30:duration=3",
                                                                    "-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p",
                                                                    m_video2},
                                   3, &err));
    QVERIFY(FFmpegJob::runBlocking(commonFFmpegArgs() + QStringList{"-f", "lavfi", "-i", "sine=frequency=880:duration=6", "-af",
                                                                    "volume=6", "-c:a", "libmp3lame", m_audio},
                                   6, &err));
    QImage logo(120, 60, QImage::Format_ARGB32);
    logo.fill(QColor(255, 0, 0));
    QVERIFY(logo.save(m_logo));
}

void GuiTest::imageTools()
{
    ImageEditor ed;
    ed.resize(1200, 800);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));
    Document* doc = ed.document();
    doc->reset(QSize(400, 300), Qt::white);
    CanvasView* canvas = ed.canvas();
    canvas->fitToWindow();
    ToolSettings* ts = ed.toolSettings();

    // Brush stroke paints the foreground color.
    ed.selectTool(ToolId::Brush);
    ts->setForeground(Qt::red);
    ts->brushSize = 10;
    ts->hardness = 100;
    dragCanvas(canvas, {50, 50}, {150, 50});
    QVERIFY(near(px(doc, 0, 100, 50), qRgb(255, 0, 0)));
    QVERIFY(near(px(doc, 0, 100, 80), qRgb(255, 255, 255)));
    ed.action("undo")->trigger();
    QVERIFY(near(px(doc, 0, 100, 50), qRgb(255, 255, 255)));
    ed.action("redo")->trigger();
    QVERIFY(near(px(doc, 0, 100, 50), qRgb(255, 0, 0)));

    // Rectangle selection, then a fill limited to it.
    ed.selectTool(ToolId::RectSelect);
    dragCanvas(canvas, {200, 100}, {300, 200});
    QVERIFY(doc->hasSelection());
    const QRect sb = doc->selectionBounds();
    QVERIFY2(std::abs(sb.left() - 200) <= 1 && std::abs(sb.width() - 100) <= 2, qPrintable(QString::number(sb.width())));
    ed.selectTool(ToolId::Fill);
    ts->setForeground(Qt::blue);
    ts->tolerance = 10;
    clickCanvas(canvas, {250, 150});
    QVERIFY(near(px(doc, 0, 250, 150), qRgb(0, 0, 255)));
    QVERIFY(near(px(doc, 0, 150, 150), qRgb(255, 255, 255)));

    // Instant filter respects the selection.
    ed.action("invert")->trigger();
    QVERIFY(near(px(doc, 0, 250, 150), qRgb(255, 255, 0)));
    QVERIFY(near(px(doc, 0, 150, 150), qRgb(255, 255, 255)));

    // Moving the selected pixels leaves transparency behind.
    ed.selectTool(ToolId::Move);
    dragCanvas(canvas, {250, 150}, {290, 150});
    QCOMPARE(qAlpha(px(doc, 0, 210, 150)), 0);
    QVERIFY(near(px(doc, 0, 330, 150), qRgb(255, 255, 0)));
    QVERIFY(doc->selectionBounds().left() >= 238);
    ed.action("deselect")->trigger();
    QVERIFY(!doc->hasSelection());

    // Eyedropper reads the composite color.
    ed.selectTool(ToolId::Picker);
    clickCanvas(canvas, {100, 50});
    QCOMPARE(ts->foreground.rgb(), qRgb(255, 0, 0));

    // Gradient on a new layer: from foreground to background.
    ed.action("newLayer")->trigger();
    QCOMPARE(doc->layerCount(), 2);
    ts->setForeground(Qt::black);
    ts->setBackground(Qt::white);
    ed.selectTool(ToolId::Gradient);
    dragCanvas(canvas, {0, 10}, {399, 10});
    QVERIFY(qRed(px(doc, 1, 5, 200)) < 20);
    QVERIFY(qRed(px(doc, 1, 395, 200)) > 235);
    QVERIFY(std::abs(qRed(px(doc, 1, 200, 200)) - 128) < 20);

    // Eraser makes the stroke transparent on the active layer.
    ed.selectTool(ToolId::Eraser);
    ts->brushSize = 20;
    dragCanvas(canvas, {100, 250}, {200, 250});
    QCOMPARE(qAlpha(px(doc, 1, 150, 250)), 0);
    QCOMPARE(qAlpha(px(doc, 1, 150, 200)), 255);

    // Lasso selection.
    ed.selectTool(ToolId::Lasso);
    {
        const QTransform t = canvas->canvasToView();
        mouse(canvas, QEvent::MouseButtonPress, t.map(QPointF(20, 20)), Qt::LeftButton, Qt::LeftButton);
        for (QPointF p : {QPointF(120, 20), QPointF(120, 120), QPointF(20, 120)})
            mouse(canvas, QEvent::MouseMove, t.map(p), Qt::NoButton, Qt::LeftButton);
        mouse(canvas, QEvent::MouseButtonRelease, t.map(QPointF(20, 120)), Qt::LeftButton, Qt::NoButton);
    }
    QVERIFY(doc->hasSelection());
    QVERIFY(doc->selection().contains(QPointF(60, 60)));
    // Shift adds an ellipse to it.
    ed.selectTool(ToolId::EllipseSelect);
    dragCanvas(canvas, {250, 200}, {350, 280}, Qt::ShiftModifier);
    QVERIFY(doc->selection().contains(QPointF(60, 60)));
    QVERIFY(doc->selection().contains(QPointF(300, 240)));
    ed.action("deselect")->trigger();

    // Text tool opens the dialog; a text layer appears.
    ed.selectTool(ToolId::Text);
    acceptNextDialog([](QDialog* d) {
        if (auto* edit = d->findChild<QPlainTextEdit*>())
            edit->setPlainText(QStringLiteral("Тест"));
    });
    clickCanvas(canvas, {30, 30});
    QCOMPARE(doc->layerCount(), 3);
    QVERIFY(doc->layer(2).text.has_value());
    QCOMPARE(doc->layer(2).text->text, QStringLiteral("Тест"));

    // Crop with the crop tool and Enter.
    ed.selectTool(ToolId::Crop);
    dragCanvas(canvas, {10, 10}, {110, 60});
    QTest::keyClick(canvas, Qt::Key_Return);
    QCOMPARE(doc->size(), QSize(100, 50));
    ed.action("undo")->trigger();
    QCOMPARE(doc->size(), QSize(400, 300));

    // Undo history is deep enough.
    QVERIFY(doc->undoStack()->undoLimit() >= 50);
}

void GuiTest::imageDialogsAndLayers()
{
    ImageEditor ed;
    ed.resize(1100, 700);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));
    Document* doc = ed.document();
    QImage img(200, 100, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    for (int y = 0; y < 100; ++y)
        for (int x = 100; x < 200; ++x)
            img.setPixel(x, y, qRgb(255, 255, 255));
    ed.openImage(img, QStringLiteral("edge"));
    QCOMPARE(doc->size(), QSize(200, 100));

    // Gaussian blur through its dialog with live preview.
    acceptNextDialog({}, 700);
    ed.action("blur")->trigger();
    const int mid = qRed(px(doc, 0, 100, 50));
    QVERIFY2(mid > 30 && mid < 225, qPrintable(QString::number(mid)));
    QCOMPARE(doc->undoStack()->count(), 1);

    // Cancelling a dialog restores the original pixels.
    const QImage before = doc->layer(0).image;
    QTimer::singleShot(600, [] {
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
            d->reject();
    });
    ed.action("hueSat")->trigger();
    QCOMPARE(doc->layer(0).image, before);

    // Brightness with a changed value.
    acceptNextDialog([](QDialog* d) {
        const auto spins = d->findChildren<QDoubleSpinBox*>();
        if (!spins.isEmpty())
            spins.first()->setValue(50);
    }, 500);
    ed.action("brightness")->trigger();
    QVERIFY(qRed(px(doc, 0, 5, 50)) > 40);

    acceptNextDialog({}, 400);
    ed.action("curves")->trigger();
    QCOMPARE(doc->undoStack()->count(), 3);

    // Layer operations from the layer menu.
    ed.action("newLayer")->trigger();
    ed.action("dupLayer")->trigger();
    QCOMPARE(doc->layerCount(), 3);
    ed.action("layerDown")->trigger();
    QCOMPARE(doc->activeIndex(), 1);
    ed.action("merge")->trigger();
    QCOMPARE(doc->layerCount(), 2);
    ed.action("delLayer")->trigger();
    QCOMPARE(doc->layerCount(), 1);
    ed.action("rotCW")->trigger();
    QCOMPARE(doc->size(), QSize(100, 200));
    ed.action("flipH")->trigger();
    // Undo flip, rotation, layer deletion and the merge.
    for (int i = 0; i < 4; ++i)
        ed.action("undo")->trigger();
    QCOMPARE(doc->layerCount(), 3);
    QCOMPARE(doc->size(), QSize(200, 100));
}

void GuiTest::videoEditing()
{
    VideoEditor ved;
    ved.resize(1300, 850);
    ved.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ved));
    ved.openFiles({m_video, m_video2, m_audio, m_logo});
    const TimelineState& st = ved.timeline()->state();
    QCOMPARE(st.main.size(), 3); // two videos + the image clip
    QCOMPARE(st.audio.size(), 1);
    QCOMPARE(st.resolution, QSize(640, 360));
    QVERIFY(std::abs(st.duration() - (6 + 3 + kDefaultImageDuration)) < 0.2);
    QTRY_VERIFY_WITH_TIMEOUT(!ved.preview()->frame().isNull(), 5000);

    // Split the first clip at 2 s, then undo.
    ved.seek(2.0);
    ved.timelineWidget()->clearSelection();
    ved.action("split")->trigger();
    QCOMPARE(ved.timeline()->state().main.size(), 4);
    ved.action("undo")->trigger();
    QCOMPARE(ved.timeline()->state().main.size(), 3);

    // Speed from the Clip menu.
    ved.timelineWidget()->select(Track::Main, ved.timeline()->state().main[0].id);
    QMenu* clipMenu = nullptr;
    for (QMenu* m : ved.menus())
        if (m->title().contains(QStringLiteral("Клип")))
            clipMenu = m;
    QVERIFY(clipMenu);
    QAction* twice = nullptr;
    for (QAction* a : clipMenu->actions())
        if (a->menu())
            for (QAction* s : a->menu()->actions())
                if (s->text() == QStringLiteral("2×"))
                    twice = s;
    QVERIFY(twice);
    twice->trigger();
    QVERIFY(std::abs(ved.timeline()->state().main[0].length() - 3.0) < 1e-6);

    // Cut a fragment marked with In/Out.
    const double before = ved.timeline()->state().duration();
    ved.seek(1.0);
    ved.action("markIn")->trigger();
    ved.seek(2.5);
    ved.action("markOut")->trigger();
    ved.action("cutRange")->trigger();
    QVERIFY(std::abs(ved.timeline()->state().duration() - (before - 1.5)) < 1e-3);
    QVERIFY(std::abs(ved.playhead() - 1.0) < 1e-6);

    // Playback advances the playhead and delivers frames.
    const QImage firstFrame = ved.preview()->frame();
    ved.action("play")->trigger();
    QTest::qWait(1500);
    ved.action("play")->trigger();
    QVERIFY2(ved.playhead() > 1.8, qPrintable(QString::number(ved.playhead())));
    QVERIFY(!(ved.preview()->frame() == firstFrame));

    // Overlay from a file and mute toggles.
    ved.timelineWidget()->select(Track::Audio, ved.timeline()->state().audio[0].id);
    ved.action("muteClip")->trigger();
    QVERIFY(ved.timeline()->state().audio[0].muted);
    ved.action("muteOriginal")->trigger();
    QVERIFY(ved.timeline()->state().muteOriginal);

    // Send the current frame to the image editor.
    QSignalSpy spy(&ved, &VideoEditor::sendFrameToImageEditor);
    ved.action("frameToImage")->trigger();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<QImage>().size(), QSize(640, 360));

    // Delete the selected clip.
    ved.timelineWidget()->select(Track::Main, ved.timeline()->state().main.last().id);
    const int n = ved.timeline()->state().main.size();
    ved.action("delete")->trigger();
    QCOMPARE(ved.timeline()->state().main.size(), n - 1);
}

void GuiTest::audioEditing()
{
    AudioEditor aed;
    aed.resize(1100, 600);
    aed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&aed));
    aed.openFiles({m_audio});
    const PcmBuffer& pcm = aed.buffer();
    QVERIFY(std::abs(pcm.duration() - 6.0) < 0.1);
    WaveformWidget* w = aed.waveform();

    w->setSelection(pcm.frameAt(1.0), pcm.frameAt(3.0));
    aed.action("trim")->trigger();
    QVERIFY(std::abs(aed.buffer().duration() - 2.0) < 1e-3);
    aed.action("undo")->trigger();
    QVERIFY(std::abs(aed.buffer().duration() - 6.0) < 0.1);

    w->setSelection(aed.buffer().frameAt(1.0), aed.buffer().frameAt(2.0));
    aed.action("delete")->trigger();
    QVERIFY(std::abs(aed.buffer().duration() - 5.0) < 0.1);

    w->setSelection(0, aed.buffer().frameAt(1.0));
    aed.action("silence")->trigger();
    QCOMPARE(audioops::peak(aed.buffer(), 0, aed.buffer().frameAt(1.0)), 0.f);

    w->setSelection(aed.buffer().frameAt(2.0), aed.buffer().frameAt(3.0));
    aed.action("fadeIn")->trigger();
    QVERIFY(audioops::peak(aed.buffer(), aed.buffer().frameAt(2.0), aed.buffer().frameAt(2.02)) <
            audioops::peak(aed.buffer(), aed.buffer().frameAt(2.9), aed.buffer().frameAt(2.95)));

    w->setSelection(0, 0);
    aed.action("normalize")->trigger();
    QVERIFY(std::abs(audioops::peak(aed.buffer(), 0, aed.buffer().frames()) - 0.891f) < 0.01f);

    // Copy half a second and paste it at the playhead.
    const double d0 = aed.buffer().duration();
    w->setSelection(aed.buffer().frameAt(3.0), aed.buffer().frameAt(3.5));
    aed.action("copy")->trigger();
    w->setSelection(0, 0);
    w->setPlayhead(aed.buffer().frameAt(4.0));
    aed.action("paste")->trigger();
    QVERIFY(std::abs(aed.buffer().duration() - (d0 + 0.5)) < 1e-3);

    // Speed through its dialog (pitch-preserving).
    w->setSelection(0, 0);
    const double d1 = aed.buffer().duration();
    acceptNextDialog([](QDialog* d) {
        if (auto* s = d->findChild<QDoubleSpinBox*>())
            s->setValue(2.0);
    });
    aed.action("speed")->trigger();
    QVERIFY2(std::abs(aed.buffer().duration() - d1 / 2) < 0.05, qPrintable(QString::number(aed.buffer().duration())));

    // Hand the edited sound to the video editor.
    QSignalSpy spy(&aed, &AudioEditor::sendToVideo);
    aed.action("toVideoReplace")->trigger();
    QCOMPARE(spy.count(), 1);
    const QString wav = spy.at(0).at(0).toString();
    QVERIFY(spy.at(0).at(1).toBool());
    PcmBuffer back;
    QString err;
    QVERIFY2(decodeAudio(wav, &back, &err), qPrintable(err));
    QVERIFY(std::abs(back.duration() - aed.buffer().duration()) < 0.01);
    QFile::remove(wav);
    QVERIFY(aed.undoStack()->count() >= 6);
}

void GuiTest::mainWindowRouting()
{
    MainWindow w;
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    auto menuTitles = [&w] {
        QStringList t;
        for (QAction* a : w.menuBar()->actions())
            t << a->text().remove('&');
        return t;
    };
    QVERIFY(menuTitles().contains(QStringLiteral("Фильтры")));
    w.openFiles({m_audio});
    QCOMPARE(w.workspaceIndex(), 2);
    QVERIFY(menuTitles().contains(QStringLiteral("Эффекты")));
    QVERIFY(!menuTitles().contains(QStringLiteral("Фильтры")));
    w.openFiles({m_video});
    QCOMPARE(w.workspaceIndex(), 1);
    QVERIFY(menuTitles().contains(QStringLiteral("Клип")));
    w.openFiles({m_logo});
    QCOMPARE(w.workspaceIndex(), 0);
    QCOMPARE(w.imageEditor()->document()->size(), QSize(120, 60));
    // Video → image editor round trip.
    w.setWorkspace(1);
    w.videoEditor()->action("frameToImage")->trigger();
    QCOMPARE(w.workspaceIndex(), 0);
    QCOMPARE(w.imageEditor()->document()->size(), QSize(640, 360));
    // Nothing is modified by these imports except the image (frame opened unmodified).
    w.imageEditor()->document()->markClean();
    w.videoEditor()->timeline()->undoStack()->setClean();
    w.audioEditor()->undoStack()->setClean();
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    GuiTest t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_gui.moc"
