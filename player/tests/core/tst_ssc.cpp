#include "SleepInhibitor.h"
#include "SscLink.h"
#include "SscRotation.h"
#include "SscWire.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

#include <memory>
#include <optional>

using namespace casdap;

namespace {

constexpr int busTimeoutMs = 5000;
constexpr int retryMs = 50;
constexpr int holdMs = 300;
constexpr int shortMs = 100;
constexpr quint32 fakeNode = 1;
constexpr quint32 serverNode = 9;
constexpr quint32 serverPort = 13;
constexpr quint32 otherPort = 14;
constexpr ssc::Suid orientSuid{0x1122334455667788ull, 0x99aabbccddeeff00ull};
constexpr ssc::Suid otherSuid{0x0102030405060708ull, 0x1112131415161718ull};

struct Datagram
{
    quint32 node = 0;
    quint32 port = 0;
    QByteArray bytes;
};

struct Sent
{
    quint16 txn = 0;
    quint32 msgId = 0;
    ssc::Suid suid;
    QByteArray body;
};

class FakeLink : public SscLink
{
public:
    FakeLink(QList<Datagram> *sent, bool open)
        : m_sent(sent)
        , m_open(open)
    {
    }

    bool open() const override
    {
        return m_open;
    }

    quint32 localNode() const override
    {
        return fakeNode;
    }

    bool send(quint32 node, quint32 port, const QByteArray &bytes) override
    {
        if (m_failSends > 0) {
            --m_failSends;
            return false;
        }
        m_sent->append(Datagram{node, port, bytes});
        return true;
    }

    bool reopen() override
    {
        ++m_reopens;
        m_open = true;
        return true;
    }

    void inject(quint32 node, quint32 port, const QByteArray &bytes)
    {
        emit received(node, port, bytes);
    }

    void fail()
    {
        m_open = false;
        emit failed();
    }

    void failSends(int count)
    {
        m_failSends = count;
    }

    int reopens() const
    {
        return m_reopens;
    }

private:
    QList<Datagram> *m_sent = nullptr;
    bool m_open = true;
    int m_failSends = 0;
    int m_reopens = 0;
};

void putLe16(QByteArray &buf, quint16 value)
{
    buf.append(char(value & 0xff));
    buf.append(char(value >> 8));
}

void putLe32(QByteArray &buf, quint32 value)
{
    for (int i = 0; i < 4; ++i)
        buf.append(char((value >> (8 * i)) & 0xff));
}

QByteArray bytesOf(std::initializer_list<int> values)
{
    QByteArray buf;
    for (const int value : values)
        buf.append(char(value));
    return buf;
}

QByteArray controlPacket(quint32 cmd, quint32 service, quint32 node, quint32 port)
{
    QByteArray buf;
    putLe32(buf, cmd);
    putLe32(buf, service);
    putLe32(buf, service ? 1 : 0);
    putLe32(buf, node);
    putLe32(buf, port);
    return buf;
}

QByteArray qmi(quint8 type, quint16 txn, quint16 msgId, const QByteArray &tlvs)
{
    QByteArray buf;
    buf.append(char(type));
    putLe16(buf, txn);
    putLe16(buf, msgId);
    putLe16(buf, quint16(tlvs.size()));
    buf.append(tlvs);
    return buf;
}

QByteArray response(quint16 txn, quint16 result = 0, quint16 error = 0)
{
    QByteArray tlvs;
    tlvs.append(char(2));
    putLe16(tlvs, 4);
    putLe16(tlvs, result);
    putLe16(tlvs, error);
    return qmi(ssc::typeResponse, txn, ssc::msgControl, tlvs);
}

QByteArray event(quint32 msgId, const QByteArray &payload)
{
    QByteArray buf;
    ssc::putFixed32(buf, 1, msgId);
    ssc::putBytes(buf, 3, payload);
    return buf;
}

QByteArray indication(const ssc::Suid &suid, const QList<QByteArray> &events)
{
    QByteArray report;
    ssc::putBytes(report, 1, ssc::suidBytes(suid));
    for (const QByteArray &entry : events)
        ssc::putBytes(report, 2, entry);
    QByteArray data;
    putLe16(data, quint16(report.size()));
    data.append(report);
    QByteArray tlvs;
    tlvs.append(char(2));
    putLe16(tlvs, quint16(data.size()));
    tlvs.append(data);
    return qmi(ssc::typeIndication, 0, ssc::msgReportLarge, tlvs);
}

QByteArray suidAnswer(const std::optional<ssc::Suid> &suid)
{
    QByteArray payload;
    ssc::putBytes(payload, 1, QByteArrayLiteral("device_orient"));
    if (suid)
        ssc::putBytes(payload, 2, ssc::suidBytes(*suid));
    return indication(ssc::lookupSuid, {event(ssc::msgSuidEvent, payload)});
}

QByteArray orientEvent(quint64 value)
{
    QByteArray payload;
    ssc::putUint(payload, 1, value);
    return event(ssc::msgOrientEvent, payload);
}

QByteArray orient(quint64 value, const ssc::Suid &suid = orientSuid)
{
    return indication(suid, {orientEvent(value)});
}

std::optional<Sent> decode(const Datagram &datagram)
{
    const std::optional<ssc::Message> message = ssc::parse(datagram.bytes);
    if (!message || message->type != ssc::typeRequest || message->msgId != ssc::msgControl)
        return std::nullopt;
    const std::optional<QByteArrayView> tlv = message->tlv(1);
    if (!tlv || tlv->size() < 2)
        return std::nullopt;
    Sent sent;
    sent.txn = message->txn;
    for (const ssc::Field &field : ssc::fields(tlv->sliced(2))) {
        if (field.id == 1 && field.type == ssc::WireType::Bytes)
            sent.suid = ssc::parseSuid(field.bytes).value_or(ssc::Suid{});
        else if (field.id == 2 && field.type == ssc::WireType::Fixed32)
            sent.msgId = quint32(field.value);
        else if (field.id == 4 && field.type == ssc::WireType::Bytes)
            sent.body = field.bytes.toByteArray();
    }
    return sent;
}

}

class SscTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void cleanupTestCase();
    void wireRoundTrip();
    void wireTruncated();
    void wireControl();
    void wireReport();
    void wireShort();
    void wireRequest();
    void turnOf();
    void lookupOnStart();
    void closedLink();
    void otherServiceIgnored();
    void suidSetsSupported();
    void secondSuidIgnored();
    void emptySuidRetries();
    void endMarkerIgnored();
    void enableFlushes();
    void staleTxnDoesNotClaim();
    void lateEnableAfterRelease();
    void suidRetryResponseDoesNotClaim();
    void valueBeforeEnableDropped();
    void twoEventsThenUnknown();
    void shortValueReverted();
    void otherSuidOrPortDropped();
    void releaseKeepingThenClearing();
    void delServerHoldsThenLoses();
    void delServerNotWanted();
    void enableFailure();
    void linkFailureReopens();
    void failedEnableRetries();
    void destructorWhileEnabling();
    void destructorWhileClaimed();
    void sleepReleases();
    void sleepReleaseTimeout();
    void sleepIdleReleasesAtOnce();
    void lateEnableAcrossSleep();
    void suidDuringSleep();

private:
    bool startLogin1();
    QDBusMessage login1(const QString &method, const QList<QVariant> &arguments = {});
    void make(bool open = true);
    void makeSleeping();
    void serve(quint32 port = serverPort);
    void answer(const std::optional<ssc::Suid> &suid, quint32 port = serverPort);
    void inject(const QByteArray &bytes, quint32 port = serverPort);
    void ready();
    void claim();
    void claimSettled(quint64 value);
    QList<Sent> sentOf(quint32 msgId) const;
    int count(quint32 msgId) const;
    quint16 lastTxn(quint32 msgId) const;

    QProcess m_login1;
    bool m_login1Running = false;
    QList<Datagram> m_sent;
    FakeLink *m_link = nullptr;
    std::unique_ptr<SleepInhibitor> m_sleep;
    std::unique_ptr<SscRotation> m_rotation;
};

bool SscTest::startLogin1()
{
    if (!QDBusConnection::sessionBus().isConnected())
        return false;
    m_login1.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_login1.start(QStringLiteral("python3"), {QStringLiteral(CASDAP_FAKE_LOGIN1)});
    if (!m_login1.waitForStarted(busTimeoutMs))
        return false;
    QDBusConnectionInterface *iface = QDBusConnection::sessionBus().interface();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < busTimeoutMs) {
        if (iface->isServiceRegistered(SleepInhibitor::service()))
            return true;
        if (m_login1.state() != QProcess::Running)
            return false;
        QTest::qWait(50);
    }
    return false;
}

QDBusMessage SscTest::login1(const QString &method, const QList<QVariant> &arguments)
{
    QDBusMessage call = QDBusMessage::createMethodCall(SleepInhibitor::service(),
                                                       QStringLiteral("/org/casdap/FakeLogin1"),
                                                       QStringLiteral("org.casdap.FakeLogin1"), method);
    call.setArguments(arguments);
    return QDBusConnection::sessionBus().call(call, QDBus::Block, busTimeoutMs);
}

void SscTest::make(bool open)
{
    m_sent.clear();
    m_link = new FakeLink(&m_sent, open);
    m_rotation = std::make_unique<SscRotation>(m_link, m_sleep.get());
}

void SscTest::makeSleeping()
{
    m_sleep = std::make_unique<SleepInhibitor>(QDBusConnection::sessionBus(), QStringLiteral("release sensor"));
    QVERIFY(m_sleep->subscribed());
}

void SscTest::serve(quint32 port)
{
    m_link->inject(fakeNode, ssc::controlPort, controlPacket(ssc::newServer, ssc::service, serverNode, port));
}

