#include "LinkLibrary.h"

#include "Favorites.h"
#include "HubModels.h"
#include "Library.h"
#include "LinkProto.h"
#include "Models.h"

namespace microdap {

namespace {

namespace proto = fd::proto;
namespace v = fd::proto::val::VIEW;

bool parseId(const QString &key, qint64 &id)
{
    if (key.isEmpty() || key.size() > 19)
        return false;
    for (QChar c : key) {
        if (!c.isDigit() || c.unicode() > '9')
            return false;
    }
    bool ok = false;
    id = key.toLongLong(&ok);
    return ok;
}

QList<qint64> idList(const QVariantList &ids)
{
    QList<qint64> out;
    out.reserve(ids.size());
    for (const QVariant &id : ids)
        out.push_back(id.toLongLong());
    return out;
}

}

LinkView::LinkView(const LinkSources &sources)
    : m_src(sources)
{
}

uint8_t LinkView::open(uint8_t view, const QString &parent)
{
    m_view = view;
    m_total = 0;
    m_item = -1;
    m_dir = -1;
    m_ids.clear();
    m_tracks.clear();
    m_entries.clear();
    m_titles.clear();
    Library *lib = m_src.library;
    if (!lib || !lib->isOpen())
        return proto::err::noLibrary;
    auto titled = [this, lib] {
        const QVariantList rows = lib->trackRows(m_tracks);
        for (const QVariant &r : rows) {
            const QVariantMap row = r.toMap();
            m_titles.insert(row.value(QStringLiteral("id")).toLongLong(), row.value(QStringLiteral("title")).toString());
        }
        QVariantList kept;
        for (const QVariant &id : std::as_const(m_tracks)) {
            if (m_titles.contains(id.toLongLong()))
                kept.push_back(id);
        }
        m_tracks = kept;
        m_total = quint32(m_tracks.size());
    };
    switch (view) {
    case v::artists:
        if (!m_src.artists)
            return proto::err::noLibrary;
        m_src.artists->ensureLoaded();
        m_total = quint32(m_src.artists->rowCount());
        return parent.isEmpty() ? 0 : proto::err::badValue;
    case v::albums:
        if (!m_src.albums)
            return proto::err::noLibrary;
        m_src.albums->ensureLoaded();
        m_total = quint32(m_src.albums->rowCount());
        return parent.isEmpty() ? 0 : proto::err::badValue;
    case v::songs:
        if (!m_src.songs)
            return proto::err::noLibrary;
        m_src.songs->ensureLoaded();
        m_total = quint32(m_src.songs->rowCount());
        return parent.isEmpty() ? 0 : proto::err::badValue;
    case v::artistAlbums: {
        if (!m_src.artists)
            return proto::err::noLibrary;
        m_src.artists->ensureLoaded();
        for (int i = 0; i < m_src.artists->rowCount() && m_item < 0; ++i) {
            const QVariantMap row = m_src.artists->get(i);
            if (row.value(QStringLiteral("name")).toString() == parent)
                m_item = row.value(QStringLiteral("id")).toLongLong();
        }
        if (m_item < 0)
            return proto::err::staleRev;
        m_ids = idList(lib->artistAlbums(m_item));
        m_tracks = lib->artistTracks(m_item);
        m_total = quint32(m_ids.size());
        return 0;
    }
    case v::albumTracks: {
        qint64 id = 0;
        if (!parseId(parent, id))
            return proto::err::staleRev;
        const QVariantList rows = lib->trackRows({id});
        if (rows.isEmpty())
            return proto::err::staleRev;
        m_item = rows.constFirst().toMap().value(QStringLiteral("albumId")).toLongLong();
        const QVariantList tracks = lib->albumTracks(m_item);
        if (tracks.isEmpty() || tracks.constFirst().toMap().value(QStringLiteral("id")).toLongLong() != id)
            return proto::err::staleRev;
        for (const QVariant &t : tracks) {
            const QVariantMap row = t.toMap();
            const qint64 track = row.value(QStringLiteral("id")).toLongLong();
            m_tracks.push_back(track);
            m_titles.insert(track, row.value(QStringLiteral("title")).toString());
        }
        m_total = quint32(m_tracks.size());
        return 0;
    }
    case v::playlists:
        if (!m_src.playlists)
            return proto::err::noLibrary;
        m_src.playlists->ensureLoaded();
        if (parent.isEmpty()) {
            for (int i = 0; i < m_src.playlists->rowCount(); ++i)
                m_ids.push_back(m_src.playlists->get(i).value(QStringLiteral("id")).toLongLong());
            m_total = quint32(m_ids.size());
            return 0;
        }
        for (int i = 0; i < m_src.playlists->rowCount() && m_item < 0; ++i) {
            const qint64 id = m_src.playlists->get(i).value(QStringLiteral("id")).toLongLong();
            if (lib->playlistInfo(id).value(QStringLiteral("source")).toString() == parent)
                m_item = id;
        }
        if (m_item < 0)
            return proto::err::staleRev;
        m_tracks = lib->playlistTracks(m_item);
        titled();
        return 0;
    case v::folders:
        if (!m_src.folders)
            return parent.isEmpty() ? 0 : proto::err::staleRev;
        m_dir = m_src.folders->find(parent);
        if (m_dir < -1)
            return proto::err::staleRev;
        m_entries = m_src.folders->entries(m_dir);
        m_total = quint32(m_entries.size());
        return 0;
    case v::favorites:
        if (m_src.favorites)
            m_tracks = m_src.favorites->trackIds();
        titled();
        return parent.isEmpty() ? 0 : proto::err::badValue;
    default:
        return proto::err::badValue;
    }
}

quint32 LinkView::total() const
{
    return m_total;
}

LinkRow LinkView::trackRow(qint64 id) const
{
    LinkRow out;
    out.name = m_titles.value(id);
    out.key = QString::number(id);
    out.count = 1;
    out.first = id;
    out.hasFirst = true;
    return out;
}

LinkRow LinkView::groupRow(const QString &name, const QString &key, const QVariantList &ids) const
{
    LinkRow out;
    out.name = name;
    out.key = key;
    out.count = quint32(ids.size());
    out.hasFirst = !ids.isEmpty();
    out.first = out.hasFirst ? ids.constFirst().toLongLong() : 0;
    return out;
}

LinkRow LinkView::albumRow(qint64 albumId) const
{
    QVariantList ids;
    for (const QVariant &t : m_src.library->albumTracks(albumId))
        ids.push_back(t.toMap().value(QStringLiteral("id")));
    LinkRow out = groupRow(m_src.library->albumInfo(albumId).value(QStringLiteral("title")).toString(), QString(), ids);
    out.key = out.hasFirst ? QString::number(out.first) : QString();
    return out;
}

LinkRow LinkView::row(quint32 index) const
{
    if (index >= m_total || !m_src.library)
        return {};
    switch (m_view) {
    case v::artists: {
        const QVariantMap row = m_src.artists->get(int(index));
        const QString name = row.value(QStringLiteral("name")).toString();
        return groupRow(name, name, m_src.library->artistTracks(row.value(QStringLiteral("id")).toLongLong()));
    }
    case v::albums:
        return albumRow(m_src.albums->get(int(index)).value(QStringLiteral("id")).toLongLong());
    case v::songs: {
        const QVariantMap row = m_src.songs->get(int(index));
        LinkRow out = trackRow(row.value(QStringLiteral("id")).toLongLong());
        out.name = row.value(QStringLiteral("title")).toString();
        return out;
    }
    case v::artistAlbums:
        return albumRow(m_ids.at(index));
    case v::playlists:
        if (m_item < 0) {
            const QVariantMap info = m_src.library->playlistInfo(m_ids.at(index));
            return groupRow(info.value(QStringLiteral("name")).toString(), info.value(QStringLiteral("source")).toString(),
                            m_src.library->playlistTracks(m_ids.at(index)));
        }
        return trackRow(m_tracks.at(index).toLongLong());
    case v::folders: {
        const QVariantMap e = m_entries.at(index).toMap();
        if (e.value(QStringLiteral("kind")).toString() == QLatin1StringView("song")) {
            LinkRow out = trackRow(e.value(QStringLiteral("id")).toLongLong());
            out.name = e.value(QStringLiteral("name")).toString();
            return out;
        }
        LinkRow out;
        out.name = e.value(QStringLiteral("name")).toString();
        out.key = e.value(QStringLiteral("path")).toString();
        out.count = e.value(QStringLiteral("count")).toUInt();
        out.hasFirst = e.value(QStringLiteral("first")).isValid();
        out.first = e.value(QStringLiteral("first")).toLongLong();
        return out;
    }
    default:
        return trackRow(m_tracks.at(index).toLongLong());
    }
}

QVariantList LinkView::tracks() const
{
    QVariantList out;
    if (!m_src.library)
        return out;
    switch (m_view) {
    case v::artists:
        for (quint32 i = 0; i < m_total; ++i)
            out.append(m_src.library->artistTracks(m_src.artists->get(int(i)).value(QStringLiteral("id")).toLongLong()));
        return out;
    case v::albums:
        for (quint32 i = 0; i < m_total; ++i) {
            for (const QVariant &t : m_src.library->albumTracks(m_src.albums->get(int(i)).value(QStringLiteral("id")).toLongLong()))
                out.push_back(t.toMap().value(QStringLiteral("id")));
        }
        return out;
    case v::songs:
        for (quint32 i = 0; i < m_total; ++i)
            out.push_back(m_src.songs->get(int(i)).value(QStringLiteral("id")));
        return out;
    case v::playlists:
        return m_item < 0 ? out : m_tracks;
    case v::folders:
        for (const QVariant &e : m_entries) {
            const QVariantMap entry = e.toMap();
            if (entry.value(QStringLiteral("kind")).toString() == QLatin1StringView("song"))
                out.push_back(entry.value(QStringLiteral("id")));
            else
                out.append(m_src.folders->tracks(m_src.folders->find(entry.value(QStringLiteral("path")).toString())));
        }
        return out;
    default:
        return m_tracks;
    }
}

}
