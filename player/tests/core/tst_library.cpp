#include "Eq.h"
#include "Library.h"
#include "LibraryWatcher.h"
#include "Models.h"
#include "Settings.h"

#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/tbytevector.h>
#include <taglib/tpropertymap.h>
#include <taglib/tstring.h>
#include <taglib/tstringlist.h>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include <memory>
#include <utility>

using namespace microdap;

namespace {

constexpr int scanTimeoutMs = 60000;

const char *autoEq =
    "Preamp: -6.2 dB\n"
    "Filter 1: ON PK Fc 1000 Hz Gain -3.2 dB Q 1.41\n";

bool copyTree(const QString &from, const QString &to)
{
    if (!QDir().mkpath(to))
        return false;
    QDirIterator it(from, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        const QString relative = QDir(from).relativeFilePath(info.absoluteFilePath());
        const QString target = to + QLatin1Char('/') + relative;
        if (!QDir().mkpath(QFileInfo(target).absolutePath()))
            return false;
        if (!QFile::copy(info.absoluteFilePath(), target))
            return false;
    }
    return true;
}

QVariant scalar(const QSqlDatabase &db, const QString &sql, const QVariantList &binds = {})
{
    QSqlQuery query(db);
    query.prepare(sql);
    for (const QVariant &bind : binds)
        query.addBindValue(bind);
    if (!query.exec() || !query.next())
        return {};
    return query.value(0);
}

QList<qint64> column(const QSqlDatabase &db, const QString &sql)
{
    QList<qint64> values;
    QSqlQuery query(sql, db);
    while (query.next())
        values.append(query.value(0).toLongLong());
    return values;
}

QHash<QString, qint64> addedByPath(const QSqlDatabase &db)
{
    QHash<QString, qint64> map;
    QSqlQuery query(QStringLiteral("SELECT path, added FROM tracks"), db);
    while (query.next())
        map.insert(query.value(0).toString(), query.value(1).toLongLong());
    return map;
}

bool bumpMtime(const QString &path, int secs)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite))
        return false;
    return file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(secs),
                            QFileDevice::FileModificationTime);
}

bool retag(const QString &path, const char *key, const QStringList &values)
{
    TagLib::FLAC::File file(QFile::encodeName(path).constData());
    if (!file.isValid())
        return false;
    TagLib::PropertyMap props = file.properties();
    TagLib::StringList list;
    for (const QString &value : values)
        list.append(TagLib::String(value.toUtf8().constData(), TagLib::String::UTF8));
    props[key] = list;
    file.setProperties(props);
    return file.save();
}

bool retagAlbum(const QString &path, const QString &album)
{
    return retag(path, "ALBUM", {album});
}

int rowOf(const LibraryModel &model, const QString &title)
{
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.data(model.index(row), LibraryModel::TitleRole).toString() == title)
            return row;
    }
    return -1;
}

QString subtitleOf(const LibraryModel &model, const QString &title)
{
    const int row = rowOf(model, title);
    return row < 0 ? QString() : model.data(model.index(row), LibraryModel::SubtitleRole).toString();
}

bool replaceCover(const QString &path, const QByteArray &data)
{
    TagLib::FLAC::File file(QFile::encodeName(path).constData());
    if (!file.isValid())
        return false;
    file.removePictures();
    auto *picture = new TagLib::FLAC::Picture;
    picture->setData(TagLib::ByteVector(data.constData(), static_cast<unsigned int>(data.size())));
    picture->setMimeType("image/png");
    picture->setType(TagLib::FLAC::Picture::FrontCover);
    file.addPicture(picture);
    return file.save();
}

QString sha1Name(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex())
           + QStringLiteral(".png");
}

bool writeBytes(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(data) == data.size();
}

bool dropPlaylistStamps(QSqlQuery &query)
{
    return query.exec(QStringLiteral("DROP INDEX playlist_items_track"))
           && query.exec(QStringLiteral("ALTER TABLE playlists DROP COLUMN mtime"))
           && query.exec(QStringLiteral("ALTER TABLE playlists DROP COLUMN size"));
}

bool dropVersion6(QSqlQuery &query)
{
    return query.exec(QStringLiteral("DROP TABLE meta"))
           && query.exec(QStringLiteral("ALTER TABLE tracks DROP COLUMN rg_track_gain"))
           && query.exec(QStringLiteral("ALTER TABLE tracks DROP COLUMN rg_album_gain"))
           && query.exec(QStringLiteral("ALTER TABLE tracks DROP COLUMN rg_track_peak"))
           && query.exec(QStringLiteral("ALTER TABLE tracks DROP COLUMN rg_album_peak"))
           && query.exec(QStringLiteral("ALTER TABLE tracks DROP COLUMN art_path"))
           && query.exec(QStringLiteral("ALTER TABLE tracks DROP COLUMN has_lyrics"));
}

const char *const version5Schema[] = {
    "CREATE TABLE artists(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE)",
    "CREATE TABLE genres(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE)",
    "CREATE TABLE albums(id INTEGER PRIMARY KEY, title TEXT NOT NULL, artist TEXT NOT NULL, year INTEGER, "
    "art_path TEXT, added INTEGER NOT NULL, UNIQUE(title, artist))",
    "CREATE TABLE tracks(id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, mtime INTEGER NOT NULL, "
    "size INTEGER NOT NULL, title TEXT NOT NULL, artist TEXT NOT NULL, album_artist TEXT NOT NULL, "
    "album_id INTEGER NOT NULL REFERENCES albums(id), track_no INTEGER, disc_no INTEGER, year INTEGER, "
    "genre TEXT NOT NULL DEFAULT '', duration_ms INTEGER, added INTEGER NOT NULL, "
    "rating INTEGER NOT NULL DEFAULT 0, sample_rate INTEGER, bit_depth INTEGER)",
    "CREATE INDEX tracks_album ON tracks(album_id)",
    "CREATE INDEX tracks_artist ON tracks(artist)",
    "CREATE INDEX tracks_genre ON tracks(genre)",
    "CREATE TABLE track_artists(track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE, "
    "artist_id INTEGER NOT NULL REFERENCES artists(id) ON DELETE CASCADE, pos INTEGER NOT NULL, "
    "PRIMARY KEY(track_id, pos))",
    "CREATE INDEX track_artists_artist ON track_artists(artist_id)",
    "CREATE TABLE history(id INTEGER PRIMARY KEY, "
    "track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE, played_at INTEGER NOT NULL)",
    "CREATE INDEX history_played ON history(played_at)",
    "CREATE INDEX history_track ON history(track_id)",
    "CREATE TABLE playlists(id INTEGER PRIMARY KEY, name TEXT NOT NULL, source TEXT, "
    "skipped INTEGER NOT NULL DEFAULT 0, mtime INTEGER NOT NULL DEFAULT -1, size INTEGER NOT NULL DEFAULT -1)",
    "CREATE UNIQUE INDEX playlists_source ON playlists(source)",
    "CREATE TABLE playlist_items(playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE, "
    "pos INTEGER NOT NULL, track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE, "
    "PRIMARY KEY(playlist_id, pos))",
    "CREATE INDEX playlist_items_track ON playlist_items(track_id)",
    "CREATE TABLE pins(id INTEGER PRIMARY KEY, kind TEXT NOT NULL, ref_id INTEGER NOT NULL, pos INTEGER NOT NULL)",
    "INSERT INTO albums(id, title, artist, year, art_path, added) VALUES(1, 'Old', 'Band', 2001, NULL, 10)",
    "INSERT INTO tracks(id, path, mtime, size, title, artist, album_artist, album_id, track_no, added, rating, "
    "sample_rate, bit_depth) VALUES(1, '/old/1.flac', 100, 1000, 'First', 'Band', 'Band', 1, 1, 10, 1, 44100, 16)",
    "INSERT INTO tracks(id, path, mtime, size, title, artist, album_artist, album_id, track_no, added, rating, "
    "sample_rate, bit_depth) VALUES(2, '/old/2.flac', 200, 2000, 'Second', 'Band', 'Band', 1, 2, 10, 0, 96000, 24)",
    "INSERT INTO artists(id, name) VALUES(1, 'Band')",
    "INSERT INTO artists(id, name) VALUES(2, 'Guest')",
    "INSERT INTO track_artists(track_id, artist_id, pos) VALUES(1, 1, 0)",
    "INSERT INTO track_artists(track_id, artist_id, pos) VALUES(2, 1, 0)",
    "INSERT INTO track_artists(track_id, artist_id, pos) VALUES(2, 2, 1)",
    "INSERT INTO history(id, track_id, played_at) VALUES(1, 1, 1000)",
    "INSERT INTO history(id, track_id, played_at) VALUES(2, 2, 2000)",
    "INSERT INTO history(id, track_id, played_at) VALUES(3, 1, 3000)",
    "INSERT INTO playlists(id, name, source, skipped, mtime, size) VALUES(1, 'Mix', '/old/mix.m3u', 1, 5, 6)",
    "INSERT INTO playlist_items(playlist_id, pos, track_id) VALUES(1, 0, 2)",
    "INSERT INTO playlist_items(playlist_id, pos, track_id) VALUES(1, 1, 1)",
    "INSERT INTO pins(id, kind, ref_id, pos) VALUES(1, 'song', 2, 0)",
    "INSERT INTO pins(id, kind, ref_id, pos) VALUES(2, 'album', 1, 1)",
    "INSERT INTO pins(id, kind, ref_id, pos) VALUES(3, 'playlist', 1, 2)",
    "INSERT INTO pins(id, kind, ref_id, pos) VALUES(4, 'artist', 2, 3)",
    "PRAGMA user_version = 5",
};

const char *const version5Tables[] = {"albums", "artists", "track_artists", "history",
                                      "playlists", "playlist_items", "pins"};

bool makeVersion5(const QString &path, const QStringList &extra = {})
{
    const QString name = QStringLiteral("v5-") + QFileInfo(path).fileName();
    bool ok = true;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        ok = db.open();
        QSqlQuery query(db);
        for (const char *sql : version5Schema)
            ok = ok && query.exec(QString::fromLatin1(sql));
        for (const QString &sql : extra)
            ok = ok && query.exec(sql);
        query.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

QStringList dumpTable(const QSqlDatabase &db, const QString &table, const QString &columns = QStringLiteral("*"))
{
    QStringList rows;
    QSqlQuery query(QStringLiteral("SELECT %1 FROM %2 ORDER BY rowid").arg(columns, table), db);
    while (query.next()) {
        QStringList values;
        for (int i = 0; i < query.record().count(); ++i)
            values.append(query.value(i).isNull() ? QStringLiteral("NULL") : query.value(i).toString());
        rows.append(values.join(QLatin1Char('|')));
    }
    return rows;
}

constexpr const char *version5TrackColumns =
    "id, path, mtime, size, title, artist, album_artist, album_id, track_no, disc_no, year, genre, "
    "duration_ms, added, rating, sample_rate, bit_depth";

QHash<QString, QStringList> dumpVersion5(const QString &path)
{
    QHash<QString, QStringList> dump;
    const QString name = QStringLiteral("dump-") + QFileInfo(path).fileName();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        if (db.open()) {
            for (const char *table : version5Tables)
                dump.insert(QString::fromLatin1(table), dumpTable(db, QString::fromLatin1(table)));
            dump.insert(QStringLiteral("tracks"),
                        dumpTable(db, QStringLiteral("tracks"), QString::fromLatin1(version5TrackColumns)));
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return dump;
}

QByteArray fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha1);
}

bool setPragma(const QString &path, const QStringList &statements)
{
    const QString name = QStringLiteral("pragma-") + QFileInfo(path).fileName();
    bool ok = true;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        ok = db.open();
        QSqlQuery query(db);
        for (const QString &sql : statements)
            ok = ok && query.exec(sql);
        query.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

bool dropPlaylistColumns(QSqlQuery &query)
{
    return dropPlaylistStamps(query)
           && query.exec(QStringLiteral("DROP INDEX playlists_source"))
           && query.exec(QStringLiteral("ALTER TABLE playlists DROP COLUMN skipped"))
           && query.exec(QStringLiteral("ALTER TABLE playlists DROP COLUMN source"));
}

QString fakeMount;

bool fakeMountCheck(const QString &dir)
{
    return !fakeMount.isEmpty() && QDir::cleanPath(dir) == fakeMount;
}

QString fakeDevice;
void (*deviceFlip)() = nullptr;

QString fakeDeviceCheck(const QString &)
{
    const QString device = fakeDevice;
    if (deviceFlip)
        std::exchange(deviceFlip, nullptr)();
    return device;
}

QString pulledCard;
QString pulledRoot;
int pulledChecks = 0;

bool pullingMountCheck(const QString &dir)
{
    if (QDir::cleanPath(dir) != pulledCard)
        return false;
    if (++pulledChecks == 1)
        QDir(pulledRoot).removeRecursively();
    return true;
}

QList<qint64> idsOf(const QList<QueueEntry> &entries)
{
    QList<qint64> ids;
    for (const QueueEntry &entry : entries)
        ids.append(entry.id);
    return ids;
}

QList<qint64> idsOf(const QVariantList &values)
{
    QList<qint64> ids;
    for (const QVariant &value : values)
        ids.append(value.toLongLong());
    return ids;
}

qint64 trackId(const QSqlDatabase &db, const QString &path)
{
    return scalar(db, QStringLiteral("SELECT id FROM tracks WHERE path = ?"), {path}).toLongLong();
}

qint64 playlistId(const QSqlDatabase &db, const QString &name)
{
    return scalar(db, QStringLiteral("SELECT id FROM playlists WHERE name = ?"), {name}).toLongLong();
}

}

class LibraryTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void counts();
    void cjk();
    void art();
    void rescanUnchanged();
    void songs();
    void albums();
    void genres();
    void artists();
    void splitArtists();
    void migrate();
    void history();
    void historyLimit();
    void historyPrune();
    void newAlbums();
    void rating();
    void touched();
    void removed();
    void missingRoot();
    void artContent();
    void pruneArtForeign();
    void pins();
    void pinsPrune();
    void rateAndDepth();
    void batchedRows();
    void entriesFor();
    void migrateFrom2();
    void playlists();
    void genreTracks();
    void folderOff();
    void mediaUnmounted();
    void mediaMountLayout();
    void candidateRoots();
    void playlistFiles();
    void playlistStamps();
    void playlistUnreadable();
    void playlistStable();
    void migrateFrom3();
    void migrateFrom4();
    void rootsChange();
    void watcherDebounce();
    void watcherNewDirectory();
    void watcherLimit();
    void watcherMissingRoot();
    void watcherClimbStops();
    void watcherPoll();
    void mediaDefaultRoots();
    void mediaKeepsUserData();
    void watcherMedia();
    void watcherMediaLimit();
    void watcherMountOverDir();
    void scanCardPulled();
    void scanCardRecheck();
    void mediaMountsFilter();
    void migrateFrom5();
    void migrateFailureRollsBack();
    void newerReader();
    void trackTags();
    void trackArtFirst();
    void newFormats();
    void migrateFrom6();

private:
    bool scan(int &added, int &updated, int &removed);
    bool scan(const QStringList &roots, int &added, int &updated, int &removed);
    bool scanRoots();
    QString firstTrackOf(const QString &album) const;
    QString artPathOf(const QString &album) const;

    QTemporaryDir m_dir;
    QString m_music;
    std::unique_ptr<Library> m_library;
    std::unique_ptr<Settings> m_settings;
};

bool LibraryTest::scanRoots()
{
    QSignalSpy spy(m_library.get(), &Library::scanned);
    m_library->rescanRoots();
    return spy.wait(scanTimeoutMs) && spy.takeFirst().at(3).toBool();
}

bool LibraryTest::scan(int &added, int &updated, int &removed)
{
    return scan({m_music}, added, updated, removed);
}

bool LibraryTest::scan(const QStringList &roots, int &added, int &updated, int &removed)
{
    QSignalSpy spy(m_library.get(), &Library::scanned);
    m_library->rescan(roots);
    if (!spy.wait(scanTimeoutMs))
        return false;
    const QList<QVariant> args = spy.takeFirst();
    added = args.at(0).toInt();
    updated = args.at(1).toInt();
    removed = args.at(2).toInt();
    return args.at(3).toBool();
}

QString LibraryTest::firstTrackOf(const QString &album) const
{
    return scalar(m_library->database(),
                  QStringLiteral("SELECT t.path FROM tracks t JOIN albums a ON a.id = t.album_id "
                                 "WHERE a.title = ? ORDER BY t.track_no LIMIT 1"),
                  {album})
        .toString();
}

QString LibraryTest::artPathOf(const QString &album) const
{
    return scalar(m_library->database(),
                  QStringLiteral("SELECT art_path FROM albums WHERE title = ?"), {album})
        .toString();
}

void LibraryTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    fakeDevice = m_dir.path() + QStringLiteral("/dev-card");
    QVERIFY(writeBytes(fakeDevice, QByteArray()));
    Scanner::setDeviceCheck(fakeDeviceCheck);
    m_music = m_dir.path() + QStringLiteral("/music");
    QVERIFY(copyTree(QStringLiteral(MICRODAP_FIXTURE_MUSIC), m_music));

    m_library = std::make_unique<Library>(m_dir.path() + QStringLiteral("/library.db"),
                                          m_dir.path() + QStringLiteral("/art"));
    QVERIFY(m_library->isOpen());
    QCOMPARE(scalar(m_library->database(), QStringLiteral("PRAGMA user_version")).toInt(), 7);
    QCOMPARE(scalar(m_library->database(), QStringLiteral("SELECT value FROM meta WHERE key = 'min_reader'"))
                 .toInt(),
             5);
    QCOMPARE(scalar(m_library->database(),
                    QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' "
                                   "AND name = 'playlist_items_track'"))
                 .toInt(),
             1);

    QSignalSpy scanningSpy(m_library.get(), &Library::scanningChanged);
    QSignalSpy tracksSpy(m_library.get(), &Library::tracksChanged);
    QSignalSpy albumsSpy(m_library.get(), &Library::albumsChanged);
    QSignalSpy countsSpy(m_library.get(), &Library::countsChanged);
    QSignalSpy playlistsSpy(m_library.get(), &Library::playlistsChanged);
    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 8);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);
    QCOMPARE(scanningSpy.count(), 2);
    QCOMPARE(tracksSpy.count(), 1);
    QCOMPARE(albumsSpy.count(), 1);
    QCOMPARE(countsSpy.count(), 1);
    QCOMPARE(playlistsSpy.count(), 0);
    QVERIFY(!m_library->scanning());
}

void LibraryTest::counts()
{
    const QVariantMap counts = m_library->counts();
    QCOMPARE(counts.value(QStringLiteral("tracks")).toInt(), 8);
    QCOMPARE(counts.value(QStringLiteral("albums")).toInt(), 3);
    QCOMPARE(counts.value(QStringLiteral("artists")).toInt(), 6);
    QCOMPARE(counts.value(QStringLiteral("genres")).toInt(), 3);
}

void LibraryTest::cjk()
{
    const QSqlDatabase db = m_library->database();
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM albums WHERE title = ? AND artist = ?"),
                    {QStringLiteral("アルバム二"), QStringLiteral("アーティスト")})
                 .toInt(),
             1);
    QCOMPARE(scalar(db, QStringLiteral("SELECT title FROM tracks WHERE track_no = 1 AND artist = ?"),
                    {QStringLiteral("アーティスト")})
                 .toString(),
             QStringLiteral("一"));
    QCOMPARE(scalar(db, QStringLiteral("SELECT year FROM albums WHERE title = ?"),
                    {QStringLiteral("Album One")})
                 .toInt(),
             2020);
    QVERIFY(scalar(db, QStringLiteral("SELECT duration_ms FROM tracks WHERE title = ?"),
                   {QStringLiteral("One")})
                .toInt()
            > 1000);
}

void LibraryTest::art()
{
    const QSqlDatabase db = m_library->database();
    const QString embedded = artPathOf(QStringLiteral("Album One"));
    QVERIFY(!embedded.isEmpty());
    QVERIFY(QFileInfo::exists(embedded));
    QVERIFY(embedded.startsWith(m_library->artDir()));
    QVERIFY(embedded.endsWith(QLatin1StringView(".png")));

    const QString folder = artPathOf(QStringLiteral("アルバム二"));
    QVERIFY(!folder.isEmpty());
    QVERIFY(QFileInfo::exists(folder));
    QCOMPARE(QFileInfo(folder).fileName(), QStringLiteral("cover.png"));

    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM albums WHERE title = ? AND art_path IS NULL"),
                    {QStringLiteral("Single")})
                 .toInt(),
             1);
}

void LibraryTest::rescanUnchanged()
{
    const QHash<QString, qint64> before = addedByPath(m_library->database());
    QCOMPARE(before.size(), 8);

    QSignalSpy scanningSpy(m_library.get(), &Library::scanningChanged);
    QSignalSpy tracksSpy(m_library.get(), &Library::tracksChanged);
    QSignalSpy albumsSpy(m_library.get(), &Library::albumsChanged);
    QSignalSpy countsSpy(m_library.get(), &Library::countsChanged);
    QSignalSpy playlistsSpy(m_library.get(), &Library::playlistsChanged);
    QSignalSpy playlistCountSpy(m_library.get(), &Library::playlistCountChanged);
    QSignalSpy pinsSpy(m_library.get(), &Library::pinsChanged);
    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);
    QCOMPARE(addedByPath(m_library->database()), before);
    QCOMPARE(scanningSpy.count(), 2);
    QCOMPARE(tracksSpy.count(), 0);
    QCOMPARE(albumsSpy.count(), 0);
    QCOMPARE(countsSpy.count(), 0);
    QCOMPARE(playlistsSpy.count(), 0);
    QCOMPARE(playlistCountSpy.count(), 0);
    QCOMPARE(pinsSpy.count(), 0);
}

void LibraryTest::songs()
{
    SongsModel model(m_library.get());
    model.ensureLoaded();
    QCOMPARE(model.rowCount(), 8);
    const int row = model.indexOf(
        scalar(m_library->database(), QStringLiteral("SELECT id FROM tracks WHERE title = ?"),
               {QStringLiteral("One")})
            .toLongLong());
    QVERIFY(row >= 0);
    QCOMPARE(model.data(model.index(row), LibraryModel::SectionRole).toString(), QStringLiteral("O"));
    QCOMPARE(model.data(model.index(row), LibraryModel::SubtitleRole).toString(),
             QStringLiteral("Artist A"));
    QVERIFY(model.roleNames().value(LibraryModel::TitleRole) == QByteArrayLiteral("title"));
    QStringList titles;
    QStringList sections;
    for (int r = 0; r < model.rowCount(); ++r) {
        titles.append(model.data(model.index(r), LibraryModel::TitleRole).toString());
        sections.append(model.data(model.index(r), LibraryModel::SectionRole).toString());
    }
    QCOMPARE(titles.mid(5), QStringList({QStringLiteral("一"), QStringLiteral("三"), QStringLiteral("二")}));
    QCOMPARE(sections.mid(5), QStringList({QStringLiteral("漢"), QStringLiteral("漢"), QStringLiteral("漢")}));
    QVariantList ids;
    for (int r = 0; r < model.rowCount(); ++r)
        ids.append(model.data(model.index(r), LibraryModel::IdRole).toLongLong());
    QCOMPARE(m_library->allTracks(), ids);
}

void LibraryTest::albums()
{
    AlbumsModel model(m_library.get());
    model.ensureLoaded();
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(0), LibraryModel::TitleRole).toString(), QStringLiteral("Album One"));
    QCOMPARE(model.data(model.index(0), LibraryModel::SectionRole).toString(), QStringLiteral("A"));
    QCOMPARE(model.data(model.index(1), LibraryModel::TitleRole).toString(), QStringLiteral("Single"));
    QCOMPARE(model.data(model.index(2), LibraryModel::TitleRole).toString(), QStringLiteral("アルバム二"));
    QCOMPARE(model.data(model.index(2), LibraryModel::SectionRole).toString(), QStringLiteral("あ"));
}

void LibraryTest::genres()
{
    GenresModel model(m_library.get());
    model.ensureLoaded();
    QCOMPARE(model.rowCount(), 3);
    const int row = model.indexOf(
        scalar(m_library->database(), QStringLiteral("SELECT id FROM genres WHERE name = ?"),
               {QStringLiteral("Electronic")})
            .toLongLong());
    QVERIFY(row >= 0);
    QCOMPARE(model.data(model.index(row), LibraryModel::SubtitleRole).toString(),
             QStringLiteral("4 songs"));
    QVERIFY(model.roleNames().value(LibraryModel::TitleRole) == QByteArrayLiteral("name"));
}

void LibraryTest::artists()
{
    const QSqlDatabase db = m_library->database();
    ArtistsModel model(m_library.get());
    model.ensureLoaded();
    QCOMPARE(model.rowCount(), 6);
    QCOMPARE(model.data(model.index(0), LibraryModel::TitleRole).toString(), QStringLiteral("Artist A"));
    QCOMPARE(model.data(model.index(1), LibraryModel::TitleRole).toString(), QStringLiteral("Artist C"));
    QCOMPARE(model.data(model.index(2), LibraryModel::TitleRole).toString(), QStringLiteral("Artist E"));
    QCOMPARE(model.data(model.index(3), LibraryModel::TitleRole).toString(), QStringLiteral("Artist F"));
    QCOMPARE(model.data(model.index(4), LibraryModel::TitleRole).toString(), QStringLiteral("The Band"));
    QCOMPARE(model.data(model.index(4), LibraryModel::SectionRole).toString(), QStringLiteral("B"));
    QCOMPARE(model.data(model.index(5), LibraryModel::TitleRole).toString(), QStringLiteral("アーティスト"));
    QCOMPARE(model.data(model.index(5), LibraryModel::SectionRole).toString(), QStringLiteral("あ"));
    QCOMPARE(model.data(model.index(4), LibraryModel::SubtitleRole).toString(),
             QStringLiteral("1 album, 1 song"));
    QCOMPARE(model.data(model.index(0), LibraryModel::SubtitleRole).toString(),
             QStringLiteral("1 album, 3 songs"));
    QCOMPARE(model.data(model.index(1), LibraryModel::SubtitleRole).toString(),
             QStringLiteral("1 album, 1 song"));
    QCOMPARE(model.data(model.index(2), LibraryModel::SubtitleRole).toString(),
             QStringLiteral("1 album, 1 song"));
    QCOMPARE(model.data(model.index(3), LibraryModel::SubtitleRole).toString(),
             QStringLiteral("1 album, 1 song"));
    QCOMPARE(rowOf(model, QStringLiteral("Artist E, Artist F")), -1);
    QVERIFY(!model.data(model.index(0), LibraryModel::ArtPathRole).toString().isEmpty());
    QVERIFY(model.roleNames().value(LibraryModel::TitleRole) == QByteArrayLiteral("name"));

    const qint64 three = scalar(db, QStringLiteral("SELECT id FROM tracks WHERE title = ?"),
                                {QStringLiteral("Three")})
                             .toLongLong();
    QVERIFY(three > 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT artist FROM tracks WHERE id = ?"), {three}).toString(),
             QStringLiteral("Artist A, Artist C"));
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM track_artists WHERE track_id = ?"),
                    {three})
                 .toInt(),
             2);
    QCOMPARE(scalar(db, QStringLiteral("SELECT ar.name FROM track_artists ta "
                                       "JOIN artists ar ON ar.id = ta.artist_id "
                                       "WHERE ta.track_id = ? AND ta.pos = 1"),
                    {three})
                 .toString(),
             QStringLiteral("Artist C"));

    const qint64 four = scalar(db, QStringLiteral("SELECT id FROM tracks WHERE title = ?"),
                               {QStringLiteral("Four")})
                            .toLongLong();
    QVERIFY(four > 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT artist FROM tracks WHERE id = ?"), {four}).toString(),
             QStringLiteral("Artist E, Artist F"));
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM track_artists WHERE track_id = ?"),
                    {four})
                 .toInt(),
             2);
    QCOMPARE(scalar(db, QStringLiteral("SELECT ar.name FROM track_artists ta "
                                       "JOIN artists ar ON ar.id = ta.artist_id "
                                       "WHERE ta.track_id = ? AND ta.pos = 0"),
                    {four})
                 .toString(),
             QStringLiteral("Artist E"));
    QCOMPARE(scalar(db, QStringLiteral("SELECT ar.name FROM track_artists ta "
                                       "JOIN artists ar ON ar.id = ta.artist_id "
                                       "WHERE ta.track_id = ? AND ta.pos = 1"),
                    {four})
                 .toString(),
             QStringLiteral("Artist F"));
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM track_artists")).toInt(), 10);

    const qint64 artistA = model.data(model.index(0), LibraryModel::IdRole).toLongLong();
    const qint64 artistC = model.data(model.index(1), LibraryModel::IdRole).toLongLong();
    const qint64 albumOne = scalar(db, QStringLiteral("SELECT id FROM albums WHERE title = ?"),
                                   {QStringLiteral("Album One")})
                                .toLongLong();
    QCOMPARE(m_library->artistAlbums(artistC), QVariantList{albumOne});
    QCOMPARE(m_library->artistTracks(artistC), QVariantList{three});
    QCOMPARE(m_library->artistAlbums(artistA), QVariantList{albumOne});
    QCOMPARE(m_library->artistTracks(artistA).size(), 3);
    QCOMPARE(m_library->artistTracks(artistA).last().toLongLong(), three);
}

void LibraryTest::splitArtists()
{
    const QSqlDatabase db = m_library->database();
    const QString source = firstTrackOf(QStringLiteral("Album One"));
    QVERIFY(!source.isEmpty());
    const QString splitDir = m_music + QStringLiteral("/Split");
    const QString semi = splitDir + QStringLiteral("/01 Semi.flac");
    const QString band = splitDir + QStringLiteral("/02 Band.flac");
    QVERIFY(QDir().mkpath(splitDir));
    QVERIFY(QFile::copy(source, semi));
    QVERIFY(QFile::copy(source, band));
    QVERIFY(retag(semi, "ALBUM", {QStringLiteral("Split")}));
    QVERIFY(retag(semi, "TITLE", {QStringLiteral("Semi")}));
    QVERIFY(retag(semi, "ARTIST", {QStringLiteral("Artist A; Artist D")}));
    QVERIFY(retag(band, "ALBUM", {QStringLiteral("Split")}));
    QVERIFY(retag(band, "TITLE", {QStringLiteral("Band")}));
    QVERIFY(retag(band, "ARTIST", {QStringLiteral("Earth, Wind & Fire")}));
    QVERIFY(retag(band, "ALBUMARTIST", {QStringLiteral("earth, wind & fire")}));

    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 2);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);

    ArtistsModel model(m_library.get());
    model.ensureLoaded();
    QCOMPARE(model.rowCount(), 8);
    QCOMPARE(rowOf(model, QStringLiteral("Artist A; Artist D")), -1);
    QCOMPARE(rowOf(model, QStringLiteral("Earth")), -1);
    QCOMPARE(rowOf(model, QStringLiteral("Wind & Fire")), -1);
    QCOMPARE(subtitleOf(model, QStringLiteral("Artist D")), QStringLiteral("1 album, 1 song"));
    QCOMPARE(subtitleOf(model, QStringLiteral("Earth, Wind & Fire")), QStringLiteral("1 album, 1 song"));
    QCOMPARE(subtitleOf(model, QStringLiteral("Artist A")), QStringLiteral("2 albums, 4 songs"));
    QCOMPARE(scalar(db, QStringLiteral("SELECT artist FROM tracks WHERE path = ?"), {semi}).toString(),
             QStringLiteral("Artist A, Artist D"));
    QCOMPARE(scalar(db, QStringLiteral("SELECT artist FROM tracks WHERE path = ?"), {band}).toString(),
             QStringLiteral("Earth, Wind & Fire"));
    QCOMPARE(scalar(db, QStringLiteral("SELECT album_artist FROM tracks WHERE path = ?"), {band})
                 .toString(),
             QStringLiteral("earth, wind & fire"));
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM track_artists WHERE track_id = "
                                       "(SELECT id FROM tracks WHERE path = ?)"),
                    {band})
                 .toInt(),
             1);

    QVERIFY(QDir(splitDir).removeRecursively());
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 2);
    QCOMPARE(model.rowCount(), 6);
    QCOMPARE(rowOf(model, QStringLiteral("Artist D")), -1);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM artists")).toInt(), 6);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM track_artists")).toInt(), 10);
    QCOMPARE(m_library->counts().value(QStringLiteral("albums")).toInt(), 3);
}

void LibraryTest::migrate()
{
    const QString path = m_dir.path() + QStringLiteral("/migrate.db");
    QVERIFY(QFile::copy(m_library->dbPath(), path));
    const QString setup = QStringLiteral("migrate-setup");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), setup);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(dropVersion6(query));
        QVERIFY(query.exec(QStringLiteral("DROP TABLE track_artists")));
        QVERIFY(query.exec(QStringLiteral("ALTER TABLE tracks DROP COLUMN sample_rate")));
        QVERIFY(query.exec(QStringLiteral("ALTER TABLE tracks DROP COLUMN bit_depth")));
        QVERIFY(dropPlaylistColumns(query));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 1")));
        query.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(setup);

    Library library(path, m_dir.path() + QStringLiteral("/art"));
    QVERIFY(library.isOpen());
    const QSqlDatabase db = library.database();
    QCOMPARE(scalar(db, QStringLiteral("PRAGMA user_version")).toInt(), 7);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM track_artists")).toInt(), 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE mtime = -1")).toInt(), 8);

    QSignalSpy spy(&library, &Library::scanned);
    library.rescan({m_music});
    QVERIFY(spy.wait(scanTimeoutMs));
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 0);
    QCOMPARE(args.at(1).toInt(), 8);
    QCOMPARE(args.at(2).toInt(), 0);
    QVERIFY(args.at(3).toBool());
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE mtime = -1")).toInt(), 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM track_artists")).toInt(), 10);

    ArtistsModel model(&library);
    model.ensureLoaded();
    QCOMPARE(model.rowCount(), 6);
    QCOMPARE(subtitleOf(model, QStringLiteral("Artist C")), QStringLiteral("1 album, 1 song"));
}

