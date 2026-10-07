#include "BluezModel.h"

namespace casdap {

BluezModel::BluezModel(QObject *parent)
    : QObject(parent)
{
}

int BluezModel::volumeFromTransport(int raw)
{
    return qRound(qBound(0, raw, transportVolumeMax) * 100.0 / transportVolumeMax);
}

int BluezModel::volumeToTransport(int percent)
{
    return qRound(qBound(0, percent, 100) * transportVolumeMax / 100.0);
}

bool BluezModel::connected() const
{
    return m_connected;
}

QString BluezModel::deviceName() const
{
    return m_deviceName;
}

bool BluezModel::transportActive() const
{
    return m_transportActive;
}

QString BluezModel::title() const
{
    return m_title;
}

QString BluezModel::artist() const
{
    return m_artist;
}

QString BluezModel::album() const
{
    return m_album;
}

int BluezModel::trackNumber() const
{
    return m_trackNumber;
}

int BluezModel::numberOfTracks() const
{
    return m_numberOfTracks;
}

qint64 BluezModel::durationMs() const
{
    return m_durationMs;
}

qint64 BluezModel::positionMs() const
{
    return m_positionMs;
}

QString BluezModel::status() const
{
    return m_status;
}

QString BluezModel::shuffle() const
{
    return m_shuffle;
}

QString BluezModel::repeat() const
{
    return m_repeat;
}

int BluezModel::volume() const
{
    return m_volume;
}

int BluezModel::battery() const
{
    return m_battery;
}

bool BluezModel::pairing() const
{
    return m_pairing;
}

int BluezModel::pairingRemaining() const
{
    return m_pairingRemaining;
}

bool BluezModel::active() const
{
    return m_active;
}

void BluezModel::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    emit activeChanged();
}

bool BluezModel::ready() const
{
    return m_ready;
}

void BluezModel::updateReady(bool ready)
{
    if (m_ready == ready)
        return;
    m_ready = ready;
    emit readyChanged();
}

void BluezModel::updateConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    emit connectedChanged();
}

void BluezModel::updateDeviceName(const QString &name)
{
    if (m_deviceName == name)
        return;
    m_deviceName = name;
    emit deviceNameChanged();
}

void BluezModel::updateTransportActive(bool active)
{
    if (m_transportActive == active)
        return;
    m_transportActive = active;
    emit transportActiveChanged();
}

void BluezModel::updateTitle(const QString &title)
{
    if (m_title == title)
        return;
    m_title = title;
    emit titleChanged();
}

void BluezModel::updateArtist(const QString &artist)
{
    if (m_artist == artist)
        return;
    m_artist = artist;
    emit artistChanged();
}

void BluezModel::updateAlbum(const QString &album)
{
    if (m_album == album)
        return;
    m_album = album;
    emit albumChanged();
}

void BluezModel::updateTrackNumber(int number)
{
    if (m_trackNumber == number)
        return;
    m_trackNumber = number;
    emit trackNumberChanged();
}

void BluezModel::updateNumberOfTracks(int count)
{
    if (m_numberOfTracks == count)
        return;
    m_numberOfTracks = count;
    emit numberOfTracksChanged();
}

void BluezModel::updateDurationMs(qint64 ms)
{
    if (m_durationMs == ms)
        return;
    m_durationMs = ms;
    emit durationMsChanged();
}

void BluezModel::updatePositionMs(qint64 ms)
{
    if (m_positionMs == ms)
        return;
    m_positionMs = ms;
    emit positionMsChanged();
}

void BluezModel::updateStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void BluezModel::updateShuffle(const QString &shuffle)
{
    if (m_shuffle == shuffle)
        return;
    m_shuffle = shuffle;
    emit shuffleChanged();
}

void BluezModel::updateRepeat(const QString &repeat)
{
    if (m_repeat == repeat)
        return;
    m_repeat = repeat;
    emit repeatChanged();
}

void BluezModel::updateVolume(int percent)
{
    m_volume = qBound(0, percent, 100);
    emit volumeChanged(m_volume);
}

void BluezModel::updateBattery(int percent)
{
    const int value = percent < 0 ? -1 : qMin(percent, 100);
    if (m_battery == value)
        return;
    m_battery = value;
    emit batteryChanged();
}

void BluezModel::updatePairing(bool pairing)
{
    if (m_pairing == pairing)
        return;
    m_pairing = pairing;
    emit pairingChanged();
}

void BluezModel::updatePairingRemaining(int seconds)
{
    const int value = qMax(0, seconds);
    if (m_pairingRemaining == value)
        return;
    m_pairingRemaining = value;
    emit pairingRemainingChanged();
}

}
