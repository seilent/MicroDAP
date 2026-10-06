#include "OutputControl.h"

#include "Settings.h"

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
    drop(m_status);
    drop(m_word);
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

QString OutputControl::systemctlPath()
{
    return binary("CASDAP_SYSTEMCTL_BIN", QStringLiteral("systemctl"));
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

QString OutputControl::defaultSink(const QString &status)
{
    const int sinks = status.indexOf(QLatin1StringView("Sinks:"));
    if (sinks < 0)
        return {};
    const int sources = status.indexOf(QLatin1StringView("Sources:"), sinks);
    const QString block = status.mid(sinks, sources < 0 ? -1 : sources - sinks);
    static const QRegularExpression re(QStringLiteral("\\*\\s+\\d+\\.\\s+(\\S+)"));
    const QRegularExpressionMatch match = re.match(block);
    return match.hasMatch() ? match.captured(1) : QString();
}

bool OutputControl::parseJack(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("name='Headphone Jack'[^\\n]*\\n(?:[^\\n]*\\n)?\\s*:\\s*values=(on|off)\\b"));
    const QRegularExpressionMatch match = re.match(text);
    return match.hasMatch() && match.captured(1) == QLatin1StringView("on");
}

QString OutputControl::unitFor(const QString &word)
{
    return QStringLiteral("casdap-priv@") + word + QStringLiteral(".service");
}

QString OutputControl::output() const
{
    return m_output;
}

bool OutputControl::jackPresent() const
{
    return m_jackPresent;
}

bool OutputControl::headphonesDefault() const
{
    return m_headphonesDefault;
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

QString OutputControl::hpGain() const
{
    return m_hpGain;
}

QString OutputControl::hpAmp() const
{
    return m_hpAmp;
}

int OutputControl::spkGain() const
{
    return m_spkGain;
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
    m_applying = true;
    updateBusy();
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

void OutputControl::setHeadphone(const QString &gain, const QString &amp)
{
    const QString g = gain.toLower();
    const QString a = amp.toLower();
    if (!Settings::hpGains().contains(g) || !Settings::hpAmps().contains(a)) {
        qWarning() << "casdap output: unknown headphone setting" << gain << amp;
        return;
    }
    if (m_hpGain != g) {
        m_hpGain = g;
        emit hpGainChanged();
    }
    if (m_hpAmp != a) {
        m_hpAmp = a;
        emit hpAmpChanged();
    }
    refreshDefault();
}

void OutputControl::setSpkGain(int step)
{
    if (!Settings::spkGains().contains(step)) {
        qWarning() << "casdap output: unknown speaker gain" << step;
        return;
    }
    if (m_spkGain != step) {
        m_spkGain = step;
        emit spkGainChanged();
    }
    startWords({QStringLiteral("spk-gain-") + QString::number(step)});
}

void OutputControl::refreshDefault()
{
    run(m_status, wpctlPath(), {QStringLiteral("status"), QStringLiteral("--name")},
        [this](bool ok, const QByteArray &out) {
        if (ok)
            setHeadphonesDefault(defaultSink(QString::fromUtf8(out)) == headphonesSink());
        if (!m_applying)
            applyHeadphone();
    });
}

void OutputControl::applyHeadphone()
{
    if (m_hpGain.isEmpty() || m_hpAmp.isEmpty())
        return;
    if (m_output != QLatin1StringView("headphones") && !m_headphonesDefault) {
        qInfo() << "casdap output: headphones not active, hp words skipped";
        return;
    }
    startWords({QStringLiteral("hp-gain-") + m_hpGain, QStringLiteral("hp-amp-") + m_hpAmp});
}

void OutputControl::startWords(const QStringList &words)
{
    m_words.append(words);
    if (!m_word)
        nextWord();
    updateBusy();
}

void OutputControl::nextWord()
{
    if (m_words.isEmpty()) {
        updateBusy();
        return;
    }
    const QString word = m_words.takeFirst();
    qInfo() << "casdap output: start" << unitFor(word);
    run(m_word, systemctlPath(), {QStringLiteral("start"), unitFor(word)},
        [this, word](bool ok, const QByteArray &) {
        qInfo() << "casdap output: applied" << word << ok;
        emit applied(word, ok);
        nextWord();
    });
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
    m_applying = false;
    updateBusy();
    qInfo() << "casdap output: applied" << m_output << ok;
    emit applied(m_output, ok);
    refreshJack();
    refreshDefault();
}

void OutputControl::updateBusy()
{
    const bool busy = m_applying || m_word != nullptr || !m_words.isEmpty();
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void OutputControl::setJackPresent(bool present)
{
    const bool first = !m_jackRead;
    m_jackRead = true;
    if (m_jackPresent == present)
        return;
    m_jackPresent = present;
    emit jackPresentChanged();
    if (present && !first)
        refreshDefault();
}

void OutputControl::setHeadphonesDefault(bool headphones)
{
    if (m_headphonesDefault == headphones)
        return;
    m_headphonesDefault = headphones;
    emit headphonesDefaultChanged();
}

}
