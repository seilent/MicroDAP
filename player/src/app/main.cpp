#include "Screenshot.h"
#include "Version.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTextStream>

int main(int argc, char *argv[])
{
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
    parser.addOptions({versionOption, screenshotOption, sizeOption, outOption});
    parser.process(app);

    if (parser.isSet(versionOption)) {
        QTextStream(stdout) << "casdap-player " << casdap::version() << '\n'
                            << casdap::dependencies() << '\n';
        return 0;
    }

    if (parser.isSet(screenshotOption))
        return casdap::screenshot(parser.value(screenshotOption), parser.value(sizeOption),
                                  parser.value(outOption));

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Casdap", "Main");
    return app.exec();
}
