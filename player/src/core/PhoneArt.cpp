#include "PhoneArt.h"

#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSignalBlocker>

namespace microdap {

namespace {

QString propertiesInterface()
{
    return QStringLiteral("org.freedesktop.DBus.Properties");
}

QString objectManagerInterface()
{
    return QStringLiteral("org.freedesktop.DBus.ObjectManager");
}

QString objectPath(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusObjectPath>())
        return qvariant_cast<QDBusObjectPath>(value).path();
    return value.toString();
}

QString artDirectory()
{
    const QString dir = qEnvironmentVariable("MICRODAP_BT_ART_DIR");
    if (!dir.isEmpty())
        return dir;
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    return runtime.isEmpty() ? QString() : runtime + QStringLiteral("/microdap/phone-art");
}

int fetchTimeout()
{
    bool ok = false;
    const int value = qEnvironmentVariableIntValue("MICRODAP_BT_ART_FETCH_MS", &ok);
    return ok && value > 0 ? value : PhoneArt::fetchTimeoutMs;
}

QList<int> retryDelays()
{
    QList<int> delays;
    const QStringList parts = qEnvironmentVariable("MICRODAP_BT_ART_RETRY_MS", QStringLiteral("2000,5000,15000"))
                                  .split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        bool ok = false;
        const int value = part.trimmed().toInt(&ok);
        if (ok && value >= 0)
            delays.append(value);
    }
    return delays;
}

}

PhoneArt::PhoneArt(const QDBusConnection &session, QObject *parent)
    : QObject(parent)
    , m_bus(session)
    , m_watcher(obexService(), session, QDBusServiceWatcher::WatchForOwnerChange)
    , m_dir(artDirectory())
    , m_retryMs(retryDelays())
{
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &PhoneArt::sync);
    m_fetchTimer.setSingleShot(true);
    m_fetchTimer.setInterval(fetchTimeout());
    connect(&m_fetchTimer, &QTimer::timeout, this, [this] {
        qInfo() << "microdap phone art: thumbnail" << m_fetchHandle << "timed out";
        if (!m_transferPath.isEmpty() && m_bus.isConnected()) {
            m_bus.asyncCall(QDBusMessage::createMethodCall(obexService(), m_transferPath,
                                                           transferInterface(), QStringLiteral("Cancel")));
        }
        finishFetch(false);
    });
    connect(&m_watcher, &QDBusServiceWatcher::serviceRegistered, this, &PhoneArt::onObexRegistered);
    connect(&m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &PhoneArt::onObexUnregistered);

    if (m_dir.isEmpty())
        qWarning() << "microdap phone art: XDG_RUNTIME_DIR not set, no phone art";
    QDir dir(m_dir);
    if (!m_dir.isEmpty() && dir.exists()) {
        const QStringList stale = dir.entryList(QDir::Files);
        for (const QString &name : stale)
            dir.remove(name);
    }

    if (!m_bus.isConnected())
        return;
    if (!m_bus.connect(obexService(), QString(), propertiesInterface(), QStringLiteral("PropertiesChanged"),
                       this, SLOT(onPropertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)))) {
        qWarning() << "microdap phone art: cannot subscribe to PropertiesChanged"
                   << m_bus.lastError().message();
    }
    if (!m_bus.connect(obexService(), QStringLiteral("/"), objectManagerInterface(),
                       QStringLiteral("InterfacesRemoved"), this, SLOT(onInterfacesRemoved(QDBusMessage)))) {
        qWarning() << "microdap phone art: cannot subscribe to InterfacesRemoved"
                   << m_bus.lastError().message();
    }
}

PhoneArt::~PhoneArt()
{
    const QSignalBlocker blocker(this);
    closeSession();
}

QString PhoneArt::obexService()
{
    return QStringLiteral("org.bluez.obex");
}

QString PhoneArt::clientPath()
{
    return QStringLiteral("/org/bluez/obex");
}

QString PhoneArt::clientInterface()
{
    return QStringLiteral("org.bluez.obex.Client1");
}

QString PhoneArt::imageInterface()
{
    return QStringLiteral("org.bluez.obex.Image1");
}

