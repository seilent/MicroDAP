#include "FakeBluezModel.h"
#include "HubModels.h"
#include "Library.h"
#include "LinkProfile.h"
#include "LinkServer.h"
#include "Models.h"
#include "Player.h"
#include "Queue.h"
#include "Settings.h"
#include "SettingsRows.h"
#include "VolumeKeys.h"

#include "LinkProto.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusUnixFileDescriptor>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QSqlQuery>
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

bool copyTree(const QString &from, const QString &to)
{
    QDirIterator it(from, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        const QString target = to + QLatin1Char('/') + QDir(from).relativeFilePath(info.absoluteFilePath());
        if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::copy(info.absoluteFilePath(), target))
            return false;
    }
    return true;
}

struct LibRig
{
    QTemporaryDir dir;
    QString music;
    Library library;
    Settings settings{nullptr};
    Player player{&library};
    ArtistsModel artists{&library};
    AlbumsModel albums{&library};
    SongsModel songs{&library};
    PlaylistsModel playlists{&library};
    FolderModel folders{&library};
    LinkServer server{&player, &settings};
    Phone phone;
    qint64 t = 1000;

    LibRig()
        : music(dir.filePath(QStringLiteral("music")))
        , library(dir.filePath(QStringLiteral("library.db")), dir.filePath(QStringLiteral("art")))
    {
        copyTree(QStringLiteral(MICRODAP_FIXTURE_MUSIC "/Album One"), music + QStringLiteral("/Album One"));
        copyTree(QStringLiteral(MICRODAP_FIXTURE_MUSIC "/Single"), music + QStringLiteral("/Single"));
        settings.setCompanion(true);
        folders.setRoots({music});
        server.setClock([this] { return LinkTime{t, t + 5000000}; });
        server.setSources({&library, &artists, &albums, &songs, &playlists, &folders, nullptr});
    }

    bool scan()
    {
        QSignalSpy spy(&library, &Library::scanned);
        library.rescan({music});
        return spy.wait(60000) && spy.takeFirst().at(3).toBool();
    }

    void connect()
    {
        server.open(phone.dev, phoneA);
        phone.dev = -1;
        phone.hello();
        server.poll();
        phone.frames();
    }

    std::vector<fd::LinkFrame> ask(uint8_t type, const fd::LinkWriter &w)
    {
        phone.send(type, w);
        server.poll();
        return phone.frames();
    }

    qint64 id(const QString &title)
    {
        QSqlQuery query(library.database());
        query.prepare(QStringLiteral("SELECT id FROM tracks WHERE title = ?"));
        query.addBindValue(title);
        return query.exec() && query.next() ? query.value(0).toLongLong() : -1;
    }
};

uint64_t errorCode(const std::vector<fd::LinkFrame> &frames, uint8_t type)
{
    for (const fd::LinkFrame &f : frames) {
        if (f.type == type && (f.flags & proto::flagError))
            return body(f).u("code");
    }
    return 0;
}

bool okReply(const std::vector<fd::LinkFrame> &frames, uint8_t type)
{
    for (const fd::LinkFrame &f : frames) {
        if (f.type == type && f.flags == proto::flagReply)
            return true;
    }
    return false;
}

struct Rows
{
    QStringList names;
    QStringList keys;
    QList<quint64> counts;
    QList<quint64> firsts;
    quint64 total = 0;
    int pages = 0;
    bool more = false;
};

Rows rowsOf(const std::vector<fd::LinkFrame> &frames, uint8_t type, const char *field)
{
    Rows out;
    for (const fd::LinkFrame &f : frames) {
        if (f.type != type || (f.flags & proto::flagReply))
            continue;
        const fd::LinkBody b = body(f);
        ++out.pages;
        out.total = b.u("total");
        out.more = b.u("more") != 0;
        for (const fd::LinkField *r : b.all(field)) {
            for (const fd::LinkField &x : r->rec) {
                const std::string_view name = x.def ? x.def->field : "";
                if (name == "name" || name == "title")
                    out.names.push_back(QString::fromStdString(x.s));
                else if (name == "key")
                    out.keys.push_back(QString::fromStdString(x.s));
                else if (name == "count")
                    out.counts.push_back(x.u);
                else if (name == "first" || name == "track")
                    out.firsts.push_back(x.u);
            }
        }
    }
    return out;
}

