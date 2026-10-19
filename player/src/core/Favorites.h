#pragma once

#include "PlaylistStore.h"
#include "Queue.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace microdap {

class Library;

class Favorites : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)

public:
    static constexpr const char *fileName = "favorites.m3u8";

    explicit Favorites(Library *library, QObject *parent = nullptr);

    static QList<qsizetype> order(const QList<qint64> &seqs);
    static QStringList cardMounts();

    int count() const;
    QList<QueueEntry> entries() const;
    bool contains(qint64 trackId) const;
    QStringList media() const;
    QString fileFor(const QString &medium) const;
    Q_INVOKABLE void toggle(qint64 trackId);
    Q_INVOKABLE QString add(const QVariantList &trackIds);

public slots:
    void reload();

signals:
    void changed();
    void notice(const QString &text);

private:
    struct Medium
    {
        QString id;
        QString root;
        QString dir;
        QString file;
        bool card = false;
        bool failed = false;
        M3u doc;
    };

    Medium *mediumOf(const QString &path);
    QString ready(Medium &medium);
    QString key(const Medium &medium, const QString &path) const;
    bool sameTrack(const Medium &medium, const QString &a, const QString &b) const;
    void readMedium(Medium &medium);
    void migrate(Medium &medium);
    QList<qint64> resolve(const Medium &medium, const M3u &doc) const;
    void rebuild();

    Library *m_library;
    QList<Medium> m_media;
    QList<qint64> m_ids;
    QSet<qint64> m_set;
};

}
