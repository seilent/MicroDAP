#include "AudioInfo.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace casdap {

namespace {

constexpr int pollMs = 2000;
constexpr int dumpTimeoutMs = 5000;
constexpr int allowedRates[] = {44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000};

int rateOf(const QJsonValue &value)
{
    if (value.isDouble())
        return value.toInt();
    if (value.isString())
        return value.toString().toInt();
    return 0;
}

QJsonValue metadataValue(const QJsonArray &objects, const QString &name, const QString &key)
{
    for (const QJsonValue &entry : objects) {
        const QJsonObject object = entry.toObject();
        if (object.value(QStringLiteral("type")).toString() != QLatin1StringView("PipeWire:Interface:Metadata"))
            continue;
        const QJsonObject props = object.value(QStringLiteral("props")).toObject();
        if (props.value(QStringLiteral("metadata.name")).toString() != name)
            continue;
        const QJsonArray rows = object.value(QStringLiteral("metadata")).toArray();
        for (const QJsonValue &rowValue : rows) {
            const QJsonObject row = rowValue.toObject();
            if (row.value(QStringLiteral("key")).toString() == key)
                return row.value(QStringLiteral("value"));
        }
    }
    return {};
}

QJsonObject nodeNamed(const QJsonArray &objects, const QString &name)
{
    for (const QJsonValue &entry : objects) {
        const QJsonObject object = entry.toObject();
        if (object.value(QStringLiteral("type")).toString() != QLatin1StringView("PipeWire:Interface:Node"))
            continue;
        const QJsonObject info = object.value(QStringLiteral("info")).toObject();
        const QJsonObject props = info.value(QStringLiteral("props")).toObject();
        if (props.value(QStringLiteral("node.name")).toString() == name)
            return info;
    }
    return {};
}

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

}

AudioInfo::AudioInfo(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(pollMs);
    connect(&m_timer, &QTimer::timeout, this, &AudioInfo::refresh);
}

AudioInfo::~AudioInfo()
{
    if (!m_dump)
        return;
    m_dump->disconnect(this);
    if (m_dump->state() != QProcess::NotRunning) {
        m_dump->kill();
        m_dump->waitForFinished(1000);
    }
}

QString AudioInfo::pcmFormat() const
{
    return m_pcmFormat;
}

int AudioInfo::graphRate() const
{
    return m_graphRate;
}

int AudioInfo::hwRate() const
{
    return m_hwRate;
}

bool AudioInfo::active() const
{
    return m_active;
}

void AudioInfo::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (m_active) {
        refresh();
        m_timer.start();
    } else {
        m_timer.stop();
    }
    emit activeChanged();
}

QString AudioInfo::asoundDir()
{
    return qEnvironmentVariable("CASDAP_ASOUND_DIR", QStringLiteral("/proc/asound"));
}

QString AudioInfo::parseState(const QString &text)
{
    static const QRegularExpression stateRe(QStringLiteral("^state:\\s*(\\S+)"), QRegularExpression::MultilineOption);
    return stateRe.match(text).captured(1);
}

QString AudioInfo::hwParamsPath()
{
    const QDir root(asoundDir());
    const auto dirs = QDir::Dirs | QDir::NoDotAndDotDot;
    QString open;
    for (const QString &card : root.entryList({QStringLiteral("card*")}, dirs, QDir::Name)) {
        const QDir cardDir(root.filePath(card));
        for (const QString &pcm : cardDir.entryList({QStringLiteral("pcm*p")}, dirs, QDir::Name)) {
            const QString sub = cardDir.filePath(pcm + QStringLiteral("/sub0"));
            const QString params = sub + QStringLiteral("/hw_params");
            if (parse(readText(params)).isEmpty())
                continue;
            if (parseState(readText(sub + QStringLiteral("/status"))) == QLatin1StringView("RUNNING"))
                return params;
            if (open.isEmpty())
                open = params;
        }
    }
    return open;
}

QString AudioInfo::pwDumpPath()
{
    const QString value = qEnvironmentVariable("CASDAP_PW_DUMP_BIN", QStringLiteral("pw-dump"));
    if (value.contains(QLatin1Char('/')))
        return QFile::exists(value) ? value : QString();
    return QStandardPaths::findExecutable(value);
}

QString AudioInfo::parse(const QString &text)
{
    static const QRegularExpression formatRe(QStringLiteral("^format:\\s*(\\S+)"), QRegularExpression::MultilineOption);
    static const QRegularExpression rateRe(QStringLiteral("^rate:\\s*(\\d+)"), QRegularExpression::MultilineOption);
    const QRegularExpressionMatch format = formatRe.match(text);
    const QRegularExpressionMatch rate = rateRe.match(text);
    if (!format.hasMatch() || !rate.hasMatch())
        return {};
    return rate.captured(1) + QLatin1Char(' ') + format.captured(1);
}

