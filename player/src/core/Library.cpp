#include "Library.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>

#include <atomic>
#include <utility>

namespace casdap {

namespace {

std::atomic_int connectionCounter{0};

const char *const schema[] = {
    "PRAGMA foreign_keys = ON",
    "CREATE TABLE IF NOT EXISTS artists(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE)",
    "CREATE TABLE IF NOT EXISTS genres(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE)",
    "CREATE TABLE IF NOT EXISTS albums(id INTEGER PRIMARY KEY, title TEXT NOT NULL, "
    "artist TEXT NOT NULL, year INTEGER, art_path TEXT, added INTEGER NOT NULL, "
    "UNIQUE(title, artist))",
    "CREATE TABLE IF NOT EXISTS tracks(id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, "
    "mtime INTEGER NOT NULL, size INTEGER NOT NULL, title TEXT NOT NULL, artist TEXT NOT NULL, "
    "album_artist TEXT NOT NULL, album_id INTEGER NOT NULL REFERENCES albums(id), "
    "track_no INTEGER, disc_no INTEGER, year INTEGER, genre TEXT NOT NULL DEFAULT '', "
    "duration_ms INTEGER, added INTEGER NOT NULL, rating INTEGER NOT NULL DEFAULT 0)",
    "CREATE INDEX IF NOT EXISTS tracks_album ON tracks(album_id)",
    "CREATE TABLE IF NOT EXISTS history(id INTEGER PRIMARY KEY, "
    "track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE, "
    "played_at INTEGER NOT NULL)",
    "CREATE INDEX IF NOT EXISTS history_played ON history(played_at)",
    "CREATE TABLE IF NOT EXISTS playlists(id INTEGER PRIMARY KEY, name TEXT NOT NULL)",
    "CREATE TABLE IF NOT EXISTS playlist_items("
    "playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE, "
    "pos INTEGER NOT NULL, "
    "track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE, "
    "PRIMARY KEY(playlist_id, pos))",
    "CREATE TABLE IF NOT EXISTS pins(id INTEGER PRIMARY KEY, kind TEXT NOT NULL, "
    "ref_id INTEGER NOT NULL, pos INTEGER NOT NULL)",
};

const char *const pruneStatements[] = {
    "DELETE FROM albums WHERE id NOT IN (SELECT DISTINCT album_id FROM tracks)",
    "INSERT OR IGNORE INTO artists(name) SELECT DISTINCT artist FROM tracks",
    "DELETE FROM artists WHERE name NOT IN (SELECT DISTINCT artist FROM tracks)",
    "INSERT OR IGNORE INTO genres(name) SELECT DISTINCT genre FROM tracks WHERE genre <> ''",
    "DELETE FROM genres WHERE name NOT IN (SELECT DISTINCT genre FROM tracks WHERE genre <> '')",
};

QVariant nullable(const QString &value)
{
    return value.isEmpty() ? QVariant(QMetaType::fromType<QString>()) : QVariant(value);
}

QVariant nullable(int value)
{
    return value > 0 ? QVariant(value) : QVariant(QMetaType::fromType<int>());
}

bool run(QSqlQuery &query)
{
    if (query.exec())
        return true;
    qWarning() << "casdap library:" << query.lastError().text() << query.lastQuery();
    return false;
}

bool run(const QSqlDatabase &db, const char *sql)
{
    QSqlQuery query(db);
    if (query.exec(QString::fromLatin1(sql)))
        return true;
    qWarning() << "casdap library:" << query.lastError().text() << QLatin1StringView(sql);
    return false;
}

}

Library::Library(QObject *parent)
    : Library(defaultDbPath(), parent)
{
}

Library::Library(const QString &dbPath, QObject *parent)
    : QObject(parent)
    , m_dbPath(dbPath)
    , m_artDir(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
               + QStringLiteral("/casdap/art"))
    , m_connection(QStringLiteral("casdap-library-%1").arg(connectionCounter.fetch_add(1)))
{
    open();
}

Library::~Library()
{
    if (m_thread)
        m_thread->wait();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connection);
}

QString Library::defaultDbPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/casdap/library.db");
}

