#include "PhoneArt.h"
#include "SystemBluezModel.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace microdap;

namespace {

constexpr int busTimeoutMs = 5000;

QString propertiesInterface()
{
    return QStringLiteral("org.freedesktop.DBus.Properties");
}

QString objectManagerInterface()
{
    return QStringLiteral("org.freedesktop.DBus.ObjectManager");
}

QString address()
{
    return QStringLiteral("AA:BB:CC:DD:EE:FF");
}

QString devicePath()
{
    return QStringLiteral("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF");
}

QString transportPath()
{
    return devicePath() + QStringLiteral("/fd0");
}

QString playerPath()
{
    return devicePath() + QStringLiteral("/player0");
}

QVariantMap trackMap(const QString &title, int number, const QString &handle)
{
    QVariantMap track;
    track.insert(QStringLiteral("Title"), title);
    track.insert(QStringLiteral("Artist"), QStringLiteral("Phone Artist"));
    track.insert(QStringLiteral("Album"), QStringLiteral("Phone Album"));
    track.insert(QStringLiteral("TrackNumber"), QVariant::fromValue(static_cast<uint>(number)));
    track.insert(QStringLiteral("NumberOfTracks"), QVariant::fromValue(static_cast<uint>(11)));
    if (!handle.isEmpty())
        track.insert(QStringLiteral("ImgHandle"), handle);
    return track;
}

}

class FakeObex;

class FakeTransfer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.obex.Transfer1")

public:
    explicit FakeTransfer(QStringList *calls)
        : m_calls(calls)
    {
    }

public slots:
    void Cancel() { m_calls->append(QStringLiteral("Cancel")); }

private:
    QStringList *m_calls;
};

class FakeObexSession : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.obex.Image1")

public:
    FakeObexSession(FakeObex *owner, const QString &path)
        : m_owner(owner)
        , m_path(path)
    {
    }

    QString path() const { return m_path; }

public slots:
    QDBusObjectPath GetThumbnail(const QString &file, const QString &handle, QVariantMap &properties);

private:
    FakeObex *m_owner;
    QString m_path;
    int m_transfers = 0;
};

class FakeObex : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.obex.Client1")

public:
    explicit FakeObex(const QDBusConnection &bus)
        : m_bus(bus)
    {
    }

    ~FakeObex() override
    {
        for (FakeObexSession *session : std::as_const(m_sessions)) {
            m_bus.unregisterObject(session->path());
            delete session;
        }
        m_bus.unregisterObject(PhoneArt::clientPath());
    }

    bool start()
    {
        return m_bus.registerService(PhoneArt::obexService())
               && m_bus.registerObject(PhoneArt::clientPath(), this, QDBusConnection::ExportAllSlots);
    }

    void drop(const QString &path)
    {
        for (int i = 0; i < m_sessions.size(); ++i) {
            if (m_sessions.at(i)->path() != path)
                continue;
            m_bus.unregisterObject(path);
            delete m_sessions.takeAt(i);
            QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/"), objectManagerInterface(),
                                                              QStringLiteral("InterfacesRemoved"));
            signal << QVariant::fromValue(QDBusObjectPath(path))
                   << QVariant(QStringList{PhoneArt::imageInterface()});
            m_bus.send(signal);
            return;
        }
    }

    void transferStatus(const QString &path, const QString &status)
    {
        QVariantMap changed;
        changed.insert(QStringLiteral("Status"), status);
        QDBusMessage signal = QDBusMessage::createSignal(path, propertiesInterface(),
                                                          QStringLiteral("PropertiesChanged"));
        signal << PhoneArt::transferInterface() << QVariant(changed) << QVariant(QStringList());
        m_bus.send(signal);
    }

    QStringList openSessions() const
    {
        QStringList paths;
        for (const FakeObexSession *session : m_sessions)
            paths.append(session->path());
        return paths;
    }

    QDBusConnection m_bus;
    QStringList calls;
    QVariantMap lastOptions;
    int refuse = 0;
    QMap<QString, QString> modes;
    int completeDelayMs = 20;

