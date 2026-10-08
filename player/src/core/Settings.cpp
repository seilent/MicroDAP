#include "Settings.h"

#include "Scanner.h"

#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace casdap {

namespace {

QString keyOutput()
{
    return QStringLiteral("output");
}

QString keyRoots()
{
    return QStringLiteral("roots");
}

QString keyAccent()
{
    return QStringLiteral("accent");
}

QString keyAccentHue()
{
    return QStringLiteral("accentHue");
}

QString keyAccentFromArt()
{
    return QStringLiteral("accentFromArt");
}

QString keyScale()
{
    return QStringLiteral("scale");
}

QString keySaverSeconds()
{
    return QStringLiteral("saverSeconds");
}

QString keyScreenOffSeconds()
{
    return QStringLiteral("screenOffSeconds");
}

QString keySuspendMinutes()
{
    return QStringLiteral("suspendMinutes");
}

QString keyWifiOffScreenOff()
{
    return QStringLiteral("wifiOffScreenOff");
}

QString keyBtCodec()
{
    return QStringLiteral("btCodec");
}

QString keyOverlayToasts()
{
    return QStringLiteral("overlayToasts");
}

QString keyHpGain()
{
    return QStringLiteral("hpGain");
}

QString keyHpAmp()
{
    return QStringLiteral("hpAmp");
}

QString keySpkGain()
{
    return QStringLiteral("spkGain");
}

QString keyVolumeStep()
{
    return QStringLiteral("volumeStep");
}

QString keyRgb()
{
    return QStringLiteral("rgb");
}

QString keyOrientation()
{
    return QStringLiteral("orientation");
}

QString keyAutoRotate()
{
    return QStringLiteral("autoRotate");
}

QString keyEqHeadphones()
{
    return QStringLiteral("eqHeadphones");
}

QString keyEqUsb()
{
    return QStringLiteral("eqUsb");
}

QString keyEqSpeaker()
{
    return QStringLiteral("eqSpeaker");
}

double normalizeScale(double scale)
{
    const double stepped = qRound(scale * 10.0) / 10.0;
    return qBound(Settings::minScale, stepped, Settings::maxScale);
}

int normalizeSaver(int seconds)
{
    return qBound(Settings::minSaverSeconds, seconds, Settings::maxSaverSeconds);
}

}

Settings::Settings(QObject *parent)
    : Settings(defaultPath(), parent)
{
}

Settings::Settings(const QString &path, QObject *parent)
    : QObject(parent)
    , m_settings(path, QSettings::IniFormat)
{
    m_syncTimer.setSingleShot(true);
    m_syncTimer.setInterval(syncDelayMs);
    connect(&m_syncTimer, &QTimer::timeout, this, &Settings::sync);
    if (QCoreApplication *app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this, &Settings::sync);
    load();
}

Settings::~Settings()
{
    sync();
}

QString Settings::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/casdap/settings.ini");
}

QStringList Settings::outputs()
{
    return {QStringLiteral("auto"), QStringLiteral("speaker"), QStringLiteral("headphones"), QStringLiteral("usb")};
}

QStringList Settings::codecs()
{
    return {QStringLiteral("auto"), QStringLiteral("aptx"), QStringLiteral("aac"),
            QStringLiteral("ldac"), QStringLiteral("sbc")};
}

int Settings::normalizeHue(int hue)
{
    const int stepped = qRound(hue / 10.0) * 10;
    return ((stepped % 360) + 360) % 360;
}

int Settings::hueFromColor(const QString &color)
{
    const QColor value = QColor::fromString(color);
    if (!value.isValid())
        return defaultAccentHue;
    const float hue = value.hsvHueF();
    if (hue < 0)
        return defaultAccentHue;
    return ((qRound(hue * 36.0f) * 10) % 360 + 360) % 360;
}

QStringList Settings::hpGains()
{
    return {QStringLiteral("low"), QStringLiteral("normal"), QStringLiteral("high")};
}

QStringList Settings::hpAmps()
{
    return {QStringLiteral("lohifi"), QStringLiteral("ulp"), QStringLiteral("hifi")};
}

QList<int> Settings::spkGains()
{
    return {84, 90, 96, 99, 102, 105, 108};
}

QList<int> Settings::volumeSteps()
{
    return {2, 5, 10};
}

