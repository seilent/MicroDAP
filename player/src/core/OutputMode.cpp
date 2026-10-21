#include "OutputMode.h"

#include "AudioInfo.h"
#include "BluezModel.h"
#include "OutputControl.h"
#include "WirePlumberRestart.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QPointer>

namespace microdap {

namespace {

const QString off = QStringLiteral("off");
const QString bitperfect = QStringLiteral("bitperfect");
const QString exclusiveLevel = QStringLiteral("exclusive");
const QString usb = QStringLiteral("usb");

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}

bool exactFormat(const QString &format, bool exclusive)
{
    static const QStringList integers{QStringLiteral("S8"), QStringLiteral("U8"), QStringLiteral("S16"),
                                      QStringLiteral("U16"), QStringLiteral("S24"), QStringLiteral("U24"),
                                      QStringLiteral("F32")};
    for (const QString &prefix : integers) {
        if (format.startsWith(prefix))
            return true;
    }
    return exclusive && (format.startsWith(QLatin1StringView("S32")) || format.startsWith(QLatin1StringView("U32")));
}

}

OutputMode::OutputMode(const Bounds &bounds, const Ports &ports, QObject *parent)
    : QObject(parent)
    , m_bounds(bounds)
    , m_ports(ports)
{
    m_clock.start();
    m_park.setSingleShot(true);
    m_park.setInterval(bounds.parkMs);
    connect(&m_park, &QTimer::timeout, this, [this] {
        if (m_state != State::ExHeld || !m_nameOwned || m_r.running)
            return;
        log(QStringLiteral("park timer fired"));
        startRelease(Reason::Idle, Build::Park, true);
    });
    m_acquireTimer.setSingleShot(true);
    m_acquireTimer.setInterval(bounds.acquireMs);
    connect(&m_acquireTimer, &QTimer::timeout, this, &OutputMode::acquireTimedOut);
    m_pcmPoll.setSingleShot(true);
    m_pcmPoll.setInterval(bounds.pcmPollMs);
    connect(&m_pcmPoll, &QTimer::timeout, this, &OutputMode::pollPcm);
    m_restartWait.setSingleShot(true);
    m_restartWait.setInterval(bounds.restartWaitMs);
    connect(&m_restartWait, &QTimer::timeout, this, [this] {
        if (!m_restartWaiting)
            return;
        m_restartWaiting = false;
        log(QStringLiteral("wireplumber restart still running, bit perfect chain"));
        if (m_waiting) {
            m_waiting = false;
            driveBuild(bitPerfectChain());
        }
        updateAll();
    });
    m_netTimer.setSingleShot(true);
    connect(&m_netTimer, &QTimer::timeout, this, &OutputMode::onNetTimer);
    m_resolveTimer.setSingleShot(true);
    m_resolveTimer.setInterval(bounds.resolveMs);
    connect(&m_resolveTimer, &QTimer::timeout, this, [this] {
        if (m_resolved)
            return;
        log(QStringLiteral("no first output resolve within the bound"));
        m_resolved = true;
        if (m_restoreWanted)
            waitReady();
    });
    m_dacTimer.setSingleShot(true);
    m_dacTimer.setInterval(bounds.dacReleaseMs);
    connect(&m_dacTimer, &QTimer::timeout, this, [this] {
        if (m_switchDone || m_switchDac)
            return;
        log(QStringLiteral("switch bound reached, dac eq not released"));
        m_switchDac = true;
        emitSwitchReady();
    });
    m_readyTimer.setSingleShot(true);
    m_readyTimer.setInterval(bounds.readyMs);
    connect(&m_readyTimer, &QTimer::timeout, this, [this] {
        if (!m_restoreWanted)
            return;
        log(QStringLiteral("bluetooth not ready within the bound"));
        resumeRestore();
    });
    m_rateTimer.setSingleShot(true);
    m_rateTimer.setInterval(bounds.sinkRateMs);
    connect(&m_rateTimer, &QTimer::timeout, this, [this] {
        if (!m_rateWaiting)
            return;
        m_rateWaiting = false;
        m_rateBound = true;
        log(QStringLiteral("no sink rate within the bound"));
        if (m_waiting && !m_r.running && m_acq == Acquire::None)
            decideWaiting();
        updateAll();
    });
    m_rates = rates();
}

OutputMode::~OutputMode() = default;

void OutputMode::attach()
{
    m_lastPlay = m_ports.player->playState();
}

int OutputMode::switchBoundMs() const
{
    return m_bounds.reservation.callMs + m_bounds.switchMs + (m_ports.dacEq ? m_bounds.dacReleaseMs : 0);
}

QList<int> OutputMode::builtInRates()
{
    return {44100, 48000, 88200, 96000, 176400, 192000};
}

OutputMode::State OutputMode::state() const
{
    return m_state;
}

OutputMode::Reason OutputMode::reason() const
{
    return m_reason;
}

QString OutputMode::stateName() const
{
    switch (m_state) {
    case State::Pw:
        return QStringLiteral("pw");
    case State::Bp:
        return QStringLiteral("bp");
    case State::ExIdle:
        return QStringLiteral("ex idle");
    case State::ExAcquiring:
        return QStringLiteral("ex acquiring");
    case State::ExHeld:
        return QStringLiteral("ex held");
    }
    return {};
}

QString OutputMode::reasonName() const
{
    switch (m_reason) {
    case Reason::None:
        return {};
    case Reason::Phone:
        return QStringLiteral("phone");
    case Reason::Volume:
        return QStringLiteral("volume");
    case Reason::NoVolume:
        return QStringLiteral("no volume");
    case Reason::Unavailable:
        return QStringLiteral("unavailable");
    case Reason::Released:
        return QStringLiteral("released");
    case Reason::Idle:
        return QStringLiteral("idle");
    case Reason::Switch:
        return QStringLiteral("switch");
    case Reason::Lost:
        return QStringLiteral("lost");
    }
    return {};
}

QString OutputMode::level() const
{
    return m_level;
}

QString OutputMode::levelStatus() const
{
    return m_status;
}

bool OutputMode::effective() const
{
    return m_level != off && m_active == usb && !m_cardGone;
}

QVariantList OutputMode::rates() const
{
    const QList<int> list = m_active == usb && m_card.alsa >= 0 ? AudioInfo::ratesFor(m_card.alsa) : builtInRates();
    QVariantList out;
    for (const int rate : list)
        out.append(rate);
    return out;
}

bool OutputMode::rateReadOnly() const
{
    return effective();
}

bool OutputMode::releasing() const
{
    return m_r.running;
}

quint64 OutputMode::generation() const
{
    return m_generations.value(m_card.id);
}

QString OutputMode::cardId() const
{
    return m_card.id;
}

int OutputMode::alsaCard() const
{
    return m_card.alsa;
}

bool OutputMode::exHeld() const
{
    return m_state == State::ExHeld;
}

bool OutputMode::switchStoppedStream() const
{
    return m_switchStopped;
}

void OutputMode::setState(State state)
{
    const bool was = m_state == State::ExHeld;
    m_state = state;
    if (was != (state == State::ExHeld))
        emit exHeldChanged(!was);
}

void OutputMode::setCardGone(bool gone)
{
    if (gone == m_cardGone)
        return;
    m_cardGone = gone;
    emit cardGoneChanged(gone);
}

bool OutputMode::exclusive() const
{
    return effective() && m_level == exclusiveLevel;
}

bool OutputMode::retryPending() const
{
    return m_retryAt >= 0 && m_clock.elapsed() < m_retryAt;
}

bool OutputMode::blocked() const
{
    if (m_ports.bluez && (m_ports.bluez->connected() || !m_ports.bluez->ready()))
        return true;
    return m_refused || m_noVolume || m_netSticky || m_deferredNet || retryPending() || m_volumeBlock;
}

OutputMode::Reason OutputMode::blockReason() const
{
    if (m_ports.bluez && m_ports.bluez->connected())
        return Reason::Phone;
    if (m_refused || m_netSticky || m_deferredNet || retryPending())
        return Reason::Unavailable;
    if (m_noVolume)
        return Reason::NoVolume;
    if (m_volumeBlock)
        return Reason::Volume;
    return Reason::None;
}

