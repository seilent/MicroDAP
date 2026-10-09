#include "SscRotation.h"

#include "QrtrLink.h"
#include "SensorRotation.h"
#include "SleepInhibitor.h"
#include "SscLink.h"

#include <QDebug>

namespace casdap {

namespace {

QByteArray dataType()
{
    return QByteArrayLiteral("device_orient");
}

QString suidHex(const ssc::Suid &suid)
{
    return QStringLiteral("%1%2").arg(suid.high, 16, 16, QLatin1Char('0')).arg(suid.low, 16, 16, QLatin1Char('0'));
}

}

SscRotation::SscRotation(QObject *parent)
    : SscRotation(new QrtrLink, nullptr, parent)
{
    m_sleep = new SleepInhibitor(QStringLiteral("release sensor"), this);
    watchSleep();
}

SscRotation::SscRotation(SscLink *link, SleepInhibitor *sleep, QObject *parent)
    : QObject(parent)
    , m_link(link)
    , m_sleep(sleep)
    , m_retryBase(defaultRetryMs())
    , m_retryDelay(m_retryBase)
    , m_sensorTurn(SensorRotation::defaultSensorTurn())
{
    m_link->setParent(this);
    m_settle.setSingleShot(true);
    m_settle.setInterval(settleMs);
    connect(&m_settle, &QTimer::timeout, this, &SscRotation::settle);
    m_hold.setSingleShot(true);
    m_hold.setInterval(defaultReclaimMs());
    connect(&m_hold, &QTimer::timeout, this, &SscRotation::lose);
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &SscRotation::requestSuid);
    m_enableRetry.setSingleShot(true);
    m_enableRetry.setInterval(m_retryBase);
    connect(&m_enableRetry, &QTimer::timeout, this, &SscRotation::maybeEnable);
    m_reopen.setSingleShot(true);
    m_reopen.setInterval(m_retryBase);
    connect(&m_reopen, &QTimer::timeout, this, &SscRotation::onReopen);
    m_sleepRelease.setSingleShot(true);
    m_sleepRelease.setInterval(sleepReleaseMs);
    connect(&m_sleepRelease, &QTimer::timeout, this, &SscRotation::releaseSleep);
    connect(m_link, &SscLink::received, this, &SscRotation::onReceived);
    connect(m_link, &SscLink::failed, this, &SscRotation::onLinkFailed);
    watchSleep();

    if (!m_link->open())
        return;
    sendLookup();
}

SscRotation::~SscRotation()
{
    if (m_enableTxn)
        disable();
}

int SscRotation::defaultReclaimMs()
{
    bool ok = false;
    const int ms = qEnvironmentVariableIntValue("CASDAP_SENSOR_RECLAIM_MS", &ok);
    return ok && ms > 0 ? ms : reclaimMs;
}

int SscRotation::defaultRetryMs()
{
    bool ok = false;
    const int ms = qEnvironmentVariableIntValue("CASDAP_SSC_RETRY_MS", &ok);
    return ok && ms > 0 ? ms : retryMs;
}

int SscRotation::turnOf(int value, int sensorTurn)
{
    switch (value) {
    case 0:
        return 0;
    case 1:
        return sensorTurn;
    case 2:
        return 2;
    case 3:
        return (sensorTurn + 2) % 4;
    default:
        return -1;
    }
}

bool SscRotation::supported() const
{
    return m_supported;
}

int SscRotation::turn() const
{
    return m_turn;
}

QString SscRotation::raw() const
{
    return m_raw;
}

QString SscRotation::settledRaw() const
{
    return m_settledRaw;
}

bool SscRotation::wanted() const
{
    return m_wanted;
}

bool SscRotation::claimed() const
{
    return m_claimed;
}

int SscRotation::sensorTurn() const
{
    return m_sensorTurn;
}

void SscRotation::setWanted(bool wanted, bool keepTurn)
{
    if (!wanted && !keepTurn) {
        m_last = -1;
        setSettledRaw(QString());
    }
    if (m_wanted == wanted)
        return;
    m_wanted = wanted;
    if (wanted) {
        maybeEnable();
        return;
    }
    m_settle.stop();
    m_enableRetry.stop();
    if (m_holding) {
        m_hold.stop();
        m_holding = false;
        updateSupported();
    }
    if (m_enableTxn)
        disable();
    m_claimed = false;
    setTurn(-1);
}