QStringList Settings::rgbModes()
{
    return {QStringLiteral("accent"), QStringLiteral("off"), QStringLiteral("armada")};
}

QStringList Settings::orientations()
{
    return {QStringLiteral("portrait"), QStringLiteral("portrait-flipped"), QStringLiteral("landscape"),
            QStringLiteral("landscape-flipped")};
}

QStringList Settings::defaultRoots()
{
    return {QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
            QStringLiteral("/run/media")};
}

QStringList Settings::candidateRoots()
{
    QStringList out = defaultRoots();
    const auto offer = [&out](const QString &mount) {
        out.append(mount);
        const QString music = mount + QStringLiteral("/Music");
        if (QFileInfo(music).isDir())
            out.append(music);
    };
    const QFileInfoList children = QDir(Scanner::mediaBase()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &child : children) {
        const QString path = child.absoluteFilePath();
        if (Scanner::isMountPoint(path)) {
            offer(path);
            continue;
        }
        QStringList mounts;
        const QFileInfoList subs = QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &sub : subs) {
            if (Scanner::isMountPoint(sub.absoluteFilePath()))
                mounts.append(sub.absoluteFilePath());
        }
        if (mounts.isEmpty())
            offer(path);
        for (const QString &mount : std::as_const(mounts))
            offer(mount);
    }
    out.removeDuplicates();
    return out;
}

void Settings::load()
{
    const QString output = m_settings.value(keyOutput()).toString();
    m_output = outputs().contains(output) ? output : outputs().first();

    const QStringList roots = m_settings.value(keyRoots()).toStringList();
    m_roots = roots.isEmpty() ? defaultRoots() : roots;

    const QVariant storedHue = m_settings.value(keyAccentHue());
    bool hueOk = false;
    const int hue = storedHue.toInt(&hueOk);
    if (storedHue.isValid()) {
        m_accentHue = hueOk ? normalizeHue(hue) : defaultAccentHue;
    } else if (m_settings.contains(keyAccent())) {
        m_accentHue = hueFromColor(m_settings.value(keyAccent()).toString());
        m_dirty.insert(keyAccentHue(), m_accentHue);
    } else {
        m_accentHue = defaultAccentHue;
    }
    m_dropAccent = m_settings.contains(keyAccent());
    m_accentFromArt = m_settings.value(keyAccentFromArt(), false).toBool();

    m_scale = normalizeScale(m_settings.value(keyScale(), 1.0).toDouble());
    m_saverSeconds = normalizeSaver(m_settings.value(keySaverSeconds(), 30).toInt());
    m_screenOffSeconds = qMax(0, m_settings.value(keyScreenOffSeconds(), 300).toInt());
    m_suspendMinutes = qMax(0, m_settings.value(keySuspendMinutes(), 10).toInt());
    m_wifiOffScreenOff = m_settings.value(keyWifiOffScreenOff(), false).toBool();

    const QString codec = m_settings.value(keyBtCodec()).toString().toLower();
    m_btCodec = codecs().contains(codec) ? codec : codecs().first();
    m_overlayToasts = m_settings.value(keyOverlayToasts(), false).toBool();

    const QString hpGain = m_settings.value(keyHpGain()).toString().toLower();
    m_hpGain = hpGains().contains(hpGain) ? hpGain : QStringLiteral("normal");
    const QString hpAmp = m_settings.value(keyHpAmp()).toString().toLower();
    m_hpAmp = hpAmps().contains(hpAmp) ? hpAmp : QStringLiteral("lohifi");
    const int spkGain = m_settings.value(keySpkGain(), 102).toInt();
    m_spkGain = spkGains().contains(spkGain) ? spkGain : 102;
    const int volumeStep = m_settings.value(keyVolumeStep(), 5).toInt();
    m_volumeStep = volumeSteps().contains(volumeStep) ? volumeStep : 5;
    const QString rgb = m_settings.value(keyRgb()).toString().toLower();
    m_rgb = rgbModes().contains(rgb) ? rgb : QStringLiteral("armada");
    const QString orientation = m_settings.value(keyOrientation()).toString();
    m_orientation = orientations().contains(orientation) ? orientation : orientations().first();
    m_autoRotate = m_settings.value(keyAutoRotate(), false).toBool();
    m_eqHeadphones = normalizeEq(m_settings.value(keyEqHeadphones()).toString());
    m_eqUsb = normalizeEq(m_settings.value(keyEqUsb()).toString());
    m_eqSpeaker = normalizeEq(m_settings.value(keyEqSpeaker()).toString());
}