fd::LinkWriter libGet(uint8_t view, const std::string &parent = {}, int offset = 0, int count = -1)
{
    fd::LinkWriter w;
    w.u8(proto::tag::LIB_GET::view, view);
    if (!parent.empty())
        w.str(proto::tag::LIB_GET::parent, parent);
    w.u32(proto::tag::LIB_GET::offset, uint32_t(offset));
    if (count >= 0)
        w.u32(proto::tag::LIB_GET::count, uint32_t(count));
    else if (count == -2)
        w.u32(proto::tag::LIB_GET::count, std::numeric_limits<uint32_t>::max());
    return w;
}

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
    void libraryPaging();
    void libraryViews();
    void libraryRevs();
    void pruneBumpsSnapRev();
    void playAndQueue();
    void libraryScale();
    void schemaAfterHello();
    void setKeptAndRefused();
    void draftRefused();
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
    QCOMPARE(b.u("features"), uint64_t(1) << proto::feature::settings | uint64_t(1) << proto::feature::library
                                  | uint64_t(1) << proto::feature::queue | uint64_t(1) << proto::feature::stateClock);
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

void LinkTest::libraryPaging()
{
    LibRig rig;
    QVERIFY(rig.scan());
    rig.connect();
    std::vector<fd::LinkFrame> frames = rig.ask(proto::msg::LIB_GET, libGet(proto::val::VIEW::songs, {}, 0, 2));
    QVERIFY(okReply(frames, proto::msg::LIB_GET));
    Rows rows = rowsOf(frames, proto::msg::LIB, "row");
    QCOMPARE(rows.total, 5u);
    QCOMPARE(rows.names.size(), 2);
    QVERIFY(rows.more);
    QCOMPARE(rows.keys.at(0), QString::number(rows.firsts.at(0)));
    QCOMPARE(rows.counts, QList<quint64>({1, 1}));
    const QStringList head = rows.names;
    frames = rig.ask(proto::msg::LIB_GET, libGet(proto::val::VIEW::songs, {}, 2));
    rows = rowsOf(frames, proto::msg::LIB, "row");
    QCOMPARE(rows.names.size(), 3);
    QVERIFY(!rows.more);
    QStringList all = head + rows.names;
    QCOMPARE(QSet<QString>(all.begin(), all.end()),
             QSet<QString>({QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three"), QStringLiteral("Four"),
                            QStringLiteral("Single")}));
    rows = rowsOf(rig.ask(proto::msg::LIB_GET, libGet(proto::val::VIEW::songs, {}, 1, -2)), proto::msg::LIB, "row");
    QCOMPARE(rows.names.size(), 4);
    QVERIFY(!rows.more);
    frames = rig.ask(proto::msg::LIB_GET, libGet(proto::val::VIEW::songs, {}, 6));
    QCOMPARE(errorCode(frames, proto::msg::LIB_GET), uint64_t(proto::err::badValue));
    frames = rig.ask(proto::msg::LIB_GET, libGet(proto::val::VIEW::songs, "x"));
    QCOMPARE(errorCode(frames, proto::msg::LIB_GET), uint64_t(proto::err::badValue));
}

