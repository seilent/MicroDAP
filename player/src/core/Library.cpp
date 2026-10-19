#include "Library.h"
#include "Favorites.h"
#include "SectionKey.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <limits>
#include <optional>
#include <span>
#include <utility>

#include <taglib/fileref.h>
#include <taglib/mp4file.h>
#include <taglib/oggflacfile.h>
#include <taglib/opusfile.h>

namespace microdap {

namespace {

std::atomic_int connectionCounter{0};

constexpr int schemaVersion = 8;

constexpr const char *trackArtistsTable =
    "CREATE TABLE IF NOT EXISTS track_artists("
    "track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE, "
    "artist_id INTEGER NOT NULL REFERENCES artists(id) ON DELETE CASCADE, "
    "pos INTEGER NOT NULL, PRIMARY KEY(track_id, pos))";
constexpr const char *trackArtistsIndex =
    "CREATE INDEX IF NOT EXISTS track_artists_artist ON track_artists(artist_id)";
constexpr const char *addSampleRate = "ALTER TABLE tracks ADD COLUMN sample_rate INTEGER";
constexpr const char *addBitDepth = "ALTER TABLE tracks ADD COLUMN bit_depth INTEGER";
constexpr const char *markReread = "UPDATE tracks SET mtime = -1";
constexpr const char *addPlaylistSource = "ALTER TABLE playlists ADD COLUMN source TEXT";
constexpr const char *addPlaylistSkipped =
    "ALTER TABLE playlists ADD COLUMN skipped INTEGER NOT NULL DEFAULT 0";
constexpr const char *playlistSourceIndex =
    "CREATE UNIQUE INDEX IF NOT EXISTS playlists_source ON playlists(source)";
constexpr const char *dropUnsourced = "DELETE FROM playlists WHERE source IS NULL";
constexpr const char *dropUnsourcedItems =
    "DELETE FROM playlist_items WHERE playlist_id NOT IN (SELECT id FROM playlists)";
constexpr const char *dropUnsourcedPins =
    "DELETE FROM pins WHERE kind = 'playlist' AND ref_id NOT IN (SELECT id FROM playlists)";
constexpr const char *addPlaylistMtime =
    "ALTER TABLE playlists ADD COLUMN mtime INTEGER NOT NULL DEFAULT -1";
constexpr const char *addPlaylistSize =
    "ALTER TABLE playlists ADD COLUMN size INTEGER NOT NULL DEFAULT -1";
constexpr const char *playlistItemsTrackIndex =
    "CREATE INDEX IF NOT EXISTS playlist_items_track ON playlist_items(track_id)";
constexpr const char *metaTable =
    "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value NOT NULL)";
constexpr const char *setMinReader =
    "INSERT OR REPLACE INTO meta(key, value) VALUES('min_reader', 5)";
constexpr const char *favoriteOrderTable =
    "CREATE TABLE IF NOT EXISTS favorite_order(medium TEXT NOT NULL, rel_path TEXT NOT NULL, "
    "cue_index INTEGER NOT NULL DEFAULT 0, seq INTEGER NOT NULL, PRIMARY KEY(medium, rel_path, cue_index))";
constexpr const char *setFavoriteSeq = "INSERT OR IGNORE INTO meta(key, value) VALUES('favorite_seq', 0)";
constexpr const char *setMinReader8 = "INSERT OR REPLACE INTO meta(key, value) VALUES('min_reader', 8)";
constexpr const char *tracksNew =
    "CREATE TABLE tracks_new(id INTEGER PRIMARY KEY, path TEXT NOT NULL, "
    "mtime INTEGER NOT NULL, size INTEGER NOT NULL, title TEXT NOT NULL, artist TEXT NOT NULL, "
    "album_artist TEXT NOT NULL, album_id INTEGER NOT NULL REFERENCES albums(id), "
    "track_no INTEGER, disc_no INTEGER, year INTEGER, genre TEXT NOT NULL DEFAULT '', "
    "duration_ms INTEGER, added INTEGER NOT NULL, rating INTEGER NOT NULL DEFAULT 0, "
    "sample_rate INTEGER, bit_depth INTEGER, rg_track_gain REAL, rg_album_gain REAL, "
    "rg_track_peak REAL, rg_album_peak REAL, art_path TEXT, has_lyrics INTEGER NOT NULL DEFAULT 0, "
    "cue_index INTEGER NOT NULL DEFAULT 0, start_frame INTEGER NOT NULL DEFAULT 0, "
    "end_frame INTEGER NOT NULL DEFAULT 0, sheet_mtime INTEGER NOT NULL DEFAULT 0, "
    "sheet_size INTEGER NOT NULL DEFAULT 0, UNIQUE(path, cue_index))";
constexpr const char *renameTracks = "ALTER TABLE tracks_new RENAME TO tracks";
constexpr const char *tracksAlbumIndex = "CREATE INDEX IF NOT EXISTS tracks_album ON tracks(album_id)";
constexpr const char *tracksArtistIndex = "CREATE INDEX IF NOT EXISTS tracks_artist ON tracks(artist)";
constexpr const char *tracksGenreIndex = "CREATE INDEX IF NOT EXISTS tracks_genre ON tracks(genre)";

const char *const schema[] = {
    "CREATE TABLE IF NOT EXISTS artists(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE)",
    "CREATE TABLE IF NOT EXISTS genres(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE)",
    "CREATE TABLE IF NOT EXISTS albums(id INTEGER PRIMARY KEY, title TEXT NOT NULL, "
    "artist TEXT NOT NULL, year INTEGER, art_path TEXT, added INTEGER NOT NULL, "
    "UNIQUE(title, artist))",
    tracksNew,
    renameTracks,
    tracksAlbumIndex,
    tracksArtistIndex,
    tracksGenreIndex,
    trackArtistsTable,
    trackArtistsIndex,
    "CREATE TABLE IF NOT EXISTS history(id INTEGER PRIMARY KEY, "
    "track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE, "
    "played_at INTEGER NOT NULL)",
    "CREATE INDEX IF NOT EXISTS history_played ON history(played_at)",
    "CREATE INDEX IF NOT EXISTS history_track ON history(track_id)",
    "CREATE TABLE IF NOT EXISTS playlists(id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
    "source TEXT, skipped INTEGER NOT NULL DEFAULT 0, mtime INTEGER NOT NULL DEFAULT -1, "
    "size INTEGER NOT NULL DEFAULT -1)",
    playlistSourceIndex,
    "CREATE TABLE IF NOT EXISTS playlist_items("
    "playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE, "
    "pos INTEGER NOT NULL, "
    "track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE, "
    "PRIMARY KEY(playlist_id, pos))",
    playlistItemsTrackIndex,
    "CREATE TABLE IF NOT EXISTS pins(id INTEGER PRIMARY KEY, kind TEXT NOT NULL, "
    "ref_id INTEGER NOT NULL, pos INTEGER NOT NULL)",
    metaTable,
    setMinReader8,
    favoriteOrderTable,
    setFavoriteSeq,
};

const char *const toVersion2[] = {
    trackArtistsTable,
    trackArtistsIndex,
};

const char *const toVersion3[] = {
    addSampleRate,
    addBitDepth,
    markReread,
};

const char *const toVersion4[] = {
    addPlaylistSource,
    addPlaylistSkipped,
    playlistSourceIndex,
    dropUnsourced,
    dropUnsourcedItems,
    dropUnsourcedPins,
};

const char *const toVersion5[] = {
    addPlaylistMtime,
    addPlaylistSize,
    playlistItemsTrackIndex,
};

const char *const toVersion6[] = {
    "ALTER TABLE tracks ADD COLUMN rg_track_gain REAL",
    "ALTER TABLE tracks ADD COLUMN rg_album_gain REAL",
    "ALTER TABLE tracks ADD COLUMN rg_track_peak REAL",
    "ALTER TABLE tracks ADD COLUMN rg_album_peak REAL",
    "ALTER TABLE tracks ADD COLUMN art_path TEXT",
    "ALTER TABLE tracks ADD COLUMN has_lyrics INTEGER NOT NULL DEFAULT 0",
    metaTable,
    setMinReader,
    markReread,
};

const char *const toVersion7[] = {
    favoriteOrderTable,
    setFavoriteSeq,
};

constexpr const char *copyTracks =
    "INSERT INTO tracks_new(id, path, mtime, size, title, artist, album_artist, album_id, track_no, disc_no, year, "
    "genre, duration_ms, added, rating, sample_rate, bit_depth, rg_track_gain, rg_album_gain, rg_track_peak, "
    "rg_album_peak, art_path, has_lyrics) SELECT id, path, mtime, size, title, artist, album_artist, album_id, "
    "track_no, disc_no, year, genre, duration_ms, added, rating, sample_rate, bit_depth, rg_track_gain, "
    "rg_album_gain, rg_track_peak, rg_album_peak, art_path, has_lyrics FROM tracks";

const char *const toVersion8[] = {
    tracksNew,
    copyTracks,
    "DROP TABLE tracks",
    renameTracks,
    tracksAlbumIndex,
    tracksArtistIndex,
    tracksGenreIndex,
    "UPDATE tracks SET mtime = -1 WHERE path LIKE '%.flac'",
    setMinReader8,
};

struct Orphans
{
    const char *table;
    const char *sql;
};

const Orphans cascadeOrphans[] = {
    {"history", "DELETE FROM history WHERE track_id NOT IN (SELECT id FROM tracks)"},
    {"playlist_items", "DELETE FROM playlist_items WHERE track_id NOT IN (SELECT id FROM tracks) "
                       "OR playlist_id NOT IN (SELECT id FROM playlists)"},
    {"track_artists", "DELETE FROM track_artists WHERE track_id NOT IN (SELECT id FROM tracks) "
                      "OR artist_id NOT IN (SELECT id FROM artists)"},
};

struct Migration
{
    int to;
    std::span<const char *const> statements;
};

const Migration migrations[] = {
    {2, toVersion2},
    {3, toVersion3},
    {4, toVersion4},
    {5, toVersion5},
    {6, toVersion6},
    {7, toVersion7},
    {8, toVersion8},
};

const char *const pruneAlbumsAndArtists[] = {
    "DELETE FROM albums WHERE id NOT IN (SELECT DISTINCT album_id FROM tracks)",
    "DELETE FROM artists WHERE id NOT IN (SELECT DISTINCT artist_id FROM track_artists)",
};

const char *const pruneGenres[] = {
    "INSERT OR IGNORE INTO genres(name) SELECT DISTINCT genre FROM tracks WHERE genre <> ''",
    "DELETE FROM genres WHERE name NOT IN (SELECT DISTINCT genre FROM tracks WHERE genre <> '')",
};

constexpr const char *entryColumns =
    "SELECT t.id, t.path, t.title, t.artist, a.title, COALESCE(t.art_path, a.art_path), t.duration_ms, "
    "t.rg_track_gain, t.rg_album_gain, t.rg_track_peak, t.rg_album_peak, t.album_id, t.disc_no, t.track_no, t.sample_rate, "
    "t.cue_index, t.start_frame, t.end_frame";

constexpr const char *rowColumns =
    "SELECT t.id, t.title, t.artist, a.title, a.id, t.duration_ms, t.sample_rate, t.bit_depth";

QString placeholders(qsizetype count)
{
    QString out;
    out.reserve(count * 3);
    for (qsizetype i = 0; i < count; ++i)
        out += i == 0 ? QStringLiteral("?") : QStringLiteral(", ?");
    return out;
}

QList<qint64> distinctIds(const QVariantList &ids)
{
    QList<qint64> out;
    QSet<qint64> seen;
    out.reserve(ids.size());
    for (const QVariant &id : ids) {
        const qint64 value = id.toLongLong();
        if (!seen.contains(value)) {
            seen.insert(value);
            out.append(value);
        }
    }
    return out;
}

std::optional<double> optionalOf(const QVariant &value)
{
    return value.isNull() ? std::nullopt : std::optional<double>(value.toDouble());
}

QueueEntry entryOf(const QSqlQuery &query)
{
    QueueEntry entry;
    entry.id = query.value(0).toLongLong();
    entry.path = query.value(1).toString();
    entry.title = query.value(2).toString();
    entry.artist = query.value(3).toString();
    entry.album = query.value(4).toString();
    entry.artPath = query.value(5).toString();
    entry.durationMs = query.value(6).toLongLong();
    entry.trackGainDb = optionalOf(query.value(7));
    entry.albumGainDb = optionalOf(query.value(8));
    entry.trackPeak = optionalOf(query.value(9));
    entry.albumPeak = optionalOf(query.value(10));
    entry.albumId = query.value(11).toLongLong();
    entry.discNo = query.value(12).toInt();
    entry.trackNo = query.value(13).toInt();
    entry.sampleRate = query.value(14).toInt();
    entry.cueIndex = query.value(15).toInt();
    entry.startFrame = query.value(16).toInt();
    entry.endFrame = query.value(17).toInt();
    return entry;
}

QVariantMap rowOf(const QSqlQuery &query)
{
    QVariantMap row;
    row.insert(QStringLiteral("id"), query.value(0).toLongLong());
    row.insert(QStringLiteral("title"), query.value(1).toString());
    row.insert(QStringLiteral("artist"), query.value(2).toString());
    row.insert(QStringLiteral("album"), query.value(3).toString());
    row.insert(QStringLiteral("albumId"), query.value(4).toLongLong());
    row.insert(QStringLiteral("durationMs"), query.value(5).toLongLong());
    row.insert(QStringLiteral("sampleRate"), query.value(6).toInt());
    row.insert(QStringLiteral("bitDepth"), query.value(7).toInt());
    return row;
}

QVariant nullable(const QString &value)
{
    return value.isEmpty() ? QVariant(QMetaType::fromType<QString>()) : QVariant(value);
}

QVariant nullable(int value)
{
    return value > 0 ? QVariant(value) : QVariant(QMetaType::fromType<int>());
}

QVariant nullable(const std::optional<double> &value)
{
    return value ? QVariant(*value) : QVariant(QMetaType::fromType<double>());
}

bool run(QSqlQuery &query)
{
    if (query.exec())
        return true;
    qWarning() << "microdap library:" << query.lastError().text() << query.lastQuery();
    return false;
}

bool run(const QSqlDatabase &db, const char *sql)
{
    QSqlQuery query(db);
    if (query.exec(QString::fromLatin1(sql)))
        return true;
    qWarning() << "microdap library:" << query.lastError().text() << QLatin1StringView(sql);
    return false;
}

const QRegularExpression &artNamePattern()
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{40}\\.(png|jpg)$"));
    return pattern;
}

const char *pinTable(const QString &kind)
{
    if (kind == QLatin1StringView("album"))
        return "albums";
    if (kind == QLatin1StringView("artist"))
        return "artists";
    if (kind == QLatin1StringView("song"))
        return "tracks";
    if (kind == QLatin1StringView("playlist"))
        return "playlists";
    return nullptr;
}

constexpr const char *pinKinds[] = {"album", "artist", "song", "playlist"};

QString songSortKey(const QString &title)
{
    return sectionKey(title).sortKey;
}

struct TailTrack
{
    qint64 id = -1;
    QString path;
};

QString tailMatch(const QString &dir, const QString &root, const QString &tail, const QList<TailTrack> &tracks)
{
    const QString prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    if (dir != root && !dir.startsWith(prefix))
        return {};
    const QString relDir = dir == root ? QString() : dir.mid(prefix.size());
    QStringList ups;
    for (QString up = relDir; !up.isEmpty();) {
        const qsizetype slash = up.lastIndexOf(QLatin1Char('/'));
        up = slash < 0 ? QString() : up.left(slash);
        ups.append(up);
    }
    const qsizetype n = tail.size();
    QString best;
    qsizetype bestRank = -1;
    bool tied = false;
    for (const TailTrack &track : tracks) {
        if (!track.path.startsWith(prefix))
            continue;
        const QStringView rel = QStringView(track.path).mid(prefix.size());
        const qsizetype m = rel.size();
        if (m < n || (m > n && rel[m - n - 1] != QLatin1Char('/'))
            || rel.right(n).compare(tail, Qt::CaseInsensitive) != 0)
            continue;
        const QStringView folder = rel.left(m > n ? m - n - 1 : 0);
        if (folder.compare(relDir, Qt::CaseInsensitive) == 0)
            continue;
        qsizetype step = 0;
        while (step < ups.size() && folder.compare(ups.at(step), Qt::CaseInsensitive) != 0)
            ++step;
        if (step == ups.size() && folder.contains(QLatin1Char('/')))
            ++step;
        const bool exact = rel.right(n) == tail && (step >= ups.size() || folder == ups.at(step));
        const qsizetype rank = step * 2 + (exact ? 0 : 1);
        if (bestRank < 0 || rank < bestRank) {
            bestRank = rank;
            best = track.path;
            tied = false;
        } else if (rank == bestRank && track.path != best && step >= ups.size()) {
            tied = true;
        }
    }
    return tied ? QString() : best;
}

struct PathRow
{
    int cue = 0;
    qint64 id = -1;
};

}

