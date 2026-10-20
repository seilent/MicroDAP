#include "Eq.h"

#include "EqFit.h"
#include "OutputControl.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
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

constexpr std::array<std::pair<EqBand::Type, const char *>, 8> typeKeys{{
    {EqBand::Peak, "peak"},
    {EqBand::LowShelf, "lowshelf"},
    {EqBand::HighShelf, "highshelf"},
    {EqBand::BandPass, "bandpass"},
    {EqBand::LowPass, "lowpass"},
    {EqBand::HighPass, "highpass"},
    {EqBand::AllPass, "allpass"},
    {EqBand::MatchedPeak, "matchedpeak"},
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
    return type != EqBand::Peak && type != EqBand::LowShelf && type != EqBand::HighShelf
           && type != EqBand::MatchedPeak;
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

EqProfile EqProfile::graphic(const std::array<double, bandCount> &gains)
{
    EqProfile profile = flat();
    for (int i = 0; i < bandCount; ++i) {
        profile.bands[i].type = EqBand::MatchedPeak;
        profile.bands[i].q = graphicQ;
        profile.bands[i].gain = gains[i];
    }
    const double peak = profile.boostPeak();
    profile.preamp = peak > 0.0 ? qMax(EqParser::minPreamp, -std::ceil((peak + 0.05) * 10.0) / 10.0) : 0.0;
    return profile;
}

double EqProfile::boostPeak() const
{
    double peak = 1.0;
    for (const double rate : boostRates) {
        std::vector<BiquadCoeffs> coeffs;
        std::vector<double> centres;
        for (int i = 0; i < count; ++i) {
            const EqBand &band = bands[i];
            if (!band.enabled || band.gain <= 0.0 || gainFree(band.type))
                continue;
            const BiquadCoeffs c = biquadFor(BiquadType(int(band.type)), band.freq, band.gain, band.q, rate);
            if (c.b.size() < 3 || c.guarded)
                continue;
            coeffs.push_back(c);
            centres.push_back(band.freq);
        }
        if (coeffs.empty())
            continue;
        const auto at = [&](double f) {
            double m = 1.0;
            for (const BiquadCoeffs &c : coeffs) {
                const double v = biquadMagnitude(c, f, rate);
                m *= v * v;
            }
            return m;
        };
        const double nyquist = rate / 2.0;
        const double low = 10.0;
        const int steps = int(std::ceil(24.0 * std::log2(nyquist / low)));
        const double ratio = std::pow(nyquist / low, 1.0 / steps);
        double best = 1.0;
        double bestF = 0.0;
        double f = low;
        for (int i = 0; i <= steps; ++i, f *= ratio) {
            const double m = at(std::min(f, nyquist));
            if (m > best) {
                best = m;
                bestF = f;
            }
        }
        for (const double centre : centres)
            best = std::max(best, at(centre));
        if (bestF > 0.0) {
            double a = bestF / ratio;
            double b = std::min(bestF * ratio, nyquist);
            for (int i = 0; i < 40; ++i) {
                const double m1 = a + (b - a) * 0.381966;
                const double m2 = b - (b - a) * 0.381966;
                if (at(m1) < at(m2))
                    a = m1;
                else
                    b = m2;
            }
            best = std::max(best, at((a + b) / 2.0));
        }
        peak = std::max(peak, best);
    }
    return 10.0 * std::log10(peak);
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
    , m_fitOptions(std::make_unique<EqFitOptions>())
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
    m_fitCancel = true;
    if (m_fitThread.joinable())
        m_fitThread.join();
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

QString EqControl::graphicDir()
{
    return sourceDir() + QStringLiteral("/graphic");
}

QString EqControl::settingKey(const QString &name, const QString &output)
{
    return QStringLiteral("microdap.") + name + QLatin1Char('_') + output;
}

QString EqControl::gainsText(const std::array<double, EqProfile::bandCount> &gains)
{
    QString out = QStringLiteral("[General]\n");
    for (int i = 0; i < EqProfile::bandCount; ++i)
        out += QStringLiteral("gain") + QString::number(i + 1) + QLatin1Char('=') + QString::number(gains[i], 'f', 1)
               + QLatin1Char('\n');
    return out;
}

std::optional<std::array<double, EqProfile::bandCount>> EqControl::readGains(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;
    std::array<double, EqProfile::bandCount> gains{};
    bool general = false;
    bool any = false;
    for (const QString &raw : QString::fromUtf8(file.read(64 * 1024)).split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            general = line.compare(QLatin1StringView("[general]"), Qt::CaseInsensitive) == 0;
            continue;
        }
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (!general || eq < 0)
            continue;
        const QString key = line.left(eq).trimmed().toLower();
        if (!key.startsWith(QLatin1StringView("gain")))
            continue;
        bool okIndex = false;
        bool okValue = false;
        const int index = key.mid(4).toInt(&okIndex);
        const double value = line.mid(eq + 1).trimmed().toDouble(&okValue);
        if (!okIndex || !okValue || index < 1 || index > EqProfile::bandCount || !std::isfinite(value))
            continue;
        gains[index - 1] = qBound(EqParser::minGain, std::round(value * 10.0) / 10.0, EqParser::maxGain);
        any = true;
    }
    if (!any)
        return std::nullopt;
    return gains;
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

QVariant EqControl::stored(const QString &key) const
{
    return m_settings ? m_settings->value(key) : m_local.value(key);
}

void EqControl::store(const QString &key, const QVariant &value)
{
    if (m_settings) {
        m_settings->setValue(key, value);
        return;
    }
    if (m_local.value(key) == value)
        return;
    m_local.insert(key, value);
    onValueChanged(key);
}

bool EqControl::on(const QString &output) const
{
    const auto pending = m_pendingOn.constFind(output);
    if (pending != m_pendingOn.constEnd())
        return *pending;
    return stored(EqFilters::key(output, QStringLiteral("on"))).toBool();
}

QString EqControl::storedName(const QString &output) const
{
    return stored(EqFilters::key(output, QStringLiteral("name"))).toString();
}

QStringList EqControl::storedFilters(const QString &output) const
{
    return stored(EqFilters::key(output, QStringLiteral("filters"))).toStringList();
}

void EqControl::put(const QString &output, const QString &field, const QVariant &value)
{
    store(EqFilters::key(output, field), value);
}

void EqControl::onValueChanged(const QString &key)
{
    for (const QString &output : outputs()) {
        for (const QString &name : {QStringLiteral("eq_mode"), QStringLiteral("eq_graphic"),
                                    QStringLiteral("eq_graphic_preset"), QStringLiteral("eq_parametric_on")}) {
            if (key != settingKey(name, output))
                continue;
            if (name == QLatin1StringView("eq_graphic") && !m_gainsDirty.contains(output))
                m_gains.remove(output);
            if (graphic(output) && !m_unwritten.contains(output))
                m_live.remove(output);
            changed(output);
            return;
        }
    }
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
        if (!graphic(output) && on(output) && storedName(output).isEmpty()) {
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
        it = m_live.insert(output, graphic(output) ? graphicProfile(output) : restored(output));
    return *it;
}

EqProfile EqControl::restored(const QString &output) const
{
    const QStringList filters = storedFilters(output);
    const QString storedLabel = storedName(output);
    const auto sound = [](QStringList items) {
        static const QRegularExpression flat(QStringLiteral("^peak \\S+ 0 \\S+$"));
        while (items.size() > 1 && flat.match(items.last()).hasMatch())
            items.removeLast();
        return items;
    };
    if (storedLabel.isEmpty()) {
        const EqProfile &c = custom(output);
        if (sound(EqFilters::encode(c)) == sound(filters))
            return c;
    } else {
        for (const QString &path : m_sourcePaths) {
            if (displayName(path) != storedLabel)
                continue;
            const std::optional<EqProfile> file = fileProfile(path);
            if (file && EqFilters::encode(*file) == filters)
                return *file;
        }
    }
    return EqFilters::decode(filters);
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

QString EqControl::paramKey(const QString &output) const
{
    if (!outputs().contains(output))
        return QStringLiteral("off");
    const bool enabled = graphic(output) ? stored(settingKey(QStringLiteral("eq_parametric_on"), output)).toBool()
                                         : on(output);
    if (!enabled)
        return QStringLiteral("off");
    const QString name = storedName(output);
    return name.isEmpty() ? QStringLiteral("custom") : name;
}

QString EqControl::key(const QString &output) const
{
    if (!graphic(output))
        return paramKey(output);
    const QString preset = graphicPreset(output);
    if (preset == QLatin1StringView("off") || graphicMissing(output))
        return QStringLiteral("off");
    return QStringLiteral("graphic:") + preset;
}

QString EqControl::name(const QString &output) const
{
    const QString k = graphic(output) ? graphicPreset(output) : paramKey(output);
    if (k == QLatin1StringView("off"))
        return QStringLiteral("OFF");
    if (k == QLatin1StringView("custom"))
        return QStringLiteral("CUSTOM");
    if (k == QLatin1StringView("flat"))
        return QStringLiteral("FLAT");
    return k;
}

bool EqControl::missing(const QString &output) const
{
    return graphic(output) && graphicMissing(output);
}

bool EqControl::editable(const QString &output) const
{
    return key(output) != QLatin1StringView("off");
}

QVariantMap EqControl::profileMap(const QString &output) const
{
    QVariantMap map = profile(output).toMap();
    const bool g = graphic(output);
    if (g && (graphicPreset(output) == QLatin1StringView("off") || graphicMissing(output)))
        map.insert(QStringLiteral("bands"), QVariantList());
    map.insert(QStringLiteral("mode"), g ? QStringLiteral("graphic") : QStringLiteral("parametric"));
    map.insert(QStringLiteral("key"), g ? graphicPreset(output) : paramKey(output));
    map.insert(QStringLiteral("name"), name(output));
    map.insert(QStringLiteral("missing"), missing(output));
    map.insert(QStringLiteral("editable"), editable(output));
    map.insert(QStringLiteral("saved"), saved(output));
    return map;
}

const QStringList &EqControl::paths(bool graphicList) const
{
    return graphicList ? m_graphicPaths : m_sourcePaths;
}

QString EqControl::entryName(const QString &path, bool graphicList) const
{
    return graphicList ? QFileInfo(path).completeBaseName().toUpper().left(nameMax) : displayName(path);
}

QString EqControl::pathOfName(const QString &name, bool graphicList) const
{
    for (const QString &path : paths(graphicList)) {
        if (entryName(path, graphicList) == name)
            return path;
    }
    return {};
}

QString EqControl::pathOf(const QString &key) const
{
    return pathOfName(key, false);
}

QString EqControl::graphicPathOf(const QString &name) const
{
    return pathOfName(name, true);
}

bool EqControl::saved(const QString &output) const
{
    const bool g = graphic(output);
    const QString k = g ? graphicPreset(output) : paramKey(output);
    if (k == QLatin1StringView("off") || k == QLatin1StringView("custom") || k == QLatin1StringView("flat"))
        return false;
    const QString path = pathOfName(k, g);
    return !path.isEmpty() && QFileInfo(path).absolutePath() == QDir::cleanPath(g ? graphicDir() : presetDir());
}

QString EqControl::presetName(const QString &output) const
{
    if (!saved(output))
        return {};
    const bool g = graphic(output);
    return QFileInfo(pathOfName(g ? graphicPreset(output) : paramKey(output), g)).completeBaseName();
}

QString EqControl::savePreset(const QString &output)
{
    const bool g = graphic(output);
    if ((g ? graphicPreset(output) : paramKey(output)) != QLatin1StringView("custom"))
        return QStringLiteral("preset not saved");
    switch (presetSave(output, g, QString(), profile(output), customGains(output), false, nullptr)) {
    case EqRefusal::None:
        return {};
    case EqRefusal::Full:
        return QStringLiteral("too many presets");
    default:
        return QStringLiteral("preset not saved");
    }
}

int EqControl::presetCount(bool graphicList) const
{
    const QString dir = QDir::cleanPath(graphicList ? graphicDir() : presetDir());
    int count = 0;
    for (const QString &path : paths(graphicList)) {
        if (QFileInfo(path).absolutePath() == dir)
            ++count;
    }
    return count;
}

bool EqControl::readGraphic(const QString &path, std::vector<double> &freqs, std::vector<double> &gains,
                            QStringList *log)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.size() <= graphicFileMax
           && EqFit::parse(file.read(graphicFileMax), freqs, gains, log);
}

QString EqControl::importName(const QString &path)
{
    QString name = QFileInfo(path).fileName();
    if (name.endsWith(QLatin1StringView(".txt"), Qt::CaseInsensitive))
        name.chop(4);
    const auto suffix = QLatin1StringView(" GraphicEQ");
    if (name.size() > suffix.size() && name.endsWith(suffix, Qt::CaseInsensitive))
        name.chop(suffix.size());
    return name;
}

QVariantList EqControl::imports() const
{
    return m_imports;
}

QString EqControl::importing() const
{
    return m_importPath;
}

EqFitOptions &EqControl::fitOptions()
{
    return *m_fitOptions;
}

QString EqControl::importPresetName(const QString &base) const
{
    for (int n = 1; n <= presetsMax + 1; ++n) {
        const QString tail = n == 1 ? QString() : QLatin1Char(' ') + QString::number(n);
        QString b = base;
        while (b.startsWith(QLatin1Char(' ')))
            b.remove(0, 1);
        while (!b.isEmpty() && (b + tail).toUtf8().size() > presetNameMax)
            b.chop(b.size() >= 2 && b.at(b.size() - 1).isLowSurrogate() ? 2 : 1);
        while (b.endsWith(QLatin1Char(' ')))
            b.chop(1);
        const QString name = b + tail;
        if (!nameOk(name))
            continue;
        const QString path = QDir::cleanPath(presetDir() + QLatin1Char('/') + name + QStringLiteral(".txt"));
        bool taken = QFileInfo::exists(path);
        for (const QString &listed : paths(false))
            taken = taken || entryName(listed, false) == entryName(path, false);
        if (!taken)
            return name;
    }
    return {};
}

QString EqControl::importGraphic(const QString &output, const QString &path)
{
    if (!outputs().contains(output) || !m_importPath.isEmpty() || graphic(output) || !m_importPaths.contains(path))
        return {};
    if (presetCount(false) >= presetsMax)
        return QStringLiteral("too many presets");
    if (m_fitThread.joinable())
        m_fitThread.join();
    m_fitCancel = false;
    m_importPath = path;
    m_importName = importName(path);
    m_importOutput = output;
    m_importKey = key(output);
    EqFitOptions options = *m_fitOptions;
    options.cancel = &m_fitCancel;
    m_fitThread = std::thread([this, path, options] {
        std::vector<double> freqs;
        std::vector<double> gains;
        QStringList log;
        const bool read = readGraphic(path, freqs, gains, &log);
        EqProfile profile;
        EqFitStats stats;
        QElapsedTimer clock;
        clock.start();
        const EqFitEnd end = read ? EqFit::fit(freqs, gains, profile, options, &stats) : EqFitEnd::Cancelled;
        const qint64 ms = clock.elapsed();
        for (const QString &line : std::as_const(log))
            qInfo().noquote() << "microdap eq: import" << path << line;
        if (read)
            qInfo().noquote() << "microdap eq: import" << path << stats.iterations << "iterations" << stats.trials
                              << "trials" << ms << "ms" << QString::number(stats.rms, 'f', 2) << "db rms end"
                              << int(end);
        QMetaObject::invokeMethod(this, [this, read, end, profile] { imported(read, end, profile); },
                                  Qt::QueuedConnection);
    });
    emit importingChanged();
    return {};
}

void EqControl::imported(bool read, EqFitEnd end, const EqProfile &profile)
{
    if (m_fitThread.joinable())
        m_fitThread.join();
    const QString name = m_importName;
    const QString output = m_importOutput;
    const bool kept = key(output) == m_importKey && !graphic(output);
    const bool gone = m_fitCancel || !m_importPaths.contains(m_importPath) || !QFileInfo::exists(m_importPath);
    m_importPath.clear();
    m_importName.clear();
    emit importingChanged();
    if (!read || gone || end == EqFitEnd::Cancelled) {
        emit importNote(QStringLiteral("import failed"), name);
        return;
    }
    if (end == EqFitEnd::Boost) {
        emit importNote(QStringLiteral("import failed"), QStringLiteral("boost over 24 db"));
        return;
    }
    if (presetCount(false) >= presetsMax) {
        emit importNote(QStringLiteral("too many presets"), QString());
        return;
    }
    const QString saved = importPresetName(name);
    if (saved.isEmpty()) {
        emit importNote(QStringLiteral("import failed"), name);
        return;
    }
    const QString path = QDir::cleanPath(presetDir() + QLatin1Char('/') + saved + QStringLiteral(".txt"));
    QDir().mkpath(presetDir());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(autoEqText(profile).toUtf8()) < 0
        || !file.commit()) {
        qWarning() << "microdap eq: cannot write preset" << path;
        emit importNote(QStringLiteral("preset not saved"), saved);
        return;
    }
    qInfo() << "microdap eq: preset imported" << path;
    relist();
    const std::optional<EqProfile> parsed = kept ? fileProfile(path) : std::nullopt;
    if (parsed)
        select(output, true, displayName(path), *parsed);
    emit importNote(QStringLiteral("imported"), saved);
}

bool EqControl::deletePreset(const QString &output)
{
    if (!saved(output))
        return false;
    const bool g = graphic(output);
    return presetRemove(g ? graphicPreset(output) : paramKey(output), g) == EqRefusal::None;
}

EqRefusal EqControl::presetRemove(const QString &k, bool g)
{
    const QString folded = k.toLower();
    if (folded == QLatin1StringView("off") || folded == QLatin1StringView("custom")
        || folded == QLatin1StringView("flat"))
        return EqRefusal::ReadOnly;
    const QString path = pathOfName(k, g);
    if (path.isEmpty())
        return EqRefusal::NotFound;
    if (!devicePreset(k, g))
        return EqRefusal::ReadOnly;
    if (!QFile::remove(path)) {
        qWarning() << "microdap eq: cannot remove preset" << path;
        return EqRefusal::Failed;
    }
    qInfo() << "microdap eq: preset deleted" << path;
    relist();
    for (const QString &o : outputs()) {
        if (g && graphicPreset(o) == k) {
            store(settingKey(QStringLiteral("eq_graphic_preset"), o), QStringLiteral("off"));
            if (graphic(o))
                applyGraphic(o, true);
        } else if (!g && paramKey(o) == k) {
            if (graphic(o)) {
                store(settingKey(QStringLiteral("eq_parametric_on"), o), false);
                put(o, QStringLiteral("name"), QString());
            } else {
                select(o, false, QString(), profile(o));
            }
        }
    }
    return EqRefusal::None;
}

QString EqControl::renameRefusal(const QString &output, const QString &name) const
{
    if (!saved(output) || !nameOk(name))
        return QStringLiteral("name not allowed");
    const bool g = graphic(output);
    const QString old = pathOfName(g ? graphicPreset(output) : paramKey(output), g);
    const QString path = QDir::cleanPath((g ? graphicDir() : presetDir()) + QLatin1Char('/') + name
                                         + (g ? QStringLiteral(".ini") : QStringLiteral(".txt")));
    for (const QString &listed : paths(g)) {
        if (listed != old && entryName(listed, g) == entryName(path, g))
            return QStringLiteral("name taken");
    }
    if (path != old && QFileInfo::exists(path))
        return QStringLiteral("name taken");
    return {};
}

bool EqControl::renamePreset(const QString &output, const QString &name)
{
    if (!renameRefusal(output, name).isEmpty())
        return false;
    const bool g = graphic(output);
    return presetRename(g ? graphicPreset(output) : paramKey(output), name, false, g, nullptr) == EqRefusal::None;
}

EqRefusal EqControl::presetRename(const QString &k, const QString &name, bool replace, bool g, QString *to)
{
    const QString folded = k.toLower();
    if (folded == QLatin1StringView("off") || folded == QLatin1StringView("custom")
        || folded == QLatin1StringView("flat"))
        return EqRefusal::ReadOnly;
    const QString old = pathOfName(k, g);
    if (old.isEmpty())
        return EqRefusal::NotFound;
    if (!devicePreset(k, g))
        return EqRefusal::ReadOnly;
    if (!nameOk(name))
        return EqRefusal::Name;
    const QString dir = QDir::cleanPath(g ? graphicDir() : presetDir());
    QString path = QDir::cleanPath(dir + QLatin1Char('/') + name + (g ? QStringLiteral(".ini") : QStringLiteral(".txt")));
    QString clash;
    for (const QString &listed : paths(g)) {
        if (listed != old && entryName(listed, g) == entryName(path, g))
            clash = listed;
    }
    if (clash.isEmpty() && path != old && QFileInfo::exists(path))
        clash = path;
    if (!clash.isEmpty()) {
        if (!replace || QFileInfo(clash).absolutePath() != dir)
            return EqRefusal::Exists;
        path = clash;
    }
    if (to)
        *to = entryName(path, g);
    if (path == old)
        return EqRefusal::None;
    QFile source(old);
    if (!source.open(QIODevice::ReadOnly)) {
        qWarning() << "microdap eq: cannot read preset" << old;
        return EqRefusal::Failed;
    }
    const QByteArray bytes = source.readAll();
    source.close();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) < 0 || !file.commit()) {
        qWarning() << "microdap eq: cannot write preset" << path;
        return EqRefusal::Failed;
    }
    if (!QFile::remove(old))
        qWarning() << "microdap eq: cannot remove preset" << old;
    qInfo() << "microdap eq: preset renamed" << old << "to" << path;
    const QString renamed = entryName(path, g);
    for (const QString &o : outputs()) {
        if (g && graphicPreset(o) == k)
            store(settingKey(QStringLiteral("eq_graphic_preset"), o), renamed);
        else if (!g && paramKey(o) == k)
            put(o, QStringLiteral("name"), renamed);
    }
    relist();
    if (!g)
        reload(renamed);
    return EqRefusal::None;
}

bool EqControl::graphic(const QString &output) const
{
    return stored(settingKey(QStringLiteral("eq_mode"), output)).toString() == QLatin1StringView("graphic");
}

QString EqControl::graphicPreset(const QString &output) const
{
    const QString preset = stored(settingKey(QStringLiteral("eq_graphic_preset"), output)).toString();
    return preset.isEmpty() ? QStringLiteral("flat") : preset;
}

std::array<double, EqProfile::bandCount> EqControl::customGains(const QString &output) const
{
    auto it = m_gains.constFind(output);
    if (it != m_gains.constEnd())
        return *it;
    std::array<double, EqProfile::bandCount> gains{};
    const QStringList items = stored(settingKey(QStringLiteral("eq_graphic"), output)).toStringList();
    for (int i = 0; i < EqProfile::bandCount && i < items.size(); ++i) {
        bool ok = false;
        const double v = items.at(i).toDouble(&ok);
        gains[i] = ok ? qBound(EqParser::minGain, std::round(v * 10.0) / 10.0, EqParser::maxGain) : 0.0;
    }
    m_gains.insert(output, gains);
    return gains;
}

bool EqControl::graphicMissing(const QString &output) const
{
    const QString preset = graphicPreset(output);
    if (preset == QLatin1StringView("off") || preset == QLatin1StringView("flat")
        || preset == QLatin1StringView("custom"))
        return false;
    const QString path = graphicPathOf(preset);
    return path.isEmpty() || !gainFile(path);
}

EqProfile EqControl::graphicProfile(const QString &output) const
{
    const QString preset = graphicPreset(output);
    if (preset == QLatin1StringView("off"))
        return EqProfile::off();
    if (preset == QLatin1StringView("flat"))
        return EqProfile::graphic({});
    if (preset == QLatin1StringView("custom"))
        return EqProfile::graphic(customGains(output));
    const QString path = graphicPathOf(preset);
    const std::optional<std::array<double, EqProfile::bandCount>> gains = path.isEmpty() ? std::nullopt : gainFile(path);
    return gains ? EqProfile::graphic(*gains) : EqProfile::off();
}

void EqControl::applyGraphic(const QString &output, bool now)
{
    const EqProfile p = graphicProfile(output);
    m_live.insert(output, p);
    const bool enabled = graphicPreset(output) != QLatin1StringView("off") && !graphicMissing(output);
    if (!now) {
        defer(output, enabled);
        return;
    }
    m_writeTimer.stop();
    writeFilters();
    put(output, QStringLiteral("filters"), EqFilters::encode(p));
    put(output, QStringLiteral("on"), enabled);
    changed(output);
}

void EqControl::cycleMode(const QString &output, int direction)
{
    if (!outputs().contains(output) || sign(direction) == 0)
        return;
    if (!graphic(output)) {
        store(settingKey(QStringLiteral("eq_parametric_on"), output), on(output));
        store(settingKey(QStringLiteral("eq_mode"), output), QStringLiteral("graphic"));
        applyGraphic(output, false);
        return;
    }
    leaveGraphic(output);
}

void EqControl::leaveGraphic(const QString &output)
{
    const bool enabled = stored(settingKey(QStringLiteral("eq_parametric_on"), output)).toBool();
    const QString label = storedName(output);
    store(settingKey(QStringLiteral("eq_mode"), output), QStringLiteral("parametric"));
    std::optional<EqProfile> p = label.isEmpty() ? std::optional<EqProfile>(custom(output)) : std::nullopt;
    if (!label.isEmpty() && !pathOf(label).isEmpty())
        p = fileProfile(pathOf(label));
    m_live.insert(output, p.value_or(EqProfile::off()));
    defer(output, enabled && p.has_value());
}

void EqControl::defer(const QString &output, bool enabled)
{
    m_pendingOn.insert(output, enabled);
    m_unwritten.insert(output);
    if (m_settings)
        m_writeTimer.start();
    else
        writeFilters();
    changed(output);
}

void EqControl::cycleGraphic(const QString &output, int direction)
{
    const int s = sign(direction);
    if (!outputs().contains(output) || s == 0 || !graphic(output))
        return;
    QStringList list{QStringLiteral("off"), QStringLiteral("flat")};
    for (const QString &path : std::as_const(m_graphicPaths))
        list.append(entryName(path, true));
    list.append(QStringLiteral("custom"));
    const qsizetype at = list.indexOf(graphicPreset(output));
    const qsizetype n = list.size();
    const qsizetype next = at < 0 ? (s > 0 ? 0 : n - 1) : ((at + s) % n + n) % n;
    store(settingKey(QStringLiteral("eq_graphic_preset"), output), list.at(next));
    applyGraphic(output, false);
}

bool EqControl::beginGraphicEdit(const QString &output)
{
    if (!graphic(output))
        return false;
    const QString preset = graphicPreset(output);
    if (preset == QLatin1StringView("off") || graphicMissing(output))
        return false;
    if (preset == QLatin1StringView("custom"))
        return true;
    std::array<double, EqProfile::bandCount> gains{};
    if (preset != QLatin1StringView("flat")) {
        if (const auto file = gainFile(graphicPathOf(preset)))
            gains = *file;
    }
    m_gains.insert(output, gains);
    m_gainsDirty.insert(output);
    store(settingKey(QStringLiteral("eq_graphic_preset"), output), QStringLiteral("custom"));
    return true;
}

void EqControl::stepGraphic(const QString &output, int band, int direction, bool coarse)
{
    if (sign(direction) == 0 || band < 0 || band >= EqProfile::bandCount || !beginGraphicEdit(output))
        return;
    std::array<double, EqProfile::bandCount> gains = customGains(output);
    gains[band] = qBound(EqParser::minGain, stepGrid(gains[band], coarse ? gainCoarse : gainFine, direction, 10.0),
                         EqParser::maxGain);
    m_gains.insert(output, gains);
    m_gainsDirty.insert(output);
    applyGraphic(output, false);
}

bool EqControl::setGraphicGain(const QString &output, int band, double value)
{
    if (!std::isfinite(value) || value < EqParser::minGain || value > EqParser::maxGain || band < 0
        || band >= EqProfile::bandCount || !beginGraphicEdit(output))
        return false;
    std::array<double, EqProfile::bandCount> gains = customGains(output);
    gains[band] = std::round(value * 10.0) / 10.0;
    m_gains.insert(output, gains);
    m_gainsDirty.insert(output);
    applyGraphic(output, false);
    return true;
}

QVariantMap EqControl::snapshot(const QString &output) const
{
    QVariantList gains;
    for (const double g : customGains(output))
        gains.append(g);
    return {
        {QStringLiteral("mode"), graphic(output) ? QStringLiteral("graphic") : QStringLiteral("parametric")},
        {QStringLiteral("preset"), graphicPreset(output)},
        {QStringLiteral("gains"), gains},
        {QStringLiteral("parametricOn"), graphic(output)
                                             ? stored(settingKey(QStringLiteral("eq_parametric_on"), output)).toBool()
                                             : on(output)},
    };
}

void EqControl::restore(const QString &output, const QVariantMap &state)
{
    if (!outputs().contains(output) || state.isEmpty())
        return;
    const bool wantGraphic = state.value(QStringLiteral("mode")).toString() == QLatin1StringView("graphic");
    const bool wasGraphic = graphic(output);
    std::array<double, EqProfile::bandCount> gains{};
    const QVariantList list = state.value(QStringLiteral("gains")).toList();
    for (int i = 0; i < EqProfile::bandCount && i < list.size(); ++i)
        gains[i] = list.at(i).toDouble();
    if (gains != customGains(output)) {
        m_gains.insert(output, gains);
        m_gainsDirty.insert(output);
    }
    store(settingKey(QStringLiteral("eq_graphic_preset"), output), state.value(QStringLiteral("preset")).toString());
    if (wantGraphic) {
        store(settingKey(QStringLiteral("eq_parametric_on"), output), state.value(QStringLiteral("parametricOn")));
        store(settingKey(QStringLiteral("eq_mode"), output), QStringLiteral("graphic"));
        applyGraphic(output, false);
    } else if (wasGraphic) {
        leaveGraphic(output);
    }
}

void EqControl::relist()
{
    m_files.clear();
    m_gainFiles.clear();
    QVariantList list;
    QStringList paths;
    QVariantList importList;
    QStringList importPaths;
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
                if (paths.contains(path) || importPaths.contains(path))
                    continue;
                std::vector<double> freqs;
                std::vector<double> gains;
                if (readGraphic(path, freqs, gains)) {
                    importPaths.append(path);
                    importList.append(QVariantMap{
                        {QStringLiteral("path"), path},
                        {QStringLiteral("name"), importName(path)},
                    });
                    continue;
                }
                const QString name = displayName(path);
                bool listed = name == QLatin1StringView("OFF") || name == QLatin1StringView("CUSTOM");
                for (const QVariant &entry : std::as_const(list))
                    listed = listed || entry.toMap().value(QStringLiteral("name")).toString() == name;
                if (listed || !fileProfile(path))
                    continue;
                paths.append(path);
                list.append(QVariantMap{
                    {QStringLiteral("path"), path},
                    {QStringLiteral("name"), displayName(path)},
                });
            }
        }
    }
    QStringList graphicPaths;
    if (m_settings) {
        QFileInfoList entries = QDir(graphicDir()).entryInfoList({QStringLiteral("*.ini")}, QDir::Files | QDir::Readable);
        std::sort(entries.begin(), entries.end(), [](const QFileInfo &a, const QFileInfo &b) {
            return nameLess(a.fileName(), b.fileName());
        });
        for (const QFileInfo &entry : entries) {
            const QString path = QDir::cleanPath(entry.absoluteFilePath());
            const QString folded = entry.completeBaseName().toLower();
            const bool builtIn = folded == QLatin1StringView("off") || folded == QLatin1StringView("flat")
                                 || folded == QLatin1StringView("custom");
            if (!builtIn && graphicPaths.size() < presetsMax && readGains(path))
                graphicPaths.append(path);
        }
    }
    qInfo() << "microdap eq: listed" << paths.size() << "profiles" << graphicPaths.size() << "graphic presets"
            << importPaths.size() << "imports";
    m_graphicPaths = graphicPaths;
    if (!m_importPath.isEmpty() && !importPaths.contains(m_importPath))
        m_fitCancel = true;
    if (importPaths != m_importPaths) {
        m_importPaths = importPaths;
        m_imports = importList;
        emit importsChanged();
    }
    if (paths != m_sourcePaths) {
        m_sourcePaths = paths;
        m_sources = list;
        emit sourcesChanged();
    }
    for (const QString &output : outputs()) {
        if (graphic(output) && m_live.contains(output) && !(m_live.value(output) == graphicProfile(output)))
            applyGraphic(output, true);
        changed(output);
    }
}

