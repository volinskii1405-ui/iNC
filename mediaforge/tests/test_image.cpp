#include "TestUtil.h"

#include "image/Document.h"
#include "image/Filters.h"
#include "image/FloodFill.h"
#include "image/ImageIO.h"
#include "image/Transform.h"

#include <QFile>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>

using namespace mf;
using namespace testutil;

namespace {

Document* twoLayerDoc(QColor bottom, QColor top, BlendMode mode, QObject* parent)
{
    auto* doc = new Document(parent);
    doc->reset(QSize(8, 8), bottom);
    doc->addLayer(QStringLiteral("top"), solid(8, 8, top));
    doc->setLayerBlendMode(1, mode);
    return doc;
}

int overlayCh(int s, int b)
{
    return b < 128 ? (2 * s * b + 127) / 255 : 255 - (2 * (255 - s) * (255 - b) + 127) / 255;
}

} // namespace

void ImageCoreTest::blendModes_data()
{
    QTest::addColumn<int>("mode");
    QTest::addColumn<QColor>("bottom");
    QTest::addColumn<QColor>("top");
    QTest::addColumn<QColor>("expected");

    const QColor b(200, 100, 50), s(100, 150, 250);
    QTest::newRow("normal") << int(BlendMode::Normal) << b << s << s;
    QTest::newRow("multiply") << int(BlendMode::Multiply) << b << s
                              << QColor(200 * 100 / 255, 100 * 150 / 255, 50 * 250 / 255);
    QTest::newRow("screen") << int(BlendMode::Screen) << b << s
                            << QColor(200 + 100 - 200 * 100 / 255, 100 + 150 - 100 * 150 / 255,
                                      50 + 250 - 50 * 250 / 255);
    QTest::newRow("overlay") << int(BlendMode::Overlay) << b << s
                             << QColor(overlayCh(100, 200), overlayCh(150, 100), overlayCh(250, 50));
    QTest::newRow("difference") << int(BlendMode::Difference) << b << s << QColor(100, 50, 200);
    QTest::newRow("darken") << int(BlendMode::Darken) << b << s << QColor(100, 100, 50);
    QTest::newRow("lighten") << int(BlendMode::Lighten) << b << s << QColor(200, 150, 250);
}

void ImageCoreTest::blendModes()
{
    QFETCH(int, mode);
    QFETCH(QColor, bottom);
    QFETCH(QColor, top);
    QFETCH(QColor, expected);
    Document* doc = twoLayerDoc(bottom, top, BlendMode(mode), this);
    const QRgb got = doc->composite().pixel(3, 3);
    QVERIFY2(near(got, expected.rgba(), 2), qPrintable(rgbaStr(got) + " vs " + rgbaStr(expected.rgba())));
    // Round-trip through the id used in project files.
    bool ok = false;
    QCOMPARE(blendModeFromId(blendModeId(BlendMode(mode)), &ok), BlendMode(mode));
    QVERIFY(ok);
    delete doc;
}

void ImageCoreTest::layerOpacityAndVisibility()
{
    Document doc;
    doc.reset(QSize(4, 4), QColor(0, 0, 0));
    doc.addLayer(QString(), solid(4, 4, QColor(200, 100, 0)));
    doc.setLayerOpacity(1, 0.5);
    QVERIFY(near(doc.composite().pixel(1, 1), qRgb(100, 50, 0), 2));
    doc.setLayerVisible(1, false);
    QVERIFY(near(doc.composite().pixel(1, 1), qRgb(0, 0, 0), 0));
    // Semi-transparent top over transparent bottom keeps alpha.
    Document d2;
    d2.reset(QSize(2, 2), Qt::transparent);
    d2.addLayer(QString(), solid(2, 2, QColor(255, 0, 0)));
    d2.setLayerOpacity(1, 0.4);
    QVERIFY(near(d2.composite().pixel(0, 0), qRgba(102, 0, 0, 102), 2));
}