class Library::PathIndex
{
public:
    explicit PathIndex(const QSqlDatabase &db)
    {
        QSqlQuery query(QStringLiteral("SELECT id, path, cue_index FROM tracks ORDER BY path, cue_index"), db);
        while (query.next())
            m_rows[query.value(1).toString()].append({query.value(2).toInt(), query.value(0).toLongLong()});
    }

    QString find(const QString &path, bool fold)
    {
        if (m_rows.contains(path))
            return path;
        if (!fold)
            return {};
        if (!m_foldedBuilt) {
            m_foldedBuilt = true;
            for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it)
                m_folded[it.key().toCaseFolded()].append(it.key());
        }
        const QStringList hits = m_folded.value(path.toCaseFolded());
        return hits.size() == 1 ? hits.first() : QString();
    }

    QList<qint64> rows(const QString &path, int cue, bool all) const
    {
        const QList<PathRow> found = m_rows.value(path);
        if (found.isEmpty())
            return {};
        if (found.first().cue == 0)
            return {found.first().id};
        QList<qint64> out;
        for (const PathRow &row : found) {
            if (cue && row.cue != cue)
                continue;
            out.append(row.id);
            if (!cue && !all)
                break;
        }
        return out;
    }

    const QList<TailTrack> &named(const QString &name)
    {
        if (!m_namedBuilt) {
            m_namedBuilt = true;
            for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it)
                m_named[QFileInfo(it.key()).fileName().toCaseFolded()].append({it.value().first().id, it.key()});
            for (QList<TailTrack> &tracks : m_named)
                std::sort(tracks.begin(), tracks.end(), [](const TailTrack &a, const TailTrack &b) { return a.id < b.id; });
        }
        static const QList<TailTrack> none;
        const auto it = m_named.constFind(name);
        return it == m_named.cend() ? none : *it;
    }

private:
    QHash<QString, QList<PathRow>> m_rows;
    QHash<QString, QStringList> m_folded;
    QHash<QString, QList<TailTrack>> m_named;
    bool m_foldedBuilt = false;
    bool m_namedBuilt = false;
};

Library::Library(QObject *parent)
    : Library(defaultDbPath(), defaultArtDir(), parent)
{
}

Library::Library(const QString &dbPath, QObject *parent)
    : Library(dbPath, defaultArtDir(), parent)
{
}

Library::Library(const QString &dbPath, const QString &artDir, QObject *parent)
    : QObject(parent)
    , m_dbPath(dbPath)
    , m_artDir(QDir::cleanPath(artDir))
    , m_connection(QStringLiteral("microdap-library-%1").arg(connectionCounter.fetch_add(1)))
{
    open();
}

Library::~Library()
{
    m_cancel.store(true);
    const QList<QThread *> threads = findChildren<QThread *>();
    for (QThread *thread : threads)
        thread->wait();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connection);
}

QString Library::defaultDbPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/microdap/library.db");
}

QString Library::defaultArtDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
           + QStringLiteral("/microdap/art");
}

