#include "FakeBluezModel.h"
#include "LinkProfile.h"
#include "LinkServer.h"
#include "Player.h"
#include "Queue.h"
#include "Settings.h"
#include "VolumeKeys.h"

#include "LinkProto.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusUnixFileDescriptor>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>

#include <limits>
#include <memory>
#include <utility>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

using namespace microdap;
namespace proto = fd::proto;

namespace {

constexpr int waitMs = 5000;
const QString phoneA = QStringLiteral("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF");
const QString phoneB = QStringLiteral("/org/bluez/hci0/dev_11_22_33_44_55_66");

class FakeVolume : public SinkVolume
{
public:
    void step(int percent) override { steps.push_back(percent); }
    void set(int percent) override { sets.push_back(percent); }

    QList<int> steps;
    QList<int> sets;
};

struct Phone
{
    int app = -1;
    int dev = -1;
    uint16_t seq = 0;
    fd::LinkReader reader;

    Phone()
    {
        int sv[2];
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) == 0) {
            dev = sv[0];
            app = sv[1];
        }
    }
    ~Phone()
    {
        if (app >= 0)
            ::close(app);
    }
    void send(uint8_t type, const fd::LinkWriter &w, uint8_t flags = proto::flagWant)
    {
        const std::vector<uint8_t> bytes = fd::link_frame(type, flags, ++seq, w.data());
        size_t at = 0;
        while (at < bytes.size()) {
            const ssize_t r = ::send(app, bytes.data() + at, bytes.size() - at, MSG_NOSIGNAL);
            if (r <= 0)
                return;
            at += size_t(r);
        }
    }
    void hello(uint16_t major = uint16_t(proto::major))
    {
        fd::LinkWriter w;
        w.u32(proto::tag::HELLO::magic, uint32_t(proto::magic))
            .u16(proto::tag::HELLO::major, major)
            .u16(proto::tag::HELLO::minor, uint16_t(proto::minor))
            .str(proto::tag::HELLO::appVersion, "test");
        send(proto::msg::HELLO, w);
    }
    std::vector<fd::LinkFrame> frames()
    {
        uint8_t buf[4096];
        ssize_t r;
        while ((r = ::recv(app, buf, sizeof buf, MSG_DONTWAIT)) > 0)
            reader.feed(buf, size_t(r));
        std::vector<fd::LinkFrame> out;
        fd::LinkFrame f;
        while (reader.next(f) == 1)
            out.push_back(f);
        return out;
    }
    bool eof()
    {
        uint8_t b;
        return ::recv(app, &b, 1, MSG_DONTWAIT) == 0;
    }
};

fd::LinkBody body(const fd::LinkFrame &f)
{
    fd::LinkBody b;
    fd::link_decode_frame(f, b);
    return b;
}

const fd::LinkFrame *find(const std::vector<fd::LinkFrame> &frames, uint8_t type, bool reply = false)
{
    for (const fd::LinkFrame &f : frames) {
        if (f.type == type && bool(f.flags & proto::flagReply) == reply)
            return &f;
    }
    return nullptr;
}

int count(const std::vector<fd::LinkFrame> &frames, uint8_t type)
{
    int n = 0;
    for (const fd::LinkFrame &f : frames)
        n += f.type == type && !(f.flags & proto::flagReply) ? 1 : 0;
    return n;
}

uint64_t byeReason(const std::vector<fd::LinkFrame> &frames)
{
    const fd::LinkFrame *f = find(frames, proto::msg::BYE);
    return f ? body(*f).u("reason", 255) : 255;
}

struct Rig
{
    Settings settings{nullptr};
    Player player;
    FakeBluezModel bluez;
    FakeVolume *volume = new FakeVolume;
    LinkServer server{&player, &settings};
    qint64 t = 1000;

    Rig()
    {
        settings.setCompanion(true);
        server.setBluez(&bluez);
        server.setVolume(volume);
        server.setClock([this] { return LinkTime{t, t + 5000000}; });
    }
    ~Rig() { delete volume; }

    std::vector<fd::LinkFrame> connect(Phone &phone, const QString &device = phoneA)
    {
        server.open(phone.dev, device);
        phone.dev = -1;
        phone.hello();
        server.poll();
        return phone.frames();
    }
};

class SilentBluez : public FakeBluezModel
{
public:
    void startCompanionPairing(int) override { asked = true; }

    bool asked = false;
};

class ProfileManagerAdaptor;

