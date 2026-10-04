#include "Queue.h"

#include <QRandomGenerator>

#include <algorithm>

namespace casdap {

QVariantMap QueueEntry::toMap() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("path"), path},
        {QStringLiteral("title"), title},
        {QStringLiteral("artist"), artist},
        {QStringLiteral("album"), album},
        {QStringLiteral("artPath"), artPath},
        {QStringLiteral("durationMs"), durationMs},
    };
}

QueueEntry QueueEntry::fromMap(const QVariantMap &map)
{
    QueueEntry entry;
    entry.id = map.value(QStringLiteral("id")).toLongLong();
    entry.path = map.value(QStringLiteral("path")).toString();
    entry.title = map.value(QStringLiteral("title")).toString();
    entry.artist = map.value(QStringLiteral("artist")).toString();
    entry.album = map.value(QStringLiteral("album")).toString();
    entry.artPath = map.value(QStringLiteral("artPath")).toString();
    entry.durationMs = map.value(QStringLiteral("durationMs")).toLongLong();
    return entry;
}

Queue::Queue(QObject *parent)
    : QAbstractListModel(parent)
{
}

int Queue::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant Queue::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const QueueEntry &entry = m_entries.at(index.row());
    switch (role) {
    case IdRole:
        return entry.id;
    case PathRole:
        return entry.path;
    case TitleRole:
    case Qt::DisplayRole:
        return entry.title;
    case ArtistRole:
        return entry.artist;
    case AlbumRole:
        return entry.album;
    case ArtPathRole:
        return entry.artPath;
    case DurationRole:
        return entry.durationMs;
    case IsCurrentRole:
        return index.row() == m_index;
    default:
        return {};
    }
}

QHash<int, QByteArray> Queue::roleNames() const
{
    return {
        {IdRole, QByteArrayLiteral("id")},
        {PathRole, QByteArrayLiteral("path")},
        {TitleRole, QByteArrayLiteral("title")},
        {ArtistRole, QByteArrayLiteral("artist")},
        {AlbumRole, QByteArrayLiteral("album")},
        {ArtPathRole, QByteArrayLiteral("artPath")},
        {DurationRole, QByteArrayLiteral("durationMs")},
        {IsCurrentRole, QByteArrayLiteral("isCurrent")},
    };
}

int Queue::currentIndex() const
{
    return m_index;
}

void Queue::notifyCurrent(int oldIndex)
{
    if (oldIndex >= 0 && oldIndex < m_entries.size())
        emit dataChanged(createIndex(oldIndex, 0), createIndex(oldIndex, 0), {IsCurrentRole});
    if (m_index >= 0 && m_index < m_entries.size() && m_index != oldIndex)
        emit dataChanged(createIndex(m_index, 0), createIndex(m_index, 0), {IsCurrentRole});
    emit indexChanged();
}

void Queue::setCurrentIndex(int index)
{
    const int clamped = m_entries.isEmpty() ? -1 : qBound(0, index, static_cast<int>(m_entries.size()) - 1);
    if (clamped == m_index)
        return;
    const int old = m_index;
    m_index = clamped;
    notifyCurrent(old);
}

int Queue::count() const
{
    return static_cast<int>(m_entries.size());
}

bool Queue::shuffle() const
{
    return m_shuffle;
}

void Queue::setShuffle(bool shuffle)
{
    if (m_shuffle == shuffle)
        return;
    m_shuffle = shuffle;
    rebuildOrder();
    emit shuffleChanged();
}

Queue::Repeat Queue::repeat() const
{
    return m_repeat;
}

void Queue::setRepeat(Repeat repeat)
{
    if (m_repeat == repeat)
        return;
    m_repeat = repeat;
    emit repeatChanged();
}

QueueEntry Queue::entry(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return {};
    return m_entries.at(row);
}

QList<QueueEntry> Queue::entries() const
{
    return m_entries;
}

QList<int> Queue::order() const
{
    return m_order;
}

QVariantMap Queue::get(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return {};
    QVariantMap map = m_entries.at(row).toMap();
    map.insert(QStringLiteral("index"), row);
    return map;
}

void Queue::rebuildOrder()
{
    m_order.clear();
    if (!m_shuffle || m_entries.isEmpty())
        return;
    QList<int> rest;
    rest.reserve(m_entries.size());
    for (int row = 0; row < m_entries.size(); ++row) {
        if (row != m_index)
            rest.append(row);
    }
    std::shuffle(rest.begin(), rest.end(), *QRandomGenerator::global());
    if (m_index >= 0)
        m_order.append(m_index);
    m_order.append(rest);
}

void Queue::set(const QList<QueueEntry> &entries, int startIndex)
{
    beginResetModel();
    m_entries = entries;
    m_index = m_entries.isEmpty() ? -1 : qBound(0, startIndex, static_cast<int>(m_entries.size()) - 1);
    endResetModel();
    rebuildOrder();
    emit countChanged();
    emit indexChanged();
}