void LibraryTest::history()
{
    const QSqlDatabase db = m_library->database();
    const qint64 first = scalar(db, QStringLiteral("SELECT id FROM tracks WHERE title = ?"),
                                {QStringLiteral("One")})
                             .toLongLong();
    const qint64 second = scalar(db, QStringLiteral("SELECT id FROM tracks WHERE title = ?"),
                                 {QStringLiteral("Single")})
                              .toLongLong();
    QVERIFY(first > 0);
    QVERIFY(second > 0);

    HistoryModel model(m_library.get());
    QCOMPARE(model.mode(), HistoryModel::Recent);
    QCOMPARE(model.rowCount(), 0);
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy moveSpy(&model, &QAbstractItemModel::rowsMoved);
    QSignalSpy playedSpy(m_library.get(), &Library::played);

    const QDateTime base = QDateTime::currentDateTimeUtc();
    m_library->addHistory(first, base);
    m_library->addHistory(second, base.addSecs(1));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), LibraryModel::IdRole).toLongLong(), second);
    QCOMPARE(model.data(model.index(1), LibraryModel::IdRole).toLongLong(), first);
    QCOMPARE(model.data(model.index(0), LibraryModel::KindRole).toString(), QStringLiteral("song"));
    QCOMPARE(insertSpy.count(), 2);

    m_library->addHistory(first, base.addSecs(2));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), LibraryModel::IdRole).toLongLong(), first);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM history")).toInt(), 3);
    QCOMPARE(moveSpy.count(), 1);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(playedSpy.count(), 3);
    QCOMPARE(playedSpy.at(2).at(0).toLongLong(), first);

    HistoryModel reloaded(m_library.get());
    QCOMPARE(reloaded.rowCount(), 2);
    QCOMPARE(reloaded.data(reloaded.index(0), LibraryModel::IdRole).toLongLong(), first);
}

void LibraryTest::historyLimit()
{
    const QList<qint64> ids = column(m_library->database(), QStringLiteral("SELECT id FROM tracks ORDER BY id"));
    QVERIFY(ids.size() >= 4);
    HistoryModel recent(m_library.get(), HistoryModel::Recent, 2);
    HistoryModel fresh(m_library.get(), HistoryModel::New, 2);
    QCOMPARE(recent.limit(), 2);
    QCOMPARE(recent.rowCount(), 2);
    QCOMPARE(fresh.rowCount(), 2);
    QCOMPARE(fresh.data(fresh.index(0), LibraryModel::KindRole).toString(), QStringLiteral("album"));
    QSignalSpy recentReset(&recent, &QAbstractItemModel::modelReset);
    QSignalSpy freshReset(&fresh, &QAbstractItemModel::modelReset);

    const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(60);
    for (int i = 0; i < 4; ++i) {
        m_library->addHistory(ids.at(i), base.addSecs(i));
        QCOMPARE(recent.rowCount(), 2);
        QCOMPARE(recent.data(recent.index(0), LibraryModel::IdRole).toLongLong(), ids.at(i));
    }
    QCOMPARE(recent.data(recent.index(1), LibraryModel::IdRole).toLongLong(), ids.at(2));
    QCOMPARE(recentReset.count(), 0);
    QCOMPARE(freshReset.count(), 0);

    QSignalSpy limitSpy(&recent, &HistoryModel::limitChanged);
    recent.setLimit(0);
    QCOMPARE(limitSpy.count(), 1);
    QVERIFY(recent.rowCount() >= 4);

    emit m_library->albumsChanged();
    QCOMPARE(freshReset.count(), 1);
    QCOMPARE(recentReset.count(), 1);
    emit m_library->tracksChanged();
    QCOMPARE(recentReset.count(), 2);
    QCOMPARE(freshReset.count(), 1);

    QSqlQuery cleanup(m_library->database());
    cleanup.prepare(QStringLiteral("DELETE FROM history WHERE played_at >= ?"));
    cleanup.addBindValue(base.toMSecsSinceEpoch());
    QVERIFY(cleanup.exec());
    QCOMPARE(scalar(m_library->database(), QStringLiteral("SELECT COUNT(*) FROM history")).toInt(), 3);
}

void LibraryTest::historyPrune()
{
    const QString path = m_dir.path() + QStringLiteral("/history.db");
    QVERIFY(QFile::copy(m_library->dbPath(), path));
    Library library(path, m_dir.path() + QStringLiteral("/art"));
    QVERIFY(library.isOpen());
    const QSqlDatabase db = library.database();
    const qint64 id = scalar(db, QStringLiteral("SELECT id FROM tracks ORDER BY id LIMIT 1")).toLongLong();
    QVERIFY(id > 0);
    {
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral("DELETE FROM history")));
        QVERIFY(query.exec(QStringLiteral("BEGIN")));
        query.prepare(QStringLiteral("INSERT INTO history(track_id, played_at) VALUES(?, ?)"));
        for (int i = 0; i < 2050; ++i) {
            query.bindValue(0, id);
            query.bindValue(1, i);
            QVERIFY(query.exec());
        }
        QVERIFY(query.exec(QStringLiteral("COMMIT")));
    }
    for (int i = 0; i < Library::historyPruneEvery - 1; ++i)
        library.addHistory(id);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM history")).toInt(),
             2050 + Library::historyPruneEvery - 1);
    library.addHistory(id);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM history")).toInt(), Library::historyKeep);
    QVERIFY(scalar(db, QStringLiteral("SELECT MIN(played_at) FROM history")).toLongLong() >= 2050 - 1900);
}

void LibraryTest::newAlbums()
{
    HistoryModel model(m_library.get());
    QSignalSpy modeSpy(&model, &HistoryModel::modeChanged);
    model.setMode(HistoryModel::New);
    QCOMPARE(modeSpy.count(), 1);
    QCOMPARE(model.rowCount(), 3);

    QList<qint64> actual;
    for (int row = 0; row < model.rowCount(); ++row)
        actual.append(model.data(model.index(row), LibraryModel::IdRole).toLongLong());
    const QList<qint64> expected = column(
        m_library->database(), QStringLiteral("SELECT id FROM albums ORDER BY added DESC, id DESC"));
    QCOMPARE(expected.size(), 3);
    QCOMPARE(actual, expected);
}

void LibraryTest::rating()
{
    const QSqlDatabase db = m_library->database();
    const qint64 id = scalar(db, QStringLiteral("SELECT id FROM tracks WHERE title = ?"),
                             {QStringLiteral("Two")})
                          .toLongLong();
    QVERIFY(id > 0);
    QCOMPARE(m_library->rating(id), 0);

    QSignalSpy spy(m_library.get(), &Library::ratingChanged);
    m_library->setRating(id, 1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(scalar(db, QStringLiteral("SELECT rating FROM tracks WHERE id = ?"), {id}).toInt(), 1);
    QCOMPARE(m_library->rating(id), 1);

    m_library->setRating(id, 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT rating FROM tracks WHERE id = ?"), {id}).toInt(), 0);
}

void LibraryTest::touched()
{
    const QString path = firstTrackOf(QStringLiteral("Album One"));
    QVERIFY(!path.isEmpty());
    const QHash<QString, qint64> before = addedByPath(m_library->database());

    QVERIFY(bumpMtime(path, 3600));

    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 1);
    QCOMPARE(removed, 0);
    QCOMPARE(addedByPath(m_library->database()), before);
}

void LibraryTest::removed()
{
    const QString path = firstTrackOf(QStringLiteral("Album One"));
    QVERIFY(!path.isEmpty());
    QVERIFY(QFile::remove(path));

    int added = 0;
    int updated = 0;
    int removedCount = 0;
    QVERIFY(scan(added, updated, removedCount));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removedCount, 1);

    const QSqlDatabase db = m_library->database();
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks")).toInt(), 7);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE path = ?"), {path}).toInt(), 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM history")).toInt(), 1);
    QCOMPARE(m_library->counts().value(QStringLiteral("albums")).toInt(), 3);
}

void LibraryTest::missingRoot()
{
    const QSqlDatabase db = m_library->database();
    const QString extra = m_dir.path() + QStringLiteral("/extra");
    const QString hidden = m_dir.path() + QStringLiteral("/extra.gone");
    QVERIFY(copyTree(QStringLiteral(MICRODAP_FIXTURE_MUSIC) + QStringLiteral("/Single"),
                     extra + QStringLiteral("/Single")));
    const QStringList roots{m_music, extra};

    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(roots, added, updated, removed));
    QCOMPARE(added, 1);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks")).toInt(), 8);

    const qint64 id = scalar(db, QStringLiteral("SELECT id FROM tracks ORDER BY id DESC LIMIT 1"))
                          .toLongLong();
    QVERIFY(id > 0);
    QVERIFY(scalar(db, QStringLiteral("SELECT path FROM tracks WHERE id = ?"), {id})
                .toString()
                .startsWith(extra + QLatin1Char('/')));
    m_library->addHistory(id);
    const int history = scalar(db, QStringLiteral("SELECT COUNT(*) FROM history")).toInt();
    QCOMPARE(history, 2);

    QVERIFY(QDir().rename(extra, hidden));
    QVERIFY(scan(roots, added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks")).toInt(), 8);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE id = ?"), {id}).toInt(), 1);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM history WHERE track_id = ?"), {id}).toInt(), 1);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM history")).toInt(), history);

    QVERIFY(QDir().rename(hidden, extra));
    QVERIFY(scan(roots, added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);

    QVERIFY(QDir(extra).removeRecursively());
    QVERIFY(QDir().mkpath(extra));
    QVERIFY(scan(roots, added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 1);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks")).toInt(), 7);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM history")).toInt(), history - 1);
    QCOMPARE(m_library->counts().value(QStringLiteral("albums")).toInt(), 3);
}

void LibraryTest::artContent()
{
    const QSqlDatabase db = m_library->database();
    const QString source = firstTrackOf(QStringLiteral("Album One"));
    QVERIFY(!source.isEmpty());
    const QString albumDir = m_music + QStringLiteral("/Album Two");
    const QString copy = albumDir + QStringLiteral("/01 Copy.flac");
    QVERIFY(QDir().mkpath(albumDir));
    QVERIFY(QFile::copy(source, copy));
    QVERIFY(retagAlbum(copy, QStringLiteral("Album Two")));
    QVERIFY(bumpMtime(copy, 3600));

    const QString coverPath = m_music + QStringLiteral("/アルバム二/cover.png");
    QFile coverFile(coverPath);
    QVERIFY(coverFile.open(QIODevice::ReadOnly));
    const QByteArray cover = coverFile.readAll();
    coverFile.close();
    QVERIFY(!cover.isEmpty());

    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 1);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);
    QCOMPARE(m_library->counts().value(QStringLiteral("albums")).toInt(), 4);

    const QString one = artPathOf(QStringLiteral("Album One"));
    const QString two = artPathOf(QStringLiteral("Album Two"));
    QVERIFY(!one.isEmpty());
    QCOMPARE(two, one);
    QVERIFY(QFileInfo::exists(one));
    QCOMPARE(QFileInfo(one).fileName(), sha1Name(cover));
    QCOMPARE(QDir(m_library->artDir()).entryList(QDir::Files).size(), 1);

    QByteArray altered = cover;
    altered.append(QByteArrayLiteral("microdap"));
    QVERIFY(replaceCover(copy, altered));
    QVERIFY(bumpMtime(copy, 7200));
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 1);
    QCOMPARE(removed, 0);

    const QString replaced = artPathOf(QStringLiteral("Album Two"));
    QVERIFY(replaced != one);
    QCOMPARE(QFileInfo(replaced).fileName(), sha1Name(altered));
    QVERIFY(QFileInfo::exists(replaced));
    QVERIFY(QFileInfo::exists(one));
    QCOMPARE(artPathOf(QStringLiteral("Album One")), one);
    QCOMPARE(QDir(m_library->artDir()).entryList(QDir::Files).size(), 2);

    QVERIFY(QDir(albumDir).removeRecursively());
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 1);
    QCOMPARE(m_library->counts().value(QStringLiteral("albums")).toInt(), 3);
    QVERIFY(!QFileInfo::exists(replaced));
    QVERIFY(QFileInfo::exists(one));
    QCOMPARE(QDir(m_library->artDir()).entryList(QDir::Files).size(), 1);

    QVERIFY(QFile::remove(coverPath));
    QVERIFY(bumpMtime(firstTrackOf(QStringLiteral("アルバム二")), 3600));
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 1);
    QCOMPARE(removed, 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM albums WHERE title = ? AND art_path IS NULL"),
                    {QStringLiteral("アルバム二")})
                 .toInt(),
             1);
}

void LibraryTest::pruneArtForeign()
{
    const QString artDir = m_library->artDir();
    QVERIFY(QDir(artDir).exists());
    const QString kept = artPathOf(QStringLiteral("Album One"));
    QVERIFY(QFileInfo::exists(kept));

    const QString foreign = artDir + QStringLiteral("/notes.txt");
    const QString lookalike = artDir + QStringLiteral("/cover.png");
    const QString shortHash = artDir + QStringLiteral("/") + QString(39, QLatin1Char('a'))
                              + QStringLiteral(".png");
    const QString upper = artDir + QStringLiteral("/") + QString(40, QLatin1Char('A'))
                          + QStringLiteral(".png");
    const QString stalePng = artDir + QStringLiteral("/") + QString(40, QLatin1Char('b'))
                             + QStringLiteral(".png");
    const QString staleJpg = artDir + QStringLiteral("/") + QString(40, QLatin1Char('c'))
                             + QStringLiteral(".jpg");
    QVERIFY(writeBytes(foreign, QByteArrayLiteral("notes")));
    QVERIFY(writeBytes(lookalike, QByteArrayLiteral("png")));
    QVERIFY(writeBytes(shortHash, QByteArrayLiteral("png")));
    QVERIFY(writeBytes(upper, QByteArrayLiteral("png")));
    QVERIFY(writeBytes(stalePng, QByteArrayLiteral("png")));
    QVERIFY(writeBytes(staleJpg, QByteArrayLiteral("jpg")));

    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);
    QVERIFY(QFileInfo::exists(stalePng));
    QVERIFY(QFileInfo::exists(staleJpg));

    QVERIFY(bumpMtime(firstTrackOf(QStringLiteral("Album One")), 10800));
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 1);
    QCOMPARE(removed, 0);

    QVERIFY(QFileInfo::exists(kept));
    QVERIFY(QFileInfo::exists(foreign));
    QVERIFY(QFileInfo::exists(lookalike));
    QVERIFY(QFileInfo::exists(shortHash));
    QVERIFY(QFileInfo::exists(upper));
    QVERIFY(!QFileInfo::exists(stalePng));
    QVERIFY(!QFileInfo::exists(staleJpg));

    QVERIFY(QFile::remove(foreign));
    QVERIFY(QFile::remove(lookalike));
    QVERIFY(QFile::remove(shortHash));
    QVERIFY(QFile::remove(upper));
}

