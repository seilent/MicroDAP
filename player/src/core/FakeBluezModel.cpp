#include "FakeBluezModel.h"

namespace microdap {

namespace {

constexpr qint64 presetPositionMs = 125000;
constexpr qint64 presetDurationMs = 228000;
constexpr int presetPairingRemaining = 73;

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
        QStringLiteral("pairing"),
    };
}

void FakeBluezModel::clearAll()
{
    setTransportState(QStringLiteral("none"));
    setStatus(QStringLiteral("stopped"));
    setTrack({}, {}, {}, 0, 0, 0);
    setPositionMs(0);
    setBattery(-1);
    setPairing(false);
    setPairingRemaining(0);
    setConnected(false);
    setDeviceName({});
    forgetVolume();
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
    if (name == QLatin1StringView("active")) {
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
    m_commands.append(QStringLiteral("startPairing"));
    updatePairingRemaining(timeoutSeconds);
    updatePairing(true);
}

void FakeBluezModel::stopPairing()
{
    m_commands.append(QStringLiteral("stopPairing"));
    updatePairing(false);
    updatePairingRemaining(0);
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
