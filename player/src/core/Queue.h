#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QVariantMap>

#include <optional>

namespace microdap {

struct QueueEntry
{
    qint64 id = 0;
    QString path;
    QString title;
    QString artist;
    QString album;
    QString artPath;
    qint64 durationMs = 0;
    std::optional<double> trackGainDb;
    std::optional<double> albumGainDb;
    std::optional<double> trackPeak;
    std::optional<double> albumPeak;
    qint64 albumId = 0;
    int discNo = 0;
    int trackNo = 0;
    int sampleRate = 0;

    QVariantMap toMap() const;
    static QueueEntry fromMap(const QVariantMap &map);
};

class Queue;

class PlayOrderModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        RowRole = Qt::UserRole + 1,
        SlotRole,
        NumberRole,
        IdRole,
        TitleRole,
        ArtistRole,
        AlbumRole,
        DurationRole,
        IsCurrentRole,
    };
    Q_ENUM(Role)

    explicit PlayOrderModel(Queue *queue);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    Q_INVOKABLE QVariantMap get(int slot) const;

signals:
    void countChanged();

private:
    friend class Queue;

    void changed(int first, int last, const QList<int> &roles);

    Queue *m_queue;
};

class Queue : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int index READ currentIndex WRITE setCurrentIndex NOTIFY indexChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(Repeat repeat READ repeat WRITE setRepeat NOTIFY repeatChanged)
    Q_PROPERTY(int currentSlot READ currentSlot NOTIFY currentSlotChanged)
    Q_PROPERTY(microdap::PlayOrderModel *playOrder READ playOrder CONSTANT)

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

    static constexpr int incrementalInsertLimit = 64;

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
    PlayOrderModel *playOrder() const;

    QueueEntry entry(int row) const;
    QList<QueueEntry> entries() const;
    QList<int> order() const;
    QList<int> slotIndex() const;
    bool setOrder(const QList<int> &order);
    int currentSlot() const;
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QVariantList orderedRows() const;
    Q_INVOKABLE int rowAt(int slot) const;
    Q_INVOKABLE int slotOf(int row) const;
    Q_INVOKABLE QVariantList upcoming(int n) const;

    void set(const QList<QueueEntry> &entries, int startIndex, const QList<int> &order = {});
    void set(const QList<QueueEntry> &entries, int startIndex, const QList<int> &order, bool shuffle);
    void playNext(const QList<QueueEntry> &entries);
    void enqueue(const QList<QueueEntry> &entries);
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void moveSlot(int from, int to);
    Q_INVOKABLE void playNextRow(int row);
    Q_INVOKABLE void clear();

    Q_INVOKABLE int nextIndex(bool manual = false) const;
    Q_INVOKABLE int previousIndex(bool manual = false) const;

    static int nextOf(int index, int count, const QList<int> &order, const QList<int> &slot,
                      bool shuffle, Repeat repeat, bool manual);
    static int previousOf(int index, int count, const QList<int> &order, const QList<int> &slot,
                          bool shuffle, Repeat repeat, bool manual);

signals:
    void indexChanged();
    void countChanged();
    void shuffleChanged();
    void repeatChanged();
    void orderChanged();
    void currentSlotChanged();

private:
    friend class PlayOrderModel;

    bool shuffled() const;
    int slotCount() const;
    bool isPermutation(const QList<int> &order) const;
    void rebuildOrder();
    void rebuildSlots();
    void updateSlots(int first, int last);
    void moveOrderSlot(int from, int to);
    void touchCurrent(int row);
    void notifyIndex(int oldIndex, int oldSlot);
    void renumber(int first);

    QList<QueueEntry> m_entries;
    QList<int> m_order;
    QList<int> m_slot;
    int m_index = -1;
    bool m_shuffle = false;
    Repeat m_repeat = Off;
    PlayOrderModel *m_playOrder;
};

}