class FakeDevice : public QObject
{
    Q_OBJECT

public:
    explicit FakeDevice(bool paired)
        : paired(paired)
    {
    }

    bool paired;
};

class Device1Adaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Device1")
    Q_PROPERTY(bool Paired READ paired)
    Q_PROPERTY(bool Bonded READ paired)

public:
    explicit Device1Adaptor(FakeDevice *device)
        : QDBusAbstractAdaptor(device)
        , m_device(device)
    {
    }

    bool paired() const { return m_device->paired; }

private:
    FakeDevice *m_device;
};

class FakeBluez : public QObject
{
    Q_OBJECT

public:
    FakeBluez();
    ~FakeBluez() override;

    bool start();
    void stop();

    QDBusConnection bus;
    int registers = 0;
    int unregisters = 0;
    int failNext = 0;
    QString failName = QStringLiteral("org.bluez.Error.NotPermitted");
    bool holdNext = false;
    QDBusMessage held;
    QString uuid;
    QVariantMap options;
};

class ProfileManagerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.ProfileManager1")

public:
    explicit ProfileManagerAdaptor(FakeBluez *fake)
        : QDBusAbstractAdaptor(fake)
        , m_fake(fake)
    {
    }

public slots:
    void RegisterProfile(const QDBusObjectPath &, const QString &uuid, const QVariantMap &options,
                         const QDBusMessage &message)
    {
        ++m_fake->registers;
        m_fake->uuid = uuid;
        m_fake->options = options;
        if (m_fake->holdNext) {
            m_fake->holdNext = false;
            message.setDelayedReply(true);
            m_fake->held = message;
            return;
        }
        if (m_fake->failNext > 0) {
            --m_fake->failNext;
            message.setDelayedReply(true);
            m_fake->bus.send(message.createErrorReply(m_fake->failName, QStringLiteral("UUID already registered")));
        }
    }
    void UnregisterProfile(const QDBusObjectPath &) { ++m_fake->unregisters; }

private:
    FakeBluez *m_fake;
};

FakeBluez::FakeBluez()
    : bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("fake-bluez")))
{
    new ProfileManagerAdaptor(this);
    bus.registerObject(QStringLiteral("/org/bluez"), this, QDBusConnection::ExportAdaptors);
    for (auto [path, paired] : {std::pair{phoneA, true}, std::pair{phoneB, false}}) {
        auto *device = new FakeDevice(paired);
        device->setParent(this);
        new Device1Adaptor(device);
        bus.registerObject(path, device, QDBusConnection::ExportAdaptors | QDBusConnection::ExportAllProperties);
    }
}

FakeBluez::~FakeBluez()
{
    stop();
    bus.unregisterObject(QStringLiteral("/org/bluez"));
    bus.unregisterObject(phoneA);
    bus.unregisterObject(phoneB);
    QDBusConnection::disconnectFromBus(QStringLiteral("fake-bluez"));
}

bool FakeBluez::start()
{
    return bus.registerService(LinkProfile::bluezService());
}

void FakeBluez::stop()
{
    bus.unregisterService(LinkProfile::bluezService());
}

QDBusPendingCallWatcher *profileCall(FakeBluez &fake, const QString &method, const QVariantList &args)
{
    QDBusMessage call = QDBusMessage::createMethodCall(QDBusConnection::sessionBus().baseService(), LinkProfile::path(),
                                                       QStringLiteral("org.bluez.Profile1"), method);
    call.setArguments(args);
    return new QDBusPendingCallWatcher(fake.bus.asyncCall(call));
}

}

class LinkTest : public QObject
{
    Q_OBJECT

private slots:
    void helloReply();
    void versionBye();
    void offClosesTheSocket();
    void stateOnChangeAndHeartbeat();
    void stateClockStamps();
    void boundPhoneRefused();
    void pairWindowRebinds();
    void pairingCommitBinds();
    void pairWindowRebindsAtHello();
    void pairWindowDeadline();
    void pairWindowRefusesUnknown();
    void pairWindowRefusesRemoving();
    void pairWindowCodeOutlivesDeadline();
    void volumeAboveCapLandsAtCap();
    void stateVolumeSeeded();
    void transportOnThePhone();
    void companionOffSendsBye();
    void endSendsShutdown();
    void queueRevCounts();
    void address();
    void profileRegisters();
    void profileBackoffOnNotPermitted();
    void profileAlreadyExistsIsOurs();
    void profileLateSuccessWhileOff();
    void profileAlreadyExistsWhileOff();
    void profileRefusesUnpaired();
    void profileReregistersOnOwnerChange();
    void profileReregistersOnRelease();
    void profileNewConnection();
    void profileRefuseWindow();
    void profileOffUnregisters();
};

