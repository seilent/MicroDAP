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

#include <cmath>
#include <utility>

namespace casdap {

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
        qInfo() << "casdap eq:" << what << value << "clamped to" << clamped;
    return clamped;
}

EqBand::Type typeFor(const QString &code)
{
    const QString c = code.toLower();
    if (c == QLatin1StringView("lsc") || c == QLatin1StringView("ls"))
        return EqBand::LowShelf;
    if (c == QLatin1StringView("hsc") || c == QLatin1StringView("hs"))
        return EqBand::HighShelf;
    return EqBand::Peak;
}

QString typeKey(EqBand::Type type)
{
    switch (type) {
    case EqBand::LowShelf:
        return QStringLiteral("lowshelf");
    case EqBand::HighShelf:
        return QStringLiteral("highshelf");
    case EqBand::Peak:
        break;
    }
    return QStringLiteral("peak");
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

EqProfile EqProfile::off()
{
    EqProfile profile = flat();
    for (EqBand &band : profile.bands)
        band.enabled = false;
    return profile;
}

double EqProfile::bandwidth(const EqBand &band)
{
    const double q = qBound(EqParser::minQ, band.q, EqParser::maxQ);
    return qMin(band.freq / q, maxBandwidth);
}

double EqProfile::preampVolume() const
{
    return std::pow(10.0, preamp / 20.0);
}

QVariantMap EqProfile::toMap() const
{
    QVariantList list;
    for (const EqBand &band : bands) {
        list.append(QVariantMap{
            {QStringLiteral("enabled"), band.enabled},
            {QStringLiteral("type"), band.enabled ? typeKey(band.type) : QStringLiteral("off")},
            {QStringLiteral("freq"), band.freq},
            {QStringLiteral("freqIndex"), band.freqIndex},
            {QStringLiteral("gain"), band.gain},
            {QStringLiteral("q"), band.q},
            {QStringLiteral("bandwidth"), bandwidth(band)},
        });
    }
    return {
        {QStringLiteral("preamp"), preamp},
        {QStringLiteral("bands"), list},
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
        QStringLiteral("^filter\\s*(\\d+)\\s*:\\s*(on|off)\\s+(pk|lsc|hsc|ls|hs)\\s+fc\\s+") + number()
            + QStringLiteral("\\s*hz\\s+gain\\s+") + number() + QStringLiteral("\\s*db\\s+q\\s+") + number()
            + QStringLiteral("\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression offRe(QStringLiteral("^filter\\s*\\d+\\s*:\\s*off\\b"),
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
        if (f.hasMatch()) {
            if (count >= EqProfile::bandCount) {
                ++dropped;
                continue;
            }
            EqBand band;
            band.enabled = f.captured(2).compare(QLatin1StringView("on"), Qt::CaseInsensitive) == 0;
            band.type = typeFor(f.captured(3));
            band.freq = clampLogged(f.captured(4).toDouble(), minFreq, maxFreq, "freq");
            band.gain = clampLogged(f.captured(5).toDouble(), minGain, maxGain, "gain");
            band.q = clampLogged(f.captured(6).toDouble(), minQ, maxQ, "q");
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
        qInfo() << "casdap eq: line" << lineNo << "skipped:" << line;
    }
    if (dropped > 0)
        qInfo() << "casdap eq:" << dropped << "filters past the first" << EqProfile::bandCount << "dropped";
    if (count == 0) {
        if (error)
            *error = QStringLiteral("no valid filter");
        return std::nullopt;
    }
    for (int i = count; i < EqProfile::bandCount; ++i)
        profile.bands[i].enabled = false;
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
    for (const EqBand &band : profile.bands) {
        if (band.enabled && band.gain > 0.0)
            sum += band.gain;
    }
    return sum > 0.0 ? qMax(minPreamp, -sum) : 0.0;
}

QString EqStore::typeName(const EqBand &band)
{
    return band.enabled ? typeKey(band.type) : QStringLiteral("off");
}

QString EqStore::text(const EqProfile &profile)
{
    QString out = QStringLiteral("[General]\npreamp=") + formatNumber(profile.preamp) + QLatin1Char('\n');
    for (int i = 0; i < EqProfile::bandCount; ++i) {
        const EqBand &band = profile.bands[i];
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
        return EqProfile::flat();
    QSettings settings(path, QSettings::IniFormat);
    if (settings.status() != QSettings::NoError) {
        qWarning() << "casdap eq: cannot parse" << path;
        return EqProfile::flat();
    }
    EqProfile profile;
    bool ok = false;
    profile.preamp = qBound(EqParser::minPreamp, settings.value(QStringLiteral("preamp")).toDouble(&ok),
                            EqParser::maxPreamp);
    if (!ok) {
        qWarning() << "casdap eq: no preamp in" << path;
        return EqProfile::flat();
    }
    for (int i = 0; i < EqProfile::bandCount; ++i) {
        settings.beginGroup(QStringLiteral("band") + QString::number(i + 1));
        EqBand band;
        const QString type = settings.value(QStringLiteral("type")).toString().toLower();
        bool valid = type == QLatin1StringView("off") || type == QLatin1StringView("peak")
                     || type == QLatin1StringView("lowshelf") || type == QLatin1StringView("highshelf");
        band.enabled = type != QLatin1StringView("off");
        band.type = type == QLatin1StringView("lowshelf") ? EqBand::LowShelf
                    : type == QLatin1StringView("highshelf") ? EqBand::HighShelf : EqBand::Peak;
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
            qWarning() << "casdap eq: band" << i + 1 << "unreadable in" << path;
            return EqProfile::flat();
        }
        profile.bands[i] = band;
    }
    return profile;
}

bool EqStore::write(const QString &path, const EqProfile &profile)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qWarning() << "casdap eq: cannot create" << QFileInfo(path).absolutePath();
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "casdap eq: cannot write" << path;
        return false;
    }
    file.write(text(profile).toUtf8());
    if (!file.commit()) {
        qWarning() << "casdap eq: cannot commit" << path;
        return false;
    }
    qInfo() << "casdap eq: store written" << path;
    return true;
}

EqControl::EqControl(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_syncTimer.setSingleShot(true);
    m_syncTimer.setInterval(syncDelayMs);
    connect(&m_syncTimer, &QTimer::timeout, this, &EqControl::sync);
    m_sinkTimer.setInterval(sinkPollMs);
    connect(&m_sinkTimer, &QTimer::timeout, this, &EqControl::pollSink);
    if (QCoreApplication *app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this, &EqControl::sync);
    if (m_settings) {
        connect(m_settings, &Settings::eqHeadphonesChanged, this, [this] { keyChanged(QStringLiteral("headphones")); });
        connect(m_settings, &Settings::eqUsbChanged, this, [this] { keyChanged(QStringLiteral("usb")); });
        connect(m_settings, &Settings::eqSpeakerChanged, this, [this] { keyChanged(QStringLiteral("speaker")); });
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
    return {QStringLiteral("headphones"), QStringLiteral("usb"), QStringLiteral("speaker")};
}

QString EqControl::configDir()
{
    return qEnvironmentVariable("CASDAP_EQ_DIR",
                                QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                                    + QStringLiteral("/casdap"));
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
    return name.toUpper();
}

QString EqControl::outputForSink(const QString &sink)
{
    if (sink.isEmpty())
        return {};
    if (sink.startsWith(OutputControl::usbPrefix()))
        return QStringLiteral("usb");
    if (sink == OutputControl::headphonesSink())
        return QStringLiteral("headphones");
    return QStringLiteral("speaker");
}

QString EqControl::defaultSinkOutput()
{
    QProcess process;
    process.start(OutputControl::pactlPath(), {QStringLiteral("get-default-sink")});
    if (!process.waitForStarted(sinkTimeoutMs)) {
        qWarning() << "casdap eq: cannot start" << OutputControl::pactlPath();
        return {};
    }
    if (!process.waitForFinished(sinkTimeoutMs)) {
        process.kill();
        process.waitForFinished(sinkTimeoutMs);
        qWarning() << "casdap eq: get-default-sink timed out";
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
    if (m_sinkProcess) {
        if (m_sinkProcess->state() != QProcess::NotRunning) {
            qWarning() << "casdap eq: get-default-sink timed out";
            dropSinkProcess();
        }
        return;
    }
    auto *process = new QProcess(this);
    m_sinkProcess = process;
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        if (m_sinkProcess == process)
            m_sinkProcess = nullptr;
        process->deleteLater();
        if (status != QProcess::NormalExit || code != 0)
            return;
        m_sinkWarned = false;
        setActiveOutput(outputForSink(QString::fromUtf8(process->readAllStandardOutput()).trimmed()));
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        if (m_sinkProcess == process)
            m_sinkProcess = nullptr;
        process->deleteLater();
        if (!m_sinkWarned)
            qWarning() << "casdap eq: cannot start" << process->program();
        m_sinkWarned = true;
    });
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
            qWarning() << "casdap eq: unknown output" << output;
        return;
    }
    if (m_activeOutput == output)
        return;
    m_activeOutput = output;
    qInfo() << "casdap eq: active output" << output << "key" << key(output);
    emit activeOutputChanged();
    emit activeProfileChanged();
    applyActive();
}

