#include "SystemBluezModel.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QtTest>

#include <memory>

using namespace casdap;

namespace {

constexpr int busTimeoutMs = 5000;
constexpr int pollWaitMs = 3000;

QString propertiesInterface()
{
    return QStringLiteral("org.freedesktop.DBus.Properties");
}

QString objectManagerInterface()
{
    return QStringLiteral("org.freedesktop.DBus.ObjectManager");
}

QString device1()
{
    return QStringLiteral("org.bluez.Device1");
}

QString mediaTransport1()
{
    return QStringLiteral("org.bluez.MediaTransport1");
}

QString mediaPlayer1()
{
    return QStringLiteral("org.bluez.MediaPlayer1");
}

QString battery1()
{
    return QStringLiteral("org.bluez.Battery1");
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

QString secondDevicePath()
{
    return QStringLiteral("/org/bluez/hci0/dev_11_22_33_44_55_66");
}

QVariantMap trackMap(const QString &title, int number)
{
    QVariantMap track;
    track.insert(QStringLiteral("Title"), title);
    track.insert(QStringLiteral("Artist"), QStringLiteral("Phone Artist"));
    track.insert(QStringLiteral("Album"), QStringLiteral("Phone Album"));
    track.insert(QStringLiteral("TrackNumber"), QVariant::fromValue(static_cast<uint>(number)));
    track.insert(QStringLiteral("NumberOfTracks"), QVariant::fromValue(static_cast<uint>(11)));
    track.insert(QStringLiteral("Duration"), QVariant::fromValue(static_cast<uint>(228000)));
    return track;
}

}

class FakeObject : public QObject
{
    Q_OBJECT

public:
    FakeObject(const QDBusConnection &bus, const QString &path, const InterfaceList &properties,
               QObject *parent = nullptr)
        : QObject(parent)
        , m_bus(bus)
        , m_path(path)
        , m_properties(properties)
    {
    }

    QString path() const { return m_path; }
    InterfaceList interfaces() const { return m_properties; }

    QVariant get(const QString &interface, const QString &name) const
    {
        return m_properties.value(interface).value(name);
    }

    void store(const QString &interface, const QString &name, const QVariant &value)
    {
        m_properties[interface].insert(name, value);
    }

    void set(const QString &interface, const QString &name, const QVariant &value)
    {
        store(interface, name, value);
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

class Device1Adaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Device1")
    Q_PROPERTY(QString Alias READ alias)
    Q_PROPERTY(bool Connected READ connected)
    Q_PROPERTY(bool Paired READ paired)
    Q_PROPERTY(QStringList UUIDs READ uuids)

public:
    explicit Device1Adaptor(FakeObject *object)
        : QDBusAbstractAdaptor(object)
        , m_object(object)
    {
    }

    QString alias() const { return m_object->get(device1(), QStringLiteral("Alias")).toString(); }
    bool connected() const { return m_object->get(device1(), QStringLiteral("Connected")).toBool(); }
    bool paired() const { return m_object->get(device1(), QStringLiteral("Paired")).toBool(); }
    QStringList uuids() const { return m_object->get(device1(), QStringLiteral("UUIDs")).toStringList(); }

private:
    FakeObject *m_object;
};

class MediaTransport1Adaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.MediaTransport1")
    Q_PROPERTY(QString State READ state)
    Q_PROPERTY(ushort Volume READ volume)
    Q_PROPERTY(QDBusObjectPath Device READ device)

public:
    explicit MediaTransport1Adaptor(FakeObject *object)
        : QDBusAbstractAdaptor(object)
        , m_object(object)
    {
    }

    QString state() const { return m_object->get(mediaTransport1(), QStringLiteral("State")).toString(); }
    ushort volume() const
    {
        return static_cast<ushort>(m_object->get(mediaTransport1(), QStringLiteral("Volume")).toUInt());
    }
    QDBusObjectPath device() const
    {
        return qvariant_cast<QDBusObjectPath>(m_object->get(mediaTransport1(), QStringLiteral("Device")));
    }

private:
    FakeObject *m_object;
};

class MediaPlayer1Adaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.MediaPlayer1")
    Q_PROPERTY(QVariantMap Track READ track)
    Q_PROPERTY(uint Position READ position)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(QString Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QString Repeat READ repeat WRITE setRepeat)
    Q_PROPERTY(QDBusObjectPath Device READ device)

public:
    explicit MediaPlayer1Adaptor(FakeObject *object)
        : QDBusAbstractAdaptor(object)
        , m_object(object)
    {
    }