void LinkTest::helloReply()
{
    Rig rig;
    Phone phone;
    const std::vector<fd::LinkFrame> frames = rig.connect(phone);
    const fd::LinkFrame *reply = find(frames, proto::msg::HELLO, true);
    QVERIFY(reply);
    const fd::LinkBody b = body(*reply);
    QCOMPARE(QString::fromStdString(b.str("board")), QStringLiteral("pm2"));
    QCOMPARE(b.u("screenW"), 640u);
    QCOMPARE(b.u("screenH"), 960u);
    QCOMPARE(b.u("backdropW"), 960u);
    QCOMPARE(b.u("backdropH"), 960u);
    QCOMPARE(b.u("features"), uint64_t(1) << proto::feature::stateClock);
    QCOMPARE(b.u("minor"), uint64_t(proto::minor));
    QVERIFY(find(frames, proto::msg::STATE));
    QVERIFY(rig.server.connected());
    QCOMPARE(rig.settings.companionPhone(), QStringLiteral("AA:BB:CC:DD:EE:FF"));
}

void LinkTest::versionBye()
{
    Rig rig;
    Phone phone;
    rig.server.open(phone.dev, phoneA);
    phone.dev = -1;
    phone.hello(2);
    rig.server.poll();
    const std::vector<fd::LinkFrame> frames = phone.frames();
    QVERIFY(!find(frames, proto::msg::HELLO, true));
    const fd::LinkFrame *bye = find(frames, proto::msg::BYE);
    QVERIFY(bye);
    QCOMPARE(body(*bye).u("reason"), uint64_t(proto::val::BYE_REASON::version));
    QCOMPARE(body(*bye).u("major"), uint64_t(proto::major));
    QVERIFY(!rig.server.connected());
}

void LinkTest::offClosesTheSocket()
{
    Rig rig;
    rig.settings.setCompanion(false);
    Phone phone;
    rig.server.open(phone.dev, phoneA);
    phone.dev = -1;
    QVERIFY(phone.eof());
    QCOMPARE(rig.server.sessions(), 0);
}

void LinkTest::stateOnChangeAndHeartbeat()
{
    Rig rig;
    rig.bluez.setTitle(QStringLiteral("first"));
    rig.bluez.setStatus(QStringLiteral("playing"));
    rig.bluez.setTransportActive(true);
    Phone phone;
    std::vector<fd::LinkFrame> frames = rig.connect(phone);
    const fd::LinkFrame *state = find(frames, proto::msg::STATE);
    QVERIFY(state);
    fd::LinkBody b = body(*state);
    QCOMPARE(b.u("source"), uint64_t(proto::val::SOURCE::phone));
    QCOMPARE(b.u("output"), uint64_t(proto::val::OUTPUT::phoneReceiver));
    QCOMPARE(QString::fromStdString(b.str("title")), QStringLiteral("first"));
    QCOMPARE(b.u("playing"), 1u);

    rig.t += proto::stateIntervalMs - 1;
    rig.server.poll();
    QCOMPARE(count(phone.frames(), proto::msg::STATE), 0);
    rig.t += 1;
    rig.server.poll();
    QCOMPARE(count(phone.frames(), proto::msg::STATE), 1);

    rig.bluez.setTitle(QStringLiteral("second"));
    rig.server.poll();
    frames = phone.frames();
    state = find(frames, proto::msg::STATE);
    QVERIFY(state);
    QCOMPARE(QString::fromStdString(body(*state).str("title")), QStringLiteral("second"));

    rig.bluez.setStatus(QStringLiteral("paused"));
    rig.server.poll();
    frames = phone.frames();
    state = find(frames, proto::msg::STATE);
    QVERIFY(state);
    QCOMPARE(body(*state).u("playing"), 0u);
    rig.t += 2 * proto::stateIntervalMs;
    rig.server.poll();
    QCOMPARE(count(phone.frames(), proto::msg::STATE), 0);
}

