#include "Settings.h"

#include "Eq.h"
#include "Scanner.h"

#include <microset/client.h>

#include <QColor>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

#include <cstdlib>

namespace microdap {

namespace {

struct Key
{
    const char *name;
    QMetaType::Type type;
    void (Settings::*notify)();
};

const QString keyOutput = QStringLiteral("audio.output");
const QString keyHpGain = QStringLiteral("audio.hp_gain");
const QString keyHpAmp = QStringLiteral("audio.hp_amp");
const QString keySpkGain = QStringLiteral("audio.speaker_gain");
const QString keyVolumeStep = QStringLiteral("audio.volume_step");
const QString keyMaxVolume = QStringLiteral("audio.max_volume");
const QString keyPowerOnVolume = QStringLiteral("audio.power_on_volume");
const QString keyBtCodec = QStringLiteral("audio.bt_codec");
const QString keyOrientation = QStringLiteral("display.orientation");
const QString keyAutoRotate = QStringLiteral("display.auto_rotate");
const QString keyBootSession = QStringLiteral("session.default");
const QString keyRoots = QStringLiteral("microdap.roots");
const QString keyAccentHue = QStringLiteral("microdap.accent_hue");
const QString keyAccentFromArt = QStringLiteral("microdap.accent_from_art");
const QString keyScale = QStringLiteral("microdap.scale");
const QString keySaverSeconds = QStringLiteral("microdap.saver_seconds");
const QString keySaverLyrics = QStringLiteral("microdap.saver_lyrics");
const QString keyScreenOffSeconds = QStringLiteral("microdap.screen_off_seconds");
const QString keySuspendMinutes = QStringLiteral("microdap.suspend_minutes");
const QString keyWifiOffScreenOff = QStringLiteral("microdap.wifi_off_screen_off");
const QString keyOverlayToasts = QStringLiteral("microdap.overlay_toasts");
const QString keyRgb = QStringLiteral("microdap.rgb");
const QString keyTouch = QStringLiteral("microdap.touch");
const QString keyOutputLevel = QStringLiteral("microdap.output_level");
const QString keySampleRate = QStringLiteral("microdap.sample_rate");
const QString keyPlayThrough = QStringLiteral("microdap.play_through");
const QString keyReplayGain = QStringLiteral("microdap.replaygain");
const QString keyCrossfadeMs = QStringLiteral("microdap.crossfade_ms");
const QString keyResume = QStringLiteral("microdap.resume");
const QString keyAutoNowPlaying = QStringLiteral("microdap.auto_now_playing");
const QString keyClock = QStringLiteral("microdap.clock");
const QString keyBatteryPercent = QStringLiteral("microdap.battery_percent");
const QString keyQuickplay = QStringLiteral("microdap.quickplay");

constexpr Key keys[] = {
    {"audio.output", QMetaType::QString, &Settings::outputChanged},
    {"audio.hp_gain", QMetaType::QString, &Settings::hpGainChanged},
    {"audio.hp_amp", QMetaType::QString, &Settings::hpAmpChanged},
    {"audio.speaker_gain", QMetaType::UInt, &Settings::spkGainChanged},
    {"audio.volume_step", QMetaType::UInt, &Settings::volumeStepChanged},
    {"audio.max_volume", QMetaType::UInt, &Settings::maxVolumeChanged},
    {"audio.power_on_volume", QMetaType::UInt, &Settings::powerOnVolumeChanged},
    {"audio.bt_codec", QMetaType::QString, &Settings::btCodecChanged},
    {"display.orientation", QMetaType::QString, &Settings::orientationChanged},
    {"display.auto_rotate", QMetaType::Bool, &Settings::autoRotateChanged},
    {"session.default", QMetaType::QString, &Settings::bootSessionChanged},
    {"microdap.roots", QMetaType::QStringList, &Settings::rootsChanged},
    {"microdap.accent_hue", QMetaType::UInt, &Settings::accentHueChanged},
    {"microdap.accent_from_art", QMetaType::Bool, &Settings::accentFromArtChanged},
    {"microdap.scale", QMetaType::Double, &Settings::scaleChanged},
    {"microdap.saver_seconds", QMetaType::UInt, &Settings::saverSecondsChanged},
    {"microdap.saver_lyrics", QMetaType::Bool, &Settings::saverLyricsChanged},
    {"microdap.screen_off_seconds", QMetaType::UInt, &Settings::screenOffSecondsChanged},
    {"microdap.suspend_minutes", QMetaType::UInt, &Settings::suspendMinutesChanged},
    {"microdap.wifi_off_screen_off", QMetaType::Bool, &Settings::wifiOffScreenOffChanged},
    {"microdap.overlay_toasts", QMetaType::Bool, &Settings::overlayToastsChanged},
    {"microdap.rgb", QMetaType::QString, &Settings::rgbChanged},
    {"microdap.touch", QMetaType::Bool, &Settings::touchChanged},
    {"microdap.output_level", QMetaType::QString, &Settings::outputLevelChanged},
    {"microdap.sample_rate", QMetaType::QString, &Settings::sampleRateChanged},
    {"microdap.play_through", QMetaType::QString, &Settings::playThroughChanged},
    {"microdap.replaygain", QMetaType::QString, &Settings::replaygainChanged},
    {"microdap.crossfade_ms", QMetaType::UInt, &Settings::crossfadeMsChanged},
    {"microdap.resume", QMetaType::QString, &Settings::resumeChanged},
    {"microdap.auto_now_playing", QMetaType::Bool, &Settings::autoNowPlayingChanged},
    {"microdap.clock", QMetaType::QString, &Settings::clockChanged},
    {"microdap.battery_percent", QMetaType::Bool, &Settings::batteryPercentChanged},
    {"microdap.quickplay", QMetaType::Bool, &Settings::quickplayChanged},
};

const Key *find(const QString &key)
{
    for (const Key &entry : keys) {
        if (key == QLatin1StringView(entry.name))
            return &entry;
    }
    return nullptr;
}

QStringList eqFields()
{
    return {QStringLiteral("on"), QStringLiteral("name"), QStringLiteral("filters")};
}

bool eqKey(const QString &key)
{
    for (const QString &slot : EqControl::outputs()) {
        for (const QString &field : eqFields()) {
            if (key == EqFilters::key(slot, field))
                return true;
        }
    }
    return false;
}

const QString balancePrefix = QStringLiteral("audio.balance.");
const QString crossfeedPrefix = QStringLiteral("audio.crossfeed.");

QMetaType::Type typeOf(const QString &key)
{
    if (const Key *entry = find(key))
        return entry->type;
    if (key.startsWith(balancePrefix))
        return QMetaType::Int;
    if (key.endsWith(QLatin1StringView(".on")))
        return QMetaType::Bool;
    if (key.endsWith(QLatin1StringView(".filters")))
        return QMetaType::QStringList;
    return QMetaType::QString;
}

QVariant typed(const QString &key, const QVariant &value)
{
    switch (typeOf(key)) {
    case QMetaType::UInt:
        return QVariant::fromValue(value.toUInt());
    case QMetaType::Int:
        return QVariant::fromValue(value.toInt());
    case QMetaType::Bool:
        return QVariant::fromValue(value.toBool());
    case QMetaType::Double:
        return QVariant::fromValue(value.toDouble());
    case QMetaType::QStringList:
        return QVariant::fromValue(value.toStringList());
    default:
        return QVariant::fromValue(value.toString());
    }
}

QVariantMap defaults()
{
    QVariantMap out{
        {keyOutput, QStringLiteral("auto")},
        {keyHpGain, QStringLiteral("high")},
        {keyHpAmp, QStringLiteral("lohifi")},
        {keySpkGain, 102u},
        {keyVolumeStep, 5u},
        {keyMaxVolume, 100u},
        {keyPowerOnVolume, 0u},
        {keyBtCodec, QStringLiteral("auto")},
        {keyOrientation, QStringLiteral("portrait")},
        {keyAutoRotate, true},
        {keyBootSession, QStringLiteral("steam")},
        {keyRoots, QStringList()},
        {keyAccentHue, uint(Settings::defaultAccentHue)},
        {keyAccentFromArt, false},
        {keyScale, 1.0},
        {keySaverSeconds, 30u},
        {keySaverLyrics, false},
        {keyScreenOffSeconds, 300u},
        {keySuspendMinutes, 10u},
        {keyWifiOffScreenOff, false},
        {keyOverlayToasts, false},
        {keyRgb, QStringLiteral("system")},
        {keyTouch, true},
        {keyOutputLevel, QStringLiteral("off")},
        {keySampleRate, QStringLiteral("auto")},
        {keyPlayThrough, QStringLiteral("off")},
        {keyReplayGain, QStringLiteral("off")},
        {keyCrossfadeMs, 0u},
        {keyResume, QStringLiteral("position")},
        {keyAutoNowPlaying, false},
        {keyClock, QStringLiteral("24h")},
        {keyBatteryPercent, false},
        {keyQuickplay, true},
    };
    for (const QString &slot : EqControl::outputs()) {
        out.insert(EqFilters::key(slot, QStringLiteral("on")), false);
        out.insert(EqFilters::key(slot, QStringLiteral("name")), QString());
        out.insert(EqFilters::key(slot, QStringLiteral("filters")), QStringList{QStringLiteral("preamp 0")});
        out.insert(Settings::balanceKey(slot), 0);
    }
    for (const QString &slot : Settings::crossfeedSlots())
        out.insert(Settings::crossfeedKey(slot), QStringLiteral("off"));
    return out;
}

QStringList oldDefaultRoots()
{
    return {QStandardPaths::writableLocation(QStandardPaths::MusicLocation), QStringLiteral("/run/media")};
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

QString pick(const QString &value, const QStringList &allowed, const QString &fallback)
{
    return allowed.contains(value) ? value : fallback;
}

void legacyEq(QSettings &ini, const QString &iniKey, const QString &slot, QVariantMap &out)
{
    if (!ini.contains(iniKey))
        return;
    const QString value = ini.value(iniKey).toString();
    std::optional<EqProfile> profile;
    QString name;
    if (value == QLatin1StringView("custom")) {
        const QString path = QFileInfo(ini.fileName()).absolutePath() + QStringLiteral("/eq-") + slot
                             + QStringLiteral(".ini");
        profile = EqStore::read(path);
    } else if (!value.isEmpty() && QDir::isAbsolutePath(value)) {
        QString error;
        profile = EqParser::parseFile(QDir::cleanPath(value), &error);
        if (!profile)
            qWarning().noquote() << "microdap settings: import of" << iniKey << "skipped," << error;
        name = EqControl::displayName(value);
    } else {
        out.insert(EqFilters::key(slot, QStringLiteral("on")), false);
        return;
    }
    if (!profile)
        return;
    out.insert(EqFilters::key(slot, QStringLiteral("on")), true);
    out.insert(EqFilters::key(slot, QStringLiteral("name")), name);
    out.insert(EqFilters::key(slot, QStringLiteral("filters")), EqFilters::encode(*profile));
}

}

Settings::Settings()
    : m_owned(std::make_unique<MicroSet::Client>(bus()))
    , m_client(m_owned.get())
{
    load();
}

Settings::Settings(MicroSet::Client *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
    load();
}

Settings::~Settings() = default;

QDBusConnection Settings::bus()
{
    const QString address = qEnvironmentVariable("MICRODAP_MICROSET_BUS");
    if (address.isEmpty())
        return QDBusConnection::systemBus();
    return QDBusConnection::connectToBus(address, QStringLiteral("microdap-microset"));
}

QString Settings::legacyPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/microdap/settings.ini");
}

QStringList Settings::outputs()
{
    return {QStringLiteral("auto"), QStringLiteral("speaker"), QStringLiteral("headphones"), QStringLiteral("usb"),
            QStringLiteral("bt")};
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

QList<int> Settings::maxVolumeSteps()
{
    QList<int> out;
    for (int v = 50; v <= 100; v += 5)
        out.push_back(v);
    return out;
}

QList<int> Settings::powerOnVolumeSteps()
{
    QList<int> out{0};
    for (int v = 10; v <= 100; v += 5)
        out.push_back(v);
    return out;
}

QStringList Settings::crossfeedModes()
{
    return {QStringLiteral("off"), QStringLiteral("default"), QStringLiteral("chuMoy"), QStringLiteral("janMeier")};
}

QStringList Settings::crossfeedSlots()
{
    return {QStringLiteral("headphones"), QStringLiteral("usb")};
}

QString Settings::balanceKey(const QString &slot)
{
    return balancePrefix + slot;
}

QString Settings::crossfeedKey(const QString &slot)
{
    return crossfeedPrefix + slot;
}

QStringList Settings::playThroughModes()
{
    return {QStringLiteral("off"), QStringLiteral("folders"), QStringLiteral("albums")};
}

QStringList Settings::replaygainModes()
{
    return {QStringLiteral("off"), QStringLiteral("track"), QStringLiteral("album")};
}

QStringList Settings::resumeModes()
{
    return {QStringLiteral("off"), QStringLiteral("track"), QStringLiteral("position")};
}

QStringList Settings::clockModes()
{
    return {QStringLiteral("24h"), QStringLiteral("12h"), QStringLiteral("off")};
}

QStringList Settings::rgbModes()
{
    return {QStringLiteral("accent"), QStringLiteral("off"), QStringLiteral("system")};
}

QStringList Settings::orientations()
{
    return {QStringLiteral("portrait"), QStringLiteral("portrait-flipped"), QStringLiteral("landscape"),
            QStringLiteral("landscape-flipped")};
}

QStringList Settings::outputLevels()
{
    return {QStringLiteral("off"), QStringLiteral("bitperfect"), QStringLiteral("exclusive")};
}

QStringList Settings::sampleRates()
{
    return {QStringLiteral("auto"), QStringLiteral("44100"), QStringLiteral("48000"), QStringLiteral("88200"),
            QStringLiteral("96000"), QStringLiteral("176400"), QStringLiteral("192000"),
            QStringLiteral("352800"), QStringLiteral("384000")};
}

QStringList Settings::defaultRoots()
{
    QStringList out{QStandardPaths::writableLocation(QStandardPaths::MusicLocation)};
    const auto dirs = [](const QString &path) {
        return QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    };
    const auto named = [](const QFileInfo &info, QLatin1StringView name) {
        return info.fileName().compare(name, Qt::CaseInsensitive) == 0;
    };
    const auto offer = [&](const QString &card) {
        for (const QFileInfo &entry : dirs(card)) {
            if (named(entry, QLatin1StringView("music"))) {
                out.append(entry.absoluteFilePath());
                continue;
            }
            if (!named(entry, QLatin1StringView("roms")))
                continue;
            for (const QFileInfo &music : dirs(entry.absoluteFilePath())) {
                if (named(music, QLatin1StringView("music")))
                    out.append(music.absoluteFilePath());
            }
        }
    };
    for (const QFileInfo &child : dirs(Scanner::mediaBase())) {
        const QString path = child.absoluteFilePath();
        if (Scanner::isMountPoint(path)) {
            offer(path);
            continue;
        }
        for (const QFileInfo &sub : dirs(path))
            offer(sub.absoluteFilePath());
    }
    return out;
}

QList<int> Settings::saverSteps()
{
    return {10, 15, 30, 60, 120, 300};
}

QList<int> Settings::screenOffSteps()
{
    return {0, 60, 120, 300, 600, 1800};
}

QList<int> Settings::crossfadeSteps()
{
    return {0, 1000, 2000, 3000, 5000};
}

QList<int> Settings::suspendSteps()
{
    return {0, 5, 10, 30, 60};
}

int Settings::snap(int value, const QList<int> &steps)
{
    int best = steps.first();
    for (const int step : steps) {
        if (std::abs(step - value) < std::abs(best - value))
            best = step;
    }
    return best;
}

QStringList Settings::candidateRoots()
{
    QStringList out;
    QSet<QString> seen;
    for (const QString &root : defaultRoots()) {
        const QString canonical = QFileInfo(root).canonicalFilePath();
        const QString key = canonical.isEmpty() ? QDir::cleanPath(root) : canonical;
        if (seen.contains(key))
            continue;
        seen.insert(key);
        out.append(root);
    }
    return out;
}

QVariantMap Settings::legacyValues(const QString &path)
{
    QVariantMap out;
    QSettings ini(path, QSettings::IniFormat);
    const auto text = [&ini](const char *key) { return ini.value(QLatin1StringView(key)).toString().toLower(); };
    const auto has = [&ini](const char *key) { return ini.contains(QLatin1StringView(key)); };
    const auto put = [&out](const QString &key, const QVariant &value) { out.insert(key, typed(key, value)); };

    if (has("output") && outputs().contains(text("output")))
        put(keyOutput, text("output"));
    if (has("hpAmp") && hpAmps().contains(text("hpAmp")))
        put(keyHpAmp, text("hpAmp"));
    if (has("spkGain") && spkGains().contains(ini.value(QStringLiteral("spkGain")).toInt()))
        put(keySpkGain, ini.value(QStringLiteral("spkGain")).toInt());
    if (has("volumeStep") && volumeSteps().contains(ini.value(QStringLiteral("volumeStep")).toInt()))
        put(keyVolumeStep, ini.value(QStringLiteral("volumeStep")).toInt());
    if (has("btCodec") && codecs().contains(text("btCodec")))
        put(keyBtCodec, text("btCodec"));
    const QString orientation = ini.value(QStringLiteral("orientation")).toString();
    if (orientations().contains(orientation))
        put(keyOrientation, orientation);
    if (has("autoRotate"))
        put(keyAutoRotate, ini.value(QStringLiteral("autoRotate")).toBool());

    if (has("rgb")) {
        const QString rgb = text("rgb");
        if (rgb == QLatin1StringView("armada"))
            put(keyRgb, QStringLiteral("system"));
        else if (rgb == QLatin1StringView("accent") || rgb == QLatin1StringView("off"))
            put(keyRgb, rgb);
    }
    bool hueOk = false;
    const int hue = ini.value(QStringLiteral("accentHue")).toInt(&hueOk);
    if (has("accentHue") && hueOk)
        put(keyAccentHue, normalizeHue(hue));
    else if (!has("accentHue") && has("accent"))
        put(keyAccentHue, hueFromColor(ini.value(QStringLiteral("accent")).toString()));

    legacyEq(ini, QStringLiteral("eqHeadphones"), QStringLiteral("headphones"), out);
    legacyEq(ini, QStringLiteral("eqUsb"), QStringLiteral("usb"), out);
    legacyEq(ini, QStringLiteral("eqSpeaker"), QStringLiteral("speaker"), out);

    if (has("touch"))
        put(keyTouch, ini.value(QStringLiteral("touch")).toBool());
    const QStringList roots = ini.value(QStringLiteral("roots")).toStringList();
    if (!roots.isEmpty() && roots != oldDefaultRoots())
        put(keyRoots, roots);
    if (has("accentFromArt"))
        put(keyAccentFromArt, ini.value(QStringLiteral("accentFromArt")).toBool());
    if (has("scale"))
        put(keyScale, normalizeScale(ini.value(QStringLiteral("scale")).toDouble()));
    if (has("saverSeconds"))
        put(keySaverSeconds, snap(ini.value(QStringLiteral("saverSeconds")).toInt(), saverSteps()));
    if (has("screenOffSeconds"))
        put(keyScreenOffSeconds, snap(ini.value(QStringLiteral("screenOffSeconds")).toInt(), screenOffSteps()));
    if (has("suspendMinutes"))
        put(keySuspendMinutes, snap(ini.value(QStringLiteral("suspendMinutes")).toInt(), suspendSteps()));
    if (has("wifiOffScreenOff"))
        put(keyWifiOffScreenOff, ini.value(QStringLiteral("wifiOffScreenOff")).toBool());
    if (has("overlayToasts"))
        put(keyOverlayToasts, ini.value(QStringLiteral("overlayToasts")).toBool());
    if (has("outputLevel") && outputLevels().contains(text("outputLevel")))
        put(keyOutputLevel, text("outputLevel"));
    if (has("sampleRate") && sampleRates().contains(text("sampleRate")))
        put(keySampleRate, text("sampleRate"));
    return out;
}

void Settings::importLegacy(const QString &path)
{
    if (!m_client || m_importing || !QFileInfo::exists(path))
        return;
    const QVariantMap values = legacyValues(path);
    qInfo() << "microdap settings: import" << path << values.size() << "keys";
    m_importing = true;
    m_client->import(values, [this, path](const QStringList &taken, const QMap<QString, QString> &refused,
                                          const MicroSet::Error &error) {
        m_importing = false;
        if (!error.ok()) {
            qWarning().noquote() << "microdap settings: import failed," << error.name << error.message
                                 << "- kept" << path;
            emit imported(false);
            return;
        }
        for (auto it = refused.cbegin(); it != refused.cend(); ++it)
            qWarning().noquote() << "microdap settings: import refused" << it.key() << it.value();
        const QString done = path + QStringLiteral(".imported");
        QFile::remove(done);
        if (!QFile::rename(path, done))
            qWarning() << "microdap settings: cannot rename" << path;
        qInfo() << "microdap settings: imported" << taken.size() << "keys, refused" << refused.size();
        emit imported(true);
    });
}

MicroSet::Client *Settings::client() const
{
    return m_client;
}

bool Settings::available() const
{
    return m_available;
}

void Settings::load()
{
    m_values = defaults();
    if (!m_client)
        return;
    connect(m_client, &MicroSet::Client::changed, this, &Settings::take);
    const QStringList watched = m_values.keys();
    for (const QString &key : watched)
        m_client->watch(key);
    listAll();
}

void Settings::listAll()
{
    MicroSet::Error error;
    const QVariantMap values = m_client->list(QString(), &error);
    if (!error.ok()) {
        qWarning().noquote() << "microdap settings: microsetd not reachable," << error.name << "- defaults";
        return;
    }
    m_available = true;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        take(it.key(), it.value());
    if (m_values.value(keyRoots).toStringList() == oldDefaultRoots())
        store(keyRoots, QStringList());
}

void Settings::take(const QString &key, const QVariant &value)
{
    if (!m_values.contains(key))
        return;
    if (!m_available) {
        m_available = true;
        listAll();
        emit availableChanged();
    }
    if (m_values.value(key) == value)
        return;
    m_values.insert(key, value);
    notify(key);
}

void Settings::notify(const QString &key)
{
    if (const Key *entry = find(key))
        emit (this->*(entry->notify))();
    else if (key == balanceKey(m_slot))
        emit balanceChanged();
    else if (key == crossfeedKey(m_slot))
        emit crossfeedChanged();
    emit valueChanged(key);
}

QVariant Settings::value(const QString &key) const
{
    return m_values.value(key);
}

void Settings::setValue(const QString &key, const QVariant &value)
{
    if (!m_values.contains(key) && !eqKey(key)) {
        qWarning() << "microdap settings: unknown key" << key;
        return;
    }
    store(key, typed(key, value));
}

void Settings::store(const QString &key, const QVariant &value)
{
    if (m_values.value(key) == value)
        return;
    m_values.insert(key, value);
    notify(key);
    if (!m_client)
        return;
    m_client->set(key, value, [this, key](const MicroSet::Error &error) {
        if (error.ok())
            return;
        qWarning().noquote() << "microdap settings: set" << key << "refused," << error.name << error.message;
        refresh(key);
    });
}

void Settings::refresh(const QString &key)
{
    MicroSet::Error error;
    const QVariant value = m_client ? m_client->get(key, &error) : QVariant();
    if (!error.ok() || !value.isValid())
        return;
    take(key, value);
}

QString Settings::output() const
{
    return pick(m_values.value(keyOutput).toString(), outputs(), outputs().first());
}

void Settings::setOutput(const QString &output)
{
    if (outputs().contains(output))
        store(keyOutput, output);
}

QStringList Settings::roots() const
{
    const QStringList roots = m_values.value(keyRoots).toStringList();
    return roots.isEmpty() ? defaultRoots() : roots;
}

bool Settings::rootsDefault() const
{
    return m_values.value(keyRoots).toStringList().isEmpty();
}

void Settings::setRoots(const QStringList &roots)
{
    store(keyRoots, roots);
}

void Settings::refreshRoots()
{
    if (rootsDefault())
        emit rootsChanged();
}

int Settings::accentHue() const
{
    return normalizeHue(m_values.value(keyAccentHue).toInt());
}

void Settings::setAccentHue(int hue)
{
    store(keyAccentHue, QVariant::fromValue(uint(normalizeHue(hue))));
}

bool Settings::accentFromArt() const
{
    return m_values.value(keyAccentFromArt).toBool();
}

void Settings::setAccentFromArt(bool on)
{
    store(keyAccentFromArt, on);
}

double Settings::scale() const
{
    return normalizeScale(m_values.value(keyScale).toDouble());
}

void Settings::setScale(double scale)
{
    store(keyScale, normalizeScale(scale));
}

int Settings::saverSeconds() const
{
    return normalizeSaver(m_values.value(keySaverSeconds).toInt());
}

void Settings::setSaverSeconds(int seconds)
{
    store(keySaverSeconds, QVariant::fromValue(uint(snap(seconds, saverSteps()))));
}

int Settings::screenOffSeconds() const
{
    return qMax(0, m_values.value(keyScreenOffSeconds).toInt());
}

void Settings::setScreenOffSeconds(int seconds)
{
    store(keyScreenOffSeconds, QVariant::fromValue(uint(snap(seconds, screenOffSteps()))));
}

int Settings::suspendMinutes() const
{
    return qMax(0, m_values.value(keySuspendMinutes).toInt());
}

void Settings::setSuspendMinutes(int minutes)
{
    store(keySuspendMinutes, QVariant::fromValue(uint(snap(minutes, suspendSteps()))));
}

bool Settings::wifiOffScreenOff() const
{
    return m_values.value(keyWifiOffScreenOff).toBool();
}

void Settings::setWifiOffScreenOff(bool on)
{
    store(keyWifiOffScreenOff, on);
}

QString Settings::btCodec() const
{
    return pick(m_values.value(keyBtCodec).toString(), codecs(), codecs().first());
}

void Settings::setBtCodec(const QString &codec)
{
    if (codecs().contains(codec.toLower()))
        store(keyBtCodec, codec.toLower());
}

bool Settings::overlayToasts() const
{
    return m_values.value(keyOverlayToasts).toBool();
}

void Settings::setOverlayToasts(bool on)
{
    store(keyOverlayToasts, on);
}

QString Settings::hpGain() const
{
    return pick(m_values.value(keyHpGain).toString(), hpGains(), QStringLiteral("high"));
}

void Settings::setHpGain(const QString &gain)
{
    if (hpGains().contains(gain.toLower()))
        store(keyHpGain, gain.toLower());
}

QString Settings::hpAmp() const
{
    return pick(m_values.value(keyHpAmp).toString(), hpAmps(), QStringLiteral("lohifi"));
}

void Settings::setHpAmp(const QString &amp)
{
    if (hpAmps().contains(amp.toLower()))
        store(keyHpAmp, amp.toLower());
}

int Settings::spkGain() const
{
    const int value = m_values.value(keySpkGain).toInt();
    return spkGains().contains(value) ? value : 102;
}

void Settings::setSpkGain(int step)
{
    if (spkGains().contains(step))
        store(keySpkGain, QVariant::fromValue(uint(step)));
}

int Settings::volumeStep() const
{
    const int value = m_values.value(keyVolumeStep).toInt();
    return volumeSteps().contains(value) ? value : 5;
}

void Settings::setVolumeStep(int step)
{
    if (volumeSteps().contains(step))
        store(keyVolumeStep, QVariant::fromValue(uint(step)));
}

int Settings::maxVolume() const
{
    const int value = m_values.value(keyMaxVolume).toInt();
    return maxVolumeSteps().contains(value) ? value : 100;
}

void Settings::setMaxVolume(int percent)
{
    if (maxVolumeSteps().contains(percent))
        store(keyMaxVolume, QVariant::fromValue(uint(percent)));
}

int Settings::powerOnVolume() const
{
    const int value = m_values.value(keyPowerOnVolume).toInt();
    return powerOnVolumeSteps().contains(value) ? value : 0;
}

void Settings::setPowerOnVolume(int percent)
{
    if (powerOnVolumeSteps().contains(percent))
        store(keyPowerOnVolume, QVariant::fromValue(uint(percent)));
}

QString Settings::outputSlot() const
{
    return m_slot;
}

void Settings::setOutputSlot(const QString &slot)
{
    const QString next = EqControl::outputs().contains(slot) ? slot : QString();
    if (next == m_slot)
        return;
    m_slot = next;
    emit outputSlotChanged();
    emit balanceChanged();
    emit crossfeedChanged();
}

int Settings::balance() const
{
    if (m_slot.isEmpty())
        return 0;
    return qBound(-maxBalance, m_values.value(balanceKey(m_slot)).toInt(), maxBalance);
}

void Settings::setBalance(int balance)
{
    if (!m_slot.isEmpty())
        store(balanceKey(m_slot), qBound(-maxBalance, balance, maxBalance));
}

QString Settings::crossfeed() const
{
    if (!crossfeedSlots().contains(m_slot))
        return QStringLiteral("off");
    return pick(m_values.value(crossfeedKey(m_slot)).toString(), crossfeedModes(), QStringLiteral("off"));
}

void Settings::setCrossfeed(const QString &mode)
{
    if (crossfeedSlots().contains(m_slot) && crossfeedModes().contains(mode))
        store(crossfeedKey(m_slot), mode);
}

QString Settings::rgb() const
{
    return pick(m_values.value(keyRgb).toString(), rgbModes(), QStringLiteral("system"));
}

void Settings::setRgb(const QString &mode)
{
    if (rgbModes().contains(mode.toLower()))
        store(keyRgb, mode.toLower());
}

QString Settings::orientation() const
{
    return pick(m_values.value(keyOrientation).toString(), orientations(), orientations().first());
}

void Settings::setOrientation(const QString &orientation)
{
    if (orientations().contains(orientation))
        store(keyOrientation, orientation);
}

bool Settings::autoRotate() const
{
    return m_values.value(keyAutoRotate).toBool();
}

void Settings::setAutoRotate(bool on)
{
    store(keyAutoRotate, on);
}

bool Settings::saverLyrics() const
{
    return m_values.value(keySaverLyrics).toBool();
}

void Settings::setSaverLyrics(bool on)
{
    store(keySaverLyrics, on);
}

QString Settings::playThrough() const
{
    return pick(m_values.value(keyPlayThrough).toString(), playThroughModes(), QStringLiteral("off"));
}

void Settings::setPlayThrough(const QString &mode)
{
    if (playThroughModes().contains(mode))
        store(keyPlayThrough, mode);
}

QString Settings::replaygain() const
{
    return pick(m_values.value(keyReplayGain).toString(), replaygainModes(), QStringLiteral("off"));
}

void Settings::setReplaygain(const QString &mode)
{
    if (replaygainModes().contains(mode))
        store(keyReplayGain, mode);
}

int Settings::crossfadeMs() const
{
    const int value = m_values.value(keyCrossfadeMs).toInt();
    return crossfadeSteps().contains(value) ? value : 0;
}

void Settings::setCrossfadeMs(int ms)
{
    if (crossfadeSteps().contains(ms))
        store(keyCrossfadeMs, QVariant::fromValue(uint(ms)));
}

QString Settings::resume() const
{
    return pick(m_values.value(keyResume).toString(), resumeModes(), QStringLiteral("position"));
}

void Settings::setResume(const QString &mode)
{
    if (resumeModes().contains(mode))
        store(keyResume, mode);
}

bool Settings::autoNowPlaying() const
{
    return m_values.value(keyAutoNowPlaying).toBool();
}

void Settings::setAutoNowPlaying(bool on)
{
    store(keyAutoNowPlaying, on);
}

QString Settings::clock() const
{
    return pick(m_values.value(keyClock).toString(), clockModes(), QStringLiteral("24h"));
}

void Settings::setClock(const QString &mode)
{
    if (clockModes().contains(mode))
        store(keyClock, mode);
}

bool Settings::batteryPercent() const
{
    return m_values.value(keyBatteryPercent).toBool();
}

void Settings::setBatteryPercent(bool on)
{
    store(keyBatteryPercent, on);
}

bool Settings::quickplay() const
{
    return m_values.value(keyQuickplay).toBool();
}

void Settings::setQuickplay(bool on)
{
    store(keyQuickplay, on);
}

bool Settings::touch() const
{
    return m_values.value(keyTouch).toBool();
}

void Settings::setTouch(bool on)
{
    store(keyTouch, on);
}

QString Settings::outputLevel() const
{
    return pick(m_values.value(keyOutputLevel).toString(), outputLevels(), outputLevels().first());
}

void Settings::setOutputLevel(const QString &level)
{
    if (outputLevels().contains(level.toLower()))
        store(keyOutputLevel, level.toLower());
}

QString Settings::sampleRate() const
{
    return pick(m_values.value(keySampleRate).toString(), sampleRates(), sampleRates().first());
}

void Settings::setSampleRate(const QString &rate)
{
    if (sampleRates().contains(rate.toLower()))
        store(keySampleRate, rate.toLower());
}

QString Settings::bootSession() const
{
    return m_values.value(keyBootSession).toString();
}

void Settings::setBootSession(const QString &name)
{
    if (bootSessions().contains(name))
        store(keyBootSession, name);
}

QStringList Settings::bootSessions() const
{
    if (!m_client)
        return {};
    MicroSet::Error error;
    const QString text = m_client->schema(keyBootSession, &error);
    if (!error.ok())
        return {};
    const QJsonArray values = QJsonDocument::fromJson(text.toUtf8()).object().value(keyBootSession).toObject()
                                  .value(QStringLiteral("enum")).toArray();
    QStringList out;
    for (const QJsonValue &value : values)
        out.append(value.toString());
    return out;
}

}
