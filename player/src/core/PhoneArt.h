#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

namespace microdap {

class PhoneArt : public QObject
{
    Q_OBJECT

public:
    static constexpr int cacheSize = 4;
    static constexpr int fetchTimeoutMs = 8000;
    static constexpr int steadyMs = 30000;

    explicit PhoneArt(const QDBusConnection &session, QObject *parent = nullptr);
    ~PhoneArt() override;

    static QString obexService();
    static QString clientPath();
    static QString clientInterface();
    static QString imageInterface();
    static QString transferInterface();
    static QString target();

    void setTarget(const QString &address, int psm, const QString &handle);
    QString artPath(const QString &handle) const;
    bool sessionOpen() const;
    QString sessionPath() const;
    bool gaveUp() const;
    QString directory() const;

signals:
    void artChanged();
    void sessionOpenChanged();

private slots:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed,
                             const QStringList &invalidated, const QDBusMessage &message);
    void onInterfacesRemoved(const QDBusMessage &message);
    void onObexRegistered();
    void onObexUnregistered();

private:
    void sync();
    void createSession();
    void closeSession();
    void sessionLost();
    void failed();
    void fetch(const QString &handle);
    void finishFetch(bool ok);
    void clearCache();

    QDBusConnection m_bus;
    QDBusServiceWatcher m_watcher;
    QString m_dir;
    QList<int> m_retryMs;
    QString m_address;
    int m_psm = 0;
    QString m_handle;
    QString m_session;
    bool m_creating = false;
    quint64 m_generation = 0;
    quint64 m_serial = 0;
    int m_tries = 0;
    bool m_gaveUp = false;
    QTimer m_retry;
    QElapsedTimer m_openedAt;
    QList<QPair<QString, QString>> m_cache;
    QSet<QString> m_noArt;
    QString m_fetchHandle;
    QString m_fetchFile;
    QString m_transferPath;
    QTimer m_fetchTimer;
};

}
