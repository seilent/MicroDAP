#include "HubModels.h"

#include "Library.h"
#include "PathFit.h"
#include "Scanner.h"
#include "SectionKey.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QSqlQuery>
#include <QStandardPaths>

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

FolderModel::FolderModel(Library *library, QObject *parent)
    : QAbstractListModel(parent)
    , m_library(library)
{
    if (library)
        connect(library, &Library::tracksChanged, this, &FolderModel::rebuild);
}

QStringList FolderModel::roots() const
{
    return m_roots;
}

void FolderModel::setRoots(const QStringList &roots)
{
    if (m_roots == roots)
        return;
    m_roots = roots;
    emit rootsChanged();
    rebuild();
}

bool FolderModel::active() const
{
    return m_active;
}

void FolderModel::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    emit activeChanged();
    if (m_active && !m_built)
        rebuild();
}

int FolderModel::count() const
{
    return static_cast<int>(m_rows.size());
}

int FolderModel::folder() const
{
    return m_open;
}

bool FolderModel::folderLess(const QString &a, const QString &b)
{
    const int folded = a.compare(b, Qt::CaseInsensitive);
    return folded != 0 ? folded < 0 : a < b;
}

QString FolderModel::rootLabel(const QString &root)
{
    const QString path = QDir::cleanPath(root);
    if (path == QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::MusicLocation)))
        return QStringLiteral("internal");
    const QString base = QDir::cleanPath(Scanner::mediaBase());
    if (!path.startsWith(base + QLatin1Char('/')))
        return QFileInfo(path).fileName();
    const QStringList parts = path.mid(base.size() + 1).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const bool nested = parts.size() >= 3 && !Scanner::isMountPoint(base + QLatin1Char('/') + parts.first());
    const qsizetype label = nested ? 1 : 0;
    const QString folder = parts.mid(label + 1).join(QLatin1Char('/'));
    return folder.isEmpty() ? parts.at(label) : parts.at(label) + QLatin1Char(' ') + folder;
}

QString FolderModel::fitPath(const QStringList &parts, const Advance &advance, qreal width)
{
    return fitPathParts(parts, advance, width);
}

void FolderModel::rebuild()
{
    if (!m_active) {
        m_built = false;
        return;
    }
    beginResetModel();
    build();
    fillRows();
    endResetModel();
    emit countChanged();
    emit folderChanged();
}

void FolderModel::build()
{
    m_built = true;
    m_dirs.clear();
    m_top.clear();
    QStringList roots;
    for (const QString &root : std::as_const(m_roots)) {
        const QString path = QDir::cleanPath(root);
        if (!path.isEmpty() && !roots.contains(path))
            roots.append(path);
    }
    if (!m_library || !m_library->isOpen() || roots.isEmpty()) {
        m_open = -1;
        return;
    }

    QHash<QString, int> index;
    const std::function<int(const QString &, const QString &)> ensure = [&](const QString &path,
                                                                         const QString &root) {
        if (const auto it = index.constFind(path); it != index.cend())
            return *it;
        Dir dir;
        dir.path = path;
        if (path == root) {
            dir.name = rootLabel(root);
        } else {
            dir.name = QFileInfo(path).fileName();
            dir.parent = ensure(QFileInfo(path).path(), root);
        }
        const int id = static_cast<int>(m_dirs.size());
        m_dirs.append(dir);
        index.insert(path, id);
        if (dir.parent < 0)
            m_top.append(id);
        else
            m_dirs[dir.parent].dirs.append(id);
        return id;
    };

    QSqlQuery query(QStringLiteral("SELECT id, path, title, artist, cue_index FROM tracks"), m_library->database());
    while (query.next()) {
        const QString path = query.value(1).toString();
        QString root;
        for (const QString &candidate : std::as_const(roots)) {
            if (path.startsWith(candidate + QLatin1Char('/')) && candidate.size() > root.size())
                root = candidate;
        }
        if (root.isEmpty())
            continue;
        const QFileInfo info(path);
        const int dir = ensure(info.path(), root);
        m_dirs[dir].songs.append({query.value(0).toLongLong(), info.fileName(), query.value(2).toString(),
                                  query.value(3).toString(), query.value(4).toInt()});
    }

    for (Dir &dir : m_dirs) {
        std::sort(dir.dirs.begin(), dir.dirs.end(),
                  [this](int a, int b) { return folderLess(m_dirs.at(a).name, m_dirs.at(b).name); });
        std::sort(dir.songs.begin(), dir.songs.end(), [](const Song &a, const Song &b) {
            if (folderLess(a.file, b.file))
                return true;
            if (folderLess(b.file, a.file))
                return false;
            return a.cue != b.cue ? a.cue < b.cue : a.id < b.id;
        });
        dir.total = static_cast<int>(dir.songs.size());
    }
    for (qsizetype i = m_dirs.size() - 1; i >= 0; --i) {
        if (m_dirs.at(i).parent >= 0)
            m_dirs[m_dirs.at(i).parent].total += m_dirs.at(i).total;
    }
    std::sort(m_top.begin(), m_top.end(), [this, &roots](int a, int b) {
        return roots.indexOf(m_dirs.at(a).path) < roots.indexOf(m_dirs.at(b).path);
    });

    m_open = -1;
    QString path = m_openPath;
    while (!path.isEmpty()) {
        if (const auto it = index.constFind(path); it != index.cend()) {
            m_open = *it;
            break;
        }
        const QString parent = QFileInfo(path).path();
        if (parent == path)
            break;
        path = parent;
    }
    m_openPath = m_open < 0 ? QString() : m_dirs.at(m_open).path;
}

