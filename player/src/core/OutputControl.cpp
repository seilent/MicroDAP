#include "OutputControl.h"

#include "AudioInfo.h"
#include "Settings.h"

#include <QDebug>
#include <QFile>
#include <QRegularExpression>

namespace casdap {

namespace {

constexpr int pollMs = 2000;
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
    m_pollTimer.setInterval(pollMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &OutputControl::refresh);
}

OutputControl::~OutputControl()
{
    drop(m_apply);
    drop(m_jack);
    drop(m_sinkList);
    drop(m_word);
}

QStringList OutputControl::outputs()
{
    return {QStringLiteral("auto"), QStringLiteral("speaker"), QStringLiteral("headphones"), QStringLiteral("usb")};
}

QString OutputControl::speakerSink()
{
    return QStringLiteral("alsa_output.platform-sound.HiFi__Speaker__sink");
}

QString OutputControl::headphonesSink()
{
    return QStringLiteral("alsa_output.platform-sound.HiFi__Headphones__sink");
}

QString OutputControl::usbPrefix()
{
    return QStringLiteral("alsa_output.usb-");
}

QString OutputControl::wpctlPath()
{
    return binary("CASDAP_WPCTL_BIN", QStringLiteral("wpctl"));
}

QString OutputControl::pactlPath()
{
    return binary("CASDAP_PACTL_BIN", QStringLiteral("pactl"));
}

QString OutputControl::amixerPath()
{
    return binary("CASDAP_AMIXER_BIN", QStringLiteral("amixer"));
}

QString OutputControl::systemctlPath()
{
    return binary("CASDAP_SYSTEMCTL_BIN", QStringLiteral("systemctl"));
}

QStringList OutputControl::parseSinks(const QString &text)
{
    QStringList names;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.size() < 2)
            continue;
        const QString name = fields.at(1).trimmed();
        if (!name.isEmpty())
            names.append(name);
    }
    return names;
}

QString OutputControl::usbSinkIn(const QStringList &sinks)
{
    for (const QString &name : sinks) {
        if (name.startsWith(usbPrefix()))
            return name;
    }
    return {};
}

bool OutputControl::parseJack(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("name='Headphone Jack'[^\\n]*\\n(?:[^\\n]*\\n)?\\s*:\\s*values=(on|off)\\b"));
    const QRegularExpressionMatch match = re.match(text);
    return match.hasMatch() && match.captured(1) == QLatin1StringView("on");
}

QString OutputControl::resolve(const QString &output, bool jack, bool usb)
{
    if (output == QLatin1StringView("speaker"))
        return QStringLiteral("speaker");
    if (output == QLatin1StringView("headphones"))
        return QStringLiteral("headphones");
    if (usb)
        return QStringLiteral("usb");
    return jack ? QStringLiteral("headphones") : QStringLiteral("speaker");
}

QString OutputControl::unitFor(const QString &word)
{
    return QStringLiteral("casdap-priv@") + word + QStringLiteral(".service");
}

QString OutputControl::output() const
{
    return m_output;
}

QString OutputControl::activeOutput() const
{
    return m_activeOutput;
}

bool OutputControl::jackPresent() const
{
    return m_jackPresent;
}

QString OutputControl::usbSink() const
{
    return m_usbSink;
}

