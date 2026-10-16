#include "LibraryWatcher.h"

#include "Library.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QSocketNotifier>

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
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &path) {
        if (m_media.contains(path))
            m_mediaEvent = true;
        m_debounce.start();
    });
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        emit triggered();
        Settings *settings = m_library->settings();
        if (std::exchange(m_mediaEvent, false) && settings && settings->rootsDefault())
            settings->refreshRoots();
        else
            m_library->rescanRoots();
    });
    connect(m_library, &Library::scanned, this, &LibraryWatcher::rebuild);
    m_poll.setInterval(defaultPollMs);
    connect(&m_poll, &QTimer::timeout, this, &LibraryWatcher::poll);
    m_mountinfo.setFileName(QStringLiteral("/proc/self/mountinfo"));
    if (m_mountinfo.open(QIODevice::ReadOnly | QIODevice::Unbuffered)) {
        m_mediaMounts = mediaMounts(m_mountinfo.readAll());
        m_notifier = std::make_unique<QSocketNotifier>(m_mountinfo.handle(), QSocketNotifier::Exception);
        connect(m_notifier.get(), &QSocketNotifier::activated, this, [this] {
            m_mountinfo.seek(0);
            QStringList mounts = mediaMounts(m_mountinfo.readAll());
            if (mounts == m_mediaMounts)
                return;
            m_mediaMounts = std::move(mounts);
            mountsChanged();
        });
    }
}

LibraryWatcher::~LibraryWatcher() = default;

QStringList LibraryWatcher::mediaMounts(const QByteArray &mountinfo)
{
    const QString base = QDir::cleanPath(QDir(Scanner::mediaBase()).absolutePath());
    const QString prefix = base + QLatin1Char('/');
    QStringList out;
    for (const QByteArray &line : mountinfo.split('\n')) {
        const QList<QByteArray> fields = line.split(' ');
        if (fields.size() < 5)
            continue;
        QByteArray target = fields.at(4);
        target.replace("\\040", " ").replace("\\011", "\t").replace("\\012", "\n").replace("\\134", "\\");
        const QString path = QString::fromLocal8Bit(target);
        if (path == base || path.startsWith(prefix))
            out.append(QString::fromLocal8Bit(line));
    }
    return out;
}

void LibraryWatcher::mountsChanged()
{
    const Settings *settings = m_library->settings();
    if (!settings || !settings->rootsDefault())
        return;
    m_mediaEvent = true;
    m_debounce.start();
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

    m_media.clear();
    const Settings *settings = m_library->settings();
    const QString base = QDir::cleanPath(QDir(Scanner::mediaBase()).absolutePath());
    if (settings && settings->rootsDefault() && QFileInfo(base).isDir()) {
        const auto dirs = [](const QString &path) {
            return QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        };
        const auto card = [this, &dirs](const QString &path) {
            m_media.append(path);
            for (const QFileInfo &entry : dirs(path)) {
                if (entry.fileName().compare(QLatin1StringView("roms"), Qt::CaseInsensitive) == 0)
                    m_media.append(entry.absoluteFilePath());
            }
        };
        m_media.append(base);
        for (const QFileInfo &child : dirs(base)) {
            const QString path = child.absoluteFilePath();
            if (Scanner::isMountPoint(path)) {
                card(path);
                continue;
            }
            m_media.append(path);
            for (const QFileInfo &sub : dirs(path)) {
                if (Scanner::isMountPoint(sub.absoluteFilePath()))
                    card(sub.absoluteFilePath());
            }
        }
        for (const QString &path : std::as_const(m_media))
            add(path);
    }

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