void SscRotation::watchSleep()
{
    if (!m_sleep)
        return;
    connect(m_sleep, &SleepInhibitor::prepareForSleep, this, &SscRotation::onPrepareForSleep);
    m_sleep->take();
}

void SscRotation::onReceived(quint32 node, quint32 port, const QByteArray &bytes)
{
    if (port == ssc::controlPort) {
        handleControl(bytes);
        return;
    }
    if (!m_server || node != m_server->node || port != m_server->port)
        return;
    const std::optional<ssc::Message> message = ssc::parse(bytes);
    if (!message)
        return;
    if (message->type == ssc::typeResponse && message->msgId == ssc::msgControl) {
        handleResponse(*message);
        return;
    }
    if (message->type != ssc::typeIndication
        || (message->msgId != ssc::msgReportSmall && message->msgId != ssc::msgReportLarge))
        return;
    const std::optional<QByteArrayView> data = message->data();
    if (!data)
        return;
    const std::optional<ssc::Report> report = ssc::parseReport(*data);
    if (report)
        handleReport(*report);
}

void SscRotation::onLinkFailed()
{
    dropServer();
    m_reopen.start();
}

void SscRotation::onReopen()
{
    if (m_link->reopen())
        sendLookup();
}

void SscRotation::onPrepareForSleep(bool start)
{
    if (start) {
        m_asleep = true;
        m_enableRetry.stop();
        if (m_enableTxn) {
            m_settle.stop();
            m_sleepTxn = disable();
            if (m_sleepTxn) {
                m_sleepRelease.start();
                return;
            }
        }
        releaseSleep();
        return;
    }
    m_asleep = false;
    m_sleepRelease.stop();
    m_sleepTxn = 0;
    if (m_sleep)
        m_sleep->take();
    maybeEnable();
}

void SscRotation::handleControl(const QByteArray &bytes)
{
    const std::optional<ssc::Control> control = ssc::parseControl(bytes);
    if (!control || control->service != ssc::service)
        return;
    if (control->cmd == ssc::newServer && !m_server) {
        m_server = control;
        m_retryDelay = m_retryBase;
        qInfo() << "casdap sensor: ssc service" << control->node << control->port;
        requestSuid();
        return;
    }
    if (control->cmd == ssc::delServer && m_server && control->node == m_server->node
        && control->port == m_server->port && control->instance == m_server->instance)
        dropServer();
}

void SscRotation::handleResponse(const ssc::Message &message)
{
    const auto entry = m_requests.constFind(message.txn);
    if (entry == m_requests.constEnd())
        return;
    const Request kind = entry.value();
    m_requests.erase(entry);
    const std::optional<ssc::Result> result = message.result();
    const bool ok = result && result->result == 0;
    if (kind == Request::Enable) {
        if (message.txn != m_enableTxn)
            return;
        if (!ok) {
            qWarning() << "casdap sensor: ssc enable failed" << (result ? result->error : 0);
            m_enableTxn = 0;
            return;
        }
        m_claimed = true;
        qInfo() << "casdap sensor: ssc enabled";
        setTurn(m_last);
        send(Request::Flush, ssc::request(*m_suid, ssc::msgFlush, std::nullopt));
        return;
    }
    if (!ok)
        qWarning() << "casdap sensor: ssc request failed" << (result ? result->error : 0);
    if (kind == Request::Disable && m_sleepTxn && message.txn == m_sleepTxn)
        releaseSleep();
}

void SscRotation::handleReport(const ssc::Report &report)
{
    const bool lookup = report.suid == ssc::lookupSuid;
    if (!lookup && !(m_suid && report.suid == *m_suid))
        return;
    for (const ssc::Event &event : report.events) {
        if (event.msgId == ssc::msgError) {
            qWarning() << "casdap sensor: ssc error" << ssc::uintField(event.payload, 1).value_or(0);
            continue;
        }
        if (lookup && event.msgId == ssc::msgSuidEvent)
            handleSuid(event.payload);
        else if (!lookup && event.msgId == ssc::msgOrientEvent)
            handleValue(event.payload);
    }
}

void SscRotation::handleSuid(const QByteArray &payload)
{
    if (m_suid)
        return;
    QByteArray type;
    std::optional<ssc::Suid> suid;
    for (const ssc::Field &field : ssc::fields(payload)) {
        if (field.type != ssc::WireType::Bytes)
            continue;
        if (field.id == 1)
            type = field.bytes.toByteArray();
        else if (field.id == 2 && !suid)
            suid = ssc::parseSuid(field.bytes);
    }
    if (type != dataType())
        return;
    if (!suid) {
        scheduleRetry();
        return;
    }
    m_suid = suid;
    m_retry.stop();
    m_retryDelay = m_retryBase;
    qInfo().noquote() << "casdap sensor: ssc device_orient" << suidHex(*suid);
    m_hold.stop();
    m_holding = false;
    updateSupported();
    maybeEnable();
}