void Library::open()
{
    QDir().mkpath(QFileInfo(m_dbPath).absolutePath());
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    m_db.setDatabaseName(m_dbPath);
    if (!m_db.open()) {
        qWarning() << "microdap library: cannot open" << m_dbPath << m_db.lastError().text();
        return;
    }
    run(m_db, "PRAGMA foreign_keys = ON");

    QSqlQuery version(QStringLiteral("PRAGMA user_version"), m_db);
    const int current = version.next() ? version.value(0).toInt() : 0;
    version.finish();
    if (current > schemaVersion) {
        const int reader = minReader();
        if (reader < 0 || reader > schemaVersion) {
            qWarning() << "microdap library: schema version" << current << "needs reader" << reader << "in"
                       << m_dbPath;
            m_db.close();
        }
        return;
    }
    if (current == 0) {
        if (!migrate(schemaVersion, schema))
            m_db.close();
        return;
    }
    for (const Migration &step : migrations) {
        if (step.to <= current)
            continue;
        if (!migrate(step.to, step.statements)) {
            qWarning() << "microdap library: migration to" << step.to << "failed, kept at"
                       << step.to - 1 << "in" << m_dbPath;
            m_db.close();
            return;
        }
    }
}

bool Library::migrate(int to, std::span<const char *const> statements)
{
    run(m_db, "PRAGMA foreign_keys = OFF");
    QSqlQuery keys(QStringLiteral("PRAGMA foreign_keys"), m_db);
    const bool off = keys.next() && keys.value(0).toInt() == 0;
    keys.finish();
    if (!off) {
        qWarning() << "microdap library: foreign keys still on before migration to" << to;
        run(m_db, "PRAGMA foreign_keys = ON");
        return false;
    }
    bool ok = m_db.transaction();
    const bool began = ok;
    for (const Orphans &orphans : cascadeOrphans) {
        QSqlQuery table(m_db);
        table.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?"));
        table.addBindValue(QString::fromLatin1(orphans.table));
        const bool present = ok && table.exec() && table.next();
        table.finish();
        if (present)
            ok = run(m_db, orphans.sql);
    }
    for (const char *sql : statements)
        ok = ok && run(m_db, sql);
    if (ok) {
        QSqlQuery check(m_db);
        ok = check.exec(QStringLiteral("PRAGMA foreign_key_check"));
        if (ok && check.next()) {
            qWarning() << "microdap library: foreign key violation in" << check.value(0).toString()
                       << "after migration to" << to;
            ok = false;
        }
    }
    if (ok) {
        QSqlQuery stamp(m_db);
        ok = stamp.exec(QStringLiteral("PRAGMA user_version = %1").arg(to));
    }
    ok = ok && m_db.commit();
    if (!ok && began)
        m_db.rollback();
    run(m_db, "PRAGMA foreign_keys = ON");
    return ok;
}

int Library::minReader() const
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT value FROM meta WHERE key = 'min_reader'")) || !query.next())
        return -1;
    return query.value(0).toInt();
}

bool Library::isOpen() const
{
    return m_db.isOpen();
}

QString Library::dbPath() const
{
    return m_dbPath;
}

QString Library::artDir() const
{
    return m_artDir;
}

QSqlDatabase Library::database() const
{
    return m_db;
}

bool Library::scanning() const
{
    return m_scanning;
}

void Library::setScanning(bool scanning)
{
    if (m_scanning == scanning)
        return;
    m_scanning = scanning;
    emit scanningChanged();
}

int Library::count(const QString &table) const
{
    QSqlQuery query(QStringLiteral("SELECT COUNT(*) FROM ") + table, m_db);
    return query.next() ? query.value(0).toInt() : 0;
}

QVariantMap Library::counts() const
{
    return {
        {QStringLiteral("tracks"), count(QStringLiteral("tracks"))},
        {QStringLiteral("albums"), count(QStringLiteral("albums"))},
        {QStringLiteral("artists"), count(QStringLiteral("artists"))},
        {QStringLiteral("genres"), count(QStringLiteral("genres"))},
    };
}

int Library::songCount() const
{
    return m_db.isOpen() ? count(QStringLiteral("tracks")) : 0;
}

int Library::playlistCount() const
{
    return m_db.isOpen() ? count(QStringLiteral("playlists")) : 0;
}

Settings *Library::settings() const
{
    return m_settings;
}

void Library::setSettings(Settings *settings)
{
    if (m_settings == settings)
        return;
    if (m_settings)
        disconnect(m_settings.data(), nullptr, this, nullptr);
    m_settings = settings;
    if (m_settings)
        connect(m_settings.data(), &Settings::rootsChanged, this, &Library::rescanRoots);
}

QStringList Library::scannedRoots() const
{
    return m_scannedRoots;
}

QStringList Library::scannedDirectories() const
{
    return m_scannedDirectories;
}

void Library::rescanRoots()
{
    if (!m_settings) {
        qWarning() << "microdap library: rescan without settings";
        return;
    }
    rescan(m_settings->roots());
}

QHash<QString, FileStamp> Library::snapshot() const
{
    QHash<QString, FileStamp> map;
    QSqlQuery query(QStringLiteral("SELECT path, MIN(mtime), MAX(size), MAX(sheet_mtime), MAX(sheet_size) FROM tracks "
                                   "GROUP BY path"),
                    m_db);
    while (query.next())
        map.insert(query.value(0).toString(), {query.value(1).toLongLong(), query.value(2).toLongLong(),
                                               query.value(3).toLongLong(), query.value(4).toLongLong()});
    return map;
}

QHash<QString, FileStamp> Library::playlistStamps() const
{
    QHash<QString, FileStamp> map;
    QSqlQuery query(QStringLiteral("SELECT source, mtime, size FROM playlists WHERE source IS NOT NULL"),
                    m_db);
    while (query.next())
        map.insert(query.value(0).toString(),
                   {query.value(1).toLongLong(), query.value(2).toLongLong()});
    return map;
}

