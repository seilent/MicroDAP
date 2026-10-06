#include "OutputControl.h"

#include <QDebug>
#include <QRegularExpression>

namespace casdap {

namespace {

constexpr int jackPollMs = 2000;
constexpr int processTimeoutMs = 5000;

QString binary(const char *env, const QString &fallback)
{
    const QString value = qEnvironmentVariable(env);
    return value.isEmpty() ? fallback : value;
}

}

OutputControl::OutputControl(QObject *parent)
    : QObject(parent)
    , m_output(QStringLiteral("auto"))
{
    m_jackTimer.setInterval(jackPollMs);
    connect(&m_jackTimer, &QTimer::timeout, this, &OutputControl::refreshJack);
}

OutputControl::~OutputControl()
{
    drop(m_apply);
    drop(m_jack);
}

QStringList OutputControl::outputs()
{
    return {QStringLiteral("auto"), QStringLiteral("speaker"), QStringLiteral("headphones")};
}

QString OutputControl::speakerSink()
{
    return QStringLiteral("alsa_output.platform-sound.HiFi__Speaker__sink");
}

QString OutputControl::headphonesSink()
{
    return QStringLiteral("alsa_output.platform-sound.HiFi__Headphones__sink");
}

QString OutputControl::sinkFor(const QString &output)
{
    if (output == QLatin1StringView("speaker"))
        return speakerSink();
    if (output == QLatin1StringView("headphones"))
        return headphonesSink();
    return {};
}

QString OutputControl::wpctlPath()
{
    return binary("CASDAP_WPCTL_BIN", QStringLiteral("wpctl"));
}

QString OutputControl::pwMetadataPath()
{
    return binary("CASDAP_PW_METADATA_BIN", QStringLiteral("pw-metadata"));
}

QString OutputControl::amixerPath()
{
    return binary("CASDAP_AMIXER_BIN", QStringLiteral("amixer"));
}

int OutputControl::sinkId(const QString &status, const QString &name)
{
    const QRegularExpression re(QStringLiteral("(\\d+)\\.\\s+") + QRegularExpression::escape(name)
                                + QStringLiteral("(?=\\s|$)"));
    const QRegularExpressionMatch match = re.match(status);
    if (!match.hasMatch())
        return -1;
    return match.captured(1).toInt();
}

bool OutputControl::parseJack(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("name='Headphone Jack'[^\\n]*\\n(?:[^\\n]*\\n)?\\s*:\\s*values=(on|off)\\b"));
    const QRegularExpressionMatch match = re.match(text);
    return match.hasMatch() && match.captured(1) == QLatin1StringView("on");
}

QString OutputControl::output() const
{
    return m_output;
}

bool OutputControl::jackPresent() const
{
    return m_jackPresent;
}

bool OutputControl::busy() const
{
    return m_busy;
}

bool OutputControl::active() const
{
    return m_active;
}

void OutputControl::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (m_active) {
        refreshJack();
        m_jackTimer.start();
    } else {
        m_jackTimer.stop();
    }
    emit activeChanged();
}

void OutputControl::apply(const QString &output)
{
    if (!outputs().contains(output)) {
        qWarning() << "casdap output: unknown output" << output;
        return;
    }
    if (m_output != output) {
        m_output = output;
        emit outputChanged();
    }
    drop(m_apply);
    if (!m_busy) {
        m_busy = true;
        emit busyChanged();
    }
    qInfo() << "casdap output: apply" << output;

    const QString sink = sinkFor(output);
    if (sink.isEmpty()) {
        run(m_apply, pwMetadataPath(),
            {QStringLiteral("-d"), QStringLiteral("0"), QStringLiteral("default.configured.audio.sink")},
            [this](bool ok, const QByteArray &) { finish(ok); });
        return;
    }

    run(m_apply, wpctlPath(), {QStringLiteral("status"), QStringLiteral("--name")},
        [this, sink](bool ok, const QByteArray &out) {
        if (!ok) {
            finish(false);
            return;
        }
        const int id = sinkId(QString::fromUtf8(out), sink);
        if (id < 0) {
            qWarning() << "casdap output: sink not listed" << sink;
            finish(false);
            return;
        }
        run(m_apply, wpctlPath(), {QStringLiteral("set-default"), QString::number(id)},
            [this](bool ok, const QByteArray &) { finish(ok); });
    });
}

void OutputControl::refreshJack()
{
    if (m_jack)
        return;
    run(m_jack, amixerPath(),
        {QStringLiteral("-c0"), QStringLiteral("contents")},
        [this](bool ok, const QByteArray &out) { setJackPresent(ok && parseJack(QString::fromUtf8(out))); });
}

void OutputControl::run(QProcess *&slot, const QString &program, const QStringList &args, Handler handler)
{
    drop(slot);
    auto *process = new QProcess(this);
    slot = process;
    connect(process, &QProcess::finished, this,
            [this, &slot, process, handler](int code, QProcess::ExitStatus status) {
        if (slot == process)
            slot = nullptr;
        process->deleteLater();
        const bool ok = status == QProcess::NormalExit && code == 0;
        if (!ok)
            qWarning() << "casdap output:" << process->program() << process->arguments() << "exit" << code;
        handler(ok, process->readAllStandardOutput());
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, &slot, process, handler](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        if (slot == process)
            slot = nullptr;
        process->deleteLater();
        qWarning() << "casdap output: cannot start" << process->program();
        handler(false, {});
    });
    QTimer::singleShot(processTimeoutMs, process, [process] {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
    process->start(program, args);
}

void OutputControl::drop(QProcess *&slot)
{
    if (!slot)
        return;
    QProcess *process = slot;
    slot = nullptr;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(1000);
    }
    process->deleteLater();
}

void OutputControl::finish(bool ok)
{
    if (m_busy) {
        m_busy = false;
        emit busyChanged();
    }
    qInfo() << "casdap output: applied" << m_output << ok;
    emit applied(m_output, ok);
    refreshJack();
}

void OutputControl::setJackPresent(bool present)
{
    if (m_jackPresent == present)
        return;
    m_jackPresent = present;
    emit jackPresentChanged();
}

}
