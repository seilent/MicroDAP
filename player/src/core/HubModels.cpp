#include "HubModels.h"

#include "SectionKey.h"

#include <algorithm>
#include <utility>

namespace microdap {

void HubSource::attach(QAbstractItemModel *model)
{
    m_model = model;
    m_idRole = -1;
    m_titleRole = -1;
    m_nameRole = -1;
    m_subtitleRole = -1;
    m_artPathRole = -1;
    m_sectionRole = -1;
    if (!model)
        return;
    const QHash<int, QByteArray> names = model->roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        const QByteArray &name = it.value();
        if (name == "id")
            m_idRole = it.key();
        else if (name == "title")
            m_titleRole = it.key();
        else if (name == "name")
            m_nameRole = it.key();
        else if (name == "subtitle")
            m_subtitleRole = it.key();
        else if (name == "artPath")
            m_artPathRole = it.key();
        else if (name == "section")
            m_sectionRole = it.key();
    }
}

QAbstractItemModel *HubSource::model() const
{
    return m_model.data();
}

int HubSource::count() const
{
    return m_model ? m_model->rowCount() : 0;
}

QVariant HubSource::value(int row, int role) const
{
    if (!m_model || role < 0)
        return {};
    return m_model->data(m_model->index(row, 0), role);
}

QVariant HubSource::id(int row) const
{
    const QVariant v = value(row, m_idRole);
    return v.isValid() ? v : QVariant(qint64(-1));
}

QString HubSource::title(int row) const
{
    const QVariant v = value(row, m_titleRole);
    if (v.isValid() && !v.isNull())
        return v.toString();
    return value(row, m_nameRole).toString();
}

QString HubSource::subtitle(int row) const
{
    return value(row, m_subtitleRole).toString();
}

QString HubSource::artPath(int row) const
{
    return value(row, m_artPathRole).toString();
}

QString HubSource::letter(int row) const
{
    const QString section = value(row, m_sectionRole).toString();
    if (!section.isEmpty())
        return section.toLower();
    return letterOf(title(row));
}

void HubSource::ensureLoaded() const
{
    if (!m_model || m_model->metaObject()->indexOfMethod("ensureLoaded()") < 0)
        return;
    QMetaObject::invokeMethod(m_model.data(), "ensureLoaded", Qt::DirectConnection);
}

QString HubSource::letterOf(const QString &title)
{
    return sectionKey(title).letter;
}

SectionedModel::SectionedModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

QAbstractItemModel *SectionedModel::source() const
{
    return m_source.model();
}

void SectionedModel::setSource(QAbstractItemModel *source)
{
    if (m_source.model() == source)
        return;
    for (const QMetaObject::Connection &c : std::as_const(m_connections))
        disconnect(c);
    m_connections.clear();
    m_source.attach(source);
    connectSource();
    emit sourceChanged();
    if (m_active)
        m_source.ensureLoaded();
    rebuild();
}

QString SectionedModel::kind() const
{
    return m_kind;
}

void SectionedModel::setKind(const QString &kind)
{
    if (m_kind == kind)
        return;
    m_kind = kind;
    emit kindChanged();
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1), {KindRole});
}

bool SectionedModel::active() const
{
    return m_active;
}

void SectionedModel::setActive(bool active)
{
    if (m_active == active)
        return;
    if (active)
        m_source.ensureLoaded();
    m_active = active;
    emit activeChanged();
    rebuild();
}

int SectionedModel::count() const
{
    return static_cast<int>(m_rows.size());
}

void SectionedModel::connectSource()
{
    QAbstractItemModel *model = m_source.model();
    if (!model)
        return;
    m_connections << connect(model, &QAbstractItemModel::modelReset, this, &SectionedModel::rebuild)
                  << connect(model, &QAbstractItemModel::layoutChanged, this, &SectionedModel::rebuild)
                  << connect(model, &QAbstractItemModel::rowsMoved, this, &SectionedModel::rebuild)
                  << connect(model, &QAbstractItemModel::rowsInserted, this, &SectionedModel::onRowsInserted)
                  << connect(model, &QAbstractItemModel::rowsRemoved, this, &SectionedModel::onRowsRemoved)
                  << connect(model, &QAbstractItemModel::dataChanged, this, &SectionedModel::onDataChanged)
                  << connect(model, &QObject::destroyed, this, [this] {
                         m_connections.clear();
                         m_source.attach(nullptr);
                         rebuild();
                     });
}

