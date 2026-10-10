#include "DeviceReservation.h"

#include "AudioInfo.h"

#include <QCoreApplication>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDeadlineTimer>
#include <QDebug>
#include <QEvent>
#include <QFile>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QTimer>

namespace casdap {

namespace {

constexpr uint nameAllowReplacement = 0x1;
constexpr uint nameReplaceExisting = 0x2;
constexpr uint nameDoNotQueue = 0x4;
constexpr uint primaryOwner = 1;
constexpr uint alreadyOwner = 4;

std::atomic<int> connectionCount{0};

QString busService()
{
    return QStringLiteral("org.freedesktop.DBus");
}

QString busPath()
{
    return QStringLiteral("/org/freedesktop/DBus");
}

QString localPath()
{
    return QStringLiteral("/org/freedesktop/DBus/Local");
}

QString localInterface()
{
    return QStringLiteral("org.freedesktop.DBus.Local");
}

QDBusMessage busCall(const QString &method)
{
    return QDBusMessage::createMethodCall(busService(), busPath(), busService(), method);
}

bool noOwner(const QString &error)
{
    return error == QLatin1StringView("org.freedesktop.DBus.Error.ServiceUnknown")
        || error == QLatin1StringView("org.freedesktop.DBus.Error.NameHasNoOwner");
}

bool timedOut(const QString &error)
{
    return error == QLatin1StringView("org.freedesktop.DBus.Error.NoReply")
        || error == QLatin1StringView("org.freedesktop.DBus.Error.Timeout");
}

}

DeviceReservation::DeviceReservation(const Bounds &bounds, QObject *parent)
    : QObject(parent)
    , m_bounds(bounds)
    , m_shared(new ReserveShared)
    , m_thread(new QThread)
    , m_worker(new ReserveWorker(bounds, m_shared))
{
    m_thread->setObjectName(QStringLiteral("casdap-reserve"));
    m_worker->moveToThread(m_thread);
    connect(m_worker, &ReserveWorker::acquired, this, &DeviceReservation::acquired, Qt::QueuedConnection);
    connect(m_worker, &ReserveWorker::refused, this, &DeviceReservation::refused, Qt::QueuedConnection);
    connect(m_worker, &ReserveWorker::failed, this, &DeviceReservation::failed, Qt::QueuedConnection);
    connect(m_worker, &ReserveWorker::releaseRequested, this, &DeviceReservation::onReleaseRequested,
            Qt::QueuedConnection);
    connect(m_worker, &ReserveWorker::releaseTimedOut, this, &DeviceReservation::releaseTimedOut,
            Qt::QueuedConnection);
    connect(m_worker, &ReserveWorker::nameLost, this, &DeviceReservation::nameLost, Qt::QueuedConnection);
    connect(m_worker, &ReserveWorker::connectionLost, this, &DeviceReservation::connectionLost,
            Qt::QueuedConnection);
    connect(m_worker, &ReserveWorker::released, this, &DeviceReservation::released, Qt::QueuedConnection);
    m_thread->start();
}

DeviceReservation::~DeviceReservation()
{
    const int bound = m_bounds.callMs * 3;
    if (!m_joined)
        quitAndJoin(bound);
    else
        m_thread->wait(QDeadlineTimer(bound));
    if (m_thread->isRunning()) {
        qWarning() << "casdap reserve: thread did not stop within" << bound << "ms, left running";
        return;
    }
    delete m_worker;
    delete m_thread;
    delete m_shared;
}

QString DeviceReservation::interfaceName()
{
    return QStringLiteral("org.freedesktop.ReserveDevice1");
}

QString DeviceReservation::serviceFor(const QString &reserveName)
{
    return QStringLiteral("org.freedesktop.ReserveDevice1.") + reserveName;
}

QString DeviceReservation::pathFor(const QString &reserveName)
{
    return QStringLiteral("/org/freedesktop/ReserveDevice1/") + reserveName;
}

QString DeviceReservation::applicationName()
{
    return QStringLiteral("casdap");
}

void DeviceReservation::acquire(const QString &reserveName, const QString &cardId)
{
    if (m_joined)
        return;
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, reserveName, cardId] {
        worker->acquire(reserveName, cardId);
    }, Qt::QueuedConnection);
}

