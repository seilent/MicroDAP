#include "Queue.h"

#include <QRandomGenerator>

#include <algorithm>

namespace microdap {

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

PlayOrderModel::PlayOrderModel(Queue *queue)
    : QAbstractListModel(queue)
    , m_queue(queue)
{
}

int PlayOrderModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_queue->slotCount();
}

int PlayOrderModel::count() const
{
    return m_queue->slotCount();
}

QVariant PlayOrderModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    const int slot = index.row();
    const int row = m_queue->rowAt(slot);
    if (row < 0 || row >= m_queue->m_entries.size())
        return {};
    const QueueEntry &entry = m_queue->m_entries.at(row);
    switch (role) {
    case RowRole:
        return row;
    case SlotRole:
        return slot;
    case NumberRole:
        return slot + 1;
    case IdRole:
        return entry.id;
    case TitleRole:
    case Qt::DisplayRole:
        return entry.title;
    case ArtistRole:
        return entry.artist;
    case AlbumRole:
        return entry.album;
    case DurationRole:
        return entry.durationMs;
    case IsCurrentRole:
        return row == m_queue->m_index;
    default:
        return {};
    }
}

QHash<int, QByteArray> PlayOrderModel::roleNames() const
{
    return {
        {RowRole, QByteArrayLiteral("row")},
        {SlotRole, QByteArrayLiteral("slot")},
        {NumberRole, QByteArrayLiteral("number")},
        {IdRole, QByteArrayLiteral("id")},
        {TitleRole, QByteArrayLiteral("title")},
        {ArtistRole, QByteArrayLiteral("artist")},
        {AlbumRole, QByteArrayLiteral("album")},
        {DurationRole, QByteArrayLiteral("durationMs")},
        {IsCurrentRole, QByteArrayLiteral("isCurrent")},
    };
}

QVariantMap PlayOrderModel::get(int slot) const
{
    const int row = m_queue->rowAt(slot);
    if (row < 0)
        return {};
    QVariantMap map = m_queue->m_entries.at(row).toMap();
    map.insert(QStringLiteral("row"), row);
    map.insert(QStringLiteral("slot"), slot);
    map.insert(QStringLiteral("number"), slot + 1);
    map.insert(QStringLiteral("isCurrent"), row == m_queue->m_index);
    return map;
}

void PlayOrderModel::changed(int first, int last, const QList<int> &roles)
{
    const int n = rowCount();
    first = std::max(0, first);
    last = std::min(n - 1, last);
    if (first > last)
        return;
    emit dataChanged(index(first), index(last), roles);
}

