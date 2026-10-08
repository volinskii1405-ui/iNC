#include "app/MainWindow.h"
#include "app/Theme.h"
#include "media/MediaInfo.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFontDatabase>
#include <QLibraryInfo>
#include <QLocale>
#include <QTimer>
#include <QTranslator>

#include <cstdio>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MediaForge"));
    QApplication::setOrganizationName(QStringLiteral("MediaForge"));
    QApplication::setApplicationVersion(QStringLiteral(MF_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("MediaForge — редактор изображений, видео и звука"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Файлы для открытия"), QStringLiteral("[files...]"));
    QCommandLineOption workspace(QStringLiteral("workspace"), QStringLiteral("Начальная вкладка: image, video, audio"),
                                 QStringLiteral("name"));
    QCommandLineOption screenshot(QStringLiteral("screenshot"),
                                  QStringLiteral("Сохранить снимок окна в файл и выйти (для проверки)"), QStringLiteral("file"));
    QCommandLineOption delay(QStringLiteral("screenshot-delay"), QStringLiteral("Задержка перед снимком, мс"),
                             QStringLiteral("ms"), QStringLiteral("2500"));
    parser.addOptions({workspace, screenshot, delay});
    parser.process(app);

    // The interface is in Russian, so Qt's own dialogs (file, color, message boxes) should be too.
    QTranslator qtTranslator;
    const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../translations"));
    if (qtTranslator.load(QStringLiteral("qtbase_ru"), bundled) ||
        qtTranslator.load(QStringLiteral("qtbase_ru"), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);

    mf::initFFmpegLogging();
    // A bundled font guarantees Cyrillic glyphs even on systems without fonts installed.
    const int fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DejaVuSans.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DejaVuSans-Bold.ttf"));
    if (fontId >= 0) {
        QFont f(QFontDatabase::applicationFontFamilies(fontId).value(0), 10);
        QApplication::setFont(f);
    }
    mf::applyDarkTheme(app);

    mf::MainWindow w;
    w.show();
    const QString ws = parser.value(workspace);
    if (ws == QLatin1String("video"))
        w.setWorkspace(1);
    else if (ws == QLatin1String("audio"))
        w.setWorkspace(2);
    QStringList files;
    for (const QString& f : parser.positionalArguments())
        files << QDir::current().absoluteFilePath(f);
    if (!files.isEmpty())
        QTimer::singleShot(0, &w, [&w, files] { w.openFiles(files); });

    if (parser.isSet(screenshot)) {
        const QString out = parser.value(screenshot);
        QTimer::singleShot(parser.value(delay).toInt(), &w, [&w, out] {
            const bool ok = w.grab().save(out);
            std::fprintf(stderr, "screenshot %s: %s\n", ok ? "saved" : "FAILED", qPrintable(out));
            QApplication::exit(ok ? 0 : 3);
        });
    }
    return app.exec();
}
