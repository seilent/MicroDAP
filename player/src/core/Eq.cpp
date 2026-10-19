#include "Eq.h"

#include "OutputControl.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

#include <cmath>
#include <algorithm>
#include <utility>

namespace microdap {

namespace {

constexpr double epsilon = 1e-6;
constexpr std::array<int, EqProfile::bandCount> flatIndexes{4, 10, 16, 22, 28, 34, 40, 46, 52, 58};

QString number()
{
    return QStringLiteral("([-+]?(?:\\d+\\.?\\d*|\\.\\d+))");
}

double clampLogged(double value, double low, double high, const char *what)
{
    const double clamped = qBound(low, value, high);
    if (clamped != value)
        qInfo() << "microdap eq:" << what << value << "clamped to" << clamped;
    return clamped;
}

struct FilterCode
{
    const char *code;
    EqBand::Type type;
    bool needsGain;
    bool needsQ;
};

constexpr std::array<FilterCode, 11> filterCodes{{
    {"pk", EqBand::Peak, true, true},
    {"lsc", EqBand::LowShelf, true, false},
    {"ls", EqBand::LowShelf, true, false},
    {"hsc", EqBand::HighShelf, true, false},
    {"hs", EqBand::HighShelf, true, false},
    {"lp", EqBand::LowPass, false, false},
    {"lpq", EqBand::LowPass, false, false},
    {"hp", EqBand::HighPass, false, false},
    {"hpq", EqBand::HighPass, false, false},
    {"bp", EqBand::BandPass, false, false},
    {"ap", EqBand::AllPass, false, true},
}};

constexpr std::array<std::pair<EqBand::Type, const char *>, 7> typeKeys{{
    {EqBand::Peak, "peak"},
    {EqBand::LowShelf, "lowshelf"},
    {EqBand::HighShelf, "highshelf"},
    {EqBand::BandPass, "bandpass"},
    {EqBand::LowPass, "lowpass"},
    {EqBand::HighPass, "highpass"},
    {EqBand::AllPass, "allpass"},
}};

const FilterCode *codeFor(const QString &code)
{
    const QString c = code.toLower();
    for (const FilterCode &entry : filterCodes) {
        if (c == QLatin1StringView(entry.code))
            return &entry;
    }
    return nullptr;
}

QString codePattern()
{
    QStringList codes;
    for (const FilterCode &entry : filterCodes)
        codes.append(QLatin1StringView(entry.code));
    return QStringLiteral("(") + codes.join(QLatin1Char('|')) + QStringLiteral(")");
}

QString typeKey(EqBand::Type type)
{
    for (const auto &[t, name] : typeKeys) {
        if (t == type)
            return QLatin1StringView(name);
    }
    return QStringLiteral("peak");
}

std::optional<EqBand::Type> typeOfKey(const QString &key)
{
    for (const auto &[t, name] : typeKeys) {
        if (key == QLatin1StringView(name))
            return t;
    }
    return std::nullopt;
}

bool gainFree(EqBand::Type type)
{
    return type != EqBand::Peak && type != EqBand::LowShelf && type != EqBand::HighShelf;
}

QString formatNumber(double value)
{
    return QString::number(value, 'g', 12);
}

int sign(int direction)
{
    return direction > 0 ? 1 : direction < 0 ? -1 : 0;
}

double stepGrid(double value, double increment, int direction, double precision)
{
    const double units = value / increment;
    const double next = direction > 0 ? std::floor(units + epsilon) + 1.0 : std::ceil(units - epsilon) - 1.0;
    return std::round(next * increment * precision) / precision;
}

}

EqProfile EqProfile::flat()
{
    EqProfile profile;
    for (int i = 0; i < bandCount; ++i) {
        EqBand &band = profile.bands[i];
        band.freqIndex = flatIndexes[i];
        band.freq = EqGrid::frequency(band.freqIndex);
    }
    return profile;
}

EqProfile EqProfile::fresh()
{
    EqProfile profile = flat();
    profile.bands[0] = freshBand();
    profile.count = 1;
    return profile;
}

EqBand EqProfile::freshBand()
{
    EqBand band;
    band.freqIndex = freshIndex;
    band.freq = EqGrid::frequency(freshIndex);
    return band;
}

void EqProfile::trim()
{
    int last = 0;
    for (int i = 0; i < bandCount; ++i) {
        const EqBand &band = bands[i];
        if (band.enabled && (gainFree(band.type) || band.gain != 0.0))
            last = i + 1;
    }
    count = qMax(1, last);
}

bool EqProfile::operator==(const EqProfile &other) const
{
    if (preamp != other.preamp || count != other.count)
        return false;
    for (int i = 0; i < count; ++i) {
        if (!(bands[i] == other.bands[i]))
            return false;
    }
    return true;
}

EqProfile EqProfile::off()
{
    EqProfile profile = flat();
    for (EqBand &band : profile.bands)
        band.enabled = false;
    return profile;
}

double EqProfile::preampVolume() const
{
    return std::pow(10.0, preamp / 20.0);
}

QVariantMap EqProfile::toMap() const
{
    QVariantList list;
    for (int i = 0; i < count; ++i) {
        const EqBand &band = bands[i];
        list.append(QVariantMap{
            {QStringLiteral("enabled"), band.enabled},
            {QStringLiteral("type"), band.enabled ? typeKey(band.type) : QStringLiteral("off")},
            {QStringLiteral("freq"), band.freq},
            {QStringLiteral("freqIndex"), band.freqIndex},
            {QStringLiteral("gain"), band.gain},
            {QStringLiteral("q"), band.q},
        });
    }
    return {
        {QStringLiteral("preamp"), preamp},
        {QStringLiteral("bands"), list},
        {QStringLiteral("count"), count},
    };
}

double EqGrid::frequency(int index)
{
    const int i = qBound(0, index, size - 1);
    if (i == 0)
        return EqParser::minFreq;
    if (i == size - 1)
        return EqParser::maxFreq;
    return 1000.0 * std::pow(2.0, (i - anchor) / 6.0);
}

int EqGrid::step(const EqBand &band, int direction, int count)
{
    const int s = sign(direction);
    const int extra = s * qMax(0, count - 1);
    if (band.freqIndex >= 0 && band.freqIndex < size)
        return qBound(0, band.freqIndex + s * qMax(1, count), size - 1);
    if (s > 0) {
        for (int i = 0; i < size; ++i) {
            if (frequency(i) > band.freq * (1.0 + epsilon))
                return qBound(0, i + extra, size - 1);
        }
        return size - 1;
    }
    for (int i = size - 1; i >= 0; --i) {
        if (frequency(i) < band.freq * (1.0 - epsilon))
            return qBound(0, i + extra, size - 1);
    }
    return 0;
}

std::optional<EqProfile> EqParser::parse(const QString &text, QString *error)
{
    static const QRegularExpression preampRe(
        QStringLiteral("^preamp\\s*:\\s*") + number() + QStringLiteral("\\s*db\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression filterRe(
        QStringLiteral("^filter(?:\\s*\\d+)?\\s*:\\s*(on|off)\\s+([a-z]+)\\s+fc\\s+") + number()
            + QStringLiteral("\\s*hz(?:\\s+gain\\s+") + number() + QStringLiteral("\\s*db)?(?:\\s+q\\s+") + number()
            + QStringLiteral(")?\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression offRe(
        QStringLiteral("^filter(?:\\s*\\d+)?\\s*:\\s*off\\s+") + codePattern() + QStringLiteral("\\s+fc\\b"),
        QRegularExpression::CaseInsensitiveOption);

    EqProfile profile = EqProfile::flat();
    const EqProfile defaults = profile;
    bool havePreamp = false;
    double preamp = 0.0;
    int count = 0;
    int dropped = 0;
    int lineNo = 0;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        ++lineNo;
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        const QRegularExpressionMatch p = preampRe.match(line);
        if (p.hasMatch()) {
            havePreamp = true;
            preamp = p.captured(1).toDouble();
            continue;
        }
        const QRegularExpressionMatch f = filterRe.match(line);
        const FilterCode *code = f.hasMatch() ? codeFor(f.captured(2)) : nullptr;
        const bool hasGain = f.hasMatch() && f.capturedLength(4) > 0;
        const bool hasQ = f.hasMatch() && f.capturedLength(5) > 0;
        if (code && (hasGain || !code->needsGain) && (hasQ || !code->needsQ)) {
            if (count >= EqProfile::bandCount) {
                ++dropped;
                continue;
            }
            EqBand band;
            band.enabled = f.captured(1).compare(QLatin1StringView("on"), Qt::CaseInsensitive) == 0;
            band.type = code->type;
            band.freq = clampLogged(f.captured(3).toDouble(), minFreq, maxFreq, "freq");
            band.gain = hasGain ? clampLogged(f.captured(4).toDouble(), minGain, maxGain, "gain") : 0.0;
            band.q = hasQ ? clampLogged(f.captured(5).toDouble(), minQ, maxQ, "q") : defaultQ;
            band.freqIndex = -1;
            profile.bands[count++] = band;
            continue;
        }
        if (offRe.match(line).hasMatch()) {
            if (count >= EqProfile::bandCount) {
                ++dropped;
                continue;
            }
            EqBand band = defaults.bands[count];
            band.enabled = false;
            profile.bands[count++] = band;
            continue;
        }
        qInfo() << "microdap eq: line" << lineNo << "skipped:" << line;
    }
    if (dropped > 0)
        qInfo() << "microdap eq:" << dropped << "filters past the first" << EqProfile::bandCount << "dropped";
    if (count == 0) {
        if (error)
            *error = QStringLiteral("no valid filter");
        return std::nullopt;
    }
    for (int i = count; i < EqProfile::bandCount; ++i)
        profile.bands[i].enabled = false;
    profile.count = count;
    profile.preamp = havePreamp ? clampLogged(preamp, minPreamp, maxPreamp, "preamp") : fallbackPreamp(profile);
    if (error)
        error->clear();
    return profile;
}

std::optional<EqProfile> EqParser::parseFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("cannot read ") + path;
        return std::nullopt;
    }
    QString reason;
    std::optional<EqProfile> profile = parse(QString::fromUtf8(file.readAll()), &reason);
    if (!profile && error)
        *error = path + QStringLiteral(": ") + reason;
    return profile;
}

