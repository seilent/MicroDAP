#include "AudioInfo.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

namespace casdap {

namespace {

constexpr int pollMs = 2000;
constexpr int dumpTimeoutMs = 5000;

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

QString AudioInfo::hwParamsPath()
{
    const QString dir = qEnvironmentVariable("CASDAP_HW_PARAMS_DIR", QStringLiteral("/proc/asound/card0"));
    const QString pcm = qEnvironmentVariable("CASDAP_HW_PARAMS_PCM", QStringLiteral("1"));
    return dir + QStringLiteral("/pcm") + pcm + QStringLiteral("p/sub0/hw_params");
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

void AudioInfo::refresh()
{
    QString value;
    QFile file(hwParamsPath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        value = parse(QString::fromUtf8(file.readAll()));
    m_hwRate = value.section(QLatin1Char(' '), 0, 0).toInt();
    if (value != m_pcmFormat) {
        m_pcmFormat = value;
        emit pcmFormatChanged();
    }
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