void Library::rescan(const QStringList &roots)
{
    if (!m_db.isOpen()) {
        emit scanned(0, 0, 0, false);
        return;
    }
    if (m_scanning) {
        m_pendingRoots = roots;
        m_hasPending = true;
        return;
    }
    setScanning(true);
    m_scanRoots = roots;

    const QHash<QString, FileStamp> snap = snapshot();
    const QHash<QString, FileStamp> stamps = playlistStamps();
    const QString artDir = m_artDir;
    const std::atomic_bool *cancel = &m_cancel;
    QThread *thread = QThread::create([this, roots, snap, stamps, artDir, cancel] {
        Scanner scanner(artDir);
        ScanResult result = scanner.scan(roots, snap, cancel, stamps);
        QMetaObject::invokeMethod(
            this, [this, result = std::move(result)] { finishScan(result); },
            Qt::QueuedConnection);
    });
    thread->setParent(this);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void Library::endScan()
{
    if (m_hasPending) {
        m_hasPending = false;
        const QStringList roots = std::exchange(m_pendingRoots, QStringList());
        rescan(roots);
    }
}

void Library::finishScan(const ScanResult &result)
{
    QList<PlaylistFile> readable;
    for (const PlaylistFile &playlist : result.playlists) {
        if (playlist.readable)
            readable.append(playlist);
        else
            qWarning().noquote() << QStringLiteral("microdap: playlist %1: unreadable, kept as is")
                                        .arg(playlist.source);
    }
    const int added = static_cast<int>(result.added.size());
    const int updated = static_cast<int>(result.updated.size());
    const int removed = static_cast<int>(result.removed.size());
    const bool tracksTouched = added + updated + removed > 0;

    if (result.complete) {
        m_scannedRoots = m_scanRoots;
        m_scannedDirectories = result.directories;
        QSet<QString> dirs;
        for (const QString &source : playlistStamps().keys())
            dirs.insert(QFileInfo(source).absolutePath());
        for (const PlaylistFile &playlist : result.playlists)
            dirs.insert(QFileInfo(playlist.source).absolutePath());
        for (const QString &dir : std::as_const(dirs))
            m_store.settle(dir);
    }

    if (result.complete && !tracksTouched && readable.isEmpty() && result.removedPlaylists.isEmpty()) {
        setScanning(false);
        emit scanned(0, 0, 0, true);
        endScan();
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QHash<QString, qint64> albums;
    QHash<QString, qint64> artists;
    bool playlistsTouched = false;

    bool ok = result.complete && m_db.transaction();
    const bool began = ok;
    for (const QList<TrackInfo> *tracks : {&result.added, &result.updated}) {
        for (qsizetype from = 0; ok && from < tracks->size();) {
            qsizetype to = from + 1;
            while (to < tracks->size() && tracks->at(to).path == tracks->at(from).path)
                ++to;
            ok = applyFile(tracks->mid(from, to - from), now, albums, artists);
            from = to;
        }
    }
    if (ok && removed > 0) {
        const int itemsBefore = count(QStringLiteral("playlist_items"));
        for (const QString &path : result.removed)
            ok = ok && removeTrack(path);
        if (ok && count(QStringLiteral("playlist_items")) != itemsBefore)
            playlistsTouched = true;
    }
    ok = ok && applyPlaylists(readable, playlistsTouched);
    if (ok && !result.removedPlaylists.isEmpty()) {
        const int dropped = removePlaylists(result.removedPlaylists);
        ok = dropped >= 0;
        if (dropped > 0)
            playlistsTouched = true;
    }
    if (ok && tracksTouched)
        ok = prune(updated + removed > 0);
    const int unpinned = ok ? prunePins() : -1;
    ok = ok && unpinned >= 0;
    if (ok)
        ok = m_db.commit();
    if (ok) {
        if (tracksTouched)
            pruneArt();
    } else {
        qWarning() << "microdap library: scan apply failed" << m_db.lastError().text();
        if (began)
            m_db.rollback();
    }

    setScanning(false);
    if (ok) {
        if (tracksTouched) {
            emit countsChanged();
            emit tracksChanged();
            emit albumsChanged();
        }
        if (playlistsTouched) {
            emit playlistsChanged();
            emit playlistCountChanged();
        }
        if (unpinned > 0)
            emit pinsChanged();
        emit scanned(added, updated, removed, true);
    } else {
        emit scanned(0, 0, 0, false);
    }
    endScan();
}

qint64 Library::ensureAlbum(const TrackInfo &track, qint64 now, QHash<QString, qint64> &albums)
{
    const QString key = Scanner::albumKey(track.album, track.albumArtist);
    if (const auto cached = albums.constFind(key); cached != albums.cend())
        return *cached;

    QSqlQuery insert(m_db);
    insert.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO albums(title, artist, year, art_path, added) VALUES(?, ?, ?, ?, ?)"));
    insert.addBindValue(track.album);
    insert.addBindValue(track.albumArtist);
    insert.addBindValue(nullable(track.year));
    insert.addBindValue(nullable(track.artPath));
    insert.addBindValue(now);
    if (!run(insert))
        return -1;

    QSqlQuery update(m_db);
    update.prepare(QStringLiteral("UPDATE albums SET art_path = COALESCE(?, art_path), "
                                  "year = COALESCE(year, ?) WHERE title = ? AND artist = ?"));
    update.addBindValue(nullable(track.artPath));
    update.addBindValue(nullable(track.year));
    update.addBindValue(track.album);
    update.addBindValue(track.albumArtist);
    if (!run(update))
        return -1;

    QSqlQuery select(m_db);
    select.prepare(QStringLiteral("SELECT id FROM albums WHERE title = ? AND artist = ?"));
    select.addBindValue(track.album);
    select.addBindValue(track.albumArtist);
    if (!run(select) || !select.next())
        return -1;

    const qint64 id = select.value(0).toLongLong();
    albums.insert(key, id);
    return id;
}

qint64 Library::ensureArtist(const QString &name, QHash<QString, qint64> &artists)
{
    if (const auto cached = artists.constFind(name); cached != artists.cend())
        return *cached;

    QSqlQuery insert(m_db);
    insert.prepare(QStringLiteral("INSERT OR IGNORE INTO artists(name) VALUES(?)"));
    insert.addBindValue(name);
    if (!run(insert))
        return -1;

    QSqlQuery select(m_db);
    select.prepare(QStringLiteral("SELECT id FROM artists WHERE name = ?"));
    select.addBindValue(name);
    if (!run(select) || !select.next())
        return -1;

    const qint64 id = select.value(0).toLongLong();
    artists.insert(name, id);
    return id;
}

bool Library::linkArtists(qint64 trackId, const QStringList &names, QHash<QString, qint64> &artists)
{
    QSqlQuery clear(m_db);
    clear.prepare(QStringLiteral("DELETE FROM track_artists WHERE track_id = ?"));
    clear.addBindValue(trackId);
    if (!run(clear))
        return false;

    for (qsizetype pos = 0; pos < names.size(); ++pos) {
        const qint64 artistId = ensureArtist(names.at(pos), artists);
        if (artistId < 0)
            return false;
        QSqlQuery insert(m_db);
        insert.prepare(QStringLiteral(
            "INSERT INTO track_artists(track_id, artist_id, pos) VALUES(?, ?, ?)"));
        insert.addBindValue(trackId);
        insert.addBindValue(artistId);
        insert.addBindValue(static_cast<int>(pos));
        if (!run(insert))
            return false;
    }
    return true;
}

bool Library::upsertTrack(const TrackInfo &track, qint64 now, QHash<QString, qint64> &albums,
                          QHash<QString, qint64> &artists)
{
    const qint64 albumId = ensureAlbum(track, now, albums);
    if (albumId < 0)
        return false;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO tracks(path, mtime, size, title, artist, album_artist, album_id, track_no, "
        "disc_no, year, genre, duration_ms, added, rating, sample_rate, bit_depth, rg_track_gain, "
        "rg_album_gain, rg_track_peak, rg_album_peak, art_path, has_lyrics, cue_index, start_frame, end_frame, "
        "sheet_mtime, sheet_size) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(path, cue_index) DO UPDATE SET mtime = excluded.mtime, size = excluded.size, "
        "title = excluded.title, artist = excluded.artist, album_artist = excluded.album_artist, "
        "album_id = excluded.album_id, track_no = excluded.track_no, disc_no = excluded.disc_no, "
        "year = excluded.year, genre = excluded.genre, duration_ms = excluded.duration_ms, "
        "sample_rate = excluded.sample_rate, bit_depth = excluded.bit_depth, "
        "rg_track_gain = excluded.rg_track_gain, rg_album_gain = excluded.rg_album_gain, "
        "rg_track_peak = excluded.rg_track_peak, rg_album_peak = excluded.rg_album_peak, "
        "art_path = excluded.art_path, has_lyrics = excluded.has_lyrics, start_frame = excluded.start_frame, "
        "end_frame = excluded.end_frame, sheet_mtime = excluded.sheet_mtime, sheet_size = excluded.sheet_size"));
    query.addBindValue(track.path);
    query.addBindValue(track.mtime);
    query.addBindValue(track.size);
    query.addBindValue(track.title.isNull() ? QStringLiteral("") : track.title);
    query.addBindValue(track.artist.isNull() ? QStringLiteral("") : track.artist);
    query.addBindValue(track.albumArtist.isNull() ? QStringLiteral("") : track.albumArtist);
    query.addBindValue(albumId);
    query.addBindValue(nullable(track.trackNo));
    query.addBindValue(nullable(track.discNo));
    query.addBindValue(nullable(track.year));
    query.addBindValue(track.genre.isNull() ? QStringLiteral("") : track.genre);
    query.addBindValue(track.durationMs);
    query.addBindValue(now);
    query.addBindValue(track.sampleRate);
    query.addBindValue(track.bitDepth);
    query.addBindValue(nullable(track.trackGain));
    query.addBindValue(nullable(track.albumGain));
    query.addBindValue(nullable(track.trackPeak));
    query.addBindValue(nullable(track.albumPeak));
    query.addBindValue(nullable(track.trackArtPath));
    query.addBindValue(track.hasLyrics ? 1 : 0);
    query.addBindValue(track.cueIndex);
    query.addBindValue(track.startFrame);
    query.addBindValue(track.endFrame);
    query.addBindValue(track.sheetMtime);
    query.addBindValue(track.sheetSize);
    if (!run(query))
        return false;

    QSqlQuery select(m_db);
    select.prepare(QStringLiteral("SELECT id FROM tracks WHERE path = ? AND cue_index = ?"));
    select.addBindValue(track.path);
    select.addBindValue(track.cueIndex);
    if (!run(select) || !select.next())
        return false;
    return linkArtists(select.value(0).toLongLong(), track.artists, artists);
}

bool Library::removeTrack(const QString &path)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM tracks WHERE path = ?"));
    query.addBindValue(path);
    return run(query);
}

bool Library::applyFile(const QList<TrackInfo> &tracks, qint64 now, QHash<QString, qint64> &albums,
                        QHash<QString, qint64> &artists)
{
    const QString &path = tracks.constFirst().path;
    QList<int> wanted;
    for (const TrackInfo &track : tracks)
        wanted.append(track.cueIndex);
    QSet<int> present;
    QSqlQuery rows(m_db);
    rows.prepare(QStringLiteral("SELECT cue_index FROM tracks WHERE path = ?"));
    rows.addBindValue(path);
    if (!run(rows))
        return false;
    while (rows.next())
        present.insert(rows.value(0).toInt());
    rows.finish();
    const int first = *std::min_element(wanted.cbegin(), wanted.cend());
    int from = -1;
    if (first > 0 && present.contains(0) && !present.contains(first))
        from = 0;
    else if (first == 0 && !present.isEmpty() && !present.contains(0))
        from = *std::min_element(present.cbegin(), present.cend());
    if (from >= 0) {
        QSqlQuery rekey(m_db);
        rekey.prepare(QStringLiteral("UPDATE tracks SET cue_index = ? WHERE path = ? AND cue_index = ?"));
        rekey.addBindValue(first);
        rekey.addBindValue(path);
        rekey.addBindValue(from);
        if (!run(rekey))
            return false;
    }
    for (const TrackInfo &track : tracks) {
        if (!upsertTrack(track, now, albums, artists))
            return false;
    }
    QStringList keep;
    for (int cue : std::as_const(wanted))
        keep.append(QString::number(cue));
    QSqlQuery drop(m_db);
    drop.prepare(QStringLiteral("DELETE FROM tracks WHERE path = ? AND cue_index NOT IN (%1)").arg(keep.join(QLatin1Char(','))));
    drop.addBindValue(path);
    return run(drop);
}