void DeviceReservation::grantRelease()
{
    if (m_joined)
        return;
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, upTo = m_lastRequest] {
        worker->grant(upTo);
    }, Qt::QueuedConnection);
}

void DeviceReservation::release()
{
    if (m_joined)
        return;
    quint64 seq = 0;
    {
        QMutexLocker locker(&m_shared->mutex);
        seq = ++m_requested;
    }
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, seq] {
        worker->release(seq);
    }, Qt::QueuedConnection);
}

void DeviceReservation::markGone()
{
    if (m_joined)
        return;
    QMetaObject::invokeMethod(m_worker, &ReserveWorker::markGone, Qt::QueuedConnection);
}

bool DeviceReservation::waitReleased(int ms)
{
    if (m_joined)
        return true;
    QMutexLocker locker(&m_shared->mutex);
    const QDeadlineTimer deadline(ms);
    while (m_shared->completed < m_requested || m_shared->held) {
        if (!m_shared->done.wait(&m_shared->mutex, deadline))
            return m_shared->completed >= m_requested && !m_shared->held;
    }
    return true;
}

void DeviceReservation::quitAndJoin(int ms)
{
    if (m_joined)
        return;
    m_joined = true;
    quint64 seq = 0;
    {
        QMutexLocker locker(&m_shared->mutex);
        seq = ++m_requested;
    }
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, seq] {
        worker->quit(seq);
    }, Qt::QueuedConnection);
    if (!m_thread->wait(QDeadlineTimer(ms)))
        qWarning() << "casdap reserve: thread still running" << ms << "ms after quit";
}

void DeviceReservation::onReleaseRequested(quint64 id, int priority)
{
    if (id <= m_shared->answered.load()) {
        qInfo() << "casdap reserve: RequestRelease" << priority << "answered before the release ran, dropped";
        return;
    }
    m_lastRequest = id;
    emit releaseRequested(priority);
}

ReserveEndpointAdaptor::ReserveEndpointAdaptor(ReserveEndpoint *endpoint)
    : QDBusAbstractAdaptor(endpoint)
    , m_endpoint(endpoint)
{
}

int ReserveEndpointAdaptor::priority() const
{
    return DeviceReservation::ownPriority;
}

QString ReserveEndpointAdaptor::applicationName() const
{
    return DeviceReservation::applicationName();
}

QString ReserveEndpointAdaptor::applicationDeviceName() const
{
    return m_endpoint->deviceName();
}

bool ReserveEndpointAdaptor::RequestRelease(int priority)
{
    return m_endpoint->requestRelease(priority);
}

ReserveEndpoint::ReserveEndpoint(ReserveWorker *worker, const QString &deviceName)
    : QObject(worker)
    , m_worker(worker)
    , m_deviceName(deviceName)
{
    new ReserveEndpointAdaptor(this);
}

QString ReserveEndpoint::deviceName() const
{
    return m_deviceName;
}

bool ReserveEndpoint::requestRelease(int priority)
{
    return m_worker->onRequest(priority, this);
}

QDBusMessage ReserveEndpoint::delayReply()
{
    setDelayedReply(true);
    return message();
}

ReserveWorker::ReserveWorker(const DeviceReservation::Bounds &bounds, ReserveShared *shared)
    : m_bounds(bounds)
    , m_shared(shared)
    , m_bus(QString())
    , m_nameLostTimer(new QTimer(this))
{
    m_nameLostTimer->setSingleShot(true);
    m_nameLostTimer->setInterval(bounds.nameLostMs);
    connect(m_nameLostTimer, &QTimer::timeout, this, [this] {
        qWarning().noquote() << "casdap reserve: no NameLost for" << m_service << "within" << m_bounds.nameLostMs
                             << "ms of a true reply, releasing";
        releaseNow();
        emit releaseTimedOut();
    });
}