    QVariantMap track() const { return m_object->get(mediaPlayer1(), QStringLiteral("Track")).toMap(); }
    uint position() const { return m_object->get(mediaPlayer1(), QStringLiteral("Position")).toUInt(); }
    QString status() const { return m_object->get(mediaPlayer1(), QStringLiteral("Status")).toString(); }
    QString shuffle() const { return m_object->get(mediaPlayer1(), QStringLiteral("Shuffle")).toString(); }
    void setShuffle(const QString &value) { m_object->set(mediaPlayer1(), QStringLiteral("Shuffle"), value); }
    QString repeat() const { return m_object->get(mediaPlayer1(), QStringLiteral("Repeat")).toString(); }
    void setRepeat(const QString &value) { m_object->set(mediaPlayer1(), QStringLiteral("Repeat"), value); }
    QDBusObjectPath device() const
    {
        return qvariant_cast<QDBusObjectPath>(m_object->get(mediaPlayer1(), QStringLiteral("Device")));
    }

public slots:
    void Play() { m_object->calls.append(QStringLiteral("Play")); }
    void Pause() { m_object->calls.append(QStringLiteral("Pause")); }
    void Stop() { m_object->calls.append(QStringLiteral("Stop")); }
    void Next() { m_object->calls.append(QStringLiteral("Next")); }
    void Previous() { m_object->calls.append(QStringLiteral("Previous")); }
    void FastForward() { m_object->calls.append(QStringLiteral("FastForward")); }
    void Rewind() { m_object->calls.append(QStringLiteral("Rewind")); }

private:
    FakeObject *m_object;
};

class Battery1Adaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Battery1")
    Q_PROPERTY(uchar Percentage READ percentage)

public:
    explicit Battery1Adaptor(FakeObject *object)
        : QDBusAbstractAdaptor(object)
        , m_object(object)
    {
    }

    uchar percentage() const
    {
        return static_cast<uchar>(m_object->get(battery1(), QStringLiteral("Percentage")).toUInt());
    }

private:
    FakeObject *m_object;
};

class AgentAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.casdap.Bluetooth")
    Q_PROPERTY(bool PairingActive READ pairingActive)
    Q_PROPERTY(uint PairingRemaining READ pairingRemaining)

public:
    explicit AgentAdaptor(FakeObject *object)
        : QDBusAbstractAdaptor(object)
        , m_object(object)
    {
    }

    bool pairingActive() const
    {
        return m_object->get(SystemBluezModel::agentInterface(), QStringLiteral("PairingActive")).toBool();
    }
    uint pairingRemaining() const
    {
        return m_object->get(SystemBluezModel::agentInterface(), QStringLiteral("PairingRemaining")).toUInt();
    }

public slots:
    void StartPairing(uint seconds)
    {
        m_object->calls.append(QStringLiteral("StartPairing %1").arg(seconds));
        m_object->set(SystemBluezModel::agentInterface(), QStringLiteral("PairingRemaining"),
                      QVariant::fromValue(seconds));
        m_object->set(SystemBluezModel::agentInterface(), QStringLiteral("PairingActive"), true);
    }

    void StopPairing()
    {
        m_object->calls.append(QStringLiteral("StopPairing"));
        m_object->set(SystemBluezModel::agentInterface(), QStringLiteral("PairingActive"), false);
        m_object->set(SystemBluezModel::agentInterface(), QStringLiteral("PairingRemaining"),
                      QVariant::fromValue(static_cast<uint>(0)));
    }

private:
    FakeObject *m_object;
};

class FakeBluez : public QObject
{
    Q_OBJECT

public:
    explicit FakeBluez(const QDBusConnection &bus, QObject *parent = nullptr);
    ~FakeBluez() override;

    bool registerRoot();
    FakeObject *add(const QString &path, const InterfaceList &properties, bool announce);
    bool remove(const QString &path);
    FakeObject *object(const QString &path) const;
    ManagedObjectList managedObjects() const;

private:
    QDBusConnection m_bus;
    QMap<QString, FakeObject *> m_objects;
};

class ObjectManagerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.DBus.ObjectManager")

public:
    explicit ObjectManagerAdaptor(FakeBluez *bluez)
        : QDBusAbstractAdaptor(bluez)
        , m_bluez(bluez)
    {
    }

public slots:
    ManagedObjectList GetManagedObjects() { return m_bluez->managedObjects(); }

private:
    FakeBluez *m_bluez;
};

FakeBluez::FakeBluez(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
    new ObjectManagerAdaptor(this);
}

FakeBluez::~FakeBluez()
{
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it)
        m_bus.unregisterObject(it.key());
    m_bus.unregisterObject(QStringLiteral("/"));
}

bool FakeBluez::registerRoot()
{
    return m_bus.registerObject(QStringLiteral("/"), this, QDBusConnection::ExportAdaptors);
}

FakeObject *FakeBluez::add(const QString &path, const InterfaceList &properties, bool announce)
{
    auto *object = new FakeObject(m_bus, path, properties, this);
    if (properties.contains(device1()))
        new Device1Adaptor(object);
    if (properties.contains(mediaTransport1()))
        new MediaTransport1Adaptor(object);
    if (properties.contains(mediaPlayer1()))
        new MediaPlayer1Adaptor(object);
    if (properties.contains(battery1()))
        new Battery1Adaptor(object);
    if (!m_bus.registerObject(path, object, QDBusConnection::ExportAdaptors)) {
        delete object;
        return nullptr;
    }
    m_objects.insert(path, object);
    if (announce) {
        QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/"), objectManagerInterface(),
                                                          QStringLiteral("InterfacesAdded"));
        signal << QVariant::fromValue(QDBusObjectPath(path)) << QVariant::fromValue(properties);
        m_bus.send(signal);
    }
    return object;
}

bool FakeBluez::remove(const QString &path)
{
    FakeObject *object = m_objects.take(path);
    if (!object)
        return false;
    const QStringList interfaces = object->interfaces().keys();
    m_bus.unregisterObject(path);
    delete object;
    QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/"), objectManagerInterface(),
                                                      QStringLiteral("InterfacesRemoved"));
    signal << QVariant::fromValue(QDBusObjectPath(path)) << QVariant(interfaces);
    return m_bus.send(signal);
}

FakeObject *FakeBluez::object(const QString &path) const
{
    return m_objects.value(path);
}

ManagedObjectList FakeBluez::managedObjects() const
{
    ManagedObjectList list;
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it)
        list.insert(QDBusObjectPath(it.key()), it.value()->interfaces());
    return list;
}

class SystemBluezTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void initialState();
    void propertiesChanged();
    void positionPolling();
    void controls();
    void pairing();
    void deviceLifecycle();

private:
    std::unique_ptr<FakeBluez> m_fake;
    std::unique_ptr<FakeObject> m_agent;
    std::unique_ptr<SystemBluezModel> m_model;
    FakeObject *m_device = nullptr;
    FakeObject *m_transport = nullptr;
    FakeObject *m_player = nullptr;
};