public slots:
    QDBusObjectPath CreateSession(const QString &destination, const QVariantMap &options)
    {
        calls.append(QStringLiteral("CreateSession ") + destination);
        lastOptions = options;
        if (refuse > 0) {
            --refuse;
            sendErrorReply(QStringLiteral("org.bluez.obex.Error.Failed"), QStringLiteral("refused"));
            return {};
        }
        const QString path = QStringLiteral("/org/bluez/obex/client/session%1").arg(++m_count);
        auto *session = new FakeObexSession(this, path);
        if (!m_bus.registerObject(path, session, QDBusConnection::ExportAllSlots)) {
            delete session;
            sendErrorReply(QStringLiteral("org.bluez.obex.Error.Failed"), QStringLiteral("register"));
            return {};
        }
        m_sessions.append(session);
        return QDBusObjectPath(path);
    }

    void RemoveSession(const QDBusObjectPath &path)
    {
        calls.append(QStringLiteral("RemoveSession"));
        drop(path.path());
    }

private:
    QList<FakeObexSession *> m_sessions;
    int m_count = 0;
};

QDBusObjectPath FakeObexSession::GetThumbnail(const QString &file, const QString &handle,
                                              QVariantMap &properties)
{
    m_owner->calls.append(QStringLiteral("GetThumbnail ") + handle);
    const QString mode = m_owner->modes.value(handle, QStringLiteral("ok"));
    if (mode == QLatin1StringView("fail")) {
        sendErrorReply(QStringLiteral("org.bluez.obex.Error.Failed"), QStringLiteral("not found"));
        return {};
    }
    const QString transfer = m_path + QStringLiteral("/transfer%1").arg(++m_transfers);
    properties.insert(QStringLiteral("Status"), QStringLiteral("queued"));
    if (mode == QLatin1StringView("ok")) {
        QFile out(file);
        if (out.open(QIODevice::WriteOnly))
            out.write(QByteArray("\xff\xd8\xff\xe0 thumbnail ") + handle.toUtf8());
    }
    if (mode == QLatin1StringView("stall")) {
        auto *object = new FakeTransfer(&m_owner->calls);
        object->setParent(m_owner);
        m_owner->m_bus.registerObject(transfer, object, QDBusConnection::ExportAllSlots);
        return QDBusObjectPath(transfer);
    }
    const QString status = mode == QLatin1StringView("error") ? QStringLiteral("error")
                                                              : QStringLiteral("complete");
    FakeObex *owner = m_owner;
    QTimer::singleShot(owner->completeDelayMs, owner, [owner, transfer, status] {
        owner->transferStatus(transfer, status);
    });
    return QDBusObjectPath(transfer);
}

class FakeObject : public QObject
{
    Q_OBJECT

public:
    FakeObject(const QDBusConnection &bus, const QString &path, const InterfaceList &properties)
        : m_bus(bus)
        , m_path(path)
        , m_properties(properties)
    {
    }

    InterfaceList interfaces() const { return m_properties; }

    void set(const QString &interface, const QString &name, const QVariant &value)
    {
        m_properties[interface].insert(name, value);
        QVariantMap changed;
        changed.insert(name, value);
        QDBusMessage signal = QDBusMessage::createSignal(m_path, propertiesInterface(),
                                                          QStringLiteral("PropertiesChanged"));
        signal << interface << QVariant(changed) << QVariant(QStringList());
        m_bus.send(signal);
    }

    QStringList calls;

private:
    QDBusConnection m_bus;
    QString m_path;
    InterfaceList m_properties;
};

class PlayerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.MediaPlayer1")

public:
    explicit PlayerAdaptor(FakeObject *object)
        : QDBusAbstractAdaptor(object)
        , m_object(object)
    {
    }

public slots:
    void Play() { m_object->calls.append(QStringLiteral("Play")); }
    void Pause() { m_object->calls.append(QStringLiteral("Pause")); }
    void Next() { m_object->calls.append(QStringLiteral("Next")); }
    void Previous() { m_object->calls.append(QStringLiteral("Previous")); }

private:
    FakeObject *m_object;
};

