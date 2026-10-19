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

QString entryTail(const M3uItem &item)
{
    const QByteArray text = item.text.trimmed();
    return Scanner::playlistTail(QString::fromUtf8(text.mid(text.lastIndexOf('\n') + 1)).trimmed());
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

QList<qint64> Favorites::resolve(const Medium &medium, const M3u &doc) const
{
    QHash<QString, qint64> exact;
    {
        QSqlQuery query(QStringLiteral("SELECT id, path FROM tracks"), m_library->database());
        while (query.next())
            exact.insert(query.value(1).toString(), query.value(0).toLongLong());
    }
    QHash<QString, QList<qint64>> folded;
    QList<qint64> ids(doc.items.size(), -1);
    QList<qsizetype> missed;
    QStringList tails;
    for (qsizetype i = 0; i < doc.items.size(); ++i) {
        const M3uItem &item = doc.items.at(i);
        if (!item.entry || item.path.isEmpty())
            continue;
        ids[i] = exact.value(item.path, -1);
        if (ids.at(i) < 0 && medium.card) {
            if (folded.isEmpty()) {
                for (auto it = exact.cbegin(); it != exact.cend(); ++it)
                    folded[it.key().toCaseFolded()].append(it.value());
            }
            const QList<qint64> hits = folded.value(item.path.toCaseFolded());
            if (hits.size() == 1)
                ids[i] = hits.first();
        }
        if (ids.at(i) < 0) {
            const QString tail = entryTail(item);
            if (!tail.isEmpty()) {
                missed.append(i);
                tails.append(tail);
            }
        }
    }
    if (!missed.isEmpty()) {
        const QList<qint64> found = m_library->tailTracks(medium.dir, tails);
        for (qsizetype k = 0; k < missed.size(); ++k)
            ids[missed.at(k)] = found.at(k);
    }
    return ids;
}

QList<Favorites::Slot> Favorites::ordered() const
{
    QList<Slot> all;
    QList<qint64> seqs;
    for (const Medium &medium : std::as_const(m_media)) {
        if (medium.failed)
            continue;
        const QHash<QString, qint64> stamps = m_library->favoriteStamps(medium.id);
        const QList<qint64> resolved = resolve(medium, medium.doc);
        for (qsizetype i = 0; i < resolved.size(); ++i) {
            if (resolved.at(i) < 0)
                continue;
            const M3uItem &item = medium.doc.items.at(i);
            const QString k = key(medium, item.path);
            all.append({resolved.at(i), medium.id, k, item.path, item.cue, i});
            seqs.append(stamps.value(Library::favoriteKey(k, item.cue), -1));
        }
    }
    QList<Slot> out;
    QSet<qint64> seen;
    for (qsizetype i : order(seqs)) {
        if (!seen.contains(all.at(i).id)) {
            seen.insert(all.at(i).id);
            out.append(all.at(i));
        }
    }
    return out;
}

Favorites::Medium *Favorites::find(const QString &id)
{
    for (Medium &medium : m_media) {
        if (medium.id == id)
            return &medium;
    }
    return nullptr;
}

void Favorites::rebuild()
{
    QList<qint64> ids;
    QSet<qint64> set;
    for (const Slot &slot : ordered()) {
        ids.append(slot.id);
        set.insert(slot.id);
    }
    if (ids == m_ids)
        return;
    m_ids = std::move(ids);
    m_set = std::move(set);
    emit changed();
}

QVariantList Favorites::trackIds() const
{
    QVariantList out;
    out.reserve(m_ids.size());
    for (qint64 id : m_ids)
        out.append(id);
    return out;
}

QString Favorites::move(int row, int delta)
{
    QList<Slot> list = ordered();
    const int to = row + delta;
    if (row < 0 || row >= list.size() || to < 0 || to >= list.size() || delta == 0)
        return {};
    for (const int at : {row, to}) {
        Medium *medium = find(list.at(at).medium);
        if (!medium)
            return QStringLiteral("card not available");
        const QString refused = ready(*medium);
        if (!refused.isEmpty())
            return refused;
    }
    const Slot a = list.at(row);
    const Slot b = list.at(to);
    if (a.medium == b.medium) {
        Medium *medium = find(a.medium);
        bool stale = false;
        const PlaylistStore::Edit result = m_library->playlistStore().edit(medium->file, [&](M3u &doc) {
            const auto same = [&](const Slot &slot) {
                if (slot.index < 0 || slot.index >= doc.items.size())
                    return false;
                const M3uItem &item = doc.items.at(slot.index);
                return item.entry && item.cue == slot.cue && key(*medium, item.path) == slot.key;
            };
            if (!same(a) || !same(b)) {
                stale = true;
                return false;
            }
            PlaylistStore::swap(doc, a.index, b.index);
            return true;
        });
        if (result == PlaylistStore::Edit::Unreadable) {
            medium->failed = true;
            rebuild();
            return QStringLiteral("favorites not readable");
        }
        if (result == PlaylistStore::Edit::Unwritten)
            return QStringLiteral("card not available");
        readMedium(*medium);
        if (stale) {
            rebuild();
            return QStringLiteral("favorites not readable");
        }
    }
    list.swapItemsAt(row, to);
    bool stamped = true;
    for (qsizetype end = list.size(); end > 0;) {
        const QString medium = list.at(end - 1).medium;
        QList<std::pair<QString, int>> run;
        while (end > 0 && list.at(end - 1).medium == medium) {
            run.append({list.at(end - 1).key, list.at(end - 1).cue});
            --end;
        }
        stamped = m_library->stampFavorites(medium, run) && stamped;
    }
    rebuild();
    if (!stamped) {
        qWarning() << "microdap favorites: move order not stored";
        return QStringLiteral("favorites not readable");
    }
    return {};
}

QString Favorites::ready(Medium &medium)
{
    if (medium.card && !Scanner::isMountPoint(medium.root)) {
        reload();
        return QStringLiteral("card not available");
    }
    if (medium.failed)
        readMedium(medium);
    migrate(medium);
    if (medium.failed || m_library->metaValue(migratedKey + medium.id).isEmpty())
        return QStringLiteral("favorites not readable");
    return {};
}

QString Favorites::add(const QVariantList &trackIds)
{
    const QList<QueueEntry> found = m_library->queueEntries(trackIds);
    if (found.isEmpty())
        return {};
    QStringList order;
    QHash<QString, QList<QueueEntry>> byMedium;
    for (const QueueEntry &entry : found) {
        Medium *medium = mediumOf(entry.path);
        if (!medium)
            return QStringLiteral("card not available");
        if (!byMedium.contains(medium->id))
            order.append(medium->id);
        byMedium[medium->id].append(entry);
    }
    QList<Medium *> media;
    for (const QString &id : std::as_const(order)) {
        Medium *medium = nullptr;
        for (Medium &m : m_media) {
            if (m.id == id)
                medium = &m;
        }
        if (!medium)
            return QStringLiteral("card not available");
        const QString refused = ready(*medium);
        if (!refused.isEmpty())
            return refused;
        media.append(medium);
    }
    bool stamped = true;
    for (auto it = media.crbegin(); it != media.crend(); ++it) {
        Medium *medium = *it;
        const QList<QueueEntry> &entries = byMedium.value(medium->id);
        QList<std::pair<QString, int>> stamps;
        const PlaylistStore::Edit result = m_library->playlistStore().edit(medium->file, [&](M3u &doc) {
            const QList<qint64> resolved = resolve(*medium, doc);
            QList<QString> present;
            for (qsizetype i = 0; i < doc.items.size(); ++i) {
                if (doc.items.at(i).entry)
                    present.append(doc.items.at(i).path);
            }
            for (qsizetype k = entries.size() - 1; k >= 0; --k) {
                const QueueEntry &entry = entries.at(k);
                const bool had = resolved.contains(entry.id)
                    || std::any_of(present.cbegin(), present.cend(),
                                   [&](const QString &p) { return sameTrack(*medium, p, entry.path); });
                if (had)
                    continue;
                PlaylistStore::insert(doc, 0, entry.path, medium->dir);
                present.append(entry.path);
                stamps.append({key(*medium, entry.path), 0});
            }
            return !stamps.isEmpty();
        });
        if (result == PlaylistStore::Edit::Unreadable) {
            medium->failed = true;
            rebuild();
            return QStringLiteral("favorites not readable");
        }
        if (result == PlaylistStore::Edit::Unwritten) {
            rebuild();
            return QStringLiteral("card not available");
        }
        if (!stamps.isEmpty())
            stamped = m_library->stampFavorites(medium->id, stamps) && stamped;
        readMedium(*medium);
    }
    rebuild();
    if (!stamped) {
        qWarning() << "microdap favorites: add order not stored";
        return QStringLiteral("favorites not readable");
    }
    return {};
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
    const QString refused = ready(*medium);
    if (!refused.isEmpty()) {
        emit notice(refused);
        return;
    }
    bool had = false;
    const PlaylistStore::Edit result = m_library->playlistStore().edit(medium->file, [&](M3u &doc) {
        const QList<qint64> resolved = resolve(*medium, doc);
        for (qsizetype i = doc.items.size() - 1; i >= 0; --i) {
            if (doc.items.at(i).entry
                && (resolved.at(i) == trackId || sameTrack(*medium, doc.items.at(i).path, path))) {
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
