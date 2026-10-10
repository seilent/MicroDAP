#include "AccentControl.h"
#include "FakeBluezModel.h"
#include "CodecFragment.h"
#include "DbusPad.h"
#include "Eq.h"
#include "Gamescope.h"
#include "GuideChord.h"
#include "HubModels.h"
#include "Intercept.h"
#include "Keep.h"
#include "Library.h"
#include "LibraryWatcher.h"
#include "Models.h"
#include "Mpris.h"
#include "OutputControl.h"
#include "OutputWatch.h"
#include "OverlayBridge.h"
#include "Pad.h"
#include "Player.h"
#include "PowerBridge.h"
#include "RgbControl.h"
#include "Screenshot.h"
#include "SensorRotation.h"
#include "Settings.h"
#include "SscRotation.h"
#include "State.h"
#include "StatusModel.h"
#include "AudioInfo.h"
#include "SystemBluezModel.h"
#include "Version.h"
#include "VolumeKeys.h"

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QQuickGraphicsConfiguration>
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
    const QCommandLineOption orientationOption(QStringLiteral("orientation"),
                                               QStringLiteral("Orientation for --screenshot: portrait, portrait-flipped, landscape, landscape-flipped."),
                                               QStringLiteral("value"), QStringLiteral("portrait"));
    const QCommandLineOption overlayOption(QStringLiteral("overlay"),
                                           QStringLiteral("Run as the game mode overlay: hidden window, pad from InputPlumber."));
    parser.addOptions({versionOption, screenshotOption, sizeOption, outOption, angleOption, scanOnlyOption,
                       bluezOption, stateOption, libraryOption, pivotOption, orientationOption, overlayOption});
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
        options.orientation = parser.value(orientationOption);
        options.overlay = parser.isSet(overlayOption);
        return casdap::screenshot(options);
    }

    if (parser.isSet(scanOnlyOption)) {
        const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        return scanOnly(parser.positionalArguments().value(0, music));
    }

    casdap::Settings settings;
    casdap::Library library;
    casdap::HistoryModel quickplayRecent(&library, casdap::HistoryModel::Recent, 8);
    casdap::HistoryModel quickplayNew(&library, casdap::HistoryModel::New, 8);
    casdap::HistoryModel historyRecent(&library, casdap::HistoryModel::Recent, 100);
    casdap::HistoryModel historyNew(&library, casdap::HistoryModel::New, 100);
    casdap::ArtistsModel artists(&library);
    casdap::AlbumsModel albums(&library);
    casdap::SongsModel songs(&library);
    casdap::GenresModel genres(&library);
    casdap::SectionedModel artistsHub;
    artistsHub.setKind(QStringLiteral("artist"));
    artistsHub.setSource(&artists);
    casdap::SectionedModel songsHub;
    songsHub.setKind(QStringLiteral("song"));
    songsHub.setSource(&songs);
    casdap::SectionedModel genresHub;
    genresHub.setKind(QStringLiteral("genre"));
    genresHub.setSource(&genres);
    casdap::AlbumGridModel albumsGrid;
    albumsGrid.setSource(&albums);
    casdap::PinsModel pins(&library);
    casdap::PlaylistsModel playlists(&library);
    casdap::AudioInfo audioInfo;
    casdap::Player player(&library);
    std::unique_ptr<casdap::BluezModel> bluez;
    if (qEnvironmentVariableIntValue("CASDAP_BLUEZ_FAKE") == 1)
        bluez = std::make_unique<casdap::FakeBluezModel>();
    else
        bluez = std::make_unique<casdap::SystemBluezModel>(QDBusConnection::systemBus(),
                                                           QDBusConnection::sessionBus());
    player.setBluez(bluez.get());
    const bool overlayMode = parser.isSet(overlayOption);
    std::unique_ptr<casdap::EqControl> eqControl = std::make_unique<casdap::EqControl>(&settings);
    QObject::connect(eqControl.get(), &casdap::EqControl::profileApplied, &player, &casdap::Player::applyEq);
    const QString sinkOutput = casdap::EqControl::defaultSinkOutput();
    eqControl->setActiveOutput(sinkOutput.isEmpty() ? QStringLiteral("speaker") : sinkOutput);
    QObject::connect(&library, &casdap::Library::scanningChanged, eqControl.get(), [&library, eq = eqControl.get()] {
        if (!library.scanning())
            eq->relist();
    });
    QObject::connect(&settings, &casdap::Settings::rootsChanged, eqControl.get(), &casdap::EqControl::relist);
    if (overlayMode)
        eqControl->startSinkPoll();
    casdap::State state(&player);
    state.installSignalHandlers();
    state.load();
    casdap::Mpris mpris(&player, QDBusConnection::sessionBus());
    casdap::PowerBridge power(&player, QDBusConnection::sessionBus());
    if (overlayMode && !power.isRegistered()) {
        qWarning() << "casdap overlay: another player owns org.casdap.Player, exiting";
        return 0;
    }
    if (!overlayMode) {
        casdap::Keep::clear();
        power.runScreenOn();
        power.setState(&state);
        power.enableSleepWatch();
    }
    std::unique_ptr<casdap::LibraryWatcher> libraryWatcher;
    if (!overlayMode) {
        library.setSettings(&settings);
        libraryWatcher = std::make_unique<casdap::LibraryWatcher>(&library);
    }
    casdap::StatusModel status;

    casdap::AccentControl accent;
    accent.setSliderHue(settings.accentHue());
    accent.setFromArt(settings.accentFromArt());
    accent.setPhoneActive(bluez->transportActive());
    accent.setArtPath(player.current().value(QStringLiteral("artPath")).toString());
    QObject::connect(&settings, &casdap::Settings::accentHueChanged, &accent,
                     [&settings, &accent] { accent.setSliderHue(settings.accentHue()); });
    QObject::connect(&settings, &casdap::Settings::accentFromArtChanged, &accent,
                     [&settings, &accent] { accent.setFromArt(settings.accentFromArt()); });
    QObject::connect(&player, &casdap::Player::currentChanged, &accent, [&player, &accent] {
        accent.setArtPath(player.current().value(QStringLiteral("artPath")).toString());
    });
    QObject::connect(bluez.get(), &casdap::BluezModel::transportActiveChanged, &accent,
                     [&accent, model = bluez.get()] { accent.setPhoneActive(model->transportActive()); });
    QObject::connect(&library, &casdap::Library::albumsChanged, &accent, &casdap::AccentControl::clearCache);
    std::unique_ptr<casdap::OutputControl> outputControl;
    std::unique_ptr<casdap::CodecFragment> codecFragment;
    std::unique_ptr<casdap::RgbControl> rgbControl;
    if (!overlayMode) {
        outputControl = std::make_unique<casdap::OutputControl>();
        outputControl->setHeadphone(settings.hpGain(), settings.hpAmp());
        outputControl->setSpkGain(settings.spkGain());
        outputControl->apply(settings.output());
        eqControl->setOutputControl(outputControl.get());
        QObject::connect(&settings, &casdap::Settings::outputChanged, outputControl.get(),
                         [&settings, control = outputControl.get()] { control->apply(settings.output()); });
        auto applyHeadphone = [&settings, control = outputControl.get()] {
            control->setHeadphone(settings.hpGain(), settings.hpAmp());
        };
        QObject::connect(&settings, &casdap::Settings::hpGainChanged, outputControl.get(), applyHeadphone);
        QObject::connect(&settings, &casdap::Settings::hpAmpChanged, outputControl.get(), applyHeadphone);
        QObject::connect(&settings, &casdap::Settings::spkGainChanged, outputControl.get(),
                         [&settings, control = outputControl.get()] { control->setSpkGain(settings.spkGain()); });
        codecFragment = std::make_unique<casdap::CodecFragment>();
        codecFragment->setBluez(bluez.get());
        codecFragment->apply(settings.btCodec());
        QObject::connect(&settings, &casdap::Settings::btCodecChanged, codecFragment.get(),
                         [&settings, fragment = codecFragment.get()] { fragment->apply(settings.btCodec()); });
        rgbControl = std::make_unique<casdap::RgbControl>();
        rgbControl->setScreenOff(power.screenOff());
        rgbControl->start(settings.rgb(), accent.hue(), accent.level());
        QObject::connect(&settings, &casdap::Settings::rgbChanged, rgbControl.get(),
                         [&settings, rgb = rgbControl.get()] { rgb->setMode(settings.rgb()); });
        QObject::connect(&accent, &casdap::AccentControl::hueChanged, rgbControl.get(),
                         [&accent, rgb = rgbControl.get()] { rgb->setColor(accent.hue(), accent.level()); });
        QObject::connect(&power, &casdap::PowerBridge::screenOffChanged, rgbControl.get(),
                         [&power, rgb = rgbControl.get()] { rgb->setScreenOff(power.screenOff()); });
    }

    std::unique_ptr<casdap::PadSource> pad;
    std::unique_ptr<casdap::OverlayBridge> overlay;
    if (overlayMode) {
        auto dbusPad = std::make_unique<casdap::DbusPad>(casdap::Intercept::bus());
        overlay = std::make_unique<casdap::OverlayBridge>(dbusPad.get(), &player);
        overlay->setState(&state);
        power.setOverlay(overlay.get());
        pad = std::move(dbusPad);
    } else {
        pad = std::make_unique<casdap::Pad>();
    }

    auto applyActive = [&power, &status, &audioInfo, model = bluez.get(), bridge = overlay.get()] {
        const bool active = !power.screenOff() && (!bridge || bridge->mapped());
        status.setActive(active);
        audioInfo.setActive(active);
        model->setActive(active);
    };
    QObject::connect(&power, &casdap::PowerBridge::screenOffChanged, &status, applyActive);
    if (overlay) {
        QObject::connect(overlay.get(), &casdap::OverlayBridge::mappedChanged, &status, applyActive);
        applyActive();
    } else {
        outputControl->setActive(true);
    }
    QObject::connect(&power, &casdap::PowerBridge::screenOffChanged, pad.get(),
                     [&power, source = pad.get()] { source->setScreenOff(power.screenOff()); });
    std::unique_ptr<casdap::GuideChord> guide;
    std::unique_ptr<casdap::OutputWatch> outputs;
    std::unique_ptr<QObject> sensor;
    std::unique_ptr<casdap::VolumeKeys> volumeKeys;
    if (!overlayMode) {
        guide = std::make_unique<casdap::GuideChord>();
        volumeKeys = std::make_unique<casdap::VolumeKeys>(new casdap::EvdevKeys, new casdap::PulseSinkVolume,
                                                          &settings);
        QObject::connect(volumeKeys.get(), &casdap::VolumeKeys::levelChanged, &power, &casdap::PowerBridge::setVolume);
        const QString backend = qEnvironmentVariable("CASDAP_SENSOR_BACKEND");
        if (backend == QLatin1StringView("ssc")
            || (backend != QLatin1StringView("proxy") && QFileInfo::exists(QStringLiteral("/dev/fastrpc-sdsp"))))
            sensor = std::make_unique<casdap::SscRotation>();
        else
            sensor = std::make_unique<casdap::SensorRotation>();
        outputs = std::make_unique<casdap::OutputWatch>();
        outputs->setScreenOff(power.screenOff());
        QObject::connect(&power, &casdap::PowerBridge::screenOffChanged, outputs.get(),
                         [&power, watch = outputs.get()] { watch->setScreenOff(power.screenOff()); });
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Library"), &library);
    engine.rootContext()->setContextProperty(QStringLiteral("HistoryRecent"), &historyRecent);
    engine.rootContext()->setContextProperty(QStringLiteral("HistoryNew"), &historyNew);
    engine.rootContext()->setContextProperty(QStringLiteral("QuickplayRecent"), &quickplayRecent);
    engine.rootContext()->setContextProperty(QStringLiteral("QuickplayNew"), &quickplayNew);
    engine.rootContext()->setContextProperty(QStringLiteral("ArtistsHub"), &artistsHub);
    engine.rootContext()->setContextProperty(QStringLiteral("SongsHub"), &songsHub);
    engine.rootContext()->setContextProperty(QStringLiteral("GenresHub"), &genresHub);
    engine.rootContext()->setContextProperty(QStringLiteral("AlbumsGrid"), &albumsGrid);
    engine.rootContext()->setContextProperty(QStringLiteral("Playlists"), &playlists);
    engine.rootContext()->setContextProperty(QStringLiteral("AudioInfo"), &audioInfo);
    engine.rootContext()->setContextProperty(QStringLiteral("OutputControl"),
                                             static_cast<QObject *>(outputControl.get()));
    engine.rootContext()->setContextProperty(QStringLiteral("CodecFragment"),
                                             static_cast<QObject *>(codecFragment.get()));
    engine.rootContext()->setContextProperty(QStringLiteral("RgbControl"),
                                             static_cast<QObject *>(rgbControl.get()));
    engine.rootContext()->setContextProperty(QStringLiteral("Eq"),
                                             static_cast<QObject *>(eqControl.get()));
    engine.rootContext()->setContextProperty(QStringLiteral("Artists"), &artists);
    engine.rootContext()->setContextProperty(QStringLiteral("Albums"), &albums);
    engine.rootContext()->setContextProperty(QStringLiteral("Songs"), &songs);
    engine.rootContext()->setContextProperty(QStringLiteral("Genres"), &genres);
    engine.rootContext()->setContextProperty(QStringLiteral("Pins"), &pins);
    engine.rootContext()->setContextProperty(QStringLiteral("Player"), &player);
    engine.rootContext()->setContextProperty(QStringLiteral("Pad"), pad.get());
    engine.rootContext()->setContextProperty(QStringLiteral("Settings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("Accent"), &accent);
    engine.rootContext()->setContextProperty(QStringLiteral("PowerBridge"), &power);
    engine.rootContext()->setContextProperty(QStringLiteral("PlayerState"), &state);
    engine.rootContext()->setContextProperty(QStringLiteral("StatusModel"), &status);
    engine.rootContext()->setContextProperty(QStringLiteral("BluezModel"),
                                             static_cast<QObject *>(bluez.get()));
    if (overlay)
        engine.rootContext()->setContextProperty(QStringLiteral("OverlayBridge"), overlay.get());
    if (guide)
        engine.rootContext()->setContextProperty(QStringLiteral("Guide"), guide.get());
    if (outputs)
        engine.rootContext()->setContextProperty(QStringLiteral("Outputs"), outputs.get());
    if (sensor)
        engine.rootContext()->setContextProperty(QStringLiteral("Sensor"), sensor.get());
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Casdap", "Main");
    if (!engine.rootObjects().isEmpty()) {
        auto *root = qobject_cast<QWindow *>(engine.rootObjects().constFirst());
        if (overlayMode) {
            if (auto *quick = qobject_cast<QQuickWindow *>(root)) {
                const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
                                         + QStringLiteral("/casdap");
                if (QDir().mkpath(cacheDir)) {
                    const QString cacheFile = cacheDir + QStringLiteral("/overlay-pipeline.bin");
                    QQuickGraphicsConfiguration config = quick->graphicsConfiguration();
                    config.setPipelineCacheLoadFile(cacheFile);
                    config.setPipelineCacheSaveFile(cacheFile);
                    quick->setGraphicsConfiguration(config);
                }
            }
            overlay->setWindow(root, casdap::setOverlayAtoms);
            auto *toast = qobject_cast<QWindow *>(qvariant_cast<QObject *>(root->property("toastWindow")));
            if (toast)
                overlay->setToastWindow(toast, casdap::setToastAtoms);
        } else {
            casdap::claimGamescopeFocus(root);
        }
    }

    const QString grabOut = qEnvironmentVariable("CASDAP_GRAB_OUT");
    auto *window = engine.rootObjects().isEmpty()
                       ? nullptr
                       : qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!overlayMode && window)
        QObject::connect(window, &QQuickWindow::frameSwapped, &library,
                         [&library] { library.rescanRoots(); }, Qt::SingleShotConnection);
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