void LinkTest::stateClockStamps()
{
    Rig rig;
    Phone phone;
    const std::vector<fd::LinkFrame> frames = rig.connect(phone);
    const fd::LinkFrame *state = find(frames, proto::msg::STATE);
    QVERIFY(state);
    const fd::LinkBody b = body(*state);
    QCOMPARE(b.u("sampleMs"), uint64_t(rig.t + 5000000));
    QCOMPARE(b.u("sentMs"), uint64_t(rig.t + 5000000));
    QVERIFY(b.has("positionHeld"));
    QVERIFY(b.fields.back().def && std::string_view(b.fields.back().def->field) == "sentMs");
    QCOMPARE(b.u("maxVolume"), 100u);
    QCOMPARE(b.u("volumeStepPct"), 5u);
    QCOMPARE(b.u("queueRev"), uint64_t(rig.player.queue()->rev()));
    QCOMPARE(b.u("snapRev"), uint64_t(rig.server.snapRev()));
}

void LinkTest::boundPhoneRefused()
{
    Rig rig;
    Phone a;
    rig.connect(a);
    Phone b;
    rig.server.open(b.dev, phoneB);
    b.dev = -1;
    rig.server.poll();
    QCOMPARE(byeReason(b.frames()), uint64_t(proto::val::BYE_REASON::refused));
    QCOMPARE(byeReason(a.frames()), uint64_t(255));
    QCOMPARE(rig.settings.companionPhone(), QStringLiteral("AA:BB:CC:DD:EE:FF"));
    QVERIFY(rig.server.connected());
}

void LinkTest::pairWindowRebinds()
{
    Rig rig;
    Phone a;
    rig.connect(a);
    rig.server.pairPhoneApp();
    QVERIFY(rig.bluez.commands().contains(QStringLiteral("startCompanionPairing")));
    QVERIFY(rig.server.pairWindow());
    rig.bluez.setCompanionSets({phoneA, phoneB}, {});
    Phone b;
    const std::vector<fd::LinkFrame> frames = rig.connect(b, phoneB);
    QVERIFY(find(frames, proto::msg::HELLO, true));
    QCOMPARE(byeReason(frames), uint64_t(255));
    rig.server.poll();
    QCOMPARE(byeReason(a.frames()), uint64_t(proto::val::BYE_REASON::replaced));
    QCOMPARE(rig.settings.companionPhone(), QStringLiteral("11:22:33:44:55:66"));
    QVERIFY(!rig.server.pairWindow());
    QVERIFY(rig.bluez.commands().contains(QStringLiteral("stopPairing")));
    Phone c;
    QCOMPARE(byeReason(rig.connect(c, QStringLiteral("/org/bluez/hci0/dev_99_88_77_66_55_44"))),
             uint64_t(proto::val::BYE_REASON::refused));
    QCOMPARE(rig.settings.companionPhone(), QStringLiteral("11:22:33:44:55:66"));
}

void LinkTest::pairWindowRebindsAtHello()
{
    Rig rig;
    Phone a;
    rig.connect(a);
    rig.server.pairPhoneApp();
    rig.bluez.setCompanionSets({phoneA, phoneB}, {});
    Phone b;
    rig.server.open(b.dev, phoneB);
    b.dev = -1;
    rig.server.poll();
    QCOMPARE(byeReason(a.frames()), uint64_t(255));
    QCOMPARE(rig.settings.companionPhone(), QStringLiteral("AA:BB:CC:DD:EE:FF"));
    QVERIFY(rig.server.pairWindow());
}

void LinkTest::pairWindowDeadline()
{
    Rig rig;
    SilentBluez silent;
    rig.server.setBluez(&silent);
    rig.server.setPairWindowMs(200);
    rig.server.pairPhoneApp();
    QVERIFY(silent.asked);
    QVERIFY(!silent.pairing());
    QVERIFY(rig.server.pairWindow());
    QTRY_VERIFY_WITH_TIMEOUT(!rig.server.pairWindow(), 2000);
    Phone a;
    rig.connect(a);
    Phone b;
    QCOMPARE(byeReason(rig.connect(b, phoneB)), uint64_t(proto::val::BYE_REASON::refused));
}

void LinkTest::pairWindowRefusesUnknown()
{
    Rig rig;
    rig.server.pairPhoneApp();
    rig.bluez.setCompanionSets({phoneA}, {});
    Phone b;
    QCOMPARE(byeReason(rig.connect(b, phoneB)), uint64_t(proto::val::BYE_REASON::refused));
    QVERIFY(rig.settings.companionPhone().isEmpty());
    QVERIFY(rig.server.pairWindow());
    Phone a;
    QVERIFY(find(rig.connect(a, phoneA), proto::msg::HELLO, true));
    QCOMPARE(rig.settings.companionPhone(), QStringLiteral("AA:BB:CC:DD:EE:FF"));
    QVERIFY(rig.server.pairWindow());

    Phone c;
    rig.server.open(c.dev, phoneB);
    c.dev = -1;
    rig.server.poll();
    QCOMPARE(byeReason(c.frames()), uint64_t(proto::val::BYE_REASON::refused));
    QCOMPARE(byeReason(a.frames()), uint64_t(255));
    QCOMPARE(rig.settings.companionPhone(), QStringLiteral("AA:BB:CC:DD:EE:FF"));
}