class FakeBluez : public QObject
{
    Q_OBJECT

public:
    explicit FakeBluez(const QDBusConnection &bus)
        : m_bus(bus)
    {
    }

    ~FakeBluez() override
    {
        for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
            m_bus.unregisterObject(it.key());
            delete it.value();
        }
    }

    FakeObject *add(const QString &path, const InterfaceList &properties)
    {
        auto *object = new FakeObject(m_bus, path, properties);
        if (properties.contains(QStringLiteral("org.bluez.MediaPlayer1")))
            new PlayerAdaptor(object);
        m_bus.registerObject(path, object, QDBusConnection::ExportAdaptors);
        m_objects.insert(path, object);
        QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/"), objectManagerInterface(),
                                                          QStringLiteral("InterfacesAdded"));
        signal << QVariant::fromValue(QDBusObjectPath(path)) << QVariant::fromValue(properties);
        m_bus.send(signal);
        return object;
    }

    void remove(const QString &path)
    {
        FakeObject *object = m_objects.take(path);
        if (!object)
            return;
        const QStringList interfaces = object->interfaces().keys();
        m_bus.unregisterObject(path);
        delete object;
        QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/"), objectManagerInterface(),
                                                          QStringLiteral("InterfacesRemoved"));
        signal << QVariant::fromValue(QDBusObjectPath(path)) << QVariant(interfaces);
        m_bus.send(signal);
    }

private:
    QDBusConnection m_bus;
    QMap<QString, FakeObject *> m_objects;
};

class PhoneArtTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void sessionAndThumbnail();
    void cacheAndEviction();
    void failedHandlesNotAskedAgain();
    void handleChangeMidFetch();
    void stalledFetchCancelled();
    void badHandlesIgnored();
    void refusedSessionsGiveUp();
    void emptyTargetClosesSession();
    void sessionDroppedReopens();
    void obexGoneReopens();
    void modelShowsArt();
    void modelGatesStreamStart();
    void modelNudgesFirstTrack();
    void modelNoNudgeAfterPlayingOpen();

private:
    QTemporaryDir m_dir;
    std::unique_ptr<FakeObex> m_obex;
};

void PhoneArtTest::initTestCase()
{
    if (!QDBusConnection::sessionBus().isConnected())
        QSKIP("no session bus, run under dbus-run-session");
    QVERIFY(m_dir.isValid());
    qputenv("MICRODAP_BT_ART_DIR", m_dir.filePath(QStringLiteral("art")).toUtf8());
    qputenv("MICRODAP_BT_ART_RETRY_MS", "50,50");
    qputenv("MICRODAP_BT_ART_FETCH_MS", "1000");
    SystemBluezModel::registerMetaTypes();
}

void PhoneArtTest::init()
{
    m_obex = std::make_unique<FakeObex>(QDBusConnection::sessionBus());
    QVERIFY(m_obex->start());
}

void PhoneArtTest::cleanup()
{
    m_obex.reset();
    QDBusConnection::sessionBus().unregisterService(PhoneArt::obexService());
}

void PhoneArtTest::sessionAndThumbnail()
{
    PhoneArt art(QDBusConnection::sessionBus());
    QSignalSpy artSpy(&art, &PhoneArt::artChanged);
    art.setTarget(address(), 0x1001, QStringLiteral("1000001"));
    QTRY_VERIFY_WITH_TIMEOUT(!art.artPath(QStringLiteral("1000001")).isEmpty(), busTimeoutMs);
    QVERIFY(art.sessionOpen());
    QCOMPARE(m_obex->calls, QStringList({QStringLiteral("CreateSession AA:BB:CC:DD:EE:FF"),
                                         QStringLiteral("GetThumbnail 1000001")}));
    QCOMPARE(m_obex->lastOptions.value(QStringLiteral("Target")).toString(), QStringLiteral("bip-avrcp"));
    const QVariant psm = m_obex->lastOptions.value(QStringLiteral("PSM"));
    QCOMPARE(psm.metaType(), QMetaType::fromType<ushort>());
    QCOMPARE(psm.toInt(), 0x1001);
    const QString path = art.artPath(QStringLiteral("1000001"));
    QVERIFY(path.startsWith(art.directory()));
    QVERIFY(QFileInfo(path).size() > 0);
    QCOMPARE(artSpy.count(), 1);
}

