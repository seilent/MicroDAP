#pragma once

#include "BluezModel.h"

#include <QMap>
#include <QStringList>
#include <QTemporaryDir>

#include <memory>

namespace microdap {

class FakeBluezModel : public BluezModel
{
    Q_OBJECT

public:
    explicit FakeBluezModel(QObject *parent = nullptr);

    static QStringList presets();
    bool applyPreset(const QString &name);

    QStringList commands() const;
    void clearCommands();

    void setConnected(bool connected);
    void setReady(bool ready);
    void setDeviceName(const QString &name);
    void setLastPhone(const QString &name);
    void setTransportActive(bool active);
    void setTransportState(const QString &state);
    void setTrack(const QString &title, const QString &artist, const QString &album,
                  int trackNumber, int numberOfTracks, qint64 durationMs);
    void setTitle(const QString &title);
    void setArtist(const QString &artist);
    void setAlbum(const QString &album);
    void setArtUrl(const QString &url);
    void setTrackNumber(int number);
    void setNumberOfTracks(int count);
    void setDurationMs(qint64 ms);
    void setPositionMs(qint64 ms);
    void setStatus(const QString &status);
    void setTransportVolume(int raw);
    void setVolume(int percent);
    void setBattery(int percent);
    void setPairing(bool pairing);
    void setPairingRemaining(int seconds);
    void setSearchRemaining(int seconds);
    void setHeadsetName(const QString &name);
    void setRoleDevices(const QString &role, int count);
    bool reportDevice(const QString &device, bool source, bool sink);

    void play() override;
    void pause() override;
    void next() override;
    void previous() override;
    void rewind() override;
    void fastForward() override;
    void startPairing(int timeoutSeconds = defaultPairingTimeout) override;
    void stopPairing() override;
    void startSearch(int timeoutSeconds = defaultPairingTimeout) override;
    void stopSearch() override;
    void setShuffle(const QString &shuffle) override;
    void setRepeat(const QString &repeat) override;

protected:
    int roleDevices(const QString &role) const override;
    void dropRole(const QString &role) override;
    void applyRole(const QString &role) override;

private:
    void clearAll();
    QString sampleArt();

    QStringList m_commands;
    QMap<QString, int> m_roleDevices;
    std::unique_ptr<QTemporaryDir> m_artDir;
};

}