void SscTest::answer(const std::optional<ssc::Suid> &suid, quint32 port)
{
    inject(suidAnswer(suid), port);
}

void SscTest::inject(const QByteArray &bytes, quint32 port)
{
    m_link->inject(serverNode, port, bytes);
}

void SscTest::ready()
{
    make();
    serve();
    answer(orientSuid);
    QVERIFY(m_rotation->supported());
}

void SscTest::claim()
{
    ready();
    m_rotation->setWanted(true);
    QCOMPARE(count(ssc::msgOnChange), 1);
    inject(response(lastTxn(ssc::msgOnChange)));
    QVERIFY(m_rotation->claimed());
}

void SscTest::claimSettled(quint64 value)
{
    claim();
    inject(orient(value));
    QTRY_COMPARE_WITH_TIMEOUT(m_rotation->turn(), SscRotation::turnOf(int(value), 1), busTimeoutMs);
}

QList<Sent> SscTest::sentOf(quint32 msgId) const
{
    QList<Sent> out;
    for (const Datagram &datagram : m_sent) {
        const std::optional<Sent> sent = decode(datagram);
        if (sent && sent->msgId == msgId)
            out.append(*sent);
    }
    return out;
}

int SscTest::count(quint32 msgId) const
{
    return int(sentOf(msgId).size());
}

quint16 SscTest::lastTxn(quint32 msgId) const
{
    const QList<Sent> sent = sentOf(msgId);
    return sent.isEmpty() ? 0 : sent.constLast().txn;
}