void ImageCoreTest::layerOperationsWithUndo()
{
    Document doc;
    doc.reset(QSize(10, 10), Qt::white);
    QCOMPARE(doc.layerCount(), 1);
    doc.addLayer(QStringLiteral("A"));
    doc.addLayer(QStringLiteral("B"));
    QCOMPARE(doc.layerCount(), 3);
    QCOMPARE(doc.activeIndex(), 2);
    QCOMPARE(doc.layer(2).name, QStringLiteral("B"));

    doc.duplicateLayer(2);
    QCOMPARE(doc.layerCount(), 4);
    QCOMPARE(doc.layer(3).name, QStringLiteral("B копия"));

    doc.moveLayer(3, 1);
    QCOMPARE(doc.layer(1).name, QStringLiteral("B копия"));
    doc.undoStack()->undo();
    QCOMPARE(doc.layer(3).name, QStringLiteral("B копия"));
    doc.undoStack()->redo();
    QCOMPARE(doc.layer(1).name, QStringLiteral("B копия"));

    doc.deleteLayer(1);
    QCOMPARE(doc.layerCount(), 3);
    doc.undoStack()->undo();
    QCOMPARE(doc.layerCount(), 4);

    // Merge a red layer down onto white with multiply.
    Document m;
    m.reset(QSize(4, 4), QColor(255, 255, 255));
    m.addLayer(QString(), solid(4, 4, QColor(255, 0, 0)));
    m.setLayerBlendMode(1, BlendMode::Multiply);
    m.mergeDown(1);
    QCOMPARE(m.layerCount(), 1);
    QVERIFY(near(m.layer(0).image.pixel(0, 0), qRgb(255, 0, 0), 1));
    m.undoStack()->undo();
    QCOMPARE(m.layerCount(), 2);

    doc.renameLayer(0, QStringLiteral("Фон 2"));
    QCOMPARE(doc.layer(0).name, QStringLiteral("Фон 2"));
    // The last layer can't be deleted.
    Document single;
    single.reset(QSize(2, 2), Qt::white);
    single.deleteLayer(0);
    QCOMPARE(single.layerCount(), 1);
}

void ImageCoreTest::undoDepth()
{
    Document doc;
    doc.reset(QSize(100, 1), Qt::black);
    QVERIFY(doc.undoStack()->undoLimit() >= 50);
    for (int i = 0; i < 70; ++i) {
        doc.beginPixelEdit(0);
        doc.editImage().setPixel(i, 0, qRgb(255, 255, 255));
        doc.endPixelEdit(QStringLiteral("dot"), QRect(i, 0, 1, 1));
    }
    QCOMPARE(doc.undoStack()->count(), 70);
    for (int i = 0; i < 60; ++i)
        doc.undoStack()->undo();
    const QImage img = doc.layer(0).image;
    for (int i = 0; i < 70; ++i)
        QCOMPARE(img.pixel(i, 0) == qRgb(255, 255, 255), i < 10);
    for (int i = 0; i < 60; ++i)
        doc.undoStack()->redo();
    QCOMPARE(doc.layer(0).image.pixel(69, 0), qRgb(255, 255, 255));
}

