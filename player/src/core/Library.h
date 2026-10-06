#pragma once

#include "Queue.h"
#include "Scanner.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <atomic>

namespace casdap {

class Library : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QVariantMap counts READ counts NOTIFY countsChanged)
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
    int playlistCount() const;

    Q_INVOKABLE void rescan(const QStringList &roots);
    Q_INVOKABLE void addHistory(qint64 trackId,
                                const QDateTime &playedAt = QDateTime::currentDateTimeUtc());
    Q_INVOKABLE void setRating(qint64 trackId, int rating);
    Q_INVOKABLE int rating(qint64 trackId) const;
    Q_INVOKABLE QVariantList artistAlbums(qint64 artistId) const;
    Q_INVOKABLE QVariantList artistTracks(qint64 artistId) const;
    Q_INVOKABLE QVariantMap artistInfo(qint64 artistId) const;
    Q_INVOKABLE QVariantMap albumInfo(qint64 albumId) const;
    Q_INVOKABLE QVariantList albumTracks(qint64 albumId) const;
    Q_INVOKABLE QVariantMap playlistInfo(qint64 playlistId) const;
    Q_INVOKABLE QVariantList playlistTracks(qint64 playlistId) const;
    Q_INVOKABLE QVariantList trackRows(const QVariantList &trackIds) const;
    QList<QueueEntry> queueEntries(const QVariantList &trackIds) const;
    Q_INVOKABLE bool pin(const QString &kind, qint64 refId);
    Q_INVOKABLE bool unpin(const QString &kind, qint64 refId);
    Q_INVOKABLE bool isPinned(const QString &kind, qint64 refId) const;

signals:
    void scanningChanged();
    void countsChanged();
    void playlistCountChanged();
    void changed();
    void historyChanged();
    void pinsChanged();
    void playlistsChanged();
    void ratingChanged(qint64 trackId, int rating);
    void scanned(int added, int updated, int removed, bool ok);

private:
    void open();
    void setScanning(bool scanning);
    int count(const QString &table) const;
    QHash<QString, FileStamp> snapshot() const;
    void finishScan(const ScanResult &result);
    bool upsertTrack(const TrackInfo &track, qint64 now, QHash<QString, qint64> &albums,
                     QHash<QString, qint64> &artists);
    qint64 ensureAlbum(const TrackInfo &track, qint64 now, QHash<QString, qint64> &albums);
    qint64 ensureArtist(const QString &name, QHash<QString, qint64> &artists);
    bool linkArtists(qint64 trackId, const QStringList &names, QHash<QString, qint64> &artists);
    QVariantList idColumn(const QString &sql, qint64 bind) const;
    bool removeTrack(const QString &path);
    bool prune();
    int prunePins();
    void pruneArt();

    QString m_dbPath;
    QString m_artDir;
    QString m_connection;
    QSqlDatabase m_db;
    std::atomic_bool m_cancel{false};
    bool m_scanning = false;
    bool m_hasPending = false;
    QStringList m_pendingRoots;
};

}
