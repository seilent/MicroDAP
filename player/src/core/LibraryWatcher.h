#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QStringList>
#include <QTimer>

namespace microdap {

class Library;

class LibraryWatcher : public QObject
{
    Q_OBJECT

public:
    static constexpr int defaultLimit = 4096;
    static constexpr int defaultDebounceMs = 5000;
    static constexpr int defaultPollMs = 5000;

    explicit LibraryWatcher(Library *library, QObject *parent = nullptr);

    int limit() const;
    void setLimit(int limit);
    int debounceMs() const;
    void setDebounceMs(int ms);
    int pollMs() const;
    void setPollMs(int ms);
    QStringList watched() const;
    QStringList missing() const;

    static QString nearestExisting(const QString &path);

public slots:
    void rebuild();

signals:
    void triggered();

private:
    void poll();

    Library *m_library;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QTimer m_poll;
    QStringList m_missing;
    int m_limit = defaultLimit;
};

}