void LinkTest::libraryViews()
{
    LibRig rig;
    QVERIFY(rig.scan());
    rig.connect();
    Rows albums = rowsOf(rig.ask(proto::msg::LIB_GET, libGet(proto::val::VIEW::albums)), proto::msg::LIB, "row");
    QCOMPARE(albums.names, QStringList({QStringLiteral("Album One"), QStringLiteral("Single")}));
    QCOMPARE(albums.counts, QList<quint64>({4, 1}));
    QCOMPARE(albums.keys.at(0), QString::number(rig.id(QStringLiteral("One"))));
    Rows tracks = rowsOf(rig.ask(proto::msg::LIB_GET, libGet(proto::val::VIEW::albumTracks, albums.keys.at(0).toStdString())),
                         proto::msg::LIB, "row");
    QCOMPARE(tracks.names, QStringList({QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three"),
                                        QStringLiteral("Four")}));
    std::vector<fd::LinkFrame> frames = rig.ask(proto::msg::LIB_GET, libGet(proto::val::VIEW::albumTracks,
                                                                           std::to_string(rig.id(QStringLiteral("Two")))));
    QCOMPARE(errorCode(frames, proto::msg::LIB_GET), uint64_t(proto::err::staleRev));
    Rows artists = rowsOf(rig.ask(proto::msg::LIB_GET, libGet(proto::val::VIEW::artists)), proto::msg::LIB, "row");
    QVERIFY(artists.names.contains(QStringLiteral("Artist A")));
    QCOMPARE(artists.names, artists.keys);
    Rows artistAlbums = rowsOf(rig.ask(proto::msg::LIB_GET, libGet(proto::val::VIEW::artistAlbums, "Artist A")),
                               proto::msg::LIB, "row");
    QCOMPARE(artistAlbums.names, QStringList({QStringLiteral("Album One")}));
    frames = rig.ask(proto::msg::LIB_GET, libGet(proto::val::VIEW::artistAlbums, "Nobody"));
    QCOMPARE(errorCode(frames, proto::msg::LIB_GET), uint64_t(proto::err::staleRev));
    Rows top = rowsOf(rig.ask(proto::msg::LIB_GET, libGet(proto::val::VIEW::folders)), proto::msg::LIB, "row");
    QCOMPARE(top.keys, QStringList({QDir::cleanPath(rig.music)}));
    QCOMPARE(top.counts, QList<quint64>({5}));
    Rows inside = rowsOf(rig.ask(proto::msg::LIB_GET, libGet(proto::val::VIEW::folders, top.keys.at(0).toStdString())),
                         proto::msg::LIB, "row");
    QCOMPARE(inside.names, QStringList({QStringLiteral("Album One"), QStringLiteral("Single")}));
    QCOMPARE(inside.counts, QList<quint64>({4, 1}));
    Rows lists = rowsOf(rig.ask(proto::msg::LIB_GET, libGet(proto::val::VIEW::playlists)), proto::msg::LIB, "row");
    QCOMPARE(lists.total, 0u);
}

void LinkTest::libraryRevs()
{
    LibRig rig;
    QVERIFY(rig.scan());
    rig.connect();
    fd::LinkWriter stale = libGet(proto::val::VIEW::songs);
    stale.u32(proto::tag::LIB_GET::libRev, rig.server.libRev() + 1);
    QCOMPARE(errorCode(rig.ask(proto::msg::LIB_GET, stale), proto::msg::LIB_GET), uint64_t(proto::err::staleRev));
    fd::LinkWriter snap = libGet(proto::val::VIEW::songs);
    snap.u32(proto::tag::LIB_GET::snapRev, rig.server.snapRev());
    QVERIFY(okReply(rig.ask(proto::msg::LIB_GET, snap), proto::msg::LIB_GET));
    fd::LinkWriter listSnap = libGet(proto::val::VIEW::playlists);
    listSnap.u32(proto::tag::LIB_GET::snapRev, rig.server.snapRev());
    QCOMPARE(errorCode(rig.ask(proto::msg::LIB_GET, listSnap), proto::msg::LIB_GET), uint64_t(proto::err::badValue));
    fd::LinkWriter play;
    play.u8(proto::tag::PLAY::view, proto::val::VIEW::songs);
    QCOMPARE(errorCode(rig.ask(proto::msg::PLAY, play), proto::msg::PLAY), uint64_t(proto::err::staleRev));
    const quint32 lib = rig.server.libRev();
    emit rig.library.playlistsChanged();
    QVERIFY(rig.server.libRev() != lib);
    const quint32 before = rig.server.snapRev();
    rig.folders.setRoots({rig.music + QStringLiteral("/Single")});
    QVERIFY(rig.server.snapRev() != before);
}