FolderModel::Row FolderModel::folderRow(int dir) const
{
    const Dir &d = m_dirs.at(dir);
    return {QStringLiteral("folder"), dir, d.name,
            QStringLiteral("%1 %2").arg(d.total).arg(d.total == 1 ? QStringLiteral("song") : QStringLiteral("songs")),
            QString()};
}

void FolderModel::fillRows()
{
    m_rows.clear();
    if (m_open < 0) {
        for (int dir : std::as_const(m_top))
            m_rows.append(folderRow(dir));
        return;
    }
    const Dir &open = m_dirs.at(m_open);
    m_rows.append({QStringLiteral("path"), -1, pathParts().join(QStringLiteral(" / ")), QString(), QString()});
    for (int dir : open.dirs)
        m_rows.append(folderRow(dir));
    for (qsizetype i = 0; i < open.songs.size(); ++i) {
        const Song &song = open.songs.at(i);
        m_rows.append({QStringLiteral("song"), song.id, song.title, song.artist, QString::number(i + 1)});
    }
}

QStringList FolderModel::pathParts() const
{
    QStringList parts;
    for (int dir = m_open; dir >= 0 && dir < m_dirs.size(); dir = m_dirs.at(dir).parent)
        parts.prepend(m_dirs.at(dir).name);
    return parts;
}

void FolderModel::setOpen(int dir)
{
    beginResetModel();
    m_open = dir;
    m_openPath = dir < 0 ? QString() : m_dirs.at(dir).path;
    fillRows();
    endResetModel();
    emit countChanged();
    emit folderChanged();
}

bool FolderModel::openFolder(int id)
{
    if (id < 0 || id >= m_dirs.size() || id == m_open)
        return false;
    setOpen(id);
    return true;
}

int FolderModel::up()
{
    if (m_open < 0)
        return -1;
    const int left = m_open;
    setOpen(m_dirs.at(left).parent);
    for (qsizetype row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row).kind == QLatin1StringView("folder") && m_rows.at(row).id == left)
            return static_cast<int>(row);
    }
    return 0;
}

void FolderModel::collect(int dir, QVariantList &out) const
{
    const Dir &d = m_dirs.at(dir);
    for (int child : d.dirs)
        collect(child, out);
    for (const Song &song : d.songs)
        out.append(song.id);
}

QVariantList FolderModel::tracks(int id) const
{
    QVariantList out;
    if (id >= 0 && id < m_dirs.size())
        collect(id, out);
    return out;
}

QString FolderModel::name(int id) const
{
    return id >= 0 && id < m_dirs.size() ? m_dirs.at(id).name : QString();
}

QString FolderModel::pathLine(const QFont &font, qreal width) const
{
    QFont measured(font);
    measured.setCapitalization(QFont::MixedCase);
    const QFontMetricsF metrics(measured);
    return fitPath(
        pathParts(), [&metrics](const QString &text) { return metrics.horizontalAdvance(text.toUpper()); }, width);
}

int FolderModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant FolderModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case KindRole:
        return row.kind;
    case LetterRole:
    case ArtPathRole:
        return QString();
    case IdRole:
        return row.id;
    case TitleRole:
        return row.title;
    case SubtitleRole:
        return row.subtitle;
    case NumberRole:
        return row.number;
    case SourceRowRole:
        return index.row();
    default:
        return {};
    }
}

QHash<int, QByteArray> FolderModel::roleNames() const
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

QVariantMap FolderModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= m_rows.size())
        return map;
    const Row &r = m_rows.at(row);
    map.insert(QStringLiteral("kind"), r.kind);
    map.insert(QStringLiteral("letter"), QString());
    map.insert(QStringLiteral("id"), r.id);
    map.insert(QStringLiteral("title"), r.title);
    map.insert(QStringLiteral("subtitle"), r.subtitle);
    map.insert(QStringLiteral("artPath"), QString());
    map.insert(QStringLiteral("number"), r.number);
    map.insert(QStringLiteral("sourceRow"), row);
    return map;
}

bool FolderModel::isFocusable(int row) const
{
    return row >= 0 && row < m_rows.size() && m_rows.at(row).kind != QLatin1StringView("path");
}

int FolderModel::nearestFocusable(int from, int dir) const
{
    const int n = count();
    const int step = dir >= 0 ? 1 : -1;
    for (int row = from; row >= 0 && row < n; row += step) {
        if (isFocusable(row))
            return row;
    }
    for (int row = from - step; row >= 0 && row < n; row -= step) {
        if (isFocusable(row))
            return row;
    }
    return -1;
}

}