void SscTest::initTestCase()
{
    qputenv("CASDAP_SSC_RETRY_MS", QByteArray::number(retryMs));
    qputenv("CASDAP_SENSOR_RECLAIM_MS", QByteArray::number(holdMs));
    qunsetenv("CASDAP_SENSOR_TURN");
    m_login1Running = startLogin1();
    if (m_login1Running)
        qputenv("CASDAP_LOGIN1_BUS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
}

void SscTest::cleanup()
{
    m_rotation.reset();
    m_link = nullptr;
    m_sleep.reset();
    m_sent.clear();
}

void SscTest::cleanupTestCase()
{
    if (m_login1.state() == QProcess::Running) {
        login1(QStringLiteral("Quit"));
        if (!m_login1.waitForFinished(busTimeoutMs)) {
            m_login1.terminate();
            m_login1.waitForFinished(busTimeoutMs);
        }
    }
}

void SscTest::wireRoundTrip()
{
    QByteArray buf;
    ssc::putFixed64(buf, 1, 0xABABABABABABABABull);
    ssc::putFixed32(buf, 2, 513);
    ssc::putUint(buf, 3, 300);
    ssc::putBytes(buf, 4, QByteArrayLiteral("accel"));
    const QList<ssc::Field> fields = ssc::fields(buf);
    QCOMPARE(fields.size(), 4);
    QCOMPARE(fields.at(0).id, 1u);
    QVERIFY(fields.at(0).type == ssc::WireType::Fixed64);
    QCOMPARE(fields.at(0).value, 0xABABABABABABABABull);
    QCOMPARE(fields.at(1).id, 2u);
    QVERIFY(fields.at(1).type == ssc::WireType::Fixed32);
    QCOMPARE(fields.at(1).value, 513ull);
    QCOMPARE(fields.at(2).id, 3u);
    QVERIFY(fields.at(2).type == ssc::WireType::Varint);
    QCOMPARE(fields.at(2).value, 300ull);
    QCOMPARE(fields.at(3).id, 4u);
    QVERIFY(fields.at(3).type == ssc::WireType::Bytes);
    QCOMPARE(fields.at(3).bytes.toByteArray(), QByteArrayLiteral("accel"));

    QByteArray varint;
    ssc::putVarint(varint, 300);
    QCOMPARE(varint, bytesOf({0xac, 0x02}));
    QCOMPARE(ssc::uintField(buf, 3).value_or(0), 300ull);
    QVERIFY(!ssc::uintField(buf, 1));
}

void SscTest::wireTruncated()
{
    QCOMPARE(ssc::fields(bytesOf({0x0a, 0x05, 0x01})).size(), 0);
    QCOMPARE(ssc::fields(bytesOf({0x09, 0x01})).size(), 0);
    QCOMPARE(ssc::fields(bytesOf({0x08, 0x01, 0x09, 0x01})).size(), 1);
    QCOMPARE(ssc::fields(bytesOf({0x08, 0x01, 0x0b, 0x01})).size(), 1);
}

void SscTest::wireControl()
{
    const QByteArray buf = ssc::control(7, bytesOf({0xaa, 0xbb}));
    QCOMPARE(buf, bytesOf({0, 7, 0, 0x20, 0, 11, 0, 0x01, 4, 0, 2, 0, 0xaa, 0xbb, 0x10, 1, 0, 1}));
    const std::optional<ssc::Message> message = ssc::parse(buf);
    QVERIFY(message);
    QCOMPARE(int(message->type), int(ssc::typeRequest));
    QCOMPARE(message->txn, quint16(7));
    QCOMPARE(message->msgId, ssc::msgControl);
    QCOMPARE(message->tlv(0x10).value_or(QByteArrayView()).toByteArray(), bytesOf({1}));
}

void SscTest::wireReport()
{
    const QByteArray buf = bytesOf({4, 5, 0, 0x22, 0, 16, 0, 0x01, 8, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0x02, 2, 0, 0, 0});
    const std::optional<ssc::Message> message = ssc::parse(buf);
    QVERIFY(message);
    QCOMPARE(int(message->type), int(ssc::typeIndication));
    QCOMPARE(message->txn, quint16(5));
    QCOMPARE(message->msgId, ssc::msgReportLarge);
    const std::optional<QByteArrayView> data = message->data();
    QVERIFY(data);
    QCOMPARE(data->size(), 0);

    const QByteArray report = indication(orientSuid, {orientEvent(1), orientEvent(3)});
    const std::optional<ssc::Message> parsed = ssc::parse(report);
    QVERIFY(parsed);
    const std::optional<ssc::Report> events = ssc::parseReport(parsed->data().value_or(QByteArrayView()));
    QVERIFY(events);
    QVERIFY(events->suid == orientSuid);
    QCOMPARE(events->events.size(), 2);
    QCOMPARE(events->events.at(0).msgId, ssc::msgOrientEvent);
    QCOMPARE(ssc::uintField(events->events.at(0).payload, 1).value_or(9), 1ull);
    QCOMPARE(ssc::uintField(events->events.at(1).payload, 1).value_or(9), 3ull);
}

void SscTest::wireShort()
{
    QVERIFY(!ssc::parse(bytesOf({4, 5, 0, 0x22, 0, 16, 0, 0x01, 8, 0})));
    QVERIFY(!ssc::parse(bytesOf({4, 5, 0, 0x22})));
    const QByteArray packet = controlPacket(ssc::newServer, ssc::service, serverNode, serverPort);
    QVERIFY(!ssc::parseControl(packet.first(19)));
    const std::optional<ssc::Control> control = ssc::parseControl(packet);
    QVERIFY(control);
    QCOMPARE(control->cmd, ssc::newServer);
    QCOMPARE(control->service, ssc::service);
    QCOMPARE(control->node, serverNode);
    QCOMPARE(control->port, serverPort);
}

void SscTest::wireRequest()
{
    const std::optional<Sent> suid = decode(Datagram{serverNode, serverPort,
                                                     ssc::control(1, ssc::suidRequest("device_orient"))});
    QVERIFY(suid);
    QCOMPARE(suid->msgId, ssc::msgSuidRequest);
    QVERIFY(suid->suid == ssc::lookupSuid);
    QByteArray payload;
    ssc::putBytes(payload, 1, QByteArrayLiteral("device_orient"));
    ssc::putUint(payload, 2, 0);
    ssc::putUint(payload, 3, 1);
    QByteArray body;
    ssc::putBytes(body, 2, payload);
    QCOMPARE(suid->body, body);

    const std::optional<Sent> enable = decode(Datagram{serverNode, serverPort,
                                                       ssc::control(2, ssc::request(orientSuid, ssc::msgOnChange,
                                                                                    QByteArray()))});
    QVERIFY(enable);
    QVERIFY(enable->suid == orientSuid);
    QCOMPARE(enable->body, bytesOf({0x12, 0x00}));

    const std::optional<Sent> disable = decode(Datagram{serverNode, serverPort,
                                                        ssc::control(3, ssc::request(orientSuid, ssc::msgDisable,
                                                                                     std::nullopt))});
    QVERIFY(disable);
    QCOMPARE(disable->msgId, ssc::msgDisable);
    QVERIFY(disable->body.isEmpty());
}

void SscTest::turnOf()
{
    QCOMPARE(SscRotation::turnOf(0, 1), 0);
    QCOMPARE(SscRotation::turnOf(1, 1), 1);
    QCOMPARE(SscRotation::turnOf(2, 1), 2);
    QCOMPARE(SscRotation::turnOf(3, 1), 3);
    QCOMPARE(SscRotation::turnOf(0, 3), 0);
    QCOMPARE(SscRotation::turnOf(1, 3), 3);
    QCOMPARE(SscRotation::turnOf(2, 3), 2);
    QCOMPARE(SscRotation::turnOf(3, 3), 1);
    QCOMPARE(SscRotation::turnOf(7, 1), -1);
    QCOMPARE(SscRotation::turnOf(-1, 1), -1);
}

void SscTest::lookupOnStart()
{
    make();
    QCOMPARE(m_sent.size(), 1);
    QCOMPARE(m_sent.at(0).node, fakeNode);
    QCOMPARE(m_sent.at(0).port, ssc::controlPort);
    QCOMPARE(m_sent.at(0).bytes, ssc::lookup(ssc::service));
    const std::optional<ssc::Control> control = ssc::parseControl(m_sent.at(0).bytes);
    QVERIFY(control);
    QCOMPARE(control->cmd, ssc::newLookup);
    QCOMPARE(control->service, ssc::service);
    QVERIFY(!m_rotation->supported());
    QCOMPARE(m_rotation->turn(), -1);
    QCOMPARE(m_rotation->raw(), QString());
}

void SscTest::closedLink()
{
    make(false);
    QVERIFY(m_sent.isEmpty());
    m_rotation->setWanted(true);
    QTest::qWait(shortMs);
    QVERIFY(m_sent.isEmpty());
    QVERIFY(!m_rotation->supported());
}

void SscTest::otherServiceIgnored()
{
    make();
    m_link->inject(fakeNode, ssc::controlPort, controlPacket(ssc::newServer, 401, serverNode, otherPort));
    QCOMPARE(m_sent.size(), 1);
    serve();
    QCOMPARE(m_sent.size(), 2);
    QCOMPARE(count(ssc::msgSuidRequest), 1);
    QCOMPARE(m_sent.constLast().node, serverNode);
    QCOMPARE(m_sent.constLast().port, serverPort);
    serve(otherPort);
    QCOMPARE(m_sent.size(), 2);
}

void SscTest::suidSetsSupported()
{
    make();
    serve();
    QSignalSpy spy(m_rotation.get(), &SscRotation::supportedChanged);
    answer(orientSuid);
    QVERIFY(m_rotation->supported());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(count(ssc::msgOnChange), 0);
    m_rotation->setWanted(true);
    const QList<Sent> enables = sentOf(ssc::msgOnChange);
    QCOMPARE(enables.size(), 1);
    QVERIFY(enables.at(0).suid == orientSuid);
    QCOMPARE(m_sent.constLast().port, serverPort);
}

void SscTest::secondSuidIgnored()
{
    ready();
    m_rotation->setWanted(true);
    QCOMPARE(count(ssc::msgOnChange), 1);
    answer(otherSuid);
    QCOMPARE(count(ssc::msgOnChange), 1);
    inject(response(lastTxn(ssc::msgOnChange)));
    QVERIFY(m_rotation->claimed());
    inject(orient(1, otherSuid));
    QCOMPARE(m_rotation->raw(), QString());
    inject(orient(1));
    QCOMPARE(m_rotation->raw(), QStringLiteral("1"));
}

void SscTest::emptySuidRetries()
{
    make();
    serve();
    QCOMPARE(count(ssc::msgSuidRequest), 1);
    QElapsedTimer timer;
    timer.start();
    answer(std::nullopt);
    QVERIFY(!m_rotation->supported());
    QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgSuidRequest), 2, busTimeoutMs);
    QVERIFY(timer.elapsed() >= retryMs * 9 / 10);
    timer.restart();
    answer(std::nullopt);
    QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgSuidRequest), 3, busTimeoutMs);
    QVERIFY(timer.elapsed() >= 2 * retryMs * 9 / 10);
    QVERIFY(!m_rotation->supported());
    answer(orientSuid);
    QVERIFY(m_rotation->supported());
    QTest::qWait(4 * retryMs);
    QCOMPARE(count(ssc::msgSuidRequest), 3);
}