QString PhoneArt::transferInterface()
{
    return QStringLiteral("org.bluez.obex.Transfer1");
}

QString PhoneArt::target()
{
    return QStringLiteral("bip-avrcp");
}

QString PhoneArt::artPath(const QString &handle) const
{
    if (handle.isEmpty())
        return {};
    for (const auto &entry : m_cache) {
        if (entry.first == handle)
            return entry.second;
    }
    return {};
}

bool PhoneArt::sessionOpen() const
{
    return !m_session.isEmpty();
}

QString PhoneArt::sessionPath() const
{
    return m_session;
}

bool PhoneArt::gaveUp() const
{
    return m_gaveUp;
}

QString PhoneArt::directory() const
{
    return m_dir;
}

void PhoneArt::setTarget(const QString &address, int psm, const QString &handle)
{
    static const QRegularExpression valid(QStringLiteral("^[0-9]{7}$"));
    const int port = address.isEmpty() || psm <= 0 ? 0 : psm;
    const QString wanted = port > 0 ? address : QString();
    if (wanted != m_address || port != m_psm) {
        closeSession();
        m_address = wanted;
        m_psm = port;
        m_tries = 0;
        m_gaveUp = false;
        m_retry.stop();
    }
    m_handle = m_psm > 0 && valid.match(handle).hasMatch() ? handle : QString();
    for (qsizetype i = 0; i < m_cache.size(); ++i) {
        if (m_cache.at(i).first == m_handle) {
            m_cache.move(i, m_cache.size() - 1);
            break;
        }
    }
    sync();
}

void PhoneArt::sync()
{
    if (m_address.isEmpty() || m_psm <= 0 || m_dir.isEmpty() || !m_bus.isConnected())
        return;
    if (m_session.isEmpty()) {
        if (!m_creating && !m_retry.isActive() && !m_gaveUp)
            createSession();
        return;
    }
    if (!m_fetchHandle.isEmpty() || m_handle.isEmpty() || m_noArt.contains(m_handle)
        || !artPath(m_handle).isEmpty())
        return;
    fetch(m_handle);
}

void PhoneArt::createSession()
{
    QVariantMap options;
    options.insert(QStringLiteral("Target"), target());
    options.insert(QStringLiteral("PSM"), QVariant::fromValue(static_cast<quint16>(m_psm)));
    QDBusMessage call = QDBusMessage::createMethodCall(obexService(), clientPath(), clientInterface(),
                                                       QStringLiteral("CreateSession"));
    call << m_address << options;
    m_creating = true;
    const quint64 generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        const QDBusMessage reply = finished->reply();
        const QString path = reply.type() == QDBusMessage::ErrorMessage
                                 ? QString()
                                 : objectPath(reply.arguments().value(0));
        if (generation != m_generation) {
            if (!path.isEmpty()) {
                QDBusMessage remove = QDBusMessage::createMethodCall(obexService(), clientPath(),
                                                                     clientInterface(),
                                                                     QStringLiteral("RemoveSession"));
                remove << QVariant::fromValue(QDBusObjectPath(path));
                m_bus.asyncCall(remove);
            }
            return;
        }
        m_creating = false;
        if (path.isEmpty()) {
            qInfo() << "microdap phone art: CreateSession failed" << reply.errorName() << reply.errorMessage();
            failed();
            return;
        }
        m_session = path;
        ++m_serial;
        m_openedAt.start();
        emit sessionOpenChanged();
        sync();
    });
}

void PhoneArt::closeSession()
{
    ++m_generation;
    m_creating = false;
    m_fetchTimer.stop();
    m_fetchHandle.clear();
    m_fetchFile.clear();
    m_transferPath.clear();
    if (m_session.isEmpty()) {
        clearCache();
        return;
    }
    if (m_bus.isConnected()) {
        QDBusMessage remove = QDBusMessage::createMethodCall(obexService(), clientPath(), clientInterface(),
                                                             QStringLiteral("RemoveSession"));
        remove << QVariant::fromValue(QDBusObjectPath(m_session));
        m_bus.asyncCall(remove);
    }
    m_session.clear();
    clearCache();
    emit sessionOpenChanged();
}