void LinkTest::libraryScale()
{
    LibRig rig;
    QSqlDatabase db = rig.library.database();
    QSqlQuery q(db);
    QVERIFY(db.transaction());
    constexpr int artists = 300;
    constexpr int albums = 700;
    constexpr int tracks = 8400;
    for (int a = 1; a <= artists; ++a) {
        q.prepare(QStringLiteral("INSERT INTO artists(id, name) VALUES(?, ?)"));
        q.addBindValue(a);
        q.addBindValue(QStringLiteral("Artist %1").arg(a, 4, 10, QLatin1Char('0')));
        QVERIFY(q.exec());
    }
    for (int b = 1; b <= albums; ++b) {
        q.prepare(QStringLiteral("INSERT INTO albums(id, title, artist, year, added) VALUES(?, ?, ?, 2000, 0)"));
        q.addBindValue(b);
        q.addBindValue(QStringLiteral("Album %1").arg(b, 4, 10, QLatin1Char('0')));
        q.addBindValue(QStringLiteral("Artist %1").arg((b - 1) % artists + 1, 4, 10, QLatin1Char('0')));
        QVERIFY(q.exec());
    }
    for (int t = 1; t <= tracks; ++t) {
        const int album = (t - 1) % albums + 1;
        q.prepare(QStringLiteral("INSERT INTO tracks(id, path, mtime, size, title, artist, album_artist, album_id, "
                                 "track_no, disc_no, duration_ms, added) VALUES(?, ?, 0, 0, ?, ?, ?, ?, ?, 1, 200000, 0)"));
        q.addBindValue(t);
        q.addBindValue(QStringLiteral("/scale/%1.flac").arg(t));
        q.addBindValue(QStringLiteral("Song %1").arg(t, 5, 10, QLatin1Char('0')));
        q.addBindValue(QStringLiteral("Artist %1").arg((album - 1) % artists + 1, 4, 10, QLatin1Char('0')));
        q.addBindValue(QStringLiteral("Artist %1").arg((album - 1) % artists + 1, 4, 10, QLatin1Char('0')));
        q.addBindValue(album);
        q.addBindValue((t - 1) / albums + 1);
        QVERIFY(q.exec());
        q.prepare(QStringLiteral("INSERT INTO track_artists(track_id, artist_id, pos) VALUES(?, ?, 0)"));
        q.addBindValue(t);
        q.addBindValue((album - 1) % artists + 1);
        QVERIFY(q.exec());
    }
    QVERIFY(db.commit());
    rig.connect();
    QElapsedTimer timer;
    timer.start();
    Rows albumRows = rowsOf(rig.ask(proto::msg::LIB_GET, libGet(proto::val::VIEW::albums, {}, 0, 100)), proto::msg::LIB, "row");
    const qint64 albumPage = timer.restart();
    Rows fill = rowsOf(rig.ask(proto::msg::LIB_GET, libGet(proto::val::VIEW::albums)), proto::msg::LIB, "row");
    const qint64 albumFill = timer.restart();
    Rows artistRows = rowsOf(rig.ask(proto::msg::LIB_GET, libGet(proto::val::VIEW::artists, {}, 0, 100)), proto::msg::LIB, "row");
    const qint64 artistPage = timer.restart();
    LinkView view({&rig.library, &rig.artists, &rig.albums, &rig.songs, &rig.playlists, &rig.folders, nullptr});
    QCOMPARE(view.open(proto::val::VIEW::albums, {}), uint8_t(0));
    const QVariantList all = view.tracks();
    const qint64 albumTracks = timer.restart();
    const QList<QueueEntry> entries = rig.library.queueEntries(all);
    const qint64 queueEntries = timer.restart();
    QCOMPARE(view.open(proto::val::VIEW::artists, {}), uint8_t(0));
    const qint64 artistTracks = (view.tracks(), timer.restart());
    qInfo().noquote() << "link scale: albums page" << albumPage << "ms," << albumRows.names.size() << "rows; albums fill"
                      << albumFill << "ms," << fill.names.size() << "rows; artists page" << artistPage << "ms;"
                      << "albums PLAY tracks" << albumTracks << "ms, queue entries" << queueEntries << "ms for"
                      << entries.size() << "; artists PLAY tracks" << artistTracks << "ms";
    QCOMPARE(albumRows.names.size(), 100);
    QCOMPARE(artistRows.names.size(), 100);
    QCOMPARE(entries.size(), tracks);
}

void LinkTest::pruneBumpsSnapRev()
{
    LibRig rig;
    QVERIFY(rig.scan());
    rig.connect();
    const quint32 snap = rig.server.snapRev();
    const quint32 lib = rig.server.libRev();
    QVERIFY(QDir(rig.music + QStringLiteral("/Single")).removeRecursively());
    QVERIFY(rig.scan());
    QVERIFY(rig.server.snapRev() != snap);
    QVERIFY(rig.server.libRev() != lib);
    rig.server.poll();
    const fd::LinkFrame *state = nullptr;
    const std::vector<fd::LinkFrame> frames = rig.phone.frames();
    state = find(frames, proto::msg::STATE);
    QVERIFY(state);
    QCOMPARE(body(*state).u("snapRev"), uint64_t(rig.server.snapRev()));
    QCOMPARE(body(*state).u("libRev"), uint64_t(rig.server.libRev()));
}