void EqControl::cycleProfile(const QString &output, int direction)
{
    const int s = sign(direction);
    if (!outputs().contains(output) || s == 0)
        return;
    const int n = static_cast<int>(m_sourcePaths.size()) + 2;
    int i = 0;
    const QString k = paramKey(output);
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
    if (!outputs().contains(output) || graphic(output))
        return nullptr;
    const QString k = paramKey(output);
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
    const QHash<QString, bool> pendingOn = std::exchange(m_pendingOn, {});
    for (const QString &output : unwritten)
        put(output, QStringLiteral("filters"), EqFilters::encode(m_live.value(output)));
    for (auto it = pendingOn.cbegin(); it != pendingOn.cend(); ++it)
        put(it.key(), QStringLiteral("on"), it.value());
    for (const QString &output : QSet<QString>(m_gainsDirty)) {
        QStringList items;
        for (const double g : customGains(output))
            items.append(EqFilters::number(g));
        store(settingKey(QStringLiteral("eq_graphic"), output), items);
        m_gainsDirty.remove(output);
    }
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

bool EqControl::setPreamp(const QString &output, double value)
{
    if (!std::isfinite(value) || value < EqParser::minPreamp || value > EqParser::maxPreamp)
        return false;
    EqProfile *profile = beginEdit(output);
    if (!profile)
        return false;
    profile->preamp = std::round(value * 10.0) / 10.0;
    endEdit(output);
    return true;
}

bool EqControl::setBandValue(const QString &output, int band, const QString &field, double value)
{
    if (!std::isfinite(value) || band < 0 || band >= profile(output).count)
        return false;
    const bool freq = field == QLatin1StringView("freq");
    const bool gain = field == QLatin1StringView("gain");
    const bool q = field == QLatin1StringView("q");
    if ((freq && (value < EqParser::minFreq || value > EqParser::maxFreq))
        || (gain && (value < EqParser::minGain || value > EqParser::maxGain))
        || (q && (value < EqParser::minQ || value > EqParser::maxQ)) || (!freq && !gain && !q))
        return false;
    const EqBand current = profile(output).bands[band];
    if (gain && current.enabled && gainFree(current.type))
        return false;
    EqBand *b = beginBandEdit(output, band);
    if (!b)
        return false;
    if (freq) {
        b->freq = std::round(value);
        b->freqIndex = -1;
        for (int g = 0; g < EqGrid::size; ++g) {
            if (EqGrid::frequency(g) == b->freq) {
                b->freqIndex = g;
                break;
            }
        }
    } else if (gain) {
        b->gain = std::round(value * 10.0) / 10.0;
    } else {
        b->q = std::round(value * 100.0) / 100.0;
    }
    endEdit(output);
    return true;
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

EqTarget EqControl::target(const QString &output) const
{
    EqTarget t;
    if (!outputs().contains(output))
        return t;
    t.graphic = graphic(output);
    t.key = paramKey(output);
    t.preset = graphicPreset(output);
    return t;
}

void EqControl::beginRemote()
{
    emit remoteEdit();
}

void EqControl::apply(const QString &output, const EqTarget &t)
{
    if (!outputs().contains(output))
        return;
    m_writeTimer.stop();
    writeFilters();
    if (t.custom) {
        m_custom.insert(output, *t.custom);
        m_dirty.insert(output);
        if (m_settings)
            m_syncTimer.start();
    }
    if (t.gains) {
        m_gains.insert(output, *t.gains);
        m_gainsDirty.insert(output);
    }
    const bool paramOn = t.key != QLatin1StringView("off");
    const QString name = !paramOn ? storedName(output) : t.key == QLatin1StringView("custom") ? QString() : t.key;
    store(settingKey(QStringLiteral("eq_graphic_preset"), output), t.preset);
    if (t.graphic) {
        store(settingKey(QStringLiteral("eq_parametric_on"), output), paramOn);
        put(output, QStringLiteral("name"), name);
        store(settingKey(QStringLiteral("eq_mode"), output), QStringLiteral("graphic"));
        applyGraphic(output, true);
        return;
    }
    const bool wasGraphic = graphic(output);
    store(settingKey(QStringLiteral("eq_mode"), output), QStringLiteral("parametric"));
    if (!wasGraphic && !t.custom && t.key == paramKey(output)) {
        writeFilters();
        changed(output);
        return;
    }
    std::optional<EqProfile> p;
    bool enabled = paramOn;
    if (paramOn && t.key != QLatin1StringView("custom")) {
        if (!pathOf(t.key).isEmpty())
            p = fileProfile(pathOf(t.key));
        enabled = p.has_value();
    }
    select(output, enabled, name, p.value_or(custom(output)));
}

EqProfile EqControl::parametric(const QString &output) const
{
    if (!outputs().contains(output))
        return EqProfile::flat();
    const QString k = paramKey(output);
    if (k != QLatin1StringView("off") && k != QLatin1StringView("custom") && !pathOf(k).isEmpty()) {
        if (const std::optional<EqProfile> file = fileProfile(pathOf(k)))
            return *file;
    }
    return custom(output);
}

EqProfile EqControl::customProfile(const QString &output) const
{
    return outputs().contains(output) ? custom(output) : EqProfile::flat();
}

std::array<double, EqProfile::bandCount> EqControl::customGraphic(const QString &output) const
{
    return customGains(output);
}

std::optional<std::array<double, EqProfile::bandCount>> EqControl::presetGains(const QString &output) const
{
    const QString preset = graphicPreset(output);
    if (preset == QLatin1StringView("off") || graphicMissing(output))
        return std::nullopt;
    if (preset == QLatin1StringView("flat"))
        return std::array<double, EqProfile::bandCount>{};
    if (preset == QLatin1StringView("custom"))
        return customGains(output);
    return gainFile(graphicPathOf(preset));
}

QStringList EqControl::profileKeys() const
{
    QStringList out;
    for (const QString &path : m_sourcePaths)
        out.append(displayName(path));
    return out;
}

QStringList EqControl::graphicKeys() const
{
    QStringList out;
    for (const QString &path : m_graphicPaths)
        out.append(entryName(path, true));
    return out;
}

QStringList EqControl::presetKeys(bool g) const
{
    const QString dir = QDir::cleanPath(g ? graphicDir() : presetDir());
    QStringList out;
    for (const QString &path : paths(g)) {
        if (QFileInfo(path).absolutePath() == dir)
            out.append(entryName(path, g));
    }
    return out;
}

std::optional<std::array<double, EqProfile::bandCount>> EqControl::gainFile(const QString &path) const
{
    auto it = m_gainFiles.constFind(path);
    if (it == m_gainFiles.constEnd())
        it = m_gainFiles.insert(path, readGains(path));
    return *it;
}

void EqControl::reload(const QString &key)
{
    const QString path = pathOf(key);
    if (path.isEmpty())
        return;
    const std::optional<EqProfile> file = fileProfile(path);
    if (!file)
        return;
    for (const QString &o : outputs()) {
        if (!graphic(o) && paramKey(o) == key && !(profile(o) == *file))
            select(o, true, key, *file);
    }
}

bool EqControl::devicePreset(const QString &key, bool g) const
{
    const QString path = pathOfName(key, g);
    return !path.isEmpty() && QFileInfo(path).absolutePath() == QDir::cleanPath(g ? graphicDir() : presetDir());
}

EqRefusal EqControl::presetSave(const QString &output, bool g, const QString &name, const EqProfile &profile,
                                const std::array<double, EqProfile::bandCount> &gains, bool replace, QString *key)
{
    if (!outputs().contains(output))
        return EqRefusal::NotFound;
    const QString dir = QDir::cleanPath(g ? graphicDir() : presetDir());
    const QString suffix = g ? QStringLiteral(".ini") : QStringLiteral(".txt");
    QString path;
    QString clash;
    if (name.isEmpty()) {
        for (int n = 1;; ++n) {
            path = QDir::cleanPath(dir + QStringLiteral("/Preset ") + QString::number(n) + suffix);
            bool taken = QFileInfo::exists(path);
            for (const QString &listed : paths(g))
                taken = taken || entryName(listed, g) == entryName(path, g);
            if (!taken)
                break;
        }
    } else {
        if (!nameOk(name))
            return EqRefusal::Name;
        path = QDir::cleanPath(dir + QLatin1Char('/') + name + suffix);
        for (const QString &listed : paths(g)) {
            if (entryName(listed, g) == entryName(path, g))
                clash = listed;
        }
        if (clash.isEmpty() && QFileInfo::exists(path))
            clash = path;
        if (!clash.isEmpty()) {
            if (!replace || QFileInfo(clash).absolutePath() != dir)
                return EqRefusal::Exists;
            path = clash;
        }
    }
    if (clash.isEmpty() && presetCount(g) >= presetsMax)
        return EqRefusal::Full;
    const QString text = g ? gainsText(gains) : autoEqText(profile);
    QDir().mkpath(dir);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(text.toUtf8()) < 0 || !file.commit()) {
        qWarning() << "microdap eq: cannot write preset" << path;
        return EqRefusal::Failed;
    }
    qInfo() << "microdap eq: preset saved" << path;
    m_files.remove(path);
    relist();
    const QString k = entryName(path, g);
    if (!g)
        reload(k);
    if (!(g ? graphicKeys() : profileKeys()).contains(k))
        return EqRefusal::Failed;
    EqTarget t = target(output);
    t.graphic = g;
    (g ? t.preset : t.key) = k;
    apply(output, t);
    if (key)
        *key = k;
    return EqRefusal::None;
}

}
