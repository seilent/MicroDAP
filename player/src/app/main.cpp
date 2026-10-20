#include "AccentControl.h"
#include "AlsaVolume.h"
#include "FakeBluezModel.h"
#include "DacEq.h"
#include "DbusPad.h"
#include "DeviceReservation.h"
#include "Eq.h"
#include "Favorites.h"
#include "Gamescope.h"
#include "GuideKey.h"
#include "HubModels.h"
#include "Intercept.h"
#include "Keep.h"
#include "Library.h"
#include "LibraryWatcher.h"
#include "LinkProfile.h"
#include "LinkServer.h"
#include "Lyrics.h"
#include "Models.h"
#include "Mpris.h"
#include "OutputControl.h"
#include "OutputMode.h"
#include "OutputWatch.h"
#include "OverlayBridge.h"
#include "PlayThrough.h"
#include "Player.h"
#include "PowerBridge.h"
#include "RgbControl.h"
#include "Rotation.h"
#include "Screenshot.h"
#include "Settings.h"
#include "State.h"
#include "StatusModel.h"
#include "AudioInfo.h"
#include "SystemBluezModel.h"
#include "Version.h"
#include "VolumeKeys.h"
#include "WirePlumberRestart.h"

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
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
    microdap::Library library;
    if (!library.isOpen()) {
        QTextStream(stderr) << "microdap-player: cannot open " << library.dbPath() << '\n';
        return 1;
    }

    QElapsedTimer timer;
    timer.start();
    QObject::connect(&library, &microdap::Library::scanned, &library,
                     [&library, &timer](int, int, int, bool ok) {
        if (!ok) {
            QTextStream(stderr) << "microdap-player: scan failed\n";
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
    microdap::initGStreamer(argc, argv);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("microdap-player"));
    QGuiApplication::setApplicationVersion(microdap::version());

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
                                         QStringLiteral("Hub pivot for --screenshot hub: artists, albums, songs, genres, playlists, folders."),
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
        QTextStream(stdout) << "microdap-player " << microdap::version() << '\n'
                            << microdap::dependencies() << '\n';
        return 0;
    }

    if (parser.isSet(screenshotOption)) {
        microdap::ScreenshotOptions options;
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
        return microdap::screenshot(options);
    }

    if (parser.isSet(scanOnlyOption)) {
        const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        return scanOnly(parser.positionalArguments().value(0, music));
    }

    microdap::Settings settings;
    microdap::Library library;
    microdap::HistoryModel quickplayRecent(&library, microdap::HistoryModel::Recent, 8);
    microdap::HistoryModel quickplayNew(&library, microdap::HistoryModel::New, 8);
    microdap::HistoryModel historyRecent(&library, microdap::HistoryModel::Recent, 100);
    microdap::HistoryModel historyNew(&library, microdap::HistoryModel::New, 100);
    microdap::ArtistsModel artists(&library);
    microdap::AlbumsModel albums(&library);
    microdap::SongsModel songs(&library);
    microdap::GenresModel genres(&library);
    microdap::SectionedModel artistsHub;
    artistsHub.setKind(QStringLiteral("artist"));
    artistsHub.setSource(&artists);
    microdap::SectionedModel songsHub;
    songsHub.setKind(QStringLiteral("song"));
    songsHub.setSource(&songs);
    microdap::SectionedModel genresHub;
    genresHub.setKind(QStringLiteral("genre"));
    genresHub.setSource(&genres);
    microdap::FolderModel foldersHub(&library);
    foldersHub.setRoots(settings.roots());
    QObject::connect(&settings, &microdap::Settings::rootsChanged, &foldersHub,
                     [&settings, &foldersHub] { foldersHub.setRoots(settings.roots()); });
    microdap::AlbumGridModel albumsGrid;
    albumsGrid.setSource(&albums);
    microdap::PinsModel pins(&library);
    microdap::PlaylistsModel playlists(&library);
    microdap::AudioInfo audioInfo;
    microdap::Player player(&library);
    microdap::Lyrics lyrics;
    const auto lyricsTracks = [queue = player.queue(), &lyrics] {
        const auto track = [queue](int row) {
            if (row < 0 || row >= queue->count())
                return microdap::LyricsTrack{};
            const microdap::QueueEntry entry = queue->entry(row);
            return microdap::LyricsTrack{entry.path, microdap::cueFrameMs(entry.startFrame),
                                         entry.endFrame ? microdap::cueFrameMs(entry.endFrame) : 0, entry.cueIndex > 0};
        };
        lyrics.setTracks(track(queue->currentIndex()), track(queue->nextIndex()));
    };
    for (const auto signal : {&microdap::Queue::indexChanged, &microdap::Queue::countChanged,
                              &microdap::Queue::orderChanged, &microdap::Queue::repeatChanged,
                              &microdap::Queue::shuffleChanged})
        QObject::connect(player.queue(), signal, &lyrics, lyricsTracks);
    QObject::connect(&library, &microdap::Library::scanned, &lyrics, &microdap::Lyrics::refresh);
    lyricsTracks();
    std::unique_ptr<microdap::BluezModel> bluez;
    if (qEnvironmentVariableIntValue("MICRODAP_BLUEZ_FAKE") == 1)
        bluez = std::make_unique<microdap::FakeBluezModel>();
    else
        bluez = std::make_unique<microdap::SystemBluezModel>(QDBusConnection::systemBus(),
                                                           QDBusConnection::sessionBus());
    player.setBluez(bluez.get());
    const bool overlayMode = parser.isSet(overlayOption);
    if (auto *system = dynamic_cast<microdap::SystemBluezModel *>(bluez.get()); system && !overlayMode) {
        system->setDapSession(true);
        QObject::connect(&player, &microdap::Player::statusChanged, system, [&player, system] {
            if (player.status() == microdap::Player::Playing)
                system->want();
        });
    }
    if (!overlayMode) {
        settings.importLegacy();
        QObject::connect(&settings, &microdap::Settings::availableChanged, &settings, [&settings] {
            if (settings.available())
                settings.importLegacy();
        });
    }
    std::unique_ptr<microdap::EqControl> eqControl = std::make_unique<microdap::EqControl>(&settings);
    QObject::connect(&library, &microdap::Library::scanningChanged, eqControl.get(), [&library, eq = eqControl.get()] {
        if (!library.scanning())
            eq->relist();
    });
    std::unique_ptr<microdap::OutputControl> outputControl;
    std::unique_ptr<microdap::WirePlumberRestart> wireplumber;
    std::unique_ptr<microdap::AlsaVolume> alsaVolume;
    std::unique_ptr<microdap::DeviceReservation> reservation;
    std::unique_ptr<microdap::VolumeKeys> volumeKeys;
    std::unique_ptr<microdap::DacEq> dacEq;
    std::unique_ptr<microdap::OutputMode> outputMode;
    microdap::PulseSinkVolume *pulseVolume = nullptr;
    microdap::RoutedSinkVolume *routedVolume = nullptr;
    if (!overlayMode) {
        const microdap::OutputMode::Bounds bounds{};
        outputControl = std::make_unique<microdap::OutputControl>();
        wireplumber = std::make_unique<microdap::WirePlumberRestart>();
        wireplumber->setBluez(bluez.get());
        alsaVolume = std::make_unique<microdap::AlsaVolume>(std::make_unique<microdap::SndMixer>(), bounds.pacingMs);
        reservation = std::make_unique<microdap::DeviceReservation>(bounds.reservation);
        pulseVolume = new microdap::PulseSinkVolume;
        routedVolume = new microdap::RoutedSinkVolume(pulseVolume, pulseVolume, alsaVolume.get(), bounds.storeDelayMs);
        routedVolume->setCap(settings.available() ? settings.maxVolume()
                                                  : microdap::RoutedSinkVolume::lastCap(settings.maxVolume()));
        routedVolume->setPowerOn(settings.powerOnVolume());
        QObject::connect(&settings, &microdap::Settings::maxVolumeChanged, routedVolume,
                         [&settings, routedVolume] { routedVolume->setCap(settings.maxVolume()); });
        QObject::connect(&settings, &microdap::Settings::availableChanged, routedVolume,
                         [&settings, routedVolume] { routedVolume->setCap(settings.maxVolume()); });
        QObject::connect(&settings, &microdap::Settings::powerOnVolumeChanged, routedVolume,
                         [&settings, routedVolume] { routedVolume->setPowerOn(settings.powerOnVolume()); });
        microdap::DacEq::Ports dacPorts;
        dacPorts.eq = eqControl.get();
        dacPorts.dac = pulseVolume;
        dacPorts.player = &player;
        dacPorts.usb = std::make_unique<microdap::UsbfsIo>();
        dacPorts.paths = microdap::fiioPathsFromEnv();
        dacPorts.releasing = [&outputMode] { return outputMode && outputMode->releasing(); };
        dacEq = std::make_unique<microdap::DacEq>(microdap::DacEq::Bounds{}, std::move(dacPorts));
        microdap::OutputMode::Ports ports;
        ports.player = &player;
        ports.reservation = reservation.get();
        ports.output = outputControl.get();
        ports.dac = pulseVolume;
        ports.volume = routedVolume;
        ports.mixer = alsaVolume.get();
        ports.wireplumber = wireplumber.get();
        ports.bluez = bluez.get();
        ports.dacEq = dacEq.get();
        outputMode = std::make_unique<microdap::OutputMode>(bounds, ports);
        outputMode->setLevel(settings.outputLevel());
        outputMode->setSampleRate(settings.sampleRate());
        player.setPolicy(outputMode.get());
        microdap::DacEq *dac = dacEq.get();
        microdap::OutputMode *mode = outputMode.get();
        microdap::OutputControl *control = outputControl.get();
        QObject::connect(mode, &microdap::OutputMode::effectiveChanged, dac,
                         [dac, mode] { dac->onEffectiveChanged(mode->effective()); });
        QObject::connect(mode, &microdap::OutputMode::cardChanged, dac, &microdap::DacEq::onCardChanged);
        QObject::connect(mode, &microdap::OutputMode::generationChanged, dac, &microdap::DacEq::onGenerationChanged);
        QObject::connect(mode, &microdap::OutputMode::cardGoneChanged, dac, &microdap::DacEq::onCardGoneChanged);
        QObject::connect(mode, &microdap::OutputMode::exHeldChanged, dac, &microdap::DacEq::onExHeldChanged,
                         Qt::QueuedConnection);
        QObject::connect(control, &microdap::OutputControl::applied, dac, &microdap::DacEq::onApplied);
        QObject::connect(&settings, &microdap::Settings::outputLevelChanged, dac,
                         [&settings, dac] { dac->setLevel(settings.outputLevel()); });
        QObject::connect(&player, &microdap::Player::statusChanged, dac, &microdap::DacEq::onPlayStateChanged);
        QObject::connect(&player, &microdap::Player::currentChanged, dac, [&player, dac] {
            dac->setStreamRate(player.current().value(QStringLiteral("sampleRate")).toInt());
        });
        QObject::connect(control, &microdap::OutputControl::activeOutputChanged, dac,
                         [control, dac] { dac->onActiveOutputChanged(control->activeOutput()); });
        QObject::connect(eqControl.get(), &microdap::EqControl::profileChanged, dac, &microdap::DacEq::onProfileChanged);
        QObject::connect(pulseVolume, &microdap::PulseSinkVolume::sinkAppeared, dac, &microdap::DacEq::onSinkAppeared);
        QObject::connect(pulseVolume, &microdap::PulseSinkVolume::cardRemoved, dac, &microdap::DacEq::onCardRemoved);
        dac->setLevel(settings.outputLevel());
        dac->onEffectiveChanged(mode->effective());
        dac->onActiveOutputChanged(control->activeOutput());
        dac->onPlayStateChanged();
        dac->start();
    }
    if (overlayMode) {
        const QString sinkOutput = microdap::EqControl::defaultSinkOutput();
        eqControl->setActiveOutput(sinkOutput.isEmpty() ? QStringLiteral("speaker") : sinkOutput);
        eqControl->startSinkPoll();
    } else {
        eqControl->awaitOutput(outputControl.get());
    }
    settings.setOutputSlot(eqControl->activeOutput());
    QObject::connect(eqControl.get(), &microdap::EqControl::activeOutputChanged, &settings,
                     [&settings, eq = eqControl.get()] { settings.setOutputSlot(eq->activeOutput()); });
    player.setReplayGain(settings.replaygain());
    QObject::connect(&settings, &microdap::Settings::replaygainChanged, &player,
                     [&settings, &player] { player.setReplayGain(settings.replaygain()); });
    player.setCrossfade(settings.crossfadeMs());
    QObject::connect(&settings, &microdap::Settings::crossfadeMsChanged, &player,
                     [&settings, &player] { player.setCrossfade(settings.crossfadeMs()); });
    microdap::PlayThrough playThrough(player.queue(), &library, &albums);
    playThrough.setMode(settings.playThrough());
    playThrough.setRoots(settings.roots());
    QObject::connect(&settings, &microdap::Settings::playThroughChanged, &playThrough,
                     [&settings, &playThrough] { playThrough.setMode(settings.playThrough()); });
    QObject::connect(&settings, &microdap::Settings::rootsChanged, &playThrough,
                     [&settings, &playThrough] { playThrough.setRoots(settings.roots()); });
    QObject::connect(&player, &microdap::Player::trackStarted, &playThrough, &microdap::PlayThrough::onTrackStarted);
    microdap::State state(&player);
    state.setRestore(outputMode.get());
    state.setResume(settings.resume());
    state.installSignalHandlers();
    state.load();
    microdap::Mpris mpris(&player, QDBusConnection::sessionBus());
    microdap::PowerBridge power(&player, QDBusConnection::sessionBus());
    QObject::connect(&power, &microdap::PowerBridge::switchingChanged, eqControl.get(),
                     [&power, eq = eqControl.get()] {
                         if (power.switching())
                             eq->sync();
                     });
    if (dacEq) {
        QObject::connect(&power, &microdap::PowerBridge::switchingChanged, dacEq.get(),
                         [&power, dac = dacEq.get()] { dac->onSwitchingChanged(power.switching()); });
        dacEq->onSwitchingChanged(power.switching());
    }
    if (overlayMode && !power.isRegistered()) {
        qWarning() << "microdap overlay: another player owns net.seilent.MicroDap.Player, exiting";
        return 0;
    }
    microdap::Favorites favorites(&library);
    QObject::connect(&library, &microdap::Library::scanned, &favorites, &microdap::Favorites::reload);
    player.setFavorites(&favorites);
    if (!overlayMode) {
        microdap::Keep::clear();
        QObject::connect(&power, &microdap::PowerBridge::screenOffChanged, bluez.get(), [&power, model = bluez.get()] {
            if (!power.screenOff())
                model->want();
        });
        power.setSettings(&settings);
        power.runScreenOn();
        power.enableHolds();
        power.setState(&state);
        power.setOutputMode(outputMode.get(), &player);
        power.enableSleepWatch();
    }
    std::unique_ptr<microdap::LibraryWatcher> libraryWatcher;
    if (!overlayMode) {
        library.setSettings(&settings);
        libraryWatcher = std::make_unique<microdap::LibraryWatcher>(&library);
    }
    microdap::StatusModel status;
    status.setClockMode(settings.clock());
    status.setBatteryPercent(settings.batteryPercent());
    QObject::connect(&settings, &microdap::Settings::clockChanged, &status,
                     [&settings, &status] { status.setClockMode(settings.clock()); });
    QObject::connect(&settings, &microdap::Settings::batteryPercentChanged, &status,
                     [&settings, &status] { status.setBatteryPercent(settings.batteryPercent()); });

    microdap::AccentControl accent;
    accent.setSliderHue(settings.accentHue());
    accent.setFromArt(settings.accentFromArt());
    accent.setPhoneActive(bluez->transportActive());
    accent.setArtPath(player.current().value(QStringLiteral("artPath")).toString());
    QObject::connect(&settings, &microdap::Settings::accentHueChanged, &accent,
                     [&settings, &accent] { accent.setSliderHue(settings.accentHue()); });
    QObject::connect(&settings, &microdap::Settings::accentFromArtChanged, &accent,
                     [&settings, &accent] { accent.setFromArt(settings.accentFromArt()); });
    QObject::connect(&player, &microdap::Player::currentChanged, &accent, [&player, &accent] {
        accent.setArtPath(player.current().value(QStringLiteral("artPath")).toString());
    });
    QObject::connect(bluez.get(), &microdap::BluezModel::transportActiveChanged, &accent,
                     [&accent, model = bluez.get()] { accent.setPhoneActive(model->transportActive()); });
    QObject::connect(&library, &microdap::Library::albumsChanged, &accent, &microdap::AccentControl::clearCache);
    std::unique_ptr<microdap::RgbControl> rgbControl;
    if (!overlayMode) {
        outputControl->apply(settings.output());
        QObject::connect(&settings, &microdap::Settings::outputChanged, outputControl.get(),
                         [&settings, control = outputControl.get()] { control->apply(settings.output()); });
        rgbControl = std::make_unique<microdap::RgbControl>(settings.client());
        rgbControl->setScreenOff(power.screenOff());
        rgbControl->start(settings.rgb(), accent.hue(), accent.level());
        QObject::connect(&settings, &microdap::Settings::rgbChanged, rgbControl.get(),
                         [&settings, rgb = rgbControl.get()] { rgb->setMode(settings.rgb()); });
        QObject::connect(&accent, &microdap::AccentControl::hueChanged, rgbControl.get(),
                         [&accent, rgb = rgbControl.get()] { rgb->setColor(accent.hue(), accent.level()); });
        QObject::connect(&power, &microdap::PowerBridge::screenOffChanged, rgbControl.get(),
                         [&power, rgb = rgbControl.get()] { rgb->setScreenOff(power.screenOff()); });
    }

    auto pad = std::make_unique<microdap::DbusPad>(QDBusConnection::sessionBus(), microdap::Intercept::bus());
    std::unique_ptr<microdap::OverlayBridge> overlay;
    if (overlayMode) {
        overlay = std::make_unique<microdap::OverlayBridge>(pad.get(), &player);
        overlay->setState(&state);
        power.setOverlay(overlay.get());
    } else {
        pad->setShown(true);
    }

    auto applyActive = [&power, &status, &audioInfo, model = bluez.get(), bridge = overlay.get()] {
        const bool active = !power.screenOff() && (!bridge || bridge->mapped());
        status.setActive(active);
        audioInfo.setActive(active);
        model->setActive(active);
    };
    QObject::connect(&power, &microdap::PowerBridge::screenOffChanged, &status, applyActive);
    if (overlay) {
        QObject::connect(overlay.get(), &microdap::OverlayBridge::mappedChanged, &status, applyActive);
        applyActive();
    } else {
        outputControl->setActive(true);
        audioInfo.setActive(!power.screenOff());
    }
    QObject::connect(&power, &microdap::PowerBridge::screenOffChanged, pad.get(),
                     [&power, source = pad.get()] { source->setScreenOff(power.screenOff()); });
    std::unique_ptr<microdap::GuideKey> guide;
    std::unique_ptr<microdap::OutputWatch> outputs;
    std::unique_ptr<QObject> sensor;
    if (!overlayMode) {
        guide = std::make_unique<microdap::GuideKey>();
        guide->setScreenOff(power.screenOff());
        QObject::connect(&power, &microdap::PowerBridge::screenOffChanged, guide.get(),
                         [&power, key = guide.get()] { key->setScreenOff(power.screenOff()); });
        QObject::connect(guide.get(), &microdap::GuideKey::playPause, &player, &microdap::Player::playPause);
        QObject::connect(guide.get(), &microdap::GuideKey::next, &player, &microdap::Player::next);
        QObject::connect(guide.get(), &microdap::GuideKey::previous, &player, &microdap::Player::previous);
        QObject::connect(guide.get(), &microdap::GuideKey::switchPrompt, &power,
                         &microdap::PowerBridge::requestSwitchPrompt);
        QObject::connect(guide.get(), &microdap::GuideKey::buzz, pad->intercept(), &microdap::Intercept::rumble);
        volumeKeys = std::make_unique<microdap::VolumeKeys>(new microdap::EvdevKeys, routedVolume, &settings);
        QObject::connect(volumeKeys.get(), &microdap::VolumeKeys::levelChanged, &power, &microdap::PowerBridge::setVolume);
        sensor = std::make_unique<microdap::Rotation>(settings.client());
        outputs = std::make_unique<microdap::OutputWatch>();
        outputs->setScreenOff(power.screenOff());
        QObject::connect(&power, &microdap::PowerBridge::screenOffChanged, outputs.get(),
                         [&power, watch = outputs.get()] { watch->setScreenOff(power.screenOff()); });
    }
    std::unique_ptr<microdap::LinkServer> link;
    std::unique_ptr<microdap::LinkProfile> linkProfile;
    std::unique_ptr<microdap::FolderModel> linkFolders;
    if (!overlayMode) {
        linkFolders = std::make_unique<microdap::FolderModel>(&library);
        linkFolders->setRoots(settings.roots());
        QObject::connect(&settings, &microdap::Settings::rootsChanged, linkFolders.get(),
                         [&settings, folders = linkFolders.get()] { folders->setRoots(settings.roots()); });
        link = std::make_unique<microdap::LinkServer>(&player, &settings);
        link->setBluez(bluez.get());
        link->setStatus(&status);
        link->setVolume(routedVolume);
        link->setSources({&library, &artists, &albums, &songs, &playlists, linkFolders.get(), &favorites});
        link->setLyrics(&lyrics);
        linkProfile = std::make_unique<microdap::LinkProfile>(QDBusConnection::systemBus());
        QObject::connect(linkProfile.get(), &microdap::LinkProfile::connection, link.get(), &microdap::LinkServer::open);
        QObject::connect(linkProfile.get(), &microdap::LinkProfile::ended, link.get(), &microdap::LinkServer::end);
        QObject::connect(&settings, &microdap::Settings::companionChanged, linkProfile.get(),
                         [&settings, profile = linkProfile.get()] { profile->setEnabled(settings.companion()); });
        QObject::connect(&app, &QCoreApplication::aboutToQuit, link.get(),
                         [server = link.get()] { server->bye(fd::proto::val::BYE_REASON::shutdown); });
        linkProfile->setEnabled(settings.companion());
    }
    if (outputMode) {
        microdap::OutputMode *mode = outputMode.get();
        microdap::OutputControl *control = outputControl.get();
        microdap::DeviceReservation *reserve = reservation.get();
        QObject::connect(&settings, &microdap::Settings::outputLevelChanged, mode,
                         [&settings, mode] { mode->setLevel(settings.outputLevel()); });
        QObject::connect(&settings, &microdap::Settings::sampleRateChanged, mode,
                         [&settings, mode] { mode->setSampleRate(settings.sampleRate()); });
        auto activeOutput = [control, mode] { mode->onActiveOutputChanged(control->activeOutput(), control->usbSink()); };
        QObject::connect(control, &microdap::OutputControl::activeOutputChanged, mode, activeOutput);
        QObject::connect(control, &microdap::OutputControl::usbSinkChanged, mode, activeOutput);
        QObject::connect(control, &microdap::OutputControl::applied, mode, &microdap::OutputMode::onApplied);
        QObject::connect(control, &microdap::OutputControl::cardGone, mode, &microdap::OutputMode::onCardGone);
        QObject::connect(control, &microdap::OutputControl::routeLost, &player, &microdap::Player::outputLost);
        QObject::connect(pulseVolume, &microdap::PulseSinkVolume::cardRemoved, control, &microdap::OutputControl::refresh);
        QObject::connect(control, &microdap::OutputControl::sinksRead, mode, &microdap::OutputMode::onSinksRead);
        QObject::connect(&player, &microdap::Player::statusChanged, mode, &microdap::OutputMode::onStatusChanged);
        QObject::connect(&player, &microdap::Player::endOfStream, mode, &microdap::OutputMode::onEndOfStream);
        QObject::connect(&player, &microdap::Player::formatChanged, mode, &microdap::OutputMode::onFormatChanged);
        QObject::connect(&audioInfo, &microdap::AudioInfo::hwRateChanged, mode,
                         [&audioInfo, mode] { mode->onHwRateChanged(audioInfo.hwRate()); });
        QObject::connect(alsaVolume.get(), &microdap::AlsaVolume::disconnected, mode,
                         &microdap::OutputMode::onMixerDisconnected);
        QObject::connect(alsaVolume.get(), &microdap::AlsaVolume::levelChanged, mode,
                         &microdap::OutputMode::onVolumePressed);
        QObject::connect(pulseVolume, &microdap::PulseSinkVolume::sinkAppeared, mode,
                         &microdap::OutputMode::onSinkAppeared);
        QObject::connect(pulseVolume, &microdap::PulseSinkVolume::sinkChanged, mode, &microdap::OutputMode::onSinkChanged);
        QObject::connect(pulseVolume, &microdap::PulseSinkVolume::cardRemoved, mode, &microdap::OutputMode::onCardRemoved);
        QObject::connect(bluez.get(), &microdap::BluezModel::connectedChanged, mode,
                         &microdap::OutputMode::onBluezConnectedChanged);
        QObject::connect(bluez.get(), &microdap::BluezModel::readyChanged, mode, &microdap::OutputMode::onBluezReadyChanged);
        QObject::connect(bluez.get(), &microdap::BluezModel::transportStateChanged, mode,
                         &microdap::OutputMode::onTransportStateChanged);
        QObject::connect(wireplumber.get(), &microdap::WirePlumberRestart::restartFinished, mode,
                         &microdap::OutputMode::onRestartFinished);
        QObject::connect(reserve, &microdap::DeviceReservation::acquired, mode, &microdap::OutputMode::onAcquired);
        QObject::connect(reserve, &microdap::DeviceReservation::refused, mode, &microdap::OutputMode::onRefused);
        QObject::connect(reserve, &microdap::DeviceReservation::failed, mode, &microdap::OutputMode::onFailed);
        QObject::connect(reserve, &microdap::DeviceReservation::releaseRequested, mode,
                         &microdap::OutputMode::onReleaseRequested);
        QObject::connect(reserve, &microdap::DeviceReservation::releaseTimedOut, mode,
                         &microdap::OutputMode::onReleaseTimedOut);
        QObject::connect(reserve, &microdap::DeviceReservation::nameLost, mode, &microdap::OutputMode::onNameLost);
        QObject::connect(reserve, &microdap::DeviceReservation::connectionLost, mode,
                         &microdap::OutputMode::onConnectionLost);
        QObject::connect(reserve, &microdap::DeviceReservation::released, mode, &microdap::OutputMode::onReleased);
        QObject::connect(&app, &QCoreApplication::aboutToQuit, mode, &microdap::OutputMode::aboutToQuit);
        mode->onHwRateChanged(audioInfo.hwRate());
        mode->attach();
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
    engine.rootContext()->setContextProperty(QStringLiteral("FoldersHub"), &foldersHub);
    engine.rootContext()->setContextProperty(QStringLiteral("AlbumsGrid"), &albumsGrid);
    engine.rootContext()->setContextProperty(QStringLiteral("Playlists"), &playlists);
    engine.rootContext()->setContextProperty(QStringLiteral("Favorites"), &favorites);
    engine.rootContext()->setContextProperty(QStringLiteral("Lyrics"), &lyrics);
    engine.rootContext()->setContextProperty(QStringLiteral("AudioInfo"), &audioInfo);
    engine.rootContext()->setContextProperty(QStringLiteral("OutputControl"),
                                             static_cast<QObject *>(outputControl.get()));
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
    if (outputMode)
        engine.rootContext()->setContextProperty(QStringLiteral("OutputMode"), outputMode.get());
    if (dacEq)
        engine.rootContext()->setContextProperty(QStringLiteral("DacEq"), dacEq.get());
    if (link)
        engine.rootContext()->setContextProperty(QStringLiteral("Link"), link.get());
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("MicroDap", "Main");
    if (!engine.rootObjects().isEmpty()) {
        auto *root = qobject_cast<QWindow *>(engine.rootObjects().constFirst());
        if (overlayMode) {
            if (auto *quick = qobject_cast<QQuickWindow *>(root)) {
                const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
                                         + QStringLiteral("/microdap");
                if (QDir().mkpath(cacheDir)) {
                    const QString cacheFile = cacheDir + QStringLiteral("/overlay-pipeline.bin");
                    QQuickGraphicsConfiguration config = quick->graphicsConfiguration();
                    config.setPipelineCacheLoadFile(cacheFile);
                    config.setPipelineCacheSaveFile(cacheFile);
                    quick->setGraphicsConfiguration(config);
                }
            }
            overlay->setWindow(root, microdap::setOverlayAtoms);
            auto *toast = qobject_cast<QWindow *>(qvariant_cast<QObject *>(root->property("toastWindow")));
            if (toast)
                overlay->setToastWindow(toast, microdap::setToastAtoms);
        } else {
            microdap::claimGamescopeFocus(root);
        }
    }

    const QString grabOut = qEnvironmentVariable("MICRODAP_GRAB_OUT");
    auto *window = engine.rootObjects().isEmpty()
                       ? nullptr
                       : qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!overlayMode && window)
        QObject::connect(window, &QQuickWindow::frameSwapped, &library,
                         [&library] { library.rescanRoots(); }, Qt::SingleShotConnection);
    if (!grabOut.isEmpty() && window) {
        const int delay = qEnvironmentVariableIntValue("MICRODAP_GRAB_AFTER_MS");
        QTimer::singleShot(delay > 0 ? delay : 3000, window, [window, grabOut] {
            const QImage image = window->grabWindow();
            QTextStream(stderr) << "microdap-player: grab " << image.width() << 'x' << image.height()
                                << (image.save(grabOut, "PNG") ? " saved " : " failed ") << grabOut << '\n';
        });
    }
    const int code = app.exec();
    player.setPolicy(nullptr);
    return code;
}
