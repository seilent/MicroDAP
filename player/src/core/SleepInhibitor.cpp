#include "SleepInhibitor.h"

#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDebug>

#include <unistd.h>

namespace microdap {

SleepInhibitor::SleepInhibitor(const QString &why, QObject *parent)
    : SleepInhibitor(bus(), why, parent)
{
}

SleepInhibitor::SleepInhibitor(const QDBusConnection &bus, const QString &why, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_why(why)
{
    if (!m_bus.isConnected()) {
        qWarning() << "microdap sleep: login1 bus not connected";
        return;
    }
    m_subscribed = m_bus.connect(service(), path(), interface(), QStringLiteral("PrepareForSleep"), this,
                                 SLOT(onPrepareForSleep(bool)));
    if (!m_subscribed)
        qWarning() << "microdap sleep: cannot subscribe to PrepareForSleep" << m_bus.lastError().message();
}

SleepInhibitor::~SleepInhibitor()
{
    release();
}

QDBusConnection SleepInhibitor::bus()
{
    const QString address = qEnvironmentVariable("MICRODAP_LOGIN1_BUS");
    if (address.isEmpty())
        return QDBusConnection::systemBus();
    return QDBusConnection::connectToBus(address, QStringLiteral("microdap-login1"));
}

QString SleepInhibitor::service()
{
    return QStringLiteral("org.freedesktop.login1");
}

QString SleepInhibitor::path()
{
    return QStringLiteral("/org/freedesktop/login1");
}

QString SleepInhibitor::interface()
{
    return QStringLiteral("org.freedesktop.login1.Manager");
}

bool SleepInhibitor::subscribed() const
{
    return m_subscribed;
}

bool SleepInhibitor::held() const
{
    return m_fd >= 0;
}

int SleepInhibitor::takeCount() const
{
    return m_takeCount;
}

QString SleepInhibitor::why() const
{
    return m_why;
}

bool SleepInhibitor::take()
{
    if (m_fd >= 0)
        return true;
    if (!m_bus.isConnected())
        return false;
    QDBusMessage call = QDBusMessage::createMethodCall(service(), path(), interface(), QStringLiteral("Inhibit"));
    call << QStringLiteral("sleep") << QStringLiteral("microdap") << m_why << QStringLiteral("delay");
    const QDBusMessage reply = m_bus.call(call, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        qWarning() << "microdap sleep: Inhibit failed" << reply.errorMessage();
        return false;
    }
    const auto descriptor = qvariant_cast<QDBusUnixFileDescriptor>(reply.arguments().constFirst());
    if (!descriptor.isValid()) {
        qWarning() << "microdap sleep: Inhibit returned no descriptor";
        return false;
    }
    m_fd = ::dup(descriptor.fileDescriptor());
    if (m_fd < 0) {
        qWarning() << "microdap sleep: cannot keep the inhibitor descriptor";
        return false;
    }
    ++m_takeCount;
    qInfo() << "microdap sleep: delay inhibitor taken," << m_why;
    emit heldChanged();
    return true;
}

void SleepInhibitor::release()
{
    if (m_fd < 0)
        return;
    ::close(m_fd);
    m_fd = -1;
    qInfo() << "microdap sleep: delay inhibitor released";
    emit heldChanged();
}

void SleepInhibitor::onPrepareForSleep(bool start)
{
    qInfo() << "microdap sleep: PrepareForSleep" << start;
    emit prepareForSleep(start);
}

}
