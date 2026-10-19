#pragma once

#include <QDBusConnection>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <memory>

namespace MicroSet {
class Client;
}

namespace microdap {

class Settings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString output READ output WRITE setOutput NOTIFY outputChanged)
    Q_PROPERTY(QStringList roots READ roots WRITE setRoots NOTIFY rootsChanged)
    Q_PROPERTY(int accentHue READ accentHue WRITE setAccentHue NOTIFY accentHueChanged)
    Q_PROPERTY(bool accentFromArt READ accentFromArt WRITE setAccentFromArt NOTIFY accentFromArtChanged)
    Q_PROPERTY(double scale READ scale WRITE setScale NOTIFY scaleChanged)
    Q_PROPERTY(int saverSeconds READ saverSeconds WRITE setSaverSeconds NOTIFY saverSecondsChanged)
    Q_PROPERTY(bool saverLyrics READ saverLyrics WRITE setSaverLyrics NOTIFY saverLyricsChanged)
    Q_PROPERTY(int screenOffSeconds READ screenOffSeconds WRITE setScreenOffSeconds NOTIFY screenOffSecondsChanged)
    Q_PROPERTY(int suspendMinutes READ suspendMinutes WRITE setSuspendMinutes NOTIFY suspendMinutesChanged)
    Q_PROPERTY(bool wifiOffScreenOff READ wifiOffScreenOff WRITE setWifiOffScreenOff NOTIFY wifiOffScreenOffChanged)
    Q_PROPERTY(QString btCodec READ btCodec WRITE setBtCodec NOTIFY btCodecChanged)
    Q_PROPERTY(bool overlayToasts READ overlayToasts WRITE setOverlayToasts NOTIFY overlayToastsChanged)
    Q_PROPERTY(QString hpGain READ hpGain WRITE setHpGain NOTIFY hpGainChanged)
    Q_PROPERTY(QString hpAmp READ hpAmp WRITE setHpAmp NOTIFY hpAmpChanged)
    Q_PROPERTY(int spkGain READ spkGain WRITE setSpkGain NOTIFY spkGainChanged)
    Q_PROPERTY(int volumeStep READ volumeStep WRITE setVolumeStep NOTIFY volumeStepChanged)
    Q_PROPERTY(QString rgb READ rgb WRITE setRgb NOTIFY rgbChanged)
    Q_PROPERTY(QString orientation READ orientation WRITE setOrientation NOTIFY orientationChanged)
    Q_PROPERTY(bool autoRotate READ autoRotate WRITE setAutoRotate NOTIFY autoRotateChanged)
    Q_PROPERTY(bool touch READ touch WRITE setTouch NOTIFY touchChanged)
    Q_PROPERTY(QString outputLevel READ outputLevel WRITE setOutputLevel NOTIFY outputLevelChanged)
    Q_PROPERTY(QString sampleRate READ sampleRate WRITE setSampleRate NOTIFY sampleRateChanged)
    Q_PROPERTY(QString playThrough READ playThrough WRITE setPlayThrough NOTIFY playThroughChanged)
    Q_PROPERTY(QString resume READ resume WRITE setResume NOTIFY resumeChanged)
    Q_PROPERTY(bool autoNowPlaying READ autoNowPlaying WRITE setAutoNowPlaying NOTIFY autoNowPlayingChanged)
    Q_PROPERTY(QString clock READ clock WRITE setClock NOTIFY clockChanged)
    Q_PROPERTY(bool batteryPercent READ batteryPercent WRITE setBatteryPercent NOTIFY batteryPercentChanged)
    Q_PROPERTY(QString bootSession READ bootSession WRITE setBootSession NOTIFY bootSessionChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)

