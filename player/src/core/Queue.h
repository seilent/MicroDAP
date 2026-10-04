#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QVariantMap>

namespace casdap {

struct QueueEntry
{
    qint64 id = 0;
    QString path;
    QString title;
    QString artist;
    QString album;
    QString artPath;
    qint64 durationMs = 0;

    QVariantMap toMap() const;
    static QueueEntry fromMap(const QVariantMap &map);
};

class Queue : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int index READ currentIndex WRITE setCurrentIndex NOTIFY indexChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(Repeat repeat READ repeat WRITE setRepeat NOTIFY repeatChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        PathRole,
        TitleRole,
        ArtistRole,
        AlbumRole,
        ArtPathRole,
        DurationRole,
        IsCurrentRole,
    };
    Q_ENUM(Role)

    enum Repeat {
        Off,
        All,
        One,
    };
    Q_ENUM(Repeat)

    explicit Queue(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int currentIndex() const;
    void setCurrentIndex(int index);
    int count() const;
    bool shuffle() const;
    void setShuffle(bool shuffle);
    Repeat repeat() const;
    void setRepeat(Repeat repeat);

    QueueEntry entry(int row) const;
    QList<QueueEntry> entries() const;
    QList<int> order() const;
    Q_INVOKABLE QVariantMap get(int row) const;

    void set(const QList<QueueEntry> &entries, int startIndex);
    void playNext(const QList<QueueEntry> &entries);
    void enqueue(const QList<QueueEntry> &entries);
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void clear();

    Q_INVOKABLE int nextIndex(bool manual = false) const;
    Q_INVOKABLE int previousIndex(bool manual = false) const;

    static int nextOf(int index, int count, const QList<int> &order, bool shuffle, Repeat repeat,
                      bool manual);
    static int previousOf(int index, int count, const QList<int> &order, bool shuffle,
                          Repeat repeat, bool manual);

signals:
    void indexChanged();
    void countChanged();
    void shuffleChanged();
    void repeatChanged();

private:
    void insertEntries(int at, const QList<QueueEntry> &entries);
    void rebuildOrder();
    void notifyCurrent(int oldIndex);

    QList<QueueEntry> m_entries;
    QList<int> m_order;
    int m_index = -1;
    bool m_shuffle = false;
    Repeat m_repeat = Off;
};

}