void LinkTest::playAndQueue()
{
    LibRig rig;
    QVERIFY(rig.scan());
    rig.connect();
    Rows albums = rowsOf(rig.ask(proto::msg::LIB_GET, libGet(proto::val::VIEW::albums)), proto::msg::LIB, "row");
    fd::LinkWriter play;
    play.u8(proto::tag::PLAY::view, proto::val::VIEW::albumTracks)
        .str(proto::tag::PLAY::parent, albums.keys.at(0).toStdString())
        .u32(proto::tag::PLAY::libRev, rig.server.libRev())
        .u32(proto::tag::PLAY::start, 1);
    QVERIFY(okReply(rig.ask(proto::msg::PLAY, play), proto::msg::PLAY));
    Queue *queue = rig.player.queue();
    QCOMPARE(queue->count(), 4);
    QCOMPARE(queue->entry(queue->currentIndex()).title, QStringLiteral("Two"));
    fd::LinkWriter page;
    page.u8(proto::tag::QUEUE_OP::cmd, proto::val::QUEUE_CMD::page);
    Rows items = rowsOf(rig.ask(proto::msg::QUEUE_OP, page), proto::msg::QUEUE, "item");
    QCOMPARE(items.names, QStringList({QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three"),
                                       QStringLiteral("Four")}));
    fd::LinkWriter stale;
    stale.u8(proto::tag::QUEUE_OP::cmd, proto::val::QUEUE_CMD::remove).u32(proto::tag::QUEUE_OP::rev, queue->rev() + 1)
        .u32(proto::tag::QUEUE_OP::slot, 3);
    QCOMPARE(errorCode(rig.ask(proto::msg::QUEUE_OP, stale), proto::msg::QUEUE_OP), uint64_t(proto::err::staleRev));
    fd::LinkWriter remove;
    remove.u8(proto::tag::QUEUE_OP::cmd, proto::val::QUEUE_CMD::remove).u32(proto::tag::QUEUE_OP::rev, queue->rev())
        .u32(proto::tag::QUEUE_OP::slot, 3);
    QVERIFY(okReply(rig.ask(proto::msg::QUEUE_OP, remove), proto::msg::QUEUE_OP));
    QCOMPARE(queue->count(), 3);
    const quint32 rev = queue->rev();
    QCOMPARE(rig.player.enqueueTracks({rig.id(QStringLiteral("Single"))}), 1);
    QVERIFY(queue->rev() != rev);
    fd::LinkWriter tracks;
    tracks.u64(proto::tag::PLAY::track, uint64_t(rig.id(QStringLiteral("Single"))))
        .u64(proto::tag::PLAY::track, uint64_t(rig.id(QStringLiteral("One"))));
    QVERIFY(okReply(rig.ask(proto::msg::PLAY, tracks), proto::msg::PLAY));
    QCOMPARE(queue->count(), 2);
    fd::LinkWriter gone;
    gone.u64(proto::tag::PLAY::track, 999999);
    QCOMPARE(errorCode(rig.ask(proto::msg::PLAY, gone), proto::msg::PLAY), uint64_t(proto::err::staleRev));
    fd::LinkWriter unknownFavorite;
    unknownFavorite.u8(proto::tag::ACTION::cmd, proto::val::ACTION_CMD::favorite).u64(proto::tag::ACTION::track, 999999);
    QCOMPARE(errorCode(rig.ask(proto::msg::ACTION, unknownFavorite), proto::msg::ACTION), uint64_t(proto::err::staleRev));
}

const fd::LinkField *schemaRow(const std::vector<fd::LinkFrame> &frames, const std::string &key)
{
    static fd::LinkBody kept;
    for (const fd::LinkFrame &f : frames) {
        if (f.type != proto::msg::SCHEMA)
            continue;
        kept = body(f);
        for (const fd::LinkField *r : kept.all("row")) {
            for (const fd::LinkField &x : r->rec) {
                if (x.def && std::string_view(x.def->field) == "key" && x.s == key)
                    return r;
            }
        }
    }
    return nullptr;
}

