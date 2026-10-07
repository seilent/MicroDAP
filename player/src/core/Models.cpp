#include "Models.h"

#include "Library.h"
#include "SectionKey.h"

#include <QSqlQuery>

#include <algorithm>
#include <utility>

namespace casdap {

LibraryModel::LibraryModel(Library *library, const QByteArray &titleRole, QObject *parent)
    : QAbstractListModel(parent)
    , m_library(library)
    , m_titleRole(titleRole)
{
    connect(m_library, &Library::tracksChanged, this, &LibraryModel::refresh);
}

Library *LibraryModel::library() const
{
    return m_library;
}

bool LibraryModel::isLoaded() const
{
    return m_loaded;
}

void LibraryModel::ensureLoaded()
{
    if (!m_loaded)
        reload();
}

void LibraryModel::refresh()
{
    if (m_loaded)
        reload();
}

QList<LibraryModel::Entry> &LibraryModel::rows()
{
    return m_entries;
}

int LibraryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant LibraryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case IdRole:
        return entry.id;
    case Qt::DisplayRole:
    case TitleRole:
        return entry.title;
    case SubtitleRole:
        return entry.subtitle;
    case ArtPathRole:
        return entry.artPath;
    case SectionRole:
        return entry.section;
    case KindRole:
        return entry.kind;
    default:
        return {};
    }
}

QHash<int, QByteArray> LibraryModel::roleNames() const
{
    return {
        {IdRole, QByteArrayLiteral("id")},
        {TitleRole, m_titleRole},
        {SubtitleRole, QByteArrayLiteral("subtitle")},
        {ArtPathRole, QByteArrayLiteral("artPath")},
        {SectionRole, QByteArrayLiteral("section")},
        {KindRole, QByteArrayLiteral("kind")},
    };
}

QVariantMap LibraryModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= m_entries.size())
        return map;
    const Entry &entry = m_entries.at(row);
    map.insert(QStringLiteral("id"), entry.id);
    map.insert(QString::fromLatin1(m_titleRole), entry.title);
    map.insert(QStringLiteral("subtitle"), entry.subtitle);
    map.insert(QStringLiteral("artPath"), entry.artPath);
    map.insert(QStringLiteral("section"), entry.section);
    map.insert(QStringLiteral("kind"), entry.kind);
    return map;
}

int LibraryModel::indexOf(qint64 id) const
{
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).id == id)
            return static_cast<int>(i);
    }
    return -1;
}

void LibraryModel::reload()
{
    beginResetModel();
    m_entries = load();
    m_loaded = true;
    endResetModel();
}

void LibraryModel::classify(Entry &entry, bool ignoreThe)
{
    SectionKey key = sectionKey(entry.title, ignoreThe);
    entry.section = key.letter.toUpper();
    entry.sortKey = std::move(key.sortKey);
}

void LibraryModel::sortEntries(QList<Entry> &entries)
{
    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry &a, const Entry &b) { return a.sortKey < b.sortKey; });
}

QString LibraryModel::countLabel(int count, const char *singular, const char *plural)
{
    return QStringLiteral("%1 %2").arg(count).arg(QLatin1StringView(count == 1 ? singular : plural));
}

ArtistsModel::ArtistsModel(Library *library, QObject *parent)
    : LibraryModel(library, QByteArrayLiteral("name"), parent)
{
}

QList<LibraryModel::Entry> ArtistsModel::load() const
{
    QList<Entry> entries;
    QSqlQuery query(QStringLiteral("SELECT ar.id, ar.name, COUNT(DISTINCT t.album_id), "
                                   "COUNT(DISTINCT t.id), MAX(a.art_path) FROM artists ar "
                                   "JOIN track_artists ta ON ta.artist_id = ar.id "
                                   "JOIN tracks t ON t.id = ta.track_id "
                                   "JOIN albums a ON a.id = t.album_id GROUP BY ar.id"),
                    library()->database());
    while (query.next()) {
        Entry entry;
        entry.id = query.value(0).toLongLong();
        entry.title = query.value(1).toString();
        entry.subtitle = countLabel(query.value(2).toInt(), "album", "albums")
                         + QStringLiteral(", ")
                         + countLabel(query.value(3).toInt(), "song", "songs");
        entry.artPath = query.value(4).toString();
        classify(entry, true);
        entries.append(entry);
    }
    sortEntries(entries);
    return entries;
}

AlbumsModel::AlbumsModel(Library *library, QObject *parent)
    : LibraryModel(library, QByteArrayLiteral("title"), parent)
{
}

QList<LibraryModel::Entry> AlbumsModel::load() const
{
    QList<Entry> entries;
    QSqlQuery query(QStringLiteral("SELECT id, title, artist, art_path FROM albums"),
                    library()->database());
    while (query.next()) {
        Entry entry;
        entry.id = query.value(0).toLongLong();
        entry.title = query.value(1).toString();
        entry.subtitle = query.value(2).toString();
        entry.artPath = query.value(3).toString();
        classify(entry, false);
        entries.append(entry);
    }
    sortEntries(entries);
    return entries;
}