QString OutputMode::readCardId(int alsa) const
{
    if (alsa < 0)
        return {};
    return readText(QDir(AudioInfo::asoundDir()).filePath(QStringLiteral("card%1/id").arg(alsa)));
}

bool OutputMode::cardPresent() const
{
    return m_card.alsa >= 0 && !m_card.id.isEmpty() && readCardId(m_card.alsa) == m_card.id;
}

QString OutputMode::pcmStatus() const
{
    QFile file(QDir(AudioInfo::asoundDir()).filePath(QStringLiteral("card%1/pcm0p/sub0/status").arg(m_card.alsa)));
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll()).trimmed();
}

Player::OutputChain OutputMode::chainFor(GstState target) const
{
    Q_UNUSED(target);
    if (!effective())
        return Player::OutputChain::Pw;
    if (m_level == bitperfect)
        return bitPerfectChain();
    if (m_state == State::ExHeld || m_state == State::ExAcquiring)
        return Player::OutputChain::Ex;
    return blocked() ? bitPerfectChain() : Player::OutputChain::Ex;
}

Admit OutputMode::admit(const Player::PendingBuild &build)
{
    ++m_admitSeq;
    if (m_r.running || m_acq != Acquire::None || m_restartWaiting) {
        m_waiting = true;
        m_waitTarget = build.target;
        if (build.target == GST_STATE_PLAYING)
            m_park.stop();
        return {false, Player::OutputChain::Ex};
    }
    if (!effective())
        return admitNow(build, Player::OutputChain::Pw);
    if (m_level == bitperfect)
        return admitNow(build, bitPerfectChain());
    if (m_state == State::ExHeld) {
        if (build.target == GST_STATE_PLAYING)
            m_park.stop();
        else if (!m_park.isActive())
            m_park.start();
        return {true, Player::OutputChain::Ex};
    }
    if (blocked()) {
        const Reason why = blockReason();
        if (why != Reason::None)
            m_reason = why;
        return admitNow(build, bitPerfectChain());
    }
    if (build.target != GST_STATE_PLAYING) {
        schedulePark();
        return {false, Player::OutputChain::Ex};
    }
    m_waiting = true;
    m_waitTarget = GST_STATE_PLAYING;
    if (m_ports.wireplumber && m_ports.wireplumber->restarting()) {
        log(QStringLiteral("acquire waits for the wireplumber restart"));
        m_restartWaiting = true;
        m_restartWait.start();
        return {false, Player::OutputChain::Ex};
    }
    startAcquire();
    return {false, Player::OutputChain::Ex};
}

QString OutputMode::exclusiveSink() const
{
    const QStringList formats = AudioInfo::streamFor(m_card.alsa).formats;
    QString format = QStringLiteral("S32LE");
    if (!formats.isEmpty() && !formats.contains(QStringLiteral("S32_LE"))) {
        if (formats.contains(QStringLiteral("S24_3LE")))
            format = QStringLiteral("S24LE");
        else if (formats.contains(QStringLiteral("S16_LE")))
            format = QStringLiteral("S16LE");
    }
    return QStringLiteral("audioconvert dithering=none noise-shaping=none ! capsfilter caps=audio/x-raw,format=%1 "
                          "! alsasink device=hw:CARD=%2,DEV=0")
        .arg(format, m_card.id);
}

QString OutputMode::exclusiveCard() const
{
    return m_card.id;
}

QString OutputMode::bitPerfectSink() const
{
    return m_card.sink.isEmpty() ? m_usbSink : m_card.sink;
}

Player::OutputChain OutputMode::routed(Player::OutputChain chain) const
{
    return chain == Player::OutputChain::Bp && m_ports.player->dsd() ? Player::OutputChain::Pw : chain;
}

Player::OutputChain OutputMode::bitPerfectChain() const
{
    if (!bitPerfectSink().isEmpty()) {
        m_noBitPerfectSink = false;
        return Player::OutputChain::Bp;
    }
    if (!m_noBitPerfectSink) {
        m_noBitPerfectSink = true;
        log(QStringLiteral("no usb sink for bit perfect, normal chain"));
    }
    return Player::OutputChain::Pw;
}

int OutputMode::fixedRate() const
{
    if (effective())
        return 0;
    const int rate = m_rate.toInt();
    return rate > 0 && rates().contains(rate) ? rate : 0;
}

int OutputMode::sinkRate() const
{
    return m_ports.dac ? m_ports.dac->defaultSinkRate() : 0;
}

int OutputMode::pwRate() const
{
    const int fixed = fixedRate();
    return fixed > 0 ? fixed : sinkRate();
}

bool OutputMode::holdForRate(const Player::PendingBuild &build, Player::OutputChain chain)
{
    const bool pw = chain == Player::OutputChain::Pw
                    || (chain == Player::OutputChain::Bp && Player::isDsd(build.entry.path));
    if (sinkRate() > 0)
        m_rateBound = false;
    if (!pw || m_rateBound || fixedRate() > 0 || !m_ports.dac || sinkRate() > 0)
        return false;
    m_waiting = true;
    m_waitTarget = build.target;
    if (!m_rateWaiting) {
        m_rateWaiting = true;
        m_rateTimer.start();
        log(QStringLiteral("build waits for the sink rate"));
    }
    return true;
}

Admit OutputMode::admitNow(const Player::PendingBuild &build, Player::OutputChain chain)
{
    if (holdForRate(build, chain))
        return {false, chain};
    if (chain == Player::OutputChain::Pw) {
        m_builtRate = pwRate();
        m_builtFixed = fixedRate();
    }
    return {true, chain};
}

void OutputMode::onSinkRateKnown(int rate)
{
    m_rateBound = false;
    if (!m_rateWaiting)
        return;
    m_rateWaiting = false;
    m_rateTimer.stop();
    log(QStringLiteral("sink rate %1").arg(rate));
    if (m_waiting && !m_r.running && m_acq == Acquire::None)
        decideWaiting();
    updateAll();
}

void OutputMode::outputError(int code)
{
    QMetaObject::invokeMethod(this, [this, code] { handleOutputError(code); }, Qt::QueuedConnection);
}

bool OutputMode::levelSet() const
{
    return m_level != off;
}

bool OutputMode::restorePending() const
{
    return m_restoreWanted;
}

void OutputMode::restorePlaying(bool playing)
{
    if (!playing || m_level == off)
        return;
    m_restoreWanted = true;
    log(QStringLiteral("restore waits for the first resolve and bluetooth"));
    if (m_resolved) {
        waitReady();
        return;
    }
    if (!m_resolveTimer.isActive())
        m_resolveTimer.start();
}

void OutputMode::setLevel(const QString &level)
{
    if ((level != off && level != bitperfect && level != exclusiveLevel) || level == m_level)
        return;
    const bool was = effective();
    m_level = level;
    m_refused = false;
    m_noVolume = false;
    log(QStringLiteral("level ") + level);
    if (m_acq != Acquire::None)
        abortAcquire(Reason::None, Build::Keep);
    else if (m_nameOwned && !m_r.running)
        startRelease(Reason::None, Build::Keep, true);
    else if (m_r.running)
        m_reconcile = true;
    if (!was && effective())
        enterLevel(true);
    else if (was && !effective())
        leaveLevel();
    else if (was)
        adoptStep();
    if (!m_r.running && m_acq == Acquire::None && !m_waiting)
        reconcile();
    updateAll();
}

void OutputMode::adoptStep()
{
    AlsaVolume *mixer = m_ports.mixer;
    if (m_card.id.isEmpty() || !mixer->isOpen() || mixer->cardId() != m_card.id)
        return;
    const int hw = mixer->hwStep();
    mixer->setOwnedStep(hw);
    m_ports.volume->setOwnedStep(m_card.id, hw);
}