Queue::Queue(QObject *parent)
    : QAbstractListModel(parent)
    , m_playOrder(new PlayOrderModel(this))
{
    connect(this, &Queue::countChanged, m_playOrder, &PlayOrderModel::countChanged);
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

PlayOrderModel *Queue::playOrder() const
{
    return m_playOrder;
}

bool Queue::shuffled() const
{
    return m_shuffle && !m_order.isEmpty();
}

int Queue::slotCount() const
{
    return static_cast<int>(shuffled() ? m_order.size() : m_entries.size());
}

int Queue::currentIndex() const
{
    return m_index;
}

void Queue::touchCurrent(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    const QModelIndex at = createIndex(row, 0);
    emit dataChanged(at, at, {IsCurrentRole});
    const int slot = slotOf(row);
    if (slot >= 0)
        m_playOrder->changed(slot, slot, {PlayOrderModel::IsCurrentRole});
}

void Queue::notifyIndex(int oldIndex, int oldSlot)
{
    if (oldIndex != m_index) {
        touchCurrent(oldIndex);
        touchCurrent(m_index);
        emit indexChanged();
    }
    if (currentSlot() != oldSlot)
        emit currentSlotChanged();
}

void Queue::renumber(int first)
{
    m_playOrder->changed(shuffled() ? 0 : first, slotCount() - 1,
                         {PlayOrderModel::RowRole, PlayOrderModel::SlotRole, PlayOrderModel::NumberRole});
}

void Queue::setCurrentIndex(int index)
{
    const int clamped = m_entries.isEmpty() ? -1 : qBound(0, index, static_cast<int>(m_entries.size()) - 1);
    if (clamped == m_index)
        return;
    const int old = m_index;
    const int oldSlot = currentSlot();
    m_index = clamped;
    notifyIndex(old, oldSlot);
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
    const int oldSlot = currentSlot();
    m_playOrder->beginResetModel();
    m_shuffle = shuffle;
    rebuildOrder();
    m_playOrder->endResetModel();
    emit shuffleChanged();
    emit orderChanged();
    if (currentSlot() != oldSlot)
        emit currentSlotChanged();
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

QList<int> Queue::slotIndex() const
{
    return m_slot;
}

bool Queue::isPermutation(const QList<int> &order) const
{
    const int n = static_cast<int>(m_entries.size());
    if (order.size() != n)
        return false;
    QList<bool> seen(n, false);
    for (int row : order) {
        if (row < 0 || row >= n || seen.at(row))
            return false;
        seen[row] = true;
    }
    return true;
}

bool Queue::setOrder(const QList<int> &order)
{
    if (!m_shuffle || m_entries.isEmpty() || !isPermutation(order))
        return false;
    const int oldSlot = currentSlot();
    m_playOrder->beginResetModel();
    m_order = order;
    rebuildSlots();
    m_playOrder->endResetModel();
    emit orderChanged();
    if (currentSlot() != oldSlot)
        emit currentSlotChanged();
    return true;
}

int Queue::currentSlot() const
{
    return slotOf(m_index);
}

QVariantList Queue::orderedRows() const
{
    QVariantList rows;
    const int n = count();
    rows.reserve(n);
    for (int slot = 0; slot < n; ++slot)
        rows.append(rowAt(slot));
    return rows;
}

int Queue::rowAt(int slot) const
{
    if (slot < 0 || slot >= slotCount())
        return -1;
    if (shuffled())
        return m_order.at(slot);
    return slot < count() ? slot : -1;
}

int Queue::slotOf(int row) const
{
    if (row < 0 || row >= count())
        return -1;
    if (shuffled())
        return row < m_slot.size() ? m_slot.at(row) : static_cast<int>(m_order.indexOf(row));
    return row;
}

QVariantList Queue::upcoming(int n) const
{
    QVariantList rows;
    const int total = count();
    const int start = currentSlot();
    if (start < 0 || n <= 0)
        return rows;
    for (int i = 1; i < total && rows.size() < n; ++i) {
        int slot = start + i;
        if (slot >= total) {
            if (m_repeat != All)
                break;
            slot -= total;
        }
        rows.append(rowAt(slot));
    }
    return rows;
}

QVariantMap Queue::get(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return {};
    QVariantMap map = m_entries.at(row).toMap();
    map.insert(QStringLiteral("index"), row);
    return map;
}

void Queue::rebuildSlots()
{
    m_slot.clear();
    if (!shuffled())
        return;
    m_slot.resize(m_entries.size(), -1);
    for (int slot = 0; slot < m_order.size(); ++slot) {
        const int row = m_order.at(slot);
        if (row >= 0 && row < m_slot.size())
            m_slot[row] = slot;
    }
}

void Queue::updateSlots(int first, int last)
{
    if (m_slot.size() != m_entries.size()) {
        rebuildSlots();
        return;
    }
    for (int slot = first; slot <= last; ++slot)
        m_slot[m_order.at(slot)] = slot;
}

void Queue::rebuildOrder()
{
    m_order.clear();
    if (m_shuffle && !m_entries.isEmpty()) {
        QList<int> rest;
        rest.reserve(m_entries.size());
        for (int row = 0; row < m_entries.size(); ++row) {
            if (row != m_index)
                rest.append(row);
        }
        std::shuffle(rest.begin(), rest.end(), *QRandomGenerator::global());
        m_order.reserve(m_entries.size());
        if (m_index >= 0)
            m_order.append(m_index);
        m_order.append(rest);
    }
    rebuildSlots();
}

void Queue::set(const QList<QueueEntry> &entries, int startIndex, const QList<int> &order)
{
    set(entries, startIndex, order, m_shuffle);
}

void Queue::set(const QList<QueueEntry> &entries, int startIndex, const QList<int> &order, bool shuffle)
{
    const int oldCount = count();
    const int oldIndex = m_index;
    const int oldSlot = currentSlot();
    const bool oldShuffle = m_shuffle;
    beginResetModel();
    m_playOrder->beginResetModel();
    m_entries = entries;
    m_index = m_entries.isEmpty() ? -1 : qBound(0, startIndex, static_cast<int>(m_entries.size()) - 1);
    m_shuffle = shuffle;
    if (m_shuffle && !order.isEmpty() && isPermutation(order)) {
        m_order = order;
        rebuildSlots();
    } else {
        rebuildOrder();
    }
    m_playOrder->endResetModel();
    endResetModel();
    if (m_shuffle != oldShuffle)
        emit shuffleChanged();
    if (count() != oldCount)
        emit countChanged();
    if (m_index != oldIndex)
        emit indexChanged();
    if (currentSlot() != oldSlot)
        emit currentSlotChanged();
}

void Queue::clear()
{
    set({}, -1);
}

void Queue::playNext(const QList<QueueEntry> &entries)
{
    if (entries.isEmpty())
        return;
    if (m_entries.isEmpty()) {
        set(entries, 0);
        return;
    }
    const int at = m_index < 0 ? static_cast<int>(m_entries.size()) : m_index + 1;
    const int n = static_cast<int>(entries.size());
    const int oldSlot = currentSlot();
    if (!shuffled()) {
        beginInsertRows(QModelIndex(), at, at + n - 1);
        m_playOrder->beginInsertRows(QModelIndex(), at, at + n - 1);
        for (int i = 0; i < n; ++i)
            m_entries.insert(at + i, entries.at(i));
        m_playOrder->endInsertRows();
        endInsertRows();
        renumber(at + n);
        emit countChanged();
        if (currentSlot() != oldSlot)
            emit currentSlotChanged();
        return;
    }
    const int pos = std::max(0, oldSlot + 1);
    beginInsertRows(QModelIndex(), at, at + n - 1);
    m_playOrder->beginInsertRows(QModelIndex(), pos, pos + n - 1);
    for (int i = 0; i < n; ++i)
        m_entries.insert(at + i, entries.at(i));
    for (int &row : m_order) {
        if (row >= at)
            row += n;
    }
    for (int i = 0; i < n; ++i)
        m_order.insert(pos + i, at + i);
    rebuildSlots();
    m_playOrder->endInsertRows();
    endInsertRows();
    renumber(0);
    emit countChanged();
    emit orderChanged();
    if (currentSlot() != oldSlot)
        emit currentSlotChanged();
}

void Queue::enqueue(const QList<QueueEntry> &entries)
{
    if (entries.isEmpty())
        return;
    if (m_entries.isEmpty()) {
        set(entries, 0);
        return;
    }
    const int at = static_cast<int>(m_entries.size());
    const int n = static_cast<int>(entries.size());
    if (!shuffled()) {
        beginInsertRows(QModelIndex(), at, at + n - 1);
        m_playOrder->beginInsertRows(QModelIndex(), at, at + n - 1);
        m_entries.append(entries);
        m_playOrder->endInsertRows();
        endInsertRows();
        emit countChanged();
        return;
    }
    const int oldSlot = currentSlot();
    beginInsertRows(QModelIndex(), at, at + n - 1);
    m_entries.append(entries);
    endInsertRows();
    const int lo = std::max(0, oldSlot) + 1;
    const bool each = n <= incrementalInsertLimit;
    if (!each)
        m_playOrder->beginResetModel();
    int first = static_cast<int>(m_order.size());
    for (int i = 0; i < n; ++i) {
        const int span = static_cast<int>(m_order.size()) - lo + 1;
        const int slot = lo + QRandomGenerator::global()->bounded(span);
        first = std::min(first, slot);
        if (each)
            m_playOrder->beginInsertRows(QModelIndex(), slot, slot);
        m_order.insert(slot, at + i);
        if (each)
            m_playOrder->endInsertRows();
    }
    rebuildSlots();
    if (each)
        m_playOrder->changed(first, slotCount() - 1, {PlayOrderModel::SlotRole, PlayOrderModel::NumberRole});
    else
        m_playOrder->endResetModel();
    emit countChanged();
    emit orderChanged();
    if (currentSlot() != oldSlot)
        emit currentSlotChanged();
}

void Queue::remove(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    const int oldIndex = m_index;
    const int oldSlot = currentSlot();
    const int slot = slotOf(row);
    const bool wasShuffled = shuffled();
    m_playOrder->beginRemoveRows(QModelIndex(), slot, slot);
    beginRemoveRows(QModelIndex(), row, row);
    m_entries.removeAt(row);
    if (wasShuffled) {
        m_order.removeAt(slot);
        for (int &r : m_order) {
            if (r > row)
                --r;
        }
    }
    if (row < m_index)
        --m_index;
    else if (row == m_index && m_index >= m_entries.size())
        m_index = static_cast<int>(m_entries.size()) - 1;
    rebuildSlots();
    endRemoveRows();
    m_playOrder->endRemoveRows();
    renumber(slot);
    emit countChanged();
    if (row == oldIndex || m_index != oldIndex) {
        touchCurrent(m_index);
        emit indexChanged();
    }
    if (currentSlot() != oldSlot)
        emit currentSlotChanged();
}

void Queue::move(int from, int to)
{
    const int n = static_cast<int>(m_entries.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return;
    const int dest = to > from ? to + 1 : to;
    const bool wasShuffled = shuffled();
    const int oldIndex = m_index;
    const int oldSlot = currentSlot();
    if (!beginMoveRows(QModelIndex(), from, from, QModelIndex(), dest))
        return;
    if (!wasShuffled)
        m_playOrder->beginMoveRows(QModelIndex(), from, from, QModelIndex(), dest);
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
    m_index = remap(m_index);
    rebuildSlots();
    if (!wasShuffled)
        m_playOrder->endMoveRows();
    endMoveRows();
    if (wasShuffled)
        m_playOrder->changed(0, slotCount() - 1, {PlayOrderModel::RowRole});
    else
        m_playOrder->changed(std::min(from, to), std::max(from, to),
                             {PlayOrderModel::RowRole, PlayOrderModel::SlotRole, PlayOrderModel::NumberRole});
    if (wasShuffled)
        emit orderChanged();
    if (m_index != oldIndex)
        emit indexChanged();
    if (currentSlot() != oldSlot)
        emit currentSlotChanged();
}

void Queue::moveOrderSlot(int from, int to)
{
    const int oldSlot = currentSlot();
    const int dest = to > from ? to + 1 : to;
    m_playOrder->beginMoveRows(QModelIndex(), from, from, QModelIndex(), dest);
    m_order.move(from, to);
    updateSlots(std::min(from, to), std::max(from, to));
    m_playOrder->endMoveRows();
    m_playOrder->changed(std::min(from, to), std::max(from, to),
                         {PlayOrderModel::SlotRole, PlayOrderModel::NumberRole});
    emit orderChanged();
    if (currentSlot() != oldSlot)
        emit currentSlotChanged();
}

void Queue::moveSlot(int from, int to)
{
    if (!shuffled()) {
        move(from, to);
        return;
    }
    const int n = static_cast<int>(m_order.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return;
    moveOrderSlot(from, to);
}

void Queue::playNextRow(int row)
{
    if (m_index < 0 || row < 0 || row >= count() || row == m_index)
        return;
    if (!shuffled()) {
        const int to = row < m_index ? m_index : m_index + 1;
        if (to != row)
            move(row, to);
        return;
    }
    const int from = slotOf(row);
    const int current = slotOf(m_index);
    if (from < 0 || current < 0)
        return;
    const int to = from < current ? current : current + 1;
    if (from == to)
        return;
    moveOrderSlot(from, to);
}

namespace {

int positionOf(int index, const QList<int> &order, const QList<int> &slot)
{
    if (slot.size() == order.size() && index >= 0 && index < slot.size())
        return slot.at(index);
    return static_cast<int>(order.indexOf(index));
}

}

int Queue::nextOf(int index, int count, const QList<int> &order, const QList<int> &slot, bool shuffle,
                  Repeat repeat, bool manual)
{
    if (count <= 0 || index < 0 || index >= count)
        return -1;
    if (!manual && repeat == One)
        return index;
    const bool wrap = repeat != Off;
    if (shuffle && !order.isEmpty()) {
        const int pos = positionOf(index, order, slot);
        if (pos >= 0 && pos + 1 < order.size())
            return order.at(pos + 1);
        return wrap ? order.first() : -1;
    }
    if (index + 1 < count)
        return index + 1;
    return wrap ? 0 : -1;
}

int Queue::previousOf(int index, int count, const QList<int> &order, const QList<int> &slot, bool shuffle,
                      Repeat repeat, bool manual)
{
    if (count <= 0 || index < 0 || index >= count)
        return -1;
    if (!manual && repeat == One)
        return index;
    const bool wrap = repeat != Off;
    if (shuffle && !order.isEmpty()) {
        const int pos = positionOf(index, order, slot);
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
    return nextOf(m_index, count(), m_order, m_slot, m_shuffle, m_repeat, manual);
}

int Queue::previousIndex(bool manual) const
{
    return previousOf(m_index, count(), m_order, m_slot, m_shuffle, m_repeat, manual);
}

}