void SectionedModel::build()
{
    m_rows.clear();
    m_groups.clear();
    m_letters.clear();
    m_letterGroup.clear();
    m_sourceToRow.clear();
    if (!m_active || !m_source.model())
        return;
    const int n = m_source.count();
    m_rows.reserve(n + sectionLetters().size());
    m_sourceToRow.resize(n);
    for (int r = 0; r < n; ++r) {
        QString letter = m_source.letter(r);
        if (m_groups.isEmpty() || m_groups.constLast().letter != letter) {
            const int created = static_cast<int>(m_groups.size());
            m_letters.append(letter);
            if (!m_letterGroup.contains(letter))
                m_letterGroup.insert(letter, created);
            m_groups.append({std::move(letter), static_cast<int>(m_rows.size()), 0});
            m_rows.append({-1, created});
        }
        const int g = static_cast<int>(m_groups.size()) - 1;
        ++m_groups[g].size;
        m_sourceToRow[r] = static_cast<int>(m_rows.size());
        m_rows.append({r, g});
    }
}

void SectionedModel::rebuild()
{
    beginResetModel();
    build();
    endResetModel();
    if (m_lastCount != count()) {
        m_lastCount = count();
        emit countChanged();
    }
}

void SectionedModel::reindex()
{
    m_sourceToRow.resize(m_source.count());
    for (qsizetype i = 0; i < m_rows.size(); ++i) {
        const Row &row = m_rows.at(i);
        if (row.source < 0)
            m_groups[row.group].divider = static_cast<int>(i);
        else if (row.source < m_sourceToRow.size())
            m_sourceToRow[row.source] = static_cast<int>(i);
    }
}

void SectionedModel::emitNumbersFrom(int row)
{
    if (row >= 0 && row < m_rows.size())
        emit dataChanged(index(row), index(static_cast<int>(m_rows.size()) - 1), {NumberRole, SourceRowRole});
}

void SectionedModel::onRowsInserted(const QModelIndex &parent, int first, int last)
{
    if (!m_active || parent.isValid())
        return;
    const int added = last - first + 1;
    const int oldCount = static_cast<int>(m_sourceToRow.size());
    if (m_rows.isEmpty() || first > oldCount) {
        rebuild();
        return;
    }
    QStringList incoming;
    incoming.reserve(added);
    for (int r = first; r <= last; ++r)
        incoming.append(m_source.letter(r));
    const auto all = [&incoming](const QString &letter) {
        return std::all_of(incoming.cbegin(), incoming.cend(), [&letter](const QString &l) { return l == letter; });
    };
    const int prevGroup = first > 0 ? m_rows.at(m_sourceToRow.at(first - 1)).group : -1;
    const int nextGroup = first < oldCount ? m_rows.at(m_sourceToRow.at(first)).group : -1;
    int at = -1;
    int group = -1;
    if (prevGroup >= 0 && all(m_groups.at(prevGroup).letter)) {
        at = m_sourceToRow.at(first - 1) + 1;
        group = prevGroup;
    } else if (nextGroup >= 0 && nextGroup != prevGroup && all(m_groups.at(nextGroup).letter)) {
        at = m_sourceToRow.at(first);
        group = nextGroup;
    } else {
        rebuild();
        return;
    }
    beginInsertRows(QModelIndex(), at, at + added - 1);
    for (Row &row : m_rows) {
        if (row.source >= first)
            row.source += added;
    }
    QList<Row> inserted;
    inserted.reserve(added);
    for (int k = 0; k < added; ++k)
        inserted.append({first + k, group});
    m_rows.insert(at, added, Row());
    std::copy(inserted.cbegin(), inserted.cend(), m_rows.begin() + at);
    m_groups[group].size += added;
    reindex();
    endInsertRows();
    emitNumbersFrom(at + added);
    m_lastCount = count();
    emit countChanged();
}