void LinkTest::pairWindowRefusesRemoving()
{
    Rig rig;
    Phone a;
    rig.connect(a);
    rig.server.pairPhoneApp();
    rig.bluez.setCompanionSets({phoneA, phoneB}, {});
    Phone b;
    rig.server.open(b.dev, phoneB);
    b.dev = -1;
    rig.server.poll();
    rig.bluez.setCompanionSets({phoneA, phoneB}, {phoneB});
    b.hello();
    rig.server.poll();
    QCOMPARE(byeReason(b.frames()), uint64_t(proto::val::BYE_REASON::refused));
    QCOMPARE(byeReason(a.frames()), uint64_t(255));
    QCOMPARE(rig.settings.companionPhone(), QStringLiteral("AA:BB:CC:DD:EE:FF"));
    rig.bluez.stopPairing();
    Phone c;
    QCOMPARE(byeReason(rig.connect(c, phoneB)), uint64_t(proto::val::BYE_REASON::refused));
}

void LinkTest::pairWindowCodeOutlivesDeadline()
{
    Rig rig;
    Phone a;
    rig.connect(a);
    rig.server.setPairWindowMs(200);
    rig.server.pairPhoneApp();
    rig.bluez.setCompanionSets({phoneA}, {});
    rig.bluez.setConfirm(true, 123456, QStringLiteral("Pixel 8"));
    QTest::qWait(400);
    QVERIFY(rig.server.pairWindow());
    QVERIFY(!rig.bluez.commands().contains(QStringLiteral("stopPairing")));
    QVERIFY(rig.bluez.confirmPending());
    QSignalSpy bound(&rig.server, &LinkServer::phoneBound);
    rig.bluez.answerConfirm(123456, true);
    rig.bluez.bindCompanion(phoneB);
    QCOMPARE(bound.count(), 1);
    QCOMPARE(rig.settings.companionPhone(), QStringLiteral("11:22:33:44:55:66"));
    QVERIFY(!rig.server.pairWindow());
}

void LinkTest::pairingCommitBinds()
{
    Rig rig;
    Phone a;
    rig.connect(a);
    rig.bluez.startPairing();
    QVERIFY(!rig.server.pairWindow());
    rig.server.pairPhoneApp();
    QVERIFY(rig.server.pairWindow());
    QSignalSpy bound(&rig.server, &LinkServer::phoneBound);
    rig.bluez.setConfirm(true, 123456, QStringLiteral("Pixel 8"));
    rig.bluez.answerConfirm(123456, true);
    rig.bluez.bindCompanion(phoneB);
    QCOMPARE(bound.count(), 1);
    QCOMPARE(bound.at(0).at(0).toString(), QStringLiteral("Pixel 8"));
    QVERIFY(!rig.server.pairWindow());
    QCOMPARE(rig.settings.companionPhone(), QStringLiteral("11:22:33:44:55:66"));
    QCOMPARE(byeReason(a.frames()), uint64_t(proto::val::BYE_REASON::replaced));
    Phone b;
    QVERIFY(find(rig.connect(b, phoneB), proto::msg::HELLO, true));
    QVERIFY(rig.server.connected());
}

void LinkTest::volumeAboveCapLandsAtCap()
{
    Rig rig;
    rig.settings.setMaxVolume(60);
    Phone phone;
    rig.connect(phone);
    fd::LinkWriter set;
    set.u16(proto::tag::VOLUME::set, 90);
    phone.send(proto::msg::VOLUME, set);
    fd::LinkWriter percent;
    percent.u8(proto::tag::VOLUME::percent, 100);
    phone.send(proto::msg::VOLUME, percent);
    fd::LinkWriter low;
    low.u16(proto::tag::VOLUME::set, 30);
    phone.send(proto::msg::VOLUME, low);
    fd::LinkWriter step;
    step.i32(proto::tag::VOLUME::step, -1);
    phone.send(proto::msg::VOLUME, step);
    fd::LinkWriter huge;
    huge.i32(proto::tag::VOLUME::step, std::numeric_limits<int32_t>::max());
    phone.send(proto::msg::VOLUME, huge);
    rig.server.poll();
    QCOMPARE(rig.volume->sets, QList<int>({60, 60, 30}));
    QCOMPARE(rig.volume->steps, QList<int>({-5, 500}));
    int replies = 0;
    for (const fd::LinkFrame &f : phone.frames())
        replies += f.type == proto::msg::VOLUME && f.flags == proto::flagReply ? 1 : 0;
    QCOMPARE(replies, 5);
}

