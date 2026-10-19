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
    if (name == QLatin1StringView("last")) {
        clearAll();
        setLastPhone(QStringLiteral("Pixel 8"));
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
