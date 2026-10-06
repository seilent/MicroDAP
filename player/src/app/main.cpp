#include "FakeBluezModel.h"
#include "DbusPad.h"
#include "Gamescope.h"
#include "Intercept.h"
#include "Keep.h"
#include "Library.h"
#include "Models.h"
#include "Mpris.h"
#include "OverlayBridge.h"
#include "Pad.h"
#include "Player.h"
#include "PowerBridge.h"
#include "Screenshot.h"
#include "Settings.h"
#include "State.h"
#include "StatusModel.h"
#include "SystemBluezModel.h"
#include "Version.h"

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QQuickWindow>
#include <QTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QTextStream>

#include <cstring>
#include <memory>

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
    const QCommandLineOption angleOption(QStringLiteral("angle"),
                                         QStringLiteral("Turnstile angle in degrees for --screenshot motion."),
                                         QStringLiteral("deg"), QStringLiteral("40"));
    const QCommandLineOption scanOnlyOption(QStringLiteral("scan-only"),
                                            QStringLiteral("Scan [dir] into the library and exit."));
    const QCommandLineOption bluezOption(QStringLiteral("bluez"),
                                         QStringLiteral("Fake Bluez preset for --screenshot: off, connected, active, pairing."),
                                         QStringLiteral("preset"));
    const QCommandLineOption stateOption(QStringLiteral("state"),
                                         QStringLiteral("Fixture state for --screenshot, default browse."),
                                         QStringLiteral("name"));
    const QCommandLineOption libraryOption(QStringLiteral("library"),
                                           QStringLiteral("Music directory scanned into a temporary library for --screenshot."),
                                           QStringLiteral("dir"));
    const QCommandLineOption pivotOption(QStringLiteral("pivot"),
                                         QStringLiteral("Hub pivot for --screenshot hub: artists, albums, songs, genres, playlists."),
                                         QStringLiteral("name"), QStringLiteral("albums"));
    const QCommandLineOption overlayOption(QStringLiteral("overlay"),
                                           QStringLiteral("Run as the game mode overlay: hidden window, pad from InputPlumber."));
    parser.addOptions({versionOption, screenshotOption, sizeOption, outOption, angleOption, scanOnlyOption,
                       bluezOption, stateOption, libraryOption, pivotOption, overlayOption});
    parser.addPositionalArgument(QStringLiteral("dir"),
                                 QStringLiteral("Directory for --scan-only, default ~/Music."),
                                 QStringLiteral("[dir]"));
    parser.process(app);

    if (parser.isSet(versionOption)) {
        QTextStream(stdout) << "casdap-player " << casdap::version() << '\n'
                            << casdap::dependencies() << '\n';
        return 0;
    }

    if (parser.isSet(screenshotOption)) {
        casdap::ScreenshotOptions options;
        options.screen = parser.value(screenshotOption);
        options.size = parser.value(sizeOption);
        options.out = parser.value(outOption);
        options.angle = parser.value(angleOption).toDouble();
        options.bluez = parser.value(bluezOption);
        options.state = parser.value(stateOption);
        options.library = parser.value(libraryOption);
        options.pivot = parser.value(pivotOption);
        return casdap::screenshot(options);
    }

    if (parser.isSet(scanOnlyOption)) {
        const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        return scanOnly(parser.positionalArguments().value(0, music));
    }

    casdap::Settings settings;
    casdap::Library library;
    casdap::HistoryModel historyRecent(&library);
    casdap::HistoryModel historyNew(&library);
    historyNew.setMode(casdap::HistoryModel::New);
    casdap::ArtistsModel artists(&library);
    casdap::AlbumsModel albums(&library);
    casdap::SongsModel songs(&library);
    casdap::GenresModel genres(&library);
    casdap::PinsModel pins(&library);
    casdap::Player player(&library);
    std::unique_ptr<casdap::BluezModel> bluez;
    if (qEnvironmentVariableIntValue("CASDAP_BLUEZ_FAKE") == 1)
        bluez = std::make_unique<casdap::FakeBluezModel>();
    else
        bluez = std::make_unique<casdap::SystemBluezModel>(QDBusConnection::systemBus(),
                                                           QDBusConnection::sessionBus());
    player.setBluez(bluez.get());
    casdap::State state(&player);
    state.installSignalHandlers();
    state.load();
    casdap::Mpris mpris(&player, QDBusConnection::sessionBus());
    casdap::PowerBridge power(&player, QDBusConnection::sessionBus());
    const bool overlayMode = parser.isSet(overlayOption);
    if (overlayMode && !power.isRegistered()) {
        qWarning() << "casdap overlay: another player owns org.casdap.Player, exiting";
        return 0;
    }
    if (!overlayMode) {
        casdap::Keep::clear();
        power.setState(&state);
    }
    casdap::StatusModel status;

    std::unique_ptr<casdap::PadSource> pad;
    std::unique_ptr<casdap::OverlayBridge> overlay;
    if (overlayMode) {
        auto dbusPad = std::make_unique<casdap::DbusPad>(casdap::Intercept::bus());
        overlay = std::make_unique<casdap::OverlayBridge>(dbusPad.get(), &player);
        power.setOverlay(overlay.get());
        pad = std::move(dbusPad);
    } else {
        pad = std::make_unique<casdap::Pad>();
    }

    auto applyActive = [&power, &status, model = bluez.get(), bridge = overlay.get()] {
        const bool active = !power.screenOff() && (!bridge || bridge->mapped());
        status.setActive(active);
        model->setActive(active);
    };
    QObject::connect(&power, &casdap::PowerBridge::screenOffChanged, &status, applyActive);
    if (overlay) {
        QObject::connect(overlay.get(), &casdap::OverlayBridge::mappedChanged, &status, applyActive);
        applyActive();
    }
    pad->setPocketControls(settings.pocketControls());
    QObject::connect(&settings, &casdap::Settings::pocketControlsChanged, pad.get(),
                     [&settings, source = pad.get()] { source->setPocketControls(settings.pocketControls()); });
    QObject::connect(&power, &casdap::PowerBridge::screenOffChanged, pad.get(),
                     [&power, source = pad.get()] { source->setScreenOff(power.screenOff()); });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Library"), &library);
    engine.rootContext()->setContextProperty(QStringLiteral("HistoryRecent"), &historyRecent);
    engine.rootContext()->setContextProperty(QStringLiteral("HistoryNew"), &historyNew);
    engine.rootContext()->setContextProperty(QStringLiteral("Artists"), &artists);
    engine.rootContext()->setContextProperty(QStringLiteral("Albums"), &albums);
    engine.rootContext()->setContextProperty(QStringLiteral("Songs"), &songs);
    engine.rootContext()->setContextProperty(QStringLiteral("Genres"), &genres);
    engine.rootContext()->setContextProperty(QStringLiteral("Pins"), &pins);
    engine.rootContext()->setContextProperty(QStringLiteral("Player"), &player);
    engine.rootContext()->setContextProperty(QStringLiteral("Pad"), pad.get());
    engine.rootContext()->setContextProperty(QStringLiteral("Settings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("PowerBridge"), &power);
    engine.rootContext()->setContextProperty(QStringLiteral("StatusModel"), &status);
    engine.rootContext()->setContextProperty(QStringLiteral("BluezModel"),
                                             static_cast<QObject *>(bluez.get()));
    if (overlay)
        engine.rootContext()->setContextProperty(QStringLiteral("OverlayBridge"), overlay.get());
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Casdap", "Main");
    if (!engine.rootObjects().isEmpty()) {
        auto *root = qobject_cast<QWindow *>(engine.rootObjects().constFirst());
        if (overlayMode)
            overlay->setWindow(root, casdap::setOverlayAtoms);
        else
            casdap::claimGamescopeFocus(root);
    }

    const QString grabOut = qEnvironmentVariable("CASDAP_GRAB_OUT");
    auto *window = engine.rootObjects().isEmpty()
                       ? nullptr
                       : qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!grabOut.isEmpty() && window) {
        const int delay = qEnvironmentVariableIntValue("CASDAP_GRAB_AFTER_MS");
        QTimer::singleShot(delay > 0 ? delay : 3000, window, [window, grabOut] {
            const QImage image = window->grabWindow();
            QTextStream(stderr) << "casdap-player: grab " << image.width() << 'x' << image.height()
                                << (image.save(grabOut, "PNG") ? " saved " : " failed ") << grabOut << '\n';
        });
    }
    return app.exec();
}