void SscRotation::handleValue(const QByteArray &payload)
{
    if (!m_claimed)
        return;
    const std::optional<quint64> value = ssc::uintField(payload, 1);
    if (!value)
        return;
    qInfo() << "casdap sensor: ssc value" << *value;
    const int orientation = *value <= 3 ? int(*value) : -1;
    if (turnOf(orientation, m_sensorTurn) < 0)
        return;
    take(QString::number(orientation));
}

void SscRotation::sendLookup()
{
    if (!m_link->send(m_link->localNode(), ssc::controlPort, ssc::lookup(ssc::service)))
        qWarning() << "casdap sensor: ssc lookup failed";
}

quint16 SscRotation::send(Request kind, const QByteArray &request)
{
    if (!m_server)
        return 0;
    m_txn = m_txn == 0xffff ? 1 : quint16(m_txn + 1);
    if (!m_link->send(m_server->node, m_server->port, ssc::control(m_txn, request)))
        return 0;
    m_requests.insert(m_txn, kind);
    return m_txn;
}

void SscRotation::requestSuid()
{
    if (!m_server || m_suid)
        return;
    if (!send(Request::Suid, ssc::suidRequest(dataType())))
        scheduleRetry();
}

void SscRotation::scheduleRetry()
{
    m_retry.start(m_retryDelay);
    m_retryDelay = qMin(m_retryDelay * 2, maxRetryMs);
}

void SscRotation::maybeEnable()
{
    if (!m_wanted || !m_suid || !m_server || m_asleep || m_enableTxn)
        return;
    m_enableTxn = send(Request::Enable, ssc::request(*m_suid, ssc::msgOnChange, QByteArray()));
    if (!m_enableTxn)
        m_enableRetry.start();
}

quint16 SscRotation::disable()
{
    quint16 txn = 0;
    if (m_suid)
        txn = send(Request::Disable, ssc::request(*m_suid, ssc::msgDisable, std::nullopt));
    m_enableTxn = 0;
    m_claimed = false;
    qInfo() << "casdap sensor: ssc released";
    return txn;
}

void SscRotation::dropServer()
{
    const bool held = m_supported || m_claimed;
    m_server.reset();
    m_suid.reset();
    m_enableTxn = 0;
    m_claimed = false;
    m_requests.clear();
    m_settle.stop();
    m_retry.stop();
    m_enableRetry.stop();
    if (m_wanted && held) {
        m_holding = true;
        m_hold.start();
        updateSupported();
        return;
    }
    lose();
}

void SscRotation::releaseSleep()
{
    m_sleepRelease.stop();
    m_sleepTxn = 0;
    if (m_sleep)
        m_sleep->release();
}

void SscRotation::lose()
{
    m_hold.stop();
    m_holding = false;
    m_settle.stop();
    m_last = -1;
    setSettledRaw(QString());
    setTurn(-1);
    updateSupported();
}

void SscRotation::take(const QString &raw)
{
    setRaw(raw);
    const int turn = turnOf(raw.toInt(), m_sensorTurn);
    if (turn < 0 || turn == m_turn) {
        m_settle.stop();
        return;
    }
    if (turn == m_pending && m_settle.isActive())
        return;
    m_pending = turn;
    m_pendingRaw = raw;
    m_settle.start();
}

void SscRotation::settle()
{
    if (!m_claimed)
        return;
    m_last = m_pending;
    setSettledRaw(m_pendingRaw);
    setTurn(m_pending);
}

void SscRotation::updateSupported()
{
    const bool supported = m_suid.has_value() || m_holding;
    if (m_supported == supported)
        return;
    m_supported = supported;
    emit supportedChanged();
}

void SscRotation::setTurn(int turn)
{
    if (m_turn == turn)
        return;
    m_turn = turn;
    emit turnChanged();
}

void SscRotation::setRaw(const QString &raw)
{
    if (m_raw == raw)
        return;
    m_raw = raw;
    emit rawChanged();
}

void SscRotation::setSettledRaw(const QString &raw)
{
    if (m_settledRaw == raw)
        return;
    m_settledRaw = raw;
    emit settledRawChanged();
}

}