void ReserveWorker::acquire(const QString &reserveName, const QString &cardId)
{
    if (m_owned || m_endpoint)
        releaseNow();
    ++m_generation;
    m_holdStart = m_nextCall;
    static const QRegularExpression nameRe(QStringLiteral("^Audio(\\d+)$"));
    const QRegularExpressionMatch match = nameRe.match(reserveName);
    if (!match.hasMatch() || cardId.isEmpty()) {
        qWarning().noquote() << "casdap reserve: cannot reserve" << reserveName << "for card" << cardId;
        emit failed(QStringLiteral("name"));
        return;
    }
    m_service = DeviceReservation::serviceFor(reserveName);
    m_path = DeviceReservation::pathFor(reserveName);
    m_cardId = cardId;
    m_card = match.captured(1).toInt();
    m_gone = false;
    m_granted = false;
    if (!openBus()) {
        emit failed(QStringLiteral("bus"));
        return;
    }
    qInfo().noquote() << "casdap reserve: acquiring" << m_service << "for hw:CARD=" + cardId << "on"
                      << m_connectionName;
    QDBusMessage call = QDBusMessage::createMethodCall(m_service, m_path, DeviceReservation::interfaceName(),
                                                       QStringLiteral("RequestRelease"));
    call.setAutoStartService(false);
    call << DeviceReservation::ownPriority;
    const quint64 generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, m_bounds.callMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        if (generation != m_generation)
            return;
        const QDBusMessage reply = finished->reply();
        if (reply.type() == QDBusMessage::ErrorMessage && timedOut(reply.errorName())) {
            qWarning().noquote() << "casdap reserve: RequestRelease(10) on" << m_service
                                 << "timed out, taking and releasing the name";
            takeName(false);
            releaseNow();
            emit failed(QStringLiteral("timeout"));
            return;
        }
        if (reply.type() == QDBusMessage::ErrorMessage && !noOwner(reply.errorName())) {
            qWarning().noquote() << "casdap reserve: RequestRelease(10) on" << m_service << "failed"
                                 << reply.errorName() << reply.errorMessage();
            emit failed(reply.errorName());
            return;
        }
        if (reply.type() != QDBusMessage::ErrorMessage && !reply.arguments().value(0).toBool()) {
            qInfo().noquote() << "casdap reserve: owner of" << m_service << "refused";
            emit refused();
            return;
        }
        takeName(true);
    });
}

bool ReserveWorker::takeName(bool announce)
{
    m_endpoint = new ReserveEndpoint(this, QStringLiteral("hw:CARD=") + m_cardId);
    if (!m_bus.registerObject(m_path, m_endpoint, QDBusConnection::ExportAdaptors)) {
        qWarning().noquote() << "casdap reserve: cannot export" << m_path << m_bus.lastError().message();
        delete m_endpoint;
        m_endpoint = nullptr;
        if (announce)
            emit failed(QStringLiteral("export"));
        return false;
    }
    QDBusMessage call = busCall(QStringLiteral("RequestName"));
    call << m_service << (nameDoNotQueue | nameReplaceExisting | nameAllowReplacement);
    const QDBusMessage reply = m_bus.call(call, QDBus::Block, m_bounds.callMs);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning().noquote() << "casdap reserve: RequestName" << m_service << "failed" << reply.errorName()
                             << reply.errorMessage();
        unexport();
        if (announce)
            emit failed(reply.errorName());
        return false;
    }
    const uint code = reply.arguments().value(0).toUInt();
    if (code != primaryOwner && code != alreadyOwner) {
        qInfo().noquote() << "casdap reserve:" << m_service << "kept by its owner, RequestName" << code;
        unexport();
        if (announce)
            emit refused();
        return false;
    }
    m_owned = true;
    setHeld(true);
    qInfo().noquote() << "casdap reserve: holding" << m_service;
    if (announce)
        emit acquired();
    return true;
}