void ImageCoreTest::filters()
{
    // Gradient test image with alpha.
    QImage img(64, 32, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 64; ++x)
            img.setPixel(x, y, qPremultiply(qRgba(x * 4, y * 8, 255 - x * 2, 128 + y * 4)));

    QVERIFY(imagesEqual(filters::invert(filters::invert(img)), img));

    const QImage gray = filters::grayscale(img).convertToFormat(QImage::Format_ARGB32);
    for (int x = 0; x < 64; x += 7) {
        const QRgb p = gray.pixel(x, 10);
        QVERIFY(std::abs(qRed(p) - qGreen(p)) <= 1 && std::abs(qGreen(p) - qBlue(p)) <= 1);
    }

    const QImage sep = filters::sepia(solid(2, 2, Qt::white));
    QVERIFY2(near(sep.pixel(0, 0), qRgb(255, 255, 239), 1), qPrintable(rgbaStr(sep.pixel(0, 0))));

    const QImage flat = solid(40, 40, QColor(10, 120, 200));
    QVERIFY(imagesEqual(filters::gaussianBlur(flat, 6.0), flat, 1));
    QVERIFY(imagesEqual(filters::sharpen(flat, 1.5, 2.0), flat, 1));

    // Blur must smooth a hard edge.
    QImage edge = solid(40, 10, Qt::black);
    QPainter(&edge).fillRect(20, 0, 20, 10, Qt::white);
    const QImage blurred = filters::gaussianBlur(edge, 3.0);
    const int mid = qRed(blurred.pixel(20, 5));
    QVERIFY(mid > 40 && mid < 215);
    QCOMPARE(qRed(blurred.pixel(0, 5)), 0);

    const QImage n1 = filters::addNoise(flat, 30, false, 7);
    const QImage n2 = filters::addNoise(flat, 30, false, 7);
    QVERIFY(imagesEqual(n1, n2));
    QVERIFY(!imagesEqual(n1, flat));
    const QImage mono = filters::addNoise(flat, 30, true, 3).convertToFormat(QImage::Format_ARGB32);
    const QRgb mp = mono.pixel(5, 5);
    QCOMPARE(qRed(mp) - 10, qGreen(mp) - 120);

    const QImage pix = filters::pixelate(img, 8);
    QCOMPARE(pix.pixel(0, 0), pix.pixel(7, 7));
}

void ImageCoreTest::adjustments()
{
    const QImage flat = solid(4, 4, QColor(100, 100, 100));
    QVERIFY(imagesEqual(filters::brightnessContrast(flat, 0, 0), flat, 0));
    QVERIFY(qRed(filters::brightnessContrast(flat, 50, 0).pixel(0, 0)) > 150);
    QVERIFY(qRed(filters::brightnessContrast(flat, 0, 60).pixel(0, 0)) < 100);

    const QImage red = solid(2, 2, QColor(255, 0, 0));
    QVERIFY(near(filters::hueSaturation(red, 120, 0, 0).pixel(0, 0), qRgb(0, 255, 0), 1));
    const QImage desat = filters::hueSaturation(red, 0, -100, 0);
    QCOMPARE(qRed(desat.pixel(0, 0)), qGreen(desat.pixel(0, 0)));

    const filters::Lut id = filters::curveLut({QPointF(0, 0), QPointF(255, 255)});
    for (int i = 0; i < 256; ++i)
        QCOMPARE(int(id[i]), i);
    const filters::Lut up = filters::curveLut({QPointF(0, 0), QPointF(128, 180), QPointF(255, 255)});
    QCOMPARE(int(up[128]), 180);
    for (int i = 1; i < 256; ++i)
        QVERIFY(up[i] >= up[i - 1]); // monotone

    filters::Levels lv;
    QVERIFY(imagesEqual(filters::levels(flat, lv, filters::Channel::RGB), flat, 0));
    lv.inBlack = 50;
    lv.inWhite = 150;
    const QRgb l = filters::levels(flat, lv, filters::Channel::RGB).pixel(0, 0);
    QVERIFY(near(l, qRgb(128, 128, 128), 1));
    const QRgb lr = filters::levels(flat, lv, filters::Channel::Red).pixel(0, 0);
    QVERIFY(near(lr, qRgb(128, 100, 100), 1));
}

