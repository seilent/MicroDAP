#include "Screenshot.h"

#include "AccentControl.h"
#include "Eq.h"
#include "FakeBluezModel.h"
#include "HubModels.h"
#include "Library.h"
#include "Models.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QFont>
#include <QFontInfo>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace microdap {

namespace {

constexpr int frameTimeoutMs = 5000;
constexpr int scanTimeoutMs = 60000;
constexpr int historyRows = 4;
constexpr const char *sampleEq =
    "Preamp: -6.5 dB\n"
    "Filter 1: ON LSC Fc 105 Hz Gain 5.5 dB Q 0.70\n"
    "Filter 2: ON PK Fc 158 Hz Gain -2.1 dB Q 0.64\n"
    "Filter 3: ON PK Fc 2482 Hz Gain 3.4 dB Q 2.11\n"
    "Filter 4: ON PK Fc 5345 Hz Gain -4.2 dB Q 3.07\n"
    "Filter 5: ON HSC Fc 10000 Hz Gain 1.6 dB Q 0.70\n";

void fail(const QString &message)
{
    QTextStream(stderr) << "microdap-player: " << message << '\n';
}

bool parseSize(const QString &size, int &width, int &height)
{
    static const QRegularExpression pattern(QStringLiteral("^(\\d+)x(\\d+)$"));
    const QRegularExpressionMatch match = pattern.match(size);
    if (!match.hasMatch())
        return false;
    width = match.captured(1).toInt();
    height = match.captured(2).toInt();
    return width > 0 && height > 0;
}

void grab(QQuickWindow *window, const QString &out)
{
    if (QFontInfo(QFont(QStringLiteral("Selawik"))).family() != QLatin1StringView("Selawik"))
        fail(QStringLiteral("warning: Selawik not loaded, set MICRODAP_FONT_DIR"));
    const QImage image = window->grabWindow();
    if (image.isNull()) {
        fail(QStringLiteral("grabWindow returned no image"));
        QCoreApplication::exit(1);
        return;
    }
    QDir().mkpath(QFileInfo(out).absolutePath());
    if (!image.save(out, "PNG")) {
        fail(QStringLiteral("cannot write %1").arg(out));
        QCoreApplication::exit(1);
        return;
    }
    QCoreApplication::exit(0);
}

const QStringList &screens()
{
    static const QStringList names{
        QStringLiteral("blank"),
        QStringLiteral("specimen"),
        QStringLiteral("motion"),
        QStringLiteral("components"),
        QStringLiteral("overlays"),
        QStringLiteral("volume"),
        QStringLiteral("home"),
        QStringLiteral("quickplay"),
        QStringLiteral("bluetooth"),
        QStringLiteral("prompt"),
        QStringLiteral("hub"),
        QStringLiteral("nowplaying"),
        QStringLiteral("queue"),
        QStringLiteral("artist"),
        QStringLiteral("album"),
        QStringLiteral("settings"),
        QStringLiteral("jump"),
        QStringLiteral("playlists"),
        QStringLiteral("history"),
        QStringLiteral("eq"),
        QStringLiteral("keyboard"),
        QStringLiteral("power"),
        QStringLiteral("saver"),
        QStringLiteral("saver-dim"),
    };
    return names;
}

const QStringList &pivots()
{
    static const QStringList names{
        QStringLiteral("artists"),
        QStringLiteral("albums"),
        QStringLiteral("songs"),
        QStringLiteral("genres"),
        QStringLiteral("playlists"),
        QStringLiteral("folders"),
    };
    return names;
}

const QStringList &states()
{
    static const QStringList names{
        QStringLiteral("browse"),
        QStringLiteral("playing"),
        QStringLiteral("phone"),
        QStringLiteral("phone-active"),
        QStringLiteral("pairing"),
        QStringLiteral("saver"),
        QStringLiteral("volume"),
        QStringLiteral("power"),
        QStringLiteral("jump"),
        QStringLiteral("orientation"),
        QStringLiteral("orientation-edit"),
        QStringLiteral("orientation-edit-end"),
        QStringLiteral("orientation-external"),
        QStringLiteral("auto-rotate"),
        QStringLiteral("auto-rotate-unsupported"),
        QStringLiteral("auto-rotate-below"),
        QStringLiteral("auto-rotate-off"),
        QStringLiteral("auto-rotate-edit"),
        QStringLiteral("pressed"),
        QStringLiteral("outside-library"),
        QStringLiteral("touch-scrolled"),
        QStringLiteral("folder-open"),
        QStringLiteral("touch"),
        QStringLiteral("touch-edit"),
        QStringLiteral("bit-perfect"),
        QStringLiteral("bit-perfect-edit"),
        QStringLiteral("bit-perfect-phone"),
        QStringLiteral("bit-perfect-unavailable"),
        QStringLiteral("bit-perfect-usb-only"),
        QStringLiteral("sample-rate-edit"),
        QStringLiteral("sample-rate-unsupported"),
        QStringLiteral("sample-rate-locked"),
        QStringLiteral("accent-edit"),
        QStringLiteral("saver-sheet"),
        QStringLiteral("pivot-library"),
        QStringLiteral("pivot-display"),
        QStringLiteral("pivot-power"),
        QStringLiteral("pivot-system"),
        QStringLiteral("eq-types"),
        QStringLiteral("eq-types-end"),
        QStringLiteral("eq-open-hs"),
        QStringLiteral("eq-type-edit"),
        QStringLiteral("eq-gain-unused"),
        QStringLiteral("eq-open-hp"),
        QStringLiteral("eq-open-ap"),
        QStringLiteral("eq-dac"),
        QStringLiteral("eq-dac-custom"),
        QStringLiteral("eq-dac-failed"),
        QStringLiteral("eq-dac-unsupported"),
        QStringLiteral("eq-dac-range"),
        QStringLiteral("eq-dac-preamp"),
        QStringLiteral("eq-dac-long"),
        QStringLiteral("boot"),
        QStringLiteral("boot-edit"),
        QStringLiteral("boot-missing"),
        QStringLiteral("about"),
        QStringLiteral("about-available"),
        QStringLiteral("about-missing"),
        QStringLiteral("power-none"),
        QStringLiteral("power-long"),
        QStringLiteral("tracknum"),
        QStringLiteral("seek"),
        QStringLiteral("quickplay-off"),
        QStringLiteral("clock-24h"),
        QStringLiteral("clock-12h"),
        QStringLiteral("percent"),
        QStringLiteral("percent-low"),
        QStringLiteral("art-collapse"),
        QStringLiteral("art-dock"),
        QStringLiteral("art-docked"),
        QStringLiteral("play-through"),
        QStringLiteral("play-through-sheet"),
        QStringLiteral("resume-sheet"),
        QStringLiteral("playlist-pick"),
        QStringLiteral("playlist-sheet"),
        QStringLiteral("playlist-delete"),
        QStringLiteral("track-info"),
        QStringLiteral("track-info-short"),
        QStringLiteral("view"),
        QStringLiteral("view-sheet"),
        QStringLiteral("view-long"),
        QStringLiteral("view-empty"),
        QStringLiteral("max-volume"),
        QStringLiteral("max-volume-sheet"),
        QStringLiteral("power-on-sheet"),
        QStringLiteral("balance"),
        QStringLiteral("balance-edit"),
        QStringLiteral("crossfeed-sheet"),
        QStringLiteral("crossfeed-speaker"),
        QStringLiteral("crossfeed-bit-perfect"),
        QStringLiteral("replaygain-sheet"),
        QStringLiteral("replaygain-bit-perfect"),
        QStringLiteral("crossfade-sheet"),
        QStringLiteral("crossfade-bit-perfect"),
        QStringLiteral("kb-empty"),
        QStringLiteral("kb-long"),
        QStringLiteral("kb-petal"),
        QStringLiteral("kb-petal-corner"),
        QStringLiteral("kb-shift"),
        QStringLiteral("kb-symbols"),
        QStringLiteral("kb-discard"),
        QStringLiteral("kb-number-freq"),
        QStringLiteral("kb-number-q"),
        QStringLiteral("kb-number-gain"),
        QStringLiteral("kb-refused"),
        QStringLiteral("lyrics-short"),
        QStringLiteral("lyrics-long"),
        QStringLiteral("lyrics-cjk"),
        QStringLiteral("lyrics-before"),
        QStringLiteral("lyrics-none"),
        QStringLiteral("lyrics-plain"),
        QStringLiteral("lyrics-pending"),
        QStringLiteral("mark"),
        QStringLiteral("mark-longnext"),
        QStringLiteral("mark-nonext"),
        QStringLiteral("mark-pending"),
    };
    return names;
}

const QStringList &orientations()
{
    static const QStringList names{
        QStringLiteral("portrait"),
        QStringLiteral("portrait-flipped"),
        QStringLiteral("landscape"),
        QStringLiteral("landscape-flipped"),
    };
    return names;
}

QString bluezForState(const QString &state)
{
    if (state == QLatin1StringView("phone"))
        return QStringLiteral("connected");
    if (state == QLatin1StringView("phone-active"))
        return QStringLiteral("active");
    if (state == QLatin1StringView("pairing"))
        return QStringLiteral("pairing");
    return QStringLiteral("off");
}

bool scanLibrary(Library &library, const QString &dir)
{
    QEventLoop loop;
    bool ok = false;
    QObject::connect(&library, &Library::scanned, &loop, [&loop, &ok](int, int, int, bool result) {
        ok = result;
        loop.quit();
    });
    QTimer::singleShot(scanTimeoutMs, &loop, [&loop] { loop.quit(); });
    library.rescan({QDir(dir).absolutePath()});
    loop.exec();
    return ok;
}

void seedHistory(Library &library)
{
    SongsModel songs(&library);
    songs.ensureLoaded();
    const int rows = std::min(historyRows, songs.rowCount());
    for (int row = 0; row < rows; ++row)
        library.addHistory(songs.get(row).value(QStringLiteral("id")).toLongLong());
}

}