bool Library::applyPlaylists(const QList<PlaylistFile> &playlists, bool &changed)
{
    if (playlists.isEmpty())
        return true;

    PathIndex index(m_db);
    QSqlQuery find(m_db);
    find.prepare(QStringLiteral("SELECT id, name, skipped FROM playlists WHERE source = ?"));
    QSqlQuery stored(m_db);
    stored.prepare(QStringLiteral("SELECT track_id FROM playlist_items WHERE playlist_id = ? ORDER BY pos"));

    for (const PlaylistFile &playlist : playlists) {
        QList<EntryRef> refs;
        refs.reserve(playlist.entries.size());
        for (qsizetype i = 0; i < playlist.entries.size(); ++i)
            refs.append({playlist.entries.at(i), playlist.cues.value(i), playlist.tails.value(i)});
        QList<qint64> ids;
        ids.reserve(refs.size());
        int skipped = 0;
        for (const QList<qint64> &rows : resolveWith(index, refs, QFileInfo(playlist.source).absolutePath(), true, true)) {
            ids.append(rows);
            if (rows.isEmpty())
                ++skipped;
        }

        find.bindValue(0, playlist.source);
        if (!run(find))
            return false;
        qint64 playlistId = -1;
        bool rewrite = true;
        if (find.next()) {
            playlistId = find.value(0).toLongLong();
            const bool same = find.value(1).toString() == playlist.name && find.value(2).toInt() == skipped;
            find.finish();

            QSqlQuery update(m_db);
            update.prepare(QStringLiteral(
                "UPDATE playlists SET name = ?, skipped = ?, mtime = ?, size = ? WHERE id = ?"));
            update.addBindValue(playlist.name);
            update.addBindValue(skipped);
            update.addBindValue(playlist.mtime);
            update.addBindValue(playlist.size);
            update.addBindValue(playlistId);
            if (!run(update))
                return false;

            stored.bindValue(0, playlistId);
            if (!run(stored))
                return false;
            QList<qint64> current;
            current.reserve(ids.size());
            while (stored.next())
                current.append(stored.value(0).toLongLong());
            stored.finish();
            rewrite = current != ids;
            if (!same || rewrite)
                changed = true;
        } else {
            find.finish();
            QSqlQuery insert(m_db);
            insert.prepare(QStringLiteral(
                "INSERT INTO playlists(name, source, skipped, mtime, size) VALUES(?, ?, ?, ?, ?)"));
            insert.addBindValue(playlist.name);
            insert.addBindValue(playlist.source);
            insert.addBindValue(skipped);
            insert.addBindValue(playlist.mtime);
            insert.addBindValue(playlist.size);
            if (!run(insert))
                return false;
            playlistId = insert.lastInsertId().toLongLong();
            changed = true;
        }

        if (rewrite) {
            QSqlQuery clear(m_db);
            clear.prepare(QStringLiteral("DELETE FROM playlist_items WHERE playlist_id = ?"));
            clear.addBindValue(playlistId);
            if (!run(clear))
                return false;

            QSqlQuery insert(m_db);
            insert.prepare(QStringLiteral(
                "INSERT INTO playlist_items(playlist_id, pos, track_id) VALUES(?, ?, ?)"));
            for (qsizetype pos = 0; pos < ids.size(); ++pos) {
                insert.bindValue(0, playlistId);
                insert.bindValue(1, static_cast<int>(pos));
                insert.bindValue(2, ids.at(pos));
                if (!run(insert))
                    return false;
            }
        }

        if (skipped > 0)
            qInfo().noquote() << QStringLiteral("microdap: playlist %1: %2 entries not in the library")
                                     .arg(playlist.name)
                                     .arg(skipped);
    }
    return true;
}

QList<QList<qint64>> Library::resolveWith(PathIndex &index, const QList<EntryRef> &refs, const QString &dir, bool fold,
                                          bool all) const
{
    QList<QList<qint64>> out(refs.size());
    std::optional<QString> root;
    for (qsizetype i = 0; i < refs.size(); ++i) {
        const EntryRef &ref = refs.at(i);
        if (ref.path.isEmpty())
            continue;
        QString hit = index.find(ref.path, fold);
        if (hit.isEmpty() && !ref.tail.isEmpty()) {
            if (!root)
                root = playlistRoot(dir);
            if (!root->isEmpty())
                hit = tailMatch(dir, *root, ref.tail, index.named(QFileInfo(ref.tail).fileName().toCaseFolded()));
        }
        if (!hit.isEmpty())
            out[i] = index.rows(hit, ref.cue, all);
    }
    return out;
}

QList<QList<qint64>> Library::resolveRefs(const QList<EntryRef> &refs, const QString &dir, bool fold, bool all) const
{
    PathIndex index(m_db);
    return resolveWith(index, refs, dir, fold, all);
}

QString Library::playlistRoot(const QString &dir) const
{
    const QString mount = Scanner::mountOf(dir);
    if (!mount.isEmpty())
        return mount;
    QString best;
    for (const QString &root : m_scannedRoots) {
        const QString clean = QDir::cleanPath(root);
        if (clean.size() > best.size() && (dir == clean || dir.startsWith(clean + QLatin1Char('/'))))
            best = clean;
    }
    return best;
}

QString Library::metaValue(const QString &key) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT value FROM meta WHERE key = ?"));
    query.addBindValue(key);
    if (!query.exec() || !query.next())
        return {};
    return query.value(0).toString();
}

bool Library::setMetaValue(const QString &key, const QString &value)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO meta(key, value) VALUES(?, ?)"));
    query.addBindValue(key);
    query.addBindValue(value);
    return run(query);
}

QString Library::favoriteKey(const QString &relPath, int cue)
{
    return relPath + QLatin1Char('\n') + QString::number(cue);
}

bool Library::stampFavorites(const QString &medium, const QList<std::pair<QString, int>> &entries)
{
    if (entries.isEmpty())
        return true;
    bool ok = m_db.transaction();
    const bool began = ok;
    QSqlQuery seq(m_db);
    QSqlQuery stamp(m_db);
    stamp.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO favorite_order(medium, rel_path, cue_index, seq) VALUES(?, ?, ?, "
        "(SELECT value FROM meta WHERE key = 'favorite_seq'))"));
    for (const auto &[relPath, cue] : entries) {
        ok = ok && seq.exec(QStringLiteral("UPDATE meta SET value = value + 1 WHERE key = 'favorite_seq'"));
        stamp.bindValue(0, medium);
        stamp.bindValue(1, relPath);
        stamp.bindValue(2, cue);
        ok = ok && run(stamp);
    }
    ok = ok && m_db.commit();
    if (!ok && began)
        m_db.rollback();
    return ok;
}

bool Library::dropFavoriteStamp(const QString &medium, const QString &relPath, int cue)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM favorite_order WHERE medium = ? AND rel_path = ? AND cue_index = ?"));
    query.addBindValue(medium);
    query.addBindValue(relPath);
    query.addBindValue(cue);
    return run(query);
}

QHash<QString, qint64> Library::favoriteStamps(const QString &medium) const
{
    QHash<QString, qint64> stamps;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT rel_path, cue_index, seq FROM favorite_order WHERE medium = ?"));
    query.addBindValue(medium);
    if (!query.exec())
        return stamps;
    while (query.next())
        stamps.insert(favoriteKey(query.value(0).toString(), query.value(1).toInt()), query.value(2).toLongLong());
    return stamps;
}

bool Library::keepFavoriteStamps(const QString &medium, const QSet<QString> &keys)
{
    QList<std::pair<QString, int>> gone;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT rel_path, cue_index FROM favorite_order WHERE medium = ?"));
    query.addBindValue(medium);
    if (!query.exec())
        return false;
    while (query.next()) {
        const QString relPath = query.value(0).toString();
        const int cue = query.value(1).toInt();
        if (!keys.contains(favoriteKey(relPath, cue)))
            gone.append({relPath, cue});
    }
    query.finish();
    if (gone.isEmpty())
        return true;
    bool ok = m_db.transaction();
    const bool began = ok;
    for (const auto &[relPath, cue] : std::as_const(gone))
        ok = ok && dropFavoriteStamp(medium, relPath, cue);
    ok = ok && m_db.commit();
    if (!ok && began)
        m_db.rollback();
    return ok;
}

PlaylistStore &Library::playlistStore()
{
    return m_store;
}

PlaylistStore::Edit Library::editPlaylist(const QString &path, const PlaylistStore::Change &change)
{
    const PlaylistStore::Edit result = m_store.edit(path, change);
    if (result != PlaylistStore::Edit::Done || !m_db.isOpen())
        return result;
    const PlaylistFile playlist = Scanner::readPlaylist(path);
    if (!playlist.readable)
        return result;
    bool changed = false;
    const bool began = m_db.transaction();
    if (began && applyPlaylists({playlist}, changed) && m_db.commit()) {
        if (changed) {
            emit playlistsChanged();
            emit playlistCountChanged();
        }
        return result;
    }
    qWarning() << "microdap library: playlist row update failed" << path << m_db.lastError().text();
    if (began)
        m_db.rollback();
    return result;
}

bool Library::onAbsentCard(const QString &path)
{
    const QString base = QDir::cleanPath(QDir(Scanner::mediaBase()).absolutePath()) + QLatin1Char('/');
    if (!QDir::cleanPath(path).startsWith(base))
        return false;
    const QString clean = QDir::cleanPath(path);
    QString dir = QFileInfo(clean).absolutePath();
    while (dir.startsWith(base)) {
        if (Scanner::isMountPoint(dir))
            return false;
        dir = QFileInfo(dir).absolutePath();
    }
    return true;
}

QVariantList Library::sourceTracks(const QString &kind, qint64 id) const
{
    QVariantList out;
    for (const QueueEntry &entry : entriesFor(kind, id))
        out.append(entry.id);
    return out;
}

QString Library::codecOf(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1StringView("m4a") || ext == QLatin1StringView("mp4") || ext == QLatin1StringView("m4b")) {
        TagLib::MP4::File file(QFile::encodeName(path).constData(), true, TagLib::AudioProperties::Fast);
        const TagLib::MP4::Properties *audio = file.isValid() ? file.audioProperties() : nullptr;
        if (!audio)
            return {};
        return audio->codec() == TagLib::MP4::Properties::ALAC ? QStringLiteral("alac") : QStringLiteral("aac");
    }
    if (ext == QLatin1StringView("ogg") || ext == QLatin1StringView("oga")) {
        const TagLib::FileRef ref(QFile::encodeName(path).constData(), false);
        if (dynamic_cast<const TagLib::Ogg::Opus::File *>(ref.file()))
            return QStringLiteral("opus");
        if (dynamic_cast<const TagLib::Ogg::FLAC::File *>(ref.file()))
            return QStringLiteral("flac");
        return QStringLiteral("vorbis");
    }
    if (ext == QLatin1StringView("opus"))
        return QStringLiteral("opus");
    if (ext == QLatin1StringView("flac") || ext == QLatin1StringView("mp3") || ext == QLatin1StringView("wav")
        || ext == QLatin1StringView("ape") || ext == QLatin1StringView("dsf") || ext == QLatin1StringView("dff"))
        return ext;
    return {};
}

