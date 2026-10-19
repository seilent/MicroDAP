#include "Favorites.h"

#include "Library.h"
#include "Scanner.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariantList>

#include <algorithm>
#include <numeric>
#include <utility>

namespace microdap {

namespace {

const QString internalMedium = QStringLiteral("internal");
const QString migratedKey = QStringLiteral("favorites_migrated:");

QString basePrefix()
{
    return QDir::cleanPath(QDir(Scanner::mediaBase()).absolutePath()) + QLatin1Char('/');
}

}

Favorites::Favorites(Library *library, QObject *parent)
    : QObject(parent)
    , m_library(library)
{
    reload();
}

QList<qsizetype> Favorites::order(const QList<qint64> &seqs)
{
    QList<qsizetype> out(seqs.size());
    std::iota(out.begin(), out.end(), 0);
    std::stable_sort(out.begin(), out.end(), [&seqs](qsizetype a, qsizetype b) {
        const qint64 x = seqs.at(a);
        const qint64 y = seqs.at(b);
        if ((x >= 0) != (y >= 0))
            return x >= 0;
        return x >= 0 && x > y;
    });
    return out;
}

QStringList Favorites::cardMounts()
{
    QStringList mounts;
    const auto dirs = [](const QString &path) {
        return QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    };
    for (const QFileInfo &child : dirs(QDir(Scanner::mediaBase()).absolutePath())) {
        const QString path = QDir::cleanPath(child.absoluteFilePath());
        if (Scanner::isMountPoint(path)) {
            mounts.append(path);
            continue;
        }
        for (const QFileInfo &sub : dirs(path)) {
            if (Scanner::isMountPoint(sub.absoluteFilePath()))
                mounts.append(QDir::cleanPath(sub.absoluteFilePath()));
        }
    }
    return mounts;
}

int Favorites::count() const
{
    return static_cast<int>(m_ids.size());
}

QList<QueueEntry> Favorites::entries() const
{
    QVariantList ids;
    ids.reserve(m_ids.size());
    for (qint64 id : m_ids)
        ids.append(id);
    return m_library->queueEntries(ids);
}

bool Favorites::contains(qint64 trackId) const
{
    return m_set.contains(trackId);
}

QStringList Favorites::media() const
{
    QStringList ids;
    for (const Medium &medium : m_media)
        ids.append(medium.id);
    return ids;
}

QString Favorites::fileFor(const QString &medium) const
{
    for (const Medium &m : m_media) {
        if (m.id == medium)
            return m.file;
    }
    return {};
}

Favorites::Medium *Favorites::mediumOf(const QString &path)
{
    for (Medium &medium : m_media) {
        if (medium.card && path.startsWith(medium.root + QLatin1Char('/')))
            return &medium;
    }
    if (m_media.isEmpty() || path.startsWith(basePrefix()))
        return nullptr;
    return &m_media.first();
}

QString Favorites::key(const Medium &medium, const QString &path) const
{
    const QString rel = QDir(medium.dir).relativeFilePath(path);
    return medium.card ? rel.toCaseFolded() : rel;
}

bool Favorites::sameTrack(const Medium &medium, const QString &a, const QString &b) const
{
    return medium.card ? a.compare(b, Qt::CaseInsensitive) == 0 : a == b;
}

void Favorites::readMedium(Medium &medium)
{
    PlaylistStore::Loaded loaded = m_library->playlistStore().read(medium.file);
    medium.failed = loaded.state == PlaylistStore::State::Failed;
    medium.doc = std::move(loaded.doc);
    if (medium.failed)
        return;
    QSet<QString> keys;
    for (const M3uItem &item : std::as_const(medium.doc.items)) {
        if (item.entry && !item.path.isEmpty())
            keys.insert(Library::favoriteKey(key(medium, item.path), item.cue));
    }
    m_library->keepFavoriteStamps(medium.id, keys);
}

void Favorites::migrate(Medium &medium)
{
    const QString marker = migratedKey + medium.id;
    if (medium.failed || !m_library->isOpen() || !m_library->metaValue(marker).isEmpty())
        return;
    QStringList rated;
    QSet<QString> ratedKeys;
    QSqlQuery query(m_library->database());
    if (!query.exec(QStringLiteral("SELECT t.path FROM tracks t LEFT JOIN (SELECT track_id, MAX(played_at) AS last "
                                   "FROM history GROUP BY track_id) h ON h.track_id = t.id WHERE t.rating = 1 "
                                   "ORDER BY h.last, t.added, t.id")))
        return;
    const QString base = basePrefix();
    while (query.next()) {
        const QString path = query.value(0).toString();
        const bool here = medium.card ? path.startsWith(medium.root + QLatin1Char('/')) : !path.startsWith(base);
        if (!here || ratedKeys.contains(key(medium, path)))
            continue;
        ratedKeys.insert(key(medium, path));
        rated.append(path);
    }
    query.finish();

    const auto present = [this, &medium](const M3u &doc, const QString &path) {
        return std::any_of(doc.items.cbegin(), doc.items.cend(), [this, &medium, &path](const M3uItem &item) {
            return item.entry && sameTrack(medium, item.path, path);
        });
    };
    const bool missing = std::any_of(rated.cbegin(), rated.cend(),
                                     [&present, &medium](const QString &path) { return !present(medium.doc, path); });
    if (missing) {
        const PlaylistStore::Edit result = m_library->playlistStore().edit(medium.file, [&](M3u &doc) {
            for (const QString &path : std::as_const(rated)) {
                if (!present(doc, path))
                    PlaylistStore::insert(doc, 0, path, medium.dir);
            }
            return true;
        });
        if (result != PlaylistStore::Edit::Done) {
            qWarning() << "microdap favorites: migration not written" << medium.file;
            return;
        }
        readMedium(medium);
        if (medium.failed)
            return;
        for (const QString &path : std::as_const(rated)) {
            if (!present(medium.doc, path)) {
                qWarning() << "microdap favorites: migration read back without" << path;
                return;
            }
        }
    }

    const QHash<QString, qint64> stamps = m_library->favoriteStamps(medium.id);
    QList<std::pair<QString, int>> fresh;
    for (const QString &path : std::as_const(rated)) {
        const QString rel = key(medium, path);
        if (!stamps.contains(Library::favoriteKey(rel, 0)))
            fresh.append({rel, 0});
    }
    if (!m_library->stampFavorites(medium.id, fresh))
        return;
    if (m_library->setMetaValue(marker, QStringLiteral("1")))
        qInfo().noquote() << QStringLiteral("microdap favorites: %1 migrated, %2 rated tracks").arg(medium.id).arg(rated.size());
}

void Favorites::reload()
{
    QList<Medium> media;
    Medium internal;
    internal.id = internalMedium;
    internal.root = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
    media.append(internal);
    for (const QString &mount : cardMounts()) {
        Medium card;
        card.id = QFileInfo(mount).fileName();
        card.root = mount;
        card.card = true;
        media.append(card);
    }
    std::stable_sort(media.begin() + 1, media.end(), [](const Medium &a, const Medium &b) { return a.id < b.id; });
    for (Medium &medium : media) {
        medium.dir = PlaylistStore::playlistsDir(medium.root);
        medium.file = PlaylistStore::findFile(medium.dir, QString::fromLatin1(fileName));
        readMedium(medium);
        migrate(medium);
    }
    m_media = std::move(media);
    rebuild();
}

void Favorites::rebuild()
{
    QHash<QString, qint64> exact;
    QHash<QString, QList<qint64>> folded;
    {
        QSqlQuery query(QStringLiteral("SELECT id, path FROM tracks"), m_library->database());
        while (query.next())
            exact.insert(query.value(1).toString(), query.value(0).toLongLong());
    }
    QList<qint64> ids;
    QList<qint64> seqs;
    for (const Medium &medium : std::as_const(m_media)) {
        if (medium.failed)
            continue;
        const QHash<QString, qint64> stamps = m_library->favoriteStamps(medium.id);
        for (const M3uItem &item : medium.doc.items) {
            if (!item.entry || item.path.isEmpty())
                continue;
            qint64 id = exact.value(item.path, -1);
            if (id < 0 && medium.card) {
                if (folded.isEmpty()) {
                    for (auto it = exact.cbegin(); it != exact.cend(); ++it)
                        folded[it.key().toCaseFolded()].append(it.value());
                }
                const QList<qint64> hits = folded.value(item.path.toCaseFolded());
                if (hits.size() == 1)
                    id = hits.first();
            }
            if (id < 0)
                continue;
            ids.append(id);
            seqs.append(stamps.value(Library::favoriteKey(key(medium, item.path), item.cue), -1));
        }
    }
    QList<qint64> ordered;
    QSet<qint64> set;
    for (qsizetype i : order(seqs)) {
        if (!set.contains(ids.at(i))) {
            set.insert(ids.at(i));
            ordered.append(ids.at(i));
        }
    }
    if (ordered == m_ids)
        return;
    m_ids = std::move(ordered);
    m_set = std::move(set);
    emit changed();
}

void Favorites::toggle(qint64 trackId)
{
    const QList<QueueEntry> found = m_library->queueEntries({QVariant(trackId)});
    if (found.isEmpty())
        return;
    const QString path = found.first().path;
    Medium *medium = mediumOf(path);
    if (!medium) {
        emit notice(QStringLiteral("card not available"));
        return;
    }
    if (medium->card && !Scanner::isMountPoint(medium->root)) {
        emit notice(QStringLiteral("card not available"));
        reload();
        return;
    }
    if (medium->failed)
        readMedium(*medium);
    migrate(*medium);
    if (medium->failed || m_library->metaValue(migratedKey + medium->id).isEmpty()) {
        emit notice(QStringLiteral("favorites not readable"));
        return;
    }
    bool had = false;
    const PlaylistStore::Edit result = m_library->playlistStore().edit(medium->file, [&](M3u &doc) {
        for (qsizetype i = doc.items.size() - 1; i >= 0; --i) {
            if (doc.items.at(i).entry && sameTrack(*medium, doc.items.at(i).path, path)) {
                const qsizetype from = PlaylistStore::unit(doc, i);
                PlaylistStore::erase(doc, i);
                i = from;
                had = true;
            }
        }
        if (!had)
            PlaylistStore::insert(doc, 0, path, medium->dir);
        return true;
    });
    if (result == PlaylistStore::Edit::Unreadable) {
        medium->failed = true;
        rebuild();
        emit notice(QStringLiteral("favorites not readable"));
        return;
    }
    if (result == PlaylistStore::Edit::Unwritten) {
        emit notice(QStringLiteral("card not available"));
        return;
    }
    if (!had)
        m_library->stampFavorites(medium->id, {{key(*medium, path), 0}});
    readMedium(*medium);
    rebuild();
    emit notice(had ? QStringLiteral("removed from favorites") : QStringLiteral("added to favorites"));
}

}