void SscTest::endMarkerIgnored()
{
    ready();
    const int sent = int(m_sent.size());
    QSignalSpy spy(m_rotation.get(), &SscRotation::supportedChanged);
    m_link->inject(fakeNode, ssc::controlPort, controlPacket(ssc::newServer, 0, 0, 0));
    m_link->inject(fakeNode, ssc::controlPort, controlPacket(ssc::delServer, 0, 0, 0));
    QCOMPARE(int(m_sent.size()), sent);
    QCOMPARE(spy.count(), 0);
    QVERIFY(m_rotation->supported());
}

void SscTest::enableFlushes()
{
    ready();
    m_rotation->setWanted(true);
    QCOMPARE(count(ssc::msgOnChange), 1);
    QVERIFY(!m_rotation->claimed());
    QCOMPARE(m_sent.constLast().node, serverNode);
    QCOMPARE(m_sent.constLast().port, serverPort);
    inject(response(lastTxn(ssc::msgOnChange)));
    QVERIFY(m_rotation->claimed());
    QCOMPARE(count(ssc::msgFlush), 1);
    const int sent = int(m_sent.size());
    inject(response(lastTxn(ssc::msgFlush)));
    QCOMPARE(int(m_sent.size()), sent);
    QVERIFY(m_rotation->claimed());
}

