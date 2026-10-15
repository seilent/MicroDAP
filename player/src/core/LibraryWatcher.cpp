#include "LibraryWatcher.h"

#include "Library.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <utility>

namespace microdap {

namespace {

bool limitLogged = false;

}

LibraryWatcher::LibraryWatcher(Library *library, QObject *parent)
    : QObject(parent)
    , m_library(library)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(defaultDebounceMs);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        emit triggered();
        m_library->rescanRoots();
    });
    connect(m_library, &Library::scanned, this, &LibraryWatcher::rebuild);
    m_poll.setInterval(defaultPollMs);
    connect(&m_poll, &QTimer::timeout, this, &LibraryWatcher::poll);
}

int LibraryWatcher::pollMs() const
{
    return m_poll.interval();
}

void LibraryWatcher::setPollMs(int ms)
{
    m_poll.setInterval(qMax(1, ms));
}

QStringList LibraryWatcher::missing() const
{
    return m_missing;
}

void LibraryWatcher::poll()
{
    for (const QString &root : std::as_const(m_missing)) {
        if (QFileInfo(root).isDir()) {
            m_poll.stop();
            m_debounce.stop();
            emit triggered();
            m_library->rescanRoots();
            return;
        }
    }
}

int LibraryWatcher::limit() const
{
    return m_limit;
}

void LibraryWatcher::setLimit(int limit)
{
    m_limit = qMax(1, limit);
}

int LibraryWatcher::debounceMs() const
{
    return m_debounce.interval();
}

void LibraryWatcher::setDebounceMs(int ms)
{
    m_debounce.setInterval(qMax(0, ms));
}

QStringList LibraryWatcher::watched() const
{
    return m_watcher.directories();
}

QString LibraryWatcher::nearestExisting(const QString &path)
{
    const QString base = QDir::cleanPath(QDir(Scanner::mediaBase()).absolutePath());
    const QString home = QDir::cleanPath(QDir::homePath());
    QString current = QDir::cleanPath(QDir(path).absolutePath());
    while (!current.isEmpty()) {
        if (QFileInfo(current).isDir())
            return current;
        if (current == base || current == home)
            return QString();
        const QString parent = QFileInfo(current).path();
        if (parent == current)
            return QString();
        current = parent;
    }
    return QString();
}

void LibraryWatcher::rebuild()
{
    QStringList wanted;
    QSet<QString> listed;
    const auto add = [&wanted, &listed](const QString &path) {
        if (path.isEmpty() || listed.contains(path))
            return;
        listed.insert(path);
        wanted.append(path);
    };

    m_missing.clear();
    const QStringList roots = m_library->scannedRoots();
    for (const QString &root : roots) {
        if (root.isEmpty() || QFileInfo(root).isDir())
            continue;
        m_missing.append(root);
        add(nearestExisting(root));
    }
    if (m_missing.isEmpty())
        m_poll.stop();
    else if (!m_poll.isActive())
        m_poll.start();
    const QStringList directories = m_library->scannedDirectories();
    for (const QString &directory : directories)
        add(directory);

    if (wanted.size() > m_limit) {
        if (!limitLogged) {
            limitLogged = true;
            qWarning().noquote() << QStringLiteral("microdap: watch limit %1 reached").arg(m_limit);
        }
        wanted.resize(m_limit);
        listed = QSet<QString>(wanted.cbegin(), wanted.cend());
    }

    const QStringList current = m_watcher.directories();
    QStringList drop;
    for (const QString &path : current) {
        if (!listed.contains(path))
            drop.append(path);
    }
    if (!drop.isEmpty())
        m_watcher.removePaths(drop);

    const QSet<QString> have(current.cbegin(), current.cend());
    QStringList fresh;
    for (const QString &path : std::as_const(wanted)) {
        if (!have.contains(path))
            fresh.append(path);
    }
    if (!fresh.isEmpty())
        m_watcher.addPaths(fresh);
}

}