void LinkTest::stateVolumeSeeded()
{
    Settings settings{nullptr};
    settings.setCompanion(true);
    Player player;
    FakeVolume volume;
    emit volume.levelChanged(42);
    LinkServer server(&player, &settings);
    server.setVolume(&volume);
    Phone phone;
    server.open(phone.dev, phoneA);
    phone.dev = -1;
    phone.hello();
    server.poll();
    const std::vector<fd::LinkFrame> frames = phone.frames();
    const fd::LinkFrame *state = find(frames, proto::msg::STATE);
    QVERIFY(state);
    QCOMPARE(body(*state).u("volumePct"), 42u);
}

void LinkTest::transportOnThePhone()
{
    Rig rig;
    rig.bluez.setStatus(QStringLiteral("playing"));
    rig.bluez.setTransportActive(true);
    Phone phone;
    rig.connect(phone);
    rig.bluez.clearCommands();
    fd::LinkWriter toggle;
    toggle.u8(proto::tag::TRANSPORT::cmd, proto::val::TRANSPORT_CMD::toggle);
    phone.send(proto::msg::TRANSPORT, toggle);
    fd::LinkWriter seek;
    seek.u8(proto::tag::TRANSPORT::cmd, proto::val::TRANSPORT_CMD::seek).u32(proto::tag::TRANSPORT::seekMs, 1000);
    phone.send(proto::msg::TRANSPORT, seek);
    rig.server.poll();
    QCOMPARE(rig.bluez.commands(), QStringList({QStringLiteral("pause")}));
    bool refused = false;
    for (const fd::LinkFrame &f : phone.frames()) {
        if (f.type == proto::msg::TRANSPORT && (f.flags & proto::flagError))
            refused = body(f).u("code") == proto::err::refused;
    }
    QVERIFY(refused);
}

void LinkTest::companionOffSendsBye()
{
    Rig rig;
    Phone phone;
    rig.connect(phone);
    rig.settings.setCompanion(false);
    QCOMPARE(byeReason(phone.frames()), uint64_t(proto::val::BYE_REASON::companionOff));
    QVERIFY(rig.settings.companionPhone().isEmpty());
    QVERIFY(!rig.server.connected());
}

void LinkTest::endSendsShutdown()
{
    Rig rig;
    Phone phone;
    rig.connect(phone);
    rig.server.end(phoneA);
    QCOMPARE(byeReason(phone.frames()), uint64_t(proto::val::BYE_REASON::shutdown));
    QVERIFY(phone.eof());
    QCOMPARE(rig.server.sessions(), 0);
}

void LinkTest::queueRevCounts()
{
    Queue queue;
    QueueEntry e;
    e.id = 1;
    e.path = QStringLiteral("/a.flac");
    quint32 rev = queue.rev();
    queue.set({e}, 0);
    QVERIFY(queue.rev() != rev);
    rev = queue.rev();
    e.id = 2;
    e.path = QStringLiteral("/b.flac");
    queue.enqueue({e});
    QVERIFY(queue.rev() != rev);
    rev = queue.rev();
    queue.setShuffle(true);
    QVERIFY(queue.rev() != rev);
    rev = queue.rev();
    queue.remove(1);
    QVERIFY(queue.rev() != rev);
    rev = queue.rev();
    emit queue.dataChanged(queue.index(0), queue.index(0), {Queue::TitleRole});
    QVERIFY(queue.rev() != rev);
    rev = queue.rev();
    emit queue.dataChanged(queue.index(0), queue.index(0), {Queue::IsCurrentRole});
    QCOMPARE(queue.rev(), rev);
    Queue other;
    Queue third;
    QVERIFY(other.rev() != third.rev() || other.rev() != Queue().rev());
}

void LinkTest::address()
{
    QCOMPARE(LinkServer::address(phoneA), QStringLiteral("AA:BB:CC:DD:EE:FF"));
    QCOMPARE(LinkServer::address(QStringLiteral("AA:BB:CC:DD:EE:FF")), QStringLiteral("AA:BB:CC:DD:EE:FF"));
}