void SscTest::staleTxnDoesNotClaim()
{
    ready();
    m_rotation->setWanted(true);
    const quint16 first = lastTxn(ssc::msgOnChange);
    m_rotation->setWanted(false);
    QCOMPARE(count(ssc::msgDisable), 1);
    const quint16 disable = lastTxn(ssc::msgDisable);
    m_rotation->setWanted(true);
    QCOMPARE(count(ssc::msgOnChange), 2);
    const quint16 second = lastTxn(ssc::msgOnChange);
    QVERIFY(second != first);
    inject(response(disable));
    QVERIFY(!m_rotation->claimed());
    inject(response(first));
    QVERIFY(!m_rotation->claimed());
    QCOMPARE(count(ssc::msgFlush), 0);
    inject(response(second));
    QVERIFY(m_rotation->claimed());
    QCOMPARE(count(ssc::msgFlush), 1);
}

void SscTest::lateEnableAfterRelease()
{
    ready();
    m_rotation->setWanted(true);
    const quint16 enable = lastTxn(ssc::msgOnChange);
    m_rotation->setWanted(false);
    QCOMPARE(count(ssc::msgDisable), 1);
    inject(response(enable));
    QVERIFY(!m_rotation->claimed());
    QCOMPARE(count(ssc::msgFlush), 0);
    QCOMPARE(m_rotation->turn(), -1);
}

void SscTest::suidRetryResponseDoesNotClaim()
{
    make();
    serve();
    const quint16 first = lastTxn(ssc::msgSuidRequest);
    answer(std::nullopt);
    QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgSuidRequest), 2, busTimeoutMs);
    const quint16 retry = lastTxn(ssc::msgSuidRequest);
    answer(orientSuid);
    m_rotation->setWanted(true);
    const quint16 enable = lastTxn(ssc::msgOnChange);
    QVERIFY(enable != 0);
    inject(response(retry));
    QVERIFY(!m_rotation->claimed());
    inject(response(first));
    QVERIFY(!m_rotation->claimed());
    QCOMPARE(count(ssc::msgFlush), 0);
    inject(response(enable));
    QVERIFY(m_rotation->claimed());
}

void SscTest::valueBeforeEnableDropped()
{
    ready();
    m_rotation->setWanted(true);
    inject(orient(1));
    QCOMPARE(m_rotation->raw(), QString());
    QTest::qWait(shortMs);
    QCOMPARE(m_rotation->turn(), -1);
    inject(response(lastTxn(ssc::msgOnChange)));
    QVERIFY(m_rotation->claimed());
    QCOMPARE(m_rotation->turn(), -1);
    QCOMPARE(count(ssc::msgFlush), 1);
    QElapsedTimer timer;
    timer.start();
    inject(orient(1));
    QCOMPARE(m_rotation->raw(), QStringLiteral("1"));
    QTest::qWait(shortMs);
    QCOMPARE(m_rotation->turn(), -1);
    QTRY_COMPARE_WITH_TIMEOUT(m_rotation->turn(), 1, busTimeoutMs);
    QVERIFY(timer.elapsed() >= SscRotation::settleMs * 9 / 10);
    QCOMPARE(m_rotation->settledRaw(), QStringLiteral("1"));
}

void SscTest::twoEventsThenUnknown()
{
    claim();
    QStringList raws;
    connect(m_rotation.get(), &SscRotation::rawChanged, this, [&raws, this] { raws.append(m_rotation->raw()); });
    inject(indication(orientSuid, {orientEvent(1), orientEvent(2)}));
    QCOMPARE(raws, QStringList({QStringLiteral("1"), QStringLiteral("2")}));
    QTest::qWait(shortMs);
    inject(orient(7));
    QCOMPARE(raws.size(), 2);
    QCOMPARE(m_rotation->raw(), QStringLiteral("2"));
    QTRY_COMPARE_WITH_TIMEOUT(m_rotation->turn(), 2, busTimeoutMs);
    QCOMPARE(m_rotation->settledRaw(), QStringLiteral("2"));
}

void SscTest::shortValueReverted()
{
    claimSettled(0);
    QSignalSpy turns(m_rotation.get(), &SscRotation::turnChanged);
    QSignalSpy settled(m_rotation.get(), &SscRotation::settledRawChanged);
    inject(orient(1));
    QTest::qWait(shortMs);
    inject(orient(0));
    QTest::qWait(SscRotation::settleMs);
    QCOMPARE(m_rotation->turn(), 0);
    QCOMPARE(m_rotation->settledRaw(), QStringLiteral("0"));
    QCOMPARE(turns.count(), 0);
    QCOMPARE(settled.count(), 0);
}

void SscTest::otherSuidOrPortDropped()
{
    claim();
    inject(orient(1, otherSuid));
    QCOMPARE(m_rotation->raw(), QString());
    inject(orient(1), otherPort);
    QCOMPARE(m_rotation->raw(), QString());
    m_link->inject(serverNode + 1, serverPort, orient(1));
    QCOMPARE(m_rotation->raw(), QString());
    QCOMPARE(m_rotation->turn(), -1);
}