void PhoneArtTest::cacheAndEviction()
{
    PhoneArt art(QDBusConnection::sessionBus());
    QStringList handles;
    for (int i = 1; i <= PhoneArt::cacheSize + 1; ++i)
        handles.append(QStringLiteral("100000%1").arg(i));
    art.setTarget(address(), 0x1001, handles.at(0));
    QTRY_VERIFY_WITH_TIMEOUT(!art.artPath(handles.at(0)).isEmpty(), busTimeoutMs);
    const QString first = art.artPath(handles.at(0));
    art.setTarget(address(), 0x1001, handles.at(1));
    QTRY_VERIFY_WITH_TIMEOUT(!art.artPath(handles.at(1)).isEmpty(), busTimeoutMs);
    art.setTarget(address(), 0x1001, handles.at(0));
    QTest::qWait(100);
    QCOMPARE(m_obex->calls.count(QStringLiteral("GetThumbnail ") + handles.at(0)), 1);

    const QString second = art.artPath(handles.at(1));
    for (int i = 2; i < handles.size(); ++i) {
        art.setTarget(address(), 0x1001, handles.at(i));
        QTRY_VERIFY_WITH_TIMEOUT(!art.artPath(handles.at(i)).isEmpty(), busTimeoutMs);
    }
    QVERIFY(art.artPath(handles.at(1)).isEmpty());
    QVERIFY(!QFile::exists(second));
    QCOMPARE(art.artPath(handles.at(0)), first);
    QVERIFY(QFile::exists(first));
}

void PhoneArtTest::stalledFetchCancelled()
{
    m_obex->modes.insert(QStringLiteral("8000001"), QStringLiteral("stall"));
    PhoneArt art(QDBusConnection::sessionBus());
    QElapsedTimer timer;
    timer.start();
    art.setTarget(address(), 0x1001, QStringLiteral("8000001"));
    QTRY_VERIFY_WITH_TIMEOUT(m_obex->calls.contains(QStringLiteral("Cancel")), busTimeoutMs);
    QVERIFY(timer.elapsed() >= 900);
    QVERIFY(art.artPath(QStringLiteral("8000001")).isEmpty());
    art.setTarget(address(), 0x1001, QStringLiteral("8000002"));
    QTRY_VERIFY_WITH_TIMEOUT(!art.artPath(QStringLiteral("8000002")).isEmpty(), busTimeoutMs);
    art.setTarget(address(), 0x1001, QStringLiteral("8000001"));
    QTest::qWait(100);
    QCOMPARE(m_obex->calls.count(QStringLiteral("GetThumbnail 8000001")), 1);
    QVERIFY(art.sessionOpen());
}

void PhoneArtTest::badHandlesIgnored()
{
    PhoneArt art(QDBusConnection::sessionBus());
    for (const QString &handle : {QStringLiteral("../../x"), QStringLiteral("123456"), QStringLiteral("12345678"),
                                  QStringLiteral("123456a")})
        art.setTarget(address(), 0x1001, handle);
    QTRY_VERIFY_WITH_TIMEOUT(art.sessionOpen(), busTimeoutMs);
    QTest::qWait(100);
    QCOMPARE(m_obex->calls, QStringList{QStringLiteral("CreateSession AA:BB:CC:DD:EE:FF")});
}

void PhoneArtTest::failedHandlesNotAskedAgain()
{
    m_obex->modes.insert(QStringLiteral("2000001"), QStringLiteral("fail"));
    m_obex->modes.insert(QStringLiteral("2000002"), QStringLiteral("error"));
    m_obex->modes.insert(QStringLiteral("2000003"), QStringLiteral("empty"));
    PhoneArt art(QDBusConnection::sessionBus());
    const QStringList handles{QStringLiteral("2000001"), QStringLiteral("2000002"), QStringLiteral("2000003")};
    for (const QString &handle : handles) {
        art.setTarget(address(), 0x1001, handle);
        QTRY_VERIFY_WITH_TIMEOUT(m_obex->calls.contains(QStringLiteral("GetThumbnail ") + handle), busTimeoutMs);
        QTest::qWait(100);
        QVERIFY(art.artPath(handle).isEmpty());
    }
    for (const QString &handle : handles)
        art.setTarget(address(), 0x1001, handle);
    QTest::qWait(100);
    for (const QString &handle : handles)
        QCOMPARE(m_obex->calls.count(QStringLiteral("GetThumbnail ") + handle), 1);
    QVERIFY(art.sessionOpen());
}

