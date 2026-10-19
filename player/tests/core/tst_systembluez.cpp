#include "SystemBluezModel.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QtTest>

#include <memory>

using namespace microdap;

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
    Q_PROPERTY(bool Trusted READ trusted WRITE setTrusted)

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
    bool trusted() const { return m_object->get(device1(), QStringLiteral("Trusted")).toBool(); }
    void setTrusted(bool value) { m_object->store(device1(), QStringLiteral("Trusted"), value); }

public slots:
    void Disconnect()
    {
        m_object->calls.append(QStringLiteral("Disconnect"));
        m_object->set(device1(), QStringLiteral("Connected"), false);
    }

    void Connect()
    {
        m_object->calls.append(QStringLiteral("Connect"));
        m_object->set(device1(), QStringLiteral("Connected"), true);
    }

    void Pair(const QDBusMessage &message)
    {
        m_object->calls.append(QStringLiteral("Pair"));
        if (m_object->get(device1(), QStringLiteral("Alias")).toString().startsWith(QLatin1StringView("Bad"))) {
            message.setDelayedReply(true);
            QDBusConnection::sessionBus().send(message.createErrorReply(QStringLiteral("org.bluez.Error.AuthenticationFailed"),
                                                                        QStringLiteral("failed")));
            return;
        }
        m_object->set(device1(), QStringLiteral("Paired"), true);
    }

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
    Q_CLASSINFO("D-Bus Interface", "net.seilent.MicroDap.Bluetooth")
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

    void SetRole(const QString &role)
    {
        m_object->calls.append(QStringLiteral("SetRole ") + role);
    }

    void StartSearch(uint seconds)
    {
        m_object->calls.append(QStringLiteral("StartSearch %1").arg(seconds));
        m_object->set(SystemBluezModel::agentInterface(), QStringLiteral("SearchRemaining"),
                      QVariant::fromValue(seconds));
        m_object->set(SystemBluezModel::agentInterface(), QStringLiteral("SearchActive"), true);
    }

    void StopSearch()
    {
        m_object->calls.append(QStringLiteral("StopSearch"));
        m_object->set(SystemBluezModel::agentInterface(), QStringLiteral("SearchActive"), false);
        m_object->set(SystemBluezModel::agentInterface(), QStringLiteral("SearchRemaining"),
                      QVariant::fromValue(static_cast<uint>(0)));
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

    QStringList removedDevices;

private:
    QDBusConnection m_bus;
    QMap<QString, FakeObject *> m_objects;
    QObject m_adapter;
};

class Adapter1Adaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Adapter1")

public:
    Adapter1Adaptor(QObject *object, FakeBluez *bluez)
        : QDBusAbstractAdaptor(object)
        , m_bluez(bluez)
    {
    }

public slots:
    void RemoveDevice(const QDBusObjectPath &path)
    {
        m_bluez->removedDevices.append(path.path());
        m_bluez->remove(path.path());
    }

private:
    FakeBluez *m_bluez;
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
    m_bus.unregisterObject(QStringLiteral("/org/bluez/hci0"));
    m_bus.unregisterObject(QStringLiteral("/"));
}