void SscTest::releaseKeepingThenClearing()
{
    claimSettled(1);
    m_rotation->setWanted(false, true);
    QCOMPARE(count(ssc::msgDisable), 1);
    QCOMPARE(m_rotation->turn(), -1);
    QCOMPARE(m_rotation->settledRaw(), QStringLiteral("1"));
    QSignalSpy turns(m_rotation.get(), &SscRotation::turnChanged);
    QSignalSpy settled(m_rotation.get(), &SscRotation::settledRawChanged);
    m_rotation->setWanted(true);
    QCOMPARE(count(ssc::msgOnChange), 2);
    inject(response(lastTxn(ssc::msgOnChange)));
    QVERIFY(m_rotation->claimed());
    QCOMPARE(m_rotation->turn(), 1);
    QCOMPARE(turns.count(), 1);
    inject(orient(1));
    QTest::qWait(SscRotation::settleMs + shortMs / 2);
    QCOMPARE(m_rotation->turn(), 1);
    QCOMPARE(turns.count(), 1);
    QCOMPARE(settled.count(), 0);

    m_rotation->setWanted(false);
    QCOMPARE(count(ssc::msgDisable), 2);
    QVERIFY(sentOf(ssc::msgDisable).at(1).suid == orientSuid);
    QVERIFY(!m_rotation->claimed());
    QCOMPARE(m_rotation->turn(), -1);
    QCOMPARE(m_rotation->settledRaw(), QString());
    QVERIFY(m_rotation->supported());
    inject(orient(2));
    QCOMPARE(m_rotation->raw(), QStringLiteral("1"));
}

void SscTest::delServerHoldsThenLoses()
{
    claimSettled(1);
    QSignalSpy supported(m_rotation.get(), &SscRotation::supportedChanged);
    m_link->inject(fakeNode, ssc::controlPort,
                   controlPacket(ssc::delServer, ssc::service, serverNode, serverPort));
    QVERIFY(m_rotation->supported());
    QVERIFY(!m_rotation->claimed());
    QCOMPARE(m_rotation->turn(), 1);
    inject(orient(2));
    QCOMPARE(m_rotation->raw(), QStringLiteral("1"));
    serve(otherPort);
    QCOMPARE(m_sent.constLast().port, otherPort);
    QCOMPARE(count(ssc::msgSuidRequest), 2);
    answer(std::nullopt, otherPort);
    QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgSuidRequest), 3, busTimeoutMs);
    answer(orientSuid, otherPort);
    QCOMPARE(count(ssc::msgOnChange), 2);
    QCOMPARE(m_sent.constLast().port, otherPort);
    m_link->inject(serverNode, otherPort, response(lastTxn(ssc::msgOnChange)));
    QVERIFY(m_rotation->claimed());
    QCOMPARE(m_rotation->turn(), 1);
    QTest::qWait(holdMs);
    QVERIFY(m_rotation->supported());
    QCOMPARE(m_rotation->turn(), 1);
    QCOMPARE(supported.count(), 0);

    QElapsedTimer timer;
    timer.start();
    m_link->inject(fakeNode, ssc::controlPort,
                   controlPacket(ssc::delServer, ssc::service, serverNode, otherPort));
    QVERIFY(m_rotation->supported());
    QCOMPARE(m_rotation->turn(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(!m_rotation->supported(), busTimeoutMs);
    QVERIFY(timer.elapsed() >= holdMs * 9 / 10);
    QCOMPARE(m_rotation->turn(), -1);
    QCOMPARE(m_rotation->settledRaw(), QString());
}

void SscTest::delServerNotWanted()
{
    ready();
    m_link->inject(fakeNode, ssc::controlPort,
                   controlPacket(ssc::delServer, ssc::service, serverNode, serverPort));
    QVERIFY(!m_rotation->supported());
    QCOMPARE(m_rotation->turn(), -1);
}

void SscTest::enableFailure()
{
    ready();
    m_rotation->setWanted(true);
    QTest::ignoreMessage(QtWarningMsg, "casdap sensor: ssc enable failed 31");
    inject(response(lastTxn(ssc::msgOnChange), 1, 31));
    QVERIFY(!m_rotation->claimed());
    QCOMPARE(count(ssc::msgFlush), 0);
    inject(orient(1));
    QCOMPARE(m_rotation->raw(), QString());
    QCOMPARE(m_rotation->turn(), -1);
}

void SscTest::linkFailureReopens()
{
    claim();
    m_link->fail();
    QVERIFY(m_rotation->supported());
    QVERIFY(!m_rotation->claimed());
    const int sent = int(m_sent.size());
    QTRY_COMPARE_WITH_TIMEOUT(m_link->reopens(), 1, busTimeoutMs);
    QCOMPARE(int(m_sent.size()), sent + 1);
    QCOMPARE(m_sent.constLast().port, ssc::controlPort);
    QCOMPARE(m_sent.constLast().bytes, ssc::lookup(ssc::service));
    serve();
    answer(orientSuid);
    QCOMPARE(count(ssc::msgOnChange), 2);
    inject(response(lastTxn(ssc::msgOnChange)));
    QVERIFY(m_rotation->claimed());
}

void SscTest::failedEnableRetries()
{
    ready();
    m_link->failSends(1);
    QElapsedTimer timer;
    timer.start();
    m_rotation->setWanted(true);
    QCOMPARE(count(ssc::msgOnChange), 0);
    QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgOnChange), 1, busTimeoutMs);
    QVERIFY(timer.elapsed() >= retryMs * 9 / 10);
    inject(response(lastTxn(ssc::msgOnChange)));
    QVERIFY(m_rotation->claimed());
}