double EqParser::fallbackPreamp(const EqProfile &profile)
{
    double sum = 0.0;
    for (int i = 0; i < profile.count; ++i) {
        const EqBand &band = profile.bands[i];
        if (!band.enabled)
            continue;
        if (!gainFree(band.type) && band.gain > 0.0)
            sum += band.gain;
        if (band.type == EqBand::LowPass || band.type == EqBand::HighPass)
            sum += qMax(0.0, 20.0 * std::log10(qBound(minQ, band.q, maxQ)));
    }
    return sum > 0.0 ? qMax(minPreamp, -sum) : 0.0;
}

QString EqStore::typeName(const EqBand &band)
{
    return band.enabled ? typeKey(band.type) : QStringLiteral("off");
}

QString EqStore::text(const EqProfile &profile)
{
    QString out = QStringLiteral("[General]\npreamp=") + formatNumber(profile.preamp) + QStringLiteral("\nbands=")
                  + QString::number(profile.count) + QLatin1Char('\n');
    const EqProfile flatBands = EqProfile::off();
    for (int i = 0; i < EqProfile::bandCount; ++i) {
        const EqBand &band = i < profile.count ? profile.bands[i] : flatBands.bands[i];
        out += QStringLiteral("\n[band") + QString::number(i + 1) + QStringLiteral("]\n");
        out += QStringLiteral("type=") + typeName(band) + QLatin1Char('\n');
        if (band.freqIndex >= 0)
            out += QStringLiteral("freqIndex=") + QString::number(band.freqIndex) + QLatin1Char('\n');
        else
            out += QStringLiteral("freq=") + formatNumber(band.freq) + QLatin1Char('\n');
        out += QStringLiteral("gain=") + formatNumber(band.gain) + QLatin1Char('\n');
        out += QStringLiteral("q=") + formatNumber(band.q) + QLatin1Char('\n');
    }
    return out;
}