bool OutputControl::usbPresent() const
{
    return m_held != HeldState::None || !m_usbSink.isEmpty();
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
        refresh();
        m_pollTimer.start();
    } else {
        m_pollTimer.stop();
        m_reread = false;
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

OutputControl::HeldState OutputControl::held() const
{
    return m_held;
}

void OutputControl::setHeld(HeldState state)
{
    if (m_held == state)
        return;
    const bool usb = usbPresent();
    const bool released = state == HeldState::None;
    m_held = state;
    qInfo() << "casdap output: held"
            << (released ? "none" : state == HeldState::Held ? "held" : "returning");
    if (usb != usbPresent())
        emit usbSinkChanged();
    if (released)
        refresh();
}

void OutputControl::watchCard(int index, const QString &id)
{
    m_watchIndex = index;
    m_watchId = id;
    m_watchGone = false;
    qInfo() << "casdap output: watch card" << index << id;
}

void OutputControl::unwatchCard()
{
    m_watchIndex = -1;
    m_watchId.clear();
    m_watchGone = false;
}

quint64 OutputControl::readsStarted() const
{
    return m_readsStarted;
}

void OutputControl::checkWatchedCard()
{
    if (m_watchIndex < 0 || m_watchGone)
        return;
    QFile file(AudioInfo::asoundDir() + QStringLiteral("/card") + QString::number(m_watchIndex) + QStringLiteral("/id"));
    const QString id = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
    if (id == m_watchId)
        return;
    m_watchGone = true;
    qInfo() << "casdap output: card gone" << m_watchIndex << m_watchId << "reads" << id;
    emit cardGone(m_watchIndex);
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
    drop(m_jack);
    drop(m_sinkList);
    m_reading = false;
    m_reread = false;
    m_applying = true;
    m_everApplied = true;
    updateBusy();
    qInfo() << "casdap output: apply" << output;
    readBoth([this] { resolveAndSet(); });
}

void OutputControl::refresh()
{
    checkWatchedCard();
    if (m_applying || m_reading) {
        m_reread = true;
        return;
    }
    m_reading = true;
    const bool jack = m_jackPresent;
    const QString usb = m_usbSink;
    readBoth([this, jack, usb] {
        m_reading = false;
        if (m_everApplied && (jack != m_jackPresent || usb != m_usbSink)) {
            qInfo() << "casdap output: edge, jack" << m_jackPresent << "usb" << m_usbSink;
            apply(m_output);
            return;
        }
        if (!m_reread)
            return;
        m_reread = false;
        refresh();
    });
}

void OutputControl::readBoth(std::function<void()> done)
{
    readJack([this, done](bool, const QByteArray &) {
        readSinks([done](bool, const QByteArray &) { done(); });
    });
}

void OutputControl::readJack(Handler done)
{
    run(m_jack, amixerPath(), {QStringLiteral("-c0"), QStringLiteral("contents")},
        [this, done](bool ok, const QByteArray &out) {
        if (ok)
            setJackPresent(parseJack(QString::fromUtf8(out)));
        done(ok, out);
    });
}

void OutputControl::readSinks(Handler done)
{
    const quint64 read = ++m_readsStarted;
    run(m_sinkList, pactlPath(), {QStringLiteral("list"), QStringLiteral("sinks"), QStringLiteral("short")},
        [this, done, read](bool ok, const QByteArray &out) {
        if (ok) {
            m_sinks = parseSinks(QString::fromUtf8(out));
            const QString usb = usbSinkIn(m_sinks);
            if (!usb.isEmpty() || m_held == HeldState::None)
                setUsbSink(usb);
            emit sinksRead(usb, read);
        }
        done(ok, out);
    });
}

void OutputControl::resolveAndSet()
{
    const QString active = resolve(m_output, m_jackPresent, usbPresent());
    if (active == QLatin1StringView("usb") && m_held != HeldState::None) {
        qInfo() << "casdap output: usb held, default sink kept";
        setActiveOutput(active);
        finish(true);
        return;
    }
    const QString sink = active == QLatin1StringView("usb") ? m_usbSink
        : active == QLatin1StringView("headphones") ? headphonesSink() : speakerSink();
    if (!m_sinks.contains(sink)) {
        qWarning() << "casdap output: sink not listed" << sink;
        finish(false);
        return;
    }
    run(m_apply, pactlPath(), {QStringLiteral("set-default-sink"), sink},
        [this, active](bool ok, const QByteArray &) {
        if (ok)
            setActiveOutput(active);
        finish(ok);
    });
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
    if (!m_applying)
        applyHeadphone();
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

void OutputControl::applyHeadphone()
{
    if (m_hpGain.isEmpty() || m_hpAmp.isEmpty())
        return;
    if (m_activeOutput != QLatin1StringView("headphones")) {
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
        connect(process, &QProcess::finished, process, &QObject::deleteLater);
        process->kill();
        return;
    }
    process->deleteLater();
}

void OutputControl::finish(bool ok)
{
    m_applying = false;
    updateBusy();
    qInfo() << "casdap output: applied" << m_output << "as" << m_activeOutput << ok;
    emit applied(m_output, ok);
    if (ok)
        applyHeadphone();
    if (!m_reread)
        return;
    m_reread = false;
    refresh();
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
    if (m_jackPresent == present)
        return;
    m_jackPresent = present;
    emit jackPresentChanged();
}

void OutputControl::setUsbSink(const QString &sink)
{
    if (m_usbSink == sink)
        return;
    m_usbSink = sink;
    emit usbSinkChanged();
}

void OutputControl::setActiveOutput(const QString &output)
{
    if (m_activeOutput == output)
        return;
    m_activeOutput = output;
    emit activeOutputChanged();
}

}