void LibraryTest::pins()
{
    const QSqlDatabase db = m_library->database();
    const qint64 albumOne = scalar(db, QStringLiteral("SELECT id FROM albums WHERE title = ?"),
                                   {QStringLiteral("Album One")})
                                .toLongLong();
    const qint64 artistA = scalar(db, QStringLiteral("SELECT id FROM artists WHERE name = ?"),
                                  {QStringLiteral("Artist A")})
                               .toLongLong();
    const qint64 two = scalar(db, QStringLiteral("SELECT id FROM tracks WHERE title = ?"),
                              {QStringLiteral("Two")})
                           .toLongLong();
    QVERIFY(albumOne > 0);
    QVERIFY(artistA > 0);
    QVERIFY(two > 0);
    {
        QSqlQuery insert(db);
        insert.prepare(QStringLiteral("INSERT INTO playlists(name) VALUES(?)"));
        insert.addBindValue(QStringLiteral("Mix"));
        QVERIFY(insert.exec());
    }
    const qint64 mix = scalar(db, QStringLiteral("SELECT id FROM playlists WHERE name = ?"),
                              {QStringLiteral("Mix")})
                           .toLongLong();
    QVERIFY(mix > 0);
    {
        QSqlQuery insert(db);
        insert.prepare(QStringLiteral("INSERT INTO playlist_items(playlist_id, pos, track_id) VALUES(?, 0, ?)"));
        insert.addBindValue(mix);
        insert.addBindValue(two);
        QVERIFY(insert.exec());
    }

    PinsModel model(m_library.get());
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.roleNames().value(LibraryModel::KindRole) == QByteArrayLiteral("kind"));
    QVERIFY(model.roleNames().value(LibraryModel::TitleRole) == QByteArrayLiteral("title"));

    QSignalSpy spy(m_library.get(), &Library::pinsChanged);
    QVERIFY(!m_library->isPinned(QStringLiteral("album"), albumOne));
    QVERIFY(m_library->pin(QStringLiteral("album"), albumOne));
    QVERIFY(m_library->isPinned(QStringLiteral("album"), albumOne));
    QVERIFY(!m_library->pin(QStringLiteral("album"), albumOne));
    QVERIFY(m_library->pin(QStringLiteral("artist"), artistA));
    QVERIFY(m_library->pin(QStringLiteral("song"), two));
    QVERIFY(m_library->pin(QStringLiteral("playlist"), mix));
    QVERIFY(!m_library->pin(QStringLiteral("genre"), 1));
    QCOMPARE(spy.count(), 4);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM pins")).toInt(), 4);
    QCOMPARE(scalar(db, QStringLiteral("SELECT MAX(pos) FROM pins")).toInt(), 3);

    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.data(model.index(0), LibraryModel::KindRole).toString(), QStringLiteral("album"));
    QCOMPARE(model.data(model.index(0), LibraryModel::IdRole).toLongLong(), albumOne);
    QCOMPARE(model.data(model.index(0), LibraryModel::TitleRole).toString(), QStringLiteral("Album One"));
    QCOMPARE(model.data(model.index(0), LibraryModel::SubtitleRole).toString(),
             scalar(db, QStringLiteral("SELECT artist FROM albums WHERE id = ?"), {albumOne}).toString());
    QVERIFY(!model.data(model.index(0), LibraryModel::ArtPathRole).toString().isEmpty());
    QCOMPARE(model.data(model.index(0), LibraryModel::SectionRole).toString(), QStringLiteral("pins"));
    QCOMPARE(model.data(model.index(1), LibraryModel::KindRole).toString(), QStringLiteral("artist"));
    QCOMPARE(model.data(model.index(1), LibraryModel::TitleRole).toString(), QStringLiteral("Artist A"));
    QCOMPARE(model.data(model.index(1), LibraryModel::SubtitleRole).toString(), QStringLiteral("1 album"));
    QVERIFY(!model.data(model.index(1), LibraryModel::ArtPathRole).toString().isEmpty());
    QCOMPARE(model.data(model.index(2), LibraryModel::KindRole).toString(), QStringLiteral("song"));
    QCOMPARE(model.data(model.index(2), LibraryModel::TitleRole).toString(), QStringLiteral("Two"));
    QCOMPARE(model.data(model.index(2), LibraryModel::SubtitleRole).toString(),
             scalar(db, QStringLiteral("SELECT artist FROM tracks WHERE id = ?"), {two}).toString());
    QCOMPARE(model.data(model.index(2), LibraryModel::ArtPathRole).toString(),
             artPathOf(QStringLiteral("Album One")));
    QCOMPARE(model.data(model.index(3), LibraryModel::KindRole).toString(), QStringLiteral("playlist"));
    QCOMPARE(model.data(model.index(3), LibraryModel::TitleRole).toString(), QStringLiteral("Mix"));
    QCOMPARE(model.data(model.index(3), LibraryModel::SubtitleRole).toString(), QStringLiteral("1 song"));
    QVERIFY(model.data(model.index(3), LibraryModel::ArtPathRole).toString().isEmpty());
    const QVariantMap first = model.get(0);
    QCOMPARE(first.value(QStringLiteral("kind")).toString(), QStringLiteral("album"));
    QCOMPARE(first.value(QStringLiteral("id")).toLongLong(), albumOne);

    QVERIFY(m_library->unpin(QStringLiteral("album"), albumOne));
    QVERIFY(!m_library->unpin(QStringLiteral("album"), albumOne));
    QVERIFY(!m_library->isPinned(QStringLiteral("album"), albumOne));
    QCOMPARE(spy.count(), 5);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(0), LibraryModel::KindRole).toString(), QStringLiteral("artist"));

    QVERIFY(m_library->pin(QStringLiteral("album"), albumOne));
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.data(model.index(3), LibraryModel::KindRole).toString(), QStringLiteral("album"));
    QCOMPARE(model.data(model.index(3), LibraryModel::IdRole).toLongLong(), albumOne);
}

void LibraryTest::pinsPrune()
{
    const QSqlDatabase db = m_library->database();
    const QString source = firstTrackOf(QStringLiteral("Album One"));
    QVERIFY(!source.isEmpty());
    const QString albumDir = m_music + QStringLiteral("/Pinned");
    const QString copy = albumDir + QStringLiteral("/01 Pinned.flac");
    QVERIFY(QDir().mkpath(albumDir));
    QVERIFY(QFile::copy(source, copy));
    QVERIFY(retagAlbum(copy, QStringLiteral("Pinned")));
    QVERIFY(bumpMtime(copy, 3600));

    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 1);
    const qint64 pinned = scalar(db, QStringLiteral("SELECT id FROM albums WHERE title = ?"),
                                 {QStringLiteral("Pinned")})
                              .toLongLong();
    QVERIFY(pinned > 0);

    PinsModel model(m_library.get());
    const int before = model.rowCount();
    QVERIFY(m_library->pin(QStringLiteral("album"), pinned));
    QCOMPARE(model.rowCount(), before + 1);

    QSignalSpy spy(m_library.get(), &Library::pinsChanged);
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(removed, 0);
    QCOMPARE(spy.count(), 0);
    QVERIFY(m_library->isPinned(QStringLiteral("album"), pinned));

    QVERIFY(QDir(albumDir).removeRecursively());
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(removed, 1);
    QCOMPARE(spy.count(), 1);
    QVERIFY(!m_library->isPinned(QStringLiteral("album"), pinned));
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM pins WHERE kind = ? AND ref_id = ?"),
                    {QStringLiteral("album"), pinned})
                 .toInt(),
             0);
    QCOMPARE(model.rowCount(), before);
}

void LibraryTest::rateAndDepth()
{
    const QSqlDatabase db = m_library->database();
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE sample_rate = 44100")).toInt(),
             scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks")).toInt());
    QVERIFY(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE path LIKE '%.flac' AND bit_depth = 8")).toInt() > 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE path LIKE '%.flac' AND bit_depth != 8")).toInt(), 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE path LIKE '%.mp3' AND bit_depth != 0")).toInt(), 0);
    const qint64 id = scalar(db, QStringLiteral("SELECT id FROM tracks WHERE path LIKE '%.flac' LIMIT 1")).toLongLong();
    const QVariantList rows = m_library->trackRows({id});
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().toMap().value(QStringLiteral("sampleRate")).toInt(), 44100);
    QCOMPARE(rows.first().toMap().value(QStringLiteral("bitDepth")).toInt(), 8);
}

void LibraryTest::batchedRows()
{
    const QList<qint64> ids = column(m_library->database(), QStringLiteral("SELECT id FROM tracks ORDER BY id"));
    QVERIFY(ids.size() >= 3);
    const QVariantList wanted{ids.at(2), ids.at(0), -5, ids.at(2), ids.at(1)};
    const QVariantList rows = m_library->trackRows(wanted);
    QCOMPARE(rows.size(), 4);
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("id")).toLongLong(), ids.at(2));
    QCOMPARE(rows.at(1).toMap().value(QStringLiteral("id")).toLongLong(), ids.at(0));
    QCOMPARE(rows.at(2).toMap().value(QStringLiteral("id")).toLongLong(), ids.at(2));
    QCOMPARE(rows.at(3).toMap().value(QStringLiteral("id")).toLongLong(), ids.at(1));

    const QList<QueueEntry> entries = m_library->queueEntries(wanted);
    QCOMPARE(idsOf(entries), (QList<qint64>{ids.at(2), ids.at(0), ids.at(2), ids.at(1)}));
    QVERIFY(!entries.first().path.isEmpty());
    QCOMPARE(entries.first().path,
             scalar(m_library->database(), QStringLiteral("SELECT path FROM tracks WHERE id = ?"), {ids.at(2)})
                 .toString());

    QVariantList many;
    for (int i = 0; i < Library::idChunk * 2 + 7; ++i)
        many.append(ids.at(i % ids.size()));
    QCOMPARE(m_library->queueEntries(many).size(), many.size());
    QCOMPARE(m_library->trackRows(many).size(), many.size());
    QCOMPARE(m_library->trackRows({}).size(), 0);
}

void LibraryTest::entriesFor()
{
    const QSqlDatabase db = m_library->database();
    const qint64 album = scalar(db, QStringLiteral("SELECT id FROM albums WHERE title = ?"),
                                {QStringLiteral("Album One")})
                             .toLongLong();
    const qint64 artist = scalar(db, QStringLiteral("SELECT id FROM artists WHERE name = ?"),
                                 {QStringLiteral("Artist A")})
                              .toLongLong();
    const qint64 genre = scalar(db, QStringLiteral("SELECT id FROM genres WHERE name = ?"),
                                {QStringLiteral("Electronic")})
                             .toLongLong();
    QVERIFY(album > 0);
    QVERIFY(artist > 0);
    QVERIFY(genre > 0);

    QList<qint64> albumIds;
    for (const QVariant &row : m_library->albumTracks(album))
        albumIds.append(row.toMap().value(QStringLiteral("id")).toLongLong());
    QVERIFY(!albumIds.isEmpty());
    QCOMPARE(idsOf(m_library->entriesFor(QStringLiteral("album"), album)), albumIds);
    const QString art = artPathOf(QStringLiteral("Album One"));
    QVERIFY(!art.isEmpty());
    for (const QueueEntry &entry : m_library->entriesFor(QStringLiteral("album"), album))
        QCOMPARE(entry.artPath, art);
    QCOMPARE(m_library->entriesFor(QStringLiteral("song"), albumIds.first()).first().artPath, art);
    QCOMPARE(m_library->queueEntries(QVariantList{QVariant(albumIds.first())}).first().artPath, art);
    QCOMPARE(idsOf(m_library->entriesFor(QStringLiteral("artist"), artist)),
             idsOf(m_library->artistTracks(artist)));
    QCOMPARE(idsOf(m_library->entriesFor(QStringLiteral("genre"), genre)), idsOf(m_library->genreTracks(genre)));
    QCOMPARE(idsOf(m_library->entriesFor(QStringLiteral("all"), -1)), idsOf(m_library->allTracks()));
    QCOMPARE(idsOf(m_library->entriesFor(QStringLiteral("song"), albumIds.first())),
             QList<qint64>{albumIds.first()});
    QVERIFY(m_library->entriesFor(QStringLiteral("unknown"), album).isEmpty());
    QVERIFY(m_library->entriesFor(QStringLiteral("album"), -1).isEmpty());

    const QVariantList albumRows = m_library->artistAlbumRows(artist);
    const QVariantList albumIdsOfArtist = m_library->artistAlbums(artist);
    QCOMPARE(albumRows.size(), albumIdsOfArtist.size());
    for (qsizetype i = 0; i < albumRows.size(); ++i) {
        const QVariantMap info = m_library->albumInfo(albumIdsOfArtist.at(i).toLongLong());
        QCOMPARE(albumRows.at(i).toMap(), info);
    }
    QCOMPARE(m_library->artistTrackRows(artist), m_library->trackRows(m_library->artistTracks(artist)));
    QVERIFY(m_library->artistAlbumRows(-1).isEmpty());

    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("INSERT INTO playlists(name, source) VALUES('Entries', '/entries.m3u')")));
    const qint64 playlist = query.lastInsertId().toLongLong();
    QVERIFY(playlist > 0);
    const QList<qint64> order{albumIds.last(), albumIds.first(), albumIds.last()};
    for (qsizetype pos = 0; pos < order.size(); ++pos) {
        QSqlQuery item(db);
        item.prepare(QStringLiteral("INSERT INTO playlist_items(playlist_id, pos, track_id) VALUES(?, ?, ?)"));
        item.addBindValue(playlist);
        item.addBindValue(static_cast<int>(pos));
        item.addBindValue(order.at(pos));
        QVERIFY(item.exec());
    }
    QCOMPARE(idsOf(m_library->entriesFor(QStringLiteral("playlist"), playlist)), order);
    QCOMPARE(idsOf(m_library->entriesFor(QStringLiteral("playlist"), playlist)),
             idsOf(m_library->playlistTracks(playlist)));
    QVERIFY(query.exec(QStringLiteral("DELETE FROM playlists WHERE source = '/entries.m3u'")));
}

void LibraryTest::migrateFrom2()
{
    const QString path = m_dir.path() + QStringLiteral("/migrate2.db");
    QVERIFY(QFile::copy(m_library->dbPath(), path));
    const QString setup = QStringLiteral("migrate2-setup");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), setup);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(dropVersion6(query));
        QVERIFY(query.exec(QStringLiteral("ALTER TABLE tracks DROP COLUMN sample_rate")));
        QVERIFY(query.exec(QStringLiteral("ALTER TABLE tracks DROP COLUMN bit_depth")));
        QVERIFY(dropPlaylistColumns(query));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO playlists(name) VALUES('Old')")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO playlist_items(playlist_id, pos, track_id) "
                                          "SELECT id, 0, (SELECT MIN(id) FROM tracks) FROM playlists "
                                          "WHERE name = 'Old'")));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 2")));
        query.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(setup);

    Library library(path, m_dir.path() + QStringLiteral("/art"));
    QVERIFY(library.isOpen());
    const QSqlDatabase db = library.database();
    QCOMPARE(scalar(db, QStringLiteral("PRAGMA user_version")).toInt(), 7);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM playlists WHERE name = 'Old'")).toInt(), 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM playlist_items "
                                       "WHERE playlist_id NOT IN (SELECT id FROM playlists)"))
                 .toInt(),
             0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM pragma_table_info('playlists') "
                                       "WHERE name IN ('source', 'skipped')"))
                 .toInt(),
             2);
    const int total = scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks")).toInt();
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE mtime = -1")).toInt(), total);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE sample_rate IS NOT NULL")).toInt(), 0);

    QSignalSpy spy(&library, &Library::scanned);
    library.rescan({m_music});
    QVERIFY(spy.wait(scanTimeoutMs));
    QCOMPARE(spy.takeFirst().at(1).toInt(), total);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE sample_rate = 44100")).toInt(), total);

    QSignalSpy again(&library, &Library::scanned);
    library.rescan({m_music});
    QVERIFY(again.wait(scanTimeoutMs));
    const QList<QVariant> args = again.takeFirst();
    QCOMPARE(args.at(0).toInt() + args.at(1).toInt() + args.at(2).toInt(), 0);
}

void LibraryTest::playlists()
{
    QSqlDatabase db = m_library->database();
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("DELETE FROM playlist_items")));
    QVERIFY(query.exec(QStringLiteral("DELETE FROM playlists")));
    QCOMPARE(m_library->playlistCount(), 0);
    PlaylistsModel model(m_library.get());
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(query.exec(QStringLiteral("INSERT INTO playlists(id, name) VALUES (1, 'Morning')")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO playlists(id, name) VALUES (2, 'Drive')")));
    const QList<QVariant> ids = [&] {
        QList<QVariant> out;
        QSqlQuery q(QStringLiteral("SELECT id FROM tracks ORDER BY id LIMIT 3"), db);
        while (q.next())
            out.append(q.value(0));
        return out;
    }();
    QCOMPARE(ids.size(), 3);
    for (int i = 0; i < 3; i++) {
        QSqlQuery item(db);
        item.prepare(QStringLiteral("INSERT INTO playlist_items(playlist_id, track_id, pos) VALUES (1, ?, ?)"));
        item.addBindValue(ids.at(2 - i));
        item.addBindValue(i);
        QVERIFY(item.exec());
    }
    emit m_library->playlistsChanged();
    emit m_library->playlistCountChanged();
    QCOMPARE(m_library->playlistCount(), 2);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), model.roleNames().key("title")).toString(), QStringLiteral("Drive"));
    QCOMPARE(model.data(model.index(1), model.roleNames().key("title")).toString(), QStringLiteral("Morning"));
    QCOMPARE(model.data(model.index(1), model.roleNames().key("subtitle")).toString(), QStringLiteral("3 songs"));
    QCOMPARE(model.data(model.index(0), model.roleNames().key("subtitle")).toString(), QStringLiteral("0 songs"));
    QCOMPARE(model.data(model.index(0), model.roleNames().key("kind")).toString(), QStringLiteral("playlist"));
    const QVariantList tracks = m_library->playlistTracks(1);
    QCOMPARE(tracks.size(), 3);
    QCOMPARE(tracks.at(0).toLongLong(), ids.at(2).toLongLong());
    QCOMPARE(tracks.at(2).toLongLong(), ids.at(0).toLongLong());
    QCOMPARE(m_library->playlistTracks(9).size(), 0);
}

void LibraryTest::genreTracks()
{
    const QSqlDatabase db = m_library->database();
    SongsModel songs(m_library.get());
    songs.ensureLoaded();
    QVariantList all;
    for (int row = 0; row < songs.rowCount(); ++row)
        all.append(songs.data(songs.index(row), LibraryModel::IdRole).toLongLong());
    QVERIFY(!all.isEmpty());
    QCOMPARE(m_library->allTracks(), all);
    QCOMPARE(m_library->songCount(), static_cast<int>(all.size()));

    GenresModel genres(m_library.get());
    genres.ensureLoaded();
    QCOMPARE(genres.rowCount(), 3);
    for (int row = 0; row < genres.rowCount(); ++row) {
        const qint64 genreId = genres.data(genres.index(row), LibraryModel::IdRole).toLongLong();
        const QString name = genres.data(genres.index(row), LibraryModel::TitleRole).toString();
        QVariantList expected;
        for (const QVariant &id : std::as_const(all)) {
            if (scalar(db, QStringLiteral("SELECT genre FROM tracks WHERE id = ?"), {id}).toString() == name)
                expected.append(id);
        }
        QVERIFY(!expected.isEmpty());
        QCOMPARE(m_library->genreTracks(genreId), expected);
    }
    QCOMPARE(m_library->genreTracks(-1).size(), 0);
}