void ImageCoreTest::selectionMasking()
{
    Document doc;
    doc.reset(QSize(20, 20), Qt::black);
    QPainterPath path;
    path.addRect(5, 5, 10, 10);
    doc.setSelection(path, QStringLiteral("sel"));
    QVERIFY(doc.hasSelection());
    const QImage mask = doc.selectionMask();
    QCOMPARE(maskAt(mask, 10, 10), 255);
    QCOMPARE(maskAt(mask, 2, 2), 0);
    QCOMPARE(doc.selectionBounds(), QRect(5, 5, 10, 10));

    const QImage orig = doc.layer(0).image;
    const QImage res = filters::blendWithMask(orig, filters::invert(orig), mask);
    QCOMPARE(res.pixel(10, 10), qRgb(255, 255, 255));
    QCOMPARE(res.pixel(2, 2), qRgb(0, 0, 0));

    const QImage cleared = filters::clearWithMask(orig, mask);
    QCOMPARE(qAlpha(cleared.pixel(10, 10)), 0);
    QCOMPARE(qAlpha(cleared.pixel(1, 1)), 255);

    const QImage filled = filters::fillWithMask(orig, mask, QColor(0, 0, 255), 1.0);
    QCOMPARE(filled.pixel(10, 10), qRgb(0, 0, 255));
    QCOMPARE(filled.pixel(1, 1), qRgb(0, 0, 0));

    doc.undoStack()->undo();
    QVERIFY(!doc.hasSelection());
    QVERIFY(doc.selectionMask().isNull());
}

void ImageCoreTest::transforms()
{
    DocState s;
    s.size = QSize(4, 2);
    Layer l;
    l.image = solid(4, 2, Qt::black);
    l.image.setPixel(0, 0, qRgb(255, 0, 0));
    s.layers = {l};

    const DocState r = transform::rotate(s, 90);
    QCOMPARE(r.size, QSize(2, 4));
    QCOMPARE(r.layers[0].image.pixel(1, 0), qRgb(255, 0, 0)); // top-left goes to top-right
    const DocState r2 = transform::rotate(transform::rotate(r, 90), 180);
    QVERIFY(imagesEqual(r2.layers[0].image, s.layers[0].image));

    const DocState arb = transform::rotate(s, 45);
    QVERIFY(arb.size.width() >= 4 && arb.size.height() >= 4);

    const DocState f = transform::flip(transform::flip(s, true), true);
    QVERIFY(imagesEqual(f.layers[0].image, s.layers[0].image));
    QCOMPARE(transform::flip(s, true).layers[0].image.pixel(3, 0), qRgb(255, 0, 0));
    QCOMPARE(transform::flip(s, false).layers[0].image.pixel(0, 1), qRgb(255, 0, 0));

    const DocState sc = transform::scaleImage(s, QSize(8, 4));
    QCOMPARE(sc.size, QSize(8, 4));
    QCOMPARE(sc.layers[0].image.size(), QSize(8, 4));

    const QPoint off = transform::anchorOffset(QSize(4, 2), QSize(8, 6), 4);
    QCOMPARE(off, QPoint(2, 2));
    const DocState cv = transform::resizeCanvas(s, QSize(8, 6), off);
    QCOMPARE(cv.layers[0].image.pixel(2, 2), qRgb(255, 0, 0));
    QCOMPARE(qAlpha(cv.layers[0].image.pixel(0, 0)), 0);

    const DocState cr = transform::crop(cv, QRect(2, 2, 2, 1));
    QCOMPARE(cr.size, QSize(2, 1));
    QCOMPARE(cr.layers[0].image.pixel(0, 0), qRgb(255, 0, 0));

    QImage dot = solid(20, 20, Qt::transparent);
    dot.setPixel(5, 6, qRgb(1, 2, 3));
    QCOMPARE(transform::contentBounds(dot), QRect(5, 6, 1, 1));
    const QImage moved = transform::offsetLayer(dot, QPoint(3, -2));
    QCOMPARE(moved.pixel(8, 4), qRgb(1, 2, 3));

    // Undoable canvas change through Document.
    Document doc;
    doc.reset(QSize(10, 10), Qt::white);
    doc.commitState(transform::rotate(doc.state(), 90), QStringLiteral("rot"));
    doc.commitState(transform::resizeCanvas(doc.state(), QSize(30, 20), QPoint(0, 0)), QStringLiteral("canvas"));
    QCOMPARE(doc.size(), QSize(30, 20));
    doc.undoStack()->undo();
    doc.undoStack()->undo();
    QCOMPARE(doc.size(), QSize(10, 10));
}