void PhoneArtTest::handleChangeMidFetch()
{
    m_obex->completeDelayMs = 300;
    PhoneArt art(QDBusConnection::sessionBus());
    art.setTarget(address(), 0x1001, QStringLiteral("3000001"));
    QTRY_VERIFY_WITH_TIMEOUT(m_obex->calls.contains(QStringLiteral("GetThumbnail 3000001")), busTimeoutMs);
    art.setTarget(address(), 0x1001, QStringLiteral("3000002"));
    QTest::qWait(100);
    QVERIFY(!m_obex->calls.contains(QStringLiteral("GetThumbnail 3000002")));
    QTRY_VERIFY_WITH_TIMEOUT(!art.artPath(QStringLiteral("3000002")).isEmpty(), busTimeoutMs);
    QVERIFY(!art.artPath(QStringLiteral("3000001")).isEmpty());
}

void PhoneArtTest::refusedSessionsGiveUp()
{
    m_obex->refuse = 10;
    PhoneArt art(QDBusConnection::sessionBus());
    art.setTarget(address(), 0x1001, QStringLiteral("4000001"));
    QTRY_VERIFY_WITH_TIMEOUT(art.gaveUp(), busTimeoutMs);
    QCOMPARE(m_obex->calls.count(QStringLiteral("CreateSession AA:BB:CC:DD:EE:FF")), 3);
    QTest::qWait(200);
    QCOMPARE(m_obex->calls.count(QStringLiteral("CreateSession AA:BB:CC:DD:EE:FF")), 3);

    m_obex->refuse = 0;
    art.setTarget(address(), 0x1007, QStringLiteral("4000001"));
    QTRY_VERIFY_WITH_TIMEOUT(!art.artPath(QStringLiteral("4000001")).isEmpty(), busTimeoutMs);
    QVERIFY(!art.gaveUp());
    QCOMPARE(m_obex->lastOptions.value(QStringLiteral("PSM")).toInt(), 0x1007);
}

void PhoneArtTest::emptyTargetClosesSession()
{
    PhoneArt art(QDBusConnection::sessionBus());
    QSignalSpy artSpy(&art, &PhoneArt::artChanged);
    art.setTarget(address(), 0x1001, QStringLiteral("5000001"));
    QTRY_VERIFY_WITH_TIMEOUT(!art.artPath(QStringLiteral("5000001")).isEmpty(), busTimeoutMs);
    const QString file = art.artPath(QStringLiteral("5000001"));
    art.setTarget({}, 0, {});
    QVERIFY(!art.sessionOpen());
    QVERIFY(art.artPath(QStringLiteral("5000001")).isEmpty());
    QVERIFY(!QFile::exists(file));
    QCOMPARE(artSpy.count(), 2);
    QTRY_VERIFY_WITH_TIMEOUT(m_obex->calls.contains(QStringLiteral("RemoveSession")), busTimeoutMs);
    QVERIFY(m_obex->openSessions().isEmpty());

    m_obex->calls.clear();
    art.setTarget(address(), 0, QStringLiteral("5000001"));
    QTest::qWait(100);
    QVERIFY(m_obex->calls.isEmpty());
}