void SectionedModel::onRowsRemoved(const QModelIndex &parent, int first, int last)
{
    if (!m_active || parent.isValid())
        return;
    const int removed = last - first + 1;
    if (last >= m_sourceToRow.size()) {
        rebuild();
        return;
    }
    const int from = m_sourceToRow.at(first);
    const int to = m_sourceToRow.at(last);
    const int group = m_rows.at(from).group;
    if (m_rows.at(to).group != group || to - from + 1 != removed || m_groups.at(group).size <= removed) {
        rebuild();
        return;
    }
    beginRemoveRows(QModelIndex(), from, to);
    m_rows.remove(from, removed);
    for (Row &row : m_rows) {
        if (row.source > last)
            row.source -= removed;
    }
    m_groups[group].size -= removed;
    reindex();
    endRemoveRows();
    emitNumbersFrom(from);
    m_lastCount = count();
    emit countChanged();
}

void SectionedModel::onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight)
{
    if (!m_active || topLeft.parent().isValid())
        return;
    const int top = topLeft.row();
    const int bottom = bottomRight.row();
    if (top < 0 || bottom >= m_sourceToRow.size()) {
        rebuild();
        return;
    }
    for (int r = top; r <= bottom; ++r) {
        if (m_source.letter(r) != m_groups.at(m_rows.at(m_sourceToRow.at(r)).group).letter) {
            rebuild();
            return;
        }
    }
    emit dataChanged(index(m_sourceToRow.at(top)), index(m_sourceToRow.at(bottom)));
}

int SectionedModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant SectionedModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    const QString &letter = m_groups.at(row.group).letter;
    const bool divider = row.source < 0;
    switch (role) {
    case KindRole:
        return divider ? QStringLiteral("divider") : m_kind;
    case LetterRole:
        return letter;
    case IdRole:
        return divider ? QVariant(qint64(-1)) : m_source.id(row.source);
    case Qt::DisplayRole:
    case TitleRole:
        return divider ? letter : m_source.title(row.source);
    case SubtitleRole:
        return divider ? QString() : m_source.subtitle(row.source);
    case ArtPathRole:
        return divider ? QString() : m_source.artPath(row.source);
    case NumberRole:
        return divider ? QString() : QString::number(row.source + 1);
    case SourceRowRole:
        return row.source;
    default:
        return {};
    }
}

QHash<int, QByteArray> SectionedModel::roleNames() const
{
    return {
        {KindRole, QByteArrayLiteral("kind")},
        {LetterRole, QByteArrayLiteral("letter")},
        {IdRole, QByteArrayLiteral("id")},
        {TitleRole, QByteArrayLiteral("title")},
        {SubtitleRole, QByteArrayLiteral("subtitle")},
        {ArtPathRole, QByteArrayLiteral("artPath")},
        {NumberRole, QByteArrayLiteral("number")},
        {SourceRowRole, QByteArrayLiteral("sourceRow")},
    };
}

QVariantMap SectionedModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= m_rows.size())
        return map;
    const Row &r = m_rows.at(row);
    const QString &letter = m_groups.at(r.group).letter;
    map.insert(QStringLiteral("letter"), letter);
    map.insert(QStringLiteral("sourceRow"), r.source);
    if (r.source < 0) {
        map.insert(QStringLiteral("kind"), QStringLiteral("divider"));
        map.insert(QStringLiteral("id"), qint64(-1));
        map.insert(QStringLiteral("title"), letter);
        map.insert(QStringLiteral("subtitle"), QString());
        map.insert(QStringLiteral("artPath"), QString());
        return map;
    }
    map.insert(QStringLiteral("kind"), m_kind);
    map.insert(QStringLiteral("id"), m_source.id(r.source));
    map.insert(QStringLiteral("title"), m_source.title(r.source));
    map.insert(QStringLiteral("subtitle"), m_source.subtitle(r.source));
    map.insert(QStringLiteral("artPath"), m_source.artPath(r.source));
    map.insert(QStringLiteral("row"), r.source);
    map.insert(QStringLiteral("number"), QString::number(r.source + 1));
    return map;
}

bool SectionedModel::isFocusable(int row) const
{
    return row >= 0 && row < m_rows.size();
}