bool ReserveWorker::onRequest(int priority, ReserveEndpoint *endpoint)
{
    if (!m_owned || m_releasing) {
        qInfo().noquote() << "casdap reserve: RequestRelease(" + QString::number(priority) + ") while not held, true";
        return true;
    }
    if (priority <= DeviceReservation::ownPriority) {
        const bool gone = m_gone || cardGone();
        qInfo().noquote() << "casdap reserve: RequestRelease(" + QString::number(priority) + ")"
                          << (gone ? "true, card gone" : "false");
        if (gone)
            granted();
        return gone;
    }
    const quint64 id = ++m_nextCall;
    m_calls.append({id, endpoint->delayReply()});
    QTimer::singleShot(m_bounds.replyMs, this, [this, id] {
        expire(id);
    });
    qInfo().noquote() << "casdap reserve: RequestRelease(" + QString::number(priority) + ") asks for a release";
    emit releaseRequested(id, priority);
    return false;
}

void ReserveWorker::grant(quint64 upTo)
{
    bool any = false;
    for (auto it = m_calls.begin(); it != m_calls.end();) {
        if (it->id > upTo) {
            ++it;
            continue;
        }
        reply(it->message, true);
        answered(it->id);
        it = m_calls.erase(it);
        any = true;
    }
    if (any) {
        qInfo().noquote() << "casdap reserve: release granted for" << m_service;
        granted();
        return;
    }
    if (m_owned && !m_granted && upTo > m_holdStart && upTo <= m_expired) {
        qWarning().noquote() << "casdap reserve: grant after the false reply, releasing" << m_service;
        releaseNow();
        emit released();
    }
}

void ReserveWorker::release(quint64 seq)
{
    releaseNow();
    complete(seq);
    emit released();
}

void ReserveWorker::markGone()
{
    m_gone = true;
    qInfo().noquote() << "casdap reserve: hold on" << m_service << "marked gone";
}

void ReserveWorker::quit(quint64 seq)
{
    releaseNow();
    closeBus();
    complete(seq);
    thread()->quit();
}

void ReserveWorker::onNameLost(const QString &name)
{
    if (name != m_service || !m_owned || m_releasing)
        return;
    const bool requested = m_granted;
    m_owned = false;
    m_nameLostTimer->stop();
    answerAll();
    unexport();
    flush();
    m_granted = false;
    setHeld(false);
    qInfo().noquote() << "casdap reserve: lost" << m_service << (requested ? "to the requester" : "unrequested");
    emit nameLost(requested);
}

void ReserveWorker::onDisconnected()
{
    if (m_closing)
        return;
    qWarning().noquote() << "casdap reserve: connection" << m_connectionName << "dropped";
    ++m_generation;
    m_owned = false;
    m_granted = false;
    m_nameLostTimer->stop();
    m_calls.clear();
    delete m_endpoint;
    m_endpoint = nullptr;
    setHeld(false);
    emit connectionLost();
}

bool ReserveWorker::openBus()
{
    closeBus();
    m_connectionName = QStringLiteral("casdap-reserve-%1").arg(++connectionCount);
    const QString address = qEnvironmentVariable("CASDAP_RESERVE_BUS");
    m_bus = address.isEmpty() ? QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_connectionName)
                              : QDBusConnection::connectToBus(address, m_connectionName);
    if (!m_bus.isConnected()) {
        qWarning().noquote() << "casdap reserve: no bus" << m_bus.lastError().message();
        closeBus();
        return false;
    }
    if (!m_bus.connect(QString(), busPath(), busService(), QStringLiteral("NameLost"), this,
                       SLOT(onNameLost(QString)))) {
        qWarning().noquote() << "casdap reserve: cannot watch NameLost" << m_bus.lastError().message();
    }
    if (!m_bus.connect(QString(), localPath(), localInterface(), QStringLiteral("Disconnected"), this,
                       SLOT(onDisconnected()))) {
        qWarning().noquote() << "casdap reserve: cannot watch the connection" << m_bus.lastError().message();
    }
    return true;
}

