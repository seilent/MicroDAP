#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace microdap {

class BluezModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceNameChanged)
    Q_PROPERTY(QString lastPhone READ lastPhone NOTIFY lastPhoneChanged)
    Q_PROPERTY(bool transportActive READ transportActive NOTIFY transportActiveChanged)
    Q_PROPERTY(QString transportState READ transportState NOTIFY transportStateChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY artistChanged)
    Q_PROPERTY(QString album READ album NOTIFY albumChanged)
    Q_PROPERTY(QString artUrl READ artUrl NOTIFY artUrlChanged)
    Q_PROPERTY(int trackNumber READ trackNumber NOTIFY trackNumberChanged)
    Q_PROPERTY(int numberOfTracks READ numberOfTracks NOTIFY numberOfTracksChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY durationMsChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY positionMsChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(QString repeat READ repeat WRITE setRepeat NOTIFY repeatChanged)
    Q_PROPERTY(int volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(int battery READ battery NOTIFY batteryChanged)
    Q_PROPERTY(bool pairing READ pairing NOTIFY pairingChanged)
    Q_PROPERTY(int pairingRemaining READ pairingRemaining NOTIFY pairingRemainingChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(int searchRemaining READ searchRemaining NOTIFY searchRemainingChanged)
    Q_PROPERTY(QString role READ role NOTIFY roleChanged)
    Q_PROPERTY(QString activeRole READ activeRole NOTIFY switchingChanged)
    Q_PROPERTY(bool switching READ switching NOTIFY switchingChanged)
    Q_PROPERTY(QString headsetName READ headsetName NOTIFY headsetNameChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)

public:
    static constexpr int transportVolumeMax = 127;
    static constexpr int defaultPairingTimeout = 120;
    static constexpr int roleSwitchMs = 5000;

    explicit BluezModel(QObject *parent = nullptr);

    static int volumeFromTransport(int raw);
    static int volumeToTransport(int percent);
    static QStringList roles();

    bool connected() const;
    QString deviceName() const;
    QString lastPhone() const;
    static QString lastPhonePath();
    bool transportActive() const;
    QString transportState() const;
    QString title() const;
    QString artist() const;
    QString album() const;
    QString artUrl() const;
    int trackNumber() const;
    int numberOfTracks() const;
    qint64 durationMs() const;
    qint64 positionMs() const;
    QString status() const;
    QString shuffle() const;
    QString repeat() const;
    int volume() const;
    int battery() const;
    bool pairing() const;
    int pairingRemaining() const;
    bool searching() const;
    int searchRemaining() const;
    QString role() const;
    QString activeRole() const;
    bool switching() const;
    QString headsetName() const;
    bool active() const;
    void setActive(bool active);
    bool ready() const;

    Q_INVOKABLE virtual void play() = 0;
    Q_INVOKABLE virtual void pause() = 0;
    Q_INVOKABLE virtual void next() = 0;
    Q_INVOKABLE virtual void previous() = 0;
    Q_INVOKABLE virtual void rewind() = 0;
    Q_INVOKABLE virtual void fastForward() = 0;
    Q_INVOKABLE virtual void startPairing(int timeoutSeconds = defaultPairingTimeout) = 0;
    Q_INVOKABLE virtual void stopPairing() = 0;
    Q_INVOKABLE virtual void startSearch(int timeoutSeconds = defaultPairingTimeout) = 0;
    Q_INVOKABLE virtual void stopSearch() = 0;
    Q_INVOKABLE void setRole(const QString &role);
    virtual void setShuffle(const QString &shuffle) = 0;
    virtual void setRepeat(const QString &repeat) = 0;

signals:
    void connectedChanged();
    void deviceNameChanged();
    void lastPhoneChanged();
    void transportActiveChanged();
    void transportStateChanged();
    void titleChanged();
    void artistChanged();
    void albumChanged();
    void artUrlChanged();
    void trackNumberChanged();
    void numberOfTracksChanged();
    void durationMsChanged();
    void positionMsChanged();
    void statusChanged();
    void shuffleChanged();
    void repeatChanged();
    void volumeChanged(int volume);
    void phoneVolumeChanged(int volume);
    void batteryChanged();
    void pairingChanged();
    void pairingRemainingChanged();
    void searchingChanged();
    void searchRemainingChanged();
    void roleChanged();
    void switchingChanged();
    void headsetNameChanged();
    void wrongRole(const QString &device, const QString &pivot);
    void activeChanged();
    void readyChanged();

protected:
    void updateReady(bool ready);
    void updateConnected(bool connected);
    void updateLastPhone(const QString &name);
    void updateDeviceName(const QString &name);
    void updateTransportActive(bool active);
    void updateTransport(bool active, const QString &state);
    void updateTitle(const QString &title);
    void updateArtist(const QString &artist);
    void updateAlbum(const QString &album);
    void updateArtUrl(const QString &url);
    void updateTrackNumber(int number);
    void updateNumberOfTracks(int count);
    void updateDurationMs(qint64 ms);
    void updatePositionMs(qint64 ms);
    void updateStatus(const QString &status);
    void updateShuffle(const QString &shuffle);
    void updateRepeat(const QString &repeat);
    void updateVolume(int percent);
    void forgetVolume();
    void updateBattery(int percent);
    void updatePairing(bool pairing);
    void updatePairingRemaining(int seconds);
    void updateSearching(bool searching);
    void updateSearchRemaining(int seconds);
    void updateHeadsetName(const QString &name);
    bool holdWindow(bool search, int timeoutSeconds);
    void dropHeld(bool search);
    bool wrongRoleCheck(const QString &device, bool source, bool sink);
    void roleDevicesChanged();
    void runHeld();
    virtual int roleDevices(const QString &role) const = 0;
    virtual void dropRole(const QString &role) = 0;
    virtual void applyRole(const QString &role) = 0;

private:
    bool m_connected = false;
    QString m_deviceName;
    QString m_lastPhone;
    bool m_transportActive = false;
    QString m_transportState = QStringLiteral("none");
    QString m_title;
    QString m_artist;
    QString m_album;
    QString m_artUrl;
    int m_trackNumber = 0;
    int m_numberOfTracks = 0;
    qint64 m_durationMs = 0;
    qint64 m_positionMs = 0;
    QString m_status = QStringLiteral("stopped");
    QString m_shuffle = QStringLiteral("off");
    QString m_repeat = QStringLiteral("off");
    int m_volume = 0;
    bool m_volumeKnown = false;
    int m_battery = -1;
    bool m_pairing = false;
    int m_pairingRemaining = 0;
    bool m_searching = false;
    int m_searchRemaining = 0;
    QString m_role = QStringLiteral("receiver");
    QString m_targetRole;
    bool m_switching = false;
    QString m_headsetName;
    enum class Held { None, Pairing, Search };
    Held m_held = Held::None;
    int m_heldSeconds = 0;
    QTimer m_switchTimer;
    bool m_active = true;
    bool m_ready = true;
};

}