int SectionedModel::nearestFocusable(int from, int dir) const
{
    const int n = count();
    if (n <= 0)
        return -1;
    if (from >= 0 && from < n)
        return from;
    const int back = from - (dir >= 0 ? 1 : -1);
    return back >= 0 && back < n ? back : -1;
}

QStringList SectionedModel::letters() const
{
    return m_letters;
}

int SectionedModel::rowOfLetter(const QString &letter) const
{
    const auto it = m_letterGroup.constFind(letter);
    return it == m_letterGroup.cend() ? -1 : m_groups.at(it.value()).divider;
}

int SectionedModel::firstRowOfLetter(const QString &letter) const
{
    const int divider = rowOfLetter(letter);
    return divider < 0 ? -1 : nearestFocusable(divider + 1, 1);
}

int SectionedModel::rowForSource(int sourceRow) const
{
    return sourceRow >= 0 && sourceRow < m_sourceToRow.size() ? m_sourceToRow.at(sourceRow) : -1;
}

AlbumGridModel::AlbumGridModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

QAbstractItemModel *AlbumGridModel::source() const
{
    return m_source.model();
}

void AlbumGridModel::setSource(QAbstractItemModel *source)
{
    if (m_source.model() == source)
        return;
    for (const QMetaObject::Connection &c : std::as_const(m_connections))
        disconnect(c);
    m_connections.clear();
    m_source.attach(source);
    connectSource();
    emit sourceChanged();
    if (m_active)
        m_source.ensureLoaded();
    rebuild();
}

int AlbumGridModel::columns() const
{
    return m_columns;
}

void AlbumGridModel::setColumns(int columns)
{
    columns = std::max(1, columns);
    if (m_columns == columns)
        return;
    m_columns = columns;
    emit columnsChanged();
    rebuild();
}

bool AlbumGridModel::active() const
{
    return m_active;
}

void AlbumGridModel::setActive(bool active)
{
    if (m_active == active)
        return;
    if (active)
        m_source.ensureLoaded();
    m_active = active;
    emit activeChanged();
    rebuild();
}

int AlbumGridModel::count() const
{
    return gridRows();
}

int AlbumGridModel::gridRows() const
{
    return static_cast<int>((m_cells.size() + m_columns - 1) / m_columns);
}

void AlbumGridModel::connectSource()
{
    QAbstractItemModel *model = m_source.model();
    if (!model)
        return;
    m_connections << connect(model, &QAbstractItemModel::modelReset, this, &AlbumGridModel::rebuild)
                  << connect(model, &QAbstractItemModel::layoutChanged, this, &AlbumGridModel::rebuild)
                  << connect(model, &QAbstractItemModel::rowsMoved, this, &AlbumGridModel::rebuild)
                  << connect(model, &QAbstractItemModel::rowsInserted, this, &AlbumGridModel::rebuild)
                  << connect(model, &QAbstractItemModel::rowsRemoved, this, &AlbumGridModel::rebuild)
                  << connect(model, &QAbstractItemModel::dataChanged, this, &AlbumGridModel::onDataChanged)
                  << connect(model, &QObject::destroyed, this, [this] {
                         m_connections.clear();
                         m_source.attach(nullptr);
                         rebuild();
                     });
}

void AlbumGridModel::build()
{
    m_cells.clear();
    m_letters.clear();
    m_letterCell.clear();
    m_sourceToCell.clear();
    m_idToCell.clear();
    if (!m_active || !m_source.model())
        return;
    const int n = m_source.count();
    m_cells.reserve(n + sectionLetters().size());
    m_sourceToCell.resize(n);
    m_idToCell.reserve(n);
    for (int r = 0; r < n; ++r) {
        QString letter = m_source.letter(r);
        if (m_cells.isEmpty() || m_cells.constLast().letter != letter) {
            m_letters.append(letter);
            if (!m_letterCell.contains(letter))
                m_letterCell.insert(letter, static_cast<int>(m_cells.size()));
            m_cells.append({-1, letter});
        }
        const int cell = static_cast<int>(m_cells.size());
        m_sourceToCell[r] = cell;
        m_idToCell.insert(m_source.id(r).toLongLong(), cell);
        m_cells.append({r, std::move(letter)});
    }
}

