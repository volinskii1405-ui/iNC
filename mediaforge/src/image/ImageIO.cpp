#include "image/ImageIO.h"

#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>
#include <QStorageInfo>

namespace mf::imageio {

namespace {

QString formatForSuffix(const QString& suffix)
{
    const QString s = suffix.toLower();
    if (s == "jpg" || s == "jpeg")
        return QStringLiteral("jpg");
    if (s == "tif" || s == "tiff")
        return QStringLiteral("tiff");
    if (s == "png" || s == "webp" || s == "bmp")
        return s;
    return QString();
}

bool formatSupported(const QString& fmt, const QList<QByteArray>& list)
{
    for (const QByteArray& f : list)
        if (QString::fromLatin1(f).compare(fmt, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

QString readerErrorText(QImageReader::ImageReaderError err, const QString& raw)
{
    switch (err) {
    case QImageReader::FileNotFoundError: return QStringLiteral("Файл не найден.");
    case QImageReader::DeviceError: return QStringLiteral("Не удалось прочитать файл (нет доступа или ошибка устройства).");
    case QImageReader::UnsupportedFormatError:
        return QStringLiteral("Формат файла не поддерживается или файл не является изображением.");
    case QImageReader::InvalidDataError: return QStringLiteral("Файл повреждён: данные изображения некорректны.");
    default: break;
    }
    return raw.isEmpty() ? QStringLiteral("Неизвестная ошибка чтения изображения.") : raw;
}

} // namespace

QStringList readableSuffixes()
{
    QStringList out;
    const auto fmts = QImageReader::supportedImageFormats();
    for (const char* f : {"png", "jpg", "jpeg", "webp", "bmp", "tif", "tiff", "gif"})
        if (formatSupported(QString::fromLatin1(f), fmts))
            out << QString::fromLatin1(f);
    return out;
}

QStringList writableSuffixes()
{
    QStringList out;
    const auto fmts = QImageWriter::supportedImageFormats();
    for (const char* f : {"png", "jpg", "webp", "bmp", "tiff"})
        if (formatSupported(QString::fromLatin1(f), fmts))
            out << QString::fromLatin1(f);
    return out;
}

QString openFilter()
{
    QStringList globs;
    for (const QString& s : readableSuffixes())
        globs << "*." + s << "*." + s.toUpper();
    globs << "*.mfp";
    return QStringLiteral("Изображения и проекты (%1);;Проект MediaForge (*.mfp);;Все файлы (*)").arg(globs.join(' '));
}

QString exportFilter()
{
    QStringList parts;
    for (const QString& s : writableSuffixes()) {
        if (s == "jpg")
            parts << QStringLiteral("JPEG (*.jpg *.jpeg)");
        else if (s == "tiff")
            parts << QStringLiteral("TIFF (*.tif *.tiff)");
        else
            parts << QStringLiteral("%1 (*.%2)").arg(s.toUpper(), s);
    }
    return parts.join(QStringLiteral(";;"));
}

bool hasFreeSpace(const QString& path, qint64 bytesNeeded, QString* error)
{
    QStorageInfo info(QFileInfo(path).absolutePath());
    if (!info.isValid() || !info.isReady())
        return true; // can't tell; the write itself will report failure
    if (info.bytesAvailable() >= bytesNeeded)
        return true;
    if (error)
        *error = QStringLiteral("Недостаточно места на диске: нужно около %1 МБ, свободно %2 МБ.")
                     .arg(bytesNeeded / (1024 * 1024) + 1)
                     .arg(info.bytesAvailable() / (1024 * 1024));
    return false;
}

LoadResult loadImage(const QString& path)
{
    LoadResult res;
    QFileInfo fi(path);
    if (!fi.exists()) {
        res.error = QStringLiteral("Файл не найден: %1").arg(path);
        return res;
    }
    if (!fi.isReadable()) {
        res.error = QStringLiteral("Нет прав на чтение файла: %1").arg(path);
        return res;
    }
    QImageReader::setAllocationLimit(2048);
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        res.error = readerErrorText(reader.error(), reader.errorString());
        return res;
    }
    QImage img = reader.read();
    if (img.isNull()) {
        res.error = readerErrorText(reader.error(), reader.errorString());
        return res;
    }
    res.image = img.convertToFormat(kLayerFormat);
    return res;
}

bool saveImage(const QImage& image, const QString& path, int quality, QString* error)
{
    auto fail = [error](const QString& msg) {
        if (error)
            *error = msg;
        return false;
    };
    const QString fmt = formatForSuffix(QFileInfo(path).suffix());
    if (fmt.isEmpty())
        return fail(QStringLiteral("Неизвестное расширение файла. Используйте .png, .jpg, .webp, .bmp или .tiff."));
    if (!formatSupported(fmt, QImageWriter::supportedImageFormats()))
        return fail(QStringLiteral("Запись в формат %1 недоступна: нет плагина Qt imageformats.").arg(fmt.toUpper()));
    if (image.isNull())
        return fail(QStringLiteral("Пустое изображение."));

    QImage out;
    if (fmt == "jpg" || fmt == "bmp") {
        out = QImage(image.size(), QImage::Format_RGB32);
        out.fill(Qt::white);
        QPainter p(&out);
        p.drawImage(0, 0, image);
    } else {
        out = image.convertToFormat(QImage::Format_ARGB32);
    }

    QString err;
    if (!hasFreeSpace(path, qint64(out.width()) * out.height() * 4 + 65536, &err))
        return fail(err);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("Не удалось открыть файл для записи: %1").arg(file.errorString()));
    QImageWriter writer(&file, fmt.toLatin1());
    if (fmt == "jpg" || fmt == "webp")
        writer.setQuality(qBound(0, quality, 100));
    if (fmt == "tiff")
        writer.setCompression(1);
    if (!writer.write(out)) {
        file.cancelWriting();
        return fail(QStringLiteral("Ошибка записи изображения: %1").arg(writer.errorString()));
    }
    if (!file.commit())
        return fail(QStringLiteral("Не удалось сохранить файл: %1").arg(file.errorString()));
    return true;
}

} // namespace mf::imageio

namespace mf::project {

namespace {
constexpr quint32 kMagic = 0x4D465031; // "MFP1"
constexpr quint16 kVersion = 1;
constexpr int kMaxSide = 30000;
constexpr int kMaxLayers = 2000;
} // namespace

bool save(const DocState& state, const QString& path, QString* error)
{
    auto fail = [error](const QString& msg) {
        if (error)
            *error = msg;
        return false;
    };
    QByteArray payload;
    {
        QDataStream out(&payload, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        out << kMagic << kVersion;
        out << state.size << qint32(state.active) << qint32(state.layers.size());
        for (const Layer& l : state.layers) {
            out << l.name << l.visible << double(l.opacity) << blendModeId(l.mode);
            out << bool(l.text.has_value());
            if (l.text)
                out << l.text->text << l.text->font.toString() << l.text->color << l.text->pos;
            QByteArray png;
            QBuffer buf(&png);
            buf.open(QIODevice::WriteOnly);
            if (!l.image.save(&buf, "PNG"))
                return fail(QStringLiteral("Не удалось закодировать слой «%1».").arg(l.name));
            out << png;
        }
        if (out.status() != QDataStream::Ok)
            return fail(QStringLiteral("Ошибка сериализации проекта."));
    }
    QString err;
    if (!imageio::hasFreeSpace(path, payload.size() + 65536, &err))
        return fail(err);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("Не удалось открыть файл для записи: %1").arg(file.errorString()));
    if (file.write(payload) != payload.size()) {
        const QString e = file.errorString();
        file.cancelWriting();
        return fail(QStringLiteral("Ошибка записи: %1").arg(e));
    }
    if (!file.commit())
        return fail(QStringLiteral("Не удалось сохранить проект: %1").arg(file.errorString()));
    return true;
}

bool load(const QString& path, DocState* state, QString* error)
{
    auto fail = [error](const QString& msg) {
        if (error)
            *error = msg;
        return false;
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Не удалось открыть проект: %1").arg(file.errorString()));
    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    quint16 version = 0;
    in >> magic >> version;
    if (in.status() != QDataStream::Ok || magic != kMagic)
        return fail(QStringLiteral("Это не файл проекта MediaForge или он повреждён."));
    if (version > kVersion)
        return fail(QStringLiteral("Проект создан более новой версией программы (формат %1).").arg(version));

    DocState s;
    qint32 active = 0, count = 0;
    in >> s.size >> active >> count;
    if (in.status() != QDataStream::Ok || s.size.width() <= 0 || s.size.height() <= 0 || s.size.width() > kMaxSide ||
        s.size.height() > kMaxSide || count <= 0 || count > kMaxLayers)
        return fail(QStringLiteral("Файл проекта повреждён: некорректный заголовок."));

    for (int i = 0; i < count; ++i) {
        Layer l;
        double opacity = 1.0;
        QString modeId;
        bool hasText = false;
        in >> l.name >> l.visible >> opacity >> modeId >> hasText;
        if (hasText) {
            TextInfo t;
            QString fontStr;
            in >> t.text >> fontStr >> t.color >> t.pos;
            t.font.fromString(fontStr);
            l.text = t;
        }
        QByteArray png;
        in >> png;
        if (in.status() != QDataStream::Ok)
            return fail(QStringLiteral("Файл проекта повреждён или обрезан (слой %1 из %2).").arg(i + 1).arg(count));
        l.opacity = qBound(0.0, opacity, 1.0);
        l.mode = blendModeFromId(modeId);
        QImage img;
        if (!img.loadFromData(png, "PNG") || img.size() != s.size)
            return fail(QStringLiteral("Файл проекта повреждён: не удалось прочитать слой «%1».").arg(l.name));
        l.image = img.convertToFormat(kLayerFormat);
        s.layers.push_back(l);
    }
    s.active = qBound(0, int(active), int(s.layers.size()) - 1);
    *state = s;
    return true;
}

} // namespace mf::project