SongsModel::SongsModel(Library *library, QObject *parent)
    : LibraryModel(library, QByteArrayLiteral("title"), parent)
{
}

QList<LibraryModel::Entry> SongsModel::load() const
{
    QList<Entry> entries;
    QSqlQuery query(QStringLiteral("SELECT t.id, t.title, t.artist, a.art_path FROM tracks t "
                                   "JOIN albums a ON a.id = t.album_id ORDER BY t.id"),
                    library()->database());
    while (query.next()) {
        Entry entry;
        entry.id = query.value(0).toLongLong();
        entry.title = query.value(1).toString();
        entry.subtitle = query.value(2).toString();
        entry.artPath = query.value(3).toString();
        classify(entry, false);
        entries.append(entry);
    }
    sortEntries(entries);
    return entries;
}

GenresModel::GenresModel(Library *library, QObject *parent)
    : LibraryModel(library, QByteArrayLiteral("name"), parent)
{
}

QList<LibraryModel::Entry> GenresModel::load() const
{
    QList<Entry> entries;
    QSqlQuery query(QStringLiteral("SELECT g.id, g.name, COUNT(t.id) FROM genres g "
                                   "JOIN tracks t ON t.genre = g.name GROUP BY g.id ORDER BY g.id"),
                    library()->database());
    while (query.next()) {
        Entry entry;
        entry.id = query.value(0).toLongLong();
        entry.title = query.value(1).toString();
        entry.subtitle = countLabel(query.value(2).toInt(), "song", "songs");
        classify(entry, false);
        entries.append(entry);
    }
    sortEntries(entries);
    return entries;
}

HistoryModel::HistoryModel(Library *library, QObject *parent)
    : HistoryModel(library, Recent, 0, parent)
{
}

HistoryModel::HistoryModel(Library *library, Mode mode, int limit, QObject *parent)
    : LibraryModel(library, QByteArrayLiteral("title"), parent)
    , m_mode(mode)
    , m_limit(qMax(0, limit))
{
    disconnect(library, &Library::tracksChanged, this, &LibraryModel::refresh);
    connect(library, &Library::tracksChanged, this, [this] {
        if (m_mode == Recent)
            reload();
    });
    connect(library, &Library::albumsChanged, this, [this] {
        if (m_mode == New)
            reload();
    });
    connect(library, &Library::played, this, &HistoryModel::onPlayed);
    reload();
}

HistoryModel::Mode HistoryModel::mode() const
{
    return m_mode;
}

void HistoryModel::setMode(Mode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    emit modeChanged();
    reload();
}

int HistoryModel::limit() const
{
    return m_limit;
}

void HistoryModel::setLimit(int limit)
{
    const int value = qMax(0, limit);
    if (m_limit == value)
        return;
    m_limit = value;
    emit limitChanged();
    reload();
}

bool HistoryModel::readTrack(qint64 trackId, Entry &entry) const
{
    QSqlQuery query(library()->database());
    query.prepare(QStringLiteral("SELECT t.id, t.title, t.artist, a.art_path FROM tracks t "
                                 "JOIN albums a ON a.id = t.album_id WHERE t.id = ?"));
    query.addBindValue(trackId);
    if (!query.exec() || !query.next())
        return false;
    entry.id = query.value(0).toLongLong();
    entry.title = query.value(1).toString();
    entry.subtitle = query.value(2).toString();
    entry.artPath = query.value(3).toString();
    entry.kind = QStringLiteral("song");
    return true;
}

void HistoryModel::onPlayed(qint64 trackId)
{
    if (m_mode != Recent)
        return;
    QList<Entry> &entries = rows();
    const int row = indexOf(trackId);
    if (row == 0)
        return;
    if (row > 0) {
        beginMoveRows(QModelIndex(), row, row, QModelIndex(), 0);
        entries.move(row, 0);
        endMoveRows();
        return;
    }
    Entry entry;
    if (!readTrack(trackId, entry))
        return;
    beginInsertRows(QModelIndex(), 0, 0);
    entries.prepend(entry);
    endInsertRows();
    if (m_limit > 0 && entries.size() > m_limit) {
        beginRemoveRows(QModelIndex(), m_limit, static_cast<int>(entries.size()) - 1);
        entries.resize(m_limit);
        endRemoveRows();
    }
}

QList<LibraryModel::Entry> HistoryModel::load() const
{
    QList<Entry> entries;
    QString sql = m_mode == Recent
        ? QStringLiteral("SELECT t.id, t.title, t.artist, a.art_path, MAX(h.played_at) AS last "
                         "FROM history h JOIN tracks t ON t.id = h.track_id "
                         "JOIN albums a ON a.id = t.album_id GROUP BY t.id ORDER BY last DESC, MAX(h.id) DESC")
        : QStringLiteral("SELECT id, title, artist, art_path FROM albums "
                         "ORDER BY added DESC, id DESC");
    if (m_limit > 0)
        sql += QStringLiteral(" LIMIT %1").arg(m_limit);
    const QString kind = m_mode == Recent ? QStringLiteral("song") : QStringLiteral("album");
    QSqlQuery query(sql, library()->database());
    while (query.next()) {
        Entry entry;
        entry.id = query.value(0).toLongLong();
        entry.title = query.value(1).toString();
        entry.subtitle = query.value(2).toString();
        entry.artPath = query.value(3).toString();
        entry.kind = kind;
        entries.append(entry);
    }
    return entries;
}

