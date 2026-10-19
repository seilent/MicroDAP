#include "OutputControl.h"

#include "AudioInfo.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSocketNotifier>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace microdap {

namespace {

constexpr int pollMs = 2000;
constexpr int processTimeoutMs = 5000;
constexpr int settleMs = 400;
constexpr int jackBatch = 16;

QString binary(const char *env, const QString &fallback)
{
    const QString value = qEnvironmentVariable(env);
    return value.isEmpty() ? fallback : value;
}

}

OutputControl::OutputControl(QObject *parent)
    : QObject(parent)
    , m_output(QStringLiteral("auto"))
    , m_devRoot(binary("MICRODAP_DEV_INPUT", QStringLiteral("/dev/input")))
    , m_sysRoot(binary("MICRODAP_SYS_INPUT", QStringLiteral("/sys/class/input")))
{
    m_pollTimer.setInterval(pollMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &OutputControl::refresh);
    m_settleTimer.setSingleShot(true);
    m_settleTimer.setInterval(settleMs);
    connect(&m_settleTimer, &QTimer::timeout, this, [this] { readAll([this] { finish(!m_defaultSink.isEmpty()); }); });
    openJack();
}

OutputControl::~OutputControl()
{
    if (m_jackFd >= 0)
        ::close(m_jackFd);
    drop(m_jack);
    drop(m_sinkList);
    drop(m_default);
}

QStringList OutputControl::outputs()
{
    return {QStringLiteral("auto"), QStringLiteral("speaker"), QStringLiteral("headphones"), QStringLiteral("usb"),
            QStringLiteral("bt")};
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

QString OutputControl::btPrefix()
{
    return QStringLiteral("bluez_output.");
}

QString OutputControl::pactlPath()
{
    return binary("MICRODAP_PACTL_BIN", QStringLiteral("pactl"));
}

QString OutputControl::amixerPath()
{
    return binary("MICRODAP_AMIXER_BIN", QStringLiteral("amixer"));
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

QString OutputControl::jackDeviceName()
{
    return binary("MICRODAP_JACK_NAME", QStringLiteral("PocketMICRO2 Headset Jack"));
}

QString OutputControl::outputOf(const QString &sink)
{
    if (sink.isEmpty())
        return {};
    if (sink.startsWith(usbPrefix()))
        return QStringLiteral("usb");
    if (sink.startsWith(btPrefix()))
        return QStringLiteral("bt");
    if (sink == headphonesSink())
        return QStringLiteral("headphones");
    return QStringLiteral("speaker");
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

bool OutputControl::jackEvents() const
{
    return m_jackFd >= 0;
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

QString OutputControl::defaultSink() const
{
    return m_defaultSink;
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
    qInfo() << "microdap output: held"
            << (released ? "none" : state == HeldState::Held ? "held" : "returning");
    if (usb != usbPresent())
        emit usbSinkChanged();
    if (m_everApplied)
        resolve();
    if (released)
        refresh();
}

void OutputControl::watchCard(int index, const QString &id)
{
    m_watchIndex = index;
    m_watchId = id;
    m_watchGone = false;
    qInfo() << "microdap output: watch card" << index << id;
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
    qInfo() << "microdap output: card gone" << m_watchIndex << m_watchId << "reads" << id;
    emit cardGone(m_watchIndex);
    if (m_held != HeldState::None)
        emit routeLost(QStringLiteral("usb"));
}

void OutputControl::openJack()
{
    if (m_jackFd >= 0)
        return;
    const QString name = jackDeviceName();
    QString path;
    const QStringList events =
        QDir(m_devRoot).entryList({QStringLiteral("event*")}, QDir::System | QDir::Files, QDir::Name);
    for (const QString &event : events) {
        QFile file(m_sysRoot + QLatin1Char('/') + event + QStringLiteral("/device/name"));
        if (file.open(QIODevice::ReadOnly) && QString::fromUtf8(file.readAll()).trimmed() == name) {
            path = m_devRoot + QLatin1Char('/') + event;
            break;
        }
    }
    const int fd = path.isEmpty() ? -1 : ::open(QFile::encodeName(path).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (!m_jackLogged) {
            if (path.isEmpty())
                qInfo().noquote() << "microdap output:" << name << "not found, jack from amixer";
            else
                qWarning().noquote() << "microdap output: cannot open" << path << std::strerror(errno);
            m_jackLogged = true;
        }
        return;
    }
    m_jackFd = fd;
    m_jackLogged = false;
    m_jackNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_jackNotifier, &QSocketNotifier::activated, this, &OutputControl::drainJack);
    qInfo().noquote() << "microdap output:" << name << "on" << path;
    bool present = false;
    if (readJackSwitch(present))
        setJackPresent(present);
}

void OutputControl::closeJack()
{
    if (m_jackNotifier) {
        m_jackNotifier->setEnabled(false);
        m_jackNotifier->deleteLater();
        m_jackNotifier = nullptr;
    }
    if (m_jackFd >= 0) {
        ::close(m_jackFd);
        m_jackFd = -1;
    }
    qInfo().noquote() << "microdap output:" << jackDeviceName() << "closed";
}

void OutputControl::drainJack()
{
    input_event events[jackBatch];
    while (m_jackFd >= 0) {
        const ssize_t got = ::read(m_jackFd, events, sizeof(events));
        if (got < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            qWarning().noquote() << "microdap output: jack read failed" << std::strerror(errno);
            closeJack();
            return;
        }
        if (got == 0) {
            closeJack();
            return;
        }
        const qsizetype count = qsizetype(got) / qsizetype(sizeof(input_event));
        const bool before = m_jackPresent;
        for (qsizetype i = 0; i < count; ++i) {
            if (events[i].type == EV_SW && events[i].code == SW_HEADPHONE_INSERT)
                setJackPresent(events[i].value != 0);
        }
        if (m_jackPresent != before && m_active)
            refresh();
    }
}

bool OutputControl::readJackSwitch(bool &present) const
{
    if (m_jackFd < 0)
        return false;
    unsigned long bits[(SW_MAX + 1 + 8 * sizeof(unsigned long) - 1) / (8 * sizeof(unsigned long))]{};
    if (::ioctl(m_jackFd, EVIOCGSW(sizeof(bits)), bits) < 0)
        return false;
    const unsigned long word = 8 * sizeof(unsigned long);
    present = (bits[SW_HEADPHONE_INSERT / word] >> (SW_HEADPHONE_INSERT % word)) & 1UL;
    return true;
}

void OutputControl::apply(const QString &output)
{
    if (!outputs().contains(output)) {
        qWarning() << "microdap output: unknown output" << output;
        return;
    }
    if (m_output != output) {
        m_output = output;
        emit outputChanged();
    }
    drop(m_jack);
    drop(m_sinkList);
    drop(m_default);
    m_reading = false;
    m_reread = false;
    m_applying = true;
    m_everApplied = true;
    updateBusy();
    qInfo() << "microdap output: wait for the default sink of" << output;
    m_settleTimer.start();
}

void OutputControl::refresh()
{
    checkWatchedCard();
    openJack();
    if (m_applying || m_reading) {
        m_reread = true;
        return;
    }
    m_reading = true;
    readAll([this] {
        m_reading = false;
        if (m_everApplied)
            resolve();
        if (!m_reread)
            return;
        m_reread = false;
        refresh();
    });
}

void OutputControl::readAll(std::function<void()> done)
{
    readJack([this, done](bool, const QByteArray &) {
        readSinks([this, done](bool, const QByteArray &) {
            readDefault([done](bool, const QByteArray &) { done(); });
        });
    });
}

void OutputControl::readJack(Handler done)
{
    bool present = false;
    if (readJackSwitch(present)) {
        setJackPresent(present);
        done(true, {});
        return;
    }
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
            QStringList sinks = parseSinks(QString::fromUtf8(out));
            const QString lost = outputOf(m_defaultSink);
            if (m_held == HeldState::None && (lost == QLatin1StringView("usb") || lost == QLatin1StringView("bt"))
                && m_activeOutput == lost && m_sinks.contains(m_defaultSink) && !sinks.contains(m_defaultSink)) {
                qInfo() << "microdap output: route lost" << m_defaultSink;
                emit routeLost(lost);
            }
            m_sinks = std::move(sinks);
            const QString usb = usbSinkIn(m_sinks);
            if (!usb.isEmpty() || m_held == HeldState::None)
                setUsbSink(usb);
            emit sinksRead(usb, read);
        }
        done(ok, out);
    });
}

