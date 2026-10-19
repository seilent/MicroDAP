#include "LinkProfile.h"

#include "LinkProto.h"

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDebug>

#include <fcntl.h>
#include <unistd.h>

namespace microdap {

namespace {

QString profileManager()
{
    return QStringLiteral("org.bluez.ProfileManager1");
}

QVariantMap options()
{
    return {
        {QStringLiteral("Name"), QStringLiteral("microdap companion")},
        {QStringLiteral("Role"), QStringLiteral("server")},
        {QStringLiteral("Channel"), QVariant::fromValue(quint16(0))},
        {QStringLiteral("RequireAuthentication"), true},
        {QStringLiteral("RequireAuthorization"), false},
        {QStringLiteral("AutoConnect"), false},
    };
}

}

LinkProfileAdaptor::LinkProfileAdaptor(LinkProfile *profile)
    : QDBusAbstractAdaptor(profile)
    , m_profile(profile)
{
}

void LinkProfileAdaptor::Release()
{
    m_profile->release();
}

void LinkProfileAdaptor::NewConnection(const QDBusObjectPath &device, const QDBusUnixFileDescriptor &fd,
                                       const QVariantMap &, const QDBusMessage &message)
{
    const int own = fd.isValid() ? fcntl(fd.fileDescriptor(), F_DUPFD_CLOEXEC, 0) : -1;
    message.setDelayedReply(true);
    m_profile->newConnection(device.path(), own, message);
}

void LinkProfileAdaptor::RequestDisconnection(const QDBusObjectPath &device)
{
    m_profile->disconnectRequested(device.path());
}

QString LinkProfile::path()
{
    return QStringLiteral("/microdap/companion");
}

QString LinkProfile::uuid()
{
    return QString::fromLatin1(fd::proto::uuid);
}

QString LinkProfile::bluezService()
{
    return QStringLiteral("org.bluez");
}

QList<int> LinkProfile::backoffMs()
{
    QList<int> out;
    for (const QString &part : QString::fromLatin1(fd::proto::backoffMs).split(QLatin1Char(','), Qt::SkipEmptyParts))
        out.push_back(part.toInt());
    return out;
}

LinkProfile::LinkProfile(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_watcher(bluezService(), bus, QDBusServiceWatcher::WatchForOwnerChange)
{
    new LinkProfileAdaptor(this);
    m_exported = m_bus.registerObject(path(), this, QDBusConnection::ExportAdaptors);
    if (!m_exported)
        qWarning().noquote() << "microdap link: cannot export" << path();
    m_clock.start();
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &LinkProfile::registerNow);
    connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &owner) { ownerChanged(owner); });
}

LinkProfile::~LinkProfile()
{
    unregisterNow();
    if (m_exported)
        m_bus.unregisterObject(path());
}

QDBusConnection LinkProfile::bus() const
{
    return m_bus;
}

bool LinkProfile::enabled() const
{
    return m_enabled;
}

bool LinkProfile::registered() const
{
    return m_registered;
}

void LinkProfile::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    m_attempt = 0;
    if (on)
        registerNow();
    else
        unregisterNow();
}

void LinkProfile::setRegistered(bool on)
{
    if (on == m_registered)
        return;
    m_registered = on;
    emit registeredChanged();
}

void LinkProfile::registerNow()
{
    m_retry.stop();
    if (!m_enabled || m_registered || !m_exported)
        return;
    const quint32 generation = ++m_generation;
    QDBusMessage call = QDBusMessage::createMethodCall(bluezService(), QStringLiteral("/org/bluez"), profileManager(),
                                                       QStringLiteral("RegisterProfile"));
    call << QVariant::fromValue(QDBusObjectPath(path())) << uuid() << options();
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<> reply = *w;
        const bool ours = !reply.isError() || reply.error().name() == QLatin1StringView("org.bluez.Error.AlreadyExists");
        if (!m_enabled) {
            if (ours)
                unregisterCall();
            return;
        }
        if (generation != m_generation && !ours)
            return;
        if (ours) {
            m_attempt = 0;
            m_retry.stop();
            qInfo().noquote() << "microdap link: profile registered at" << path();
            setRegistered(true);
            return;
        }
        qWarning().noquote() << "microdap link: RegisterProfile" << reply.error().name() << reply.error().message();
        retry();
    });
}

void LinkProfile::retry()
{
    const QList<int> steps = backoffMs();
    if (steps.isEmpty())
        return;
    const int delay = steps.at(qMin(m_attempt, int(steps.size()) - 1));
    ++m_attempt;
    m_retry.start(delay);
}

void LinkProfile::unregisterNow()
{
    m_retry.stop();
    ++m_generation;
    if (!m_registered)
        return;
    setRegistered(false);
    unregisterCall();
}

void LinkProfile::unregisterCall()
{
    QDBusMessage call = QDBusMessage::createMethodCall(bluezService(), QStringLiteral("/org/bluez"), profileManager(),
                                                       QStringLiteral("UnregisterProfile"));
    call << QVariant::fromValue(QDBusObjectPath(path()));
    m_bus.asyncCall(call);
}

void LinkProfile::ownerChanged(const QString &owner)
{
    ++m_generation;
    setRegistered(false);
    m_attempt = 0;
    if (owner.isEmpty()) {
        m_retry.stop();
        return;
    }
    registerNow();
}

void LinkProfile::release()
{
    qInfo() << "microdap link: profile released";
    ++m_generation;
    setRegistered(false);
    m_attempt = 0;
    registerNow();
}

void LinkProfile::newConnection(const QString &device, int fd, const QDBusMessage &message)
{
    auto refuse = [this, device, fd, message](const char *why) {
        if (fd >= 0)
            ::close(fd);
        qInfo().noquote() << "microdap link: connection from" << device << "refused," << why;
        m_bus.send(message.createErrorReply(QStringLiteral("org.bluez.Error.Rejected"), QStringLiteral("refused")));
    };
    if (fd < 0)
        return refuse("no fd");
    if (!accept(device))
        return refuse("profile off or rate");
    QDBusMessage call = QDBusMessage::createMethodCall(bluezService(), device, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                       QStringLiteral("GetAll"));
    call << QStringLiteral("org.bluez.Device1");
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, device, fd, message, refuse](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QVariantMap> reply = *w;
        if (reply.isError())
            return refuse("device unknown");
        const QVariantMap props = reply.value();
        if (!props.value(QStringLiteral("Paired")).toBool() || !props.value(QStringLiteral("Bonded"), true).toBool())
            return refuse("not paired");
        if (!m_enabled || !m_registered)
            return refuse("profile off");
        m_bus.send(message.createReply());
        emit connection(fd, device);
    });
}

void LinkProfile::disconnectRequested(const QString &device)
{
    emit ended(device);
}

bool LinkProfile::accept(const QString &)
{
    const qint64 now = m_clock.elapsed();
    while (!m_connects.isEmpty() && now - m_connects.constFirst() >= fd::proto::refuseWindowMs)
        m_connects.removeFirst();
    const bool rate = m_connects.size() < fd::proto::refuseCount;
    m_connects.push_back(now);
    return m_enabled && m_registered && rate;
}

}
