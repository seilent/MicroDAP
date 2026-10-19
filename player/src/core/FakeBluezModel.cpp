#include "FakeBluezModel.h"

#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QUrl>

namespace microdap {

namespace {

constexpr qint64 presetPositionMs = 125000;
constexpr qint64 presetDurationMs = 228000;
constexpr int presetPairingRemaining = 73;
constexpr int sampleArtSide = 200;

}

FakeBluezModel::FakeBluezModel(QObject *parent)
    : BluezModel(parent)
{
}

QStringList FakeBluezModel::presets()
{
    return {
        QStringLiteral("off"),
        QStringLiteral("connected"),
        QStringLiteral("active"),
        QStringLiteral("art"),
        QStringLiteral("pairing"),
        QStringLiteral("last"),
        QStringLiteral("headphones"),
        QStringLiteral("search"),
        QStringLiteral("appidle"),
        QStringLiteral("appopen"),
        QStringLiteral("appcode"),
        QStringLiteral("appoff"),
    };
}

void FakeBluezModel::clearAll()
{
    setTransportState(QStringLiteral("none"));
    setStatus(QStringLiteral("stopped"));
    setTrack({}, {}, {}, 0, 0, 0);
    setArtUrl({});
    setPositionMs(0);
    setBattery(-1);
    setPairing(false);
    setPairingRemaining(0);
    setPairingMode({});
    setConfirm(false, 0, {});
    updateReady(true);
    setConnected(false);
    setDeviceName({});
    forgetVolume();
    updateSearching(false);
    updateSearchRemaining(0);
    setHeadsets({});
    setDiscovered({});
    setHeadsetName({});
    setRole(QStringLiteral("receiver"));
}

namespace {

QVariantMap headsetRow(const QString &path, const QString &name, bool connected, const QString &state)
{
    return {{QStringLiteral("path"), path}, {QStringLiteral("name"), name},
            {QStringLiteral("connected"), connected}, {QStringLiteral("state"), state}};
}

}

bool FakeBluezModel::applyPreset(const QString &name)
{
    if (name == QLatin1StringView("off")) {
        clearAll();
        return true;
    }
    if (name == QLatin1StringView("connected")) {
        clearAll();
        setDeviceName(QStringLiteral("Pixel 8"));
        setBattery(73);
        setConnected(true);
        return true;
    }
    if (name == QLatin1StringView("active") || name == QLatin1StringView("art")) {
        clearAll();
        setDeviceName(QStringLiteral("Pixel 8"));
        setBattery(73);
        setConnected(true);
        setTrack(QStringLiteral("Track Four"), QStringLiteral("Phone Artist"),
                 QStringLiteral("Phone Album"), 4, 11, presetDurationMs);
        setPositionMs(presetPositionMs);
        setStatus(QStringLiteral("playing"));
        setTransportVolume(64);
        setTransportState(QStringLiteral("active"));
        if (name == QLatin1StringView("art"))
            setArtUrl(sampleArt());
        return true;
    }
    if (name == QLatin1StringView("headphones") || name == QLatin1StringView("search")) {
        clearAll();
        setRole(QStringLiteral("headphones"));
        setHeadsets({headsetRow(QStringLiteral("/org/bluez/hci0/dev_10_00_00_00_00_01"), QStringLiteral("Buds Pro"),
                                false, QStringLiteral("disconnected")),
                     headsetRow(QStringLiteral("/org/bluez/hci0/dev_10_00_00_00_00_02"), QStringLiteral("WH-1000XM5"),
                                true, QStringLiteral("audio active"))});
        setHeadsetName(QStringLiteral("WH-1000XM5"));
        if (name == QLatin1StringView("search")) {
            setDiscovered({headsetRow(QStringLiteral("/org/bluez/hci0/dev_20_00_00_00_00_01"),
                                      QStringLiteral("JBL Tune 760NC"), false, QStringLiteral("pair failed")),
                           headsetRow(QStringLiteral("/org/bluez/hci0/dev_20_00_00_00_00_02"),
                                      QStringLiteral("Soundcore Life Q30"), false, QStringLiteral("not paired"))});
            updateSearching(true);
            updateSearchRemaining(presetPairingRemaining);
        }
        return true;
    }
    if (name == QLatin1StringView("last")) {
        clearAll();
        setLastPhone(QStringLiteral("Pixel 8"));
        return true;
    }
    if (name == QLatin1StringView("appidle")) {
        clearAll();
        return true;
    }
    if (name == QLatin1StringView("appopen")) {
        clearAll();
        setPairingMode(QStringLiteral("companion"));
        setPairing(true);
        setPairingRemaining(presetPairingRemaining);
        return true;
    }
    if (name == QLatin1StringView("appcode")) {
        clearAll();
        setPairingMode(QStringLiteral("companion"));
        setPairing(true);
        setPairingRemaining(presetPairingRemaining);
        setConfirm(true, 482915, QStringLiteral("Pixel 8"));
        return true;
    }
    if (name == QLatin1StringView("appoff")) {
        clearAll();
        updateReady(false);
        return true;
    }
    if (name == QLatin1StringView("pairing")) {
        clearAll();
        setPairing(true);
        setPairingRemaining(presetPairingRemaining);
        return true;
    }
    return false;
}

QStringList FakeBluezModel::commands() const
{
    return m_commands;
}

void FakeBluezModel::clearCommands()
{
    m_commands.clear();
}

void FakeBluezModel::setConnected(bool connected)
{
    updateConnected(connected);
}

void FakeBluezModel::setReady(bool ready)
{
    updateReady(ready);
}

void FakeBluezModel::setDeviceName(const QString &name)
{
    updateDeviceName(name);
}

void FakeBluezModel::setLastPhone(const QString &name)
{
    updateLastPhone(name);
}

void FakeBluezModel::setTransportActive(bool active)
{
    updateTransportActive(active);
}

void FakeBluezModel::setTransportState(const QString &state)
{
    updateTransport(state == QLatin1StringView("active"), state);
}

void FakeBluezModel::setTrack(const QString &title, const QString &artist, const QString &album,
                              int trackNumber, int numberOfTracks, qint64 durationMs)
{
    updateTitle(title);
    updateArtist(artist);
    updateAlbum(album);
    updateTrackNumber(trackNumber);
    updateNumberOfTracks(numberOfTracks);
    updateDurationMs(durationMs);
}

void FakeBluezModel::setTitle(const QString &title)
{
    updateTitle(title);
}

void FakeBluezModel::setArtist(const QString &artist)
{
    updateArtist(artist);
}

void FakeBluezModel::setAlbum(const QString &album)
{
    updateAlbum(album);
}

void FakeBluezModel::setArtUrl(const QString &url)
{
    updateArtUrl(url);
}

QString FakeBluezModel::sampleArt()
{
    if (!m_artDir)
        m_artDir = std::make_unique<QTemporaryDir>();
    if (!m_artDir->isValid())
        return {};
    const QString path = m_artDir->filePath(QStringLiteral("phone.jpg"));
    QImage image(sampleArtSide, sampleArtSide, QImage::Format_RGB32);
    QPainter painter(&image);
    QLinearGradient gradient(0, 0, sampleArtSide, sampleArtSide);
    gradient.setColorAt(0, QColor(0x2a, 0x4d, 0x69));
    gradient.setColorAt(1, QColor(0xd9, 0x7b, 0x3f));
    painter.fillRect(image.rect(), gradient);
    painter.end();
    if (!image.save(path, "JPEG"))
        return {};
    return QUrl::fromLocalFile(path).toString();
}

void FakeBluezModel::setTrackNumber(int number)
{
    updateTrackNumber(number);
}

void FakeBluezModel::setNumberOfTracks(int count)
{
    updateNumberOfTracks(count);
}

void FakeBluezModel::setDurationMs(qint64 ms)
{
    updateDurationMs(ms);
}

void FakeBluezModel::setPositionMs(qint64 ms)
{
    updatePositionMs(ms);
}

void FakeBluezModel::setStatus(const QString &status)
{
    updateStatus(status);
}

void FakeBluezModel::setTransportVolume(int raw)
{
    updateVolume(volumeFromTransport(raw));
}

void FakeBluezModel::setVolume(int percent)
{
    updateVolume(percent);
}

void FakeBluezModel::setBattery(int percent)
{
    updateBattery(percent);
}

void FakeBluezModel::setPairing(bool pairing)
{
    updatePairing(pairing);
}

void FakeBluezModel::setPairingRemaining(int seconds)
{
    updatePairingRemaining(seconds);
}

void FakeBluezModel::play()
{
    m_commands.append(QStringLiteral("play"));
    updateStatus(QStringLiteral("playing"));
}

void FakeBluezModel::pause()
{
    m_commands.append(QStringLiteral("pause"));
    updateStatus(QStringLiteral("paused"));
}

void FakeBluezModel::next()
{
    m_commands.append(QStringLiteral("next"));
    if (numberOfTracks() > 0 && trackNumber() < numberOfTracks())
        updateTrackNumber(trackNumber() + 1);
    updatePositionMs(0);
}

void FakeBluezModel::previous()
{
    m_commands.append(QStringLiteral("previous"));
    if (trackNumber() > 1)
        updateTrackNumber(trackNumber() - 1);
    updatePositionMs(0);
}

void FakeBluezModel::rewind()
{
    m_commands.append(QStringLiteral("rewind"));
    updateStatus(QStringLiteral("reverse-seek"));
}

void FakeBluezModel::fastForward()
{
    m_commands.append(QStringLiteral("fastForward"));
    updateStatus(QStringLiteral("forward-seek"));
}

void FakeBluezModel::startPairing(int timeoutSeconds)
{
    if (holdWindow(false, timeoutSeconds))
        return;
    m_commands.append(QStringLiteral("startPairing"));
    updatePairingMode(QStringLiteral("audio"));
    updatePairingRemaining(timeoutSeconds);
    updatePairing(true);
}

void FakeBluezModel::stopPairing()
{
    dropHeld(false);
    m_commands.append(QStringLiteral("stopPairing"));
    updatePairing(false);
    updatePairingRemaining(0);
    updatePairingMode({});
    updateConfirm(false, 0, {});
}

void FakeBluezModel::startCompanionPairing(int timeoutSeconds)
{
    m_commands.append(QStringLiteral("startCompanionPairing"));
    updatePairingMode(QStringLiteral("companion"));
    updatePairingRemaining(timeoutSeconds);
    updatePairing(true);
}

void FakeBluezModel::answerConfirm(int passkey, bool yes)
{
    m_commands.append(QStringLiteral("answerConfirm %1 %2").arg(passkey).arg(yes ? QStringLiteral("yes") : QStringLiteral("no")));
    if (passkey == confirmPasskey())
        updateConfirm(false, 0, {});
}

void FakeBluezModel::setCompanionSets(const QStringList &known, const QStringList &removing)
{
    updateCompanionSets(known, removing);
}

void FakeBluezModel::setPairingMode(const QString &mode)
{
    updatePairingMode(mode);
}

void FakeBluezModel::setConfirm(bool pending, int passkey, const QString &name)
{
    updateConfirm(pending, passkey, name);
}

void FakeBluezModel::bindCompanion(const QString &device)
{
    updatePairing(false);
    updatePairingRemaining(0);
    updatePairingMode({});
    emit companionPaired(device);
}

void FakeBluezModel::startSearch(int timeoutSeconds)
{
    if (holdWindow(true, timeoutSeconds))
        return;
    m_commands.append(QStringLiteral("startSearch"));
    updateSearchRemaining(timeoutSeconds);
    updateSearching(true);
}

void FakeBluezModel::stopSearch()
{
    dropHeld(true);
    m_commands.append(QStringLiteral("stopSearch"));
    updateSearching(false);
    updateSearchRemaining(0);
}

void FakeBluezModel::setSearchRemaining(int seconds)
{
    updateSearchRemaining(seconds);
}

void FakeBluezModel::setHeadsetName(const QString &name)
{
    updateHeadsetName(name);
}

void FakeBluezModel::setRoleDevices(const QString &role, int count)
{
    m_roleDevices.insert(role, count);
    roleDevicesChanged();
}

bool FakeBluezModel::reportDevice(const QString &device, bool source, bool sink, bool target)
{
    return wrongRoleCheck(device, source, sink, target);
}

void FakeBluezModel::setHeadsets(const QVariantList &headsets)
{
    updateHeadsets(headsets);
}

void FakeBluezModel::setDiscovered(const QVariantList &discovered)
{
    updateDiscovered(discovered);
}

void FakeBluezModel::connectHeadset(const QString &path)
{
    if (holdDevice(false, path))
        return;
    m_commands.append(QStringLiteral("connectHeadset ") + path);
}

void FakeBluezModel::disconnectHeadset(const QString &path)
{
    m_commands.append(QStringLiteral("disconnectHeadset ") + path);
}

void FakeBluezModel::forgetHeadset(const QString &path)
{
    m_commands.append(QStringLiteral("forgetHeadset ") + path);
}

void FakeBluezModel::want()
{
    m_commands.append(QStringLiteral("want"));
}

void FakeBluezModel::setReceiverScope(bool held)
{
    m_commands.append(QStringLiteral("scope ") + (held ? QStringLiteral("on") : QStringLiteral("off")));
}

void FakeBluezModel::pairDevice(const QString &path)
{
    if (holdDevice(true, path))
        return;
    m_commands.append(QStringLiteral("pairDevice ") + path);
}

int FakeBluezModel::roleDevices(const QString &role) const
{
    return m_roleDevices.value(role);
}

void FakeBluezModel::dropRole(const QString &role)
{
    m_commands.append(QStringLiteral("dropRole ") + role);
}

void FakeBluezModel::applyRole(const QString &role)
{
    m_commands.append(QStringLiteral("applyRole ") + role);
}

void FakeBluezModel::setShuffle(const QString &shuffle)
{
    updateShuffle(shuffle);
}

void FakeBluezModel::setRepeat(const QString &repeat)
{
    updateRepeat(repeat);
}

}
