#include "Settings.h"

#include <QCoreApplication>
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

QString keyScale()
{
    return QStringLiteral("scale");
}

QString keyPocketControls()
{
    return QStringLiteral("pocketControls");
}

QString keySaverSeconds()
{
    return QStringLiteral("saverSeconds");
}

QString keyKeepPhoneInGameMode()
{
    return QStringLiteral("keepPhoneInGameMode");
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
    return {QStringLiteral("auto"), QStringLiteral("speaker"), QStringLiteral("headphones")};
}

QStringList Settings::codecs()
{
    return {QStringLiteral("auto"), QStringLiteral("aptx"), QStringLiteral("aac"),
            QStringLiteral("ldac"), QStringLiteral("sbc")};
}

QStringList Settings::accents()
{
    return {
        QStringLiteral("#FA2A55"),
        QStringLiteral("#F09609"),
        QStringLiteral("#1BA1E2"),
        QStringLiteral("#339933"),
        QStringLiteral("#A200FF"),
    };
}

QStringList Settings::defaultRoots()
{
    return {QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
            QStringLiteral("/run/media")};
}

void Settings::load()
{
    const QString output = m_settings.value(keyOutput()).toString();
    m_output = outputs().contains(output) ? output : outputs().first();

    const QStringList roots = m_settings.value(keyRoots()).toStringList();
    m_roots = roots.isEmpty() ? defaultRoots() : roots;

    const QString accent = m_settings.value(keyAccent()).toString().toUpper();
    m_accent = accents().contains(accent) ? accent : accents().first();

    m_scale = normalizeScale(m_settings.value(keyScale(), 1.0).toDouble());
    m_pocketControls = m_settings.value(keyPocketControls(), false).toBool();
    m_saverSeconds = normalizeSaver(m_settings.value(keySaverSeconds(), 30).toInt());
    m_keepPhoneInGameMode = m_settings.value(keyKeepPhoneInGameMode(), false).toBool();
    m_screenOffSeconds = qMax(0, m_settings.value(keyScreenOffSeconds(), 300).toInt());
    m_suspendMinutes = qMax(0, m_settings.value(keySuspendMinutes(), 10).toInt());
    m_wifiOffScreenOff = m_settings.value(keyWifiOffScreenOff(), false).toBool();

    const QString codec = m_settings.value(keyBtCodec()).toString().toLower();
    m_btCodec = codecs().contains(codec) ? codec : codecs().first();
    m_overlayToasts = m_settings.value(keyOverlayToasts(), false).toBool();
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

QString Settings::accent() const
{
    return m_accent;
}

void Settings::setAccent(const QString &accent)
{
    const QString value = accent.toUpper();
    if (!accents().contains(value) || m_accent == value)
        return;
    m_accent = value;
    store(keyAccent(), m_accent);
    emit accentChanged();
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

bool Settings::pocketControls() const
{
    return m_pocketControls;
}

void Settings::setPocketControls(bool on)
{
    if (m_pocketControls == on)
        return;
    m_pocketControls = on;
    store(keyPocketControls(), m_pocketControls);
    emit pocketControlsChanged();
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

bool Settings::keepPhoneInGameMode() const
{
    return m_keepPhoneInGameMode;
}

void Settings::setKeepPhoneInGameMode(bool on)
{
    if (m_keepPhoneInGameMode == on)
        return;
    m_keepPhoneInGameMode = on;
    store(keyKeepPhoneInGameMode(), m_keepPhoneInGameMode);
    emit keepPhoneInGameModeChanged();
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

}