void LibraryTest::folderOff()
{
    const QSqlDatabase db = m_library->database();
    const QString other = m_dir.path() + QStringLiteral("/other");
    QVERIFY(copyTree(QStringLiteral(MICRODAP_FIXTURE_MUSIC) + QStringLiteral("/Single"),
                     other + QStringLiteral("/Single")));
    const int before = m_library->songCount();

    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan({m_music, other}, added, updated, removed));
    QCOMPARE(added, 1);
    QCOMPARE(removed, 0);
    QCOMPARE(m_library->songCount(), before + 1);

    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 1);
    QCOMPARE(m_library->songCount(), before);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE path LIKE ?"),
                    {other + QStringLiteral("/%")})
                 .toInt(),
             0);
    QVERIFY(QDir(other).removeRecursively());
}

void LibraryTest::mediaUnmounted()
{
    const QSqlDatabase db = m_library->database();
    const QString base = m_dir.path() + QStringLiteral("/media");
    const QString label = base + QStringLiteral("/micro/CARD");
    const QString path = label + QStringLiteral("/Single/01 Single.mp3");
    QVERIFY(copyTree(QStringLiteral(MICRODAP_FIXTURE_MUSIC) + QStringLiteral("/Single"),
                     label + QStringLiteral("/Single")));
    qputenv("MICRODAP_MEDIA_ROOT", QFile::encodeName(base));
    const QStringList roots{m_music, base};
    const int before = m_library->songCount();

    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(roots, added, updated, removed));
    QCOMPARE(added, 1);
    QVERIFY(trackId(db, path) > 0);

    QVERIFY(QFile::remove(path));
    QVERIFY(scan(roots, added, updated, removed));
    QCOMPARE(removed, 0);

    QVERIFY(QDir(label).removeRecursively());
    QVERIFY(scan(roots, added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);
    QVERIFY(trackId(db, path) > 0);
    QCOMPARE(m_library->songCount(), before + 1);

    qunsetenv("MICRODAP_MEDIA_ROOT");
    QVERIFY(scan(roots, added, updated, removed));
    QCOMPARE(removed, 1);
    QCOMPARE(m_library->songCount(), before);
    QVERIFY(QDir(base).removeRecursively());
}

void LibraryTest::mediaMountLayout()
{
    const QSqlDatabase db = m_library->database();
    const QString base = m_dir.path() + QStringLiteral("/devmedia");
    const QString card = base + QStringLiteral("/mmcblk0p1");
    const QString music = card + QStringLiteral("/Music");
    const QString path = music + QStringLiteral("/Single/01 Single.mp3");
    const QString single = QStringLiteral(MICRODAP_FIXTURE_MUSIC) + QStringLiteral("/Single");
    QVERIFY(copyTree(single, music + QStringLiteral("/Single")));
    qputenv("MICRODAP_MEDIA_ROOT", QFile::encodeName(base));
    Scanner::setMountCheck(fakeMountCheck);
    fakeMount = card;
    const QStringList roots{m_music, base};
    const int before = m_library->songCount();

    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(roots, added, updated, removed));
    QCOMPARE(added, 1);
    QVERIFY(trackId(db, path) > 0);

    QVERIFY(QFile::remove(path));
    QVERIFY(scan(roots, added, updated, removed));
    QCOMPARE(removed, 1);
    QCOMPARE(trackId(db, path), 0);

    QVERIFY(copyTree(single, music + QStringLiteral("/Single")));
    QVERIFY(scan(roots, added, updated, removed));
    QCOMPARE(added, 1);

    fakeMount.clear();
    QVERIFY(QDir(music).removeRecursively());
    QVERIFY(scan(roots, added, updated, removed));
    QCOMPARE(removed, 0);
    QVERIFY(trackId(db, path) > 0);

    QVERIFY(QDir(card).removeRecursively());
    QVERIFY(scan(roots, added, updated, removed));
    QCOMPARE(removed, 0);
    QCOMPARE(m_library->songCount(), before + 1);

    fakeMount = card;
    QVERIFY(QDir().mkpath(card));
    QVERIFY(scan(roots, added, updated, removed));
    QCOMPARE(removed, 1);
    QCOMPARE(m_library->songCount(), before);

    fakeMount.clear();
    Scanner::setMountCheck(nullptr);
    qunsetenv("MICRODAP_MEDIA_ROOT");
    QVERIFY(QDir(base).removeRecursively());
}

void LibraryTest::candidateRoots()
{
    const QString base = m_dir.path() + QStringLiteral("/candidates");
    const QString card = base + QStringLiteral("/mmcblk0p1");
    const QString user = base + QStringLiteral("/micro");
    const QString label = user + QStringLiteral("/CARD");
    const QString empty = base + QStringLiteral("/nobody");
    QVERIFY(QDir().mkpath(card + QStringLiteral("/Music")));
    QVERIFY(QDir().mkpath(label));
    QVERIFY(QDir().mkpath(user + QStringLiteral("/stale")));
    QVERIFY(QDir().mkpath(empty + QStringLiteral("/old")));
    qputenv("MICRODAP_MEDIA_ROOT", QFile::encodeName(base));
    Scanner::setMountCheck([](const QString &dir) {
        const QString path = QDir::cleanPath(dir);
        return path.endsWith(QLatin1StringView("/mmcblk0p1")) || path.endsWith(QLatin1StringView("/CARD"));
    });

    QVERIFY(QDir().mkpath(label + QStringLiteral("/MUSIC")));
    const QStringList roots = Settings::candidateRoots();
    QCOMPARE(roots, (QStringList{QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
                                 label + QStringLiteral("/MUSIC"), card + QStringLiteral("/Music")}));
    QCOMPARE(roots, Settings::defaultRoots());

    Scanner::setMountCheck(nullptr);
    qunsetenv("MICRODAP_MEDIA_ROOT");
    QVERIFY(QDir(base).removeRecursively());
}

void LibraryTest::playlistFiles()
{
    QSqlDatabase db = m_library->database();
    {
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral("DELETE FROM playlists WHERE source IS NULL")));
    }
    const QString lists = m_music + QStringLiteral("/Lists");
    QVERIFY(QDir().mkpath(lists));
    const QString one = m_music + QStringLiteral("/アルバム二/01 一.flac");
    const QString ni = m_music + QStringLiteral("/アルバム二/02 二.flac");
    const QString two = m_music + QStringLiteral("/Album One/02 Two.flac");
    const QString three = m_music + QStringLiteral("/Album One/03 Three.flac");
    const QString single = m_music + QStringLiteral("/Single/01 Single.mp3");
    const QString cafe = lists + QStringLiteral("/Caf") + QChar(0x00E9) + QStringLiteral(".flac");
    QVERIFY(QFileInfo::exists(two));
    QVERIFY(QFile::copy(two, cafe));
    QVERIFY(QFile::copy(two, lists + QStringLiteral("/Dup.flac")));
    QVERIFY(QFile::copy(two, lists + QStringLiteral("/dup.flac")));

    const QString mix = QStringLiteral("#EXTM3U\r\n#EXTINF:2,アーティスト - 一\r\n../アルバム二/01 一.flac\r\n")
                        + two + QStringLiteral("\r\n# comment\r\n\r\n")
                        + QUrl::fromLocalFile(ni).toString(QUrl::FullyEncoded)
                        + QStringLiteral("\r\n..\\Single\\01 Single.mp3\r\n  nothere.flac  \r\n"
                                         "../アルバム二/01 一.flac\r\n");
    QVERIFY(writeBytes(lists + QStringLiteral("/Mix.m3u8"), QByteArrayLiteral("\xEF\xBB\xBF") + mix.toUtf8()));
    QVERIFY(writeBytes(lists + QStringLiteral("/Latin.m3u"),
                       QByteArrayLiteral("#EXTM3U\nCaf\xe9.flac\n../album one/03 three.flac\nDUP.flac\n")));

    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 3);
    QCOMPARE(removed, 0);

    const qint64 mixId = playlistId(db, QStringLiteral("Mix"));
    const qint64 latinId = playlistId(db, QStringLiteral("Latin"));
    QVERIFY(mixId > 0);
    QVERIFY(latinId > 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT source FROM playlists WHERE id = ?"), {mixId}).toString(),
             lists + QStringLiteral("/Mix.m3u8"));
    QCOMPARE(m_library->playlistTracks(mixId),
             (QVariantList{trackId(db, one), trackId(db, two), trackId(db, ni), trackId(db, single),
                           trackId(db, one)}));
    QCOMPARE(m_library->playlistTracks(latinId), (QVariantList{trackId(db, cafe), trackId(db, three)}));
    QCOMPARE(m_library->playlistInfo(mixId).value(QStringLiteral("skipped")).toInt(), 1);
    QCOMPARE(m_library->playlistInfo(latinId).value(QStringLiteral("skipped")).toInt(), 1);
    QCOMPARE(m_library->playlistInfo(mixId).value(QStringLiteral("count")).toInt(), 5);
    QCOMPARE(m_library->playlistCount(), 2);

    PlaylistsModel model(m_library.get());
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), LibraryModel::TitleRole).toString(), QStringLiteral("Latin"));
    QCOMPARE(model.data(model.index(0), LibraryModel::SubtitleRole).toString(), QStringLiteral("2 songs"));
    QCOMPARE(model.data(model.index(1), LibraryModel::TitleRole).toString(), QStringLiteral("Mix"));
    QCOMPARE(model.data(model.index(1), LibraryModel::SubtitleRole).toString(), QStringLiteral("5 songs"));
}

void LibraryTest::playlistStamps()
{
    QSqlDatabase db = m_library->database();
    const QString lists = m_music + QStringLiteral("/Lists");
    const QString mixPath = lists + QStringLiteral("/Mix.m3u8");
    const qint64 mixId = playlistId(db, QStringLiteral("Mix"));
    QVERIFY(mixId > 0);
    const FileStamp stamp = Scanner::stampOf(QFileInfo(mixPath));
    QCOMPARE(scalar(db, QStringLiteral("SELECT mtime FROM playlists WHERE id = ?"), {mixId}).toLongLong(),
             stamp.mtime);
    QCOMPARE(scalar(db, QStringLiteral("SELECT size FROM playlists WHERE id = ?"), {mixId}).toLongLong(),
             stamp.size);
    const QVariantList full = m_library->playlistTracks(mixId);
    QCOMPARE(full.size(), 5);

    {
        QSqlQuery query(db);
        query.prepare(QStringLiteral("DELETE FROM playlist_items WHERE playlist_id = ? AND pos = 4"));
        query.addBindValue(mixId);
        QVERIFY(query.exec());
    }
    QSignalSpy playlistsSpy(m_library.get(), &Library::playlistsChanged);
    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added + updated + removed, 0);
    QCOMPARE(playlistsSpy.count(), 0);
    QCOMPARE(m_library->playlistTracks(mixId).size(), 4);

    const QString extra = lists + QStringLiteral("/Extra.mp3");
    QVERIFY(QFile::copy(m_music + QStringLiteral("/Single/01 Single.mp3"), extra));
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 1);
    QCOMPARE(playlistsSpy.count(), 1);
    QCOMPARE(m_library->playlistTracks(mixId), full);
    QCOMPARE(playlistId(db, QStringLiteral("Mix")), mixId);

    QVERIFY(QFile::remove(extra));
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(removed, 1);
    QCOMPARE(playlistsSpy.count(), 1);
    QCOMPARE(m_library->playlistTracks(mixId), full);
}

void LibraryTest::playlistUnreadable()
{
    QSqlDatabase db = m_library->database();
    const QString latinPath = m_music + QStringLiteral("/Lists/Latin.m3u");
    const qint64 latinId = playlistId(db, QStringLiteral("Latin"));
    QVERIFY(latinId > 0);
    const QVariantList before = m_library->playlistTracks(latinId);
    QCOMPARE(before.size(), 2);
    const QFileDevice::Permissions permissions = QFile::permissions(latinPath);

    QVERIFY(bumpMtime(latinPath, 3600));
    QVERIFY(QFile::setPermissions(latinPath, QFileDevice::Permissions()));
    {
        QFile probe(latinPath);
        if (probe.open(QIODevice::ReadOnly)) {
            probe.close();
            QVERIFY(QFile::setPermissions(latinPath, permissions));
            QSKIP("mode 000 files stay readable for this user");
        }
    }
    QSignalSpy playlistsSpy(m_library.get(), &Library::playlistsChanged);
    QTest::ignoreMessage(QtWarningMsg,
                         qPrintable(QStringLiteral("microdap: playlist %1: unreadable, kept as is").arg(latinPath)));
    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added + updated + removed, 0);
    QCOMPARE(playlistsSpy.count(), 0);
    QCOMPARE(playlistId(db, QStringLiteral("Latin")), latinId);
    QCOMPARE(m_library->playlistTracks(latinId), before);
    QCOMPARE(m_library->playlistCount(), 2);

    QVERIFY(QFile::setPermissions(latinPath, permissions));
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(playlistsSpy.count(), 0);
    QCOMPARE(m_library->playlistTracks(latinId), before);
    QCOMPARE(scalar(db, QStringLiteral("SELECT mtime FROM playlists WHERE id = ?"), {latinId}).toLongLong(),
             Scanner::stampOf(QFileInfo(latinPath)).mtime);
}

void LibraryTest::playlistStable()
{
    const QSqlDatabase db = m_library->database();
    const QString lists = m_music + QStringLiteral("/Lists");
    const QString two = m_music + QStringLiteral("/Album One/02 Two.flac");
    const QString single = m_music + QStringLiteral("/Single/01 Single.mp3");
    const qint64 mixId = playlistId(db, QStringLiteral("Mix"));
    QVERIFY(mixId > 0);
    QVERIFY(m_library->pin(QStringLiteral("playlist"), mixId));

    QVERIFY(writeBytes(lists + QStringLiteral("/Mix.m3u8"),
                       (QStringLiteral("../Single/01 Single.mp3\n") + two + QStringLiteral("\n")).toUtf8()));
    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added + updated + removed, 0);
    QCOMPARE(playlistId(db, QStringLiteral("Mix")), mixId);
    QVERIFY(m_library->isPinned(QStringLiteral("playlist"), mixId));
    QCOMPARE(m_library->playlistTracks(mixId), (QVariantList{trackId(db, single), trackId(db, two)}));
    QCOMPARE(m_library->playlistInfo(mixId).value(QStringLiteral("skipped")).toInt(), 0);

    QSignalSpy pinsSpy(m_library.get(), &Library::pinsChanged);
    QVERIFY(QFile::remove(lists + QStringLiteral("/Mix.m3u8")));
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM playlists WHERE id = ?"), {mixId}).toInt(), 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM playlist_items WHERE playlist_id = ?"), {mixId})
                 .toInt(),
             0);
    QVERIFY(!m_library->isPinned(QStringLiteral("playlist"), mixId));
    QCOMPARE(pinsSpy.count(), 1);
    QCOMPARE(m_library->playlistCount(), 1);

    QVERIFY(QDir(lists).removeRecursively());
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(removed, 3);
    QCOMPARE(m_library->playlistCount(), 0);
}

void LibraryTest::migrateFrom3()
{
    const QString path = m_dir.path() + QStringLiteral("/migrate3.db");
    QVERIFY(QFile::copy(m_library->dbPath(), path));
    const QString setup = QStringLiteral("migrate3-setup");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), setup);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(dropVersion6(query));
        QVERIFY(dropPlaylistColumns(query));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO playlists(name) VALUES('Old')")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO pins(kind, ref_id, pos) "
                                          "SELECT 'playlist', id, 99 FROM playlists WHERE name = 'Old'")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO playlist_items(playlist_id, pos, track_id) "
                                          "SELECT id, 0, (SELECT MIN(id) FROM tracks) FROM playlists "
                                          "WHERE name = 'Old'")));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 3")));
        query.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(setup);

    Library library(path, m_dir.path() + QStringLiteral("/art"));
    QVERIFY(library.isOpen());
    const QSqlDatabase db = library.database();
    QCOMPARE(scalar(db, QStringLiteral("PRAGMA user_version")).toInt(), 7);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM playlists")).toInt(), 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM pins WHERE kind = 'playlist' AND pos = 99")).toInt(), 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM playlist_items")).toInt(), 0);
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("INSERT INTO playlists(name, source) VALUES('A', '/a.m3u')")));
    QVERIFY(!query.exec(QStringLiteral("INSERT INTO playlists(name, source) VALUES('B', '/a.m3u')")));
    QCOMPARE(scalar(db, QStringLiteral("SELECT skipped FROM playlists WHERE name = 'A'")).toInt(), 0);
}

