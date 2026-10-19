#pragma once

#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>

namespace microdap {

class AlbumsModel;
class ArtistsModel;
class Favorites;
class FolderModel;
class Library;
class PlaylistsModel;
class SongsModel;

struct LinkSources
{
    QPointer<Library> library;
    QPointer<ArtistsModel> artists;
    QPointer<AlbumsModel> albums;
    QPointer<SongsModel> songs;
    QPointer<PlaylistsModel> playlists;
    QPointer<FolderModel> folders;
    QPointer<Favorites> favorites;
};

struct LinkRow
{
    QString name;
    QString key;
    quint32 count = 0;
    qint64 first = 0;
    bool hasFirst = false;
};

class LinkView
{
public:
    explicit LinkView(const LinkSources &sources);

    uint8_t open(uint8_t view, const QString &parent);
    quint32 total() const;
    LinkRow row(quint32 index) const;
    QVariantList tracks() const;

private:
    LinkRow trackRow(qint64 id) const;
    LinkRow albumRow(qint64 albumId) const;
    LinkRow groupRow(const QString &name, const QString &key, const QVariantList &ids) const;

    LinkSources m_src;
    uint8_t m_view = 0;
    quint32 m_total = 0;
    qint64 m_item = -1;
    int m_dir = -1;
    QList<qint64> m_ids;
    QVariantList m_tracks;
    QVariantList m_entries;
    QHash<qint64, QString> m_titles;
};

}