QString Settings::path() const
{
    return m_settings.fileName();
}

QString Settings::output() const
{
    return m_output;
}

void Settings::setOutput(const QString &output)
{
    if (!outputs().contains(output) || m_output == output)
        return;
    m_output = output;
    store(keyOutput(), m_output);
    emit outputChanged();
}

QStringList Settings::roots() const
{
    return m_roots;
}

void Settings::setRoots(const QStringList &roots)
{
    const QStringList value = roots.isEmpty() ? defaultRoots() : roots;
    if (m_roots == value)
        return;
    m_roots = value;
    store(keyRoots(), m_roots);
    emit rootsChanged();
}

int Settings::accentHue() const
{
    return m_accentHue;
}

void Settings::setAccentHue(int hue)
{
    const int value = normalizeHue(hue);
    if (m_accentHue == value)
        return;
    m_accentHue = value;
    store(keyAccentHue(), m_accentHue);
    emit accentHueChanged();
}

bool Settings::accentFromArt() const
{
    return m_accentFromArt;
}

void Settings::setAccentFromArt(bool on)
{
    if (m_accentFromArt == on)
        return;
    m_accentFromArt = on;
    store(keyAccentFromArt(), m_accentFromArt);
    emit accentFromArtChanged();
}

double Settings::scale() const
{
    return m_scale;
}

void Settings::setScale(double scale)
{
    const double value = normalizeScale(scale);
    if (qFuzzyCompare(m_scale, value))
        return;
    m_scale = value;
    store(keyScale(), m_scale);
    emit scaleChanged();
}

int Settings::saverSeconds() const
{
    return m_saverSeconds;
}

void Settings::setSaverSeconds(int seconds)
{
    const int value = normalizeSaver(seconds);
    if (m_saverSeconds == value)
        return;
    m_saverSeconds = value;
    store(keySaverSeconds(), m_saverSeconds);
    emit saverSecondsChanged();
}

void Settings::store(const QString &key, const QVariant &value)
{
    m_dirty.insert(key, value);
    m_syncTimer.start();
}

void Settings::sync()
{
    m_syncTimer.stop();
    if (m_dirty.isEmpty())
        return;
    for (auto it = m_dirty.cbegin(); it != m_dirty.cend(); ++it)
        m_settings.setValue(it.key(), it.value());
    m_dirty.clear();
    if (m_dropAccent) {
        m_settings.remove(keyAccent());
        m_dropAccent = false;
    }
    m_settings.sync();
}

int Settings::screenOffSeconds() const
{
    return m_screenOffSeconds;
}

void Settings::setScreenOffSeconds(int seconds)
{
    const int value = qMax(0, seconds);
    if (m_screenOffSeconds == value)
        return;
    m_screenOffSeconds = value;
    store(keyScreenOffSeconds(), m_screenOffSeconds);
    emit screenOffSecondsChanged();
}

int Settings::suspendMinutes() const
{
    return m_suspendMinutes;
}

void Settings::setSuspendMinutes(int minutes)
{
    const int value = qMax(0, minutes);
    if (m_suspendMinutes == value)
        return;
    m_suspendMinutes = value;
    store(keySuspendMinutes(), m_suspendMinutes);
    emit suspendMinutesChanged();
}

bool Settings::wifiOffScreenOff() const
{
    return m_wifiOffScreenOff;
}

void Settings::setWifiOffScreenOff(bool on)
{
    if (m_wifiOffScreenOff == on)
        return;
    m_wifiOffScreenOff = on;
    store(keyWifiOffScreenOff(), m_wifiOffScreenOff);
    emit wifiOffScreenOffChanged();
}

QString Settings::btCodec() const
{
    return m_btCodec;
}

void Settings::setBtCodec(const QString &codec)
{
    const QString value = codec.toLower();
    if (!codecs().contains(value) || m_btCodec == value)
        return;
    m_btCodec = value;
    store(keyBtCodec(), m_btCodec);
    emit btCodecChanged();
}

bool Settings::overlayToasts() const
{
    return m_overlayToasts;
}

void Settings::setOverlayToasts(bool on)
{
    if (m_overlayToasts == on)
        return;
    m_overlayToasts = on;
    store(keyOverlayToasts(), m_overlayToasts);
    emit overlayToastsChanged();
}