void OutputControl::readDefault(Handler done)
{
    run(m_default, pactlPath(), {QStringLiteral("get-default-sink")}, [this, done](bool ok, const QByteArray &out) {
        if (ok)
            m_defaultSink = QString::fromUtf8(out).trimmed();
        done(ok, out);
    });
}

void OutputControl::resolve()
{
    if (m_held != HeldState::None) {
        setActiveOutput(QStringLiteral("usb"));
        return;
    }
    const QString active = outputOf(m_defaultSink);
    if (!active.isEmpty())
        setActiveOutput(active);
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
            qWarning() << "microdap output:" << process->program() << process->arguments() << "exit" << code;
        handler(ok, process->readAllStandardOutput());
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, &slot, process, handler](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        if (slot == process)
            slot = nullptr;
        process->deleteLater();
        qWarning() << "microdap output: cannot start" << process->program();
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
    resolve();
    updateBusy();
    qInfo() << "microdap output:" << m_output << "plays on" << m_defaultSink << "as" << m_activeOutput << ok;
    emit applied(m_output, ok);
    if (!m_resolvedOnce) {
        m_resolvedOnce = true;
        emit firstApplied(ok);
    }
    if (!m_reread)
        return;
    m_reread = false;
    refresh();
}

void OutputControl::updateBusy()
{
    if (m_busy == m_applying)
        return;
    m_busy = m_applying;
    emit busyChanged();
}

void OutputControl::setJackPresent(bool present)
{
    if (m_jackPresent == present)
        return;
    m_jackPresent = present;
    emit jackPresentChanged();
    if (!present && m_held == HeldState::None && m_activeOutput == QLatin1StringView("headphones")) {
        qInfo() << "microdap output: route lost, jack out";
        emit routeLost(m_activeOutput);
    }
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