void LinkTest::profileRegisters()
{
    FakeBluez fake;
    QVERIFY(fake.start());
    LinkProfile profile(QDBusConnection::sessionBus());
    profile.setEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(profile.registered(), waitMs);
    QCOMPARE(fake.registers, 1);
    QCOMPARE(fake.uuid, QString::fromLatin1(proto::uuid));
    QCOMPARE(fake.options.value(QStringLiteral("Role")).toString(), QStringLiteral("server"));
    QCOMPARE(fake.options.value(QStringLiteral("RequireAuthentication")).toBool(), true);
    QCOMPARE(fake.options.value(QStringLiteral("RequireAuthorization")).toBool(), false);
    QCOMPARE(fake.options.value(QStringLiteral("AutoConnect")).toBool(), false);
}

void LinkTest::profileAlreadyExistsIsOurs()
{
    FakeBluez fake;
    QVERIFY(fake.start());
    fake.failNext = 1;
    fake.failName = QStringLiteral("org.bluez.Error.AlreadyExists");
    LinkProfile profile(QDBusConnection::sessionBus());
    profile.setEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(profile.registered(), waitMs);
    QCOMPARE(fake.registers, 1);
}

void LinkTest::profileLateSuccessWhileOff()
{
    FakeBluez fake;
    QVERIFY(fake.start());
    fake.holdNext = true;
    LinkProfile profile(QDBusConnection::sessionBus());
    profile.setEnabled(true);
    QTRY_COMPARE_WITH_TIMEOUT(fake.registers, 1, waitMs);
    profile.setEnabled(false);
    QCOMPARE(fake.unregisters, 0);
    QVERIFY(fake.bus.send(fake.held.createReply()));
    QTRY_COMPARE_WITH_TIMEOUT(fake.unregisters, 1, waitMs);
    QVERIFY(!profile.registered());
}

void LinkTest::profileAlreadyExistsWhileOff()
{
    FakeBluez fake;
    QVERIFY(fake.start());
    fake.holdNext = true;
    LinkProfile profile(QDBusConnection::sessionBus());
    profile.setEnabled(true);
    QTRY_COMPARE_WITH_TIMEOUT(fake.registers, 1, waitMs);
    profile.setEnabled(false);
    QVERIFY(fake.bus.send(fake.held.createErrorReply(QStringLiteral("org.bluez.Error.AlreadyExists"), QStringLiteral("held"))));
    QTRY_COMPARE_WITH_TIMEOUT(fake.unregisters, 1, waitMs);
    QVERIFY(!profile.registered());
}

void LinkTest::profileRefusesUnpaired()
{
    FakeBluez fake;
    QVERIFY(fake.start());
    LinkProfile profile(QDBusConnection::sessionBus());
    profile.setEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(profile.registered(), waitMs);
    QSignalSpy spy(&profile, &LinkProfile::connection);
    for (const QString &device : {phoneB, QStringLiteral("/org/bluez/hci0/dev_00_00_00_00_00_01")}) {
        int sv[2];
        QCOMPARE(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv), 0);
        std::unique_ptr<QDBusPendingCallWatcher> call(profileCall(
            fake, QStringLiteral("NewConnection"),
            {QVariant::fromValue(QDBusObjectPath(device)), QVariant::fromValue(QDBusUnixFileDescriptor(sv[0])), QVariantMap()}));
        ::close(sv[0]);
        QTRY_VERIFY_WITH_TIMEOUT(call->isFinished(), waitMs);
        QVERIFY2(call->isError(), qPrintable(device));
        QCOMPARE(call->error().name(), QStringLiteral("org.bluez.Error.Rejected"));
        ::close(sv[1]);
    }
    QCOMPARE(spy.count(), 0);
}

void LinkTest::profileBackoffOnNotPermitted()
{
    FakeBluez fake;
    QVERIFY(fake.start());
    fake.failNext = 1;
    LinkProfile profile(QDBusConnection::sessionBus());
    QElapsedTimer timer;
    timer.start();
    profile.setEnabled(true);
    QTRY_COMPARE_WITH_TIMEOUT(fake.registers, 1, waitMs);
    QTest::qWait(200);
    QVERIFY(!profile.registered());
    QTRY_VERIFY_WITH_TIMEOUT(profile.registered(), waitMs);
    QCOMPARE(fake.registers, 2);
    QVERIFY(timer.elapsed() >= LinkProfile::backoffMs().constFirst() - 50);
}