QStringList Library::infoLines(const QString &codec, int rate, int depth, qint64 size, qint64 durationMs)
{
    QStringList out;
    if (!codec.isEmpty())
        out.append(codec);
    if (rate > 0)
        out.append(QStringLiteral("%1 hz").arg(rate));
    const bool dsd = codec == QLatin1StringView("dsf") || codec == QLatin1StringView("dff");
    const bool lossless = codec == QLatin1StringView("flac") || codec == QLatin1StringView("alac")
                          || codec == QLatin1StringView("wav") || codec == QLatin1StringView("ape");
    if (dsd && rate > 0)
        out.append(QStringLiteral("dsd%1").arg(qRound(rate / 44100.0)));
    else if (lossless && depth > 0)
        out.append(QStringLiteral("%1 bit").arg(depth));
    if (size > 0 && durationMs > 0)
        out.append(QStringLiteral("%1 kbps").arg(qRound64(double(size) * 8.0 / double(durationMs))));
    if (size > 0)
        out.append(QStringLiteral("%1 mb").arg(QString::number(double(size) / 1e6, 'f', 1)));
    return out;
}

QStringList Library::pathParts(const QString &path, const QStringList &mounts)
{
    const QString clean = QDir::cleanPath(path);
    QString root;
    for (const QString &mount : mounts) {
        if (clean.startsWith(mount + QLatin1Char('/')) && mount.size() > root.size())
            root = mount;
    }
    const QString home = QDir::cleanPath(QDir::homePath());
    if (root.isEmpty() && clean.startsWith(home + QLatin1Char('/')))
        root = home;
    const QString media = QDir::cleanPath(QDir(Scanner::mediaBase()).absolutePath());
    if (root.isEmpty() && clean.startsWith(media + QLatin1Char('/')))
        return clean.mid(media.size() + 1).split(QLatin1Char('/'), Qt::SkipEmptyParts).mid(2);
    const QString rel = root.isEmpty() ? clean.mid(1) : clean.mid(root.size() + 1);
    return rel.split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

QVariantMap Library::trackInfo(qint64 trackId) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT title, path, size, duration_ms, sample_rate, bit_depth, cue_index FROM tracks WHERE id = ?"));
    query.addBindValue(trackId);
    if (!query.exec() || !query.next())
        return {};
    const QString path = query.value(1).toString();
    qint64 durationMs = query.value(3).toLongLong();
    if (query.value(6).toInt() > 0) {
        QSqlQuery file(m_db);
        file.prepare(QStringLiteral("SELECT start_frame, duration_ms FROM tracks WHERE path = ?"));
        file.addBindValue(path);
        durationMs = 0;
        bool known = file.exec();
        while (known && file.next()) {
            const qint64 length = file.value(1).toLongLong();
            known = length > 0;
            durationMs = std::max(durationMs, cueFrameMs(file.value(0).toInt()) + length);
        }
        if (!known)
            durationMs = 0;
    }
    const QStringList lines = infoLines(codecOf(path), query.value(4).toInt(), query.value(5).toInt(),
                                        query.value(2).toLongLong(), durationMs);
    return {{QStringLiteral("title"), query.value(0).toString()},
            {QStringLiteral("lines"), lines},
            {QStringLiteral("parts"), pathParts(path, Favorites::cardMounts())}};
}

QVariantList Library::editablePlaylists() const
{
    QList<QVariantMap> rows;
    QSqlQuery query(QStringLiteral("SELECT id, name, source FROM playlists"), m_db);
    while (query.next()) {
        const QString source = query.value(2).toString();
        if (!source.endsWith(QLatin1StringView(".m3u8"), Qt::CaseInsensitive))
            continue;
        rows.append({{QStringLiteral("id"), query.value(0).toLongLong()},
                     {QStringLiteral("name"), query.value(1).toString()},
                     {QStringLiteral("source"), source}});
    }
    std::stable_sort(rows.begin(), rows.end(), [](const QVariantMap &a, const QVariantMap &b) {
        const QString x = a.value(QStringLiteral("name")).toString().toCaseFolded();
        const QString y = b.value(QStringLiteral("name")).toString().toCaseFolded();
        if (x != y)
            return x < y;
        return a.value(QStringLiteral("source")).toString() < b.value(QStringLiteral("source")).toString();
    });
    QVariantList out;
    for (const QVariantMap &row : std::as_const(rows))
        out.append(row);
    return out;
}

bool Library::editable(qint64 playlistId) const
{
    return playlistInfo(playlistId).value(QStringLiteral("source")).toString().endsWith(QLatin1StringView(".m3u8"),
                                                                                      Qt::CaseInsensitive);
}

QString Library::newPlaylistPath() const
{
    const QString dir = PlaylistStore::playlistsDir(QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
    QSet<QString> taken;
    QSqlQuery query(QStringLiteral("SELECT name FROM playlists"), m_db);
    while (query.next())
        taken.insert(query.value(0).toString().toCaseFolded());
    const QStringList files = QDir(dir).entryList(QDir::Files | QDir::Hidden);
    for (const QString &file : files) {
        QString name = file.toCaseFolded();
        for (const QLatin1StringView spare : {QLatin1StringView(".tmp"), QLatin1StringView(".new")}) {
            if (name.endsWith(spare))
                name.chop(spare.size());
        }
        for (const QLatin1StringView ext : {QLatin1StringView(".m3u8"), QLatin1StringView(".m3u")}) {
            if (name.endsWith(ext)) {
                name.chop(ext.size());
                taken.insert(name);
            }
        }
    }
    for (int n = 1;; ++n) {
        const QString name = QStringLiteral("playlist %1").arg(n);
        if (!taken.contains(name.toCaseFolded()))
            return dir + QLatin1Char('/') + name + QStringLiteral(".m3u8");
    }
}

QString Library::appendTracks(const QString &path, const QString &name, const QVariantList &trackIds)
{
    if (onAbsentCard(path))
        return QStringLiteral("card not available");
    const QList<QueueEntry> entries = queueEntries(trackIds);
    const QString dir = QFileInfo(path).absolutePath();
    const PlaylistStore::Edit result = editPlaylist(path, [&](M3u &doc) {
        for (const QueueEntry &entry : entries)
            PlaylistStore::insert(doc, std::numeric_limits<qsizetype>::max(), entry.path, dir, entry.cueIndex);
        return true;
    });
    if (result == PlaylistStore::Edit::Unreadable) {
        qWarning().noquote() << "microdap library: playlist not readable" << path;
        return QStringLiteral("playlist not readable");
    }
    if (result == PlaylistStore::Edit::Unwritten) {
        qWarning().noquote() << "microdap library: playlist not written" << path;
        return QStringLiteral("card not available");
    }
    qInfo().noquote() << "microdap library: added" << entries.size() << "to" << name;
    return {};
}

QString Library::addToPlaylist(qint64 playlistId, const QVariantList &trackIds)
{
    const QVariantMap info = playlistInfo(playlistId);
    const QString source = info.value(QStringLiteral("source")).toString();
    if (source.isEmpty() || !editable(playlistId))
        return QStringLiteral("playlist not readable");
    return appendTracks(source, info.value(QStringLiteral("name")).toString(), trackIds);
}

QVariantMap Library::createPlaylist(const QVariantList &trackIds)
{
    const QString path = newPlaylistPath();
    const QString name = QFileInfo(path).completeBaseName();
    QVariantMap out{{QStringLiteral("name"), name}, {QStringLiteral("id"), -1}};
    const QString refused = appendTracks(path, name, trackIds);
    if (!refused.isEmpty()) {
        out.insert(QStringLiteral("error"), refused);
        return out;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id FROM playlists WHERE source = ?"));
    query.addBindValue(path);
    if (query.exec() && query.next())
        out.insert(QStringLiteral("id"), query.value(0).toLongLong());
    return out;
}

QList<QList<qint64>> Library::resolveDoc(const M3u &doc, const QString &dir) const
{
    QList<EntryRef> refs;
    refs.reserve(doc.items.size());
    for (const M3uItem &item : doc.items) {
        if (!item.entry || item.path.isEmpty()) {
            refs.append(EntryRef{});
            continue;
        }
        const QByteArray text = item.text.trimmed();
        refs.append({item.path, item.cue,
                     Scanner::playlistTail(QString::fromUtf8(text.mid(text.lastIndexOf('\n') + 1)).trimmed())});
    }
    return resolveRefs(refs, dir, true, true);
}

QString Library::editPlaylistRow(qint64 playlistId, int row, qint64 trackId, const QString &verb)
{
    const QVariantMap info = playlistInfo(playlistId);
    const QString source = info.value(QStringLiteral("source")).toString();
    if (source.isEmpty() || !editable(playlistId))
        return QStringLiteral("playlist not readable");
    if (onAbsentCard(source))
        return QStringLiteral("card not available");
    const QString dir = QFileInfo(source).absolutePath();
    const int delta = verb == QLatin1StringView("move up") ? -1 : verb == QLatin1StringView("move down") ? 1 : 0;
    if (delta == 0 && verb != QLatin1StringView("remove"))
        return {};
    bool stale = false;
    const PlaylistStore::Edit result = editPlaylist(source, [&](M3u &doc) {
        struct Row
        {
            qsizetype item;
            qint64 id;
        };
        QList<QList<qint64>> ids;
        QList<Row> rows;
        const auto index = [&] {
            ids = resolveDoc(doc, dir);
            rows.clear();
            for (qsizetype i = 0; i < ids.size(); ++i) {
                for (qint64 id : ids.at(i))
                    rows.append({i, id});
            }
        };
        const auto unfold = [&](int at) {
            const qsizetype item = rows.at(at).item;
            if (ids.at(item).size() <= 1)
                return;
            QList<int> cues;
            const QList<QueueEntry> entries = queueEntries(QVariantList(ids.at(item).cbegin(), ids.at(item).cend()));
            for (const QueueEntry &entry : entries)
                cues.append(entry.cueIndex);
            PlaylistStore::split(doc, item, cues);
            index();
        };
        index();
        if (row < 0 || row >= rows.size() || rows.at(row).id != trackId) {
            stale = true;
            return false;
        }
        unfold(row);
        if (delta == 0) {
            PlaylistStore::erase(doc, rows.at(row).item);
            return true;
        }
        const int to = row + delta;
        if (to < 0 || to >= rows.size())
            return false;
        unfold(to);
        PlaylistStore::swap(doc, rows.at(row).item, rows.at(to).item);
        return true;
    });
    if (result == PlaylistStore::Edit::Unreadable || stale)
        return QStringLiteral("playlist not readable");
    if (result == PlaylistStore::Edit::Unwritten)
        return QStringLiteral("card not available");
    return {};
}

QString Library::deletePlaylist(qint64 playlistId)
{
    const QVariantMap info = playlistInfo(playlistId);
    const QString source = info.value(QStringLiteral("source")).toString();
    if (source.isEmpty() || !editable(playlistId))
        return QStringLiteral("playlist not readable");
    if (onAbsentCard(source))
        return QStringLiteral("card not available");
    if (QFileInfo::exists(source) && !QFile::remove(source)) {
        qWarning().noquote() << "microdap library: cannot delete" << source;
        return QStringLiteral("card not available");
    }
    for (const char *spare : {".tmp", ".new"})
        QFile::remove(source + QLatin1StringView(spare));
    const bool began = m_db.transaction();
    const int removed = began ? removePlaylists({source}) : -1;
    const int pins = removed >= 0 ? prunePins() : -1;
    if (removed < 0 || pins < 0 || !m_db.commit()) {
        qWarning() << "microdap library: playlist row delete failed" << source << m_db.lastError().text();
        if (began)
            m_db.rollback();
        return QStringLiteral("playlist not readable");
    }
    qInfo().noquote() << "microdap library: deleted playlist" << source;
    emit playlistsChanged();
    emit playlistCountChanged();
    if (pins > 0)
        emit pinsChanged();
    return {};
}

int Library::removePlaylists(const QStringList &sources)
{
    int removed = 0;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM playlists WHERE source = ?"));
    for (const QString &source : sources) {
        query.bindValue(0, source);
        if (!run(query))
            return -1;
        removed += qMax(0, query.numRowsAffected());
    }
    return removed;
}

bool Library::prune(bool albumsAndArtists)
{
    if (albumsAndArtists) {
        for (const char *sql : pruneAlbumsAndArtists) {
            if (!run(m_db, sql))
                return false;
        }
    }
    for (const char *sql : pruneGenres) {
        if (!run(m_db, sql))
            return false;
    }
    return true;
}

int Library::prunePins()
{
    int removed = 0;
    for (const char *kind : pinKinds) {
        const QString name = QString::fromLatin1(kind);
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral("DELETE FROM pins WHERE kind = ? AND ref_id NOT IN (SELECT id FROM %1)")
                          .arg(QLatin1StringView(pinTable(name))));
        query.addBindValue(name);
        if (!run(query))
            return -1;
        removed += qMax(0, query.numRowsAffected());
    }
    return removed;
}

bool Library::pin(const QString &kind, qint64 refId)
{
    if (!pinTable(kind) || isPinned(kind, refId))
        return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO pins(kind, ref_id, pos) "
                                 "SELECT ?, ?, COALESCE(MAX(pos), -1) + 1 FROM pins"));
    query.addBindValue(kind);
    query.addBindValue(refId);
    if (!run(query))
        return false;
    emit pinsChanged();
    return true;
}