void ReserveWorker::closeBus()
{
    if (m_connectionName.isEmpty())
        return;
    m_closing = true;
    m_bus.disconnect(QString(), busPath(), busService(), QStringLiteral("NameLost"), this, SLOT(onNameLost(QString)));
    m_bus.disconnect(QString(), localPath(), localInterface(), QStringLiteral("Disconnected"), this,
                     SLOT(onDisconnected()));
    flush();
    QDBusConnection::disconnectFromBus(m_connectionName);
    m_bus = QDBusConnection(QString());
    m_connectionName.clear();
    m_closing = false;
}

void ReserveWorker::answered(quint64 id)
{
    if (id > m_shared->answered.load())
        m_shared->answered.store(id);
}

void ReserveWorker::reply(const QDBusMessage &message, bool value)
{
    if (!m_bus.send(message.createReply(QVariant(value))))
        qWarning().noquote() << "casdap reserve: reply to RequestRelease not sent" << m_bus.lastError().message();
}

void ReserveWorker::expire(quint64 id)
{
    for (qsizetype i = 0; i < m_calls.size(); ++i) {
        if (m_calls.at(i).id != id)
            continue;
        reply(m_calls.at(i).message, false);
        m_calls.removeAt(i);
        m_expired = qMax(m_expired, id);
        answered(id);
        qWarning().noquote() << "casdap reserve: no release within" << m_bounds.replyMs << "ms, answered false";
        return;
    }
}

void ReserveWorker::granted()
{
    m_granted = true;
    m_nameLostTimer->start();
}

void ReserveWorker::answerAll()
{
    for (const Call &call : std::as_const(m_calls)) {
        reply(call.message, true);
        answered(call.id);
    }
    m_calls.clear();
}

void ReserveWorker::unexport()
{
    if (!m_endpoint)
        return;
    QCoreApplication::sendPostedEvents(m_endpoint, QEvent::MetaCall);
    m_bus.unregisterObject(m_path);
    delete m_endpoint;
    m_endpoint = nullptr;
}

void ReserveWorker::releaseNow()
{
    m_releasing = true;
    m_nameLostTimer->stop();
    ++m_generation;
    answerAll();
    if (m_owned) {
        QDBusMessage call = busCall(QStringLiteral("ReleaseName"));
        call << m_service;
        const QDBusMessage reply = m_bus.call(call, QDBus::Block, m_bounds.callMs);
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qWarning().noquote() << "casdap reserve: ReleaseName" << m_service << "failed" << reply.errorName()
                                 << reply.errorMessage();
        }
        m_owned = false;
        qInfo().noquote() << "casdap reserve: released" << m_service << "ReleaseName"
                          << reply.arguments().value(0).toUInt();
    }
    unexport();
    flush();
    m_granted = false;
    m_releasing = false;
    setHeld(false);
}

void ReserveWorker::flush()
{
    if (m_bus.isConnected())
        m_bus.call(busCall(QStringLiteral("GetId")), QDBus::Block, m_bounds.callMs);
}

void ReserveWorker::setHeld(bool held)
{
    {
        QMutexLocker locker(&m_shared->mutex);
        m_shared->held = held;
    }
    m_shared->done.wakeAll();
}

void ReserveWorker::complete(quint64 seq)
{
    {
        QMutexLocker locker(&m_shared->mutex);
        if (seq > m_shared->completed)
            m_shared->completed = seq;
    }
    m_shared->done.wakeAll();
}

bool ReserveWorker::cardGone() const
{
    QFile file(AudioInfo::asoundDir() + QStringLiteral("/card%1/id").arg(m_card));
    if (!file.open(QIODevice::ReadOnly))
        return true;
    return QString::fromUtf8(file.readAll()).trimmed() != m_cardId;
}

}