int screenshot(const ScreenshotOptions &options)
{
    if (!screens().contains(options.screen)) {
        fail(QStringLiteral("unknown screen %1, expected one of %2")
                 .arg(options.screen, screens().join(QLatin1Char(' '))));
        return 2;
    }
    const QString state = options.state.isEmpty() ? QStringLiteral("browse") : options.state;
    static const QRegularExpression slide(QStringLiteral("^lyricslide-(short|long)-[0-9]{1,3}$"));
    if (!states().contains(state) && !slide.match(state).hasMatch()) {
        fail(QStringLiteral("unknown state %1, expected one of %2")
                 .arg(state, states().join(QLatin1Char(' '))));
        return 2;
    }
    const QString bluezPreset = options.bluez.isEmpty() ? bluezForState(state) : options.bluez;
    if (!FakeBluezModel::presets().contains(bluezPreset)) {
        fail(QStringLiteral("unknown bluez preset %1, expected one of %2")
                 .arg(bluezPreset, FakeBluezModel::presets().join(QLatin1Char(' '))));
        return 2;
    }
    const QString pivot = options.pivot.isEmpty() ? QStringLiteral("albums") : options.pivot;
    if (!pivots().contains(pivot)) {
        fail(QStringLiteral("unknown pivot %1, expected one of %2")
                 .arg(pivot, pivots().join(QLatin1Char(' '))));
        return 2;
    }

    const QString orientation = options.orientation.isEmpty() ? QStringLiteral("portrait") : options.orientation;
    if (!orientations().contains(orientation)) {
        fail(QStringLiteral("unknown orientation %1, expected one of %2")
                 .arg(orientation, orientations().join(QLatin1Char(' '))));
        return 2;
    }

    int width = 0;
    int height = 0;
    if (!parseSize(options.size, width, height)) {
        fail(QStringLiteral("bad size %1, expected WxH").arg(options.size));
        return 1;
    }
    if (options.out.isEmpty()) {
        fail(QStringLiteral("--screenshot needs --out"));
        return 1;
    }

    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);

    FakeBluezModel bluez;
    bluez.applyPreset(bluezPreset);

    std::unique_ptr<QTemporaryDir> tempDir;
    std::unique_ptr<Library> library;
    std::unique_ptr<HistoryModel> recent;
    std::unique_ptr<HistoryModel> fresh;
    std::unique_ptr<HistoryModel> quickRecent;
    std::unique_ptr<HistoryModel> quickNew;
    std::unique_ptr<ArtistsModel> artists;
    std::unique_ptr<AlbumsModel> albums;
    std::unique_ptr<SongsModel> songs;
    std::unique_ptr<GenresModel> genres;
    std::unique_ptr<PinsModel> pins;
    SectionedModel artistsHub;
    artistsHub.setKind(QStringLiteral("artist"));
    SectionedModel songsHub;
    songsHub.setKind(QStringLiteral("song"));
    SectionedModel genresHub;
    genresHub.setKind(QStringLiteral("genre"));
    AlbumGridModel albumsGrid;
    std::unique_ptr<FolderModel> foldersHub;
    if (!options.library.isEmpty()) {
        tempDir = std::make_unique<QTemporaryDir>();
        if (!tempDir->isValid()) {
            fail(QStringLiteral("cannot create a temporary library directory"));
            return 1;
        }
        library = std::make_unique<Library>(tempDir->filePath(QStringLiteral("library.db")),
                                            tempDir->filePath(QStringLiteral("art")));
        if (!library->isOpen()) {
            fail(QStringLiteral("cannot open the screenshot library"));
            return 1;
        }
        if (!scanLibrary(*library, options.library)) {
            fail(QStringLiteral("library scan of %1 failed").arg(options.library));
            return 1;
        }
        seedHistory(*library);
        recent = std::make_unique<HistoryModel>(library.get(), HistoryModel::Recent, 100);
        fresh = std::make_unique<HistoryModel>(library.get(), HistoryModel::New, 100);
        quickRecent = std::make_unique<HistoryModel>(library.get(), HistoryModel::Recent, 8);
        quickNew = std::make_unique<HistoryModel>(library.get(), HistoryModel::New, 8);
        artists = std::make_unique<ArtistsModel>(library.get());
        albums = std::make_unique<AlbumsModel>(library.get());
        songs = std::make_unique<SongsModel>(library.get());
        genres = std::make_unique<GenresModel>(library.get());
        pins = std::make_unique<PinsModel>(library.get());
        artistsHub.setSource(artists.get());
        songsHub.setSource(songs.get());
        genresHub.setSource(genres.get());
        albumsGrid.setSource(albums.get());
        foldersHub = std::make_unique<FolderModel>(library.get());
        foldersHub->setRoots({QDir(options.library).absolutePath()});
    }

    EqControl eq(nullptr);
    if (const std::optional<EqProfile> sample = EqParser::parse(QString::fromUtf8(sampleEq)))
        eq.setCustom(QStringLiteral("headphones"), *sample);
    eq.setActiveOutput(QStringLiteral("headphones"));
    AccentControl accent;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Accent"), &accent);
    engine.rootContext()->setContextProperty(QStringLiteral("Eq"), &eq);
    engine.rootContext()->setContextProperty(QStringLiteral("BluezModel"), &bluez);
    engine.rootContext()->setContextProperty(QStringLiteral("HistoryRecent"),
                                             static_cast<QObject *>(recent.get()));
    engine.rootContext()->setContextProperty(QStringLiteral("HistoryNew"),
                                             static_cast<QObject *>(fresh.get()));
    engine.rootContext()->setContextProperty(QStringLiteral("Artists"),
                                             static_cast<QObject *>(artists.get()));
    engine.rootContext()->setContextProperty(QStringLiteral("Albums"),
                                             static_cast<QObject *>(albums.get()));
    engine.rootContext()->setContextProperty(QStringLiteral("Songs"),
                                             static_cast<QObject *>(songs.get()));
    engine.rootContext()->setContextProperty(QStringLiteral("Genres"),
                                             static_cast<QObject *>(genres.get()));
    engine.rootContext()->setContextProperty(QStringLiteral("Pins"),
                                             static_cast<QObject *>(pins.get()));
    engine.rootContext()->setContextProperty(QStringLiteral("QuickplayRecent"),
                                             static_cast<QObject *>(quickRecent.get()));
    engine.rootContext()->setContextProperty(QStringLiteral("QuickplayNew"),
                                             static_cast<QObject *>(quickNew.get()));
    engine.rootContext()->setContextProperty(QStringLiteral("ArtistsHub"), &artistsHub);
    engine.rootContext()->setContextProperty(QStringLiteral("SongsHub"), &songsHub);
    engine.rootContext()->setContextProperty(QStringLiteral("GenresHub"), &genresHub);
    engine.rootContext()->setContextProperty(QStringLiteral("FoldersHub"),
                                             static_cast<QObject *>(foldersHub.get()));
    engine.rootContext()->setContextProperty(QStringLiteral("AlbumsGrid"), &albumsGrid);
    engine.setInitialProperties({
        {QStringLiteral("screenshot"), true},
        {QStringLiteral("shotScreen"), options.screen},
        {QStringLiteral("shotAngle"), options.angle},
        {QStringLiteral("shotPivot"), pivot},
        {QStringLiteral("shotOverlay"), options.overlay},
        {QStringLiteral("shotWidth"), width},
        {QStringLiteral("shotHeight"), height},
        {QStringLiteral("shotOrientation"), orientation},
        {QStringLiteral("shotState"), state},
    });
    engine.loadFromModule("MicroDap", "Main");

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (!window) {
        fail(QStringLiteral("MicroDap.Main did not produce a window"));
        return 1;
    }

    const QString out = options.out;
    QObject::connect(window, &QQuickWindow::frameSwapped, window, [window, out] {
        QTimer::singleShot(0, window, [window, out] { grab(window, out); });
    }, Qt::SingleShotConnection);

    QTimer::singleShot(frameTimeoutMs, window, [] {
        fail(QStringLiteral("no frame within %1 ms").arg(frameTimeoutMs));
        QCoreApplication::exit(1);
    });

    return QCoreApplication::exec();
}

}