void PhoneArtTest::sessionDroppedReopens()
{
    PhoneArt art(QDBusConnection::sessionBus());
    art.setTarget(address(), 0x1001, QStringLiteral("6000001"));
    QTRY_VERIFY_WITH_TIMEOUT(!art.artPath(QStringLiteral("6000001")).isEmpty(), busTimeoutMs);
    const QString first = art.sessionPath();
    m_obex->drop(first);
    QTRY_VERIFY_WITH_TIMEOUT(!art.sessionOpen(), busTimeoutMs);
    QVERIFY(art.artPath(QStringLiteral("6000001")).isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(art.sessionOpen(), busTimeoutMs);
    QVERIFY(art.sessionPath() != first);
    QTRY_VERIFY_WITH_TIMEOUT(!art.artPath(QStringLiteral("6000001")).isEmpty(), busTimeoutMs);
    QCOMPARE(m_obex->calls.count(QStringLiteral("GetThumbnail 6000001")), 2);
}

void PhoneArtTest::obexGoneReopens()
{
    PhoneArt art(QDBusConnection::sessionBus());
    art.setTarget(address(), 0x1001, QStringLiteral("7000001"));
    QTRY_VERIFY_WITH_TIMEOUT(!art.artPath(QStringLiteral("7000001")).isEmpty(), busTimeoutMs);
    m_obex.reset();
    QDBusConnection::sessionBus().unregisterService(PhoneArt::obexService());
    QTRY_VERIFY_WITH_TIMEOUT(!art.sessionOpen(), busTimeoutMs);
    m_obex = std::make_unique<FakeObex>(QDBusConnection::sessionBus());
    QVERIFY(m_obex->start());
    QTRY_VERIFY_WITH_TIMEOUT(!art.artPath(QStringLiteral("7000001")).isEmpty(), busTimeoutMs);
}

namespace {

struct Phone
{
    std::unique_ptr<FakeBluez> bluez;
    FakeObject *device = nullptr;
    FakeObject *transport = nullptr;
    FakeObject *player = nullptr;
};

Phone addPhone(const QVariantMap &track, const QString &status, bool active)
{
    Phone phone;
    phone.bluez = std::make_unique<FakeBluez>(QDBusConnection::sessionBus());
    QVariantMap device;
    device.insert(QStringLiteral("Address"), address());
    device.insert(QStringLiteral("Alias"), QStringLiteral("Pixel 8"));
    device.insert(QStringLiteral("Connected"), true);
    device.insert(QStringLiteral("UUIDs"), QStringList{SystemBluezModel::a2dpSourceUuid()});
    phone.device = phone.bluez->add(devicePath(), {{QStringLiteral("org.bluez.Device1"), device}});
    QVariantMap transport;
    transport.insert(QStringLiteral("State"), active ? QStringLiteral("active") : QStringLiteral("idle"));
    transport.insert(QStringLiteral("Device"), QVariant::fromValue(QDBusObjectPath(devicePath())));
    phone.transport = phone.bluez->add(transportPath(), {{QStringLiteral("org.bluez.MediaTransport1"), transport}});
    QVariantMap player;
    player.insert(QStringLiteral("Track"), track);
    player.insert(QStringLiteral("Status"), status);
    player.insert(QStringLiteral("ObexPort"), QVariant::fromValue(static_cast<ushort>(0x1001)));
    player.insert(QStringLiteral("Device"), QVariant::fromValue(QDBusObjectPath(devicePath())));
    phone.player = phone.bluez->add(playerPath(), {{QStringLiteral("org.bluez.MediaPlayer1"), player}});
    return phone;
}

}

void PhoneArtTest::modelShowsArt()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerService(SystemBluezModel::bluezService()));
    SystemBluezModel model(bus, bus);
    QTRY_VERIFY_WITH_TIMEOUT(model.ready(), busTimeoutMs);
    Phone phone = addPhone(trackMap(QStringLiteral("Track Four"), 4, QStringLiteral("1000001")),
                           QStringLiteral("paused"), false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.artUrl().isEmpty(), busTimeoutMs);
    QVERIFY(QFileInfo(QUrl(model.artUrl()).toLocalFile()).size() > 0);
    QCOMPARE(model.phoneArt()->sessionPath().isEmpty(), false);

    phone.player->set(QStringLiteral("org.bluez.MediaPlayer1"), QStringLiteral("Track"),
                      trackMap(QStringLiteral("Track Five"), 5, {}));
    QTRY_VERIFY_WITH_TIMEOUT(model.artUrl().isEmpty(), busTimeoutMs);
    QCOMPARE(model.title(), QStringLiteral("Track Five"));

    m_obex->calls.clear();
    phone.player->set(QStringLiteral("org.bluez.MediaPlayer1"), QStringLiteral("Track"),
                      trackMap(QStringLiteral("Track Four"), 4, QStringLiteral("1000001")));
    QTRY_VERIFY_WITH_TIMEOUT(!model.artUrl().isEmpty(), busTimeoutMs);
    QVERIFY(!m_obex->calls.contains(QStringLiteral("GetThumbnail 1000001")));

    phone.device->set(QStringLiteral("org.bluez.Device1"), QStringLiteral("Connected"), false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.connected(), busTimeoutMs);
    QVERIFY(model.artUrl().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(m_obex->calls.contains(QStringLiteral("RemoveSession")), busTimeoutMs);
    phone.bluez.reset();
    bus.unregisterService(SystemBluezModel::bluezService());
}