void OutputMode::refreshCard(const QString &sink)
{
    const quint64 seq = ++m_cardSeq;
    QPointer<OutputMode> self(this);
    m_ports.dac->sinkCard(sink, [self, seq, sink](const DacCard &card) {
        if (!self || seq != self->m_cardSeq || !card.isValid())
            return;
        const QString id = self->readCardId(card.alsaCard);
        if (id.isEmpty())
            return;
        const bool was = self->effective();
        if (id != self->m_card.id) {
            self->m_refused = false;
            self->m_noVolume = false;
            self->m_volumeBlock = false;
        }
        self->m_card = Card{card.alsaCard, card.cardIndex, card.reserveName, id, sink};
        self->setCardGone(false);
        emit self->cardChanged(id, card.alsaCard);
        self->log(QStringLiteral("dac card hw:CARD=") + id);
        if (!was && self->effective()) {
            if (!self->m_r.running && self->m_acq == Acquire::None) {
                self->enterLevel(false);
                self->reconcile();
            } else {
                self->m_entryPending = true;
            }
        }
        self->updateAll();
    });
}

void OutputMode::checkStopped()
{
    if (!m_nameOwned || m_r.running || m_asleep || m_ports.player->playState() != PlayerPort::PlayState::Stopped)
        return;
    log(QStringLiteral("stopped with the name held"));
    startRelease(Reason::Idle, Build::None, true);
}

void OutputMode::setSampleRate(const QString &rate)
{
    if (rate == m_rate)
        return;
    const int before = pwRate();
    m_rate = rate;
    if (pwRate() == before || effective() || m_r.running || m_acq != Acquire::None)
        return;
    const PlayerPort::PlayState play = m_ports.player->playState();
    if (play == PlayerPort::PlayState::Stopped)
        return;
    driveRebuild(m_ports.player->positionMs(), play == PlayerPort::PlayState::Playing ? GST_STATE_PLAYING
                                                                                      : GST_STATE_PAUSED);
}

void OutputMode::onActiveOutputChanged(const QString &active, const QString &usbSink)
{
    if (!usbSink.isEmpty())
        m_usbSink = usbSink;
    if (active == m_active)
        return;
    const bool was = effective();
    m_active = active;
    if (m_level != off)
        log(QStringLiteral("active output ") + active);
    if (active == usb) {
        if (m_cardGone && (m_card.id.isEmpty() || cardPresent()))
            setCardGone(false);
        else if (m_cardGone)
            refreshCard(m_usbSink);
    } else {
        m_refused = false;
        m_noVolume = false;
        if (m_acq != Acquire::None) {
            abortAcquire(Reason::None, Build::Keep);
        } else if (m_nameOwned && !m_r.running) {
            startRelease(Reason::None, Build::Keep, false);
        } else if (m_r.running && m_r.phase != Phase::WaitReleased && m_r.phase != Phase::WaitNameLost) {
            log(QStringLiteral("output left usb, return ended"));
            m_netTimer.stop();
            m_r.phase = Phase::None;
            m_ports.output->setHeld(HeldOutputPort::HeldState::None);
            finishRelease();
        }
    }
    if (!was && effective())
        enterLevel(false);
    else if (was && !effective())
        leaveLevel();
    if (!m_r.running && m_acq == Acquire::None && !m_waiting)
        reconcile();
    else
        m_reconcile = true;
    updateAll();
}

void OutputMode::onApplied(const QString &output, bool ok)
{
    Q_UNUSED(ok);
    if (m_resolved || !OutputControl::outputs().contains(output))
        return;
    m_resolved = true;
    m_resolveTimer.stop();
    if (m_restoreWanted)
        waitReady();
}

void OutputMode::onCardGone(int index)
{
    if (index != m_card.alsa)
        return;
    disconnectEvent("card gone on the poll");
}

void OutputMode::onSinksRead(const QString &usbSink, quint64 read)
{
    if (!m_r.running || usbSink.isEmpty())
        return;
    if (m_r.phase == Phase::Sink) {
        sinkFound(usbSink);
        return;
    }
    if (m_r.phase == Phase::Read && read > m_r.readAfter) {
        m_r.read = true;
        maybeEndReturn();
    }
}

void OutputMode::onStatusChanged()
{
    const PlayerPort::PlayState play = m_ports.player->playState();
    const PlayerPort::PlayState previous = std::exchange(m_lastPlay, play);
    if (m_driving > 0 || m_quitting || play == previous) {
        updateAll();
        return;
    }
    if (m_restoreWanted) {
        log(QStringLiteral("restore dropped, the player moved"));
        m_restoreWanted = false;
        m_resolveTimer.stop();
        m_readyTimer.stop();
    }
    if (play == PlayerPort::PlayState::Playing) {
        m_park.stop();
        updateAll();
        return;
    }
    if (m_acq != Acquire::None) {
        abortAcquire(Reason::None, Build::Park);
    } else if (m_r.running) {
        m_waiting = false;
    } else if (m_restartWaiting) {
        m_restartWaiting = false;
        m_restartWait.stop();
        m_waiting = false;
    } else if (m_state == State::ExHeld && m_nameOwned && !m_asleep) {
        if (play == PlayerPort::PlayState::Stopped)
            startRelease(Reason::Idle, Build::None, true);
        else if (!m_ports.player->parked() && !m_park.isActive())
            m_park.start();
    }
    m_waiting = m_waiting && play != PlayerPort::PlayState::Stopped;
    updateAll();
}

void OutputMode::onEndOfStream()
{
    if (m_state != State::ExHeld || !m_nameOwned || m_r.running || m_asleep)
        return;
    log(QStringLiteral("end of queue"));
    startRelease(Reason::Idle, Build::None, true);
}

void OutputMode::onHwRateChanged(int rate)
{
    m_hwRate = rate;
    updateStatus();
}

void OutputMode::onMixerDisconnected()
{
    disconnectEvent("mixer poll error");
}

void OutputMode::onVolumePressed()
{
    if (!effective())
        return;
    m_volumeBlock = false;
    if (m_card.sink.isEmpty() || m_nameOwned) {
        updateAll();
        return;
    }
    const quint64 seq = ++m_volumeSeq;
    QPointer<OutputMode> self(this);
    m_ports.dac->sinkMuted(m_card.sink, [self, seq](bool ok, bool muted) {
        if (!self || seq != self->m_volumeSeq)
            return;
        self->m_volumeBlock = ok && muted;
        self->updateAll();
    });
    updateAll();
}

void OutputMode::onSinkAppeared(quint32 index, const QString &name, int card)
{
    Q_UNUSED(index);
    if (m_cardGone && m_active == usb && name.startsWith(OutputControl::usbPrefix())) {
        m_usbSink = name;
        refreshCard(name);
        return;
    }
    if (card < 0 || card != m_card.alsa)
        return;
    m_card.sink = name;
    m_usbSink = name;
    if (m_r.running) {
        if (m_r.phase == Phase::Sink || m_r.phase == Phase::Restart || m_r.phase == Phase::Second)
            sinkFound(name);
        return;
    }
    if (m_deferredNet) {
        log(QStringLiteral("dac sink back, deferred restart dropped"));
        m_deferredNet = false;
        if (m_reason == Reason::Unavailable)
            m_reason = Reason::None;
    }
    if (effective() && !m_nameOwned && m_acq == Acquire::None)
        cardReturn(name, false);
    updateAll();
}

void OutputMode::onSinkChanged(quint32 index)
{
    Q_UNUSED(index);
    if (effective() && !m_nameOwned && !m_r.running && m_acq == Acquire::None)
        checkVolume();
}

void OutputMode::onCardRemoved(quint32 cardIndex)
{
    if (cardIndex != m_card.cardIndex || m_nameOwned || m_acq != Acquire::None)
        return;
    disconnectEvent("card removed");
}

void OutputMode::onBluezConnectedChanged()
{
    if (!m_ports.bluez)
        return;
    if (!m_ports.bluez->connected()) {
        retryNet();
        updateAll();
        return;
    }
    if (!exclusive()) {
        updateAll();
        return;
    }
    if (m_r.running) {
        m_r.reason = Reason::Phone;
        m_reason = Reason::Phone;
    } else if (m_acq != Acquire::None) {
        log(QStringLiteral("phone connected, acquire aborted"));
        abortAcquire(Reason::Phone, Build::Continue);
    } else if (m_nameOwned && !m_asleep) {
        log(QStringLiteral("phone connected"));
        startRelease(Reason::Phone, Build::Continue, true);
    } else if (m_state == State::ExIdle) {
        m_reason = Reason::Phone;
    }
    updateAll();
}