bool FakeBluez::registerRoot()
{
    new Adapter1Adaptor(&m_adapter, this);
    return m_bus.registerObject(QStringLiteral("/"), this, QDBusConnection::ExportAdaptors)
           && m_bus.registerObject(QStringLiteral("/org/bluez/hci0"), &m_adapter, QDBusConnection::ExportAdaptors);
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
    void startVolumeSilent();
    void transportStates();
    void propertiesChanged();
    void positionPolling();
    void controls();
    void pairing();
    void deviceLifecycle();
    void transportStateNone();
    void headsetsAndRoles();
    void headsetLists();

private:
    QTemporaryDir m_state;
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

    qputenv("MICRODAP_BT_ROLE_CHECK_MS", QByteArrayLiteral("500"));
    QVERIFY(m_state.isValid());
    qputenv("XDG_STATE_HOME", m_state.path().toUtf8());
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

void SystemBluezTest::startVolumeSilent()
{
    SystemBluezModel model(QDBusConnection::sessionBus(), QDBusConnection::sessionBus());
    QSignalSpy volumeSpy(&model, &BluezModel::volumeChanged);
    QSignalSpy phoneSpy(&model, &BluezModel::phoneVolumeChanged);
    QTRY_VERIFY_WITH_TIMEOUT(model.connected(), busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(model.volume(), 50, busTimeoutMs);
    QVERIFY(!volumeSpy.isEmpty());
    QCOMPARE(phoneSpy.count(), 0);
}

void SystemBluezTest::transportStates()
{
    QCOMPARE(m_model->transportState(), QStringLiteral("idle"));
    QSignalSpy stateSpy(m_model.get(), &BluezModel::transportStateChanged);
    QSignalSpy activeSpy(m_model.get(), &BluezModel::transportActiveChanged);
    QStringList seen;
    connect(m_model.get(), &BluezModel::transportActiveChanged, this, [this, &seen] {
        seen.append(QStringLiteral("active ") + m_model->transportState());
    });
    connect(m_model.get(), &BluezModel::transportStateChanged, this, [this, &seen] {
        seen.append(QStringLiteral("state ") + (m_model->transportActive() ? QStringLiteral("true")
                                                                          : QStringLiteral("false")));
    });

    m_transport->set(mediaTransport1(), QStringLiteral("State"), QStringLiteral("pending"));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->transportState(), QStringLiteral("pending"), busTimeoutMs);
    QVERIFY(!m_model->transportActive());
    QCOMPARE(activeSpy.count(), 0);

    m_transport->set(mediaTransport1(), QStringLiteral("State"), QStringLiteral("active"));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->transportState(), QStringLiteral("active"), busTimeoutMs);
    QVERIFY(m_model->transportActive());
    QCOMPARE(activeSpy.count(), 1);

    m_transport->set(mediaTransport1(), QStringLiteral("State"), QStringLiteral("idle"));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->transportState(), QStringLiteral("idle"), busTimeoutMs);
    QVERIFY(!m_model->transportActive());
    QCOMPARE(activeSpy.count(), 2);
    QCOMPARE(stateSpy.count(), 3);
    QCOMPARE(seen, QStringList({QStringLiteral("state false"), QStringLiteral("active active"),
                                QStringLiteral("state true"), QStringLiteral("active idle"),
                                QStringLiteral("state false")}));
    disconnect(m_model.get(), nullptr, this, nullptr);
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
    QCOMPARE(m_model->transportState(), QStringLiteral("none"));
    QCOMPARE(m_model->deviceName(), QString());
    QCOMPARE(m_model->lastPhone(), QStringLiteral("Pixel 9"));
    QFile last(BluezModel::lastPhonePath());
    QVERIFY(last.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(last.readAll()), QStringLiteral("Pixel 9\n"));
    last.close();
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
    QCOMPARE(m_model->lastPhone(), QStringLiteral("Second Phone"));
    QVERIFY(last.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(last.readAll()), QStringLiteral("Second Phone\n"));
    last.close();
    QCOMPARE(m_model->battery(), -1);

    QVariantMap transportProps;
    transportProps.insert(QStringLiteral("State"), QStringLiteral("active"));
    transportProps.insert(QStringLiteral("Volume"), QVariant::fromValue(static_cast<ushort>(127)));
    transportProps.insert(QStringLiteral("Device"), QVariant::fromValue(QDBusObjectPath(secondDevicePath())));
    InterfaceList transportInterfaces;
    transportInterfaces.insert(mediaTransport1(), transportProps);
    QSignalSpy volumeSpy(m_model.get(), &BluezModel::volumeChanged);
    QSignalSpy phoneSpy(m_model.get(), &BluezModel::phoneVolumeChanged);
    QVERIFY(m_fake->add(secondDevicePath() + QStringLiteral("/fd0"), transportInterfaces, true));
    QTRY_VERIFY_WITH_TIMEOUT(m_model->transportActive(), busTimeoutMs);
    QCOMPARE(m_model->volume(), 100);
    QCOMPARE(volumeSpy.count(), 1);
    QCOMPARE(phoneSpy.count(), 0);

    FakeObject *transport = m_fake->object(secondDevicePath() + QStringLiteral("/fd0"));
    QVERIFY(transport);
    transport->set(mediaTransport1(), QStringLiteral("Volume"), QVariant::fromValue(static_cast<ushort>(64)));
    QTRY_COMPARE_WITH_TIMEOUT(phoneSpy.count(), 1, busTimeoutMs);
    QCOMPARE(phoneSpy.at(0).at(0).toInt(), 50);
    QCOMPARE(volumeSpy.count(), 2);
}