EqProfile EqStore::read(const QString &path)
{
    if (!QFileInfo::exists(path))
        return EqProfile::fresh();
    QSettings settings(path, QSettings::IniFormat);
    if (settings.status() != QSettings::NoError) {
        qWarning() << "microdap eq: cannot parse" << path;
        return EqProfile::fresh();
    }
    EqProfile profile = EqProfile::flat();
    bool ok = false;
    profile.preamp = qBound(EqParser::minPreamp, settings.value(QStringLiteral("preamp")).toDouble(&ok),
                            EqParser::maxPreamp);
    if (!ok) {
        qWarning() << "microdap eq: no preamp in" << path;
        return EqProfile::fresh();
    }
    const bool counted = settings.contains(QStringLiteral("bands"));
    const int count = counted ? settings.value(QStringLiteral("bands")).toInt(&ok) : EqProfile::bandCount;
    if (!ok || count < 1 || count > EqProfile::bandCount) {
        qWarning() << "microdap eq: band count unreadable in" << path;
        return EqProfile::fresh();
    }
    for (int i = 0; i < count; ++i) {
        settings.beginGroup(QStringLiteral("band") + QString::number(i + 1));
        EqBand band;
        const QString type = settings.value(QStringLiteral("type")).toString().toLower();
        const std::optional<EqBand::Type> stored = typeOfKey(type);
        bool valid = type == QLatin1StringView("off") || stored.has_value();
        band.enabled = type != QLatin1StringView("off");
        band.type = stored.value_or(EqBand::Peak);
        if (settings.contains(QStringLiteral("freqIndex"))) {
            band.freqIndex = settings.value(QStringLiteral("freqIndex")).toInt(&ok);
            valid = valid && ok && band.freqIndex >= 0 && band.freqIndex < EqGrid::size;
            band.freq = EqGrid::frequency(band.freqIndex);
        } else {
            band.freqIndex = -1;
            band.freq = qBound(EqParser::minFreq, settings.value(QStringLiteral("freq")).toDouble(&ok),
                               EqParser::maxFreq);
            valid = valid && ok;
        }
        band.gain = qBound(EqParser::minGain, settings.value(QStringLiteral("gain")).toDouble(&ok),
                           EqParser::maxGain);
        valid = valid && ok;
        band.q = qBound(EqParser::minQ, settings.value(QStringLiteral("q")).toDouble(&ok), EqParser::maxQ);
        valid = valid && ok;
        settings.endGroup();
        if (!valid) {
            qWarning() << "microdap eq: band" << i + 1 << "unreadable in" << path;
            return EqProfile::fresh();
        }
        profile.bands[i] = band;
    }
    profile.count = count;
    if (!counted)
        profile.trim();
    return profile;
}

bool EqStore::write(const QString &path, const EqProfile &profile)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qWarning() << "microdap eq: cannot create" << QFileInfo(path).absolutePath();
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "microdap eq: cannot write" << path;
        return false;
    }
    file.write(text(profile).toUtf8());
    if (!file.commit()) {
        qWarning() << "microdap eq: cannot commit" << path;
        return false;
    }
    qInfo() << "microdap eq: store written" << path;
    return true;
}

QString EqFilters::key(const QString &slot, const QString &field)
{
    return QStringLiteral("audio.eq.") + slot + QLatin1Char('.') + field;
}

QString EqFilters::number(double value)
{
    QString text = QString::number(value, 'f', 4);
    while (text.contains(QLatin1Char('.')) && (text.endsWith(QLatin1Char('0')) || text.endsWith(QLatin1Char('.'))))
        text.chop(1);
    return text == QLatin1StringView("-0") ? QStringLiteral("0") : text;
}

QStringList EqFilters::encode(const EqProfile &profile)
{
    QStringList items{QStringLiteral("preamp ")
                      + number(qBound(EqParser::minPreamp, profile.preamp, EqParser::maxPreamp))};
    int last = -1;
    for (int i = 0; i < profile.count; ++i) {
        if (profile.bands[i].enabled)
            last = i;
    }
    for (int i = 0; i <= last; ++i) {
        const EqBand &band = profile.bands[i];
        const double freq = qBound(EqParser::minFreq, band.freq, EqParser::maxFreq);
        const double q = qBound(EqParser::minQ, band.q, EqParser::maxQ);
        const double gain = band.enabled ? qBound(EqParser::minGain, band.gain, EqParser::maxGain) : 0.0;
        items.append((band.enabled ? typeKey(band.type) : QStringLiteral("peak")) + QLatin1Char(' ') + number(freq)
                     + QLatin1Char(' ') + number(gain) + QLatin1Char(' ') + number(q));
    }
    return items;
}

EqProfile EqFilters::decode(const QStringList &items)
{
    EqProfile profile = EqProfile::off();
    if (items.isEmpty())
        return profile;
    const QStringList preamp = items.first().split(QLatin1Char(' '));
    bool ok = false;
    const double value = preamp.size() == 2 && preamp.first() == QLatin1StringView("preamp")
                             ? preamp.at(1).toDouble(&ok) : 0.0;
    profile.preamp = ok ? qBound(EqParser::minPreamp, value, EqParser::maxPreamp) : 0.0;
    int index = 0;
    for (qsizetype i = 1; i < items.size() && index < EqProfile::bandCount; ++i) {
        const QStringList fields = items.at(i).split(QLatin1Char(' '));
        const std::optional<EqBand::Type> type = fields.size() == 4 ? typeOfKey(fields.first()) : std::nullopt;
        bool freqOk = false;
        bool gainOk = false;
        bool qOk = false;
        const double freq = type ? fields.at(1).toDouble(&freqOk) : 0.0;
        const double gain = type ? fields.at(2).toDouble(&gainOk) : 0.0;
        const double q = type ? fields.at(3).toDouble(&qOk) : 0.0;
        if (!type || !freqOk || !gainOk || !qOk) {
            qWarning() << "microdap eq: filter item skipped:" << items.at(i);
            continue;
        }
        EqBand &band = profile.bands[index++];
        band.type = *type;
        band.freq = qBound(EqParser::minFreq, freq, EqParser::maxFreq);
        band.gain = qBound(EqParser::minGain, gain, EqParser::maxGain);
        band.q = qBound(EqParser::minQ, q, EqParser::maxQ);
        band.enabled = !(band.type == EqBand::Peak && band.gain == 0.0);
        band.freqIndex = -1;
        for (int g = 0; g < EqGrid::size; ++g) {
            if (std::abs(EqGrid::frequency(g) - band.freq) < 0.01) {
                band.freqIndex = g;
                band.freq = EqGrid::frequency(g);
                break;
            }
        }
    }
    profile.count = qMax(1, index);
    return profile;
}