QStringList recordList(const fd::LinkField *row, const char *field)
{
    QStringList out;
    for (const fd::LinkField &x : row->rec) {
        if (x.def && std::string_view(x.def->field) == field)
            out.push_back(x.def->kind == proto::Kind::Str ? QString::fromStdString(x.s)
                          : x.def->kind == proto::Kind::I32 ? QString::number(x.i)
                                                             : QString::number(x.u));
    }
    return out;
}

QString settingsText(const std::vector<fd::LinkFrame> &frames)
{
    const fd::LinkFrame *f = find(frames, proto::msg::SETTINGS);
    return f ? QString::fromStdString(body(*f).str("text")) : QString();
}

void LinkTest::schemaAfterHello()
{
    Rig rig;
    rig.settings.setOutputSlot(QStringLiteral("headphones"));
    Phone phone;
    const std::vector<fd::LinkFrame> frames = rig.connect(phone);
    const fd::LinkFrame *reply = find(frames, proto::msg::HELLO, true);
    QVERIFY(reply);
    const uint64_t settingsRev = body(*reply).u("settingsRev");
    QStringList pivots;
    for (const fd::LinkFrame &f : frames) {
        if (f.type != proto::msg::SCHEMA)
            continue;
        const fd::LinkBody b = body(f);
        QCOMPARE(b.u("pivotCount"), 5u);
        QVERIFY(f.body.size() <= size_t(proto::maxPayload) * proto::pageFillPct / 100);
        const QString pivot = QString::fromStdString(b.str("pivot"));
        if (!pivots.contains(pivot))
            pivots.push_back(pivot);
    }
    QCOMPARE(pivots, SettingsRows::pivots());
    const fd::LinkFrame *settings = find(frames, proto::msg::SETTINGS);
    QVERIFY(settings);
    QCOMPARE(body(*settings).u("rev"), settingsRev);
    const QString text = settingsText(frames);
    QVERIFY(text.contains(QStringLiteral("maxVolume=100\n")));
    QVERIFY(text.contains(QStringLiteral("balance=0\n")));
    QVERIFY(text.contains(QStringLiteral("crossfade=0\n")));
    QVERIFY(text.contains(QStringLiteral("companion=on\n")));
    QVERIFY(!text.contains(QStringLiteral("crossfadeMs")));
    const fd::LinkField *max = schemaRow(frames, "maxVolume");
    QVERIFY(max);
    QCOMPARE(recordList(max, "value"), QStringList({QStringLiteral("50"), QStringLiteral("55"), QStringLiteral("60"),
                                                    QStringLiteral("65"), QStringLiteral("70"), QStringLiteral("75"),
                                                    QStringLiteral("80"), QStringLiteral("85"), QStringLiteral("90"),
                                                    QStringLiteral("95"), QStringLiteral("100")}));
    QCOMPARE(recordList(max, "kind"), QStringList({QString::number(proto::val::ROW_KIND::steps)}));
    const fd::LinkField *balance = schemaRow(frames, "balance");
    QVERIFY(balance);
    QCOMPARE(recordList(balance, "min"), QStringList({QStringLiteral("-20")}));
    QCOMPARE(recordList(balance, "max"), QStringList({QStringLiteral("20")}));
    QCOMPARE(recordList(balance, "step"), QStringList({QStringLiteral("1")}));
    const fd::LinkField *crossfade = schemaRow(frames, "crossfade");
    QVERIFY(crossfade);
    QCOMPARE(recordList(crossfade, "value"), QStringList({QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"),
                                                          QStringLiteral("3"), QStringLiteral("5")}));
    QCOMPARE(recordList(crossfade, "word").constFirst(), QStringLiteral("OFF"));
    const fd::LinkField *crossfeed = schemaRow(frames, "crossfeed");
    QVERIFY(crossfeed && recordList(crossfeed, "value").contains(QStringLiteral("chuMoy"))
            && recordList(crossfeed, "value").contains(QStringLiteral("janMeier")));
    const fd::LinkField *companion = schemaRow(frames, "companion");
    QVERIFY(companion);
    QCOMPARE(recordList(companion, "flag"), QStringList({QString::number(proto::val::ROW_FLAG::readOnly),
                                                         QString::number(proto::val::ROW_FLAG::boardOnly)}));
    const fd::LinkField *codec = schemaRow(frames, "btCodec");
    QVERIFY(codec && recordList(codec, "flag").contains(QString::number(proto::val::ROW_FLAG::restart)));
    QVERIFY(schemaRow(frames, "companionPair"));
    QVERIFY(!schemaRow(frames, "closePlayer"));
    rig.server.poll();
    QCOMPARE(count(phone.frames(), proto::msg::SCHEMA), 0);
}