PinsModel::PinsModel(Library *library, QObject *parent)
    : LibraryModel(library, QByteArrayLiteral("title"), parent)
{
    connect(library, &Library::pinsChanged, this, &LibraryModel::reload);
    reload();
}

QList<LibraryModel::Entry> PinsModel::load() const
{
    QList<Entry> entries;
    QSqlQuery query(QStringLiteral("SELECT kind, ref_id FROM pins ORDER BY pos, id"),
                    library()->database());
    while (query.next()) {
        Entry entry;
        entry.kind = query.value(0).toString();
        entry.id = query.value(1).toLongLong();
        entry.section = QStringLiteral("pins");
        bool found = false;
        if (entry.kind == QLatin1StringView("album"))
            found = loadAlbum(entry);
        else if (entry.kind == QLatin1StringView("artist"))
            found = loadArtist(entry);
        else if (entry.kind == QLatin1StringView("song"))
            found = loadSong(entry);
        else if (entry.kind == QLatin1StringView("playlist"))
            found = loadPlaylist(entry);
        if (found)
            entries.append(entry);
    }
    return entries;
}

bool PinsModel::loadAlbum(Entry &entry) const
{
    QSqlQuery query(library()->database());
    query.prepare(QStringLiteral("SELECT title, artist, art_path FROM albums WHERE id = ?"));
    query.addBindValue(entry.id);
    if (!query.exec() || !query.next())
        return false;
    entry.title = query.value(0).toString();
    entry.subtitle = query.value(1).toString();
    entry.artPath = query.value(2).toString();
    return true;
}

bool PinsModel::loadArtist(Entry &entry) const
{
    const QVariantMap info = library()->artistInfo(entry.id);
    if (info.isEmpty())
        return false;
    entry.title = info.value(QStringLiteral("name")).toString();
    entry.artPath = info.value(QStringLiteral("artPath")).toString();
    QSqlQuery query(library()->database());
    query.prepare(QStringLiteral("SELECT COUNT(DISTINCT t.album_id) FROM tracks t "
                                 "JOIN track_artists ta ON ta.track_id = t.id WHERE ta.artist_id = ?"));
    query.addBindValue(entry.id);
    const int albums = query.exec() && query.next() ? query.value(0).toInt() : 0;
    entry.subtitle = countLabel(albums, "album", "albums");
    return true;
}

bool PinsModel::loadSong(Entry &entry) const
{
    QSqlQuery query(library()->database());
    query.prepare(QStringLiteral("SELECT t.title, t.artist, a.art_path FROM tracks t "
                                 "JOIN albums a ON a.id = t.album_id WHERE t.id = ?"));
    query.addBindValue(entry.id);
    if (!query.exec() || !query.next())
        return false;
    entry.title = query.value(0).toString();
    entry.subtitle = query.value(1).toString();
    entry.artPath = query.value(2).toString();
    return true;
}

bool PinsModel::loadPlaylist(Entry &entry) const
{
    QSqlQuery query(library()->database());
    query.prepare(QStringLiteral("SELECT p.name, (SELECT COUNT(*) FROM playlist_items i "
                                 "WHERE i.playlist_id = p.id) FROM playlists p WHERE p.id = ?"));
    query.addBindValue(entry.id);
    if (!query.exec() || !query.next())
        return false;
    entry.title = query.value(0).toString();
    entry.subtitle = countLabel(query.value(1).toInt(), "song", "songs");
    return true;
}

PlaylistsModel::PlaylistsModel(Library *library, QObject *parent)
    : LibraryModel(library, QByteArrayLiteral("title"), parent)
{
    connect(library, &Library::playlistsChanged, this, &LibraryModel::reload);
    reload();
}

QList<LibraryModel::Entry> PlaylistsModel::load() const
{
    QList<std::pair<QString, Entry>> rows;
    QSqlQuery query(QStringLiteral("SELECT p.id, p.name, (SELECT COUNT(*) FROM playlist_items i "
                                   "WHERE i.playlist_id = p.id), p.source FROM playlists p"),
                    library()->database());
    while (query.next()) {
        Entry entry;
        entry.id = query.value(0).toLongLong();
        entry.title = query.value(1).toString();
        entry.subtitle = countLabel(query.value(2).toInt(), "song", "songs");
        entry.kind = QStringLiteral("playlist");
        entry.section = QStringLiteral("playlists");
        entry.sortKey = entry.title.toCaseFolded();
        rows.append({query.value(3).toString(), entry});
    }
    std::stable_sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
        if (a.second.sortKey != b.second.sortKey)
            return a.second.sortKey < b.second.sortKey;
        return a.first < b.first;
    });
    QList<Entry> entries;
    for (const auto &row : std::as_const(rows))
        entries.append(row.second);
    return entries;
}

}
