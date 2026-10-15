#include "Library.h"
#include "Models.h"
#include "Scanner.h"

#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <functional>
#include <memory>

using namespace microdap;

namespace {

constexpr int artistTotal = 1200;
constexpr int albumTotal = 1200;
constexpr int tracksPerAlbum = 7;
constexpr int trackTotal = albumTotal * tracksPerAlbum;
constexpr int playlistTotal = 3;
constexpr int playlistItems = 2644;
constexpr int historyRows = 2000;
constexpr int runs = 5;

QString titleOf(int n)
{
    if (n % 20 == 0)
        return QStringLiteral("%1 title").arg(n);
    return QString(QChar(char16_t(u'a' + n % 26))) + QStringLiteral("title %1").arg(n);
}

QString artistName(int n)
{
    return QStringLiteral("artist ") + titleOf(n);
}

QString trackPath(int id)
{
    return QStringLiteral("/scale/music/%1/%2.flac").arg((id - 1) / tracksPerAlbum).arg(id);
}

QString playlistSource(int p)
{
    return QStringLiteral("/scale/music/playlist %1.m3u8").arg(p);
}

double median(QList<double> values)
{
    std::sort(values.begin(), values.end());
    return values.at(values.size() / 2);
}

double measure(const std::function<void(int)> &body)
{
    QList<double> times;
    for (int run = 0; run < runs; ++run) {
        QElapsedTimer timer;
        timer.start();
        body(run);
        times.append(static_cast<double>(timer.nsecsElapsed()) / 1e6);
    }
    return median(times);
}

void report(const char *name, double ms, double budget)
{
    qInfo().noquote() << QStringLiteral("microdap perf: %1 %2").arg(QLatin1StringView(name)).arg(ms, 0, 'f', 3);
    QVERIFY2(ms <= budget, qPrintable(QStringLiteral("%1 took %2 ms, budget %3 ms")
                                          .arg(QLatin1StringView(name))
                                          .arg(ms, 0, 'f', 3)
                                          .arg(budget)));
}

}

class ScaleTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void modelsLoad();
    void snapshot();
    void emptyScan();
    void scanAddedTrack();
    void historyPlayed();
    void entriesAll();
    void batchedEntries();

private:
    bool generate();

    QTemporaryDir m_dir;
    std::unique_ptr<Library> m_library;
    QList<QStringList> m_playlistPaths;
};

