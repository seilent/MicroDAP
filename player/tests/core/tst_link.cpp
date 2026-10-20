#include "ArtistStore.h"
#include "Eq.h"
#include "FakeBluezModel.h"
#include "HubModels.h"
#include "Library.h"
#include "Lyrics.h"
#include "LinkProfile.h"
#include "LinkServer.h"
#include "Models.h"
#include "Player.h"
#include "Queue.h"
#include "Settings.h"
#include "SettingsRows.h"
#include "VolumeKeys.h"

#include "LinkProto.h"

#include <QBuffer>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusUnixFileDescriptor>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QtTest>

#include <limits>
#include <memory>
#include <optional>
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
    void hello(uint16_t major = uint16_t(proto::major), uint64_t features = 0)
    {
        fd::LinkWriter w;
        w.u32(proto::tag::HELLO::magic, uint32_t(proto::magic))
            .u16(proto::tag::HELLO::major, major)
            .u16(proto::tag::HELLO::minor, uint16_t(proto::minor))
            .str(proto::tag::HELLO::appVersion, "test");
        if (features)
            w.u64(proto::tag::HELLO::features, features);
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
    QStringList artists;
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
                else if (name == "artist")
                    out.artists.push_back(QString::fromStdString(x.s));
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
    void eqHelloAndPages();
    void eqOldApp();
    void eqSet();
    void eqSetGraphic();
    void eqPreset();
    void eqSetDuringPendingWrite();
    void eqRemoteCancelsFirst();
    void eqPresetReplaceReloads();
    void eqPresetManyProfiles();
    void eqPresetCap();
    void schemaConfirmPerPeer();
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
    void trackArt();
    void artGetCoalescedAndCapped();
    void artEncode();
    void lyricsLink();
    void queueLyricsFlags();
    void artistStore();
    void artistStoreResumeAndCap();
    void imagesOverTheLink();
    void artistAsks();
    void schemaAfterHello();
    void setKeptAndRefused();
    void draftRefused();
    void codecPerRole();
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
                                  | uint64_t(1) << proto::feature::queue | uint64_t(1) << proto::feature::art
                                  | uint64_t(1) << proto::feature::lyrics | uint64_t(1) << proto::feature::lyricsGet
                                  | uint64_t(1) << proto::feature::image | uint64_t(1) << proto::feature::imgOrigin
                                  | uint64_t(1) << proto::feature::artBlock | uint64_t(1) << proto::feature::artPick
                                  | uint64_t(1) << proto::feature::stateClock
                 | uint64_t(1) << proto::feature::rowConfirm);
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


constexpr uint64_t eqApp = 1ull << proto::feature::eqSlots | 1ull << proto::feature::eqPass
                           | 1ull << proto::feature::outputKinds | 1ull << proto::feature::rowConfirm;

bool writeText(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(text) == text.size();
}

struct EqRig
{
    QTemporaryDir dir;
    Rig rig;
    std::unique_ptr<EqControl> eq;

    explicit EqRig(int cards = 0, int presets = 0)
    {
        qputenv("MICRODAP_EQ_DIR", dir.path().toUtf8());
        for (int i = 0; i < cards; ++i)
            writeText(dir.filePath(QStringLiteral("eq/Card Headphone Model %1 ParametricEQ.txt").arg(i)),
                      "Preamp: -1 dB\nFilter 1: ON PK Fc 1000 Hz Gain 1 dB Q 1.41\n");
        for (int i = 0; i < presets; ++i)
            writeText(dir.filePath(QStringLiteral("eq/presets/Saved %1.txt").arg(i)),
                      "Preamp: -1 dB\nFilter 1: ON PK Fc 1000 Hz Gain 1 dB Q 1.41\n");
        writeText(dir.filePath(QStringLiteral("eq/presets/Rock.txt")),
                  "Preamp: -2 dB\nFilter 1: ON PK Fc 1000 Hz Gain 3 dB Q 1.41\n");
        writeText(dir.filePath(QStringLiteral("eq/HD600 ParametricEQ.txt")),
                  "Preamp: -3 dB\nFilter 1: ON LSC Fc 105 Hz Gain 4 dB Q 0.71\nFilter 2: ON LPQ Fc 18000 Hz Gain 0 dB Q 0.71\n");
        writeText(dir.filePath(QStringLiteral("eq/graphic/Bass.ini")), "[General]\ngain1=6\ngain2=4\n");
        eq = std::make_unique<EqControl>(&rig.settings);
        eq->setActiveOutput(QStringLiteral("speaker"));
        rig.server.setEq(eq.get());
    }

    std::vector<fd::LinkFrame> drain(Phone &phone)
    {
        std::vector<fd::LinkFrame> out;
        for (int i = 0; i < 20; ++i) {
            rig.server.poll();
            const std::vector<fd::LinkFrame> more = phone.frames();
            out.insert(out.end(), more.begin(), more.end());
        }
        return out;
    }

    std::vector<fd::LinkFrame> connect(Phone &phone, uint64_t features = eqApp)
    {
        rig.server.open(phone.dev, phoneA);
        phone.dev = -1;
        phone.hello(uint16_t(proto::major), features);
        return drain(phone);
    }

    std::vector<fd::LinkFrame> ask(Phone &phone, uint8_t type, const fd::LinkWriter &w)
    {
        phone.send(type, w);
        return drain(phone);
    }
};

struct EqPage
{
    int pages = 0;
    QString key;
    QString graphicKey;
    uint64_t mode = 255;
    QStringList names;
    QStringList presets;
    QStringList graphicNames;
    int bands = -1;
    int preamp10 = 0;
    QList<int> gains;
    bool output = false;
    bool lastMore = true;
};

EqPage eqPage(const std::vector<fd::LinkFrame> &frames, const QString &slot)
{
    EqPage out;
    for (const fd::LinkFrame &f : frames) {
        if (f.type != proto::msg::EQ || (f.flags & proto::flagReply))
            continue;
        const fd::LinkBody b = body(f);
        if (QString::fromStdString(b.str("slot")) != slot)
            continue;
        if (out.pages == 0 || b.u("offset") == 0) {
            if (b.u("offset") == 0 && !b.has("graphicOffset"))
                out = EqPage();
        }
        ++out.pages;
        out.output = out.output || b.has("output");
        out.key = QString::fromStdString(b.str("key"));
        out.graphicKey = QString::fromStdString(b.str("graphicKey"));
        out.mode = b.u("mode", 255);
        out.lastMore = b.u("more") != 0;
        for (const fd::LinkField *n : b.all("profile")) {
            const fd::LinkBody nb{n->rec};
            out.names.append(QString::fromStdString(nb.str("key")));
            if (nb.u("preset"))
                out.presets.append(QString::fromStdString(nb.str("key")));
        }
        for (const fd::LinkField *n : b.all("graphicName"))
            out.graphicNames.append(QString::fromStdString(fd::LinkBody{n->rec}.str("key")));
        if (const fd::LinkField *ints = b.get("ints"))
            out.bands = int(fd::LinkBody{ints->rec}.all("band").size());
        out.gains.clear();
        if (const fd::LinkField *g = b.get("gains")) {
            const fd::LinkBody gb{g->rec};
            out.preamp10 = int(gb.i("preamp10"));
            for (const fd::LinkField *band : gb.all("band"))
                out.gains.append(int(fd::LinkBody{band->rec}.i("gain10")));
        }
    }
    return out;
}

fd::LinkWriter bandsWriter(std::initializer_list<std::array<int, 4>> bands, int preamp10 = 0)
{
    fd::LinkWriter p;
    p.i32(proto::tag::Profile::preamp10, preamp10);
    for (const std::array<int, 4> &b : bands) {
        fd::LinkWriter r;
        r.u8(proto::tag::Band::type, uint8_t(b[0])).i32(proto::tag::Band::grid, b[1]).i32(proto::tag::Band::gain10, b[2])
            .i32(proto::tag::Band::q100, b[3]);
        p.rec(proto::tag::Profile::band, r);
    }
    return p;
}

fd::LinkWriter gainsWriter(std::initializer_list<int> gains, int count = 10)
{
    fd::LinkWriter g;
    int i = 0;
    for (int v : gains) {
        if (i >= count)
            break;
        fd::LinkWriter b;
        b.i32(proto::tag::GraphicBand::grid, 4 + 6 * i).i32(proto::tag::GraphicBand::gain10, v);
        g.rec(proto::tag::Graphic::band, b);
        ++i;
    }
    for (; i < count; ++i) {
        fd::LinkWriter b;
        b.i32(proto::tag::GraphicBand::grid, 4 + 6 * i).i32(proto::tag::GraphicBand::gain10, 0);
        g.rec(proto::tag::Graphic::band, b);
    }
    return g;
}

uint64_t eqCode(const std::vector<fd::LinkFrame> &frames, uint8_t type)
{
    for (const fd::LinkFrame &f : frames) {
        if (f.type == type && (f.flags & proto::flagReply))
            return (f.flags & proto::flagError) ? body(f).u("code") : 0;
    }
    return 255;
}

