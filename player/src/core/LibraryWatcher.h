#pragma once

#include <QFile>
#include <QFileSystemWatcher>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <memory>

class QSocketNotifier;

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
    ~LibraryWatcher() override;

    int limit() const;
    void setLimit(int limit);
    int debounceMs() const;
    void setDebounceMs(int ms);
    int pollMs() const;
    void setPollMs(int ms);
    QStringList watched() const;
    QStringList missing() const;

    static QString nearestExisting(const QString &path);
    static QStringList mediaMounts(const QByteArray &mountinfo);

public slots:
    void rebuild();
    void mountsChanged();

signals:
    void triggered();

private:
    void poll();

    Library *m_library;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QTimer m_poll;
    QFile m_mountinfo;
    std::unique_ptr<QSocketNotifier> m_notifier;
    QStringList m_mediaMounts;
    QStringList m_missing;
    QStringList m_media;
    bool m_mediaEvent = false;
    int m_limit = defaultLimit;
};

}
