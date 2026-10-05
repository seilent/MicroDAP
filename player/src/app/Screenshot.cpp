#include "Screenshot.h"

#include "FakeBluezModel.h"
#include "Library.h"
#include "Models.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
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

namespace casdap {

namespace {

constexpr int frameTimeoutMs = 5000;
constexpr int scanTimeoutMs = 60000;
constexpr int historyRows = 4;

void fail(const QString &message)
{
    QTextStream(stderr) << "casdap-player: " << message << '\n';
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
    if (!states().contains(state)) {
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
    std::unique_ptr<ArtistsModel> artists;
    std::unique_ptr<AlbumsModel> albums;
    std::unique_ptr<SongsModel> songs;
    std::unique_ptr<GenresModel> genres;
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
        recent = std::make_unique<HistoryModel>(library.get());
        fresh = std::make_unique<HistoryModel>(library.get());
        fresh->setMode(HistoryModel::New);
        artists = std::make_unique<ArtistsModel>(library.get());
        albums = std::make_unique<AlbumsModel>(library.get());
        songs = std::make_unique<SongsModel>(library.get());
        genres = std::make_unique<GenresModel>(library.get());
    }

    QQmlApplicationEngine engine;
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
    engine.setInitialProperties({
        {QStringLiteral("screenshot"), true},
        {QStringLiteral("shotScreen"), options.screen},
        {QStringLiteral("shotAngle"), options.angle},
        {QStringLiteral("shotPivot"), pivot},
        {QStringLiteral("shotWidth"), width},
        {QStringLiteral("shotHeight"), height},
    });
    engine.loadFromModule("Casdap", "Main");

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (!window) {
        fail(QStringLiteral("Casdap.Main did not produce a window"));
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