bool ScaleTest::generate()
{
    const QString cover = m_dir.path() + QStringLiteral("/cover.jpg");
    QImage image(64, 64, QImage::Format_RGB32);
    image.fill(Qt::darkGray);
    if (!image.save(cover, "JPG")) {
        QFile file(cover);
        if (!file.open(QIODevice::WriteOnly) || file.write("jpg") != 3)
            return false;
    }

    QSqlDatabase db = m_library->database();
    if (!db.transaction())
        return false;

    QSqlQuery artist(db);
    artist.prepare(QStringLiteral("INSERT INTO artists(id, name) VALUES(?, ?)"));
    for (int i = 1; i <= artistTotal; ++i) {
        artist.bindValue(0, i);
        artist.bindValue(1, artistName(i));
        if (!artist.exec())
            return false;
    }

    QSqlQuery album(db);
    album.prepare(QStringLiteral(
        "INSERT INTO albums(id, title, artist, year, art_path, added) VALUES(?, ?, ?, ?, ?, ?)"));
    for (int i = 1; i <= albumTotal; ++i) {
        album.bindValue(0, i);
        album.bindValue(1, QStringLiteral("album ") + titleOf(i));
        album.bindValue(2, artistName(i));
        album.bindValue(3, 2000 + i % 20);
        album.bindValue(4, i % 2 == 0 ? QVariant(cover) : QVariant(QMetaType::fromType<QString>()));
        album.bindValue(5, i);
        if (!album.exec())
            return false;
    }

    QSqlQuery track(db);
    track.prepare(QStringLiteral(
        "INSERT INTO tracks(id, path, mtime, size, title, artist, album_artist, album_id, track_no, "
        "disc_no, year, genre, duration_ms, added, sample_rate, bit_depth) "
        "VALUES(?, ?, 1, 1, ?, ?, ?, ?, ?, 1, ?, '', 200000, ?, 44100, 16)"));
    QSqlQuery link(db);
    link.prepare(QStringLiteral("INSERT INTO track_artists(track_id, artist_id, pos) VALUES(?, ?, ?)"));
    for (int id = 1; id <= trackTotal; ++id) {
        const int albumId = (id - 1) / tracksPerAlbum + 1;
        const int second = albumId % artistTotal + 1;
        const bool two = id % 10 == 0;
        const QString names = two ? artistName(albumId) + QStringLiteral(", ") + artistName(second)
                                  : artistName(albumId);
        track.bindValue(0, id);
        track.bindValue(1, trackPath(id));
        track.bindValue(2, titleOf(id));
        track.bindValue(3, names);
        track.bindValue(4, artistName(albumId));
        track.bindValue(5, albumId);
        track.bindValue(6, (id - 1) % tracksPerAlbum + 1);
        track.bindValue(7, 2000 + albumId % 20);
        track.bindValue(8, id);
        if (!track.exec())
            return false;
        link.bindValue(0, id);
        link.bindValue(1, albumId);
        link.bindValue(2, 0);
        if (!link.exec())
            return false;
        if (two) {
            link.bindValue(0, id);
            link.bindValue(1, second);
            link.bindValue(2, 1);
            if (!link.exec())
                return false;
        }
    }

    QSqlQuery playlist(db);
    playlist.prepare(QStringLiteral(
        "INSERT INTO playlists(id, name, source, skipped, mtime, size) VALUES(?, ?, ?, 0, 1, 1)"));
    QSqlQuery item(db);
    item.prepare(QStringLiteral("INSERT INTO playlist_items(playlist_id, pos, track_id) VALUES(?, ?, ?)"));
    for (int p = 1; p <= playlistTotal; ++p) {
        playlist.bindValue(0, p);
        playlist.bindValue(1, QStringLiteral("playlist %1").arg(p));
        playlist.bindValue(2, playlistSource(p));
        if (!playlist.exec())
            return false;
        QStringList paths;
        for (int pos = 0; pos < playlistItems; ++pos) {
            const int id = (p * playlistItems + pos * 13) % trackTotal + 1;
            item.bindValue(0, p);
            item.bindValue(1, pos);
            item.bindValue(2, id);
            if (!item.exec())
                return false;
            paths.append(trackPath(id));
        }
        m_playlistPaths.append(paths);
    }

    QSqlQuery history(db);
    history.prepare(QStringLiteral("INSERT INTO history(track_id, played_at) VALUES(?, ?)"));
    for (int i = 0; i < historyRows; ++i) {
        history.bindValue(0, (i * 37) % trackTotal + 1);
        history.bindValue(1, i);
        if (!history.exec())
            return false;
    }
    return db.commit();
}

void ScaleTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_library = std::make_unique<Library>(m_dir.path() + QStringLiteral("/library.db"),
                                          m_dir.path() + QStringLiteral("/art"));
    QVERIFY(m_library->isOpen());
    QVERIFY(generate());
    QCOMPARE(m_library->songCount(), trackTotal);
    QCOMPARE(m_library->playlistCount(), playlistTotal);
}

void ScaleTest::modelsLoad()
{
    int rows = 0;
    const double ms = measure([this, &rows](int) {
        ArtistsModel artists(m_library.get());
        AlbumsModel albums(m_library.get());
        SongsModel songs(m_library.get());
        artists.ensureLoaded();
        albums.ensureLoaded();
        songs.ensureLoaded();
        rows = artists.rowCount() + albums.rowCount() + songs.rowCount();
    });
    QCOMPARE(rows, artistTotal + albumTotal + trackTotal);
    report("artists albums songs load", ms, 150);
}

void ScaleTest::snapshot()
{
    qsizetype size = 0;
    const double ms = measure([this, &size](int) { size = m_library->snapshot().size(); });
    QCOMPARE(size, trackTotal);
    report("snapshot", ms, 20);
}

void ScaleTest::emptyScan()
{
    QSignalSpy tracks(m_library.get(), &Library::tracksChanged);
    QSignalSpy albums(m_library.get(), &Library::albumsChanged);
    QSignalSpy playlists(m_library.get(), &Library::playlistsChanged);
    QSignalSpy counts(m_library.get(), &Library::countsChanged);
    QSignalSpy scanned(m_library.get(), &Library::scanned);
    ScanResult result;
    result.complete = true;
    const double ms = measure([this, &result](int) { m_library->finishScan(result); });
    QCOMPARE(tracks.count(), 0);
    QCOMPARE(albums.count(), 0);
    QCOMPARE(playlists.count(), 0);
    QCOMPARE(counts.count(), 0);
    QCOMPARE(scanned.count(), runs);
    report("finishScan empty", ms, 1);
}