EqControl::EqControl(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_syncTimer.setSingleShot(true);
    m_syncTimer.setInterval(syncDelayMs);
    connect(&m_syncTimer, &QTimer::timeout, this, &EqControl::sync);
    m_writeTimer.setSingleShot(true);
    m_writeTimer.setInterval(writeDelayMs);
    connect(&m_writeTimer, &QTimer::timeout, this, &EqControl::writeFilters);
    m_sinkTimer.setInterval(sinkPollMs);
    connect(&m_sinkTimer, &QTimer::timeout, this, &EqControl::pollSink);
    m_startTimer.setSingleShot(true);
    connect(&m_startTimer, &QTimer::timeout, this, &EqControl::seedFromSink);
    if (QCoreApplication *app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this, &EqControl::sync);
    if (m_settings) {
        connect(m_settings, &Settings::valueChanged, this, &EqControl::onValueChanged);
        connect(m_settings, &Settings::rootsChanged, this, &EqControl::relist);
        connect(m_settings, &QObject::destroyed, this, [this] { m_settings = nullptr; });
        relist();
    }
}

EqControl::~EqControl()
{
    m_sinkTimer.stop();
    dropSinkProcess();
    sync();
}

QStringList EqControl::outputs()
{
    return {QStringLiteral("headphones"), QStringLiteral("usb"), QStringLiteral("speaker"), QStringLiteral("bt")};
}

QString EqControl::configDir()
{
    return qEnvironmentVariable("MICRODAP_EQ_DIR",
                                QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                                    + QStringLiteral("/microdap"));
}

QString EqControl::sourceDir()
{
    return configDir() + QStringLiteral("/eq");
}

QString EqControl::storePath(const QString &output)
{
    return configDir() + QStringLiteral("/eq-") + output + QStringLiteral(".ini");
}

QString EqControl::displayName(const QString &path)
{
    QString name = QFileInfo(path).fileName();
    if (name.endsWith(QLatin1StringView(".txt"), Qt::CaseInsensitive))
        name.chop(4);
    const auto suffix = QLatin1StringView(" ParametricEQ");
    if (name.size() > suffix.size() && name.endsWith(suffix, Qt::CaseInsensitive))
        name.chop(suffix.size());
    return name.toUpper().left(nameMax);
}

QString EqControl::presetDir()
{
    return sourceDir() + QStringLiteral("/presets");
}

bool EqControl::nameOk(const QString &name)
{
    const QByteArray bytes = name.toUtf8();
    if (bytes.isEmpty() || bytes.size() > presetNameMax || name.startsWith(QLatin1Char(' '))
        || name.endsWith(QLatin1Char(' ')) || name.startsWith(QLatin1Char('.')))
        return false;
    for (const char c : bytes) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7f || c == '/')
            return false;
    }
    if (name.endsWith(QLatin1StringView(" ParametricEQ")) || name.endsWith(QLatin1StringView(" GraphicEQ")))
        return false;
    const QString folded = name.toLower();
    return folded != QLatin1StringView("off") && folded != QLatin1StringView("flat")
           && folded != QLatin1StringView("custom");
}

bool EqControl::nameLess(const QString &a, const QString &b)
{
    qsizetype i = 0;
    qsizetype j = 0;
    while (i < a.size() && j < b.size()) {
        if (a.at(i).isDigit() && b.at(j).isDigit()) {
            qsizetype ie = i;
            qsizetype je = j;
            while (ie < a.size() && a.at(ie).isDigit())
                ++ie;
            while (je < b.size() && b.at(je).isDigit())
                ++je;
            while (i + 1 < ie && a.at(i) == QLatin1Char('0'))
                ++i;
            while (j + 1 < je && b.at(j) == QLatin1Char('0'))
                ++j;
            if (ie - i != je - j)
                return ie - i < je - j;
            const int c = QStringView(a).mid(i, ie - i).compare(QStringView(b).mid(j, je - j));
            if (c != 0)
                return c < 0;
            i = ie;
            j = je;
            continue;
        }
        const QChar x = a.at(i).toCaseFolded();
        const QChar y = b.at(j).toCaseFolded();
        if (x != y)
            return x < y;
        ++i;
        ++j;
    }
    if (a.size() - i != b.size() - j)
        return a.size() - i < b.size() - j;
    return a < b;
}

QString EqControl::autoEqText(const EqProfile &profile)
{
    QString out = QStringLiteral("Preamp: ") + formatNumber(profile.preamp) + QStringLiteral(" dB\n");
    for (int i = 0; i < profile.count; ++i) {
        const EqBand &band = profile.bands[i];
        const char *code = band.type == EqBand::LowShelf ? "LSC" : band.type == EqBand::HighShelf ? "HSC"
                         : band.type == EqBand::BandPass ? "BP" : band.type == EqBand::LowPass ? "LPQ"
                         : band.type == EqBand::HighPass ? "HPQ" : band.type == EqBand::AllPass ? "AP" : "PK";
        out += QStringLiteral("Filter ") + QString::number(i + 1) + QStringLiteral(": ")
               + (band.enabled ? QStringLiteral("ON ") : QStringLiteral("OFF ")) + QLatin1StringView(code)
               + QStringLiteral(" Fc ") + formatNumber(band.freq) + QStringLiteral(" Hz Gain ")
               + formatNumber(band.gain) + QStringLiteral(" dB Q ") + formatNumber(band.q) + QLatin1Char('\n');
    }
    return out;
}

