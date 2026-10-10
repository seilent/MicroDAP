#pragma once

#include "OutputPorts.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QThread>
#include <QWaitCondition>

#include <atomic>

class QTimer;

namespace casdap {

class ReserveWorker;

struct ReserveShared
{
    QMutex mutex;
    QWaitCondition done;
    quint64 completed = 0;
    bool held = false;
    std::atomic<quint64> answered{0};
};

class DeviceReservation : public QObject, public ReservationPort
{
    Q_OBJECT

public:
    struct Bounds
    {
        int callMs = 1000;
        int replyMs = 500;
        int nameLostMs = 1000;
    };

    static constexpr int ownPriority = 10;

    explicit DeviceReservation(const Bounds &bounds, QObject *parent = nullptr);
    ~DeviceReservation() override;

    static QString interfaceName();
    static QString serviceFor(const QString &reserveName);
    static QString pathFor(const QString &reserveName);
    static QString applicationName();

    void acquire(const QString &reserveName, const QString &cardId) override;
    void grantRelease() override;
    void release() override;
    void markGone() override;
    bool waitReleased(int ms) override;
    void quitAndJoin(int ms) override;

signals:
    void acquired();
    void refused();
    void failed(const QString &reason);
    void releaseRequested(int priority);
    void releaseTimedOut();
    void nameLost(bool requested);
    void connectionLost();
    void released();

private:
    void onReleaseRequested(quint64 id, int priority);

    Bounds m_bounds;
    ReserveShared *m_shared;
    QThread *m_thread;
    ReserveWorker *m_worker;
    quint64 m_requested = 0;
    quint64 m_lastRequest = 0;
    bool m_joined = false;
};

class ReserveEndpoint;

class ReserveEndpointAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.ReserveDevice1")
    Q_PROPERTY(int Priority READ priority)
    Q_PROPERTY(QString ApplicationName READ applicationName)
    Q_PROPERTY(QString ApplicationDeviceName READ applicationDeviceName)

public:
    explicit ReserveEndpointAdaptor(ReserveEndpoint *endpoint);

    int priority() const;
    QString applicationName() const;
    QString applicationDeviceName() const;

public slots:
    bool RequestRelease(int priority);

private:
    ReserveEndpoint *m_endpoint;
};

class ReserveEndpoint : public QObject, protected QDBusContext
{
    Q_OBJECT

public:
    ReserveEndpoint(ReserveWorker *worker, const QString &deviceName);

    QString deviceName() const;
    bool requestRelease(int priority);
    QDBusMessage delayReply();

private:
    ReserveWorker *m_worker;
    QString m_deviceName;
};

class ReserveWorker : public QObject
{
    Q_OBJECT

public:
    ReserveWorker(const DeviceReservation::Bounds &bounds, ReserveShared *shared);

    bool onRequest(int priority, ReserveEndpoint *endpoint);

public slots:
    void acquire(const QString &reserveName, const QString &cardId);
    void grant(quint64 upTo);
    void release(quint64 seq);
    void markGone();
    void quit(quint64 seq);

signals:
    void acquired();
    void refused();
    void failed(const QString &reason);
    void releaseRequested(quint64 id, int priority);
    void releaseTimedOut();
    void nameLost(bool requested);
    void connectionLost();
    void released();

private slots:
    void onNameLost(const QString &name);
    void onDisconnected();

private:
    struct Call
    {
        quint64 id;
        QDBusMessage message;
    };

    bool openBus();
    void closeBus();
    bool takeName(bool announce);
    void answered(quint64 id);
    void reply(const QDBusMessage &message, bool value);
    void expire(quint64 id);
    void granted();
    void answerAll();
    void unexport();
    void releaseNow();
    void flush();
    void setHeld(bool held);
    void complete(quint64 seq);
    bool cardGone() const;

    DeviceReservation::Bounds m_bounds;
    ReserveShared *m_shared;
    QDBusConnection m_bus;
    QTimer *m_nameLostTimer;
    ReserveEndpoint *m_endpoint = nullptr;
    QList<Call> m_calls;
    QString m_connectionName;
    QString m_service;
    QString m_path;
    QString m_cardId;
    int m_card = -1;
    quint64 m_generation = 0;
    quint64 m_nextCall = 0;
    quint64 m_expired = 0;
    quint64 m_holdStart = 0;
    bool m_owned = false;
    bool m_releasing = false;
    bool m_granted = false;
    bool m_gone = false;
    bool m_closing = false;
};

}
