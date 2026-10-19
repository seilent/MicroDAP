#include "BluezModel.h"

#include <QDebug>

#include <QDir>

namespace microdap {

namespace {

int switchMs()
{
    bool ok = false;
    const int value = qEnvironmentVariableIntValue("MICRODAP_BT_SWITCH_MS", &ok);
    return ok && value > 0 ? value : BluezModel::roleSwitchMs;
}

QString roleFor(bool search)
{
    return search ? QStringLiteral("headphones") : QStringLiteral("receiver");
}

}

BluezModel::BluezModel(QObject *parent)
    : QObject(parent)
{
    m_switchTimer.setSingleShot(true);
    connect(&m_switchTimer, &QTimer::timeout, this, [this] {
        qWarning() << "microdap bluez: role switch to" << m_targetRole << "timed out, devices of" << m_role
                   << "still connected";
        m_switchTimer.stop();
        roleDevicesChanged();
    });
}

QStringList BluezModel::roles()
{
    return {QStringLiteral("receiver"), QStringLiteral("headphones")};
}

QString BluezModel::lastPhonePath()
{
    const QString state = qEnvironmentVariable("XDG_STATE_HOME", QDir::homePath() + QStringLiteral("/.local/state"));
    return state + QStringLiteral("/microdap/last-phone");
}

QString BluezModel::lastPhone() const
{
    return m_lastPhone;
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

QString BluezModel::transportState() const
{
    return m_transportState;
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

QString BluezModel::artUrl() const
{
    return m_artUrl;
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

bool BluezModel::searching() const
{
    return m_searching;
}

int BluezModel::searchRemaining() const
{
    return m_searchRemaining;
}

QString BluezModel::role() const
{
    return m_role;
}

QString BluezModel::activeRole() const
{
    return m_switching ? m_targetRole : m_role;
}

bool BluezModel::switching() const
{
    return m_switching;
}

QString BluezModel::headsetName() const
{
    return m_headsetName;
}

void BluezModel::setRole(const QString &role)
{
    if (!roles().contains(role) || role == activeRole())
        return;
    if (m_held != Held::None && roleFor(m_held == Held::Search) != role)
        m_held = Held::None;
    if (role == QLatin1StringView("headphones") && pairing())
        stopPairing();
    else if (role == QLatin1StringView("receiver") && searching())
        stopSearch();
    if (role == m_role) {
        m_switchTimer.stop();
        m_switching = false;
        m_targetRole.clear();
        applyRole(role);
        emit switchingChanged();
        runHeld();
        return;
    }
    qInfo() << "microdap bluez: role" << m_role << "to" << role;
    m_targetRole = role;
    const bool started = !m_switching;
    m_switching = true;
    m_switchTimer.start(switchMs());
    if (started)
        emit switchingChanged();
    applyRole(role);
    dropRole(m_role);
    roleDevicesChanged();
}

void BluezModel::roleDevicesChanged()
{
    if (!m_switching || (m_switchTimer.isActive() && roleDevices(m_role) > 0))
        return;
    m_switchTimer.stop();
    m_role = m_targetRole;
    m_targetRole.clear();
    m_switching = false;
    emit roleChanged();
    emit switchingChanged();
    runHeld();
}

void BluezModel::runHeld()
{
    const Held held = m_held;
    m_held = Held::None;
    if (held == Held::Pairing)
        startPairing(m_heldSeconds);
    else if (held == Held::Search)
        startSearch(m_heldSeconds);
}

void BluezModel::dropHeld(bool search)
{
    if (m_held == (search ? Held::Search : Held::Pairing))
        m_held = Held::None;
}

bool BluezModel::holdWindow(bool search, int timeoutSeconds)
{
    const QString needed = roleFor(search);
    if (!m_switching && m_role == needed)
        return false;
    m_held = search ? Held::Search : Held::Pairing;
    m_heldSeconds = timeoutSeconds;
    setRole(needed);
    return true;
}

bool BluezModel::wrongRoleCheck(const QString &device, bool source, bool sink)
{
    if (m_switching)
        return false;
    QString pivot;
    if (m_role == QLatin1StringView("receiver") && sink && !source)
        pivot = QStringLiteral("headphones");
    else if (m_role == QLatin1StringView("headphones") && source && !sink)
        pivot = QStringLiteral("phone");
    if (pivot.isEmpty())
        return false;
    qInfo() << "microdap bluez: wrong role" << device << "in" << m_role << ", use the" << pivot << "pivot";
    emit wrongRole(device, pivot);
    return true;
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
    if (!name.isEmpty())
        updateLastPhone(name);
}

void BluezModel::updateLastPhone(const QString &name)
{
    if (m_lastPhone == name)
        return;
    m_lastPhone = name;
    emit lastPhoneChanged();
}

void BluezModel::updateTransportActive(bool active)
{
    if (m_transportActive == active)
        return;
    m_transportActive = active;
    emit transportActiveChanged();
}

void BluezModel::updateTransport(bool active, const QString &state)
{
    const bool activeChanged = m_transportActive != active;
    const bool stateChanged = m_transportState != state;
    m_transportActive = active;
    m_transportState = state;
    if (activeChanged)
        emit transportActiveChanged();
    if (stateChanged)
        emit transportStateChanged();
}

void BluezModel::updateTitle(const QString &title)
{
    if (m_title == title)
        return;
    m_title = title;
    emit titleChanged();
}

void BluezModel::updateArtUrl(const QString &url)
{
    if (m_artUrl == url)
        return;
    m_artUrl = url;
    emit artUrlChanged();
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
    const int volume = qBound(0, percent, 100);
    const bool phone = m_volumeKnown && volume != m_volume;
    m_volume = volume;
    m_volumeKnown = true;
    emit volumeChanged(m_volume);
    if (phone)
        emit phoneVolumeChanged(m_volume);
}

void BluezModel::forgetVolume()
{
    m_volumeKnown = false;
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

void BluezModel::updateSearching(bool searching)
{
    if (m_searching == searching)
        return;
    m_searching = searching;
    emit searchingChanged();
}

void BluezModel::updateSearchRemaining(int seconds)
{
    const int value = qMax(0, seconds);
    if (m_searchRemaining == value)
        return;
    m_searchRemaining = value;
    emit searchRemainingChanged();
}

void BluezModel::updateHeadsetName(const QString &name)
{
    if (m_headsetName == name)
        return;
    m_headsetName = name;
    emit headsetNameChanged();
}

}