void ImageCoreTest::floodFill()
{
    QImage img = solid(10, 10, Qt::white);
    QPainter p(&img);
    p.fillRect(4, 0, 1, 10, Qt::black); // wall splits the image in two
    p.end();
    const QImage m = floodFillMask(img, QPoint(1, 1), 0, true);
    QCOMPARE(maskAt(m, 0, 9), 255);
    QCOMPARE(maskAt(m, 4, 5), 0);
    QCOMPARE(maskAt(m, 8, 5), 0);
    const QImage g = floodFillMask(img, QPoint(1, 1), 0, false);
    QCOMPARE(maskAt(g, 8, 5), 255);

    QImage grad = solid(10, 1, Qt::black);
    for (int x = 0; x < 10; ++x)
        grad.setPixel(x, 0, qRgb(x * 10, x * 10, x * 10));
    const QImage t = floodFillMask(grad, QPoint(0, 0), 25, true);
    QCOMPARE(maskAt(t, 2, 0), 255);
    QCOMPARE(maskAt(t, 3, 0), 0);
}

void ImageCoreTest::textLayer()
{
    Document doc;
    doc.reset(QSize(200, 80), Qt::white);
    TextInfo t;
    t.text = QStringLiteral("Привет");
    t.font = QFont(QStringLiteral("DejaVu Sans"));
    t.font.setPixelSize(32);
    t.color = Qt::red;
    t.pos = QPoint(10, 10);
    doc.addTextLayer(t);
    QCOMPARE(doc.layerCount(), 2);
    QVERIFY(doc.layer(1).text.has_value());
    QVERIFY(!transform::contentBounds(doc.layer(1).image).isEmpty());
    t.text = QStringLiteral("Мир");
    doc.updateTextLayer(1, t);
    QCOMPARE(doc.layer(1).text->text, QStringLiteral("Мир"));
    // Painting converts the text layer to raster.
    doc.beginPixelEdit(1);
    doc.editImage().setPixel(0, 0, qRgb(0, 0, 0));
    doc.endPixelEdit(QStringLiteral("paint"), QRect(0, 0, 1, 1));
    QVERIFY(!doc.layer(1).text.has_value());
    doc.undoStack()->undo();
    QVERIFY(doc.layer(1).text.has_value());
}

void ImageCoreTest::projectRoundTrip()
{
    QTemporaryDir dir;
    Document doc;
    doc.reset(QSize(64, 48), QColor(10, 20, 30));
    doc.addLayer(QStringLiteral("Красный"), solid(64, 48, QColor(255, 0, 0, 128)));
    doc.setLayerBlendMode(1, BlendMode::Screen);
    doc.setLayerOpacity(1, 0.7);
    doc.addLayer(QStringLiteral("hidden"), solid(64, 48, Qt::green));
    doc.setLayerVisible(2, false);
    TextInfo t;
    t.text = QStringLiteral("abc");
    t.font.setPixelSize(20);
    t.pos = QPoint(3, 4);
    doc.addTextLayer(t);

    const QString path = dir.filePath(QStringLiteral("p.mfp"));
    QString err;
    QVERIFY2(project::save(doc.state(), path, &err), qPrintable(err));
    DocState loaded;
    QVERIFY2(project::load(path, &loaded, &err), qPrintable(err));
    QCOMPARE(loaded.size, doc.size());
    QCOMPARE(loaded.layers.size(), doc.layerCount());
    QCOMPARE(loaded.active, doc.activeIndex());
    for (int i = 0; i < loaded.layers.size(); ++i) {
        QCOMPARE(loaded.layers[i].name, doc.layer(i).name);
        QCOMPARE(loaded.layers[i].visible, doc.layer(i).visible);
        QCOMPARE(loaded.layers[i].mode, doc.layer(i).mode);
        QVERIFY(qAbs(loaded.layers[i].opacity - doc.layer(i).opacity) < 1e-9);
        QVERIFY(imagesEqual(loaded.layers[i].image, doc.layer(i).image));
    }
    QVERIFY(loaded.layers[3].text.has_value());
    QCOMPARE(loaded.layers[3].text->text, QStringLiteral("abc"));
    QCOMPARE(loaded.layers[3].text->pos, QPoint(3, 4));
}

