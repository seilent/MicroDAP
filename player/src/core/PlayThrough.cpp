#include "PlayThrough.h"

#include "HubModels.h"
#include "Library.h"
#include "Models.h"
#include "Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSqlQuery>
#include <QVariantList>

#include <algorithm>

namespace microdap {

namespace {

int rootOf(const QString &dir, const QStringList &roots)
{
    for (qsizetype i = 0; i < roots.size(); ++i) {
        const QString root = QDir::cleanPath(roots.at(i));
        if (dir == root || dir.startsWith(root + QLatin1Char('/')))
            return int(i);
    }
    return -1;
}

}

PlayThrough::PlayThrough(Queue *queue, Library *library, AlbumsModel *albums, QObject *parent)
    : QObject(parent)
    , m_queue(queue)
    , m_library(library)
    , m_albums(albums)
{
}

bool PlayThrough::due(const Queue &queue, const QString &mode)
{
    return mode != QLatin1StringView("off") && Settings::playThroughModes().contains(mode) && !queue.shuffle()
           && queue.repeat() == Queue::Off && queue.currentIndex() >= 0 && queue.nextIndex(false) < 0;
}

bool PlayThrough::dirBefore(const QString &a, const QString &b, const QStringList &roots)
{
    const int ra = rootOf(a, roots);
    const int rb = rootOf(b, roots);
    if (ra != rb)
        return ra < rb;
    const QString base = ra < 0 ? QString() : QDir::cleanPath(roots.at(ra));
    const QStringList pa = a.mid(base.size()).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QStringList pb = b.mid(base.size()).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (qsizetype i = 0; i < std::min(pa.size(), pb.size()); ++i) {
        if (pa.at(i) != pb.at(i))
            return FolderModel::folderLess(pa.at(i), pb.at(i));
    }
    return pa.size() > pb.size();
}

QString PlayThrough::nextDir(const QStringList &dirs, const QStringList &roots, const QString &dir)
{
    if (rootOf(dir, roots) < 0)
        return {};
    QString best;
    for (const QString &d : dirs) {
        if (rootOf(d, roots) < 0 || !dirBefore(dir, d, roots))
            continue;
        if (best.isEmpty() || dirBefore(d, best, roots))
            best = d;
    }
    return best;
}

QString PlayThrough::mode() const
{
    return m_mode;
}

void PlayThrough::setMode(const QString &mode)
{
    m_mode = Settings::playThroughModes().contains(mode) ? mode : QStringLiteral("off");
}

void PlayThrough::setRoots(const QStringList &roots)
{
    m_roots.clear();
    for (const QString &root : roots)
        m_roots.append(QDir::cleanPath(root));
}

void PlayThrough::onTrackStarted()
{
    if (!m_queue || !m_library || !due(*m_queue, m_mode))
        return;
    const QueueEntry current = m_queue->entry(m_queue->currentIndex());
    const QList<QueueEntry> next = m_mode == QLatin1StringView("albums") ? nextAlbum(current.id)
                                                                         : nextFolder(current.path);
    if (!next.isEmpty())
        m_queue->enqueue(next);
}

QList<QueueEntry> PlayThrough::nextFolder(const QString &path) const
{
    const QString dir = QFileInfo(path).path();
    QHash<QString, QList<std::pair<QString, qint64>>> songs;
    QSqlQuery query(m_library->database());
    query.prepare(QStringLiteral("SELECT id, path FROM tracks WHERE path >= ? AND path < ?"));
    for (const QString &root : m_roots) {
        query.bindValue(0, root + QLatin1Char('/'));
        query.bindValue(1, root + QLatin1Char('0'));
        if (!query.exec())
            continue;
        while (query.next()) {
            const QString path = query.value(1).toString();
            const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
            songs[path.left(slash)].append({path.mid(slash + 1), query.value(0).toLongLong()});
        }
    }
    const QString next = nextDir(songs.keys(), m_roots, dir);
    if (next.isEmpty())
        return {};
    QList<std::pair<QString, qint64>> files = songs.value(next);
    std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) { return FolderModel::folderLess(a.first, b.first); });
    QVariantList ids;
    for (const auto &file : std::as_const(files))
        ids.append(file.second);
    return m_library->queueEntries(ids);
}

QList<QueueEntry> PlayThrough::nextAlbum(qint64 trackId) const
{
    QSqlQuery query(m_library->database());
    query.prepare(QStringLiteral("SELECT album_id FROM tracks WHERE id = ?"));
    query.addBindValue(trackId);
    if (!query.exec() || !query.next())
        return {};
    const qint64 album = query.value(0).toLongLong();
    if (!m_albums)
        return {};
    m_albums->ensureLoaded();
    const int row = m_albums->indexOf(album);
    if (row < 0 || row + 1 >= m_albums->rowCount())
        return {};
    return m_library->entriesFor(QStringLiteral("album"),
                                 m_albums->get(row + 1).value(QStringLiteral("id")).toLongLong());
}

}