QString EqControl::outputForSink(const QString &sink)
{
    if (sink.isEmpty())
        return {};
    if (sink.startsWith(OutputControl::usbPrefix()))
        return QStringLiteral("usb");
    if (sink.startsWith(OutputControl::btPrefix()))
        return QStringLiteral("bt");
    if (sink == OutputControl::headphonesSink())
        return QStringLiteral("headphones");
    return QStringLiteral("speaker");
}

QString EqControl::defaultSinkOutput()
{
    QProcess process;
    process.start(OutputControl::pactlPath(), {QStringLiteral("get-default-sink")});
    if (!process.waitForStarted(sinkTimeoutMs)) {
        qWarning() << "microdap eq: cannot start" << OutputControl::pactlPath();
        return {};
    }
    if (!process.waitForFinished(sinkTimeoutMs)) {
        process.kill();
        process.waitForFinished(sinkTimeoutMs);
        qWarning() << "microdap eq: get-default-sink timed out";
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};
    return outputForSink(QString::fromUtf8(process.readAllStandardOutput()).trimmed());
}

void EqControl::setOutputControl(OutputControl *control)
{
    if (m_outputControl == control)
        return;
    if (m_outputControl)
        disconnect(m_outputControl, nullptr, this, nullptr);
    m_outputControl = control;
    if (!m_outputControl)
        return;
    m_sinkTimer.stop();
    dropSinkProcess();
    connect(m_outputControl, &OutputControl::activeOutputChanged, this,
            [this] { setActiveOutput(m_outputControl->activeOutput()); });
    connect(m_outputControl, &OutputControl::applied, this, [this](const QString &, bool ok) {
        if (ok)
            setActiveOutput(m_outputControl->activeOutput());
    });
    connect(m_outputControl, &QObject::destroyed, this, [this] { m_outputControl = nullptr; });
    if (!m_outputControl->activeOutput().isEmpty())
        setActiveOutput(m_outputControl->activeOutput());
}

void EqControl::awaitOutput(OutputControl *control, int boundMs)
{
    setOutputControl(control);
    if (!m_outputControl)
        return;
    connect(m_outputControl, &OutputControl::firstApplied, this, [this](bool ok) {
        m_startTimer.stop();
        if (!ok)
            seedFromSink();
    });
    m_startTimer.start(boundMs);
}

void EqControl::startSinkPoll()
{
    if (m_outputControl)
        return;
    m_sinkTimer.start();
    pollSink();
}

bool EqControl::sinkPolling() const
{
    return m_sinkTimer.isActive();
}

void EqControl::pollSink()
{
    if (m_outputControl) {
        m_sinkTimer.stop();
        return;
    }
    querySink(false);
}

void EqControl::seedFromSink()
{
    if (!m_activeOutput.isEmpty() || m_sinkProcess)
        return;
    qInfo() << "microdap eq: output not resolved, default sink";
    querySink(true);
}

void EqControl::querySink(bool seed)
{
    if (m_sinkProcess) {
        if (m_sinkProcess->state() != QProcess::NotRunning) {
            qWarning() << "microdap eq: get-default-sink timed out";
            dropSinkProcess();
        }
        return;
    }
    auto *process = new QProcess(this);
    m_sinkProcess = process;
    connect(process, &QProcess::finished, this, [this, process, seed](int code, QProcess::ExitStatus status) {
        if (m_sinkProcess == process)
            m_sinkProcess = nullptr;
        process->deleteLater();
        const bool ok = status == QProcess::NormalExit && code == 0;
        if (seed) {
            const QString output = ok ? outputForSink(QString::fromUtf8(process->readAllStandardOutput()).trimmed())
                                      : QString();
            if (m_activeOutput.isEmpty())
                setActiveOutput(output.isEmpty() ? QStringLiteral("speaker") : output);
            return;
        }
        if (!ok)
            return;
        m_sinkWarned = false;
        setActiveOutput(outputForSink(QString::fromUtf8(process->readAllStandardOutput()).trimmed()));
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, seed](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        if (m_sinkProcess == process)
            m_sinkProcess = nullptr;
        process->deleteLater();
        if (!m_sinkWarned)
            qWarning() << "microdap eq: cannot start" << process->program();
        m_sinkWarned = true;
        if (seed && m_activeOutput.isEmpty())
            setActiveOutput(QStringLiteral("speaker"));
    });
    if (seed) {
        QTimer::singleShot(sinkTimeoutMs, process, [this, process] {
            if (m_sinkProcess != process)
                return;
            qWarning() << "microdap eq: get-default-sink timed out";
            dropSinkProcess();
            if (m_activeOutput.isEmpty())
                setActiveOutput(QStringLiteral("speaker"));
        });
    }
    process->start(OutputControl::pactlPath(), {QStringLiteral("get-default-sink")});
}

void EqControl::dropSinkProcess()
{
    if (!m_sinkProcess)
        return;
    QProcess *process = m_sinkProcess;
    m_sinkProcess = nullptr;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
        connect(process, &QProcess::finished, process, &QObject::deleteLater);
        process->kill();
        return;
    }
    process->deleteLater();
}

QVariantList EqControl::sources() const
{
    return m_sources;
}

QString EqControl::activeOutput() const
{
    return m_activeOutput;
}

void EqControl::setActiveOutput(const QString &output)
{
    if (!outputs().contains(output)) {
        if (!output.isEmpty())
            qWarning() << "microdap eq: unknown output" << output;
        return;
    }
    if (m_activeOutput == output)
        return;
    m_activeOutput = output;
    qInfo() << "microdap eq: active output" << output << "key" << key(output);
    emit activeOutputChanged();
    emit activeProfileChanged();
}

