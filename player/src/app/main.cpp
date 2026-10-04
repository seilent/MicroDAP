#include "Library.h"
#include "Player.h"
#include "Screenshot.h"
#include "State.h"
#include "Version.h"

#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QTextStream>

#include <cstring>

namespace {

int scanOnly(const QString &dir)
{
    casdap::Library library;
    if (!library.isOpen()) {
        QTextStream(stderr) << "casdap-player: cannot open " << library.dbPath() << '\n';
        return 1;
    }

    QElapsedTimer timer;
    timer.start();
    QObject::connect(&library, &casdap::Library::scanned, &library,
                     [&library, &timer](int, int, int, bool ok) {
        if (!ok) {
            QTextStream(stderr) << "casdap-player: scan failed\n";
            QCoreApplication::exit(1);
            return;
        }
        const QVariantMap counts = library.counts();
        QTextStream(stdout) << "scanned " << counts.value(QStringLiteral("tracks")).toInt()
                            << " tracks, " << counts.value(QStringLiteral("albums")).toInt()
                            << " albums in " << timer.elapsed() << " ms\n";
        QCoreApplication::exit(0);
    });
    library.rescan({QDir(dir).absolutePath()});
    return QCoreApplication::exec();
}

}

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--scan-only") == 0 && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    casdap::initGStreamer(argc, argv);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("casdap-player"));
    QGuiApplication::setApplicationVersion(casdap::version());

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption versionOption(QStringLiteral("version"),
                                           QStringLiteral("Print the version and exit."));
    const QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
                                              QStringLiteral("Render <screen> offscreen to a PNG and exit."),
                                              QStringLiteral("screen"));
    const QCommandLineOption sizeOption(QStringLiteral("size"),
                                        QStringLiteral("Window size for --screenshot."),
                                        QStringLiteral("WxH"), QStringLiteral("640x960"));
    const QCommandLineOption outOption(QStringLiteral("out"),
                                       QStringLiteral("PNG path for --screenshot."),
                                       QStringLiteral("png"));
    const QCommandLineOption scanOnlyOption(QStringLiteral("scan-only"),
                                            QStringLiteral("Scan [dir] into the library and exit."));
    parser.addOptions({versionOption, screenshotOption, sizeOption, outOption, scanOnlyOption});
    parser.addPositionalArgument(QStringLiteral("dir"),
                                 QStringLiteral("Directory for --scan-only, default ~/Music."),
                                 QStringLiteral("[dir]"));
    parser.process(app);

    if (parser.isSet(versionOption)) {
        QTextStream(stdout) << "casdap-player " << casdap::version() << '\n'
                            << casdap::dependencies() << '\n';
        return 0;
    }

    if (parser.isSet(screenshotOption))
        return casdap::screenshot(parser.value(screenshotOption), parser.value(sizeOption),
                                  parser.value(outOption));

    if (parser.isSet(scanOnlyOption)) {
        const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        return scanOnly(parser.positionalArguments().value(0, music));
    }

    casdap::Library library;
    casdap::Player player(&library);
    casdap::State state(&player);
    state.installSignalHandlers();
    state.load();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Library"), &library);
    engine.rootContext()->setContextProperty(QStringLiteral("Player"), &player);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Casdap", "Main");
    return app.exec();
}