void LibraryTest::migrateFrom4()
{
    const QString path = m_dir.path() + QStringLiteral("/migrate4.db");
    QVERIFY(QFile::copy(m_library->dbPath(), path));
    const QString setup = QStringLiteral("migrate4-setup");
    int playlists = 0;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), setup);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(dropVersion6(query));
        QVERIFY(dropPlaylistStamps(query));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO playlists(name, source) VALUES('Kept', '/kept.m3u')")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO playlist_items(playlist_id, pos, track_id) "
                                          "SELECT id, 0, (SELECT MIN(id) FROM tracks) FROM playlists "
                                          "WHERE name = 'Kept'")));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 4")));
        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM playlists")) && query.next());
        playlists = query.value(0).toInt();
        query.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(setup);

    Library library(path, m_dir.path() + QStringLiteral("/art"));
    QVERIFY(library.isOpen());
    const QSqlDatabase db = library.database();
    QCOMPARE(scalar(db, QStringLiteral("PRAGMA user_version")).toInt(), 7);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM playlists")).toInt(), playlists);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM playlists WHERE mtime = -1 AND size = -1")).toInt(),
             playlists);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' "
                                       "AND name = 'playlist_items_track'"))
                 .toInt(),
             1);
    QCOMPARE(library.playlistStamps().size(), playlists);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM playlist_items i JOIN playlists p "
                                       "ON p.id = i.playlist_id WHERE p.name = 'Kept'"))
                 .toInt(),
             1);
}

void LibraryTest::rootsChange()
{
    m_settings = std::make_unique<Settings>(nullptr);
    m_settings->setRoots({m_music});
    m_library->setSettings(m_settings.get());
    const QString other = m_dir.path() + QStringLiteral("/roots");
    QVERIFY(copyTree(QStringLiteral(MICRODAP_FIXTURE_MUSIC) + QStringLiteral("/Single"),
                     other + QStringLiteral("/Single")));
    const int before = m_library->songCount();

    QSignalSpy spy(m_library.get(), &Library::scanned);
    m_settings->setRoots({m_music, other});
    QVERIFY(spy.wait(scanTimeoutMs));
    QCOMPARE(spy.takeFirst().at(0).toInt(), 1);
    QCOMPARE(m_library->songCount(), before + 1);
    QCOMPARE(m_library->scannedRoots(), (QStringList{m_music, other}));

    m_settings->setRoots({m_music});
    QVERIFY(spy.wait(scanTimeoutMs));
    QCOMPARE(spy.takeFirst().at(2).toInt(), 1);
    QCOMPARE(m_library->songCount(), before);
    QCOMPARE(m_library->scannedRoots(), QStringList{m_music});
    QVERIFY(QDir(other).removeRecursively());
}

void LibraryTest::watcherDebounce()
{
    const QSqlDatabase db = m_library->database();
    LibraryWatcher watcher(m_library.get());
    QCOMPARE(watcher.debounceMs(), 5000);
    QCOMPARE(watcher.limit(), 4096);
    QVERIFY(scanRoots());
    const QString album = m_music + QStringLiteral("/Album One");
    QVERIFY(watcher.watched().contains(m_music));
    QVERIFY(watcher.watched().contains(album));

    QSignalSpy triggered(&watcher, &LibraryWatcher::triggered);
    QSignalSpy scanned(m_library.get(), &Library::scanned);
    QElapsedTimer sinceLast;
    for (int i = 0; i < 19; ++i) {
        QVERIFY(writeBytes(album + QStringLiteral("/note%1.txt").arg(i), QByteArrayLiteral("x")));
        sinceLast.start();
        QTest::qWait(100);
    }
    const QString copy = album + QStringLiteral("/05 Burst.flac");
    QVERIFY(QFile::copy(album + QStringLiteral("/02 Two.flac"), copy));
    QCOMPARE(triggered.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(triggered.count(), 1, 10000);
    QVERIFY(sinceLast.elapsed() >= 4700);
    QTRY_COMPARE_WITH_TIMEOUT(scanned.count(), 1, scanTimeoutMs);
    QVERIFY(trackId(db, copy) > 0);
    QCOMPARE(triggered.count(), 1);

    for (int i = 0; i < 19; ++i)
        QVERIFY(QFile::remove(album + QStringLiteral("/note%1.txt").arg(i)));
    QVERIFY(QFile::remove(copy));
    QVERIFY(scanRoots());
    QCOMPARE(trackId(db, copy), 0);
}

void LibraryTest::watcherNewDirectory()
{
    LibraryWatcher watcher(m_library.get());
    watcher.setDebounceMs(600000);
    const QString fresh = m_music + QStringLiteral("/Fresh");
    QVERIFY(QDir().mkpath(fresh));
    QVERIFY(scanRoots());
    QVERIFY(watcher.watched().contains(fresh));

    QVERIFY(QDir(fresh).removeRecursively());
    QVERIFY(scanRoots());
    QVERIFY(!watcher.watched().contains(fresh));
    QVERIFY(watcher.watched().contains(m_music));
}

void LibraryTest::watcherLimit()
{
    LibraryWatcher watcher(m_library.get());
    watcher.setDebounceMs(600000);
    watcher.setLimit(8);
    const QString deep = m_music + QStringLiteral("/Deep");
    for (int i = 0; i < 10; ++i)
        QVERIFY(QDir().mkpath(deep + QStringLiteral("/d%1").arg(i)));

    QTest::ignoreMessage(QtWarningMsg, "microdap: watch limit 8 reached");
    QVERIFY(scanRoots());
    QCOMPARE(watcher.watched().size(), 8);
    QVERIFY(watcher.watched().contains(m_music));

    QTest::failOnWarning(QRegularExpression(QStringLiteral("watch limit")));
    QVERIFY(scanRoots());
    QCOMPARE(watcher.watched().size(), 8);

    QVERIFY(QDir(deep).removeRecursively());
    QVERIFY(scanRoots());
    QVERIFY(watcher.watched().size() < 8);
}

void LibraryTest::watcherMissingRoot()
{
    LibraryWatcher watcher(m_library.get());
    watcher.setDebounceMs(500);
    const QString mnt = m_dir.path() + QStringLiteral("/mnt");
    const QString card = mnt + QStringLiteral("/card/Music");
    QVERIFY(QDir().mkpath(mnt));
    QCOMPARE(LibraryWatcher::nearestExisting(card), mnt);

    QSignalSpy spy(m_library.get(), &Library::scanned);
    m_settings->setRoots({m_music, card});
    QVERIFY(spy.wait(scanTimeoutMs));
    QVERIFY(watcher.watched().contains(mnt));
    const int before = m_library->songCount();

    QVERIFY(copyTree(QStringLiteral(MICRODAP_FIXTURE_MUSIC) + QStringLiteral("/Single"),
                     card + QStringLiteral("/Single")));
    QTRY_COMPARE_WITH_TIMEOUT(m_library->songCount(), before + 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(watcher.watched().contains(card), 10000);
    QVERIFY(!watcher.watched().contains(mnt));

    m_settings->setRoots({m_music});
    QTRY_VERIFY_WITH_TIMEOUT(!m_library->scanning() && m_library->songCount() == before, scanTimeoutMs);
    QVERIFY(QDir(mnt).removeRecursively());
}

void LibraryTest::watcherClimbStops()
{
    const QString base = m_dir.path() + QStringLiteral("/climb");
    const QString root = base + QStringLiteral("/mmcblk0p1/Music");
    qputenv("MICRODAP_MEDIA_ROOT", QFile::encodeName(base));
    QCOMPARE(LibraryWatcher::nearestExisting(root), QString());
    QVERIFY(QDir().mkpath(base));
    QCOMPARE(LibraryWatcher::nearestExisting(root), base);
    QVERIFY(QDir().mkpath(base + QStringLiteral("/mmcblk0p1")));
    QCOMPARE(LibraryWatcher::nearestExisting(root), base + QStringLiteral("/mmcblk0p1"));
    qunsetenv("MICRODAP_MEDIA_ROOT");
    QVERIFY(QDir(base).removeRecursively());
}

void LibraryTest::watcherPoll()
{
    const QString base = m_dir.path() + QStringLiteral("/pollmedia");
    const QString root = base + QStringLiteral("/mmcblk0p1/Music");
    qputenv("MICRODAP_MEDIA_ROOT", QFile::encodeName(base));
    Scanner::setMountCheck(fakeMountCheck);
    fakeMount = base + QStringLiteral("/mmcblk0p1");
    LibraryWatcher watcher(m_library.get());
    watcher.setDebounceMs(600000);
    watcher.setPollMs(200);
    QCOMPARE(LibraryWatcher::defaultPollMs, 5000);

    QSignalSpy spy(m_library.get(), &Library::scanned);
    m_settings->setRoots({m_music, root});
    QVERIFY(spy.wait(scanTimeoutMs));
    QCOMPARE(watcher.missing(), QStringList{root});
    QVERIFY(!watcher.watched().contains(base));
    const int before = m_library->songCount();

    QSignalSpy triggered(&watcher, &LibraryWatcher::triggered);
    const QString stage = m_dir.path() + QStringLiteral("/pollstage");
    QVERIFY(copyTree(QStringLiteral(MICRODAP_FIXTURE_MUSIC) + QStringLiteral("/Single"),
                     stage + QStringLiteral("/mmcblk0p1/Music/Single")));
    QVERIFY(QDir().rename(stage, base));
    QTRY_VERIFY_WITH_TIMEOUT(triggered.count() >= 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(m_library->songCount(), before + 1, scanTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(watcher.missing().isEmpty(), scanTimeoutMs);

    m_settings->setRoots({m_music});
    QTRY_VERIFY_WITH_TIMEOUT(!m_library->scanning() && m_library->songCount() == before, scanTimeoutMs);
    fakeMount.clear();
    Scanner::setMountCheck(nullptr);
    qunsetenv("MICRODAP_MEDIA_ROOT");
    QVERIFY(QDir(base).removeRecursively());
}

void LibraryTest::mediaDefaultRoots()
{
    const QString base = m_dir.path() + QStringLiteral("/defaults");
    const QString card = base + QStringLiteral("/mmcblk0p1");
    const QString user = base + QStringLiteral("/micro");
    const QString fat = user + QStringLiteral("/CARD");
    const QString old = user + QStringLiteral("/old");
    const QString linked = user + QStringLiteral("/linked");
    const QString target = m_dir.path() + QStringLiteral("/defaults-target");
    for (const QString &dir : {card + QStringLiteral("/Music"), card + QStringLiteral("/roms/music"),
                               card + QStringLiteral("/roms/gba"), card + QStringLiteral("/steamapps"),
                               fat + QStringLiteral("/MUSIC"), fat + QStringLiteral("/ROMS/MUSIC"),
                               old + QStringLiteral("/Music"), linked, target + QStringLiteral("/music"),
                               base + QStringLiteral("/loose/Music")})
        QVERIFY(QDir().mkpath(dir));
    QVERIFY(QFile::link(target + QStringLiteral("/music"), linked + QStringLiteral("/Music")));
    QVERIFY(QFile::link(target, linked + QStringLiteral("/roms")));
    QVERIFY(QFile::link(card, user + QStringLiteral("/alias")));
    qputenv("MICRODAP_MEDIA_ROOT", QFile::encodeName(base));
    Scanner::setMountCheck([](const QString &dir) {
        const QString path = QDir::cleanPath(dir);
        return path.endsWith(QLatin1StringView("/mmcblk0p1")) || path.endsWith(QLatin1StringView("/CARD"));
    });

    const QString home = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    QStringList roots = Settings::defaultRoots();
    QCOMPARE(roots.first(), home);
    roots.sort();
    QStringList expected{home,
                         card + QStringLiteral("/Music"),
                         card + QStringLiteral("/roms/music"),
                         fat + QStringLiteral("/MUSIC"),
                         fat + QStringLiteral("/ROMS/MUSIC"),
                         old + QStringLiteral("/Music")};
    expected.sort();
    QCOMPARE(roots, expected);

    Settings settings(nullptr);
    QCOMPARE(settings.roots(), Settings::defaultRoots());
    settings.setRoots({m_music});
    QCOMPARE(settings.roots(), QStringList{m_music});
    settings.setRoots({});
    QVERIFY(settings.rootsDefault());
    QCOMPARE(settings.value(QStringLiteral("microdap.roots")).toStringList(), QStringList());

    Scanner::setMountCheck(nullptr);
    qunsetenv("MICRODAP_MEDIA_ROOT");
    QVERIFY(QDir(base).removeRecursively());
    QVERIFY(QDir(target).removeRecursively());
}

void LibraryTest::mediaKeepsUserData()
{
    QSqlDatabase db = m_library->database();
    const QString base = m_dir.path() + QStringLiteral("/keepmedia");
    const QString card = base + QStringLiteral("/micro/CARD");
    const QString music = card + QStringLiteral("/Music");
    const QString path = music + QStringLiteral("/Single/01 Single.mp3");
    const QString stray = card + QStringLiteral("/steamapps/01 Single.mp3");
    const QString list = music + QStringLiteral("/Card.m3u");
    const QString single = QStringLiteral(MICRODAP_FIXTURE_MUSIC) + QStringLiteral("/Single");
    QVERIFY(copyTree(single, music + QStringLiteral("/Single")));
    QVERIFY(QDir().mkpath(QFileInfo(stray).absolutePath()));
    QVERIFY(QFile::copy(single + QStringLiteral("/01 Single.mp3"), stray));
    QVERIFY(writeBytes(list, QByteArrayLiteral("Single/01 Single.mp3\n")));
    qputenv("MICRODAP_MEDIA_ROOT", QFile::encodeName(base));
    Scanner::setMountCheck(fakeMountCheck);
    fakeMount = card;
    const int before = m_library->songCount();

    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan({m_music, card}, added, updated, removed));
    QCOMPARE(added, 2);
    QVERIFY(trackId(db, stray) > 0);

    QVERIFY(scan({m_music, music}, added, updated, removed));
    QCOMPARE(removed, 1);
    QCOMPARE(trackId(db, stray), 0);
    const qint64 track = trackId(db, path);
    QVERIFY(track > 0);
    const qint64 playlist = playlistId(db, QStringLiteral("Card"));
    QVERIFY(playlist > 0);
    m_library->setRating(track, 1);
    m_library->addHistory(track);
    QVERIFY(m_library->pin(QStringLiteral("playlist"), playlist));
    QVERIFY(m_library->pin(QStringLiteral("song"), track));
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM playlist_items WHERE playlist_id = ? AND track_id = ?"),
                    {playlist, track})
                 .toInt(),
             1);

    fakeMount.clear();
    QVERIFY(QDir(card).removeRecursively());
    Scanner scanner(m_dir.path() + QStringLiteral("/art"));
    const ScanResult result = scanner.scan({m_music}, m_library->snapshot(), nullptr, m_library->playlistStamps());
    QVERIFY(result.complete);
    QVERIFY(!result.removed.contains(path));
    QVERIFY(!result.removedPlaylists.contains(list));

    QVERIFY(scan({m_music}, added, updated, removed));
    QCOMPARE(removed, 0);
    QCOMPARE(trackId(db, path), track);
    QCOMPARE(m_library->rating(track), 1);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM history WHERE track_id = ?"), {track}).toInt(), 1);
    QCOMPARE(playlistId(db, QStringLiteral("Card")), playlist);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM playlist_items WHERE playlist_id = ? AND track_id = ?"),
                    {playlist, track})
                 .toInt(),
             1);
    QVERIFY(m_library->isPinned(QStringLiteral("playlist"), playlist));
    QVERIFY(m_library->isPinned(QStringLiteral("song"), track));
    QCOMPARE(m_library->songCount(), before + 1);

    Scanner::setMountCheck(nullptr);
    qunsetenv("MICRODAP_MEDIA_ROOT");
    QVERIFY(scan({m_music}, added, updated, removed));
    QCOMPARE(removed, 1);
    QCOMPARE(m_library->songCount(), before);
    QCOMPARE(playlistId(db, QStringLiteral("Card")), 0);
    QVERIFY(QDir(base).removeRecursively());
}