bool Library::unpin(const QString &kind, qint64 refId)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM pins WHERE kind = ? AND ref_id = ?"));
    query.addBindValue(kind);
    query.addBindValue(refId);
    if (!run(query) || query.numRowsAffected() <= 0)
        return false;
    emit pinsChanged();
    return true;
}

bool Library::isPinned(const QString &kind, qint64 refId) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT 1 FROM pins WHERE kind = ? AND ref_id = ? LIMIT 1"));
    query.addBindValue(kind);
    query.addBindValue(refId);
    return query.exec() && query.next();
}

void Library::pruneArt()
{
    QSet<QString> cached;
    const QFileInfoList files = QDir(m_artDir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &file : files) {
        if (artNamePattern().match(file.fileName()).hasMatch())
            cached.insert(m_artDir + QLatin1Char('/') + file.fileName());
    }

    QSet<QString> referenced;
    QList<qint64> missing;
    QSqlQuery query(QStringLiteral("SELECT id, art_path FROM albums WHERE art_path IS NOT NULL"),
                    m_db);
    while (query.next()) {
        const QString path = query.value(1).toString();
        if (QFileInfo::exists(path))
            referenced.insert(path);
        else
            missing.append(query.value(0).toLongLong());
    }
    query.finish();

    QList<qint64> lost;
    QSqlQuery tracks(QStringLiteral("SELECT id, art_path FROM tracks WHERE art_path IS NOT NULL"), m_db);
    while (tracks.next()) {
        const QString path = tracks.value(1).toString();
        if (cached.contains(path))
            referenced.insert(path);
        else
            lost.append(tracks.value(0).toLongLong());
    }
    tracks.finish();

    if (!missing.isEmpty() || !lost.isEmpty()) {
        bool ok = m_db.transaction();
        QSqlQuery album(m_db);
        album.prepare(QStringLiteral("UPDATE albums SET art_path = NULL WHERE id = ?"));
        for (qint64 id : std::as_const(missing)) {
            album.addBindValue(id);
            ok = ok && run(album);
        }
        QSqlQuery track(m_db);
        track.prepare(QStringLiteral("UPDATE tracks SET art_path = NULL, mtime = -1 WHERE id = ?"));
        for (qint64 id : std::as_const(lost)) {
            track.addBindValue(id);
            ok = ok && run(track);
        }
        if (!ok || !m_db.commit()) {
            qWarning() << "microdap library: art prune failed" << m_db.lastError().text();
            m_db.rollback();
        }
    }

    for (const QString &path : std::as_const(cached)) {
        if (!referenced.contains(path))
            QFile::remove(path);
    }
}

void Library::addHistory(qint64 trackId, const QDateTime &playedAt)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO history(track_id, played_at) VALUES(?, ?)"));
    query.addBindValue(trackId);
    query.addBindValue(playedAt.toMSecsSinceEpoch());
    if (!run(query))
        return;
    if (++m_historyInserts % historyPruneEvery == 0) {
        QSqlQuery prune(m_db);
        prune.prepare(QStringLiteral("DELETE FROM history WHERE id <= "
                                     "(SELECT id FROM history ORDER BY id DESC LIMIT 1 OFFSET ?)"));
        prune.addBindValue(historyKeep);
        run(prune);
    }
    emit played(trackId);
}

void Library::setRating(qint64 trackId, int rating)
{
    const int value = qBound(0, rating, 1);
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE tracks SET rating = ? WHERE id = ?"));
    query.addBindValue(value);
    query.addBindValue(trackId);
    if (run(query))
        emit ratingChanged(trackId, value);
}

int Library::rating(qint64 trackId) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT rating FROM tracks WHERE id = ?"));
    query.addBindValue(trackId);
    if (!query.exec() || !query.next())
        return 0;
    return query.value(0).toInt();
}

QVariantList Library::idColumn(const QString &sql, qint64 bind) const
{
    QVariantList ids;
    QSqlQuery query(m_db);
    query.prepare(sql);
    query.addBindValue(bind);
    if (!query.exec())
        return ids;
    while (query.next())
        ids.append(query.value(0).toLongLong());
    return ids;
}

QVariantList Library::artistAlbums(qint64 artistId) const
{
    return idColumn(QStringLiteral("SELECT a.id FROM albums a WHERE a.id IN "
                                   "(SELECT t.album_id FROM tracks t "
                                   "JOIN track_artists ta ON ta.track_id = t.id "
                                   "WHERE ta.artist_id = ?) ORDER BY a.year, a.title, a.id"),
                    artistId);
}

QVariantList Library::artistTracks(qint64 artistId) const
{
    return idColumn(QStringLiteral("SELECT t.id FROM tracks t "
                                   "JOIN track_artists ta ON ta.track_id = t.id "
                                   "JOIN albums a ON a.id = t.album_id WHERE ta.artist_id = ? "
                                   "ORDER BY a.year, a.title, a.id, t.disc_no, t.track_no, t.id"),
                    artistId);
}

QVariantMap Library::artistInfo(qint64 artistId) const
{
    QVariantMap info;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT name FROM artists WHERE id = ?"));
    query.addBindValue(artistId);
    if (!query.exec() || !query.next())
        return info;
    info.insert(QStringLiteral("id"), artistId);
    info.insert(QStringLiteral("name"), query.value(0).toString());
    query.finish();
    query.prepare(QStringLiteral("SELECT a.art_path FROM albums a WHERE a.id IN "
                                 "(SELECT t.album_id FROM tracks t "
                                 "JOIN track_artists ta ON ta.track_id = t.id "
                                 "WHERE ta.artist_id = ?) AND a.art_path IS NOT NULL "
                                 "AND a.art_path != '' ORDER BY a.year, a.title, a.id LIMIT 1"));
    query.addBindValue(artistId);
    info.insert(QStringLiteral("artPath"),
                query.exec() && query.next() ? query.value(0).toString() : QString());
    return info;
}

QVariantMap Library::albumInfo(qint64 albumId) const
{
    QVariantMap info;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT title, artist, year, art_path FROM albums WHERE id = ?"));
    query.addBindValue(albumId);
    if (!query.exec() || !query.next())
        return info;
    info.insert(QStringLiteral("id"), albumId);
    info.insert(QStringLiteral("title"), query.value(0).toString());
    info.insert(QStringLiteral("artist"), query.value(1).toString());
    info.insert(QStringLiteral("year"), query.value(2).isNull() ? 0 : query.value(2).toInt());
    info.insert(QStringLiteral("artPath"), query.value(3).toString());
    return info;
}

QVariantList Library::albumTracks(qint64 albumId) const
{
    return trackRowsWhere(QStringLiteral(" FROM tracks t JOIN albums a ON a.id = t.album_id "
                                         "WHERE t.album_id = ? ORDER BY t.disc_no, t.track_no, t.id"),
                          albumId);
}