void LinkTest::setKeptAndRefused()
{
    Rig rig;
    rig.settings.setOutputSlot(QStringLiteral("headphones"));
    Phone phone;
    rig.connect(phone);
    fd::LinkWriter set;
    set.str(proto::tag::SET::text, "maxVolume=72\nvolumeStep=10\ncrossfade=2\nbalance=30\n");
    phone.send(proto::msg::SET, set);
    rig.server.poll();
    std::vector<fd::LinkFrame> frames = phone.frames();
    QCOMPARE(rig.settings.maxVolume(), 70);
    QCOMPARE(rig.settings.volumeStep(), 10);
    QCOMPARE(rig.settings.crossfadeMs(), 2000);
    QCOMPARE(rig.settings.balance(), 20);
    size_t settingsAt = frames.size();
    size_t replyAt = frames.size();
    for (size_t i = 0; i < frames.size(); ++i) {
        if (frames[i].type == proto::msg::SETTINGS && settingsAt == frames.size())
            settingsAt = i;
        if (frames[i].type == proto::msg::SET && frames[i].flags == proto::flagReply)
            replyAt = i;
    }
    QVERIFY(settingsAt < replyAt && replyAt < frames.size());
    const fd::LinkBody reply = body(frames[replyAt]);
    QCOMPARE(reply.all("kept").size(), size_t(4));
    QCOMPARE(reply.u("rev"), body(frames[settingsAt]).u("rev"));
    QVERIFY(settingsText(frames).contains(QStringLiteral("maxVolume=70\n")));

    fd::LinkWriter partly;
    partly.str(proto::tag::SET::text, "maxVolume=60\nnope=1\n");
    phone.send(proto::msg::SET, partly);
    rig.server.poll();
    frames = phone.frames();
    const fd::LinkFrame *ok = nullptr;
    for (const fd::LinkFrame &f : frames) {
        if (f.type == proto::msg::SET && f.flags == proto::flagReply)
            ok = &f;
    }
    QVERIFY(ok);
    QCOMPARE(body(*ok).all("refused").size(), size_t(1));
    QCOMPARE(body(*ok).all("refused").front()->s, std::string("nope"));
    QCOMPARE(rig.settings.maxVolume(), 60);

    for (const char *text : {"companion=off\n", "maxVolume=abc\n", "volumeStep=4.5\n", "output=loud\n",
                             "balance=1=2\n", "companionPair=on\n"}) {
        fd::LinkWriter bad;
        bad.str(proto::tag::SET::text, text);
        phone.send(proto::msg::SET, bad);
        rig.server.poll();
        frames = phone.frames();
        QCOMPARE(errorCode(frames, proto::msg::SET), uint64_t(proto::err::refused));
    }
    QVERIFY(rig.settings.companion());
    QCOMPARE(rig.settings.maxVolume(), 60);
    QCOMPARE(rig.settings.volumeStep(), 10);
    rig.settings.setOutputSlot(QStringLiteral("usb"));
    fd::LinkWriter gain;
    gain.str(proto::tag::SET::text, "hpGain=high\n");
    phone.send(proto::msg::SET, gain);
    rig.server.poll();
    frames = phone.frames();
    QCOMPARE(errorCode(frames, proto::msg::SET), uint64_t(proto::err::refused));
    QVERIFY(recordList(schemaRow(frames, "hpGain"), "flag").contains(QString::number(proto::val::ROW_FLAG::readOnly)));
}

void LinkTest::draftRefused()
{
    Rig rig;
    Phone phone;
    rig.connect(phone);
    fd::LinkWriter draft;
    draft.u8(proto::tag::DRAFT::kind, proto::val::DRAFT_KIND::sleep).u32(proto::tag::DRAFT::minutes, 30);
    phone.send(proto::msg::DRAFT, draft);
    rig.server.poll();
    QCOMPARE(errorCode(phone.frames(), proto::msg::DRAFT), uint64_t(proto::err::refused));
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