QString EqControl::activeName() const
{
    return m_activeOutput.isEmpty() ? QStringLiteral("OFF") : name(m_activeOutput);
}

QVariantMap EqControl::activeProfileMap() const
{
    return m_activeOutput.isEmpty() ? EqProfile::off().toMap() : profile(m_activeOutput).toMap();
}

int EqControl::revision() const
{
    return m_revision;
}

bool EqControl::on(const QString &output) const
{
    const QString k = EqFilters::key(output, QStringLiteral("on"));
    return m_settings ? m_settings->value(k).toBool() : m_local.value(k).toBool();
}

QString EqControl::storedName(const QString &output) const
{
    const QString k = EqFilters::key(output, QStringLiteral("name"));
    return m_settings ? m_settings->value(k).toString() : m_local.value(k).toString();
}

QStringList EqControl::storedFilters(const QString &output) const
{
    const QString k = EqFilters::key(output, QStringLiteral("filters"));
    return m_settings ? m_settings->value(k).toStringList() : m_local.value(k).toStringList();
}

void EqControl::put(const QString &output, const QString &field, const QVariant &value)
{
    const QString k = EqFilters::key(output, field);
    if (m_settings) {
        m_settings->setValue(k, value);
        return;
    }
    if (m_local.value(k) == value)
        return;
    m_local.insert(k, value);
    onValueChanged(k);
}

void EqControl::onValueChanged(const QString &key)
{
    const QStringList parts = key.split(QLatin1Char('.'));
    if (parts.size() != 4 || parts.at(0) != QLatin1StringView("audio") || parts.at(1) != QLatin1StringView("eq")
        || !outputs().contains(parts.at(2)))
        return;
    const QString output = parts.at(2);
    if (parts.at(3) == QLatin1StringView("filters")) {
        if (m_unwritten.contains(output))
            return;
        const QStringList filters = storedFilters(output);
        const auto live = m_live.constFind(output);
        if (live != m_live.constEnd() && EqFilters::encode(*live) == filters)
            return;
        m_live.insert(output, EqFilters::decode(filters));
        if (on(output) && storedName(output).isEmpty()) {
            m_custom.insert(output, m_live.value(output));
            m_dirty.insert(output);
            if (m_settings)
                m_syncTimer.start();
        }
    }
    qInfo() << "microdap eq:" << output << "key" << this->key(output);
    changed(output);
}

EqProfile EqControl::profile(const QString &output) const
{
    if (!outputs().contains(output) || !on(output))
        return EqProfile::off();
    auto it = m_live.constFind(output);
    if (it == m_live.constEnd())
        it = m_live.insert(output, restored(output));
    return *it;
}

EqProfile EqControl::restored(const QString &output) const
{
    const QStringList stored = storedFilters(output);
    const QString storedLabel = storedName(output);
    const auto sound = [](QStringList items) {
        static const QRegularExpression flat(QStringLiteral("^peak \\S+ 0 \\S+$"));
        while (items.size() > 1 && flat.match(items.last()).hasMatch())
            items.removeLast();
        return items;
    };
    if (storedLabel.isEmpty()) {
        const EqProfile &c = custom(output);
        if (sound(EqFilters::encode(c)) == sound(stored))
            return c;
    } else {
        for (const QString &path : m_sourcePaths) {
            if (displayName(path) != storedLabel)
                continue;
            const std::optional<EqProfile> file = fileProfile(path);
            if (file && EqFilters::encode(*file) == stored)
                return *file;
        }
    }
    return EqFilters::decode(stored);
}

void EqControl::setCustom(const QString &output, const EqProfile &profile)
{
    if (!outputs().contains(output))
        return;
    m_custom.insert(output, profile);
    select(output, true, QString(), profile);
    m_dirty.insert(output);
    if (m_settings)
        m_syncTimer.start();
}

QString EqControl::key(const QString &output) const
{
    if (!outputs().contains(output) || !on(output))
        return QStringLiteral("off");
    const QString stored = storedName(output);
    return stored.isEmpty() ? QStringLiteral("custom") : stored;
}

QString EqControl::name(const QString &output) const
{
    const QString k = key(output);
    if (k == QLatin1StringView("off"))
        return QStringLiteral("OFF");
    if (k == QLatin1StringView("custom"))
        return QStringLiteral("CUSTOM");
    return k;
}

bool EqControl::missing(const QString &) const
{
    return false;
}

bool EqControl::editable(const QString &output) const
{
    return key(output) != QLatin1StringView("off");
}

QVariantMap EqControl::profileMap(const QString &output) const
{
    QVariantMap map = profile(output).toMap();
    map.insert(QStringLiteral("key"), key(output));
    map.insert(QStringLiteral("name"), name(output));
    map.insert(QStringLiteral("missing"), false);
    map.insert(QStringLiteral("editable"), editable(output));
    map.insert(QStringLiteral("saved"), saved(output));
    return map;
}

QString EqControl::pathOf(const QString &key) const
{
    for (const QString &path : m_sourcePaths) {
        if (displayName(path) == key)
            return path;
    }
    return {};
}

bool EqControl::listedName(const QString &name, const QString &except) const
{
    for (const QString &path : m_sourcePaths) {
        if (path != except && displayName(path) == name)
            return true;
    }
    return false;
}

bool EqControl::saved(const QString &output) const
{
    const QString k = key(output);
    if (k == QLatin1StringView("off") || k == QLatin1StringView("custom"))
        return false;
    const QString path = pathOf(k);
    return !path.isEmpty() && QFileInfo(path).absolutePath() == QDir::cleanPath(presetDir());
}

QString EqControl::presetName(const QString &output) const
{
    return saved(output) ? QFileInfo(pathOf(key(output))).completeBaseName() : QString();
}