void SystemBluezTest::transportStateNone()
{
    QCOMPARE(m_model->transportState(), QStringLiteral("active"));
    QSignalSpy stateSpy(m_model.get(), &BluezModel::transportStateChanged);
    QSignalSpy phoneSpy(m_model.get(), &BluezModel::phoneVolumeChanged);
    FakeObject *second = m_fake->object(secondDevicePath());
    QVERIFY(second);
    second->set(device1(), QStringLiteral("Connected"), false);
    QTRY_VERIFY_WITH_TIMEOUT(!m_model->connected(), busTimeoutMs);
    QCOMPARE(m_model->transportState(), QStringLiteral("none"));
    QVERIFY(!m_model->transportActive());
    second->set(device1(), QStringLiteral("Connected"), true);
    QTRY_COMPARE_WITH_TIMEOUT(m_model->transportState(), QStringLiteral("active"), busTimeoutMs);
    QVERIFY(m_model->transportActive());
    QCOMPARE(stateSpy.count(), 2);
    QCOMPARE(m_model->volume(), 50);
    QCOMPARE(phoneSpy.count(), 0);

    QVERIFY(m_fake->remove(secondDevicePath() + QStringLiteral("/fd0")));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->transportState(), QStringLiteral("none"), busTimeoutMs);
    QVERIFY(m_model->transportPath().isEmpty());
    QVERIFY(!m_model->transportActive());
    QVERIFY(m_model->connected());
    QCOMPARE(stateSpy.count(), 3);
}

