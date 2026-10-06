#include "AudioInfo.h"

#include <QFile>
#include <QRegularExpression>

namespace casdap {

namespace {

constexpr int pollMs = 2000;

}

AudioInfo::AudioInfo(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(pollMs);
    connect(&m_timer, &QTimer::timeout, this, &AudioInfo::refresh);
}

QString AudioInfo::pcmFormat() const
{
    return m_pcmFormat;
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

void AudioInfo::refresh()
{
    QString value;
    QFile file(hwParamsPath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        value = parse(QString::fromUtf8(file.readAll()));
    if (value == m_pcmFormat)
        return;
    m_pcmFormat = value;
    emit pcmFormatChanged();
}

}