void LibraryTest::watcherMedia()
{
    const QString base = m_dir.path() + QStringLiteral("/watchmedia");
    const QString user = base + QStringLiteral("/micro");
    const QString card = user + QStringLiteral("/CARD");
    const QString stage = m_dir.path() + QStringLiteral("/watchstage");
    const QString profile = card + QStringLiteral("/Music/EQ/HD600 ParametricEQ.txt");
    QVERIFY(QDir().mkpath(user));
    qputenv("MICRODAP_MEDIA_ROOT", QFile::encodeName(base));
    Scanner::setMountCheck(fakeMountCheck);
    Settings settings(nullptr);
    Library library(m_dir.path() + QStringLiteral("/watchmedia.db"), m_dir.path() + QStringLiteral("/watchart"));
    QVERIFY(library.isOpen());
    library.setSettings(&settings);
    EqControl eq(&settings);
    LibraryWatcher watcher(&library);
    watcher.setDebounceMs(200);
    QSignalSpy scanned(&library, &Library::scanned);
    library.rescanRoots();
    QVERIFY(scanned.wait(scanTimeoutMs));
    QVERIFY(watcher.watched().contains(base));
    QVERIFY(watcher.watched().contains(user));
    QCOMPARE(library.songCount(), 0);
    const auto listed = [&eq, &profile] {
        for (const QVariant &source : eq.sources()) {
            if (source.toMap().value(QStringLiteral("path")).toString() == profile)
                return true;
        }
        return false;
    };
    QVERIFY(!listed());

    QVERIFY(copyTree(QStringLiteral(MICRODAP_FIXTURE_MUSIC) + QStringLiteral("/Single"),
                     stage + QStringLiteral("/Music/Single")));
    QVERIFY(QDir().mkpath(stage + QStringLiteral("/Roms")));
    QVERIFY(QDir().mkpath(stage + QStringLiteral("/Music/EQ")));
    QVERIFY(writeBytes(stage + QStringLiteral("/Music/EQ/HD600 ParametricEQ.txt"), QByteArray(autoEq)));
    fakeMount = card;
    QVERIFY(QDir().rename(stage, card));
    QTRY_COMPARE_WITH_TIMEOUT(library.songCount(), 1, scanTimeoutMs);
    QVERIFY(listed());
    QVERIFY(library.scannedRoots().contains(card + QStringLiteral("/Music")));
    QTRY_VERIFY_WITH_TIMEOUT(watcher.watched().contains(card + QStringLiteral("/Music/Single")), scanTimeoutMs);
    QVERIFY(watcher.watched().contains(card));
    QVERIFY(watcher.watched().contains(card + QStringLiteral("/Roms")));

    QVERIFY(copyTree(QStringLiteral(MICRODAP_FIXTURE_MUSIC) + QStringLiteral("/Single"),
                     stage + QStringLiteral("/Single")));
    QVERIFY(QDir().rename(stage, card + QStringLiteral("/Roms/MUSIC")));
    QTRY_COMPARE_WITH_TIMEOUT(library.songCount(), 2, scanTimeoutMs);
    QVERIFY(library.scannedRoots().contains(card + QStringLiteral("/Roms/MUSIC")));

    fakeMount.clear();
    QVERIFY(QDir(card).removeRecursively());
    QTRY_VERIFY_WITH_TIMEOUT(!listed(), scanTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(!library.scanning() && !library.scannedRoots().contains(card + QStringLiteral("/Music")),
                             scanTimeoutMs);
    QCOMPARE(library.songCount(), 2);

    Scanner::setMountCheck(nullptr);
    qunsetenv("MICRODAP_MEDIA_ROOT");
    QVERIFY(QDir(base).removeRecursively());
}

void LibraryTest::watcherMediaLimit()
{
    const QString base = m_dir.path() + QStringLiteral("/limitmedia");
    const QString card = base + QStringLiteral("/mmcblk0p1");
    const QString stage = m_dir.path() + QStringLiteral("/limitstage");
    QVERIFY(QDir().mkpath(base + QStringLiteral("/micro")));
    qputenv("MICRODAP_MEDIA_ROOT", QFile::encodeName(base));
    Scanner::setMountCheck(fakeMountCheck);
    Settings settings(nullptr);
    Library library(m_dir.path() + QStringLiteral("/limitmedia.db"), m_dir.path() + QStringLiteral("/limitart"));
    QVERIFY(library.isOpen());
    library.setSettings(&settings);
    LibraryWatcher watcher(&library);
    watcher.setDebounceMs(200);
    watcher.setLimit(1);
    QSignalSpy scanned(&library, &Library::scanned);
    library.rescanRoots();
    QVERIFY(scanned.wait(scanTimeoutMs));
    QCOMPARE(watcher.watched(), QStringList{base});

    QVERIFY(copyTree(QStringLiteral(MICRODAP_FIXTURE_MUSIC) + QStringLiteral("/Single"),
                     stage + QStringLiteral("/music/Single")));
    fakeMount = card;
    QVERIFY(QDir().rename(stage, card));
    QTRY_COMPARE_WITH_TIMEOUT(library.songCount(), 1, scanTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(!library.scanning(), scanTimeoutMs);
    QCOMPARE(watcher.watched(), QStringList{base});

    fakeMount.clear();
    Scanner::setMountCheck(nullptr);
    qunsetenv("MICRODAP_MEDIA_ROOT");
    QVERIFY(QDir(base).removeRecursively());
}

void LibraryTest::watcherMountOverDir()
{
    const QString base = m_dir.path() + QStringLiteral("/overmedia");
    const QString card = base + QStringLiteral("/micro/CARD");
    QVERIFY(QDir().mkpath(card));
    qputenv("MICRODAP_MEDIA_ROOT", QFile::encodeName(base));
    Scanner::setMountCheck(fakeMountCheck);
    Settings settings(nullptr);
    Library library(m_dir.path() + QStringLiteral("/overmedia.db"), m_dir.path() + QStringLiteral("/overart"));
    QVERIFY(library.isOpen());
    library.setSettings(&settings);
    LibraryWatcher watcher(&library);
    watcher.setDebounceMs(200);
    QSignalSpy scanned(&library, &Library::scanned);
    library.rescanRoots();
    QVERIFY(scanned.wait(scanTimeoutMs));
    QVERIFY(!watcher.watched().contains(card));

    QVERIFY(copyTree(QStringLiteral(MICRODAP_FIXTURE_MUSIC) + QStringLiteral("/Single"),
                     card + QStringLiteral("/Music/Single")));
    fakeMount = card;
    QTest::qWait(600);
    QCOMPARE(library.songCount(), 0);
    watcher.mountsChanged();
    QTRY_COMPARE_WITH_TIMEOUT(library.songCount(), 1, scanTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(!library.scanning() && watcher.watched().contains(card), scanTimeoutMs);

    settings.setRoots({m_music});
    QTRY_VERIFY_WITH_TIMEOUT(!library.scanning() && !watcher.watched().contains(base), scanTimeoutMs);
    QSignalSpy triggered(&watcher, &LibraryWatcher::triggered);
    watcher.mountsChanged();
    QTest::qWait(600);
    QCOMPARE(triggered.count(), 0);

    fakeMount.clear();
    Scanner::setMountCheck(nullptr);
    qunsetenv("MICRODAP_MEDIA_ROOT");
    QVERIFY(QDir(base).removeRecursively());
}

void LibraryTest::scanCardPulled()
{
    const QString base = m_dir.path() + QStringLiteral("/pullmedia");
    const QString card = base + QStringLiteral("/micro/CARD");
    const QString music = card + QStringLiteral("/Music");
    const QString path = music + QStringLiteral("/Single/01 Single.mp3");
    QVERIFY(copyTree(QStringLiteral(MICRODAP_FIXTURE_MUSIC) + QStringLiteral("/Single"),
                     music + QStringLiteral("/Single")));
    qputenv("MICRODAP_MEDIA_ROOT", QFile::encodeName(base));
    Scanner::setMountCheck(fakeMountCheck);
    fakeMount = card;
    Library library(m_dir.path() + QStringLiteral("/pullmedia.db"), m_dir.path() + QStringLiteral("/pullart"));
    QVERIFY(library.isOpen());
    QSignalSpy scanned(&library, &Library::scanned);
    library.rescan({music});
    QVERIFY(scanned.wait(scanTimeoutMs));
    QCOMPARE(library.songCount(), 1);

    pulledCard = card;
    pulledRoot = music;
    pulledChecks = 0;
    Scanner::setMountCheck(pullingMountCheck);
    Scanner scanner(m_dir.path() + QStringLiteral("/pullart"));
    const ScanResult result = scanner.scan({music}, library.snapshot());
    QVERIFY(result.complete);
    QVERIFY(pulledChecks >= 2);
    QVERIFY(!QFileInfo::exists(music));
    QVERIFY(result.removed.isEmpty());

    QVERIFY(copyTree(QStringLiteral(MICRODAP_FIXTURE_MUSIC) + QStringLiteral("/Single"),
                     music + QStringLiteral("/Single")));
    Scanner::setMountCheck(fakeMountCheck);
    QVERIFY(QFile::remove(path));
    const ScanResult gone = scanner.scan({music}, library.snapshot());
    QCOMPARE(gone.removed, QStringList{path});

    fakeMount.clear();
    pulledCard.clear();
    pulledRoot.clear();
    Scanner::setMountCheck(nullptr);
    qunsetenv("MICRODAP_MEDIA_ROOT");
    QVERIFY(QDir(base).removeRecursively());
}

void LibraryTest::scanCardRecheck()
{
    const QString base = m_dir.path() + QStringLiteral("/recheckmedia");
    const QString card = base + QStringLiteral("/micro/CARD");
    const QString music = card + QStringLiteral("/Music");
    const QString path = music + QStringLiteral("/Single/01 Single.mp3");
    const QString card0 = fakeDevice;
    const QString other = m_dir.path() + QStringLiteral("/dev-other");
    QVERIFY(writeBytes(other, QByteArray()));
    QVERIFY(copyTree(QStringLiteral(MICRODAP_FIXTURE_MUSIC) + QStringLiteral("/Single"),
                     music + QStringLiteral("/Single")));
    qputenv("MICRODAP_MEDIA_ROOT", QFile::encodeName(base));
    Scanner::setMountCheck(fakeMountCheck);
    fakeMount = card;
    Library library(m_dir.path() + QStringLiteral("/recheckmedia.db"), m_dir.path() + QStringLiteral("/recheckart"));
    QVERIFY(library.isOpen());
    QSignalSpy scanned(&library, &Library::scanned);
    library.rescan({music});
    QVERIFY(scanned.wait(scanTimeoutMs));
    QCOMPARE(library.songCount(), 1);
    const QHash<QString, FileStamp> snapshot = library.snapshot();
    QVERIFY(QFile::remove(path));
    Scanner scanner(m_dir.path() + QStringLiteral("/recheckart"));

    deviceFlip = [] { QFile::remove(fakeDevice); };
    QVERIFY(scanner.scan({music}, snapshot).removed.isEmpty());
    QVERIFY(!deviceFlip);
    QVERIFY(writeBytes(fakeDevice, QByteArray()));

    deviceFlip = [] { fakeMount.clear(); };
    QVERIFY(scanner.scan({music}, snapshot).removed.isEmpty());
    fakeMount = card;

    deviceFlip = [] { fakeDevice = QDir::cleanPath(fakeDevice + QStringLiteral("/../dev-other")); };
    QVERIFY(scanner.scan({music}, snapshot).removed.isEmpty());
    QCOMPARE(fakeDevice, other);
    fakeDevice = card0;

    QCOMPARE(scanner.scan({music}, snapshot).removed, QStringList{path});

    fakeMount.clear();
    Scanner::setMountCheck(nullptr);
    qunsetenv("MICRODAP_MEDIA_ROOT");
    QVERIFY(QFile::remove(other));
    QVERIFY(QDir(base).removeRecursively());
}

void LibraryTest::mediaMountsFilter()
{
    const QByteArray root = "36 25 0:32 / / rw - ext4 /dev/sda22 rw\n";
    const QByteArray card = "120 36 179:1 / /run/media/micro/e8ea rw,nosuid - ext4 /dev/mmcblk0p1 rw\n";
    const QByteArray spaced = "121 36 179:2 / /run/media/micro/My\\040Card rw - vfat /dev/mmcblk0p2 rw\n";
    const QByteArray user = "122 36 0:50 / /run/user/1000 rw - tmpfs tmpfs rw\n";
    const QByteArray near = "123 36 0:51 / /run/mediax rw - tmpfs tmpfs rw\n";
    const QStringList mounts = LibraryWatcher::mediaMounts(root + card + spaced + user + near);
    QCOMPARE(mounts, (QStringList{QString::fromLatin1(card).trimmed(), QString::fromLatin1(spaced).trimmed()}));
    QCOMPARE(LibraryWatcher::mediaMounts(root + card + spaced), mounts);
    QCOMPARE(LibraryWatcher::mediaMounts(root + user), QStringList());
    QVERIFY(LibraryWatcher::mediaMounts(root + card) != mounts);
}

void LibraryTest::migrateFrom5()
{
    const QString path = m_dir.path() + QStringLiteral("/migrate5.db");
    QVERIFY(makeVersion5(path));
    const QHash<QString, QStringList> before = dumpVersion5(path);
    QCOMPARE(before.value(QStringLiteral("history")).size(), 3);
    QCOMPARE(before.value(QStringLiteral("playlist_items")).size(), 2);
    QCOMPARE(before.value(QStringLiteral("track_artists")).size(), 3);
    QCOMPARE(before.value(QStringLiteral("pins")).size(), 4);

    {
        Library library(path, m_dir.path() + QStringLiteral("/art5"));
        QVERIFY(library.isOpen());
        const QSqlDatabase db = library.database();
        QCOMPARE(scalar(db, QStringLiteral("PRAGMA user_version")).toInt(), 7);
        QCOMPARE(scalar(db, QStringLiteral("SELECT value FROM meta WHERE key = 'min_reader'")).toInt(), 5);
        QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM pragma_table_info('tracks') WHERE name IN "
                                           "('rg_track_gain', 'rg_album_gain', 'rg_track_peak', 'rg_album_peak', "
                                           "'art_path', 'has_lyrics', 'rating')"))
                     .toInt(),
                 7);
        QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE mtime = -1")).toInt(), 2);
        QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE has_lyrics = 0 AND art_path IS NULL "
                                           "AND rg_track_gain IS NULL")).toInt(),
                 2);
        QCOMPARE(scalar(db, QStringLiteral("PRAGMA foreign_keys")).toInt(), 1);
        QCOMPARE(scalar(db, QStringLiteral("SELECT rating FROM tracks WHERE id = 1")).toInt(), 1);
    }

    const QHash<QString, QStringList> after = dumpVersion5(path);
    for (const char *table : version5Tables)
        QCOMPARE(after.value(QString::fromLatin1(table)), before.value(QString::fromLatin1(table)));
    QStringList tracks = before.value(QStringLiteral("tracks"));
    for (QString &row : tracks) {
        QStringList values = row.split(QLatin1Char('|'));
        values[2] = QStringLiteral("-1");
        row = values.join(QLatin1Char('|'));
    }
    QCOMPARE(after.value(QStringLiteral("tracks")), tracks);

    Library again(path, m_dir.path() + QStringLiteral("/art5"));
    QVERIFY(again.isOpen());
    QCOMPARE(scalar(again.database(), QStringLiteral("PRAGMA user_version")).toInt(), 7);
    QCOMPARE(scalar(again.database(), QStringLiteral("SELECT COUNT(*) FROM history")).toInt(), 3);
}

void LibraryTest::migrateFailureRollsBack()
{
    const QString clash = m_dir.path() + QStringLiteral("/clash5.db");
    QVERIFY(makeVersion5(clash, {QStringLiteral("ALTER TABLE tracks ADD COLUMN has_lyrics INTEGER")}));
    const QHash<QString, QStringList> clashBefore = dumpVersion5(clash);
    {
        Library library(clash, m_dir.path() + QStringLiteral("/art5"));
        QVERIFY(!library.isOpen());
    }
    QCOMPARE(dumpVersion5(clash), clashBefore);
    const QString name = QStringLiteral("clash-check");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(clash);
        QVERIFY(db.open());
        QCOMPARE(scalar(db, QStringLiteral("PRAGMA user_version")).toInt(), 5);
        QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM pragma_table_info('tracks') "
                                           "WHERE name = 'rg_track_gain'"))
                     .toInt(),
                 0);
        QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE name = 'meta'")).toInt(), 0);
        db.close();
    }
    QSqlDatabase::removeDatabase(name);

    const QString orphan = m_dir.path() + QStringLiteral("/orphan5.db");
    QVERIFY(makeVersion5(orphan, {QStringLiteral("INSERT INTO history(track_id, played_at) VALUES(99, 4000)"),
                                  QStringLiteral("INSERT INTO playlist_items(playlist_id, pos, track_id) "
                                                 "VALUES(99, 0, 1)"),
                                  QStringLiteral("INSERT INTO track_artists(track_id, artist_id, pos) "
                                                 "VALUES(99, 1, 0)")}));
    const QHash<QString, QStringList> orphanBefore = dumpVersion5(orphan);
    {
        Library library(orphan, m_dir.path() + QStringLiteral("/art5"));
        QVERIFY(library.isOpen());
        const QSqlDatabase db = library.database();
        QCOMPARE(scalar(db, QStringLiteral("PRAGMA user_version")).toInt(), 7);
        QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM history WHERE track_id = 99")).toInt(), 0);
        QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM playlist_items WHERE playlist_id = 99")).toInt(), 0);
        QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM track_artists WHERE track_id = 99")).toInt(), 0);
    }
    const QHash<QString, QStringList> orphanAfter = dumpVersion5(orphan);
    QCOMPARE(orphanAfter.value(QStringLiteral("history")).size(),
             orphanBefore.value(QStringLiteral("history")).size() - 1);
    QCOMPARE(orphanAfter.value(QStringLiteral("playlist_items")).size(),
             orphanBefore.value(QStringLiteral("playlist_items")).size() - 1);
    QCOMPARE(orphanAfter.value(QStringLiteral("track_artists")).size(),
             orphanBefore.value(QStringLiteral("track_artists")).size() - 1);
    QCOMPARE(orphanAfter.value(QStringLiteral("pins")), orphanBefore.value(QStringLiteral("pins")));

    const QString stray = m_dir.path() + QStringLiteral("/stray5.db");
    QVERIFY(makeVersion5(stray, {QStringLiteral("UPDATE tracks SET album_id = 99 WHERE id = 1")}));
    const QHash<QString, QStringList> strayBefore = dumpVersion5(stray);
    {
        Library library(stray, m_dir.path() + QStringLiteral("/art5"));
        QVERIFY(!library.isOpen());
    }
    QCOMPARE(dumpVersion5(stray), strayBefore);
    const QString strayName = QStringLiteral("stray-check");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), strayName);
        db.setDatabaseName(stray);
        QVERIFY(db.open());
        QCOMPARE(scalar(db, QStringLiteral("PRAGMA user_version")).toInt(), 5);
        QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM pragma_table_info('tracks') "
                                           "WHERE name = 'has_lyrics'"))
                     .toInt(),
                 0);
        db.close();
    }
    QSqlDatabase::removeDatabase(strayName);
}