void Queue::clear()
{
    set({}, -1);
}

void Queue::insertEntries(int at, const QList<QueueEntry> &entries)
{
    beginInsertRows(QModelIndex(), at, at + static_cast<int>(entries.size()) - 1);
    for (int i = 0; i < entries.size(); ++i)
        m_entries.insert(at + i, entries.at(i));
    endInsertRows();
}

void Queue::playNext(const QList<QueueEntry> &entries)
{
    if (entries.isEmpty())
        return;
    const bool wasEmpty = m_entries.isEmpty();
    const int at = m_index < 0 ? static_cast<int>(m_entries.size()) : m_index + 1;
    const int n = static_cast<int>(entries.size());
    insertEntries(at, entries);
    if (wasEmpty) {
        m_index = 0;
        rebuildOrder();
        emit countChanged();
        notifyCurrent(-1);
        return;
    }
    if (m_shuffle) {
        for (int &row : m_order) {
            if (row >= at)
                row += n;
        }
        const int pos = m_order.indexOf(m_index);
        for (int i = 0; i < n; ++i)
            m_order.insert(pos + 1 + i, at + i);
    }
    emit countChanged();
}

void Queue::enqueue(const QList<QueueEntry> &entries)
{
    if (entries.isEmpty())
        return;
    const bool wasEmpty = m_entries.isEmpty();
    const int at = static_cast<int>(m_entries.size());
    const int n = static_cast<int>(entries.size());
    insertEntries(at, entries);
    if (wasEmpty) {
        m_index = 0;
        rebuildOrder();
        emit countChanged();
        notifyCurrent(-1);
        return;
    }
    if (m_shuffle) {
        const int lo = m_order.indexOf(m_index) + 1;
        for (int i = 0; i < n; ++i) {
            const int span = static_cast<int>(m_order.size()) - lo + 1;
            const int slot = lo + QRandomGenerator::global()->bounded(span);
            m_order.insert(slot, at + i);
        }
    }
    emit countChanged();
}

void Queue::remove(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_entries.removeAt(row);
    endRemoveRows();
    m_order.removeOne(row);
    for (int &r : m_order) {
        if (r > row)
            --r;
    }
    const int old = m_index;
    if (row < m_index)
        --m_index;
    else if (row == m_index && m_index >= m_entries.size())
        m_index = static_cast<int>(m_entries.size()) - 1;
    emit countChanged();
    if (m_index != old || row == old)
        notifyCurrent(-1);
}

void Queue::move(int from, int to)
{
    const int n = static_cast<int>(m_entries.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return;
    const int dest = to > from ? to + 1 : to;
    if (!beginMoveRows(QModelIndex(), from, from, QModelIndex(), dest))
        return;
    m_entries.move(from, to);
    const auto remap = [from, to](int row) {
        if (row == from)
            return to;
        if (from < to && row > from && row <= to)
            return row - 1;
        if (to < from && row >= to && row < from)
            return row + 1;
        return row;
    };
    for (int &row : m_order)
        row = remap(row);
    const int index = remap(m_index);
    const bool indexMoved = index != m_index;
    m_index = index;
    endMoveRows();
    if (indexMoved)
        emit indexChanged();
}

int Queue::nextOf(int index, int count, const QList<int> &order, bool shuffle, Repeat repeat,
                  bool manual)
{
    if (count <= 0 || index < 0 || index >= count)
        return -1;
    if (!manual && repeat == One)
        return index;
    const bool wrap = repeat != Off;
    if (shuffle && !order.isEmpty()) {
        const int pos = order.indexOf(index);
        if (pos >= 0 && pos + 1 < order.size())
            return order.at(pos + 1);
        return wrap ? order.first() : -1;
    }
    if (index + 1 < count)
        return index + 1;
    return wrap ? 0 : -1;
}

int Queue::previousOf(int index, int count, const QList<int> &order, bool shuffle, Repeat repeat,
                      bool manual)
{
    if (count <= 0 || index < 0 || index >= count)
        return -1;
    if (!manual && repeat == One)
        return index;
    const bool wrap = repeat != Off;
    if (shuffle && !order.isEmpty()) {
        const int pos = order.indexOf(index);
        if (pos > 0)
            return order.at(pos - 1);
        return wrap ? order.last() : -1;
    }
    if (index > 0)
        return index - 1;
    return wrap ? count - 1 : -1;
}

int Queue::nextIndex(bool manual) const
{
    return nextOf(m_index, count(), m_order, m_shuffle, m_repeat, manual);
}

int Queue::previousIndex(bool manual) const
{
    return previousOf(m_index, count(), m_order, m_shuffle, m_repeat, manual);
}

}