void OutputMode::onBluezReadyChanged()
{
    if (m_ports.bluez && !m_ports.bluez->ready() && exclusive() && !m_r.running) {
        if (m_acq != Acquire::None) {
            log(QStringLiteral("bluetooth not ready, acquire aborted"));
            abortAcquire(Reason::None, Build::Continue);
        } else if (m_nameOwned && !m_asleep) {
            log(QStringLiteral("bluetooth not ready"));
            startRelease(Reason::None, Build::Continue, true);
        }
    }
    if (m_restoreWanted && m_resolved && m_ports.bluez && m_ports.bluez->ready()) {
        m_readyTimer.stop();
        resumeRestore();
    }
    updateAll();
}

void OutputMode::onTransportStateChanged()
{
    retryNet();
    updateAll();
}

void OutputMode::onRestartFinished(bool ok)
{
    if (m_restartWaiting) {
        m_restartWaiting = false;
        m_restartWait.stop();
        log(QStringLiteral("wireplumber restart finished"));
        if (m_waiting)
            decideWaiting();
    }
    if (m_r.running && m_r.phase == Phase::Restart) {
        log(ok ? QStringLiteral("net restart done, waiting for the dac sink")
               : QStringLiteral("net restart failed, waiting for the dac sink"));
        m_r.phase = Phase::Second;
        m_netTimer.start(m_bounds.netSecondMs);
    }
    updateAll();
}

void OutputMode::onAcquired(quint64 seq)
{
    if (m_acq != Acquire::Reserve || seq != m_reserveSeq) {
        if (m_acq == Acquire::Reserve) {
            log(QStringLiteral("stale acquire answer ignored, a newer acquire is posted"));
            return;
        }
        log(QStringLiteral("name taken after the acquire ended, releasing"));
        m_releases.append(false);
        m_ports.reservation->release();
        return;
    }
    m_nameOwned = true;
    m_acq = Acquire::Pcm;
    m_ports.output->setHeld(HeldOutputPort::HeldState::Held);
    m_ports.output->watchCard(m_card.alsa, m_card.id);
    m_pcmClock.start();
    pollPcm();
}

void OutputMode::onRefused(quint64 seq)
{
    if (m_acq != Acquire::Reserve || seq != m_reserveSeq)
        return;
    log(QStringLiteral("reservation refused"));
    m_refused = true;
    failBeforeReserve(Reason::Unavailable);
}

void OutputMode::onFailed(quint64 seq, const QString &reason)
{
    if (m_acq != Acquire::Reserve || seq != m_reserveSeq)
        return;
    log(QStringLiteral("reservation failed ") + reason);
    m_acquireTimer.stop();
    m_pcmPoll.stop();
    m_acq = Acquire::None;
    ++m_acqSeq;
    if (!cardPresent()) {
        setState(State::ExIdle);
        disconnectEvent("card gone at acquire");
        return;
    }
    m_retryAt = m_clock.elapsed() + m_bounds.retryMs;
    startRelease(Reason::Unavailable, Build::None, true);
}

void OutputMode::onReleaseRequested(int priority)
{
    if (!m_nameOwned || m_r.running)
        return;
    log(QStringLiteral("release requested at priority %1").arg(priority));
    m_park.stop();
    if (m_acq != Acquire::None) {
        m_acquireTimer.stop();
        m_pcmPoll.stop();
        m_acq = Acquire::None;
        ++m_acqSeq;
        m_waiting = false;
    }
    m_r = Release{};
    m_r.running = true;
    m_r.reason = Reason::Released;
    setState(State::ExIdle);
    m_reason = Reason::Released;
    const PlayerPort::PlayState play = m_ports.player->playState();
    if (play != PlayerPort::PlayState::Stopped && !m_ports.player->parked())
        drivePark(m_ports.player->positionMs());
    m_r.phase = Phase::WaitNameLost;
    m_ports.reservation->grantRelease();
    updateAll();
}

void OutputMode::onReleaseTimedOut()
{
    if (!m_r.running || m_r.phase != Phase::WaitNameLost)
        return;
    log(QStringLiteral("no NameLost after the grant"));
    m_nameOwned = false;
    startReturn();
}

void OutputMode::onNameLost(bool requested)
{
    if (m_r.running && m_r.phase == Phase::WaitNameLost) {
        m_nameOwned = false;
        m_sleepHeld = false;
        if (!requested) {
            startReturn();
            return;
        }
        log(QStringLiteral("name taken by the requester"));
        m_r.phase = Phase::None;
        setState(State::ExIdle);
        m_reason = Reason::Released;
        m_ports.output->setHeld(HeldOutputPort::HeldState::None);
        m_ports.output->unwatchCard();
        finishRelease();
        return;
    }
    if (!m_nameOwned)
        return;
    m_nameOwned = false;
    m_sleepHeld = false;
    log(requested ? QStringLiteral("name lost") : QStringLiteral("name lost unrequested"));
    if (m_acq != Acquire::None) {
        m_acquireTimer.stop();
        m_pcmPoll.stop();
        m_acq = Acquire::None;
        ++m_acqSeq;
    }
    if (m_r.running)
        return;
    startRelease(Reason::Lost, m_waiting ? Build::None : Build::Park, true);
}

void OutputMode::onConnectionLost()
{
    if (m_r.running && m_r.phase == Phase::WaitReleased) {
        log(QStringLiteral("reservation connection dropped during the release"));
        m_releases.clear();
        startReturn();
        return;
    }
    if (!m_nameOwned)
        return;
    log(QStringLiteral("reservation connection dropped"));
    onNameLost(false);
}

void OutputMode::onReleased()
{
    if (m_releases.isEmpty()) {
        if (m_r.running && m_r.phase == Phase::WaitNameLost) {
            log(QStringLiteral("released after the false reply"));
            m_nameOwned = false;
            startReturn();
        }
        return;
    }
    const bool mine = m_releases.takeFirst();
    if (mine && m_r.running && m_r.phase == Phase::WaitReleased)
        startReturn();
}

void OutputMode::onFormatChanged()
{
    updateStatus();
}

void OutputMode::releaseForSwitch()
{
    const quint64 seq = ++m_switchSeq;
    m_switchR = false;
    m_switchDac = false;
    m_switchDone = false;
    m_switchStopped = false;
    if (m_ports.dacEq) {
        m_dacTimer.start();
        QPointer<OutputMode> self(this);
        m_ports.dacEq->startRelease([self, seq] {
            if (!self || seq != self->m_switchSeq || self->m_switchDone || self->m_switchDac)
                return;
            self->m_dacTimer.stop();
            self->m_switchDac = true;
            self->emitSwitchReady();
        });
    } else {
        m_switchDac = true;
    }
    m_park.stop();
    if (m_restartWaiting) {
        log(QStringLiteral("switch drops the build waiting for the wireplumber restart"));
        m_restartWaiting = false;
        m_restartWait.stop();
        m_waiting = false;
        drivePark(m_ports.player->positionMs());
        updateAll();
        m_switchR = true;
        emitSwitchReady();
        return;
    }
    if (m_r.running) {
        log(QStringLiteral("switch joins the release"));
        m_switchStopped = true;
        m_r.switching = true;
        m_r.net = false;
        m_reason = Reason::Switch;
        if (m_r.phase == Phase::Sink || m_r.phase == Phase::Read) {
            m_netTimer.start(qMin(m_netTimer.remainingTime() < 0 ? m_bounds.switchMs : m_netTimer.remainingTime(),
                                  m_bounds.switchMs));
        } else if (m_r.phase == Phase::Restart || m_r.phase == Phase::Second) {
            m_switchStopped = false;
            endReturn();
        }
        return;
    }
    if (m_acq != Acquire::None && !m_nameOwned) {
        dropAcquire();
        if (m_waiting) {
            m_waiting = false;
            drivePark(m_ports.player->positionMs());
        }
        updateAll();
    }
    if (m_nameOwned) {
        if (m_acq != Acquire::None) {
            m_acquireTimer.stop();
            m_pcmPoll.stop();
            m_acq = Acquire::None;
            ++m_acqSeq;
            m_waiting = false;
        }
        m_switchStopped = true;
        startRelease(Reason::Switch, Build::Park, false, true);
        return;
    }
    m_switchR = true;
    emitSwitchReady();
}