QString EqControl::activeName() const
{
    return m_activeOutput.isEmpty() ? QStringLiteral("OFF") : name(m_activeOutput);
}

QVariantMap EqControl::activeProfileMap() const
{
    return m_applied.toMap();
}

EqProfile EqControl::activeProfile() const
{
    return m_applied;
}

int EqControl::revision() const
{
    return m_revision;
}

EqProfile EqControl::profile(const QString &output) const
{
    const QString k = key(output);
    if (k == QLatin1StringView("off"))
        return EqProfile::off();
    if (k == QLatin1StringView("custom"))
        return custom(output);
    const std::optional<EqProfile> parsed = fileProfile(k);
    return parsed ? *parsed : EqProfile::off();
}

void EqControl::setCustom(const QString &output, const EqProfile &profile)
{
    if (!outputs().contains(output))
        return;
    m_custom.insert(output, profile);
    ++m_loads;
    m_editing = true;
    setKey(output, QStringLiteral("custom"));
    m_editing = false;
    endEdit(output);
}

QString EqControl::key(const QString &output) const
{
    if (m_settings)
        return m_settings->eq(output);
    return m_keys.value(output, QStringLiteral("off"));
}

QString EqControl::name(const QString &output) const
{
    const QString k = key(output);
    if (k == QLatin1StringView("off"))
        return QStringLiteral("OFF");
    if (k == QLatin1StringView("custom"))
        return QStringLiteral("CUSTOM");
    return fileProfile(k) ? displayName(k) : displayName(k) + QStringLiteral(" (MISSING)");
}