void LinkTest::eqHelloAndPages()
{
    EqRig r;
    Phone phone;
    const std::vector<fd::LinkFrame> frames = r.connect(phone);
    const fd::LinkFrame *reply = find(frames, proto::msg::HELLO, true);
    QVERIFY(reply);
    const fd::LinkBody h = body(*reply);
    namespace f = proto::feature;
    const uint64_t features = h.u("features");
    for (int bit : {f::eq, f::eqPresets, f::eqSlots, f::eqGraphic, f::eqPass, f::outputKinds, f::rowConfirm})
        QVERIFY2((features >> bit) & 1, qPrintable(QString::number(bit)));
    QStringList slotList;
    QStringList labels;
    for (const fd::LinkField *e : h.all("eqSlot")) {
        const fd::LinkBody eb{e->rec};
        slotList.append(QString::fromStdString(eb.str("key")));
        labels.append(QString::fromStdString(eb.str("label")));
    }
    QCOMPARE(slotList, QStringList({QStringLiteral("headphones"), QStringLiteral("usb"), QStringLiteral("speaker"),
                                 QStringLiteral("bt")}));
    QCOMPARE(labels.at(0), QStringLiteral("HEADPHONES"));
    QList<uint64_t> types;
    for (const fd::LinkField *t : h.all("bandTypes"))
        types.append(t->u);
    QCOMPARE(types, QList<uint64_t>({0, 1, 2, 3, 4, 5, 6, 7}));
    for (const QString &slot : std::as_const(slotList)) {
        const EqPage page = eqPage(frames, slot);
        QVERIFY2(page.pages >= 2, qPrintable(slot));
        QVERIFY(!page.output);
        QCOMPARE(page.key, QStringLiteral("off"));
        QCOMPARE(page.mode, uint64_t(proto::val::EQ_MODE::parametric));
        QCOMPARE(page.graphicKey, QStringLiteral("flat"));
        QCOMPARE(page.names, QStringList({QStringLiteral("off"), QStringLiteral("ROCK"), QStringLiteral("HD600"),
                                          QStringLiteral("custom")}));
        QCOMPARE(page.presets, QStringList{QStringLiteral("ROCK")});
        QCOMPARE(page.graphicNames, QStringList({QStringLiteral("off"), QStringLiteral("flat"), QStringLiteral("BASS"),
                                                 QStringLiteral("custom")}));
        QCOMPARE(page.gains, QList<int>({0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
        QVERIFY(!page.lastMore);
    }
    const fd::LinkFrame *state = find(frames, proto::msg::STATE);
    QVERIFY(state);
    QCOMPARE(QString::fromStdString(body(*state).str("eqSlot")), QStringLiteral("speaker"));
    QCOMPARE(body(*state).u("output"), uint64_t(proto::val::OUTPUT::speaker));
    r.eq->setActiveOutput(QStringLiteral("usb"));
    const std::vector<fd::LinkFrame> after = r.drain(phone);
    const fd::LinkFrame *next = find(after, proto::msg::STATE);
    QVERIFY(next);
    QCOMPARE(QString::fromStdString(body(*next).str("eqSlot")), QStringLiteral("usb"));
    QCOMPARE(body(*next).u("output"), uint64_t(proto::val::OUTPUT::usbDac));
    QCOMPARE(count(r.drain(phone), proto::msg::EQ), 0);
}

void LinkTest::eqOldApp()
{
    EqRig r;
    Phone phone;
    const std::vector<fd::LinkFrame> frames = r.connect(phone, 0);
    QCOMPARE(count(frames, proto::msg::EQ), 0);
    const fd::LinkFrame *reply = find(frames, proto::msg::HELLO, true);
    QVERIFY(reply);
    const fd::LinkBody h = body(*reply);
    namespace f = proto::feature;
    for (int bit : {f::eq, f::eqPresets, f::eqSlots, f::eqGraphic, f::eqPass})
        QVERIFY2(!((h.u("features") >> bit) & 1), qPrintable(QString::number(bit)));
    QVERIFY((h.u("features") >> f::outputKinds) & 1);
    QVERIFY((h.u("features") >> f::settings) & 1);
    QVERIFY(h.all("eqSlot").empty());
    QVERIFY(h.all("bandTypes").empty());
    const fd::LinkFrame *state = find(frames, proto::msg::STATE);
    QVERIFY(state);
    QVERIFY(!body(*state).has("eqSlot"));
    QCOMPARE(body(*state).u("output"), uint64_t(proto::val::OUTPUT::wired));
    fd::LinkWriter set;
    set.u8(proto::tag::EQ_SET::output, 0).str(proto::tag::EQ_SET::key, "off");
    QCOMPARE(eqCode(r.ask(phone, proto::msg::EQ_SET, set), proto::msg::EQ_SET), uint64_t(proto::err::unknown));
    fd::LinkWriter preset;
    preset.u8(proto::tag::EQ_PRESET::op, proto::val::EQ_PRESET_OP::remove).str(proto::tag::EQ_PRESET::key, "ROCK");
    QCOMPARE(eqCode(r.ask(phone, proto::msg::EQ_PRESET, preset), proto::msg::EQ_PRESET), uint64_t(proto::err::unknown));
    QVERIFY(r.eq->devicePreset(QStringLiteral("ROCK"), false));
    Phone half;
    std::vector<fd::LinkFrame> partly = r.connect(half, 1ull << proto::feature::eqSlots);
    QCOMPARE(count(partly, proto::msg::EQ), 0);
    const fd::LinkFrame *halfReply = find(partly, proto::msg::HELLO, true);
    QVERIFY(halfReply);
    QVERIFY(!((body(*halfReply).u("features") >> proto::feature::eq) & 1));
    QVERIFY(body(*halfReply).all("eqSlot").empty());
}

void LinkTest::eqSet()
{
    EqRig r;
    Phone phone;
    r.connect(phone);
    namespace t = proto::tag::EQ_SET;
    fd::LinkWriter set;
    set.str(t::slot, "headphones").rec(t::ints, bandsWriter({{1, 34, 37, 141}, {5, -1, 0, 71}}, -40));
    std::vector<fd::LinkFrame> frames = r.ask(phone, proto::msg::EQ_SET, set);
    QCOMPARE(eqCode(frames, proto::msg::EQ_SET), 0u);
    size_t pushAt = frames.size();
    size_t replyAt = frames.size();
    for (size_t i = 0; i < frames.size(); ++i) {
        if (frames[i].type == proto::msg::EQ && pushAt == frames.size())
            pushAt = i;
        if (frames[i].type == proto::msg::EQ_SET && (frames[i].flags & proto::flagReply))
            replyAt = i;
    }
    QVERIFY(pushAt < replyAt);
    EqPage page = eqPage(frames, QStringLiteral("headphones"));
    QCOMPARE(page.key, QStringLiteral("custom"));
    QCOMPARE(page.bands, 2);
    QCOMPARE(r.eq->key(QStringLiteral("headphones")), QStringLiteral("custom"));
    const EqProfile p = r.eq->profile(QStringLiteral("headphones"));
    QCOMPARE(p.count, 2);
    QCOMPARE(p.preamp, -4.0);
    QCOMPARE(p.bands[0].gain, 3.7);
    QCOMPARE(p.bands[1].type, EqBand::LowPass);
    QCOMPARE(p.bands[1].gain, 0.0);

    fd::LinkWriter pick;
    pick.str(t::slot, "headphones").str(t::key, "ROCK");
    QCOMPARE(eqCode(r.ask(phone, proto::msg::EQ_SET, pick), proto::msg::EQ_SET), 0u);
    QCOMPARE(r.eq->key(QStringLiteral("headphones")), QStringLiteral("ROCK"));
    QCOMPARE(r.eq->profile(QStringLiteral("headphones")).bands[0].gain, 3.0);

    auto refused = [&](fd::LinkWriter w) { return eqCode(r.ask(phone, proto::msg::EQ_SET, w), proto::msg::EQ_SET); };
    fd::LinkWriter unknownKey;
    unknownKey.str(t::slot, "headphones").str(t::key, "NOPE");
    QCOMPARE(refused(unknownKey), uint64_t(proto::err::badValue));
    fd::LinkWriter withOutput;
    withOutput.u8(t::output, 0).str(t::key, "off");
    QCOMPARE(refused(withOutput), uint64_t(proto::err::badValue));
    fd::LinkWriter unlisted;
    unlisted.str(t::slot, "aux").str(t::key, "off");
    QCOMPARE(refused(unlisted), uint64_t(proto::err::badValue));
    fd::LinkWriter none;
    none.str(t::slot, "usb");
    QCOMPARE(refused(none), uint64_t(proto::err::badValue));
    fd::LinkWriter empty;
    empty.str(t::slot, "usb").rec(t::ints, bandsWriter({}));
    QCOMPARE(refused(empty), uint64_t(proto::err::badValue));
    fd::LinkWriter loud;
    loud.str(t::slot, "usb").rec(t::ints, bandsWriter({{1, 34, 130, 141}}));
    QCOMPARE(refused(loud), uint64_t(proto::err::badValue));
    fd::LinkWriter badType;
    badType.str(t::slot, "usb").rec(t::ints, bandsWriter({{9, 34, 10, 141}}));
    QCOMPARE(refused(badType), uint64_t(proto::err::badValue));
    QCOMPARE(r.eq->key(QStringLiteral("usb")), QStringLiteral("off"));
    fd::LinkWriter off;
    off.str(t::slot, "headphones").str(t::key, "off");
    QCOMPARE(refused(off), 0u);
    QCOMPARE(r.eq->key(QStringLiteral("headphones")), QStringLiteral("off"));
}

void LinkTest::eqSetGraphic()
{
    EqRig r;
    Phone phone;
    r.connect(phone);
    namespace t = proto::tag::EQ_SET;
    namespace md = proto::val::EQ_MODE;
    auto code = [&](fd::LinkWriter w) { return eqCode(r.ask(phone, proto::msg::EQ_SET, w), proto::msg::EQ_SET); };
    const QString out = QStringLiteral("speaker");
    fd::LinkWriter bass;
    bass.str(t::slot, "speaker").str(t::graphicKey, "BASS");
    std::vector<fd::LinkFrame> frames = r.ask(phone, proto::msg::EQ_SET, bass);
    QCOMPARE(eqCode(frames, proto::msg::EQ_SET), 0u);
    QVERIFY(r.eq->graphic(out));
    EqPage page = eqPage(frames, out);
    QCOMPARE(page.mode, uint64_t(md::graphic));
    QCOMPARE(page.graphicKey, QStringLiteral("BASS"));
    QCOMPARE(page.gains.mid(0, 2), QList<int>({60, 40}));
    QVERIFY(page.preamp10 < 0);

    fd::LinkWriter gains;
    gains.str(t::slot, "speaker").u8(t::mode, md::graphic).rec(t::gains, gainsWriter({-30, 25}));
    QCOMPARE(code(gains), 0u);
    QCOMPARE(r.eq->target(out).preset, QStringLiteral("custom"));
    QCOMPARE(r.eq->customGraphic(out)[1], 2.5);
    fd::LinkWriter shortList;
    shortList.str(t::slot, "speaker").rec(t::gains, gainsWriter({}, 9));
    QCOMPARE(code(shortList), uint64_t(proto::err::badValue));
    fd::LinkWriter tooLoud;
    tooLoud.str(t::slot, "speaker").rec(t::gains, gainsWriter({130}));
    QCOMPARE(code(tooLoud), uint64_t(proto::err::badValue));
    fd::LinkWriter unlisted;
    unlisted.str(t::slot, "speaker").str(t::graphicKey, "TREBLE");
    QCOMPARE(code(unlisted), uint64_t(proto::err::badValue));
    fd::LinkWriter mixed;
    mixed.str(t::slot, "speaker").str(t::key, "ROCK").str(t::graphicKey, "flat");
    QCOMPARE(code(mixed), uint64_t(proto::err::badValue));
    fd::LinkWriter mixedInts;
    mixedInts.str(t::slot, "speaker").rec(t::ints, bandsWriter({{1, 34, 10, 100}})).rec(t::gains, gainsWriter({}));
    QCOMPARE(code(mixedInts), uint64_t(proto::err::badValue));

    fd::LinkWriter keepGraphic;
    keepGraphic.str(t::slot, "speaker").u8(t::mode, md::graphic).str(t::key, "ROCK");
    QCOMPARE(code(keepGraphic), 0u);
    QVERIFY(r.eq->graphic(out));
    QCOMPARE(r.eq->target(out).key, QStringLiteral("ROCK"));
    QCOMPARE(r.eq->target(out).preset, QStringLiteral("custom"));

    fd::LinkWriter toParametric;
    toParametric.str(t::slot, "speaker").rec(t::ints, bandsWriter({{1, 34, 20, 100}}));
    frames = r.ask(phone, proto::msg::EQ_SET, toParametric);
    QCOMPARE(eqCode(frames, proto::msg::EQ_SET), 0u);
    QVERIFY(!r.eq->graphic(out));
    page = eqPage(frames, out);
    QCOMPARE(page.mode, uint64_t(md::parametric));
    QCOMPARE(page.key, QStringLiteral("custom"));
    QCOMPARE(r.eq->profile(out).bands[0].gain, 2.0);

    fd::LinkWriter keepParametric;
    keepParametric.str(t::slot, "speaker").u8(t::mode, md::parametric).str(t::graphicKey, "flat");
    QCOMPARE(code(keepParametric), 0u);
    QVERIFY(!r.eq->graphic(out));
    QCOMPARE(r.eq->target(out).preset, QStringLiteral("flat"));

    fd::LinkWriter both;
    both.str(t::slot, "speaker").str(t::graphicKey, "BASS").rec(t::gains, gainsWriter({10}));
    QCOMPARE(code(both), 0u);
    QVERIFY(r.eq->graphic(out));
    QCOMPARE(r.eq->target(out).preset, QStringLiteral("BASS"));
    QCOMPARE(r.eq->customGraphic(out)[0], 1.0);
    fd::LinkWriter offKey;
    offKey.str(t::slot, "speaker").str(t::graphicKey, "off");
    frames = r.ask(phone, proto::msg::EQ_SET, offKey);
    QCOMPARE(eqCode(frames, proto::msg::EQ_SET), 0u);
    QVERIFY(eqPage(frames, out).gains.isEmpty());
    QCOMPARE(r.eq->key(out), QStringLiteral("off"));
}

void LinkTest::eqPreset()
{
    EqRig r;
    Phone phone;
    r.connect(phone);
    namespace t = proto::tag::EQ_PRESET;
    namespace op = proto::val::EQ_PRESET_OP;
    namespace md = proto::val::EQ_MODE;
    auto ask = [&](fd::LinkWriter w, QString *key = nullptr) {
        const std::vector<fd::LinkFrame> frames = r.ask(phone, proto::msg::EQ_PRESET, w);
        for (const fd::LinkFrame &f : frames) {
            if (f.type == proto::msg::EQ_PRESET && (f.flags & proto::flagReply) && key)
                *key = QString::fromStdString(body(f).str("key"));
        }
        return eqCode(frames, proto::msg::EQ_PRESET);
    };
    QString key;
    const EqProfile before = r.eq->customProfile(QStringLiteral("headphones"));
    fd::LinkWriter save;
    save.u8(t::op, op::save).str(t::slot, "headphones").str(t::name, "Warm").rec(t::ints, bandsWriter({{2, 16, 30, 71}}));
    {
        const std::vector<fd::LinkFrame> frames = r.ask(phone, proto::msg::EQ_PRESET, save);
        QCOMPARE(eqCode(frames, proto::msg::EQ_PRESET), 0u);
        QStringList pushed;
        bool replied = false;
        for (const fd::LinkFrame &f : frames) {
            if (f.type == proto::msg::EQ_PRESET && (f.flags & proto::flagReply))
                replied = true;
            if (f.type == proto::msg::EQ && !replied && !pushed.contains(QString::fromStdString(body(f).str("slot"))))
                pushed.append(QString::fromStdString(body(f).str("slot")));
        }
        QCOMPARE(pushed, LinkEq::slotKeys());
    }
    fd::LinkWriter again;
    again.u8(t::op, op::save).str(t::slot, "headphones").str(t::name, "Warm").rec(t::ints, bandsWriter({{2, 16, 30, 71}}))
        .flag(t::replace, true);
    QCOMPARE(ask(again, &key), 0u);
    QCOMPARE(key, QStringLiteral("WARM"));
    QCOMPARE(r.eq->key(QStringLiteral("headphones")), QStringLiteral("WARM"));
    QVERIFY(r.eq->devicePreset(QStringLiteral("WARM"), false));
    QVERIFY(r.eq->customProfile(QStringLiteral("headphones")) == before);
    fd::LinkWriter clash;
    clash.u8(t::op, op::save).str(t::slot, "usb").str(t::name, "warm");
    QCOMPARE(ask(clash), uint64_t(proto::err::exists));
    fd::LinkWriter replace;
    replace.u8(t::op, op::save).str(t::slot, "usb").str(t::name, "warm").flag(t::replace, true);
    QCOMPARE(ask(replace, &key), 0u);
    QCOMPARE(key, QStringLiteral("WARM"));
    QCOMPARE(r.eq->key(QStringLiteral("usb")), QStringLiteral("WARM"));
    fd::LinkWriter card;
    card.u8(t::op, op::save).str(t::slot, "usb").str(t::name, "HD600").flag(t::replace, true);
    QCOMPARE(ask(card), uint64_t(proto::err::exists));
    fd::LinkWriter badName;
    badName.u8(t::op, op::save).str(t::slot, "usb").str(t::name, "a/b");
    QCOMPARE(ask(badName), uint64_t(proto::err::badValue));
    fd::LinkWriter noSlot;
    noSlot.u8(t::op, op::save).str(t::name, "Other");
    QCOMPARE(ask(noSlot), uint64_t(proto::err::badValue));

    fd::LinkWriter rename;
    rename.u8(t::op, op::rename).str(t::key, "WARM").str(t::name, "Cozy");
    QCOMPARE(ask(rename, &key), 0u);
    QCOMPARE(key, QStringLiteral("COZY"));
    QCOMPARE(r.eq->key(QStringLiteral("headphones")), QStringLiteral("COZY"));
    QCOMPARE(r.eq->key(QStringLiteral("usb")), QStringLiteral("COZY"));
    fd::LinkWriter taken;
    taken.u8(t::op, op::rename).str(t::key, "COZY").str(t::name, "Rock");
    QCOMPARE(ask(taken), uint64_t(proto::err::exists));
    fd::LinkWriter cardRename;
    cardRename.u8(t::op, op::rename).str(t::key, "HD600").str(t::name, "Mine");
    QCOMPARE(ask(cardRename), uint64_t(proto::err::readOnly));
    fd::LinkWriter offRemove;
    offRemove.u8(t::op, op::remove).str(t::key, "off");
    QCOMPARE(ask(offRemove), uint64_t(proto::err::readOnly));
    fd::LinkWriter missing;
    missing.u8(t::op, op::remove).str(t::key, "GONE");
    QCOMPARE(ask(missing), uint64_t(proto::err::notFound));
    fd::LinkWriter remove;
    remove.u8(t::op, op::remove).str(t::key, "COZY");
    QCOMPARE(ask(remove), 0u);
    QCOMPARE(r.eq->key(QStringLiteral("headphones")), QStringLiteral("off"));
    QCOMPARE(r.eq->key(QStringLiteral("usb")), QStringLiteral("off"));

    fd::LinkWriter graphicSave;
    graphicSave.u8(t::op, op::save).str(t::slot, "bt").u8(t::mode, md::graphic).str(t::name, "Vocal")
        .rec(t::gains, gainsWriter({0, 0, 0, 0, 30}));
    QCOMPARE(ask(graphicSave, &key), 0u);
    QCOMPARE(key, QStringLiteral("VOCAL"));
    QVERIFY(r.eq->graphic(QStringLiteral("bt")));
    QCOMPARE(r.eq->target(QStringLiteral("bt")).preset, QStringLiteral("VOCAL"));
    QCOMPARE((*r.eq->presetGains(QStringLiteral("bt")))[4], 3.0);
    fd::LinkWriter intsOnGraphic;
    intsOnGraphic.u8(t::op, op::save).str(t::slot, "bt").u8(t::mode, md::graphic).rec(t::ints, bandsWriter({{1, 34, 0, 100}}));
    QCOMPARE(ask(intsOnGraphic), uint64_t(proto::err::badValue));
    fd::LinkWriter flatRemove;
    flatRemove.u8(t::op, op::remove).u8(t::mode, md::graphic).str(t::key, "flat");
    QCOMPARE(ask(flatRemove), uint64_t(proto::err::readOnly));
    fd::LinkWriter mismatch;
    mismatch.u8(t::op, op::remove).u8(t::mode, md::graphic).str(t::key, "ROCK");
    QCOMPARE(ask(mismatch), uint64_t(proto::err::notFound));
    fd::LinkWriter graphicRemove;
    graphicRemove.u8(t::op, op::remove).u8(t::mode, md::graphic).str(t::key, "VOCAL");
    QCOMPARE(ask(graphicRemove), 0u);
    QCOMPARE(r.eq->key(QStringLiteral("bt")), QStringLiteral("off"));
}

void LinkTest::eqSetDuringPendingWrite()
{
    EqRig r;
    Phone phone;
    r.connect(phone);
    const QString out = QStringLiteral("headphones");
    r.eq->cycleProfile(out, -1);
    QCOMPARE(r.eq->key(out), QStringLiteral("custom"));
    r.eq->stepGain(out, 0, 1, true);
    namespace t = proto::tag::EQ_SET;
    fd::LinkWriter set;
    set.str(t::slot, "headphones").rec(t::ints, bandsWriter({{1, 34, -50, 141}}));
    QCOMPARE(eqCode(r.ask(phone, proto::msg::EQ_SET, set), proto::msg::EQ_SET), 0u);
    QTest::qWait(EqControl::writeDelayMs + 100);
    const EqProfile stored = EqFilters::decode(r.rig.settings.value(EqFilters::key(out, QStringLiteral("filters"))).toStringList());
    QCOMPARE(stored.bands[0].gain, -5.0);
    QCOMPARE(r.eq->profile(out).bands[0].gain, -5.0);
}

void LinkTest::eqRemoteCancelsFirst()
{
    EqRig r;
    Phone phone;
    r.connect(phone);
    const QString out = QStringLiteral("headphones");
    namespace t = proto::tag::EQ_SET;
    fd::LinkWriter rock;
    rock.str(t::slot, "headphones").str(t::key, "ROCK");
    QCOMPARE(eqCode(r.ask(phone, proto::msg::EQ_SET, rock), proto::msg::EQ_SET), 0u);
    r.eq->cycleProfile(out, 1);
    QCOMPARE(r.eq->key(out), QStringLiteral("HD600"));
    QMetaObject::Connection revert = QObject::connect(r.eq.get(), &EqControl::remoteEdit, r.eq.get(),
                                                      [&r, out] { r.eq->cycleProfile(out, -1); });
    fd::LinkWriter graphicSide;
    graphicSide.str(t::slot, "headphones").u8(t::mode, proto::val::EQ_MODE::graphic).str(t::graphicKey, "flat");
    QCOMPARE(eqCode(r.ask(phone, proto::msg::EQ_SET, graphicSide), proto::msg::EQ_SET), 0u);
    QObject::disconnect(revert);
    QVERIFY(r.eq->graphic(out));
    QCOMPARE(r.eq->target(out).key, QStringLiteral("ROCK"));

    const QString usb = QStringLiteral("usb");
    r.eq->cycleProfile(usb, -1);
    QCOMPARE(r.eq->key(usb), QStringLiteral("custom"));
    const double original = r.eq->customProfile(usb).bands[0].gain;
    r.eq->stepGain(usb, 0, 1, true);
    revert = QObject::connect(r.eq.get(), &EqControl::remoteEdit, r.eq.get(),
                              [&r, usb, original] { r.eq->setBandValue(usb, 0, QStringLiteral("gain"), original); });
    fd::LinkWriter save;
    save.u8(proto::tag::EQ_PRESET::op, proto::val::EQ_PRESET_OP::save).str(proto::tag::EQ_PRESET::slot, "usb")
        .str(proto::tag::EQ_PRESET::name, "Kept");
    QCOMPARE(eqCode(r.ask(phone, proto::msg::EQ_PRESET, save), proto::msg::EQ_PRESET), 0u);
    QObject::disconnect(revert);
    const std::optional<EqProfile> kept = EqParser::parseFile(r.dir.filePath(QStringLiteral("eq/presets/Kept.txt")));
    QVERIFY(kept);
    QCOMPARE(kept->bands[0].gain, original);
}

void LinkTest::eqPresetReplaceReloads()
{
    EqRig r;
    Phone phone;
    r.connect(phone);
    fd::LinkWriter rock;
    rock.str(proto::tag::EQ_SET::slot, "usb").str(proto::tag::EQ_SET::key, "ROCK");
    QCOMPARE(eqCode(r.ask(phone, proto::msg::EQ_SET, rock), proto::msg::EQ_SET), 0u);
    QCOMPARE(r.eq->profile(QStringLiteral("usb")).bands[0].gain, 3.0);
    namespace t = proto::tag::EQ_PRESET;
    fd::LinkWriter replace;
    replace.u8(t::op, proto::val::EQ_PRESET_OP::save).str(t::slot, "headphones").str(t::name, "rock")
        .flag(t::replace, true).rec(t::ints, bandsWriter({{1, 34, -60, 141}}));
    QCOMPARE(eqCode(r.ask(phone, proto::msg::EQ_PRESET, replace), proto::msg::EQ_PRESET), 0u);
    QCOMPARE(r.eq->key(QStringLiteral("usb")), QStringLiteral("ROCK"));
    QCOMPARE(r.eq->profile(QStringLiteral("usb")).bands[0].gain, -6.0);
    QCOMPARE(r.eq->profile(QStringLiteral("headphones")).bands[0].gain, -6.0);
}

void LinkTest::eqPresetManyProfiles()
{
    EqRig r(400);
    Phone phone;
    std::vector<fd::LinkFrame> frames = r.connect(phone);
    fd::LinkWriter save;
    save.u8(proto::tag::EQ_PRESET::op, proto::val::EQ_PRESET_OP::save).str(proto::tag::EQ_PRESET::slot, "headphones");
    phone.send(proto::msg::EQ_PRESET, save);
    QSet<QString> seen;
    bool replied = false;
    QElapsedTimer timer;
    timer.start();
    while ((seen.size() < 4 || !replied) && timer.elapsed() < waitMs) {
        r.rig.server.poll();
        for (const fd::LinkFrame &f : phone.frames()) {
            if (f.type == proto::msg::EQ_PRESET && (f.flags & proto::flagReply))
                replied = !(f.flags & proto::flagError);
            if (f.type == proto::msg::EQ)
                seen.insert(QString::fromStdString(body(f).str("slot")));
        }
        QTest::qWait(10);
    }
    QVERIFY(replied);
    QCOMPARE(seen.size(), 4);
    QVERIFY(!phone.eof());
    QCOMPARE(r.rig.server.sessions(), 1);
}

void LinkTest::eqPresetCap()
{
    EqRig r(0, EqControl::presetsMax - 1);
    Phone phone;
    r.connect(phone);
    namespace t = proto::tag::EQ_PRESET;
    fd::LinkWriter full;
    full.u8(t::op, proto::val::EQ_PRESET_OP::save).str(t::slot, "headphones").str(t::name, "New");
    QCOMPARE(eqCode(r.ask(phone, proto::msg::EQ_PRESET, full), proto::msg::EQ_PRESET), uint64_t(proto::err::noSpace));
    fd::LinkWriter replace;
    replace.u8(t::op, proto::val::EQ_PRESET_OP::save).str(t::slot, "headphones").str(t::name, "Saved 3").flag(t::replace, true);
    QCOMPARE(eqCode(r.ask(phone, proto::msg::EQ_PRESET, replace), proto::msg::EQ_PRESET), 0u);
    QCOMPARE(r.eq->key(QStringLiteral("headphones")), QStringLiteral("SAVED 3"));
}

void LinkTest::schemaConfirmPerPeer()
{
    EqRig r;
    auto confirmed = [](const std::vector<fd::LinkFrame> &frames) {
        QStringList out;
        for (const fd::LinkFrame &f : frames) {
            if (f.type != proto::msg::SCHEMA)
                continue;
            const fd::LinkBody b = body(f);
            for (const fd::LinkField *row : b.all("row")) {
                const fd::LinkBody rb{row->rec};
                for (const fd::LinkField *flag : rb.all("flag"))
                    if (flag->u == proto::val::ROW_FLAG::confirm)
                        out.append(QString::fromStdString(rb.str("key")));
            }
        }
        return out;
    };
    Phone asks;
    const QStringList withBit = confirmed(r.connect(asks));
    QStringList expected;
    for (const SettingsRows::Row &row : SettingsRows::all()) {
        if (row.raises)
            expected.append(row.linkKey);
    }
    QCOMPARE(expected, QStringList({QStringLiteral("output"), QStringLiteral("hpGain"), QStringLiteral("hpAmp"),
                                    QStringLiteral("spkGain"), QStringLiteral("maxVolume"), QStringLiteral("powerOnVolume"),
                                    QStringLiteral("balance"), QStringLiteral("replaygain"), QStringLiteral("crossfeed")}));
    for (const QString &key : {QStringLiteral("output"), QStringLiteral("spkGain"), QStringLiteral("maxVolume"),
                               QStringLiteral("powerOnVolume"), QStringLiteral("replaygain")})
        QVERIFY2(withBit.contains(key), qPrintable(key));
    for (const QString &key : {QStringLiteral("orientation"), QStringLiteral("autoRotate"), QStringLiteral("scale"),
                               QStringLiteral("crossfade"), QStringLiteral("volumeStep"), QStringLiteral("btCodec")})
        QVERIFY2(!withBit.contains(key), qPrintable(key));
    Phone plain;
    QVERIFY(confirmed(r.connect(plain, 0)).isEmpty());
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
    QCOMPARE(albums.artists.size(), 2);
    QCOMPARE(albums.artists.at(0), QStringLiteral("Artist A"));
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
    QVERIFY(artists.artists.isEmpty());
    Rows artistAlbums = rowsOf(rig.ask(proto::msg::LIB_GET, libGet(proto::val::VIEW::artistAlbums, "Artist A")),
                               proto::msg::LIB, "row");
    QCOMPARE(artistAlbums.names, QStringList({QStringLiteral("Album One")}));
    QCOMPARE(artistAlbums.artists, QStringList({QStringLiteral("Artist A")}));
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

void LinkTest::codecPerRole()
{
    Rig rig;
    rig.settings.setBtCodec(QStringLiteral("aac"));
    Phone phone;
    std::vector<fd::LinkFrame> frames = rig.connect(phone);
    const QString readOnly = QString::number(proto::val::ROW_FLAG::readOnly);
    const QString restart = QString::number(proto::val::ROW_FLAG::restart);
    const fd::LinkField *receiver = schemaRow(frames, "btCodec");
    QVERIFY(receiver);
    QCOMPARE(recordList(receiver, "value"), Settings::codecs());
    QVERIFY(recordList(receiver, "flag").contains(readOnly));
    QVERIFY(recordList(receiver, "flag").contains(restart));
    QVERIFY(settingsText(frames).contains(QStringLiteral("btCodec=aac\n")));
    fd::LinkWriter refused;
    refused.str(proto::tag::SET::text, "btCodec=sbc\n");
    phone.send(proto::msg::SET, refused);
    rig.server.poll();
    QCOMPARE(errorCode(phone.frames(), proto::msg::SET), uint64_t(proto::err::refused));
    QCOMPARE(rig.settings.btCodec(), QStringLiteral("aac"));

    rig.bluez.setRole(QStringLiteral("headphones"));
    QCOMPARE(rig.bluez.activeRole(), QStringLiteral("headphones"));
    rig.server.poll();
    frames = phone.frames();
    const fd::LinkField *headphones = schemaRow(frames, "btCodec");
    QVERIFY(headphones);
    QCOMPARE(recordList(headphones, "value"), Settings::headphoneCodecs());
    QVERIFY(recordList(headphones, "word").contains(QStringLiteral("APTX HD")));
    QVERIFY(recordList(headphones, "flag").isEmpty());
    QVERIFY(settingsText(frames).contains(QStringLiteral("btCodec=auto\n")));
    fd::LinkWriter set;
    set.str(proto::tag::SET::text, "btCodec=aptx_hd\n");
    phone.send(proto::msg::SET, set);
    rig.server.poll();
    frames = phone.frames();
    QCOMPARE(rig.settings.btCodecHeadphones(), QStringLiteral("aptx_hd"));
    QCOMPARE(rig.settings.btCodec(), QStringLiteral("aac"));
    QVERIFY(settingsText(frames).contains(QStringLiteral("btCodec=aptx_hd\n")));
}

const fd::LinkFrame *awaitReply(LibRig &rig, std::vector<fd::LinkFrame> &frames, uint8_t type)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < waitMs) {
        for (const fd::LinkFrame &f : frames) {
            if (f.type == type && (f.flags & proto::flagReply))
                return &f;
        }
        QTest::qWait(10);
        rig.server.poll();
        const std::vector<fd::LinkFrame> more = rig.phone.frames();
        frames.insert(frames.end(), more.begin(), more.end());
    }
    return nullptr;
}

fd::LinkWriter artGet(qint64 track, int px, int offset = -1, qint64 have = -1)
{
    fd::LinkWriter w;
    w.u64(proto::tag::ART_GET::track, uint64_t(track)).u16(proto::tag::ART_GET::px, uint16_t(px));
    if (offset >= 0)
        w.u32(proto::tag::ART_GET::offset, uint32_t(offset));
    if (have >= 0)
        w.u32(proto::tag::ART_GET::have, uint32_t(have));
    return w;
}

void LinkTest::trackArt()
{
    LibRig rig;
    QVERIFY(rig.scan());
    rig.connect();
    const qint64 one = rig.id(QStringLiteral("One"));
    rig.phone.send(proto::msg::ART_GET, artGet(one, 512));
    rig.server.poll();
    std::vector<fd::LinkFrame> frames = rig.phone.frames();
    const fd::LinkFrame *first = awaitReply(rig, frames, proto::msg::ART_GET);
    QVERIFY(first);
    QVERIFY(!(first->flags & proto::flagError));
    fd::LinkBody b = body(*first);
    const quint32 crc = quint32(b.u("crc"));
    const quint32 total = quint32(b.u("total"));
    QVERIFY(total > 0);
    QCOMPARE(b.u("w"), b.u("h"));
    QVERIFY(b.u("w") > 0 && b.u("w") <= 512);
    QByteArray jpeg = QByteArray::fromStdString(b.str("data"));
    QVERIFY(jpeg.size() <= proto::artChunk);
    while (quint32(jpeg.size()) < total) {
        rig.phone.send(proto::msg::ART_GET, artGet(one, 512, jpeg.size(), crc));
        rig.server.poll();
        frames = rig.phone.frames();
        const fd::LinkFrame *chunk = awaitReply(rig, frames, proto::msg::ART_GET);
        QVERIFY(chunk);
        const fd::LinkBody cb = body(*chunk);
        QCOMPARE(cb.u("offset"), uint64_t(jpeg.size()));
        jpeg += QByteArray::fromStdString(cb.str("data"));
    }
    QCOMPARE(linkCrc32(jpeg), crc);
    QImage image;
    QVERIFY(image.loadFromData(jpeg, "JPEG"));
    QCOMPARE(image.width(), int(b.u("w")));
    rig.phone.send(proto::msg::ART_GET, artGet(one, 512, 0, crc));
    rig.server.poll();
    frames = rig.phone.frames();
    const fd::LinkFrame *hit = awaitReply(rig, frames, proto::msg::ART_GET);
    QVERIFY(hit);
    QVERIFY(!body(*hit).has("data"));
    QCOMPARE(body(*hit).u("crc"), uint64_t(crc));
    rig.phone.send(proto::msg::ART_GET, artGet(one, 10));
    rig.server.poll();
    frames = rig.phone.frames();
    const fd::LinkFrame *small = awaitReply(rig, frames, proto::msg::ART_GET);
    QVERIFY(small);
    QCOMPARE(body(*small).u("w"), std::min<uint64_t>(uint64_t(proto::artMinPx), b.u("w")));
    rig.phone.send(proto::msg::ART_GET, artGet(999999, 256));
    rig.server.poll();
    frames = rig.phone.frames();
    QCOMPARE(errorCode(frames, proto::msg::ART_GET), uint64_t(proto::err::staleRev));
    rig.phone.send(proto::msg::ART_GET, artGet(rig.id(QStringLiteral("Single")), 256));
    rig.server.poll();
    frames = rig.phone.frames();
    QVERIFY(awaitReply(rig, frames, proto::msg::ART_GET));
    QCOMPARE(errorCode(frames, proto::msg::ART_GET), uint64_t(proto::err::notFound));
}

void LinkTest::artGetCoalescedAndCapped()
{
    LibRig rig;
    QVERIFY(rig.scan());
    rig.connect();
    const qint64 one = rig.id(QStringLiteral("One"));
    for (int px : {512, 512, 512, 300, 301, 302, 303})
        rig.phone.send(proto::msg::ART_GET, artGet(one, px));
    rig.server.poll();
    std::vector<fd::LinkFrame> replies;
    QElapsedTimer timer;
    timer.start();
    while (replies.size() < 7 && timer.elapsed() < waitMs) {
        for (const fd::LinkFrame &f : rig.phone.frames()) {
            if (f.type == proto::msg::ART_GET && (f.flags & proto::flagReply))
                replies.push_back(f);
        }
        QTest::qWait(10);
        rig.server.poll();
    }
    QCOMPARE(replies.size(), size_t(7));
    uint16_t last = 0;
    for (const fd::LinkFrame &f : replies)
        last = std::max(last, f.seq);
    int busy = 0;
    for (const fd::LinkFrame &f : replies) {
        if (!(f.flags & proto::flagError))
            continue;
        QCOMPARE(body(f).u("code"), uint64_t(proto::err::busy));
        QCOMPARE(f.seq, last);
        ++busy;
    }
    QCOMPARE(busy, 1);
}

void LinkTest::artEncode()
{
    QTemporaryDir dir;
    QImage wide(1200, 800, QImage::Format_RGB32);
    wide.fill(Qt::red);
    const QString path = dir.filePath(QStringLiteral("wide.png"));
    QVERIFY(wide.save(path));
    LinkJpeg jpeg;
    QVERIFY(linkEncodeArt(path, 512, jpeg));
    QCOMPARE(jpeg.side, 512);
    QImage out;
    QVERIFY(out.loadFromData(jpeg.bytes, "JPEG"));
    QCOMPARE(out.size(), QSize(512, 512));
    QCOMPARE(jpeg.crc, linkCrc32(jpeg.bytes));
    QVERIFY(linkEncodeArt(path, 300, jpeg));
    QCOMPARE(jpeg.side, 300);
    QImage tiny(40, 50, QImage::Format_RGB32);
    tiny.fill(Qt::blue);
    const QString small = dir.filePath(QStringLiteral("tiny.png"));
    QVERIFY(tiny.save(small));
    QVERIFY(linkEncodeArt(small, 512, jpeg));
    QCOMPARE(jpeg.side, 40);
    QVERIFY(!linkEncodeArt(dir.filePath(QStringLiteral("missing.png")), 256, jpeg));
    QVERIFY(!linkEncodeArt(QString(), 256, jpeg));
    QCOMPARE(linkCrc32(QByteArrayLiteral("123456789")), 0xcbf43926u);
}

fd::LinkWriter lrcPut(qint64 track, const QByteArray &text, std::optional<quint32> lib, std::optional<quint32> crc)
{
    fd::LinkWriter w;
    w.u64(proto::tag::LRC_PUT::track, uint64_t(track));
    if (lib)
        w.u32(proto::tag::LRC_PUT::libRev, *lib);
    w.bytes(proto::tag::LRC_PUT::text, text.constData(), size_t(text.size()));
    if (crc)
        w.u32(proto::tag::LRC_PUT::crc, *crc);
    return w;
}

void LinkTest::queueLyricsFlags()
{
    LibRig rig;
    QVERIFY(rig.scan());
    rig.connect();
    const quint32 lib = rig.server.libRev();
    const QByteArray timed = QByteArrayLiteral("[00:01.00]first\n");
    const QByteArray plain = QByteArrayLiteral("plain words\n");
    QVERIFY(okReply(rig.ask(proto::msg::LRC_PUT, lrcPut(rig.id(QStringLiteral("Two")), timed, lib, linkCrc32(timed))),
                    proto::msg::LRC_PUT));
    QVERIFY(okReply(rig.ask(proto::msg::LRC_PUT, lrcPut(rig.id(QStringLiteral("Three")), plain, lib, linkCrc32(plain))),
                    proto::msg::LRC_PUT));
    const QString four = rig.library.queueEntries({rig.id(QStringLiteral("Four"))}).constFirst().path;
    QVERIFY(QFile::rename(four, four + QStringLiteral(".gone")));
    QCOMPARE(rig.player.playIds({rig.id(QStringLiteral("One")), rig.id(QStringLiteral("Two")), rig.id(QStringLiteral("Three")),
                                 rig.id(QStringLiteral("Four"))},
                                QStringLiteral("play")),
             4);
    auto flags = [&rig] {
        fd::LinkWriter page;
        page.u8(proto::tag::QUEUE_OP::cmd, proto::val::QUEUE_CMD::page);
        QStringList out;
        for (const fd::LinkFrame &f : rig.ask(proto::msg::QUEUE_OP, page)) {
            if (f.type != proto::msg::QUEUE || (f.flags & proto::flagReply))
                continue;
            const fd::LinkBody b = body(f);
            for (const fd::LinkField *item : b.all("item")) {
                const fd::LinkBody ib{item->rec};
                out.append(!ib.has("lyrics") ? QStringLiteral("?")
                           : ib.u("lyrics")  ? QStringLiteral("synced")
                           : ib.u("lyricsPlain") ? QStringLiteral("plain")
                                                 : QStringLiteral("none"));
            }
        }
        return out;
    };
    QTRY_COMPARE_WITH_TIMEOUT(flags(), QStringList({QStringLiteral("none"), QStringLiteral("synced"), QStringLiteral("plain"),
                                                     QStringLiteral("?")}),
                              waitMs);
    QVERIFY(QFile::rename(four + QStringLiteral(".gone"), four));
    QTRY_COMPARE_WITH_TIMEOUT(flags().value(3), QStringLiteral("none"), waitMs);
    fd::LinkWriter del;
    del.u64(proto::tag::LRC_DEL::track, uint64_t(rig.id(QStringLiteral("Two"))));
    QVERIFY(okReply(rig.ask(proto::msg::LRC_DEL, del), proto::msg::LRC_DEL));
    QTRY_COMPARE_WITH_TIMEOUT(flags().value(1), QStringLiteral("none"), waitMs);
}

void LinkTest::lyricsLink()
{
    LibRig rig;
    Lyrics lyrics;
    rig.server.setLyrics(&lyrics);
    QVERIFY(rig.scan());
    rig.connect();
    const qint64 two = rig.id(QStringLiteral("Two"));
    const QByteArray timed = QByteArrayLiteral("[00:01.00]first\n[00:02.50]second\n");
    const quint32 lib = rig.server.libRev();
    std::vector<fd::LinkFrame> frames = rig.ask(proto::msg::LRC_PUT, lrcPut(two, timed, std::nullopt, linkCrc32(timed)));
    QCOMPARE(errorCode(frames, proto::msg::LRC_PUT), uint64_t(proto::err::badValue));
    frames = rig.ask(proto::msg::LRC_PUT, lrcPut(two, timed, lib, linkCrc32(timed) + 1));
    QCOMPARE(errorCode(frames, proto::msg::LRC_PUT), uint64_t(proto::err::crc));
    frames = rig.ask(proto::msg::LRC_PUT, lrcPut(two, timed, lib + 1, linkCrc32(timed)));
    QCOMPARE(errorCode(frames, proto::msg::LRC_PUT), uint64_t(proto::err::staleRev));
    const QByteArray big(proto::lrcMaxBytes + 1, 'x');
    frames = rig.ask(proto::msg::LRC_PUT, lrcPut(two, big, lib, linkCrc32(big)));
    QCOMPARE(errorCode(frames, proto::msg::LRC_PUT), uint64_t(proto::err::tooBig));
    frames = rig.ask(proto::msg::LRC_PUT, lrcPut(two, timed, lib, linkCrc32(timed)));
    QVERIFY(okReply(frames, proto::msg::LRC_PUT));
    QCOMPARE(errorCode(frames, proto::msg::LRC_PUT), 0u);
    const QByteArray plain = QByteArrayLiteral("plain words\n");
    frames = rig.ask(proto::msg::LRC_PUT, lrcPut(two, plain, lib, linkCrc32(plain)));
    QCOMPARE(errorCode(frames, proto::msg::LRC_PUT), uint64_t(proto::err::refused));

    fd::LinkWriter get;
    get.u64(proto::tag::LYR_GET::track, uint64_t(two));
    rig.phone.send(proto::msg::LYR_GET, get);
    rig.server.poll();
    frames = rig.phone.frames();
    const fd::LinkFrame *reply = awaitReply(rig, frames, proto::msg::LYR_GET);
    QVERIFY(reply && !(reply->flags & proto::flagError));
    fd::LinkBody b = body(*reply);
    QCOMPARE(b.u("kind"), uint64_t(proto::val::LYRICS_KIND::synced));
    QCOMPARE(b.u("total"), 2u);
    QCOMPARE(b.all("line").size(), size_t(2));
    const quint32 crc = quint32(b.u("crc"));
    fd::LinkWriter again;
    again.u64(proto::tag::LYR_GET::track, uint64_t(two)).u32(proto::tag::LYR_GET::have, crc);
    rig.phone.send(proto::msg::LYR_GET, again);
    rig.server.poll();
    frames = rig.phone.frames();
    reply = awaitReply(rig, frames, proto::msg::LYR_GET);
    QVERIFY(reply);
    QCOMPARE(body(*reply).all("line").size(), size_t(0));
    QCOMPARE(body(*reply).u("crc"), uint64_t(crc));

    fd::LinkWriter none;
    none.u64(proto::tag::LYR_GET::track, uint64_t(rig.id(QStringLiteral("Three"))));
    rig.phone.send(proto::msg::LYR_GET, none);
    rig.server.poll();
    frames = rig.phone.frames();
    QVERIFY(awaitReply(rig, frames, proto::msg::LYR_GET));
    QCOMPARE(errorCode(frames, proto::msg::LYR_GET), uint64_t(proto::err::notFound));
    rig.phone.send(proto::msg::LYR_GET, get);
    rig.phone.send(proto::msg::LYR_GET, get);
    rig.server.poll();
    std::vector<fd::LinkFrame> twice;
    QElapsedTimer timer;
    timer.start();
    while (twice.size() < 2 && timer.elapsed() < waitMs) {
        for (const fd::LinkFrame &f : rig.phone.frames()) {
            if (f.type == proto::msg::LYR_GET && (f.flags & proto::flagReply))
                twice.push_back(f);
        }
        QTest::qWait(10);
        rig.server.poll();
    }
    QCOMPARE(twice.size(), size_t(2));
    for (const fd::LinkFrame &f : twice) {
        QVERIFY(!(f.flags & proto::flagError));
        QCOMPARE(body(f).u("crc"), uint64_t(crc));
    }
    fd::LinkWriter gone;
    gone.u64(proto::tag::LYR_GET::track, 999999);
    QCOMPARE(errorCode(rig.ask(proto::msg::LYR_GET, gone), proto::msg::LYR_GET), uint64_t(proto::err::staleRev));

    QCOMPARE(rig.player.playIds({two}, QStringLiteral("play")), 1);
    const QueueEntry e = rig.player.queue()->entry(rig.player.queue()->currentIndex());
    lyrics.setTracks(e.path, QString());
    QTRY_VERIFY_WITH_TIMEOUT(!lyrics.pending(), waitMs);
    rig.server.poll();
    frames = rig.phone.frames();
    const fd::LinkFrame *state = nullptr;
    for (const fd::LinkFrame &f : frames) {
        if (f.type == proto::msg::STATE)
            state = &f;
    }
    QVERIFY(state);
    QCOMPARE(body(*state).u("lyrics"), 1u);
    QCOMPARE(body(*state).u("lyricsPlain"), 0u);
    QCOMPARE(body(*state).u("lyricsCrc"), uint64_t(crc));

    fd::LinkWriter del;
    del.u64(proto::tag::LRC_DEL::track, uint64_t(two));
    QVERIFY(okReply(rig.ask(proto::msg::LRC_DEL, del), proto::msg::LRC_DEL));
    QVERIFY(!QFileInfo::exists(Lyrics::storeFile(e.path)));
    QTRY_VERIFY_WITH_TIMEOUT(lyrics.current().empty() && !lyrics.pending(), waitMs);
    lyrics.waitForDone();
}

QByteArray squareJpeg(int side, Qt::GlobalColor color)
{
    QImage image(side, side, QImage::Format_RGB888);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", 90);
    return bytes;
}

ArtistStore::Head head(const QString &name, const QByteArray &jpeg, quint8 origin = 0, int side = 960)
{
    ArtistStore::Head h;
    h.name = name;
    h.total = quint32(jpeg.size());
    h.crc = linkCrc32(jpeg);
    h.w = side;
    h.h = side;
    h.origin = origin;
    return h;
}

void LinkTest::artistStore()
{
    QTemporaryDir dir;
    const QByteArray jpeg = squareJpeg(960, Qt::red);
    int w = 0;
    int h = 0;
    QVERIFY(ArtistStore::jpegFrame(jpeg, w, h));
    QCOMPARE(w, 960);
    {
        ArtistStore store(dir.path());
        quint32 offset = 0;
        quint32 crc = 0;
        QCOMPARE(store.begin(head(QStringLiteral("Artist A"), jpeg, 0, 640), offset, crc), ArtistStore::Code::BadValue);
        ArtistStore::Head big = head(QStringLiteral("Artist A"), jpeg);
        big.total = 300000;
        QCOMPARE(store.begin(big, offset, crc), ArtistStore::Code::TooBig);
        QCOMPARE(store.begin(head(QStringLiteral(" "), jpeg), offset, crc), ArtistStore::Code::BadValue);
        QCOMPARE(store.begin(head(QString(256, QLatin1Char('n')), jpeg), offset, crc), ArtistStore::Code::BadValue);
        QCOMPARE(store.begin(head(QStringLiteral("Artist A"), jpeg, ArtistStore::originPhone), offset, crc), ArtistStore::Code::Ok);
        QCOMPARE(offset, 0u);
        QCOMPARE(store.data(0, jpeg), ArtistStore::Code::Ok);
        QCOMPARE(store.end(linkCrc32(jpeg) + 1), ArtistStore::Code::Crc);
        QCOMPARE(store.begin(head(QStringLiteral("Artist A"), jpeg, ArtistStore::originPhone), offset, crc), ArtistStore::Code::Ok);
        QCOMPARE(store.data(0, jpeg), ArtistStore::Code::Ok);
        QCOMPARE(store.end(linkCrc32(jpeg)), ArtistStore::Code::Ok);
        QCOMPARE(store.listed().size(), 1);
        QCOMPARE(store.listed().constFirst().origin, ArtistStore::originPhone);
        QCOMPARE(store.photoArtist({QStringLiteral("Nobody"), QStringLiteral("ARTIST A ")}), QStringLiteral("ARTIST A "));
        QVERIFY(store.photoFor({QStringLiteral("artist a")}).isLocalFile());
        QVERIFY(QFileInfo::exists(store.photoFor({QStringLiteral("artist a")}).toLocalFile()));
        QCOMPARE(store.begin(head(QStringLiteral("Artist A"), jpeg, ArtistStore::originLibrary), offset, crc), ArtistStore::Code::Ok);
        QCOMPARE(offset, quint32(jpeg.size()));
        QCOMPARE(crc, linkCrc32(jpeg));
        QCOMPARE(store.listed().constFirst().origin, quint8(ArtistStore::originPhone | ArtistStore::originLibrary));
        const QByteArray blue = squareJpeg(960, Qt::blue);
        QCOMPARE(store.begin(head(QStringLiteral("Artist B"), blue, ArtistStore::originLibrary), offset, crc), ArtistStore::Code::Ok);
        QCOMPARE(store.data(0, blue), ArtistStore::Code::Ok);
        QCOMPARE(store.end(linkCrc32(blue)), ArtistStore::Code::Ok);
        {
            ArtistStore peek(dir.path());
            QCOMPARE(peek.origin(ArtistStore::key(QStringLiteral("Artist B"))), ArtistStore::originLibrary);
        }
        QVERIFY(!store.tag(QStringLiteral("Nobody"), ArtistStore::originPhone));
        QCOMPARE(store.photoFocus({QStringLiteral("Artist B")}), QPointF(0.5, 0.5));
        ArtistStore::Head focused = head(QStringLiteral("Artist A"), jpeg, ArtistStore::originPhone);
        focused.focusX = 120;
        focused.focusY = 800;
        QCOMPARE(store.begin(focused, offset, crc), ArtistStore::Code::Ok);
        QCOMPARE(offset, quint32(jpeg.size()));
        QCOMPARE(store.photoFocus({QStringLiteral("artist a")}), QPointF(0.12, 0.8));
        ArtistStore::Head wild = head(QStringLiteral("Artist B"), blue, ArtistStore::originLibrary);
        wild.focusX = 1500;
        wild.focusY = 10;
        QCOMPARE(store.begin(wild, offset, crc), ArtistStore::Code::Ok);
        QCOMPARE(store.photoFocus({QStringLiteral("Artist B")}), QPointF(0.5, 0.5));
        QVERIFY(store.flushOrigins());
    }
    {
        ArtistStore store(dir.path());
        QCOMPARE(store.listed().size(), 2);
        QCOMPARE(store.origin(ArtistStore::key(QStringLiteral("Artist B"))), ArtistStore::originLibrary);
        QCOMPARE(store.photoFocus({QStringLiteral("Artist A")}), QPointF(0.12, 0.8));
        QCOMPARE(store.prune({ArtistStore::key(QStringLiteral("Artist C"))}), 1);
        QCOMPARE(store.listed().size(), 1);
        QCOMPARE(store.listed().constFirst().name, QStringLiteral("Artist A"));
        QVERIFY(store.remove(QStringLiteral("artist a")));
        QVERIFY(store.listed().isEmpty());
        QVERIFY(store.photoFor({QStringLiteral("Artist A")}).isEmpty());
    }
}

void LinkTest::artistStoreResumeAndCap()
{
    QTemporaryDir dir;
    const QByteArray jpeg = squareJpeg(960, Qt::green);
    ArtistStore store(dir.path(), 960, 262144, 1000, qint64(jpeg.size()) * 2 + 10);
    quint32 offset = 0;
    quint32 crc = 0;
    QCOMPARE(store.begin(head(QStringLiteral("One"), jpeg), offset, crc), ArtistStore::Code::Ok);
    QCOMPARE(store.data(0, jpeg.left(2500)), ArtistStore::Code::Ok);
    QCOMPARE(store.begin(head(QStringLiteral("One"), jpeg), offset, crc), ArtistStore::Code::Ok);
    QCOMPARE(offset, 2000u);
    QCOMPARE(crc, linkCrc32(jpeg.left(2000)));
    QCOMPARE(store.data(1000, jpeg.mid(1000)), ArtistStore::Code::WriteError);
    QCOMPARE(store.data(2000, jpeg.mid(2000)), ArtistStore::Code::Ok);
    QCOMPARE(store.end(linkCrc32(jpeg)), ArtistStore::Code::Ok);
    for (const char *name : {"Two", "Three"}) {
        QTest::qWait(20);
        QCOMPARE(store.begin(head(QString::fromLatin1(name), jpeg), offset, crc), ArtistStore::Code::Ok);
        QCOMPARE(store.data(0, jpeg), ArtistStore::Code::Ok);
        QCOMPARE(store.end(linkCrc32(jpeg)), ArtistStore::Code::Ok);
    }
    QCOMPARE(store.listed().size(), 2);
    QVERIFY(store.photoFor({QStringLiteral("One")}).isEmpty());
    QVERIFY(!store.photoFor({QStringLiteral("Three")}).isEmpty());
}

void LinkTest::imagesOverTheLink()
{
    LibRig rig;
    QVERIFY(rig.scan());
    QTemporaryDir dir;
    ArtistStore store(dir.path());
    rig.server.setArtists(&store);
    rig.connect();
    const QByteArray jpeg = squareJpeg(960, Qt::red);
    fd::LinkWriter begin;
    namespace b = proto::tag::IMG_BEGIN;
    begin.u8(b::kind, proto::val::IMG_KIND::artist).str(b::name, "Artist A").u32(b::total, uint32_t(jpeg.size()))
        .u32(b::crc, linkCrc32(jpeg)).u16(b::w, 960).u16(b::h, 960).u8(b::origin, proto::val::IMG_ORIGIN::library)
        .u16(b::focusX, 250).u16(b::focusY, 300);
    std::vector<fd::LinkFrame> frames = rig.ask(proto::msg::IMG_BEGIN, begin);
    const fd::LinkFrame *reply = find(frames, proto::msg::IMG_BEGIN, true);
    QVERIFY(reply && !(reply->flags & proto::flagError));
    QCOMPARE(body(*reply).u("offset"), 0u);
    for (qsizetype at = 0; at < jpeg.size(); at += 16000) {
        fd::LinkWriter data;
        const QByteArray part = jpeg.mid(at, 16000);
        data.u32(proto::tag::IMG_DATA::offset, uint32_t(at)).bytes(proto::tag::IMG_DATA::data, part.constData(), size_t(part.size()));
        rig.phone.send(proto::msg::IMG_DATA, data, 0);
    }
    fd::LinkWriter end;
    end.u32(proto::tag::IMG_END::crc, linkCrc32(jpeg));
    frames = rig.ask(proto::msg::IMG_END, end);
    QVERIFY(okReply(frames, proto::msg::IMG_END));
    QCOMPARE(store.listed().size(), 1);
    QCOMPARE(store.photoFocus({QStringLiteral("Artist A")}), QPointF(0.25, 0.3));
    fd::LinkWriter list;
    frames = rig.ask(proto::msg::IMG_LIST, list);
    reply = find(frames, proto::msg::IMG_LIST, true);
    QVERIFY(reply);
    const fd::LinkBody lb = body(*reply);
    QCOMPARE(lb.u("total"), 1u);
    QCOMPARE(lb.all("entry").size(), size_t(1));
    QVERIFY(lb.has("cap"));
    fd::LinkWriter tag;
    tag.str(proto::tag::IMG_TAG::name, "Nobody").u8(proto::tag::IMG_TAG::origin, proto::val::IMG_ORIGIN::phone);
    QCOMPARE(errorCode(rig.ask(proto::msg::IMG_TAG, tag), proto::msg::IMG_TAG), uint64_t(proto::err::notFound));
    QVERIFY(QDir(rig.music + QStringLiteral("/Album One")).removeRecursively());
    QVERIFY(rig.scan());
    rig.server.prune();
    QVERIFY(store.listed().isEmpty());
    fd::LinkWriter del;
    del.str(proto::tag::IMG_DEL::name, "Artist A");
    QVERIFY(okReply(rig.ask(proto::msg::IMG_DEL, del), proto::msg::IMG_DEL));
    fd::LinkWriter badBegin;
    badBegin.u8(b::kind, proto::val::IMG_KIND::artist).str(b::name, "X").u32(b::total, 10).u32(b::crc, 1).u16(b::w, 640).u16(b::h, 960);
    QCOMPARE(errorCode(rig.ask(proto::msg::IMG_BEGIN, badBegin), proto::msg::IMG_BEGIN), uint64_t(proto::err::badValue));
}

void LinkTest::artistAsks()
{
    Rig rig;
    Phone phone;
    rig.server.open(phone.dev, phoneA);
    phone.dev = -1;
    phone.hello(uint16_t(proto::major), uint64_t(1) << proto::feature::artPick | uint64_t(1) << proto::feature::artBlock);
    rig.server.poll();
    phone.frames();
    QVERIFY(rig.server.canAsk(QStringLiteral("pick")));
    QSignalSpy done(&rig.server, &LinkServer::askDone);
    QVERIFY(rig.server.artPick(QStringLiteral("Artist A"), QString()));
    rig.server.poll();
    std::vector<fd::LinkFrame> frames = phone.frames();
    const fd::LinkFrame *pick = find(frames, proto::msg::ART_PICK);
    QVERIFY(pick && (pick->flags & proto::flagWant));
    QCOMPARE(QString::fromStdString(body(*pick).str("name")), QStringLiteral("Artist A"));
    QVERIFY(!rig.server.artBlock(false, QStringLiteral("Artist A"), QString(), 1));
    QCOMPARE(done.count(), 1);
    QCOMPARE(done.at(0).at(1).toString(), QStringLiteral("busy"));
    fd::LinkWriter shown;
    shown.flag(proto::tag::ART_PICK_reply::shown, true);
    std::vector<uint8_t> bytes = fd::link_frame(proto::msg::ART_PICK, proto::flagReply, pick->seq, shown.data());
    ::send(phone.app, bytes.data(), bytes.size(), MSG_NOSIGNAL);
    rig.server.poll();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, waitMs);
    QCOMPARE(done.at(1).at(0).toString(), QStringLiteral("pick"));
    QCOMPARE(done.at(1).at(1).toString(), QStringLiteral("ok"));
    QVERIFY(rig.server.artBlock(true, QStringLiteral("Artist A"), QString(), 7));
    rig.server.poll();
    frames = phone.frames();
    const fd::LinkFrame *block = find(frames, proto::msg::ART_BLOCK);
    QVERIFY(block);
    QCOMPARE(body(*block).u("cmd"), uint64_t(proto::val::BLOCK_CMD::wrongArtist));
    fd::LinkWriter err;
    err.u8(proto::tag::error::code, proto::err::staleRev).str(proto::tag::error::text, "stale rev");
    bytes = fd::link_frame(proto::msg::ART_BLOCK, proto::flagReply | proto::flagError, block->seq, err.data());
    ::send(phone.app, bytes.data(), bytes.size(), MSG_NOSIGNAL);
    rig.server.poll();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 3, waitMs);
    QCOMPARE(done.at(2).at(0).toString(), QStringLiteral("wrong"));
    QCOMPARE(done.at(2).at(1).toString(), QStringLiteral("error 7"));
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
