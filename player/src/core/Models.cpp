#include "Models.h"

#include "Library.h"

#include <QSqlQuery>

#include <algorithm>

namespace casdap {

LibraryModel::LibraryModel(Library *library, const QByteArray &titleRole, QObject *parent)
    : QAbstractListModel(parent)
    , m_library(library)
    , m_titleRole(titleRole)
{
    connect(m_library, &Library::changed, this, &LibraryModel::reload);
}

Library *LibraryModel::library() const
{
    return m_library;
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
    endResetModel();
}

void LibraryModel::classify(Entry &entry, bool ignoreThe)
{
    QString key = entry.title.trimmed().toLower();
    if (ignoreThe && key.startsWith(QLatin1StringView("the ")))
        key = key.sliced(4).trimmed();

    const char16_t first = key.isEmpty() ? u'\0' : key.at(0).unicode();
    if (first >= u'a' && first <= u'z') {
        entry.section = QString(key.at(0).toUpper());
        entry.sortKey = key;
    } else {
        entry.section = QStringLiteral("#");
        entry.sortKey = key.prepend(QLatin1Char(' '));
    }
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
    reload();
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
    reload();
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
    reload();
}

QList<LibraryModel::Entry> SongsModel::load() const
{
    QList<Entry> entries;
    QSqlQuery query(QStringLiteral("SELECT t.id, t.title, t.artist, a.art_path FROM tracks t "
                                   "JOIN albums a ON a.id = t.album_id"),
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
    reload();
}

QList<LibraryModel::Entry> GenresModel::load() const
{
    QList<Entry> entries;
    QSqlQuery query(QStringLiteral("SELECT g.id, g.name, COUNT(t.id) FROM genres g "
                                   "JOIN tracks t ON t.genre = g.name GROUP BY g.id"),
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
    : LibraryModel(library, QByteArrayLiteral("title"), parent)
{
    connect(library, &Library::historyChanged, this, &LibraryModel::reload);
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

QList<LibraryModel::Entry> HistoryModel::load() const
{
    QList<Entry> entries;
    const QString sql = m_mode == Recent
        ? QStringLiteral("SELECT t.id, t.title, t.artist, a.art_path, MAX(h.played_at) AS last "
                         "FROM history h JOIN tracks t ON t.id = h.track_id "
                         "JOIN albums a ON a.id = t.album_id GROUP BY t.id ORDER BY last DESC")
        : QStringLiteral("SELECT id, title, artist, art_path FROM albums "
                         "ORDER BY added DESC, id DESC");
    QSqlQuery query(sql, library()->database());
    while (query.next()) {
        Entry entry;
        entry.id = query.value(0).toLongLong();
        entry.title = query.value(1).toString();
        entry.subtitle = query.value(2).toString();
        entry.artPath = query.value(3).toString();
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

}