void OutputMode::emitSwitchReady()
{
    if (m_switchDone || !m_switchR || !m_switchDac)
        return;
    m_switchDone = true;
    m_dacTimer.stop();
    emit switchReady();
}

void OutputMode::prepareForSleep(bool sleeping)
{
    if (sleeping) {
        m_park.stop();
        m_asleep = true;
        m_sleepGeneration = generation();
        if (m_restartWaiting) {
            m_restartWaiting = false;
            m_restartWait.stop();
        }
        if (m_acq != Acquire::None && !m_nameOwned)
            dropAcquire();
        if (m_r.running) {
            m_netTimer.stop();
            if (m_r.phase == Phase::WaitReleased) {
                log(QStringLiteral("sleep waits for the posted release"));
                if (!m_ports.reservation->waitReleased(m_bounds.sleepReleaseMs))
                    log(QStringLiteral("release not done within the sleep bound"));
                m_releases.clear();
            }
            if (m_r.phase != Phase::WaitNameLost) {
                m_r.phase = Phase::None;
                m_sleepReturn = true;
            }
        }
        if (m_nameOwned && !m_r.running) {
            if (m_acq != Acquire::None) {
                m_acquireTimer.stop();
                m_pcmPoll.stop();
                m_acq = Acquire::None;
                ++m_acqSeq;
            }
            setState(State::ExHeld);
            m_sleepHeld = true;
            if (m_ports.player->playState() != PlayerPort::PlayState::Stopped && !m_ports.player->parked())
                drivePark(m_ports.player->positionMs());
            log(QStringLiteral("sleep with the name kept"));
        }
        if (m_waiting) {
            m_waiting = false;
            drivePark(m_ports.player->positionMs());
        }
        m_ports.mixer->close();
        updateAll();
        return;
    }
    if (!m_asleep)
        return;
    m_asleep = false;
    const bool present = cardPresent();
    const bool newer = generation() != m_sleepGeneration;
    if (m_level != off || m_sleepHeld || m_sleepReturn)
        log(QStringLiteral("resume, card %1, generation %2").arg(present ? "present" : "gone", newer ? "newer" : "same"));
    if (m_sleepHeld) {
        m_sleepHeld = false;
        m_r = Release{};
        m_r.running = true;
        m_r.reason = Reason::Idle;
        m_r.net = present;
        setState(State::ExIdle);
        m_reason = Reason::Idle;
        releaseName();
        updateAll();
        return;
    }
    if (m_sleepReturn) {
        m_sleepReturn = false;
        if (m_r.running) {
            startReturn();
            updateAll();
            return;
        }
    }
    if (effective() && present && !m_nameOwned) {
        if (m_ports.mixer->open(m_card.id) && newer)
            m_ports.mixer->watchReturn(m_bounds.pcmWatchMs);
    }
    updateAll();
}

void OutputMode::aboutToQuit()
{
    if (m_quitting)
        return;
    m_quitting = true;
    m_park.stop();
    m_acquireTimer.stop();
    m_pcmPoll.stop();
    m_restartWait.stop();
    m_netTimer.stop();
    m_resolveTimer.stop();
    m_readyTimer.stop();
    m_dacTimer.stop();
    m_rateTimer.stop();
    if (m_nameOwned) {
        log(QStringLiteral("quit with the name held"));
        if (m_ports.player->playState() != PlayerPort::PlayState::Stopped && !m_ports.player->parked())
            drivePark(m_ports.player->positionMs());
        m_nameOwned = false;
    }
    m_ports.reservation->quitAndJoin(m_bounds.quitMs);
    if (m_ports.dacEq)
        m_ports.dacEq->quitAndJoin(m_bounds.dacQuitMs);
}

void OutputMode::bump()
{
    const quint64 next = m_generations.value(m_card.id) + 1;
    m_generations.insert(m_card.id, next);
    m_netSticky = false;
    m_deferredNet = false;
    m_netFirings = 0;
    if (m_reason == Reason::Unavailable && !retryPending() && !m_refused)
        m_reason = Reason::None;
}

void OutputMode::disconnectEvent(const char *source)
{
    if (m_card.id.isEmpty() || m_quitting)
        return;
    bump();
    const bool present = cardPresent();
    log(QStringLiteral("disconnect event, ") + QLatin1StringView(source)
        + QStringLiteral(", generation %1, card %2").arg(generation()).arg(present ? "present" : "gone"));
    if (!present)
        setCardGone(true);
    emit generationChanged(m_card.id, generation(), present);
    if (m_nameOwned) {
        m_ports.reservation->markGone();
        if (m_asleep) {
            updateAll();
            return;
        }
        if (m_acq != Acquire::None) {
            m_acquireTimer.stop();
            m_pcmPoll.stop();
            m_acq = Acquire::None;
            ++m_acqSeq;
        }
        m_ports.mixer->close();
        startRelease(Reason::None, m_waiting ? Build::None : Build::Keep, present);
        return;
    }
    if (m_acq != Acquire::None) {
        abortAcquire(Reason::None, Build::Keep);
    } else if (m_r.running && !present
               && (m_r.phase == Phase::Sink || m_r.phase == Phase::Read || m_r.phase == Phase::Restart
                   || m_r.phase == Phase::Second)) {
        endReturnGone();
    } else if (!present) {
        m_ports.mixer->close();
        if (m_waiting && !m_r.running)
            decideWaiting();
        else if (!m_r.running)
            reconcile();
    }
    updateAll();
}

void OutputMode::handleOutputError(int code)
{
    if (!m_nameOwned || m_r.running || m_quitting)
        return;
    const bool present = cardPresent();
    log(QStringLiteral("output error %1, card %2").arg(code).arg(present ? "present" : "gone"));
    if (!present) {
        disconnectEvent("output error");
        return;
    }
    if (code == GST_RESOURCE_ERROR_WRITE) {
        m_ports.mixer->close();
        startRelease(Reason::None, Build::Keep, true);
        return;
    }
    m_retryAt = m_clock.elapsed() + m_bounds.retryMs;
    startRelease(Reason::Unavailable, Build::Keep, true);
}

void OutputMode::enterLevel(bool adopt)
{
    const quint64 seq = ++m_entrySeq;
    m_volumeBlock = false;
    log(adopt ? QStringLiteral("level entry, adopt") : QStringLiteral("level entry"));
    QPointer<OutputMode> self(this);
    m_ports.dac->sinkCard(m_usbSink, [self, seq, adopt](const DacCard &card) {
        if (!self || seq != self->m_entrySeq || !self->effective())
            return;
        if (!card.isValid()) {
            self->log(QStringLiteral("no dac card for ") + self->m_usbSink);
            return;
        }
        const QString id = self->readCardId(card.alsaCard);
        if (id.isEmpty()) {
            self->log(QStringLiteral("card %1 has no id").arg(card.alsaCard));
            return;
        }
        if (id != self->m_card.id) {
            self->m_refused = false;
            self->m_noVolume = false;
        }
        self->m_card = Card{card.alsaCard, card.cardIndex, card.reserveName, id, self->m_usbSink};
        emit self->cardChanged(id, card.alsaCard);
        AlsaVolume *mixer = self->m_ports.mixer;
        const int owned = self->m_ports.volume->ownedStep(id);
        if (mixer->open(id)) {
            if (adopt || owned < 0) {
                const int hw = mixer->hwStep();
                mixer->setOwnedStep(hw);
                self->m_ports.volume->setOwnedStep(id, hw);
            } else {
                mixer->setOwnedStep(owned);
                self->m_ports.volume->setOwnedStep(id, owned);
                mixer->watchReturn(self->m_bounds.pcmWatchMs);
            }
        }
        self->updateAll();
        self->m_ports.dac->unmuteSink(self->m_card.sink);
        self->checkVolume();
    });
}

void OutputMode::leaveLevel()
{
    log(QStringLiteral("level left"));
    ++m_entrySeq;
    m_storePending = false;
    m_volumeBlock = false;
    m_reason = Reason::None;
    if (!m_r.running)
        m_ports.mixer->close();
    if (!m_nameOwned && !m_r.running)
        m_ports.output->unwatchCard();
}