QString Settings::hpGain() const
{
    return m_hpGain;
}

void Settings::setHpGain(const QString &gain)
{
    const QString value = gain.toLower();
    if (!hpGains().contains(value) || m_hpGain == value)
        return;
    m_hpGain = value;
    store(keyHpGain(), m_hpGain);
    emit hpGainChanged();
}

QString Settings::hpAmp() const
{
    return m_hpAmp;
}

void Settings::setHpAmp(const QString &amp)
{
    const QString value = amp.toLower();
    if (!hpAmps().contains(value) || m_hpAmp == value)
        return;
    m_hpAmp = value;
    store(keyHpAmp(), m_hpAmp);
    emit hpAmpChanged();
}

int Settings::spkGain() const
{
    return m_spkGain;
}

void Settings::setSpkGain(int step)
{
    if (!spkGains().contains(step) || m_spkGain == step)
        return;
    m_spkGain = step;
    store(keySpkGain(), m_spkGain);
    emit spkGainChanged();
}

int Settings::volumeStep() const
{
    return m_volumeStep;
}

void Settings::setVolumeStep(int step)
{
    if (!volumeSteps().contains(step) || m_volumeStep == step)
        return;
    m_volumeStep = step;
    store(keyVolumeStep(), m_volumeStep);
    emit volumeStepChanged();
}

QString Settings::rgb() const
{
    return m_rgb;
}

void Settings::setRgb(const QString &mode)
{
    const QString value = mode.toLower();
    if (!rgbModes().contains(value) || m_rgb == value)
        return;
    m_rgb = value;
    store(keyRgb(), m_rgb);
    emit rgbChanged();
}

QString Settings::orientation() const
{
    return m_orientation;
}

void Settings::setOrientation(const QString &orientation)
{
    if (!orientations().contains(orientation) || m_orientation == orientation)
        return;
    m_orientation = orientation;
    store(keyOrientation(), m_orientation);
    emit orientationChanged();
}

bool Settings::autoRotate() const
{
    return m_autoRotate;
}

void Settings::setAutoRotate(bool on)
{
    if (m_autoRotate == on)
        return;
    m_autoRotate = on;
    store(keyAutoRotate(), m_autoRotate);
    emit autoRotateChanged();
}

QString Settings::normalizeEq(const QString &key)
{
    if (key == QLatin1StringView("custom"))
        return key;
    if (!key.isEmpty() && QDir::isAbsolutePath(key))
        return QDir::cleanPath(key);
    return QStringLiteral("off");
}

QString Settings::eqHeadphones() const
{
    return m_eqHeadphones;
}

void Settings::setEqHeadphones(const QString &key)
{
    const QString value = normalizeEq(key);
    if (m_eqHeadphones == value)
        return;
    m_eqHeadphones = value;
    store(keyEqHeadphones(), m_eqHeadphones);
    emit eqHeadphonesChanged();
}

QString Settings::eqUsb() const
{
    return m_eqUsb;
}

void Settings::setEqUsb(const QString &key)
{
    const QString value = normalizeEq(key);
    if (m_eqUsb == value)
        return;
    m_eqUsb = value;
    store(keyEqUsb(), m_eqUsb);
    emit eqUsbChanged();
}

QString Settings::eqSpeaker() const
{
    return m_eqSpeaker;
}

void Settings::setEqSpeaker(const QString &key)
{
    const QString value = normalizeEq(key);
    if (m_eqSpeaker == value)
        return;
    m_eqSpeaker = value;
    store(keyEqSpeaker(), m_eqSpeaker);
    emit eqSpeakerChanged();
}

QString Settings::eq(const QString &output) const
{
    if (output == QLatin1StringView("headphones"))
        return m_eqHeadphones;
    if (output == QLatin1StringView("usb"))
        return m_eqUsb;
    if (output == QLatin1StringView("speaker"))
        return m_eqSpeaker;
    return QStringLiteral("off");
}

void Settings::setEq(const QString &output, const QString &key)
{
    if (output == QLatin1StringView("headphones"))
        setEqHeadphones(key);
    else if (output == QLatin1StringView("usb"))
        setEqUsb(key);
    else if (output == QLatin1StringView("speaker"))
        setEqSpeaker(key);
}

}