void Library::open()
{
    QDir().mkpath(QFileInfo(m_dbPath).absolutePath());
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    m_db.setDatabaseName(m_dbPath);
    if (!m_db.open()) {
        qWarning() << "casdap library: cannot open" << m_dbPath << m_db.lastError().text();
        return;
    }
    for (const char *sql : schema)
        run(m_db, sql);
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

QHash<QString, FileStamp> Library::snapshot() const
{
    QHash<QString, FileStamp> map;
    QSqlQuery query(QStringLiteral("SELECT path, mtime, size FROM tracks"), m_db);
    while (query.next())
        map.insert(query.value(0).toString(),
                   {query.value(1).toLongLong(), query.value(2).toLongLong()});
    return map;
}

void Library::rescan(const QStringList &roots)
{
    if (!m_db.isOpen()) {
        emit scanned(0, 0, 0);
        return;
    }
    if (m_scanning) {
        m_pendingRoots = roots;
        m_hasPending = true;
        return;
    }
    setScanning(true);

    const QHash<QString, FileStamp> snap = snapshot();
    const QString artDir = m_artDir;
    QThread *thread = QThread::create([this, roots, snap, artDir] {
        Scanner scanner(artDir);
        ScanResult result = scanner.scan(roots, snap);
        QMetaObject::invokeMethod(
            this, [this, result = std::move(result)] { finishScan(result); },
            Qt::QueuedConnection);
    });
    thread->setParent(this);
    connect(thread, &QThread::finished, this, [this, thread] {
        if (m_thread == thread)
            m_thread = nullptr;
        thread->deleteLater();
    });
    m_thread = thread;
    thread->start();
}

void Library::finishScan(const ScanResult &result)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QHash<QString, qint64> albums;

    bool ok = m_db.transaction();
    for (const TrackInfo &track : result.added)
        ok = ok && upsertTrack(track, now, albums);
    for (const TrackInfo &track : result.updated)
        ok = ok && upsertTrack(track, now, albums);
    for (const QString &path : result.removed)
        ok = ok && removeTrack(path);
    ok = ok && prune();
    if (ok)
        ok = m_db.commit();
    if (!ok) {
        qWarning() << "casdap library: scan apply failed" << m_db.lastError().text();
        m_db.rollback();
    }

    setScanning(false);
    emit countsChanged();
    emit changed();
    emit scanned(static_cast<int>(result.added.size()), static_cast<int>(result.updated.size()),
                 static_cast<int>(result.removed.size()));

    if (m_hasPending) {
        m_hasPending = false;
        const QStringList roots = std::exchange(m_pendingRoots, QStringList());
        rescan(roots);
    }
}

qint64 Library::ensureAlbum(const TrackInfo &track, qint64 now, QHash<QString, qint64> &albums)
{
    const QString key = Scanner::albumKey(track.album, track.albumArtist);
    if (const auto cached = albums.constFind(key); cached != albums.cend()) {
        if (!track.artPath.isEmpty()) {
            QSqlQuery update(m_db);
            update.prepare(QStringLiteral("UPDATE albums SET art_path = ? WHERE id = ?"));
            update.addBindValue(track.artPath);
            update.addBindValue(*cached);
            if (!run(update))
                return -1;
        }
        return *cached;
    }

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

bool Library::upsertTrack(const TrackInfo &track, qint64 now, QHash<QString, qint64> &albums)
{
    const qint64 albumId = ensureAlbum(track, now, albums);
    if (albumId < 0)
        return false;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO tracks(path, mtime, size, title, artist, album_artist, album_id, track_no, "
        "disc_no, year, genre, duration_ms, added, rating) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0) "
        "ON CONFLICT(path) DO UPDATE SET mtime = excluded.mtime, size = excluded.size, "
        "title = excluded.title, artist = excluded.artist, album_artist = excluded.album_artist, "
        "album_id = excluded.album_id, track_no = excluded.track_no, disc_no = excluded.disc_no, "
        "year = excluded.year, genre = excluded.genre, duration_ms = excluded.duration_ms"));
    query.addBindValue(track.path);
    query.addBindValue(track.mtime);
    query.addBindValue(track.size);
    query.addBindValue(track.title);
    query.addBindValue(track.artist);
    query.addBindValue(track.albumArtist);
    query.addBindValue(albumId);
    query.addBindValue(nullable(track.trackNo));
    query.addBindValue(nullable(track.discNo));
    query.addBindValue(nullable(track.year));
    query.addBindValue(track.genre);
    query.addBindValue(track.durationMs);
    query.addBindValue(now);
    return run(query);
}

bool Library::removeTrack(const QString &path)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM tracks WHERE path = ?"));
    query.addBindValue(path);
    return run(query);
}

bool Library::prune()
{
    for (const char *sql : pruneStatements) {
        if (!run(m_db, sql))
            return false;
    }
    return true;
}

void Library::addHistory(qint64 trackId, const QDateTime &playedAt)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO history(track_id, played_at) VALUES(?, ?)"));
    query.addBindValue(trackId);
    query.addBindValue(playedAt.toMSecsSinceEpoch());
    if (run(query))
        emit historyChanged();
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

}
