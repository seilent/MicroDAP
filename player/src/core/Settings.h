#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

namespace casdap {

class Settings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString output READ output WRITE setOutput NOTIFY outputChanged)
    Q_PROPERTY(QStringList roots READ roots WRITE setRoots NOTIFY rootsChanged)
    Q_PROPERTY(int accentHue READ accentHue WRITE setAccentHue NOTIFY accentHueChanged)
    Q_PROPERTY(bool accentFromArt READ accentFromArt WRITE setAccentFromArt NOTIFY accentFromArtChanged)
    Q_PROPERTY(double scale READ scale WRITE setScale NOTIFY scaleChanged)
    Q_PROPERTY(int saverSeconds READ saverSeconds WRITE setSaverSeconds NOTIFY saverSecondsChanged)
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
    Q_PROPERTY(QString eqHeadphones READ eqHeadphones WRITE setEqHeadphones NOTIFY eqHeadphonesChanged)
    Q_PROPERTY(QString eqUsb READ eqUsb WRITE setEqUsb NOTIFY eqUsbChanged)
    Q_PROPERTY(QString eqSpeaker READ eqSpeaker WRITE setEqSpeaker NOTIFY eqSpeakerChanged)
    Q_PROPERTY(QString path READ path CONSTANT)

public:
    static constexpr double minScale = 0.8;
    static constexpr double maxScale = 1.4;
    static constexpr int minSaverSeconds = 10;
    static constexpr int maxSaverSeconds = 300;
    static constexpr int syncDelayMs = 500;
    static constexpr int defaultAccentHue = 350;

    explicit Settings(QObject *parent = nullptr);
    explicit Settings(const QString &path, QObject *parent = nullptr);
    ~Settings() override;

    static QString defaultPath();
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
    static QStringList defaultRoots();
    Q_INVOKABLE static QStringList candidateRoots();

    QString path() const;

    QString output() const;
    void setOutput(const QString &output);
    QStringList roots() const;
    void setRoots(const QStringList &roots);
    int accentHue() const;
    void setAccentHue(int hue);
    bool accentFromArt() const;
    void setAccentFromArt(bool on);
    double scale() const;
    void setScale(double scale);
    int saverSeconds() const;
    void setSaverSeconds(int seconds);
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
    QString eqHeadphones() const;
    void setEqHeadphones(const QString &key);
    QString eqUsb() const;
    void setEqUsb(const QString &key);
    QString eqSpeaker() const;
    void setEqSpeaker(const QString &key);

    static QString normalizeEq(const QString &key);
    Q_INVOKABLE QString eq(const QString &output) const;
    Q_INVOKABLE void setEq(const QString &output, const QString &key);

    Q_INVOKABLE void sync();

signals:
    void outputChanged();
    void rootsChanged();
    void accentHueChanged();
    void accentFromArtChanged();
    void scaleChanged();
    void saverSecondsChanged();
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
    void eqHeadphonesChanged();
    void eqUsbChanged();
    void eqSpeakerChanged();

private:
    void load();
    void store(const QString &key, const QVariant &value);

    QSettings m_settings;
    QTimer m_syncTimer;
    QVariantMap m_dirty;
    QString m_output;
    QStringList m_roots;
    int m_accentHue = defaultAccentHue;
    bool m_accentFromArt = false;
    bool m_dropAccent = false;
    double m_scale = 1.0;
    int m_saverSeconds = 30;
    int m_screenOffSeconds = 300;
    int m_suspendMinutes = 10;
    bool m_wifiOffScreenOff = false;
    QString m_btCodec;
    bool m_overlayToasts = false;
    QString m_hpGain;
    QString m_hpAmp;
    int m_spkGain = 102;
    int m_volumeStep = 5;
    QString m_rgb;
    QString m_orientation;
    bool m_autoRotate = false;
    QString m_eqHeadphones;
    QString m_eqUsb;
    QString m_eqSpeaker;
};

}