void PhoneArt::sessionLost()
{
    if (m_openedAt.isValid() && m_openedAt.elapsed() >= steadyMs)
        m_tries = 0;
    ++m_generation;
    m_fetchTimer.stop();
    m_fetchHandle.clear();
    m_fetchFile.clear();
    m_transferPath.clear();
    m_session.clear();
    m_creating = false;
    clearCache();
    emit sessionOpenChanged();
    failed();
}

void PhoneArt::failed()
{
    ++m_tries;
    if (m_tries > m_retryMs.size()) {
        if (!m_gaveUp)
            qInfo() << "microdap phone art: gave up on" << m_address;
        m_gaveUp = true;
        return;
    }
    m_retry.start(m_retryMs.at(m_tries - 1));
}

void PhoneArt::fetch(const QString &handle)
{
    if (!QDir().mkpath(m_dir)) {
        qWarning() << "microdap phone art: cannot create" << m_dir;
        m_noArt.insert(handle);
        return;
    }
    m_fetchHandle = handle;
    m_fetchFile = m_dir + QStringLiteral("/%1-%2.jpg").arg(m_serial).arg(handle);
    QFile::remove(m_fetchFile);
    QDBusMessage call = QDBusMessage::createMethodCall(obexService(), m_session, imageInterface(),
                                                       QStringLiteral("GetThumbnail"));
    call << m_fetchFile << handle;
    m_fetchTimer.start();
    const quint64 generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation, handle](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        if (generation != m_generation || handle != m_fetchHandle)
            return;
        const QDBusMessage reply = finished->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qInfo() << "microdap phone art: GetThumbnail" << handle << "failed" << reply.errorMessage();
            finishFetch(false);
            return;
        }
        m_transferPath = objectPath(reply.arguments().value(0));
        const QVariantMap properties = qdbus_cast<QVariantMap>(reply.arguments().value(1));
        const QString status = properties.value(QStringLiteral("Status")).toString();
        if (status == QLatin1StringView("complete"))
            finishFetch(true);
        else if (status == QLatin1StringView("error"))
            finishFetch(false);
    });
}

void PhoneArt::finishFetch(bool ok)
{
    m_fetchTimer.stop();
    const QString handle = m_fetchHandle;
    const QString file = m_fetchFile;
    m_fetchHandle.clear();
    m_fetchFile.clear();
    m_transferPath.clear();
    const QFileInfo info(file);
    if (ok && info.exists() && info.size() > 0) {
        m_cache.append({handle, file});
        while (m_cache.size() > cacheSize)
            QFile::remove(m_cache.takeFirst().second);
        emit artChanged();
    } else {
        QFile::remove(file);
        m_noArt.insert(handle);
    }
    sync();
}

void PhoneArt::clearCache()
{
    const bool had = !m_cache.isEmpty();
    for (const auto &entry : std::as_const(m_cache))
        QFile::remove(entry.second);
    m_cache.clear();
    m_noArt.clear();
    if (had)
        emit artChanged();
}

void PhoneArt::onPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                   const QStringList &, const QDBusMessage &message)
{
    if (interface != transferInterface() || m_transferPath.isEmpty() || message.path() != m_transferPath)
        return;
    const QString status = changed.value(QStringLiteral("Status")).toString();
    if (status == QLatin1StringView("complete"))
        finishFetch(true);
    else if (status == QLatin1StringView("error"))
        finishFetch(false);
}

void PhoneArt::onInterfacesRemoved(const QDBusMessage &message)
{
    const QString path = objectPath(message.arguments().value(0));
    if (path.isEmpty())
        return;
    if (!m_session.isEmpty() && path == m_session) {
        qInfo() << "microdap phone art: session closed by obexd";
        sessionLost();
        return;
    }
    if (!m_transferPath.isEmpty() && path == m_transferPath)
        finishFetch(false);
}

void PhoneArt::onObexRegistered()
{
    m_tries = 0;
    m_gaveUp = false;
    m_retry.stop();
    sync();
}

void PhoneArt::onObexUnregistered()
{
    if (!m_session.isEmpty())
        sessionLost();
}

}