int AudioInfo::parseGraphRate(const QByteArray &json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isArray())
        return 0;
    const QJsonArray objects = document.array();

    const QJsonValue sink = metadataValue(objects, QStringLiteral("default"), QStringLiteral("default.audio.sink"));
    const QString sinkName = sink.toObject().value(QStringLiteral("name")).toString();
    if (!sinkName.isEmpty()) {
        const QJsonObject info = nodeNamed(objects, sinkName);
        const QJsonObject props = info.value(QStringLiteral("props")).toObject();
        if (const int rate = rateOf(props.value(QStringLiteral("clock.rate"))); rate > 0)
            return rate;
        const QJsonArray formats = info.value(QStringLiteral("params")).toObject()
                                       .value(QStringLiteral("Format")).toArray();
        if (!formats.isEmpty()) {
            if (const int rate = rateOf(formats.first().toObject().value(QStringLiteral("rate"))); rate > 0)
                return rate;
        }
    }

    return rateOf(metadataValue(objects, QStringLiteral("settings"), QStringLiteral("clock.rate")));
}

UsbStream AudioInfo::streamFor(int card)
{
    static const QRegularExpression rangeRe(QStringLiteral("^(\\d+)\\s*-\\s*(\\d+)\\s*\\(continuous\\)"));
    static const QRegularExpression separatorRe(QStringLiteral("[,\\s]+"));
    UsbStream stream;
    const QString text = readText(QDir(asoundDir()).filePath(QStringLiteral("card%1/stream0").arg(card)));
    bool playback = false;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        if (!raw.at(0).isSpace()) {
            playback = line == QLatin1StringView("Playback:");
            continue;
        }
        if (!playback)
            continue;
        if (line.startsWith(QLatin1StringView("Format:"))) {
            for (const QString &format : line.mid(7).split(separatorRe, Qt::SkipEmptyParts)) {
                if (!stream.formats.contains(format))
                    stream.formats.append(format);
            }
        } else if (line.startsWith(QLatin1StringView("Rates:"))) {
            const QString value = line.mid(6).trimmed();
            const auto add = [&stream](int rate) {
                if (rate > 0 && !stream.rates.contains(rate))
                    stream.rates.append(rate);
            };
            if (const QRegularExpressionMatch range = rangeRe.match(value); range.hasMatch()) {
                const int low = range.captured(1).toInt();
                const int high = range.captured(2).toInt();
                for (const int rate : allowedRates) {
                    if (rate >= low && rate <= high)
                        add(rate);
                }
            } else {
                for (const QString &token : value.split(separatorRe, Qt::SkipEmptyParts))
                    add(token.toInt());
            }
        }
    }
    std::sort(stream.rates.begin(), stream.rates.end());
    return stream;
}

QList<int> AudioInfo::ratesFor(int card)
{
    const QList<int> offered = streamFor(card).rates;
    QList<int> rates;
    for (const int rate : allowedRates) {
        if (offered.contains(rate))
            rates.append(rate);
    }
    return rates;
}

void AudioInfo::refresh()
{
    const QString path = hwParamsPath();
    const QString value = path.isEmpty() ? QString() : parse(readText(path));
    const int hwRate = value.section(QLatin1Char(' '), 0, 0).toInt();
    const bool hwRateMoved = hwRate != m_hwRate;
    m_hwRate = hwRate;
    if (value != m_pcmFormat) {
        m_pcmFormat = value;
        emit pcmFormatChanged();
    }
    if (hwRateMoved)
        emit hwRateChanged();
    if (m_pwRate == 0)
        updateGraphRate();
}

void AudioInfo::refreshGraphRate()
{
    const QString program = pwDumpPath();
    if (program.isEmpty()) {
        m_pwRate = 0;
        updateGraphRate();
        return;
    }
    if (m_dump)
        return;
    m_dump = new QProcess(this);
    connect(m_dump, &QProcess::finished, this, &AudioInfo::onDumpFinished);
    connect(m_dump, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        qWarning() << "casdap audio: cannot start" << m_dump->program();
        m_dump->deleteLater();
        m_dump = nullptr;
        m_pwRate = 0;
        updateGraphRate();
    });
    QTimer::singleShot(dumpTimeoutMs, m_dump, [process = m_dump] {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
    m_dump->start(program, {});
}

void AudioInfo::onDumpFinished(int code, QProcess::ExitStatus status)
{
    QProcess *process = m_dump;
    m_dump = nullptr;
    process->deleteLater();
    if (status != QProcess::NormalExit || code != 0) {
        m_pwRate = 0;
        updateGraphRate();
        return;
    }
    m_pwRate = parseGraphRate(process->readAllStandardOutput());
    updateGraphRate();
}

void AudioInfo::updateGraphRate()
{
    const int rate = m_pwRate > 0 ? m_pwRate : m_hwRate;
    if (rate == m_graphRate)
        return;
    m_graphRate = rate;
    emit graphRateChanged();
}

}