void AlbumGridModel::rebuild()
{
    beginResetModel();
    build();
    endResetModel();
    if (m_lastCount != count()) {
        m_lastCount = count();
        emit countChanged();
    }
}

void AlbumGridModel::onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight)
{
    if (!m_active || topLeft.parent().isValid())
        return;
    const int top = topLeft.row();
    const int bottom = bottomRight.row();
    if (top < 0 || bottom >= m_sourceToCell.size()) {
        rebuild();
        return;
    }
    for (int r = top; r <= bottom; ++r) {
        if (m_source.letter(r) != m_cells.at(m_sourceToCell.at(r)).letter) {
            rebuild();
            return;
        }
    }
    m_idToCell.clear();
    for (int r = 0; r < m_sourceToCell.size(); ++r)
        m_idToCell.insert(m_source.id(r).toLongLong(), m_sourceToCell.at(r));
    emit dataChanged(index(m_sourceToCell.at(top) / m_columns), index(m_sourceToCell.at(bottom) / m_columns),
                     {CellsRole});
}

int AlbumGridModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant AlbumGridModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count() || role != CellsRole)
        return {};
    return cells(index.row());
}

QHash<int, QByteArray> AlbumGridModel::roleNames() const
{
    return {
        {CellsRole, QByteArrayLiteral("cells")},
    };
}

QVariantMap AlbumGridModel::cellMap(int cell) const
{
    const Cell &c = m_cells.at(cell);
    QVariantMap map;
    map.insert(QStringLiteral("letter"), c.letter);
    if (c.source < 0) {
        map.insert(QStringLiteral("kind"), QStringLiteral("letter"));
        map.insert(QStringLiteral("id"), qint64(-1));
        map.insert(QStringLiteral("title"), c.letter);
        map.insert(QStringLiteral("subtitle"), QString());
        map.insert(QStringLiteral("artPath"), QString());
        return map;
    }
    map.insert(QStringLiteral("kind"), QStringLiteral("album"));
    map.insert(QStringLiteral("id"), m_source.id(c.source));
    map.insert(QStringLiteral("title"), m_source.title(c.source));
    map.insert(QStringLiteral("subtitle"), m_source.subtitle(c.source));
    map.insert(QStringLiteral("artPath"), m_source.artPath(c.source));
    map.insert(QStringLiteral("row"), c.source);
    map.insert(QStringLiteral("number"), QString::number(c.source + 1));
    return map;
}

QVariantMap AlbumGridModel::position(int cell) const
{
    QVariantMap map;
    map.insert(QStringLiteral("row"), cell < 0 ? -1 : cell / m_columns);
    map.insert(QStringLiteral("col"), cell < 0 ? -1 : cell % m_columns);
    return map;
}

QVariantList AlbumGridModel::cells(int row) const
{
    QVariantList out;
    const int n = cellCount(row);
    out.reserve(n);
    for (int c = 0; c < n; ++c)
        out.append(cellMap(row * m_columns + c));
    return out;
}

QVariantMap AlbumGridModel::cellAt(int row, int col) const
{
    if (col < 0 || col >= cellCount(row))
        return {};
    return cellMap(row * m_columns + col);
}

int AlbumGridModel::cellCount(int row) const
{
    if (row < 0 || row >= count())
        return 0;
    return std::min(m_columns, static_cast<int>(m_cells.size()) - row * m_columns);
}

QStringList AlbumGridModel::letters() const
{
    return m_letters;
}

QVariantMap AlbumGridModel::cellOfLetter(const QString &letter) const
{
    return position(m_letterCell.value(letter, -1));
}

int AlbumGridModel::rowOfLetter(const QString &letter) const
{
    return cellOfLetter(letter).value(QStringLiteral("row")).toInt();
}

QVariantMap AlbumGridModel::cellOfAlbum(qint64 id) const
{
    return position(m_idToCell.value(id, -1));
}

int AlbumGridModel::rowOfAlbum(qint64 id) const
{
    const int cell = m_idToCell.value(id, -1);
    return cell < 0 ? -1 : cell / m_columns;
}

}
