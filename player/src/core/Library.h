#pragma once

#include "PlaylistStore.h"
#include "Queue.h"
#include "Scanner.h"
#include "Settings.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSqlDatabase>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <atomic>
#include <span>
#include <utility>

namespace microdap {

class Library : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QVariantMap counts READ counts NOTIFY countsChanged)
    Q_PROPERTY(int songCount READ songCount NOTIFY countsChanged)
    Q_PROPERTY(int playlistCount READ playlistCount NOTIFY playlistCountChanged)

public:
    explicit Library(QObject *parent = nullptr);
    Library(const QString &dbPath, QObject *parent = nullptr);
    Library(const QString &dbPath, const QString &artDir, QObject *parent = nullptr);
    ~Library() override;

    static QString defaultDbPath();
    static QString defaultArtDir();

    bool isOpen() const;
    QString dbPath() const;
    QString artDir() const;
    QSqlDatabase database() const;

    bool scanning() const;
    QVariantMap counts() const;
    int songCount() const;
    int playlistCount() const;

    Settings *settings() const;
    void setSettings(Settings *settings);
    QStringList scannedRoots() const;
    QStringList scannedDirectories() const;

    Q_INVOKABLE void rescan(const QStringList &roots);
    Q_INVOKABLE void rescanRoots();
    Q_INVOKABLE void addHistory(qint64 trackId,
                                const QDateTime &playedAt = QDateTime::currentDateTimeUtc());
    Q_INVOKABLE void setRating(qint64 trackId, int rating);
    Q_INVOKABLE int rating(qint64 trackId) const;
    Q_INVOKABLE QVariantList artistAlbums(qint64 artistId) const;
    Q_INVOKABLE QVariantList artistTracks(qint64 artistId) const;
    Q_INVOKABLE QVariantList artistAlbumRows(qint64 artistId) const;
    Q_INVOKABLE QVariantList artistTrackRows(qint64 artistId) const;
    Q_INVOKABLE QVariantMap artistInfo(qint64 artistId) const;
    Q_INVOKABLE QVariantMap albumInfo(qint64 albumId) const;
    Q_INVOKABLE QVariantList albumTracks(qint64 albumId) const;
    Q_INVOKABLE QVariantMap playlistInfo(qint64 playlistId) const;
    Q_INVOKABLE QVariantList playlistTracks(qint64 playlistId) const;
    Q_INVOKABLE QVariantList genreTracks(qint64 genreId) const;
    Q_INVOKABLE QVariantList allTracks() const;
    Q_INVOKABLE QVariantList trackRows(const QVariantList &trackIds) const;
    QList<QueueEntry> queueEntries(const QVariantList &trackIds) const;
    QList<QueueEntry> entriesFor(const QString &kind, qint64 id) const;
    Q_INVOKABLE bool pin(const QString &kind, qint64 refId);
    Q_INVOKABLE bool unpin(const QString &kind, qint64 refId);
    Q_INVOKABLE bool isPinned(const QString &kind, qint64 refId) const;

    QString metaValue(const QString &key) const;
    bool setMetaValue(const QString &key, const QString &value);
    static QString favoriteKey(const QString &relPath, int cue);
    bool stampFavorites(const QString &medium, const QList<std::pair<QString, int>> &entries);
    bool dropFavoriteStamp(const QString &medium, const QString &relPath, int cue);
    QHash<QString, qint64> favoriteStamps(const QString &medium) const;
    bool keepFavoriteStamps(const QString &medium, const QSet<QString> &keys);

    QList<qint64> tailTracks(const QString &dir, const QStringList &tails) const;

    PlaylistStore &playlistStore();
    PlaylistStore::Edit editPlaylist(const QString &path, const PlaylistStore::Change &change);

    QHash<QString, FileStamp> snapshot() const;
    QHash<QString, FileStamp> playlistStamps() const;
    void finishScan(const ScanResult &result);

    static constexpr int historyKeep = 2000;
    static constexpr int historyPruneEvery = 100;
    static constexpr int idChunk = 500;

signals:
    void scanningChanged();
    void countsChanged();
    void playlistCountChanged();
    void tracksChanged();
    void albumsChanged();
    void played(qint64 trackId);
    void pinsChanged();
    void playlistsChanged();
    void ratingChanged(qint64 trackId, int rating);
    void scanned(int added, int updated, int removed, bool ok);

private:
    void open();
    bool migrate(int to, std::span<const char *const> statements);
    int minReader() const;
    void setScanning(bool scanning);
    int count(const QString &table) const;
    bool applyPlaylists(const QList<PlaylistFile> &playlists, bool &changed);
    QString playlistRoot(const QString &dir) const;
    int removePlaylists(const QStringList &sources);
    QVariantList songOrder(const QString &where, const QVariantList &binds) const;
    QList<QueueEntry> entryRows(const QString &sql, const QVariantList &binds) const;
    QVariantList trackRowsWhere(const QString &sql, qint64 bind) const;
    bool upsertTrack(const TrackInfo &track, qint64 now, QHash<QString, qint64> &albums,
                     QHash<QString, qint64> &artists);
    qint64 ensureAlbum(const TrackInfo &track, qint64 now, QHash<QString, qint64> &albums);
    qint64 ensureArtist(const QString &name, QHash<QString, qint64> &artists);
    bool linkArtists(qint64 trackId, const QStringList &names, QHash<QString, qint64> &artists);
    QVariantList idColumn(const QString &sql, qint64 bind) const;
    bool removeTrack(const QString &path);
    bool prune(bool albumsAndArtists);
    int prunePins();
    void pruneArt();
    void endScan();

    QString m_dbPath;
    QString m_artDir;
    QString m_connection;
    QSqlDatabase m_db;
    std::atomic_bool m_cancel{false};
    bool m_scanning = false;
    bool m_hasPending = false;
    int m_historyInserts = 0;
    QStringList m_pendingRoots;
    QStringList m_scanRoots;
    QStringList m_scannedRoots;
    QStringList m_scannedDirectories;
    QPointer<Settings> m_settings;
    PlaylistStore m_store;
};

}