void SystemBluezTest::initTestCase()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        QSKIP("no session bus, run under dbus-run-session");

    SystemBluezModel::registerMetaTypes();
    QVERIFY2(bus.registerService(SystemBluezModel::bluezService()),
             qPrintable(bus.lastError().message()));
    QVERIFY2(bus.registerService(SystemBluezModel::agentService()),
             qPrintable(bus.lastError().message()));

    m_fake = std::make_unique<FakeBluez>(bus);
    QVERIFY(m_fake->registerRoot());

    QVariantMap deviceProps;
    deviceProps.insert(QStringLiteral("Alias"), QStringLiteral("Pixel 8"));
    deviceProps.insert(QStringLiteral("Connected"), true);
    deviceProps.insert(QStringLiteral("Paired"), true);
    deviceProps.insert(QStringLiteral("UUIDs"),
                       QStringList{SystemBluezModel::a2dpSourceUuid(),
                                   QStringLiteral("0000110e-0000-1000-8000-00805f9b34fb")});
    QVariantMap batteryProps;
    batteryProps.insert(QStringLiteral("Percentage"), QVariant::fromValue(static_cast<uchar>(73)));
    InterfaceList deviceInterfaces;
    deviceInterfaces.insert(device1(), deviceProps);
    deviceInterfaces.insert(battery1(), batteryProps);
    m_device = m_fake->add(devicePath(), deviceInterfaces, false);
    QVERIFY(m_device);

    QVariantMap transportProps;
    transportProps.insert(QStringLiteral("State"), QStringLiteral("idle"));
    transportProps.insert(QStringLiteral("Volume"), QVariant::fromValue(static_cast<ushort>(64)));
    transportProps.insert(QStringLiteral("Device"), QVariant::fromValue(QDBusObjectPath(devicePath())));
    InterfaceList transportInterfaces;
    transportInterfaces.insert(mediaTransport1(), transportProps);
    m_transport = m_fake->add(transportPath(), transportInterfaces, false);
    QVERIFY(m_transport);

    QVariantMap playerProps;
    playerProps.insert(QStringLiteral("Track"), trackMap(QStringLiteral("Track Four"), 4));
    playerProps.insert(QStringLiteral("Position"), QVariant::fromValue(static_cast<uint>(125000)));
    playerProps.insert(QStringLiteral("Status"), QStringLiteral("paused"));
    playerProps.insert(QStringLiteral("Shuffle"), QStringLiteral("off"));
    playerProps.insert(QStringLiteral("Repeat"), QStringLiteral("off"));
    playerProps.insert(QStringLiteral("Device"), QVariant::fromValue(QDBusObjectPath(devicePath())));
    InterfaceList playerInterfaces;
    playerInterfaces.insert(mediaPlayer1(), playerProps);
    m_player = m_fake->add(playerPath(), playerInterfaces, false);
    QVERIFY(m_player);

    QVariantMap agentProps;
    agentProps.insert(QStringLiteral("PairingActive"), false);
    agentProps.insert(QStringLiteral("PairingRemaining"), QVariant::fromValue(static_cast<uint>(0)));
    InterfaceList agentInterfaces;
    agentInterfaces.insert(SystemBluezModel::agentInterface(), agentProps);
    m_agent = std::make_unique<FakeObject>(bus, SystemBluezModel::agentPath(), agentInterfaces);
    new AgentAdaptor(m_agent.get());
    QVERIFY(bus.registerObject(SystemBluezModel::agentPath(), m_agent.get(),
                               QDBusConnection::ExportAdaptors));

    m_model = std::make_unique<SystemBluezModel>(bus, bus);
    QTRY_VERIFY_WITH_TIMEOUT(m_model->connected(), busTimeoutMs);
}

void SystemBluezTest::cleanupTestCase()
{
    m_model.reset();
    if (m_agent)
        QDBusConnection::sessionBus().unregisterObject(SystemBluezModel::agentPath());
}

void SystemBluezTest::initialState()
{
    QCOMPARE(m_model->devicePath(), devicePath());
    QCOMPARE(m_model->transportPath(), transportPath());
    QCOMPARE(m_model->playerPath(), playerPath());
    QCOMPARE(m_model->deviceName(), QStringLiteral("Pixel 8"));
    QVERIFY(!m_model->transportActive());
    QCOMPARE(m_model->title(), QStringLiteral("Track Four"));
    QCOMPARE(m_model->artist(), QStringLiteral("Phone Artist"));
    QCOMPARE(m_model->album(), QStringLiteral("Phone Album"));
    QCOMPARE(m_model->trackNumber(), 4);
    QCOMPARE(m_model->numberOfTracks(), 11);
    QCOMPARE(m_model->durationMs(), qint64(228000));
    QCOMPARE(m_model->positionMs(), qint64(125000));
    QCOMPARE(m_model->status(), QStringLiteral("paused"));
    QCOMPARE(m_model->shuffle(), QStringLiteral("off"));
    QCOMPARE(m_model->repeat(), QStringLiteral("off"));
    QCOMPARE(m_model->volume(), BluezModel::volumeFromTransport(64));
    QCOMPARE(m_model->volume(), 50);
    QCOMPARE(m_model->battery(), 73);
    QVERIFY(!m_model->pairing());
    QCOMPARE(m_model->pairingRemaining(), 0);
    QVERIFY(m_model->active());
    QVERIFY(!m_model->pollingPosition());
}