QString EqControl::savePreset(const QString &output)
{
    if (key(output) != QLatin1StringView("custom"))
        return QStringLiteral("preset not saved");
    int count = 0;
    for (const QString &path : std::as_const(m_sourcePaths)) {
        if (QFileInfo(path).absolutePath() == QDir::cleanPath(presetDir()))
            ++count;
    }
    if (count >= presetsMax)
        return QStringLiteral("too many presets");
    QString path;
    for (int n = 1;; ++n) {
        path = presetDir() + QStringLiteral("/Preset ") + QString::number(n) + QStringLiteral(".txt");
        if (!listedName(displayName(path)) && !QFileInfo::exists(path))
            break;
    }
    const EqProfile current = profile(output);
    QDir().mkpath(presetDir());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(autoEqText(current).toUtf8()) < 0
        || !file.commit()) {
        qWarning() << "microdap eq: cannot write preset" << path;
        return QStringLiteral("preset not saved");
    }
    qInfo() << "microdap eq: preset saved" << path;
    relist();
    const std::optional<EqProfile> stored = fileProfile(QDir::cleanPath(path));
    if (!stored)
        return QStringLiteral("preset not saved");
    select(output, true, displayName(path), *stored);
    return {};
}

bool EqControl::deletePreset(const QString &output)
{
    if (!saved(output))
        return false;
    const QString k = key(output);
    const QString path = pathOf(k);
    if (!QFile::remove(path)) {
        qWarning() << "microdap eq: cannot remove preset" << path;
        return false;
    }
    qInfo() << "microdap eq: preset deleted" << path;
    for (const QString &o : outputs()) {
        if (key(o) == k)
            select(o, false, storedName(o), profile(o));
    }
    relist();
    return true;
}

QString EqControl::renameRefusal(const QString &output, const QString &name) const
{
    if (!saved(output))
        return QStringLiteral("name not allowed");
    if (!nameOk(name))
        return QStringLiteral("name not allowed");
    const QString old = pathOf(key(output));
    const QString path = QDir::cleanPath(presetDir() + QLatin1Char('/') + name + QStringLiteral(".txt"));
    if (listedName(displayName(path), old) || (path != old && QFileInfo::exists(path)))
        return QStringLiteral("name taken");
    return {};
}

bool EqControl::renamePreset(const QString &output, const QString &name)
{
    if (!renameRefusal(output, name).isEmpty())
        return false;
    const QString k = key(output);
    const QString old = pathOf(k);
    const QString path = QDir::cleanPath(presetDir() + QLatin1Char('/') + name + QStringLiteral(".txt"));
    if (path == old)
        return true;
    QFile source(old);
    if (!source.open(QIODevice::ReadOnly)) {
        qWarning() << "microdap eq: cannot read preset" << old;
        return false;
    }
    const QByteArray bytes = source.readAll();
    source.close();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) < 0 || !file.commit()) {
        qWarning() << "microdap eq: cannot write preset" << path;
        return false;
    }
    if (!QFile::remove(old))
        qWarning() << "microdap eq: cannot remove preset" << old;
    qInfo() << "microdap eq: preset renamed" << old << "to" << path;
    relist();
    const QString renamed = displayName(path);
    for (const QString &o : outputs()) {
        if (key(o) == k)
            put(o, QStringLiteral("name"), renamed);
    }
    return true;
}

void EqControl::relist()
{
    m_files.clear();
    QVariantList list;
    QStringList paths;
    if (m_settings) {
        QStringList dirs{presetDir(), sourceDir()};
        for (const QString &root : m_settings->roots())
            dirs.append(root + QStringLiteral("/EQ"));
        for (const QString &dir : std::as_const(dirs)) {
            QFileInfoList entries = QDir(dir).entryInfoList({QStringLiteral("*.txt")}, QDir::Files | QDir::Readable);
            std::sort(entries.begin(), entries.end(), [](const QFileInfo &a, const QFileInfo &b) {
                return nameLess(a.fileName(), b.fileName());
            });
            for (const QFileInfo &entry : entries) {
                if (entry.suffix().compare(QLatin1StringView("txt"), Qt::CaseInsensitive) != 0)
                    continue;
                const QString path = QDir::cleanPath(entry.absoluteFilePath());
                if (paths.contains(path) || !fileProfile(path))
                    continue;
                paths.append(path);
                list.append(QVariantMap{
                    {QStringLiteral("path"), path},
                    {QStringLiteral("name"), displayName(path)},
                });
            }
        }
    }
    qInfo() << "microdap eq: listed" << paths.size() << "profiles";
    if (paths != m_sourcePaths) {
        m_sourcePaths = paths;
        m_sources = list;
        emit sourcesChanged();
    }
    for (const QString &output : outputs())
        changed(output);
}

void EqControl::cycleProfile(const QString &output, int direction)
{
    const int s = sign(direction);
    if (!outputs().contains(output) || s == 0)
        return;
    const int n = static_cast<int>(m_sourcePaths.size()) + 2;
    int i = 0;
    const QString k = key(output);
    if (k == QLatin1StringView("custom")) {
        i = n - 1;
    } else if (k != QLatin1StringView("off")) {
        i = -1;
        for (int p = 0; p < m_sourcePaths.size(); ++p) {
            if (displayName(m_sourcePaths.at(p)) == k) {
                i = p + 1;
                break;
            }
        }
    }
    const int next = i < 0 ? (s > 0 ? 0 : n - 1) : ((i + s) % n + n) % n;
    if (next == 0) {
        select(output, false, storedName(output), profile(output));
        return;
    }
    if (next == n - 1) {
        select(output, true, QString(), custom(output));
        return;
    }
    const QString path = m_sourcePaths.at(next - 1);
    const std::optional<EqProfile> parsed = fileProfile(path);
    if (!parsed)
        return;
    select(output, true, displayName(path), *parsed);
}

void EqControl::select(const QString &output, bool enabled, const QString &name, const EqProfile &profile)
{
    m_writeTimer.stop();
    writeFilters();
    m_live.insert(output, profile);
    put(output, QStringLiteral("filters"), EqFilters::encode(profile));
    put(output, QStringLiteral("name"), name);
    put(output, QStringLiteral("on"), enabled);
    changed(output);
}