void OutputMode::cardReturn(const QString &sink, bool release)
{
    if (m_card.id.isEmpty())
        return;
    setCardGone(false);
    AlsaVolume *mixer = m_ports.mixer;
    if (mixer->open(m_card.id)) {
        const int owned = m_ports.volume->ownedStep(m_card.id);
        if (owned >= 0) {
            mixer->setOwnedStep(owned);
            m_ports.volume->setOwnedStep(m_card.id, owned);
        }
        mixer->watchReturn(m_bounds.pcmWatchMs);
        if (!effective()) {
            QPointer<OutputMode> self(this);
            QTimer::singleShot(m_bounds.pcmWatchMs, this, [self] {
                if (self && !self->effective())
                    self->m_ports.mixer->close();
            });
        }
    }
    updateAll();
    if (release || m_ports.player->playState() != PlayerPort::PlayState::Playing) {
        m_storePending = false;
        m_ports.volume->storeSync(m_card.id);
    } else {
        log(QStringLiteral("store sync waits for a pause"));
        m_storePending = true;
    }
    m_ports.dac->unmuteSink(sink);
}

void OutputMode::checkVolume()
{
    if (!effective() || m_card.sink.isEmpty() || m_nameOwned)
        return;
    const QString sink = m_card.sink;
    const double tolerance = m_bounds.volumeToleranceDb;
    const quint64 seq = ++m_volumeSeq;
    QPointer<OutputMode> self(this);
    m_ports.dac->sinkMuted(sink, [self, sink, tolerance, seq](bool ok, bool muted) {
        if (!self || seq != self->m_volumeSeq)
            return;
        self->m_ports.dac->sinkVolumeDb(sink, [self, ok, muted, tolerance, seq](bool okDb, double db) {
            if (!self || seq != self->m_volumeSeq)
                return;
            AlsaVolume *mixer = self->m_ports.mixer;
            const bool part = okDb && mixer->isOpen() && db < mixer->hwDb() - tolerance;
            self->m_volumeBlock = (ok && muted) || part;
            self->updateAll();
        });
    });
}

void OutputMode::reconcile()
{
    settle();
    const PlayerPort::PlayState play = m_ports.player->playState();
    if (play == PlayerPort::PlayState::Stopped)
        return;
    const qint64 position = m_ports.player->positionMs();
    const bool playing = play == PlayerPort::PlayState::Playing;
    const Player::OutputChain built = m_ports.player->builtChain();
    const bool parked = m_ports.player->parked();
    if (exclusive()) {
        if (m_state == State::ExHeld)
            return;
        if (playing) {
            if (built != routed(chainFor(GST_STATE_PLAYING)))
                driveRebuild(position, GST_STATE_PLAYING);
        } else if (blocked()) {
            if (parked || built != routed(bitPerfectChain()))
                driveRebuild(position, GST_STATE_PAUSED);
        } else if (!parked) {
            drivePark(position);
        }
        return;
    }
    const Player::OutputChain chain = routed(chainFor(GST_STATE_PLAYING));
    if (parked || built != chain
        || (chain == Player::OutputChain::Pw && fixedRate() != m_builtFixed && pwRate() != m_builtRate))
        driveRebuild(position, playing ? GST_STATE_PLAYING : GST_STATE_PAUSED);
}

void OutputMode::settle()
{
    if (m_acq != Acquire::None || m_nameOwned)
        return;
    if (!effective()) {
        setState(State::Pw);
        m_reason = Reason::None;
    } else if (m_level == bitperfect) {
        setState(State::Bp);
        m_reason = Reason::None;
    } else if (m_state != State::ExIdle) {
        setState(State::ExIdle);
    }
}

void OutputMode::updateAll()
{
    settle();
    if (m_entryPending && !m_r.running && m_acq == Acquire::None) {
        m_entryPending = false;
        if (effective())
            enterLevel(false);
    }
    const bool on = effective();
    AlsaVolume *mixer = m_ports.mixer;
    const bool alsa = on && !m_card.id.isEmpty() && mixer->cardId() == m_card.id && mixer->rangeDb() > 0.0;
    const QString card = alsa ? m_card.id : QString();
    const QString sink = alsa ? m_card.sink : QString();
    if (alsa != m_routeAlsa || card != m_routeCard || sink != m_routeSink) {
        m_routeAlsa = alsa;
        m_routeCard = card;
        m_routeSink = sink;
        m_ports.volume->setRoute(alsa ? DacVolumePort::Route::Alsa : DacVolumePort::Route::Pulse, card, sink);
    }
    const bool store = on && m_ports.player->playState() != PlayerPort::PlayState::Playing && !m_nameOwned
                       && m_state != State::ExHeld;
    if (int(store) != m_storeAllowed) {
        m_storeAllowed = int(store);
        m_ports.volume->setStoreAllowed(store);
    }
    if (store && m_storePending && alsa) {
        m_storePending = false;
        m_ports.volume->storeSync(m_card.id);
    }
    if (on != m_effective) {
        m_effective = on;
        emit effectiveChanged();
    }
    const QVariantList list = rates();
    if (list != m_rates) {
        m_rates = list;
        emit ratesChanged();
    }
    updateStatus();
}

void OutputMode::updateStatus()
{
    const QString next = computeStatus();
    if (next == m_status)
        return;
    m_status = next;
    emit levelStatusChanged();
}

QString OutputMode::computeStatus() const
{
    if (m_level == off)
        return off;
    const QString word = m_level == bitperfect ? QStringLiteral("on") : exclusiveLevel;
    const auto suffixed = [&word](const QString &suffix) { return word + QStringLiteral(" (") + suffix + QLatin1Char(')'); };
    if (!effective())
        return QStringLiteral("usb only");
    if (m_ports.player->playState() != PlayerPort::PlayState::Playing)
        return word;
    const bool ex = m_level == exclusiveLevel;
    const bool held = m_state == State::ExHeld;
    Player::OutputChain wanted = routed(bitPerfectChain());
    if (ex && (held || m_state == State::ExAcquiring))
        wanted = Player::OutputChain::Ex;
    if (m_ports.player->builtChain() != wanted || m_r.running)
        return suffixed(QStringLiteral("pending"));
    if (m_ports.player->dsd())
        return suffixed(QStringLiteral("converted"));
    if (!held && m_volumeBlock)
        return suffixed(QStringLiteral("volume"));
    const QVariantMap source = m_ports.player->sourceFormat();
    const QVariantMap sink = m_ports.player->sinkFormat();
    const int sourceRate = source.value(QStringLiteral("rate")).toInt();
    if (m_hwRate > 0 && sourceRate > 0 && m_hwRate != sourceRate)
        return suffixed(QStringLiteral("resampled"));
    const QString format = source.value(QStringLiteral("format")).toString();
    const int sourceChannels = source.value(QStringLiteral("channels")).toInt();
    const int sinkChannels = sink.value(QStringLiteral("channels")).toInt();
    if ((!format.isEmpty() && !exactFormat(format, ex))
        || (sourceChannels > 0 && sinkChannels > 0 && sourceChannels != sinkChannels))
        return suffixed(QStringLiteral("converted"));
    if (!ex && m_ports.bluez && m_ports.bluez->transportActive())
        return suffixed(QStringLiteral("mixed"));
    if (ex && !held) {
        switch (m_reason) {
        case Reason::Phone:
        case Reason::Released:
        case Reason::Unavailable:
        case Reason::NoVolume:
        case Reason::Volume:
            return suffixed(reasonName());
        default:
            break;
        }
    }
    return word;
}

void OutputMode::schedulePark()
{
    const quint64 seq = m_admitSeq;
    QMetaObject::invokeMethod(this, [this, seq] {
        if (seq != m_admitSeq || m_r.running || m_acq != Acquire::None)
            return;
        PlayerPort *player = m_ports.player;
        if (player->playState() == PlayerPort::PlayState::Paused && !player->parked()
            && player->builtChain() == Player::OutputChain::None) {
            log(QStringLiteral("paused build parks"));
            drivePark(player->positionMs());
            updateAll();
        }
    }, Qt::QueuedConnection);
}