void SystemBluezTest::propertiesChanged()
{
    QSignalSpy volumeSpy(m_model.get(), &BluezModel::volumeChanged);

    m_transport->set(mediaTransport1(), QStringLiteral("State"), QStringLiteral("active"));
    QTRY_VERIFY_WITH_TIMEOUT(m_model->transportActive(), busTimeoutMs);

    m_transport->set(mediaTransport1(), QStringLiteral("Volume"),
                     QVariant::fromValue(static_cast<ushort>(50)));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->volume(), BluezModel::volumeFromTransport(50), busTimeoutMs);
    QCOMPARE(volumeSpy.count(), 1);
    QCOMPARE(volumeSpy.at(0).at(0).toInt(), 39);

    m_player->set(mediaPlayer1(), QStringLiteral("Track"), trackMap(QStringLiteral("Track Five"), 5));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->title(), QStringLiteral("Track Five"), busTimeoutMs);
    QCOMPARE(m_model->trackNumber(), 5);
    QCOMPARE(m_model->numberOfTracks(), 11);

    m_player->set(mediaPlayer1(), QStringLiteral("Position"), QVariant::fromValue(static_cast<uint>(0)));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->positionMs(), qint64(0), busTimeoutMs);

    m_device->set(device1(), QStringLiteral("Alias"), QStringLiteral("Pixel 9"));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->deviceName(), QStringLiteral("Pixel 9"), busTimeoutMs);

    m_device->set(battery1(), QStringLiteral("Percentage"), QVariant::fromValue(static_cast<uchar>(42)));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->battery(), 42, busTimeoutMs);

    QCOMPARE(volumeSpy.count(), 1);
}

void SystemBluezTest::positionPolling()
{
    QVERIFY(!m_model->pollingPosition());
    m_player->set(mediaPlayer1(), QStringLiteral("Status"), QStringLiteral("playing"));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->status(), QStringLiteral("playing"), busTimeoutMs);
    QVERIFY(m_model->pollingPosition());

    m_player->store(mediaPlayer1(), QStringLiteral("Position"), QVariant::fromValue(static_cast<uint>(130000)));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->positionMs(), qint64(130000), pollWaitMs);

    m_model->setActive(false);
    QVERIFY(!m_model->pollingPosition());
    QTest::qWait(200);
    m_player->store(mediaPlayer1(), QStringLiteral("Position"), QVariant::fromValue(static_cast<uint>(140000)));
    QTest::qWait(1500);
    QCOMPARE(m_model->positionMs(), qint64(130000));

    m_model->setActive(true);
    QVERIFY(m_model->pollingPosition());
    QTRY_COMPARE_WITH_TIMEOUT(m_model->positionMs(), qint64(140000), pollWaitMs);

    m_player->set(mediaPlayer1(), QStringLiteral("Status"), QStringLiteral("paused"));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->status(), QStringLiteral("paused"), busTimeoutMs);
    QVERIFY(!m_model->pollingPosition());
}

void SystemBluezTest::controls()
{
    m_player->calls.clear();
    m_model->next();
    QTRY_VERIFY_WITH_TIMEOUT(m_player->calls.contains(QStringLiteral("Next")), busTimeoutMs);
    m_model->play();
    m_model->pause();
    m_model->previous();
    m_model->fastForward();
    m_model->rewind();
    const QStringList expected{QStringLiteral("Next"), QStringLiteral("Play"), QStringLiteral("Pause"),
                               QStringLiteral("Previous"), QStringLiteral("FastForward"),
                               QStringLiteral("Rewind")};
    QTRY_COMPARE_WITH_TIMEOUT(m_player->calls, expected, busTimeoutMs);

    m_model->setShuffle(QStringLiteral("alltracks"));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->shuffle(), QStringLiteral("alltracks"), busTimeoutMs);
    QCOMPARE(m_player->get(mediaPlayer1(), QStringLiteral("Shuffle")).toString(),
             QStringLiteral("alltracks"));

    m_model->setRepeat(QStringLiteral("singletrack"));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->repeat(), QStringLiteral("singletrack"), busTimeoutMs);
    QCOMPARE(m_player->get(mediaPlayer1(), QStringLiteral("Repeat")).toString(),
             QStringLiteral("singletrack"));
}