void ScaleTest::scanAddedTrack()
{
    QSignalSpy tracks(m_library.get(), &Library::tracksChanged);
    QSignalSpy playlists(m_library.get(), &Library::playlistsChanged);
    const QVariantList before = m_library->playlistTracks(1);
    const double ms = measure([this](int run) {
        ScanResult result;
        result.complete = true;
        TrackInfo track;
        track.path = QStringLiteral("/scale/new/%1.flac").arg(run);
        track.mtime = 1;
        track.size = 1;
        track.title = QStringLiteral("new %1").arg(run);
        track.artists = {QStringLiteral("new artist")};
        track.artist = track.artists.first();
        track.albumArtist = track.artist;
        track.album = QStringLiteral("new album");
        track.trackNo = run + 1;
        track.durationMs = 200000;
        result.added.append(track);
        for (int p = 1; p <= playlistTotal; ++p) {
            PlaylistFile playlist;
            playlist.source = playlistSource(p);
            playlist.name = QStringLiteral("playlist %1").arg(p);
            playlist.entries = m_playlistPaths.at(p - 1);
            playlist.mtime = 1;
            playlist.size = 1;
            result.playlists.append(playlist);
        }
        m_library->finishScan(result);
    });
    QCOMPARE(tracks.count(), runs);
    QCOMPARE(playlists.count(), 0);
    QCOMPARE(m_library->playlistTracks(1), before);
    QCOMPARE(m_library->songCount(), trackTotal + runs);
    report("finishScan 1 added 3 playlists", ms, 150);
}

void ScaleTest::historyPlayed()
{
    HistoryModel quickRecent(m_library.get(), HistoryModel::Recent, 8);
    HistoryModel quickNew(m_library.get(), HistoryModel::New, 8);
    HistoryModel pageRecent(m_library.get(), HistoryModel::Recent, 100);
    HistoryModel pageNew(m_library.get(), HistoryModel::New, 100);
    QCOMPARE(quickRecent.rowCount(), 8);
    QCOMPARE(pageRecent.rowCount(), 100);
    QCOMPARE(quickNew.rowCount(), 8);
    QCOMPARE(pageNew.rowCount(), 100);
    QList<std::shared_ptr<QSignalSpy>> resets;
    for (HistoryModel *model : {&quickRecent, &quickNew, &pageRecent, &pageNew})
        resets.append(std::make_shared<QSignalSpy>(model, &QAbstractItemModel::modelReset));

    const double ms = measure([this](int run) { m_library->addHistory(run * 101 + 7); });
    for (const auto &spy : std::as_const(resets))
        QCOMPARE(spy->count(), 0);
    QCOMPARE(quickRecent.rowCount(), 8);
    QCOMPARE(pageRecent.rowCount(), 100);
    QCOMPARE(quickRecent.data(quickRecent.index(0), LibraryModel::IdRole).toLongLong(), (runs - 1) * 101 + 7);
    report("addHistory with 4 history models", ms, 5);
}

void ScaleTest::entriesAll()
{
    qsizetype size = 0;
    const double ms = measure([this, &size](int) { size = m_library->entriesFor(QStringLiteral("all"), -1).size(); });
    QCOMPARE(size, static_cast<qsizetype>(m_library->songCount()));
    report("entriesFor all", ms, 60);

    QCOMPARE(m_library->entriesFor(QStringLiteral("playlist"), 1).size(), playlistItems);
    QCOMPARE(m_library->artistAlbumRows(1).size(), 2);
}

void ScaleTest::batchedEntries()
{
    const QVariantList ids = m_library->allTracks();
    QList<QueueEntry> entries;
    const double ms = measure([this, &ids, &entries](int) { entries = m_library->queueEntries(ids); });
    QCOMPARE(entries.size(), ids.size());
    for (qsizetype i = 0; i < ids.size(); i += 997)
        QCOMPARE(entries.at(i).id, ids.at(i).toLongLong());
    report("queueEntries all", ms, 120);

    QVariantList rows;
    const double rowsMs = measure([this, &ids, &rows](int) { rows = m_library->trackRows(ids); });
    QCOMPARE(rows.size(), ids.size());
    QCOMPARE(rows.last().toMap().value(QStringLiteral("id")).toLongLong(), ids.last().toLongLong());
    report("trackRows all", rowsMs, 120);
}

QTEST_GUILESS_MAIN(ScaleTest)
#include "tst_scale.moc"