void ImageCoreTest::projectCorrupted()
{
    QTemporaryDir dir;
    QString err;
    DocState s;

    QVERIFY(!project::load(dir.filePath(QStringLiteral("missing.mfp")), &s, &err));
    QVERIFY(!err.isEmpty());

    const QString junk = dir.filePath(QStringLiteral("junk.mfp"));
    QFile f(junk);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("this is not a project at all");
    f.close();
    err.clear();
    QVERIFY(!project::load(junk, &s, &err));
    QVERIFY(!err.isEmpty());

    // Valid file cut in half.
    Document doc;
    doc.reset(QSize(32, 32), Qt::red);
    doc.addLayer();
    const QString good = dir.filePath(QStringLiteral("good.mfp"));
    QVERIFY(project::save(doc.state(), good, &err));
    QFile g(good);
    QVERIFY(g.open(QIODevice::ReadOnly));
    const QByteArray data = g.readAll();
    g.close();
    const QString cut = dir.filePath(QStringLiteral("cut.mfp"));
    QFile c(cut);
    QVERIFY(c.open(QIODevice::WriteOnly));
    c.write(data.left(data.size() / 2));
    c.close();
    err.clear();
    QVERIFY(!project::load(cut, &s, &err));
    QVERIFY(!err.isEmpty());

    // Writing into a directory that doesn't exist reports an error instead of crashing.
    err.clear();
    QVERIFY(!project::save(doc.state(), QStringLiteral("/nonexistent-dir/x.mfp"), &err));
    QVERIFY(!err.isEmpty());
}

void ImageCoreTest::imageFormats()
{
    QTemporaryDir dir;
    QImage img(40, 30, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 40; ++x)
            img.setPixel(x, y, qRgb(x * 6, y * 8, 100));
    for (const QString suffix : {"png", "jpg", "webp", "bmp", "tiff"}) {
        const QString path = dir.filePath("img." + suffix);
        QString err;
        QVERIFY2(imageio::saveImage(img, path, 95, &err), qPrintable(suffix + ": " + err));
        const auto res = imageio::loadImage(path);
        QVERIFY2(res.error.isEmpty(), qPrintable(suffix + ": " + res.error));
        QCOMPARE(res.image.size(), img.size());
        const bool lossless = suffix == "png" || suffix == "bmp" || suffix == "tiff";
        QVERIFY2(imagesEqual(res.image, img, lossless ? 0 : 24), qPrintable(suffix));
    }
    QString err;
    QVERIFY(!imageio::saveImage(img, dir.filePath(QStringLiteral("x.xyz")), 90, &err));
    QVERIFY(!err.isEmpty());
}

void ImageCoreTest::brokenImage()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("broken.png"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("\x89PNG\r\n\x1a\n garbage garbage garbage");
    f.close();
    auto res = imageio::loadImage(path);
    QVERIFY(res.image.isNull());
    QVERIFY(!res.error.isEmpty());

    res = imageio::loadImage(dir.filePath(QStringLiteral("nope.jpg")));
    QVERIFY(res.image.isNull());
    QVERIFY(!res.error.isEmpty());
}