void OutputMode::startAcquire()
{
    setState(State::ExAcquiring);
    m_acq = Acquire::Card;
    m_nameOwned = false;
    const quint64 seq = ++m_acqSeq;
    m_acquireTimer.start();
    log(QStringLiteral("acquire"));
    QPointer<OutputMode> self(this);
    m_ports.dac->sinkCard(m_usbSink, [self, seq](const DacCard &card) {
        if (!self || seq != self->m_acqSeq || self->m_acq != Acquire::Card)
            return;
        self->acquireCard(card);
    });
    updateStatus();
}

void OutputMode::acquireCard(const DacCard &card)
{
    const QString id = card.isValid() ? readCardId(card.alsaCard) : QString();
    if (id.isEmpty()) {
        log(QStringLiteral("acquire found no card"));
        m_acquireTimer.stop();
        m_acq = Acquire::None;
        ++m_acqSeq;
        setState(State::ExIdle);
        if (!m_card.id.isEmpty() && !cardPresent()) {
            disconnectEvent("card gone at acquire");
            return;
        }
        m_retryAt = m_clock.elapsed() + m_bounds.retryMs;
        m_reason = Reason::Unavailable;
        if (m_waiting) {
            m_waiting = false;
            driveBuild(bitPerfectChain());
        }
        updateAll();
        return;
    }
    if (id != m_card.id) {
        m_refused = false;
        m_noVolume = false;
    }
    m_card = Card{card.alsaCard, card.cardIndex, card.reserveName, id, m_usbSink};
    emit cardChanged(id, card.alsaCard);
    AlsaVolume *mixer = m_ports.mixer;
    if (!mixer->open(id) || mixer->rangeDb() < m_bounds.minRangeDb) {
        log(QStringLiteral("hw:CARD=%1 has no %2 dB volume, exclusive refused").arg(id).arg(m_bounds.minRangeDb));
        m_noVolume = true;
        failBeforeReserve(Reason::NoVolume);
        return;
    }
    m_acq = Acquire::Check;
    const quint64 seq = m_acqSeq;
    const QString sink = m_card.sink;
    const double tolerance = m_bounds.volumeToleranceDb;
    QPointer<OutputMode> self(this);
    m_ports.dac->sinkMuted(sink, [self, seq, sink, tolerance](bool ok, bool muted) {
        if (!self || seq != self->m_acqSeq || self->m_acq != Acquire::Check)
            return;
        self->m_ports.dac->sinkVolumeDb(sink, [self, seq, ok, muted, tolerance](bool okDb, double db) {
            if (!self || seq != self->m_acqSeq || self->m_acq != Acquire::Check)
                return;
            AlsaVolume *mixer = self->m_ports.mixer;
            self->acquireChecked((ok && muted) || (okDb && db < mixer->hwDb() - tolerance));
        });
    });
}

void OutputMode::acquireChecked(bool volumeBlocked)
{
    m_volumeBlock = volumeBlocked;
    if (volumeBlocked) {
        log(QStringLiteral("sink muted or with a software part, exclusive waits for a press"));
        failBeforeReserve(Reason::Volume);
        return;
    }
    m_acq = Acquire::Reserve;
    m_reserveSeq = m_acqSeq;
    m_ports.reservation->acquire(m_card.reserveName, m_card.id, m_reserveSeq);
}

void OutputMode::pollPcm()
{
    if (m_acq != Acquire::Pcm)
        return;
    const QString status = pcmStatus();
    if (status.isNull() && !cardPresent()) {
        disconnectEvent("card gone at acquire");
        return;
    }
    if (status == QLatin1StringView("closed")) {
        acquireDone();
        return;
    }
    if (m_pcmClock.elapsed() >= m_bounds.pcmFreeMs) {
        log(QStringLiteral("pcm not free within the bound"));
        acquireTimedOut();
        return;
    }
    m_pcmPoll.start();
}

void OutputMode::acquireDone()
{
    m_acquireTimer.stop();
    m_pcmPoll.stop();
    m_acq = Acquire::None;
    setState(State::ExHeld);
    m_reason = Reason::None;
    AlsaVolume *mixer = m_ports.mixer;
    const int owned = m_ports.volume->ownedStep(m_card.id);
    if (owned >= 0 && mixer->isOpen() && owned != mixer->hwStep())
        mixer->setStep(owned);
    log(QStringLiteral("held hw:CARD=") + m_card.id);
    m_waiting = false;
    updateAll();
    driveBuild(Player::OutputChain::Ex);
    if (m_ports.player->playState() == PlayerPort::PlayState::Paused && !m_park.isActive())
        m_park.start();
    updateAll();
}

void OutputMode::failBeforeReserve(Reason reason)
{
    m_acquireTimer.stop();
    m_pcmPoll.stop();
    m_acq = Acquire::None;
    ++m_acqSeq;
    setState(State::ExIdle);
    m_reason = reason;
    if (m_waiting) {
        m_waiting = false;
        driveBuild(bitPerfectChain());
    }
    updateAll();
}

void OutputMode::acquireTimedOut()
{
    if (m_acq == Acquire::None)
        return;
    log(QStringLiteral("acquire timed out"));
    m_acquireTimer.stop();
    m_pcmPoll.stop();
    m_acq = Acquire::None;
    ++m_acqSeq;
    if (!cardPresent()) {
        setState(State::ExIdle);
        disconnectEvent("card gone at acquire");
        return;
    }
    m_retryAt = m_clock.elapsed() + m_bounds.retryMs;
    startRelease(Reason::Unavailable, Build::None, true);
}

void OutputMode::abortAcquire(Reason reason, Build build)
{
    m_acquireTimer.stop();
    m_pcmPoll.stop();
    m_acq = Acquire::None;
    ++m_acqSeq;
    if (build == Build::Park)
        m_waiting = false;
    if (m_nameOwned) {
        startRelease(reason, build == Build::Park ? Build::Park : Build::None, true);
        return;
    }
    log(QStringLiteral("acquire dropped"));
    setState(State::ExIdle);
    m_reason = reason;
    settle();
    if (m_waiting)
        decideWaiting();
}

void OutputMode::dropAcquire()
{
    log(QStringLiteral("acquire dropped"));
    m_acquireTimer.stop();
    m_pcmPoll.stop();
    m_acq = Acquire::None;
    ++m_acqSeq;
    setState(State::ExIdle);
}

void OutputMode::startRelease(Reason reason, Build build, bool net, bool switching)
{
    m_park.stop();
    m_r = Release{};
    m_r.running = true;
    m_r.reason = reason;
    m_r.net = net;
    m_r.switching = switching;
    setState(State::ExIdle);
    m_reason = reason;
    log(QStringLiteral("release ") + (reasonName().isEmpty() ? QStringLiteral("output") : reasonName()));
    PlayerPort *player = m_ports.player;
    const PlayerPort::PlayState play = player->playState();
    if (build != Build::None && play != PlayerPort::PlayState::Stopped) {
        const qint64 position = player->positionMs();
        const bool playing = play == PlayerPort::PlayState::Playing;
        if (build == Build::Continue && playing)
            driveRebuild(position, GST_STATE_PLAYING);
        else if (build == Build::Keep)
            driveRebuild(position, playing ? GST_STATE_PLAYING : GST_STATE_PAUSED);
        else
            drivePark(position);
    }
    releaseName();
    updateAll();
}

void OutputMode::releaseName()
{
    if (m_nameOwned) {
        m_nameOwned = false;
        m_r.phase = Phase::WaitReleased;
        m_releases.append(true);
        m_ports.reservation->release();
        return;
    }
    startReturn();
}

void OutputMode::startReturn()
{
    setState(State::ExIdle);
    m_reason = m_r.reason;
    if (m_quitting)
        return;
    if (m_asleep) {
        m_r.phase = Phase::None;
        m_sleepReturn = true;
        return;
    }
    if (!cardPresent()) {
        endReturnGone();
        return;
    }
    if (m_active != usb) {
        m_r.phase = Phase::None;
        m_ports.output->setHeld(HeldOutputPort::HeldState::None);
        finishRelease();
        return;
    }
    m_ports.output->setHeld(HeldOutputPort::HeldState::Returning);
    m_r.phase = Phase::Sink;
    m_r.moved = false;
    m_r.read = false;
    m_netTimer.start(m_r.switching ? m_bounds.switchMs : m_bounds.netFirstMs);
    m_ports.output->refresh();
    updateAll();
}

