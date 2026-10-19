#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QDBusUnixFileDescriptor>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

namespace microdap {

class LinkProfile;

class LinkProfileAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Profile1")

public:
    explicit LinkProfileAdaptor(LinkProfile *profile);

public slots:
    void Release();
    void NewConnection(const QDBusObjectPath &device, const QDBusUnixFileDescriptor &fd, const QVariantMap &properties,
                       const QDBusMessage &message);
    void RequestDisconnection(const QDBusObjectPath &device);

private:
    LinkProfile *m_profile;
};

class LinkProfile : public QObject
{
    Q_OBJECT

public:
    static QString path();
    static QString uuid();
    static QString bluezService();
    static QList<int> backoffMs();

    explicit LinkProfile(const QDBusConnection &bus, QObject *parent = nullptr);
    ~LinkProfile() override;

    void setEnabled(bool on);
    bool enabled() const;
    bool registered() const;
    bool accept(const QString &device);
    void newConnection(const QString &device, int fd, const QDBusMessage &message);
    void release();
    void disconnectRequested(const QString &device);
    QDBusConnection bus() const;

signals:
    void connection(int fd, const QString &device);
    void ended(const QString &device);
    void registeredChanged();

private:
    void registerNow();
    void unregisterNow();
    void unregisterCall();
    void retry();
    void setRegistered(bool on);
    void ownerChanged(const QString &owner);

    QDBusConnection m_bus;
    QDBusServiceWatcher m_watcher;
    QTimer m_retry;
    QElapsedTimer m_clock;
    QList<qint64> m_connects;
    int m_attempt = 0;
    quint32 m_generation = 0;
    bool m_enabled = false;
    bool m_registered = false;
    bool m_exported = false;
};

}