void SystemBluezTest::pairing()
{
    QVERIFY(!m_model->pairing());
    m_agent->calls.clear();

    m_model->startPairing(120);
    QTRY_VERIFY_WITH_TIMEOUT(m_agent->calls.contains(QStringLiteral("StartPairing 120")), busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(m_model->pairing(), busTimeoutMs);
    QCOMPARE(m_model->pairingRemaining(), 120);

    m_agent->set(SystemBluezModel::agentInterface(), QStringLiteral("PairingRemaining"),
                 QVariant::fromValue(static_cast<uint>(119)));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->pairingRemaining(), 119, busTimeoutMs);

    m_model->stopPairing();
    QTRY_VERIFY_WITH_TIMEOUT(m_agent->calls.contains(QStringLiteral("StopPairing")), busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(!m_model->pairing(), busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(m_model->pairingRemaining(), 0, busTimeoutMs);
}

void SystemBluezTest::deviceLifecycle()
{
    QVERIFY(m_fake->remove(transportPath()));
    m_transport = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT(m_model->transportPath().isEmpty(), busTimeoutMs);
    QVERIFY(!m_model->transportActive());
    QVERIFY(m_model->connected());
    QCOMPARE(m_model->devicePath(), devicePath());
    QCOMPARE(m_model->title(), QStringLiteral("Track Five"));

    QVERIFY(m_fake->remove(playerPath()));
    m_player = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT(m_model->playerPath().isEmpty(), busTimeoutMs);
    QVERIFY(m_model->connected());
    QCOMPARE(m_model->title(), QString());
    QCOMPARE(m_model->status(), QStringLiteral("stopped"));
    QCOMPARE(m_model->positionMs(), qint64(0));
    QVERIFY(!m_model->pollingPosition());

    m_device->set(device1(), QStringLiteral("Connected"), false);
    QTRY_VERIFY_WITH_TIMEOUT(!m_model->connected(), busTimeoutMs);
    QCOMPARE(m_model->deviceName(), QString());
    QCOMPARE(m_model->battery(), -1);
    QVERIFY(m_model->devicePath().isEmpty());

    m_device->set(device1(), QStringLiteral("Connected"), true);
    QTRY_VERIFY_WITH_TIMEOUT(m_model->connected(), busTimeoutMs);
    QCOMPARE(m_model->deviceName(), QStringLiteral("Pixel 9"));
    QCOMPARE(m_model->battery(), 42);

    QVERIFY(m_fake->remove(devicePath()));
    m_device = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT(!m_model->connected(), busTimeoutMs);
    QCOMPARE(m_model->deviceName(), QString());

    QVariantMap deviceProps;
    deviceProps.insert(QStringLiteral("Alias"), QStringLiteral("Second Phone"));
    deviceProps.insert(QStringLiteral("Connected"), true);
    deviceProps.insert(QStringLiteral("UUIDs"), QStringList{SystemBluezModel::a2dpSourceUuid()});
    InterfaceList deviceInterfaces;
    deviceInterfaces.insert(device1(), deviceProps);
    FakeObject *second = m_fake->add(secondDevicePath(), deviceInterfaces, true);
    QVERIFY(second);
    QTRY_VERIFY_WITH_TIMEOUT(m_model->connected(), busTimeoutMs);
    QCOMPARE(m_model->devicePath(), secondDevicePath());
    QCOMPARE(m_model->deviceName(), QStringLiteral("Second Phone"));
    QCOMPARE(m_model->battery(), -1);

    QVariantMap transportProps;
    transportProps.insert(QStringLiteral("State"), QStringLiteral("active"));
    transportProps.insert(QStringLiteral("Volume"), QVariant::fromValue(static_cast<ushort>(127)));
    transportProps.insert(QStringLiteral("Device"), QVariant::fromValue(QDBusObjectPath(secondDevicePath())));
    InterfaceList transportInterfaces;
    transportInterfaces.insert(mediaTransport1(), transportProps);
    QSignalSpy volumeSpy(m_model.get(), &BluezModel::volumeChanged);
    QVERIFY(m_fake->add(secondDevicePath() + QStringLiteral("/fd0"), transportInterfaces, true));
    QTRY_VERIFY_WITH_TIMEOUT(m_model->transportActive(), busTimeoutMs);
    QCOMPARE(m_model->volume(), 100);
    QCOMPARE(volumeSpy.count(), 1);
}

QTEST_GUILESS_MAIN(SystemBluezTest)
#include "tst_systembluez.moc"