void EqControl::stepPreamp(const QString &output, int direction, bool coarse)
{
    if (sign(direction) == 0)
        return;
    EqProfile *profile = beginEdit(output);
    if (!profile)
        return;
    const double next = stepGrid(profile->preamp, coarse ? gainCoarse : gainFine, direction, 10.0);
    profile->preamp = qBound(EqParser::minPreamp, next, EqParser::maxPreamp);
    endEdit(output);
}

void EqControl::stepType(const QString &output, int band, int direction)
{
    const int s = sign(direction);
    if (s == 0)
        return;
    EqBand *b = beginBandEdit(output, band);
    if (!b)
        return;
    const int states = static_cast<int>(EqBand::AllPass) + 2;
    const int state = b->enabled ? static_cast<int>(b->type) + 1 : 0;
    const int next = ((state + s) % states + states) % states;
    b->enabled = next != 0;
    if (next != 0)
        b->type = static_cast<EqBand::Type>(next - 1);
    endEdit(output);
}

void EqControl::stepFrequency(const QString &output, int band, int direction, bool coarse)
{
    if (sign(direction) == 0)
        return;
    EqBand *b = beginBandEdit(output, band);
    if (!b)
        return;
    b->freqIndex = EqGrid::step(*b, direction, coarse ? freqCoarseSteps : 1);
    b->freq = EqGrid::frequency(b->freqIndex);
    endEdit(output);
}

void EqControl::stepGain(const QString &output, int band, int direction, bool coarse)
{
    if (sign(direction) == 0 || band < 0 || band >= profile(output).count)
        return;
    const EqBand current = profile(output).bands[band];
    if (current.enabled && gainFree(current.type))
        return;
    EqBand *b = beginBandEdit(output, band);
    if (!b)
        return;
    const double next = stepGrid(b->gain, coarse ? gainCoarse : gainFine, direction, 10.0);
    b->gain = qBound(EqParser::minGain, next, EqParser::maxGain);
    endEdit(output);
}

void EqControl::stepQ(const QString &output, int band, int direction, bool coarse)
{
    if (sign(direction) == 0)
        return;
    EqBand *b = beginBandEdit(output, band);
    if (!b)
        return;
    const double next = stepGrid(b->q, coarse ? qCoarse : qFine, direction, 100.0);
    b->q = qBound(EqParser::minQ, next, EqParser::maxQ);
    endEdit(output);
}

void EqControl::sync()
{
    m_syncTimer.stop();
    m_writeTimer.stop();
    writeFilters();
    if (!m_settings) {
        m_dirty.clear();
        return;
    }
    const QSet<QString> dirty = m_dirty;
    m_dirty.clear();
    for (const QString &output : dirty)
        EqStore::write(storePath(output), custom(output));
}

std::optional<EqProfile> EqControl::fileProfile(const QString &path) const
{
    auto it = m_files.constFind(path);
    if (it != m_files.constEnd())
        return *it;
    QString error;
    std::optional<EqProfile> parsed = EqParser::parseFile(path, &error);
    if (!parsed)
        qWarning() << "microdap eq: profile left out," << error;
    m_files.insert(path, parsed);
    return parsed;
}

EqProfile &EqControl::custom(const QString &output) const
{
    auto it = m_custom.find(output);
    if (it == m_custom.end())
        it = m_custom.insert(output, m_settings ? EqStore::read(storePath(output)) : EqProfile::flat());
    return *it;
}

EqProfile *EqControl::beginEdit(const QString &output)
{
    if (!outputs().contains(output))
        return nullptr;
    const QString k = key(output);
    if (k == QLatin1StringView("off"))
        return nullptr;
    if (k != QLatin1StringView("custom") || !(custom(output) == profile(output))) {
        m_custom.insert(output, profile(output));
        qInfo() << "microdap eq:" << output << "copied" << k << "into" << storePath(output);
        put(output, QStringLiteral("name"), QString());
    }
    return &custom(output);
}

EqBand *EqControl::beginBandEdit(const QString &output, int band)
{
    if (band < 0 || band >= profile(output).count)
        return nullptr;
    EqProfile *profile = beginEdit(output);
    return profile ? &profile->bands[band] : nullptr;
}

void EqControl::endEdit(const QString &output)
{
    m_live.insert(output, custom(output));
    m_dirty.insert(output);
    m_unwritten.insert(output);
    if (m_settings) {
        m_syncTimer.start();
        m_writeTimer.start();
    } else {
        writeFilters();
    }
    changed(output);
}

void EqControl::writeFilters()
{
    const QSet<QString> unwritten = std::exchange(m_unwritten, {});
    for (const QString &output : unwritten)
        put(output, QStringLiteral("filters"), EqFilters::encode(m_live.value(output)));
}

void EqControl::changed(const QString &output)
{
    ++m_revision;
    emit revisionChanged();
    emit profilesChanged();
    emit profileChanged(output);
    if (output == m_activeOutput)
        emit activeProfileChanged();
}

bool EqControl::addBand(const QString &output)
{
    if (!editable(output) || profile(output).count >= EqProfile::bandCount)
        return false;
    EqProfile *p = beginEdit(output);
    if (!p)
        return false;
    p->bands[p->count++] = EqProfile::freshBand();
    endEdit(output);
    return true;
}

bool EqControl::removeBand(const QString &output)
{
    if (!editable(output) || profile(output).count <= 1)
        return false;
    EqProfile *p = beginEdit(output);
    if (!p)
        return false;
    p->count--;
    endEdit(output);
    return true;
}

void EqControl::stepBand(const QString &output, int band, const QString &field, int direction, bool coarse)
{
    if (field == QLatin1StringView("type"))
        stepType(output, band, direction);
    else if (field == QLatin1StringView("freq"))
        stepFrequency(output, band, direction, coarse);
    else if (field == QLatin1StringView("gain"))
        stepGain(output, band, direction, coarse);
    else if (field == QLatin1StringView("q"))
        stepQ(output, band, direction, coarse);
    else
        qWarning() << "microdap eq: unknown band field" << field;
}

}