void SscTest::destructorWhileEnabling()
{
    ready();
    m_rotation->setWanted(true);
    QCOMPARE(count(ssc::msgDisable), 0);
    m_rotation.reset();
    QCOMPARE(count(ssc::msgDisable), 1);
    QVERIFY(decode(m_sent.constLast())->msgId == ssc::msgDisable);
}

void SscTest::destructorWhileClaimed()
{
    claim();
    m_rotation.reset();
    QCOMPARE(count(ssc::msgDisable), 1);
    QVERIFY(decode(m_sent.constLast())->msgId == ssc::msgDisable);
}

void SscTest::sleepReleases()
{
    if (!m_login1Running)
        QSKIP("fake login1 did not start (dbus-run-session, python3 with dbus and gi needed)");
    makeSleeping();
    claim();
    QVERIFY(m_sleep->held());
    QCOMPARE(m_sleep->takeCount(), 1);
    login1(QStringLiteral("EmitPrepareForSleep"), {true});
    QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgDisable), 1, busTimeoutMs);
    QVERIFY(!m_rotation->claimed());
    QVERIFY(m_rotation->wanted());
    QCOMPARE(m_rotation->turn(), -1);
    QVERIFY(m_sleep->held());
    inject(response(lastTxn(ssc::msgDisable)));
    QVERIFY(!m_sleep->held());
    login1(QStringLiteral("EmitPrepareForSleep"), {false});
    QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgOnChange), 2, busTimeoutMs);
    QVERIFY(m_sleep->held());
    QCOMPARE(m_sleep->takeCount(), 2);
    inject(response(lastTxn(ssc::msgOnChange)));
    QVERIFY(m_rotation->claimed());
}

void SscTest::sleepReleaseTimeout()
{
    if (!m_login1Running)
        QSKIP("fake login1 did not start (dbus-run-session, python3 with dbus and gi needed)");
    makeSleeping();
    claim();
    login1(QStringLiteral("EmitPrepareForSleep"), {true});
    QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgDisable), 1, busTimeoutMs);
    QElapsedTimer timer;
    timer.start();
    QVERIFY(m_sleep->held());
    QTRY_VERIFY_WITH_TIMEOUT(!m_sleep->held(), busTimeoutMs);
    QVERIFY(timer.elapsed() >= SscRotation::sleepReleaseMs * 8 / 10);
}

void SscTest::sleepIdleReleasesAtOnce()
{
    if (!m_login1Running)
        QSKIP("fake login1 did not start (dbus-run-session, python3 with dbus and gi needed)");
    makeSleeping();
    ready();
    QVERIFY(m_sleep->held());
    login1(QStringLiteral("EmitPrepareForSleep"), {true});
    QTRY_VERIFY_WITH_TIMEOUT(!m_sleep->held(), SscRotation::sleepReleaseMs / 2);
    QCOMPARE(count(ssc::msgDisable), 0);
}

void SscTest::lateEnableAcrossSleep()
{
    if (!m_login1Running)
        QSKIP("fake login1 did not start (dbus-run-session, python3 with dbus and gi needed)");
    makeSleeping();
    ready();
    m_rotation->setWanted(true);
    const quint16 enable = lastTxn(ssc::msgOnChange);
    login1(QStringLiteral("EmitPrepareForSleep"), {true});
    QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgDisable), 1, busTimeoutMs);
    inject(response(enable));
    QVERIFY(!m_rotation->claimed());
    QCOMPARE(count(ssc::msgFlush), 0);
}

void SscTest::suidDuringSleep()
{
    if (!m_login1Running)
        QSKIP("fake login1 did not start (dbus-run-session, python3 with dbus and gi needed)");
    makeSleeping();
    make();
    serve();
    m_rotation->setWanted(true);
    login1(QStringLiteral("EmitPrepareForSleep"), {true});
    QTRY_VERIFY_WITH_TIMEOUT(!m_sleep->held(), busTimeoutMs);
    answer(orientSuid);
    QVERIFY(m_rotation->supported());
    QCOMPARE(count(ssc::msgOnChange), 0);
    login1(QStringLiteral("EmitPrepareForSleep"), {false});
    QTRY_COMPARE_WITH_TIMEOUT(count(ssc::msgOnChange), 1, busTimeoutMs);
}

QTEST_GUILESS_MAIN(SscTest)
#include "tst_ssc.moc"
