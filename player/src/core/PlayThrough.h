#pragma once

#include "Queue.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace microdap {

class AlbumsModel;
class Library;

class PlayThrough : public QObject
{
    Q_OBJECT

public:
    PlayThrough(Queue *queue, Library *library, AlbumsModel *albums, QObject *parent = nullptr);

    static bool due(const Queue &queue, const QString &mode);
    static bool dirBefore(const QString &a, const QString &b, const QStringList &roots);
    static QString nextDir(const QStringList &dirs, const QStringList &roots, const QString &dir);

    QString mode() const;
    void setMode(const QString &mode);
    void setRoots(const QStringList &roots);
    void onTrackStarted();

private:
    QList<QueueEntry> nextFolder(const QString &path) const;
    QList<QueueEntry> nextAlbum(qint64 trackId) const;

    Queue *m_queue;
    Library *m_library;
    AlbumsModel *m_albums;
    QString m_mode = QStringLiteral("off");
    QStringList m_roots;
};

}
