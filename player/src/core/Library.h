#pragma once

#include "Scanner.h"

#include <QDateTime>
#include <QObject>
#include <QSqlDatabase>
#include <QStringList>
#include <QVariantMap>

class QThread;

namespace casdap {

class Library : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QVariantMap counts READ counts NOTIFY countsChanged)

public:
    explicit Library(QObject *parent = nullptr);
    Library(const QString &dbPath, QObject *parent = nullptr);
    ~Library() override;

    static QString defaultDbPath();

    bool isOpen() const;
    QString dbPath() const;
    QString artDir() const;
    QSqlDatabase database() const;

    bool scanning() const;
    QVariantMap counts() const;

    Q_INVOKABLE void rescan(const QStringList &roots);
    Q_INVOKABLE void addHistory(qint64 trackId,
                                const QDateTime &playedAt = QDateTime::currentDateTimeUtc());
    Q_INVOKABLE void setRating(qint64 trackId, int rating);
    Q_INVOKABLE int rating(qint64 trackId) const;

signals:
    void scanningChanged();
    void countsChanged();
    void changed();
    void historyChanged();
    void ratingChanged(qint64 trackId, int rating);
    void scanned(int added, int updated, int removed);

private:
    void open();
    void setScanning(bool scanning);
    int count(const QString &table) const;
    QHash<QString, FileStamp> snapshot() const;
    void finishScan(const ScanResult &result);
    bool upsertTrack(const TrackInfo &track, qint64 now, QHash<QString, qint64> &albums);
    qint64 ensureAlbum(const TrackInfo &track, qint64 now, QHash<QString, qint64> &albums);
    bool removeTrack(const QString &path);
    bool prune();

    QString m_dbPath;
    QString m_artDir;
    QString m_connection;
    QSqlDatabase m_db;
    QThread *m_thread = nullptr;
    bool m_scanning = false;
    bool m_hasPending = false;
    QStringList m_pendingRoots;
};

}