QVariantList Library::artistAlbumRows(qint64 artistId) const
{
    QVariantList rows;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT a.id, a.title, a.artist, a.year, a.art_path FROM albums a "
                                 "WHERE a.id IN (SELECT t.album_id FROM tracks t "
                                 "JOIN track_artists ta ON ta.track_id = t.id "
                                 "WHERE ta.artist_id = ?) ORDER BY a.year, a.title, a.id"));
    query.addBindValue(artistId);
    if (!query.exec())
        return rows;
    while (query.next()) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), query.value(0).toLongLong());
        row.insert(QStringLiteral("title"), query.value(1).toString());
        row.insert(QStringLiteral("artist"), query.value(2).toString());
        row.insert(QStringLiteral("year"), query.value(3).isNull() ? 0 : query.value(3).toInt());
        row.insert(QStringLiteral("artPath"), query.value(4).toString());
        rows.append(row);
    }
    return rows;
}

QVariantList Library::artistTrackRows(qint64 artistId) const
{
    return trackRowsWhere(QStringLiteral(" FROM tracks t JOIN track_artists ta ON ta.track_id = t.id "
                                         "JOIN albums a ON a.id = t.album_id WHERE ta.artist_id = ? "
                                         "ORDER BY a.year, a.title, a.id, t.disc_no, t.track_no, t.id"),
                          artistId);
}

QVariantList Library::trackRowsWhere(const QString &sql, qint64 bind) const
{
    QVariantList rows;
    QSqlQuery query(m_db);
    query.prepare(QString::fromLatin1(rowColumns) + sql);
    query.addBindValue(bind);
    if (!query.exec())
        return rows;
    while (query.next())
        rows.append(rowOf(query));
    return rows;
}

QVariantMap Library::playlistInfo(qint64 playlistId) const
{
    QVariantMap info;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT p.name, (SELECT COUNT(*) FROM playlist_items i "
                                 "WHERE i.playlist_id = p.id), p.skipped, p.source "
                                 "FROM playlists p WHERE p.id = ?"));
    query.addBindValue(playlistId);
    if (!query.exec() || !query.next())
        return info;
    info.insert(QStringLiteral("id"), playlistId);
    info.insert(QStringLiteral("name"), query.value(0).toString());
    info.insert(QStringLiteral("count"), query.value(1).toInt());
    info.insert(QStringLiteral("skipped"), query.value(2).toInt());
    info.insert(QStringLiteral("source"), query.value(3).toString());
    return info;
}

QVariantList Library::playlistTracks(qint64 playlistId) const
{
    return idColumn(QStringLiteral("SELECT track_id FROM playlist_items WHERE playlist_id = ? "
                                   "ORDER BY pos"),
                    playlistId);
}

QVariantList Library::songOrder(const QString &where, const QVariantList &binds) const
{
    QList<std::pair<QString, qint64>> rows;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT t.id, t.title FROM tracks t "
                                 "JOIN albums a ON a.id = t.album_id")
                  + where + QStringLiteral(" ORDER BY t.id"));
    for (const QVariant &bind : binds)
        query.addBindValue(bind);
    QVariantList ids;
    if (!query.exec())
        return ids;
    while (query.next())
        rows.append({songSortKey(query.value(1).toString()), query.value(0).toLongLong()});
    std::stable_sort(rows.begin(), rows.end(),
                     [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &row : std::as_const(rows))
        ids.append(row.second);
    return ids;
}

QVariantList Library::genreTracks(qint64 genreId) const
{
    return songOrder(QStringLiteral(" WHERE t.genre = (SELECT name FROM genres WHERE id = ?)"),
                     {genreId});
}

QVariantList Library::allTracks() const
{
    return songOrder(QString(), {});
}

QVariantList Library::trackRows(const QVariantList &trackIds) const
{
    QVariantList rows;
    if (trackIds.isEmpty())
        return rows;
    const QList<qint64> ids = distinctIds(trackIds);
    QHash<qint64, QVariantMap> found;
    found.reserve(ids.size());
    QSqlQuery query(m_db);
    for (qsizetype from = 0; from < ids.size(); from += idChunk) {
        const qsizetype size = qMin<qsizetype>(idChunk, ids.size() - from);
        query.prepare(QString::fromLatin1(rowColumns)
                      + QStringLiteral(" FROM tracks t JOIN albums a ON a.id = t.album_id WHERE t.id IN (%1)")
                            .arg(placeholders(size)));
        for (qsizetype i = 0; i < size; ++i)
            query.bindValue(static_cast<int>(i), ids.at(from + i));
        if (!query.exec())
            continue;
        while (query.next())
            found.insert(query.value(0).toLongLong(), rowOf(query));
        query.finish();
    }
    rows.reserve(trackIds.size());
    for (const QVariant &id : trackIds) {
        if (const auto hit = found.constFind(id.toLongLong()); hit != found.cend())
            rows.append(*hit);
    }
    return rows;
}

QList<QueueEntry> Library::queueEntries(const QVariantList &trackIds) const
{
    QList<QueueEntry> entries;
    if (trackIds.isEmpty())
        return entries;
    const QList<qint64> ids = distinctIds(trackIds);
    QHash<qint64, QueueEntry> found;
    found.reserve(ids.size());
    QSqlQuery query(m_db);
    for (qsizetype from = 0; from < ids.size(); from += idChunk) {
        const qsizetype size = qMin<qsizetype>(idChunk, ids.size() - from);
        query.prepare(QString::fromLatin1(entryColumns)
                      + QStringLiteral(" FROM tracks t JOIN albums a ON a.id = t.album_id WHERE t.id IN (%1)")
                            .arg(placeholders(size)));
        for (qsizetype i = 0; i < size; ++i)
            query.bindValue(static_cast<int>(i), ids.at(from + i));
        if (!query.exec())
            continue;
        while (query.next()) {
            const QueueEntry entry = entryOf(query);
            found.insert(entry.id, entry);
        }
        query.finish();
    }
    entries.reserve(trackIds.size());
    for (const QVariant &id : trackIds) {
        if (const auto hit = found.constFind(id.toLongLong()); hit != found.cend())
            entries.append(*hit);
    }
    return entries;
}

QHash<Library::CueKey, QueueEntry> Library::entriesByPath(const QList<CueKey> &keys) const
{
    QSet<QString> paths;
    for (const CueKey &key : keys)
        paths.insert(key.first);
    const QStringList unique = paths.values();
    QHash<QString, QList<QueueEntry>> rows;
    QSqlQuery query(m_db);
    for (qsizetype from = 0; from < unique.size(); from += idChunk) {
        const qsizetype size = qMin<qsizetype>(idChunk, unique.size() - from);
        query.prepare(QString::fromLatin1(entryColumns)
                      + QStringLiteral(" FROM tracks t JOIN albums a ON a.id = t.album_id WHERE t.path IN (%1) "
                                       "ORDER BY t.cue_index")
                            .arg(placeholders(size)));
        for (qsizetype i = 0; i < size; ++i)
            query.bindValue(static_cast<int>(i), unique.at(from + i));
        if (!query.exec())
            continue;
        while (query.next()) {
            const QueueEntry entry = entryOf(query);
            rows[entry.path].append(entry);
        }
        query.finish();
    }
    QHash<CueKey, QueueEntry> found;
    for (const CueKey &key : keys) {
        const QList<QueueEntry> candidates = rows.value(key.first);
        if (candidates.isEmpty())
            continue;
        const auto hit = std::find_if(candidates.cbegin(), candidates.cend(),
                                      [&key](const QueueEntry &entry) { return entry.cueIndex == key.second; });
        if (hit != candidates.cend())
            found.insert(key, *hit);
        else if (key.second == 0 || candidates.first().cueIndex == 0)
            found.insert(key, candidates.first());
    }
    return found;
}

QList<QueueEntry> Library::entryRows(const QString &sql, const QVariantList &binds) const
{
    QList<QueueEntry> entries;
    QSqlQuery query(m_db);
    query.prepare(QString::fromLatin1(entryColumns) + sql);
    for (const QVariant &bind : binds)
        query.addBindValue(bind);
    if (!query.exec())
        return entries;
    while (query.next())
        entries.append(entryOf(query));
    return entries;
}

QList<QueueEntry> Library::entriesFor(const QString &kind, qint64 id) const
{
    const QString from = QStringLiteral(" FROM tracks t JOIN albums a ON a.id = t.album_id");
    if (kind == QLatin1StringView("album"))
        return entryRows(from + QStringLiteral(" WHERE t.album_id = ? ORDER BY t.disc_no, t.track_no, t.id"),
                         {id});
    if (kind == QLatin1StringView("artist"))
        return entryRows(QStringLiteral(" FROM tracks t JOIN track_artists ta ON ta.track_id = t.id "
                                        "JOIN albums a ON a.id = t.album_id WHERE ta.artist_id = ? "
                                        "ORDER BY a.year, a.title, a.id, t.disc_no, t.track_no, t.id"),
                         {id});
    if (kind == QLatin1StringView("playlist"))
        return entryRows(QStringLiteral(" FROM playlist_items i JOIN tracks t ON t.id = i.track_id "
                                        "JOIN albums a ON a.id = t.album_id WHERE i.playlist_id = ? "
                                        "ORDER BY i.pos"),
                         {id});
    if (kind == QLatin1StringView("song"))
        return entryRows(from + QStringLiteral(" WHERE t.id = ?"), {id});

    QList<QueueEntry> rows;
    if (kind == QLatin1StringView("genre"))
        rows = entryRows(from + QStringLiteral(" WHERE t.genre = (SELECT name FROM genres WHERE id = ?) "
                                               "ORDER BY t.id"),
                         {id});
    else if (kind == QLatin1StringView("all"))
        rows = entryRows(from + QStringLiteral(" ORDER BY t.id"), {});
    else
        return rows;

    QList<std::pair<QString, qsizetype>> keys;
    keys.reserve(rows.size());
    for (qsizetype i = 0; i < rows.size(); ++i)
        keys.append({songSortKey(rows.at(i).title), i});
    std::stable_sort(keys.begin(), keys.end(),
                     [](const auto &a, const auto &b) { return a.first < b.first; });
    QList<QueueEntry> sorted;
    sorted.reserve(rows.size());
    for (const auto &key : std::as_const(keys))
        sorted.append(rows.at(key.second));
    return sorted;
}

}