bool EqControl::missing(const QString &output) const
{
    const QString k = key(output);
    if (k == QLatin1StringView("off") || k == QLatin1StringView("custom"))
        return false;
    return !fileProfile(k);
}

bool EqControl::editable(const QString &output) const
{
    const QString k = key(output);
    if (k == QLatin1StringView("off"))
        return false;
    return k == QLatin1StringView("custom") || fileProfile(k).has_value();
}

QVariantMap EqControl::profileMap(const QString &output) const
{
    QVariantMap map = profile(output).toMap();
    const QString k = key(output);
    const bool file = k != QLatin1StringView("off") && k != QLatin1StringView("custom");
    map.insert(QStringLiteral("key"), k);
    map.insert(QStringLiteral("name"), file ? displayName(k) : name(output));
    map.insert(QStringLiteral("missing"), missing(output));
    map.insert(QStringLiteral("editable"), editable(output));
    return map;
}

void EqControl::relist()
{
    m_files.clear();
    QVariantList list;
    QStringList paths;
    if (m_settings) {
        QStringList dirs{sourceDir()};
        for (const QString &root : m_settings->roots())
            dirs.append(root + QStringLiteral("/EQ"));
        for (const QString &dir : std::as_const(dirs)) {
            const QFileInfoList entries = QDir(dir).entryInfoList(
                {QStringLiteral("*.txt")}, QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
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
    qInfo() << "casdap eq: listed" << paths.size() << "profiles";
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
    QStringList cycle{QStringLiteral("off")};
    cycle.append(m_sourcePaths);
    cycle.append(QStringLiteral("custom"));
    const int n = static_cast<int>(cycle.size());
    const int i = static_cast<int>(cycle.indexOf(key(output)));
    const int next = i < 0 ? (s > 0 ? 0 : n - 1) : ((i + s) % n + n) % n;
    setKey(output, cycle.at(next));
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
    const int state = b->enabled ? static_cast<int>(b->type) + 1 : 0;
    const int next = ((state + s) % 4 + 4) % 4;
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
    if (sign(direction) == 0)
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
        qWarning() << "casdap eq: profile left out," << error;
    m_files.insert(path, parsed);
    if (path == key(m_activeOutput))
        ++m_loads;
    return parsed;
}

EqProfile &EqControl::custom(const QString &output) const
{
    auto it = m_custom.find(output);
    if (it == m_custom.end()) {
        it = m_custom.insert(output, m_settings ? EqStore::read(storePath(output)) : EqProfile::flat());
        if (output == m_activeOutput)
            ++m_loads;
    }
    return *it;
}

void EqControl::setKey(const QString &output, const QString &value)
{
    if (m_settings) {
        m_settings->setEq(output, value);
        return;
    }
    const QString normalized = Settings::normalizeEq(value);
    if (m_keys.value(output, QStringLiteral("off")) == normalized)
        return;
    m_keys.insert(output, normalized);
    keyChanged(output);
}

void EqControl::keyChanged(const QString &output)
{
    const QString k = key(output);
    if (k != QLatin1StringView("off") && k != QLatin1StringView("custom"))
        m_files.remove(k);
    qInfo() << "casdap eq:" << output << "key" << k;
    changed(output);
}

EqProfile *EqControl::beginEdit(const QString &output)
{
    if (!outputs().contains(output))
        return nullptr;
    const QString k = key(output);
    if (k == QLatin1StringView("custom"))
        return &custom(output);
    if (k == QLatin1StringView("off"))
        return nullptr;
    const std::optional<EqProfile> parsed = fileProfile(k);
    if (!parsed)
        return nullptr;
    m_custom.insert(output, *parsed);
    qInfo() << "casdap eq:" << output << "copied" << k << "into" << storePath(output);
    m_editing = true;
    setKey(output, QStringLiteral("custom"));
    m_editing = false;
    return &custom(output);
}

EqBand *EqControl::beginBandEdit(const QString &output, int band)
{
    if (band < 0 || band >= EqProfile::bandCount)
        return nullptr;
    EqProfile *profile = beginEdit(output);
    return profile ? &profile->bands[band] : nullptr;
}

void EqControl::endEdit(const QString &output)
{
    m_dirty.insert(output);
    if (m_settings)
        m_syncTimer.start();
    changed(output);
}

void EqControl::changed(const QString &output)
{
    ++m_revision;
    emit revisionChanged();
    emit profilesChanged();
    emit profileChanged(output);
    if (output != m_activeOutput)
        return;
    emit activeProfileChanged();
    if (!m_editing)
        applyActive();
}

void EqControl::applyActive()
{
    if (m_activeOutput.isEmpty())
        return;
    const EqProfile next = profile(m_activeOutput);
    if (m_appliedOnce && next == m_applied)
        return;
    m_applied = next;
    m_appliedOnce = true;
    const QString load = m_activeOutput + QLatin1Char('\n') + key(m_activeOutput) + QLatin1Char('\n')
                         + QString::number(m_loads);
    if (load != m_clampLoad) {
        m_clampLoad = load;
        for (int i = 0; i < EqProfile::bandCount; ++i) {
            const EqBand &band = m_applied.bands[i];
            if (band.freq / qBound(EqParser::minQ, band.q, EqParser::maxQ) > EqProfile::maxBandwidth)
                qInfo() << "casdap eq: band" << i + 1 << "bandwidth clamped to" << EqProfile::maxBandwidth;
        }
    }
    qInfo() << "casdap eq: apply" << m_activeOutput << name(m_activeOutput) << "preamp" << m_applied.preamp;
    emit activeProfileChanged();
    emit profileApplied(m_applied);
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
        qWarning() << "casdap eq: unknown band field" << field;
}

}