public:
    static constexpr double minScale = 0.8;
    static constexpr double maxScale = 1.4;
    static constexpr int minSaverSeconds = 10;
    static constexpr int maxSaverSeconds = 300;
    static constexpr int defaultAccentHue = 350;

    Settings();
    explicit Settings(MicroSet::Client *client, QObject *parent = nullptr);
    ~Settings() override;

    static QDBusConnection bus();
    static QString legacyPath();
    static QVariantMap legacyValues(const QString &path);
    static QStringList outputs();
    static QStringList codecs();
    static int normalizeHue(int hue);
    static int hueFromColor(const QString &color);
    static QStringList hpGains();
    static QStringList hpAmps();
    static QList<int> spkGains();
    static QList<int> volumeSteps();
    static QStringList rgbModes();
    static QStringList orientations();
    static QStringList outputLevels();
    static QStringList sampleRates();
    static QStringList playThroughModes();
    static QStringList resumeModes();
    static QStringList clockModes();
    Q_INVOKABLE static QStringList defaultRoots();
    static QList<int> saverSteps();
    static QList<int> screenOffSteps();
    static QList<int> suspendSteps();
    static int snap(int value, const QList<int> &steps);
    Q_INVOKABLE static QStringList candidateRoots();

    MicroSet::Client *client() const;
    bool available() const;
    QVariant value(const QString &key) const;
    void setValue(const QString &key, const QVariant &value);
    void importLegacy(const QString &path = legacyPath());

    QString output() const;
    void setOutput(const QString &output);
    QStringList roots() const;
    bool rootsDefault() const;
    void setRoots(const QStringList &roots);
    void refreshRoots();
    int accentHue() const;
    void setAccentHue(int hue);
    bool accentFromArt() const;
    void setAccentFromArt(bool on);
    double scale() const;
    void setScale(double scale);
    int saverSeconds() const;
    void setSaverSeconds(int seconds);
    bool saverLyrics() const;
    void setSaverLyrics(bool on);
    int screenOffSeconds() const;
    void setScreenOffSeconds(int seconds);
    int suspendMinutes() const;
    void setSuspendMinutes(int minutes);
    bool wifiOffScreenOff() const;
    void setWifiOffScreenOff(bool on);
    QString btCodec() const;
    void setBtCodec(const QString &codec);
    bool overlayToasts() const;
    void setOverlayToasts(bool on);
    QString hpGain() const;
    void setHpGain(const QString &gain);
    QString hpAmp() const;
    void setHpAmp(const QString &amp);
    int spkGain() const;
    void setSpkGain(int step);
    int volumeStep() const;
    void setVolumeStep(int step);
    QString rgb() const;
    void setRgb(const QString &mode);
    QString orientation() const;
    void setOrientation(const QString &orientation);
    bool autoRotate() const;
    void setAutoRotate(bool on);
    bool touch() const;
    void setTouch(bool on);
    QString outputLevel() const;
    void setOutputLevel(const QString &level);
    QString sampleRate() const;
    void setSampleRate(const QString &rate);
    QString playThrough() const;
    void setPlayThrough(const QString &mode);
    QString resume() const;
    void setResume(const QString &mode);
    bool autoNowPlaying() const;
    void setAutoNowPlaying(bool on);
    QString clock() const;
    void setClock(const QString &mode);
    bool batteryPercent() const;
    void setBatteryPercent(bool on);
    QString bootSession() const;
    void setBootSession(const QString &name);
    Q_INVOKABLE QStringList bootSessions() const;

signals:
    void outputChanged();
    void rootsChanged();
    void accentHueChanged();
    void accentFromArtChanged();
    void scaleChanged();
    void saverSecondsChanged();
    void saverLyricsChanged();
    void screenOffSecondsChanged();
    void suspendMinutesChanged();
    void wifiOffScreenOffChanged();
    void btCodecChanged();
    void overlayToastsChanged();
    void hpGainChanged();
    void hpAmpChanged();
    void spkGainChanged();
    void volumeStepChanged();
    void rgbChanged();
    void orientationChanged();
    void autoRotateChanged();
    void touchChanged();
    void outputLevelChanged();
    void sampleRateChanged();
    void playThroughChanged();
    void resumeChanged();
    void autoNowPlayingChanged();
    void clockChanged();
    void batteryPercentChanged();
    void bootSessionChanged();
    void availableChanged();
    void valueChanged(const QString &key);
    void imported(bool ok);

private:
    void load();
    void take(const QString &key, const QVariant &value);
    void store(const QString &key, const QVariant &value);
    void refresh(const QString &key);
    void notify(const QString &key);

    std::unique_ptr<MicroSet::Client> m_owned;
    MicroSet::Client *m_client = nullptr;
    QVariantMap m_values;
    bool m_available = false;
    bool m_importing = false;
};

}