void PhoneArtTest::modelGatesStreamStart()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerService(SystemBluezModel::bluezService()));
    SystemBluezModel model(bus, bus);
    QTRY_VERIFY_WITH_TIMEOUT(model.ready(), busTimeoutMs);
    Phone phone = addPhone(trackMap(QStringLiteral("Track Four"), 4, QStringLiteral("1000001")),
                           QStringLiteral("playing"), false);
    QTRY_VERIFY_WITH_TIMEOUT(model.phoneArt()->sessionOpen(), busTimeoutMs);
    QTest::qWait(200);
    QVERIFY(!m_obex->calls.contains(QStringLiteral("GetThumbnail 1000001")));

    QElapsedTimer started;
    phone.transport->set(QStringLiteral("org.bluez.MediaTransport1"), QStringLiteral("State"),
                         QStringLiteral("active"));
    QTRY_VERIFY_WITH_TIMEOUT(model.transportActive(), busTimeoutMs);
    started.start();
    QTRY_VERIFY_WITH_TIMEOUT(m_obex->calls.contains(QStringLiteral("GetThumbnail 1000001")), busTimeoutMs);
    QVERIFY(started.elapsed() >= 900);
    QTRY_VERIFY_WITH_TIMEOUT(!model.artUrl().isEmpty(), busTimeoutMs);

    phone.player->set(QStringLiteral("org.bluez.MediaPlayer1"), QStringLiteral("Track"),
                      trackMap(QStringLiteral("Track Five"), 5, QStringLiteral("1000002")));
    QTRY_VERIFY_WITH_TIMEOUT(m_obex->calls.contains(QStringLiteral("GetThumbnail 1000002")), busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(!model.artUrl().isEmpty(), busTimeoutMs);

    phone.transport->set(QStringLiteral("org.bluez.MediaTransport1"), QStringLiteral("State"),
                         QStringLiteral("idle"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.transportActive(), busTimeoutMs);
    m_obex->drop(model.phoneArt()->sessionPath());
    phone.transport->set(QStringLiteral("org.bluez.MediaTransport1"), QStringLiteral("State"),
                         QStringLiteral("active"));
    QTRY_VERIFY_WITH_TIMEOUT(model.transportActive(), busTimeoutMs);
    started.restart();
    QTRY_VERIFY_WITH_TIMEOUT(model.phoneArt()->sessionOpen(), busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(m_obex->calls.count(QStringLiteral("GetThumbnail 1000002")), 2, busTimeoutMs);
    QVERIFY(started.elapsed() >= 900);
    QCOMPARE(phone.player->calls, QStringList());
    phone.bluez.reset();
    bus.unregisterService(SystemBluezModel::bluezService());
}

void PhoneArtTest::modelNudgesFirstTrack()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerService(SystemBluezModel::bluezService()));
    SystemBluezModel model(bus, bus);
    QTRY_VERIFY_WITH_TIMEOUT(model.ready(), busTimeoutMs);
    Phone phone = addPhone(trackMap(QStringLiteral("Track Four"), 4, {}), QStringLiteral("paused"), false);
    QTRY_COMPARE_WITH_TIMEOUT(phone.player->calls, QStringList{QStringLiteral("Next")}, busTimeoutMs);

    phone.player->set(QStringLiteral("org.bluez.MediaPlayer1"), QStringLiteral("Track"),
                      trackMap(QStringLiteral("Track Five"), 5, QStringLiteral("1000005")));
    QTRY_COMPARE_WITH_TIMEOUT(phone.player->calls,
                              QStringList({QStringLiteral("Next"), QStringLiteral("Previous")}), busTimeoutMs);

    phone.player->set(QStringLiteral("org.bluez.MediaPlayer1"), QStringLiteral("Status"),
                      QStringLiteral("playing"));
    phone.player->set(QStringLiteral("org.bluez.MediaPlayer1"), QStringLiteral("Track"),
                      trackMap(QStringLiteral("Track Four"), 4, QStringLiteral("1000004")));
    QTRY_COMPARE_WITH_TIMEOUT(phone.player->calls,
                              QStringList({QStringLiteral("Next"), QStringLiteral("Previous"),
                                           QStringLiteral("Pause")}),
                              busTimeoutMs);
    phone.player->set(QStringLiteral("org.bluez.MediaPlayer1"), QStringLiteral("Status"),
                      QStringLiteral("paused"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.artUrl().isEmpty(), busTimeoutMs);

    phone.player->set(QStringLiteral("org.bluez.MediaPlayer1"), QStringLiteral("Track"),
                      trackMap(QStringLiteral("Track Six"), 6, {}));
    QTest::qWait(300);
    QCOMPARE(phone.player->calls.size(), 3);

    phone.device->set(QStringLiteral("org.bluez.Device1"), QStringLiteral("Connected"), false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.connected(), busTimeoutMs);
    phone.player->calls.clear();
    phone.device->set(QStringLiteral("org.bluez.Device1"), QStringLiteral("Connected"), true);
    QTRY_COMPARE_WITH_TIMEOUT(phone.player->calls, QStringList{QStringLiteral("Next")}, busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(m_obex->calls.count(QStringLiteral("CreateSession AA:BB:CC:DD:EE:FF")) == 2,
                             busTimeoutMs);

    phone.player->set(QStringLiteral("org.bluez.MediaPlayer1"), QStringLiteral("Status"),
                      QStringLiteral("playing"));
    QElapsedTimer waited;
    waited.start();
    QTRY_COMPARE_WITH_TIMEOUT(phone.player->calls,
                              QStringList({QStringLiteral("Next"), QStringLiteral("Pause")}), busTimeoutMs);
    QVERIFY(waited.elapsed() >= 2000);
    phone.bluez.reset();
    bus.unregisterService(SystemBluezModel::bluezService());
}

void PhoneArtTest::modelNoNudgeAfterPlayingOpen()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerService(SystemBluezModel::bluezService()));
    SystemBluezModel model(bus, bus);
    QTRY_VERIFY_WITH_TIMEOUT(model.ready(), busTimeoutMs);
    Phone phone = addPhone(trackMap(QStringLiteral("Track Four"), 4, {}), QStringLiteral("playing"), true);
    QTRY_VERIFY_WITH_TIMEOUT(model.phoneArt()->sessionOpen(), busTimeoutMs);
    QTest::qWait(100);
    phone.player->set(QStringLiteral("org.bluez.MediaPlayer1"), QStringLiteral("Status"),
                      QStringLiteral("paused"));
    QTRY_COMPARE_WITH_TIMEOUT(model.status(), QStringLiteral("paused"), busTimeoutMs);
    phone.player->set(QStringLiteral("org.bluez.MediaPlayer1"), QStringLiteral("Track"),
                      trackMap(QStringLiteral("Track Five"), 5, {}));
    QTest::qWait(300);
    QCOMPARE(phone.player->calls, QStringList());
    phone.bluez.reset();
    bus.unregisterService(SystemBluezModel::bluezService());
}

QTEST_GUILESS_MAIN(PhoneArtTest)
#include "tst_phoneart.moc"