void SystemBluezTest::headsetsAndRoles()
{
    QVERIFY(m_model->connected());
    QCOMPARE(m_model->devicePath(), secondDevicePath());
    FakeObject *second = m_fake->object(secondDevicePath());
    QVERIFY(second);
    second->set(device1(), QStringLiteral("Paired"), true);

    const QString headsetPath = QStringLiteral("/org/bluez/hci0/dev_77_88_99_AA_BB_CC");
    QVariantMap headsetProps;
    headsetProps.insert(QStringLiteral("Alias"), QStringLiteral("Buds"));
    headsetProps.insert(QStringLiteral("Connected"), true);
    headsetProps.insert(QStringLiteral("Paired"), true);
    headsetProps.insert(QStringLiteral("UUIDs"), QStringList{QStringLiteral("0000110b-0000-1000-8000-00805f9b34fb"),
                                                             QStringLiteral("0000110e-0000-1000-8000-00805f9b34fb")});
    InterfaceList headsetInterfaces;
    headsetInterfaces.insert(device1(), headsetProps);
    FakeObject *headset = m_fake->add(headsetPath, headsetInterfaces, true);
    QVERIFY(headset);
    QVariantMap transportProps;
    transportProps.insert(QStringLiteral("State"), QStringLiteral("active"));
    transportProps.insert(QStringLiteral("Device"), QVariant::fromValue(QDBusObjectPath(headsetPath)));
    InterfaceList transportInterfaces;
    transportInterfaces.insert(mediaTransport1(), transportProps);
    QVERIFY(m_fake->add(headsetPath + QStringLiteral("/fd1"), transportInterfaces, true));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->headsetName(), QStringLiteral("Buds"), busTimeoutMs);
    QCOMPARE(m_model->devicePath(), secondDevicePath());
    QVERIFY(!m_model->transportActive());
    QCOMPARE(m_model->role(), QStringLiteral("receiver"));

    QSignalSpy wrong(m_model.get(), &BluezModel::wrongRole);
    QCOMPARE(m_model->lastHeadset(), QString());
    headset->set(device1(), QStringLiteral("ServicesResolved"), true);
    QTRY_VERIFY_WITH_TIMEOUT(headset->calls.contains(QStringLiteral("Disconnect")), busTimeoutMs);
    QCOMPARE(wrong.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(m_model->headsetName(), QString(), busTimeoutMs);
    headset->calls.clear();
    QVERIFY(second->calls.isEmpty());

    m_agent->calls.clear();
    m_model->startSearch(90);
    QVERIFY(m_model->switching());
    QCOMPARE(m_model->activeRole(), QStringLiteral("headphones"));
    QTRY_VERIFY_WITH_TIMEOUT(m_agent->calls.contains(QStringLiteral("SetRole headphones")), busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(second->calls.contains(QStringLiteral("Disconnect")), busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(!m_model->switching(), busTimeoutMs);
    QCOMPARE(m_model->role(), QStringLiteral("headphones"));
    QVERIFY(!m_model->connected());
    QTRY_VERIFY_WITH_TIMEOUT(m_agent->calls.contains(QStringLiteral("StartSearch 90")), busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(m_model->searching(), busTimeoutMs);
    QCOMPARE(m_model->searchRemaining(), 90);
    QCOMPARE(m_agent->calls.indexOf(QStringLiteral("SetRole headphones")), 0);

    headset->set(device1(), QStringLiteral("Connected"), true);
    QTRY_COMPARE_WITH_TIMEOUT(m_model->headsetName(), QStringLiteral("Buds"), busTimeoutMs);
    QCOMPARE(m_model->lastHeadset(), headsetPath);
    headset->set(device1(), QStringLiteral("ServicesResolved"), true);
    second->calls.clear();
    second->set(device1(), QStringLiteral("Connected"), true);
    second->set(device1(), QStringLiteral("ServicesResolved"), true);
    QTRY_COMPARE_WITH_TIMEOUT(wrong.count(), 1, busTimeoutMs);
    QCOMPARE(wrong.at(0).at(0).toString(), QStringLiteral("Second Phone"));
    QCOMPARE(wrong.at(0).at(1).toString(), QStringLiteral("phone"));
    QTRY_VERIFY_WITH_TIMEOUT(second->calls.contains(QStringLiteral("Disconnect")), busTimeoutMs);
    QVERIFY(headset->calls.isEmpty());

    m_agent->calls.clear();
    m_model->setRole(QStringLiteral("receiver"));
    QTRY_VERIFY_WITH_TIMEOUT(headset->calls.contains(QStringLiteral("Disconnect")), busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(m_model->role(), QStringLiteral("receiver"), busTimeoutMs);
    QVERIFY(m_agent->calls.contains(QStringLiteral("SetRole receiver")));
    QCOMPARE(m_model->headsetName(), QString());
    QCOMPARE(wrong.count(), 1);

    headset->calls.clear();
    headset->set(device1(), QStringLiteral("Connected"), true);
    QTRY_COMPARE_WITH_TIMEOUT(wrong.count(), 2, 3000);
    QCOMPARE(wrong.at(1).at(0).toString(), QStringLiteral("Buds"));
    QCOMPARE(wrong.at(1).at(1).toString(), QStringLiteral("headphones"));
    QTRY_VERIFY_WITH_TIMEOUT(headset->calls.contains(QStringLiteral("Disconnect")), busTimeoutMs);
    headset->calls.clear();
    headset->set(device1(), QStringLiteral("Connected"), true);
    headset->set(device1(), QStringLiteral("UUIDs"), QStringList{QStringLiteral("0000110b-0000-1000-8000-00805f9b34fb")});
    headset->set(device1(), QStringLiteral("ServicesResolved"), true);
    QTRY_COMPARE_WITH_TIMEOUT(wrong.count(), 3, busTimeoutMs);
    QTest::qWait(1000);
    QCOMPARE(wrong.count(), 3);
    QCOMPARE(headset->calls.count(QStringLiteral("Disconnect")), 1);
}

void SystemBluezTest::headsetLists()
{
    const QString headsetPath = QStringLiteral("/org/bluez/hci0/dev_77_88_99_AA_BB_CC");
    FakeObject *buds = m_fake->object(headsetPath);
    QVERIFY(buds);
    QTRY_COMPARE_WITH_TIMEOUT(m_model->headsets().size(), 1, busTimeoutMs);
    QVariantMap row = m_model->headsets().at(0).toMap();
    QCOMPARE(row.value(QStringLiteral("path")).toString(), headsetPath);
    QCOMPARE(row.value(QStringLiteral("name")).toString(), QStringLiteral("Buds"));
    QCOMPARE(row.value(QStringLiteral("connected")).toBool(), false);
    QCOMPARE(row.value(QStringLiteral("state")).toString(), QStringLiteral("disconnected"));

    auto addDevice = [this](const QString &path, const QString &alias, bool paired, const QStringList &uuids, uint cls) {
        QVariantMap props;
        props.insert(QStringLiteral("Alias"), alias);
        props.insert(QStringLiteral("Connected"), false);
        props.insert(QStringLiteral("Paired"), paired);
        props.insert(QStringLiteral("UUIDs"), uuids);
        props.insert(QStringLiteral("Class"), QVariant::fromValue(cls));
        InterfaceList interfaces;
        interfaces.insert(device1(), props);
        return m_fake->add(path, interfaces, true);
    };
    const QString sink = QStringLiteral("0000110b-0000-1000-8000-00805f9b34fb");
    FakeObject *arc = addDevice(QStringLiteral("/org/bluez/hci0/dev_01_00_00_00_00_01"), QStringLiteral("Arc"), true,
                                {sink}, 0x240404);
    FakeObject *fresh = addDevice(QStringLiteral("/org/bluez/hci0/dev_01_00_00_00_00_02"), QStringLiteral("Zest"), false,
                                  {}, 0x240404);
    FakeObject *bad = addDevice(QStringLiteral("/org/bluez/hci0/dev_01_00_00_00_00_03"), QStringLiteral("Bad Cans"), false,
                                {sink}, 0);
    QVERIFY(addDevice(QStringLiteral("/org/bluez/hci0/dev_01_00_00_00_00_04"), QStringLiteral("Keyboard"), false,
                      {QStringLiteral("00001124-0000-1000-8000-00805f9b34fb")}, 0x002540));
    QVERIFY(arc && fresh && bad);
    QTRY_COMPARE_WITH_TIMEOUT(m_model->headsets().size(), 2, busTimeoutMs);
    QCOMPARE(m_model->headsets().at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Arc"));
    QCOMPARE(m_model->headsets().at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Buds"));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->discovered().size(), 2, busTimeoutMs);
    QCOMPARE(m_model->discovered().at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Bad Cans"));
    QCOMPARE(m_model->discovered().at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Zest"));
    QCOMPARE(m_model->discovered().at(1).toMap().value(QStringLiteral("state")).toString(), QStringLiteral("not paired"));

    m_agent->calls.clear();
    m_model->connectHeadset(arc->path());
    QCOMPARE(m_model->activeRole(), QStringLiteral("headphones"));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->role(), QStringLiteral("headphones"), busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(arc->calls.contains(QStringLiteral("Connect")), busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(m_model->headsetName(), QStringLiteral("Arc"), busTimeoutMs);
    QCOMPARE(m_model->headsets().at(0).toMap().value(QStringLiteral("connected")).toBool(), true);
    QCOMPARE(m_model->headsets().at(0).toMap().value(QStringLiteral("state")).toString(), QStringLiteral("connected"));
    QCOMPARE(m_model->lastHeadset(), arc->path());

    buds->calls.clear();
    buds->set(device1(), QStringLiteral("Connected"), true);
    QTRY_VERIFY_WITH_TIMEOUT(buds->calls.contains(QStringLiteral("Disconnect")), busTimeoutMs);
    QVERIFY(!arc->calls.contains(QStringLiteral("Disconnect")));

    arc->calls.clear();
    buds->calls.clear();
    m_model->connectHeadset(headsetPath);
    QTRY_VERIFY_WITH_TIMEOUT(arc->calls.contains(QStringLiteral("Disconnect")), busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(buds->calls.contains(QStringLiteral("Connect")), busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(m_model->headsetName(), QStringLiteral("Buds"), busTimeoutMs);
    buds->set(device1(), QStringLiteral("Connected"), false);
    QTRY_COMPARE_WITH_TIMEOUT(m_model->headsetName(), QString(), busTimeoutMs);
    arc->set(device1(), QStringLiteral("Connected"), true);
    QTRY_COMPARE_WITH_TIMEOUT(m_model->headsetName(), QStringLiteral("Arc"), busTimeoutMs);
    buds->calls.clear();
    buds->set(device1(), QStringLiteral("Connected"), true);
    QTRY_VERIFY_WITH_TIMEOUT(buds->calls.contains(QStringLiteral("Disconnect")), busTimeoutMs);
    arc->set(device1(), QStringLiteral("Connected"), false);
    buds->calls.clear();
    m_model->connectHeadset(headsetPath);
    QTRY_COMPARE_WITH_TIMEOUT(m_model->headsetName(), QStringLiteral("Buds"), busTimeoutMs);
    buds->calls.clear();
    m_model->disconnectHeadset(headsetPath);
    QTRY_VERIFY_WITH_TIMEOUT(buds->calls.contains(QStringLiteral("Disconnect")), busTimeoutMs);

    m_model->stopSearch();
    QTRY_VERIFY_WITH_TIMEOUT(!m_model->searching(), busTimeoutMs);
    m_agent->calls.clear();
    m_model->pairDevice(fresh->path());
    QTRY_VERIFY_WITH_TIMEOUT(m_agent->calls.contains(QStringLiteral("StartSearch 120")), busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(fresh->calls.contains(QStringLiteral("Pair")), busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(fresh->calls.contains(QStringLiteral("Connect")), busTimeoutMs);
    QCOMPARE(fresh->get(device1(), QStringLiteral("Trusted")).toBool(), true);
    QTRY_COMPARE_WITH_TIMEOUT(m_model->discovered().size(), 1, busTimeoutMs);
    fresh->set(device1(), QStringLiteral("UUIDs"), QStringList{sink});
    QTRY_COMPARE_WITH_TIMEOUT(m_model->headsets().size(), 3, busTimeoutMs);

    QVERIFY(m_model->searching());
    m_model->stopSearch();
    QTRY_VERIFY_WITH_TIMEOUT(!m_model->searching(), busTimeoutMs);
    m_agent->calls.clear();
    m_model->pairDevice(bad->path());
    m_model->pairDevice(bad->path());
    QTRY_COMPARE_WITH_TIMEOUT(m_agent->calls.count(QStringLiteral("StartSearch 120")), 1, busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(bad->calls.contains(QStringLiteral("Pair")), busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(m_model->discovered().at(0).toMap().value(QStringLiteral("state")).toString(),
                              QStringLiteral("pair failed"), busTimeoutMs);
    m_model->startSearch(30);
    QTRY_COMPARE_WITH_TIMEOUT(m_model->discovered().at(0).toMap().value(QStringLiteral("state")).toString(),
                              QStringLiteral("not paired"), busTimeoutMs);

    const QString later = QStringLiteral("/org/bluez/hci0/dev_01_00_00_00_00_05");
    FakeObject *slow = addDevice(later, QStringLiteral("Late Cans"), false, {sink}, 0);
    QVERIFY(slow);
    m_model->stopSearch();
    QTRY_VERIFY_WITH_TIMEOUT(!m_model->searching(), busTimeoutMs);
    m_model->pairDevice(later);
    m_model->setRole(QStringLiteral("receiver"));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->role(), QStringLiteral("receiver"), busTimeoutMs);
    QTest::qWait(300);
    QVERIFY(!slow->calls.contains(QStringLiteral("Pair")));
    m_model->setRole(QStringLiteral("headphones"));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->role(), QStringLiteral("headphones"), busTimeoutMs);
    m_model->startSearch(30);
    QTRY_VERIFY_WITH_TIMEOUT(m_model->searching(), busTimeoutMs);
    QTest::qWait(300);
    QVERIFY(!slow->calls.contains(QStringLiteral("Pair")));

    m_model->forgetHeadset(arc->path());
    QTRY_VERIFY_WITH_TIMEOUT(m_fake->removedDevices.contains(arc->path()), busTimeoutMs);
    const QString zest = fresh->path();
    QCOMPARE(m_model->lastHeadset(), zest);
    m_model->forgetHeadset(zest);
    QTRY_VERIFY_WITH_TIMEOUT(m_fake->removedDevices.contains(zest), busTimeoutMs);
    QCOMPARE(m_model->lastHeadset(), QString());
    m_model->setRole(QStringLiteral("receiver"));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->role(), QStringLiteral("receiver"), busTimeoutMs);
}

QTEST_GUILESS_MAIN(SystemBluezTest)
#include "tst_systembluez.moc"