void LinkTest::profileReregistersOnOwnerChange()
{
    FakeBluez fake;
    QVERIFY(fake.start());
    LinkProfile profile(QDBusConnection::sessionBus());
    profile.setEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(profile.registered(), waitMs);
    fake.stop();
    QTRY_VERIFY_WITH_TIMEOUT(!profile.registered(), waitMs);
    QVERIFY(fake.start());
    QTRY_COMPARE_WITH_TIMEOUT(fake.registers, 2, waitMs);
    QTRY_VERIFY_WITH_TIMEOUT(profile.registered(), waitMs);
}

void LinkTest::profileReregistersOnRelease()
{
    FakeBluez fake;
    QVERIFY(fake.start());
    LinkProfile profile(QDBusConnection::sessionBus());
    profile.setEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(profile.registered(), waitMs);
    std::unique_ptr<QDBusPendingCallWatcher> call(profileCall(fake, QStringLiteral("Release"), {}));
    QTRY_COMPARE_WITH_TIMEOUT(fake.registers, 2, waitMs);
    QTRY_VERIFY_WITH_TIMEOUT(profile.registered(), waitMs);
}

void LinkTest::profileNewConnection()
{
    FakeBluez fake;
    QVERIFY(fake.start());
    LinkProfile profile(QDBusConnection::sessionBus());
    profile.setEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(profile.registered(), waitMs);
    QSignalSpy spy(&profile, &LinkProfile::connection);
    int sv[2];
    QCOMPARE(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv), 0);
    std::unique_ptr<QDBusPendingCallWatcher> call(profileCall(
        fake, QStringLiteral("NewConnection"),
        {QVariant::fromValue(QDBusObjectPath(phoneA)), QVariant::fromValue(QDBusUnixFileDescriptor(sv[0])), QVariantMap()}));
    ::close(sv[0]);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, waitMs);
    QTRY_VERIFY_WITH_TIMEOUT(call->isFinished(), waitMs);
    QVERIFY(!call->isError());
    const int fd = spy.at(0).at(0).toInt();
    QCOMPARE(spy.at(0).at(1).toString(), phoneA);
    QVERIFY(fd >= 0);
    QCOMPARE(::write(fd, "x", 1), ssize_t(1));
    char c = 0;
    QCOMPARE(::read(sv[1], &c, 1), ssize_t(1));
    QCOMPARE(c, 'x');
    ::close(fd);
    ::close(sv[1]);
}

void LinkTest::profileRefuseWindow()
{
    FakeBluez fake;
    QVERIFY(fake.start());
    LinkProfile profile(QDBusConnection::sessionBus());
    profile.setEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(profile.registered(), waitMs);
    QList<int> fds;
    connect(&profile, &LinkProfile::connection, this, [&fds](int fd, const QString &) { fds.push_back(fd); });
    std::vector<std::unique_ptr<QDBusPendingCallWatcher>> calls;
    for (int i = 0; i < int(proto::refuseCount) + 1; ++i) {
        int sv[2];
        QCOMPARE(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv), 0);
        calls.emplace_back(profileCall(
            fake, QStringLiteral("NewConnection"),
            {QVariant::fromValue(QDBusObjectPath(phoneA)), QVariant::fromValue(QDBusUnixFileDescriptor(sv[0])), QVariantMap()}));
        ::close(sv[0]);
        ::close(sv[1]);
        QTRY_VERIFY_WITH_TIMEOUT(calls.back()->isFinished(), waitMs);
    }
    for (int i = 0; i < int(proto::refuseCount); ++i)
        QVERIFY2(!calls[size_t(i)]->isError(), qPrintable(QString::number(i)));
    QVERIFY(calls.back()->isError());
    QCOMPARE(calls.back()->error().name(), QStringLiteral("org.bluez.Error.Rejected"));
    QCOMPARE(fds.size(), int(proto::refuseCount));
    for (int fd : fds)
        ::close(fd);
}

void LinkTest::profileOffUnregisters()
{
    FakeBluez fake;
    QVERIFY(fake.start());
    LinkProfile profile(QDBusConnection::sessionBus());
    profile.setEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(profile.registered(), waitMs);
    profile.setEnabled(false);
    QVERIFY(!profile.registered());
    QTRY_COMPARE_WITH_TIMEOUT(fake.unregisters, 1, waitMs);
    QVERIFY(!profile.accept(phoneA));
}

QTEST_GUILESS_MAIN(LinkTest)
#include "tst_link.moc"