void OutputMode::sinkFound(const QString &name)
{
    m_r.phase = Phase::Read;
    m_r.moved = false;
    m_r.read = false;
    m_card.sink = name;
    if (m_netTimer.isActive() && !m_r.switching)
        m_netTimer.start(m_bounds.netFirstMs);
    m_usbSink = name;
    log(QStringLiteral("dac sink back ") + name);
    QPointer<OutputMode> self(this);
    const QString id = m_card.id;
    m_ports.dac->sinkCard(name, [self, id](const DacCard &card) {
        if (self && card.isValid() && card.alsaCard == self->m_card.alsa && id == self->m_card.id)
            self->m_card.cardIndex = card.cardIndex;
    });
    m_ports.dac->bluezInputs([self, name](const QList<quint32> &inputs) {
        if (!self || !self->m_r.running || self->m_r.phase != Phase::Read)
            return;
        for (const quint32 input : inputs)
            self->m_ports.dac->moveSinkInput(input, name);
        self->m_r.moved = true;
        self->maybeEndReturn();
    });
    cardReturn(name, true);
    m_r.readAfter = m_ports.output->readsStarted();
    m_ports.output->refresh();
}

void OutputMode::maybeEndReturn()
{
    if (m_r.running && m_r.phase == Phase::Read && m_r.read && m_r.moved)
        endReturn();
}

void OutputMode::endReturn()
{
    m_netTimer.stop();
    m_r.phase = Phase::None;
    m_ports.output->setHeld(HeldOutputPort::HeldState::None);
    if (!m_netSticky && !m_deferredNet)
        m_ports.output->unwatchCard();
    finishRelease();
}

void OutputMode::endReturnGone()
{
    log(QStringLiteral("card gone, return ended"));
    m_netTimer.stop();
    m_r.phase = Phase::None;
    setCardGone(true);
    m_ports.mixer->close();
    m_ports.output->setHeld(HeldOutputPort::HeldState::None);
    m_ports.output->unwatchCard();
    finishRelease();
}

void OutputMode::failReturn(bool deferred)
{
    m_netTimer.stop();
    m_r.phase = Phase::None;
    m_r.failed = true;
    m_r.reason = Reason::Unavailable;
    if (deferred)
        m_deferredNet = true;
    else
        m_netSticky = true;
    log(deferred ? QStringLiteral("dac unavailable until the phone transport ends")
                 : QStringLiteral("dac sink still missing, unavailable until the next disconnect"));
    m_ports.output->setHeld(HeldOutputPort::HeldState::None);
    m_ports.output->watchCard(m_card.alsa, m_card.id);
    if (effective())
        m_ports.mixer->open(m_card.id);
    emit notice(QStringLiteral("usb dac unavailable"));
    finishRelease();
    m_reason = Reason::Unavailable;
    updateAll();
}

void OutputMode::onNetTimer()
{
    if (!m_r.running)
        return;
    switch (m_r.phase) {
    case Phase::Sink:
    case Phase::Read:
        if (m_r.switching) {
            log(QStringLiteral("switch bound reached, dac sink not confirmed"));
            endReturn();
            return;
        }
        if (!cardPresent()) {
            endReturnGone();
            return;
        }
        if (m_r.phase == Phase::Read || !m_r.net) {
            endReturn();
            return;
        }
        ++m_netFirings;
        if (m_netSticky || m_netFirings > 1) {
            log(QStringLiteral("dac sink missing again, no further restart"));
            failReturn(false);
            return;
        }
        if (!m_ports.wireplumber || !m_ports.wireplumber->restartForNet(QStringLiteral("dac sink missing"))) {
            log(QStringLiteral("dac sink missing, restart deferred for the phone"));
            failReturn(true);
            return;
        }
        log(QStringLiteral("dac sink missing after release, restarting wireplumber"));
        m_r.phase = Phase::Restart;
        m_netTimer.start(m_bounds.restartWaitMs);
        return;
    case Phase::Restart:
        m_r.phase = Phase::Second;
        m_netTimer.start(m_bounds.netSecondMs);
        return;
    case Phase::Second:
        failReturn(false);
        return;
    default:
        return;
    }
}

void OutputMode::retryNet()
{
    if (!m_deferredNet || m_r.running || m_acq != Acquire::None || m_nameOwned || !cardPresent())
        return;
    if (!m_ports.wireplumber || !m_ports.wireplumber->restartForNet(QStringLiteral("dac sink missing"))) {
        log(QStringLiteral("dac sink missing, restart deferred for the phone"));
        return;
    }
    log(QStringLiteral("dac sink missing after release, restarting wireplumber"));
    m_deferredNet = false;
    m_r = Release{};
    m_r.running = true;
    m_r.reason = Reason::None;
    m_reason = Reason::None;
    m_r.phase = Phase::Restart;
    m_ports.output->setHeld(HeldOutputPort::HeldState::Returning);
    m_netTimer.start(m_bounds.restartWaitMs);
}

void OutputMode::finishRelease()
{
    const bool switching = m_r.switching;
    const bool failed = m_r.failed;
    m_r.running = false;
    m_r.phase = Phase::None;
    if (m_active != usb) {
        m_ports.mixer->close();
        m_ports.output->unwatchCard();
    }
    settle();
    log(QStringLiteral("release done"));
    if (m_waiting) {
        if (failed || switching) {
            m_waiting = false;
            drivePark(m_ports.player->positionMs());
        } else {
            decideWaiting();
        }
    } else if (m_reconcile && !switching) {
        reconcile();
    }
    m_reconcile = false;
    updateAll();
    if (switching) {
        m_switchR = true;
        emitSwitchReady();
    }
}

void OutputMode::decideWaiting()
{
    m_waiting = false;
    if (m_ports.player->playState() == PlayerPort::PlayState::Stopped)
        return;
    settle();
    if (!effective()) {
        driveBuild(Player::OutputChain::Pw);
        return;
    }
    if (m_level == bitperfect) {
        driveBuild(bitPerfectChain());
        return;
    }
    if (blocked()) {
        const Reason why = blockReason();
        if (why != Reason::None)
            m_reason = why;
        driveBuild(bitPerfectChain());
        return;
    }
    if (m_waitTarget != GST_STATE_PLAYING) {
        drivePark(m_ports.player->positionMs());
        return;
    }
    m_waiting = true;
    if (m_ports.wireplumber && m_ports.wireplumber->restarting()) {
        log(QStringLiteral("acquire waits for the wireplumber restart"));
        m_restartWaiting = true;
        m_restartWait.start();
        return;
    }
    startAcquire();
}

void OutputMode::resumeRestore()
{
    if (!m_restoreWanted)
        return;
    m_restoreWanted = false;
    m_readyTimer.stop();
    if (m_ports.bluez && m_ports.bluez->transportActive()) {
        log(QStringLiteral("restore stays paused, phone transport active"));
        return;
    }
    log(QStringLiteral("restore resumes"));
    ++m_driving;
    m_ports.player->resumeFrom();
    --m_driving;
    m_lastPlay = m_ports.player->playState();
    updateAll();
}

void OutputMode::waitReady()
{
    if (!m_ports.bluez || m_ports.bluez->ready()) {
        resumeRestore();
        return;
    }
    if (!m_readyTimer.isActive())
        m_readyTimer.start();
}

void OutputMode::drivePark(qint64 positionMs)
{
    ++m_driving;
    m_ports.player->park(positionMs);
    --m_driving;
    m_lastPlay = m_ports.player->playState();
}

void OutputMode::driveRebuild(qint64 positionMs, GstState state)
{
    ++m_driving;
    m_ports.player->rebuildAt(positionMs, state);
    --m_driving;
    m_lastPlay = m_ports.player->playState();
    checkStopped();
}

void OutputMode::driveBuild(Player::OutputChain chain)
{
    if (chain == Player::OutputChain::Pw) {
        m_builtRate = pwRate();
        m_builtFixed = fixedRate();
    }
    ++m_driving;
    m_ports.player->buildPending(chain);
    --m_driving;
    m_lastPlay = m_ports.player->playState();
    checkStopped();
}

void OutputMode::log(const QString &text) const
{
    qInfo().noquote() << "microdap output:" << text;
}

}