void LibraryTest::newerReader()
{
    const QString path = m_dir.path() + QStringLiteral("/newer.db");
    QVERIFY(QFile::copy(m_library->dbPath(), path));
    QVERIFY(setPragma(path, {QStringLiteral("PRAGMA user_version = 8")}));
    const int songs = m_library->songCount();
    QVERIFY(songs > 0);
    {
        Library library(path, m_dir.path() + QStringLiteral("/art"));
        QVERIFY(library.isOpen());
        QCOMPARE(scalar(library.database(), QStringLiteral("PRAGMA user_version")).toInt(), 8);
        QCOMPARE(library.songCount(), songs);
    }

    QVERIFY(setPragma(path, {QStringLiteral("UPDATE meta SET value = 8 WHERE key = 'min_reader'")}));
    const QByteArray hash = fileHash(path);
    QVERIFY(!hash.isEmpty());
    {
        Library library(path, m_dir.path() + QStringLiteral("/art"));
        QVERIFY(!library.isOpen());
        QCOMPARE(library.songCount(), 0);
    }
    QCOMPARE(fileHash(path), hash);

    QVERIFY(setPragma(path, {QStringLiteral("DROP TABLE meta")}));
    const QByteArray bare = fileHash(path);
    {
        Library library(path, m_dir.path() + QStringLiteral("/art"));
        QVERIFY(!library.isOpen());
    }
    QCOMPARE(fileHash(path), bare);
}

void LibraryTest::trackTags()
{
    const QString music = m_dir.path() + QStringLiteral("/tagged");
    QVERIFY(QDir().mkpath(music));
    const QString source = firstTrackOf(QStringLiteral("Album One"));
    const QString a = music + QStringLiteral("/a.flac");
    const QString b = music + QStringLiteral("/b.flac");
    const QString c = music + QStringLiteral("/c.flac");
    for (const QString &path : {a, b, c}) {
        QVERIFY(QFile::copy(source, path));
        QVERIFY(retagAlbum(path, QStringLiteral("Tagged")));
    }
    QVERIFY(retag(a, "REPLAYGAIN_TRACK_GAIN", {QStringLiteral("  -6.50 dB")}));
    QVERIFY(retag(a, "REPLAYGAIN_ALBUM_GAIN", {QStringLiteral("-7,25 dB")}));
    QVERIFY(retag(a, "REPLAYGAIN_TRACK_PEAK", {QStringLiteral("0.988")}));
    QVERIFY(retag(a, "REPLAYGAIN_ALBUM_PEAK", {QStringLiteral("1.02")}));
    QVERIFY(retag(b, "REPLAYGAIN_TRACK_GAIN", {QStringLiteral("+60 dB")}));
    QVERIFY(retag(b, "REPLAYGAIN_ALBUM_GAIN", {QStringLiteral("loud")}));
    QVERIFY(retag(b, "REPLAYGAIN_TRACK_PEAK", {QStringLiteral("0")}));
    QVERIFY(retag(b, "R128_TRACK_GAIN", {QStringLiteral("-512")}));
    QVERIFY(retag(b, "LYRICS", {QStringLiteral("la la")}));
    QVERIFY(writeBytes(music + QStringLiteral("/a.LRC"), QByteArrayLiteral("[00:01.00]la\n")));

    QFile coverFile(QStringLiteral(MICRODAP_FIXTURE_MUSIC "/アルバム二/cover.png"));
    QVERIFY(coverFile.open(QIODevice::ReadOnly));
    QByteArray altered = coverFile.readAll();
    coverFile.close();
    altered.append(QByteArrayLiteral("track b"));
    QVERIFY(replaceCover(b, altered));
    TagLib::FLAC::File plain(QFile::encodeName(c).constData());
    QVERIFY(plain.isValid());
    plain.removePictures();
    QVERIFY(plain.save());

    const QString artDir = m_dir.path() + QStringLiteral("/tagged-art");
    Library library(m_dir.path() + QStringLiteral("/tagged.db"), artDir);
    QVERIFY(library.isOpen());
    const auto rescan = [&library, &music] {
        QSignalSpy spy(&library, &Library::scanned);
        library.rescan({music});
        return spy.wait(scanTimeoutMs) && spy.takeFirst().at(3).toBool();
    };
    QVERIFY(rescan());
    const QSqlDatabase db = library.database();
    const auto value = [&db](const QString &column, const QString &path) {
        return scalar(db, QStringLiteral("SELECT %1 FROM tracks WHERE path = ?").arg(column), {path});
    };
    QCOMPARE(value(QStringLiteral("rg_track_gain"), a).toDouble(), -6.5);
    QCOMPARE(value(QStringLiteral("rg_album_gain"), a).toDouble(), -7.25);
    QCOMPARE(value(QStringLiteral("rg_track_peak"), a).toDouble(), 0.988);
    QCOMPARE(value(QStringLiteral("rg_album_peak"), a).toDouble(), 1.02);
    QVERIFY(value(QStringLiteral("rg_track_gain"), b).isNull());
    QVERIFY(value(QStringLiteral("rg_album_gain"), b).isNull());
    QVERIFY(value(QStringLiteral("rg_track_peak"), b).isNull());
    QVERIFY(value(QStringLiteral("rg_album_peak"), b).isNull());
    QVERIFY(value(QStringLiteral("rg_track_gain"), c).isNull());
    QCOMPARE(value(QStringLiteral("has_lyrics"), a).toInt(), 1);
    QCOMPARE(value(QStringLiteral("has_lyrics"), b).toInt(), 1);
    QCOMPARE(value(QStringLiteral("has_lyrics"), c).toInt(), 0);

    const QString artA = value(QStringLiteral("art_path"), a).toString();
    const QString artB = value(QStringLiteral("art_path"), b).toString();
    QVERIFY(!artA.isEmpty());
    QVERIFY(artA.startsWith(artDir));
    QCOMPARE(QFileInfo(artB).fileName(), sha1Name(altered));
    QVERIFY(artA != artB);
    QVERIFY(value(QStringLiteral("art_path"), c).isNull());
    QCOMPARE(scalar(db, QStringLiteral("SELECT art_path FROM albums WHERE title = 'Tagged'")).toString(), artA);
    QVERIFY(QFileInfo::exists(artB));

    QVERIFY(bumpMtime(a, 3600));
    QVERIFY(rescan());
    QVERIFY(QFileInfo::exists(artA));
    QVERIFY(QFileInfo::exists(artB));
    QCOMPARE(value(QStringLiteral("art_path"), b).toString(), artB);
    QCOMPARE(QDir(artDir).entryList(QDir::Files).size(), 2);

    QVERIFY(QFile::remove(artB));
    QVERIFY(bumpMtime(a, 7200));
    QVERIFY(rescan());
    QVERIFY(value(QStringLiteral("art_path"), b).isNull());
    QCOMPARE(value(QStringLiteral("mtime"), b).toLongLong(), -1);
    QVERIFY(rescan());
    QCOMPARE(value(QStringLiteral("art_path"), b).toString(), artB);
    QVERIFY(QFileInfo::exists(artB));

    QVERIFY(QDir(artDir).removeRecursively());
    QVERIFY(bumpMtime(c, 3600));
    QVERIFY(rescan());
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE art_path IS NOT NULL")).toInt(), 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM albums WHERE art_path IS NOT NULL")).toInt(), 0);
    QCOMPARE(value(QStringLiteral("mtime"), a).toLongLong(), -1);
    QCOMPARE(value(QStringLiteral("mtime"), b).toLongLong(), -1);
    QVERIFY(rescan());
    QCOMPARE(value(QStringLiteral("art_path"), a).toString(), artA);
    QCOMPARE(value(QStringLiteral("art_path"), b).toString(), artB);
    QVERIFY(QFileInfo::exists(artA));
    QVERIFY(QFileInfo::exists(artB));
    QVERIFY(!scalar(db, QStringLiteral("SELECT art_path FROM albums WHERE title = 'Tagged'")).isNull());
}

void LibraryTest::migrateFrom6()
{
    const QString path = m_dir.path() + QStringLiteral("/migrate6.db");
    QVERIFY(QFile::copy(m_library->dbPath(), path));
    QVERIFY(setPragma(path, {QStringLiteral("DROP TABLE favorite_order"),
                             QStringLiteral("DELETE FROM meta WHERE key = 'favorite_seq'"),
                             QStringLiteral("PRAGMA user_version = 6")}));
    const QHash<QString, QStringList> before = dumpVersion5(path);
    {
        Library library(path, m_dir.path() + QStringLiteral("/art"));
        QVERIFY(library.isOpen());
        const QSqlDatabase db = library.database();
        QCOMPARE(scalar(db, QStringLiteral("PRAGMA user_version")).toInt(), 7);
        QCOMPARE(scalar(db, QStringLiteral("SELECT value FROM meta WHERE key = 'min_reader'")).toInt(), 5);
        QCOMPARE(scalar(db, QStringLiteral("SELECT value FROM meta WHERE key = 'favorite_seq'")).toInt(), 0);
        QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM favorite_order")).toInt(), 0);
        QVERIFY(library.stampFavorites(QStringLiteral("internal"), {{QStringLiteral("../a.flac"), 0},
                                                                    {QStringLiteral("../b.flac"), 0}}));
        QVERIFY(library.stampFavorites(QStringLiteral("CARD"), {{QStringLiteral("../c.flac"), 2}}));
        QVERIFY(library.stampFavorites(QStringLiteral("internal"), {{QStringLiteral("../a.flac"), 0}}));
        const QHash<QString, qint64> internal = library.favoriteStamps(QStringLiteral("internal"));
        QCOMPARE(internal.value(Library::favoriteKey(QStringLiteral("../a.flac"), 0)), 4);
        QCOMPARE(internal.value(Library::favoriteKey(QStringLiteral("../b.flac"), 0)), 2);
        const QString cKey = Library::favoriteKey(QStringLiteral("../c.flac"), 2);
        QCOMPARE(library.favoriteStamps(QStringLiteral("CARD")).value(cKey), 3);
        const QString aKey = Library::favoriteKey(QStringLiteral("../a.flac"), 0);
        QVERIFY(library.keepFavoriteStamps(QStringLiteral("internal"), {aKey}));
        QCOMPARE(library.favoriteStamps(QStringLiteral("internal")).size(), 1);
        QCOMPARE(library.favoriteStamps(QStringLiteral("CARD")).size(), 1);
        QVERIFY(library.dropFavoriteStamp(QStringLiteral("CARD"), QStringLiteral("../c.flac"), 2));
        QVERIFY(library.favoriteStamps(QStringLiteral("CARD")).isEmpty());
    }
    QCOMPARE(dumpVersion5(path), before);
}

void LibraryTest::newFormats()
{
    for (const char *suffix : {"ape", "APE", "dsf", "dff", "mp4", "oga"})
        QVERIFY(Scanner::isAudioFile(QStringLiteral("/m/a.") + QLatin1String(suffix)));

    const QString music = m_dir.path() + QStringLiteral("/formats");
    QVERIFY(copyTree(QStringLiteral(MICRODAP_FIXTURE_FORMATS), music));
    Library library(m_dir.path() + QStringLiteral("/formats.db"), m_dir.path() + QStringLiteral("/formats-art"));
    QVERIFY(library.isOpen());
    QSignalSpy spy(&library, &Library::scanned);
    library.rescan({music});
    QVERIFY(spy.wait(scanTimeoutMs));
    QCOMPARE(spy.takeFirst().at(0).toInt(), 4);
    const QSqlDatabase db = library.database();
    const auto row = [&db, &music](const QString &file) {
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT t.title, t.artist, a.title, t.track_no, t.sample_rate, t.bit_depth, "
                                     "t.duration_ms FROM tracks t JOIN albums a ON a.id = t.album_id "
                                     "WHERE t.path = ?"));
        query.addBindValue(music + QLatin1Char('/') + file);
        QStringList values;
        if (query.exec() && query.next()) {
            for (int i = 0; i < 6; ++i)
                values.append(query.value(i).toString());
            values.append(query.value(6).toLongLong() > 0 ? QStringLiteral("timed") : QStringLiteral("untimed"));
        }
        return values;
    };
    QCOMPARE(row(QStringLiteral("dsd64.dsf")),
             (QStringList{QStringLiteral("DSF"), QStringLiteral("Format Band"), QStringLiteral("Formats"),
                          QStringLiteral("1"), QStringLiteral("2822400"), QStringLiteral("1"),
                          QStringLiteral("timed")}));
    QCOMPARE(row(QStringLiteral("dsd64.dff")),
             (QStringList{QStringLiteral("DFF"), QStringLiteral("Format Band"), QStringLiteral("Formats"),
                          QStringLiteral("1"), QStringLiteral("2822400"), QStringLiteral("1"),
                          QStringLiteral("timed")}));
    const QStringList oga = row(QStringLiteral("vorbis.oga"));
    QCOMPARE(oga.mid(0, 5), (QStringList{QStringLiteral("OGA"), QStringLiteral("Format Band"),
                                         QStringLiteral("Formats"), QStringLiteral("1"), QStringLiteral("44100")}));
    const QStringList mp4 = row(QStringLiteral("aac.mp4"));
    QCOMPARE(mp4.mid(0, 5), (QStringList{QStringLiteral("MP4"), QStringLiteral("Format Band"),
                                         QStringLiteral("Formats"), QStringLiteral("1"), QStringLiteral("44100")}));
    QCOMPARE(mp4.last(), QStringLiteral("timed"));
}

void LibraryTest::trackArtFirst()
{
    const QString music = m_dir.path() + QStringLiteral("/tagged");
    Library library(m_dir.path() + QStringLiteral("/tagged.db"), m_dir.path() + QStringLiteral("/tagged-art"));
    QVERIFY(library.isOpen());
    const QSqlDatabase db = library.database();
    const auto trackArt = [&db](const QString &name) {
        return scalar(db, QStringLiteral("SELECT art_path FROM tracks WHERE path = ?"), {name}).toString();
    };
    const QString a = music + QStringLiteral("/a.flac");
    const QString b = music + QStringLiteral("/b.flac");
    const QString c = music + QStringLiteral("/c.flac");
    const QString artA = trackArt(a);
    const QString artB = trackArt(b);
    QVERIFY(!artA.isEmpty());
    QVERIFY(!artB.isEmpty());
    QVERIFY(artA != artB);
    QVERIFY(trackArt(c).isEmpty());

    const qint64 album = scalar(db, QStringLiteral("SELECT id FROM albums WHERE title = 'Tagged'")).toLongLong();
    QCOMPARE(library.albumInfo(album).value(QStringLiteral("artPath")).toString(), artA);
    QHash<QString, QString> art;
    for (const QueueEntry &entry : library.entriesFor(QStringLiteral("album"), album))
        art.insert(entry.path, entry.artPath);
    QCOMPARE(art.value(a), artA);
    QCOMPARE(art.value(b), artB);
    QCOMPARE(art.value(c), artA);
    const qint64 idB = trackId(db, b);
    QCOMPARE(library.queueEntries({idB}).value(0).artPath, artB);

    library.addHistory(idB);
    QVERIFY(library.pin(QStringLiteral("song"), idB));
    QVERIFY(library.pin(QStringLiteral("album"), album));
    HistoryModel recent(&library, HistoryModel::Recent, 8);
    recent.ensureLoaded();
    QCOMPARE(recent.rowCount(), 1);
    QCOMPARE(recent.data(recent.index(0), LibraryModel::ArtPathRole).toString(), artB);
    PinsModel pins(&library);
    QCOMPARE(pins.rowCount(), 2);
    QCOMPARE(pins.data(pins.index(0), LibraryModel::ArtPathRole).toString(), artB);
    QCOMPARE(pins.data(pins.index(1), LibraryModel::ArtPathRole).toString(), artA);
}

QTEST_GUILESS_MAIN(LibraryTest)
#include "tst_library.moc"
