#pragma once

#include "BluezModel.h"

#include <QStringList>

namespace casdap {

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
    void setDeviceName(const QString &name);
    void setTransportActive(bool active);
    void setTrack(const QString &title, const QString &artist, const QString &album,
                  int trackNumber, int numberOfTracks, qint64 durationMs);
    void setTitle(const QString &title);
    void setArtist(const QString &artist);
    void setAlbum(const QString &album);
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

    void play() override;
    void pause() override;
    void next() override;
    void previous() override;
    void rewind() override;
    void fastForward() override;
    void startPairing(int timeoutSeconds = defaultPairingTimeout) override;
    void stopPairing() override;
    void setShuffle(const QString &shuffle) override;
    void setRepeat(const QString &repeat) override;

private:
    void clearAll();

    QStringList m_commands;
};

}
