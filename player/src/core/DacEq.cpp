#include "DacEq.h"

#include "Eq.h"
#include "OutputControl.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QVariantList>

#include <utility>

namespace microdap {

namespace {

const QString usb = QStringLiteral("usb");
const QString off = QStringLiteral("off");
const QString usbSinkPrefix = QStringLiteral("alsa_output.usb-");

bool framed(FiioWorker::Kind kind)
{
    return kind == FiioWorker::Kind::Take || kind == FiioWorker::Kind::Update;
}

QString boolText(bool on)
{
    return on ? QStringLiteral("true") : QStringLiteral("false");
}

}

DacEq::DacEq(Bounds bounds, Ports ports, QObject *parent)
    : QObject(parent)
    , m_bounds(bounds)
    , m_eq(ports.eq)
    , m_dac(ports.dac)
    , m_player(ports.player)
    , m_paths(ports.paths)
    , m_releasing(std::move(ports.releasing))
    , m_worker(std::make_unique<FiioWorker>(std::move(ports.usb), bounds.worker, ports.paths))
{
    connect(m_worker.get(), &FiioWorker::batchDone, this, &DacEq::onBatchDone);
    m_coalesce.setSingleShot(true);
    m_coalesce.setInterval(bounds.coalesceMs);
    connect(&m_coalesce, &QTimer::timeout, this, &DacEq::plan);
    m_resolve.setSingleShot(true);
    m_resolve.setInterval(bounds.firstResolveMs);
    connect(&m_resolve, &QTimer::timeout, this, [this] {
        if (!m_waitResolve)
            return;
        log(QStringLiteral("no first output resolve within the bound"));
        m_waitResolve = false;
        plan();
    });
    m_gate.setSingleShot(true);
    m_gate.setInterval(0);
    connect(&m_gate, &QTimer::timeout, this, &DacEq::checkGate);
    m_gateRetry.setInterval(bounds.gateRetryMs);
    connect(&m_gateRetry, &QTimer::timeout, this, &DacEq::checkGate);
    m_noCard.setSingleShot(true);
    m_noCard.setInterval(bounds.firstResolveMs);
    connect(&m_noCard, &QTimer::timeout, this, [this] {
        if (m_started && !m_final && m_effective && m_cardId.isEmpty())
            log(QStringLiteral("effective, no card"));
    });
}

DacEq::~DacEq() = default;

FiioProfileFrames DacEq::framesFor(const FiioModel &model, const EqProfile &profile)
{
    FiioProfileFrames frames;
    for (int i = 0; i < fiioBandCount; ++i) {
        const EqBand &band = profile.bands[i];
        FiioBand out;
        out.index = i;
        out.gain = band.gain;
        out.freq = band.freq;
        out.q = band.q;
        out.type = int(band.type);
        out.enabled = band.enabled;
        frames.bands[i] = bandPayload(model, out);
    }
    return frames;
}

void DacEq::start()
{
    if (m_started)
        return;
    m_started = true;
    m_usbKey = m_eq ? m_eq->key(usb) : QString();
    const QDir sound(QString::fromStdString(m_paths.sysRoot) + QStringLiteral("/class/sound"));
    for (const QString &entry : sound.entryList({QStringLiteral("card*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        const int alsa = entry.mid(4).toInt(&ok);
        if (ok && !find(readId(alsa)))
            addRecord(alsa, {});
    }
    log(QStringLiteral("start level %1 effective %2 output %3 dacs %4")
            .arg(m_level, boolText(m_effective), m_active.isEmpty() ? QStringLiteral("none") : m_active)
            .arg(m_records.size()));
    if (m_effective && m_cardId.isEmpty())
        m_noCard.start();
    if (m_level == off) {
        plan();
        return;
    }
    m_waitResolve = true;
    m_resolve.start();
    refresh();
}

QString DacEq::status() const
{
    return m_status;
}

QString DacEq::reason() const
{
    return m_reason;
}

QVariantMap DacEq::ranges() const
{
    const FiioModel &model = fiioModels().front();
    return {
        {QStringLiteral("gain"), QVariantList{model.minGain, model.maxGain}},
        {QStringLiteral("q"), QVariantList{model.minQ, model.maxQ}},
        {QStringLiteral("preamp"), QVariantList{0.0, 0.0}},
    };
}

bool DacEq::dacHeld() const
{
    return m_dacHeld;
}

QString DacEq::mode() const
{
    return m_mode;
}

void DacEq::startRelease(std::function<void()> released)
{
    m_switchRelease = true;
    m_releaseFailed.clear();
    if (!m_started || m_final || !needsRelease()) {
        refresh();
        if (released)
            released();
        return;
    }
    m_released = std::move(released);
    plan();
}

void DacEq::quitAndJoin(int ms)
{
    if (m_final)
        return;
    m_final = true;
    m_coalesce.stop();
    m_resolve.stop();
    m_gate.stop();
    m_gateRetry.stop();
    m_noCard.stop();
    m_released = nullptr;
    const Record *held = nullptr;
    for (const Record &record : std::as_const(m_records)) {
        if (record.held || record.known == Known::Slot) {
            held = &record;
            break;
        }
    }
    if (held) {
        FiioWorker::Target target;
        target.cardId = held->id;
        target.generation = held->generation;
        target.alsaIndex = held->alsa;
        target.sysPath = held->sysPath;
        target.exHeld = m_exHeld;
        target.kind = Kind::Bypass;
        target.seq = ++m_seq;
        log(QStringLiteral("quit bypass %1").arg(held->id));
        m_worker->postFinal(target, ms);
    }
    m_inflight.reset();
    m_worker->quitAndJoin(ms);
    refresh();
}

void DacEq::setLevel(const QString &level)
{
    if (level == m_level)
        return;
    m_level = level;
    m_blocked = false;
    plan();
}

void DacEq::onFirstApplied()
{
    if (!m_waitResolve)
        return;
    m_waitResolve = false;
    m_resolve.stop();
    plan();
}

void DacEq::onApplied(const QString &output, bool ok)
{
    Q_UNUSED(ok);
    if (OutputControl::outputs().contains(output))
        onFirstApplied();
}

void DacEq::onSinkAppeared(quint32, const QString &name, int)
{
    if (!m_started || m_final || m_effective || !m_dac || !name.startsWith(usbSinkPrefix))
        return;
    QPointer<DacEq> self(this);
    m_dac->sinkCard(name, [self](const DacCard &card) {
        if (!self || self->m_final || self->m_effective || card.alsaCard < 0)
            return;
        const QString id = self->readId(card.alsaCard);
        Record *record = self->find(id);
        if (!record)
            record = self->addRecord(card.alsaCard, id);
        if (!record)
            return;
        record->alsa = card.alsaCard;
        if (record->cardIndex != card.cardIndex) {
            record->cardIndex = card.cardIndex;
            self->bumpRecord(*record, "new card index");
        }
        record->known = Known::Unknown;
        self->plan();
    });
}

void DacEq::onCardRemoved(quint32 cardIndex)
{
    if (!m_started || m_final || m_effective)
        return;
    for (Record &record : m_records) {
        if (record.cardIndex != cardIndex)
            continue;
        record.cardIndex = 0xffffffffu;
        if (readId(record.alsa) == record.id) {
            bumpRecord(record, "card removed, id present");
            plan();
        } else {
            removeRecord(record.id, "card removed, id gone");
        }
        return;
    }
    QStringList gone;
    for (const Record &record : std::as_const(m_records)) {
        if (record.cardIndex == 0xffffffffu && !present(record))
            gone.append(record.id);
    }
    for (const QString &id : std::as_const(gone))
        removeRecord(id, "card removed, id gone");
}

void DacEq::onActiveOutputChanged(const QString &active)
{
    m_active = active;
    m_gate.start();
}

void DacEq::onPlayStateChanged()
{
    m_gate.start();
}

void DacEq::onSwitchingChanged(bool switching)
{
    m_switching = switching;
    if (!switching) {
        m_switchRelease = false;
        m_releaseFailed.clear();
        m_blocked = false;
    }
    plan();
}

void DacEq::onProfileChanged(const QString &output)
{
    if (output != usb || !m_eq)
        return;
    const QString key = m_eq->key(usb);
    if (key != m_usbKey) {
        m_usbKey = key;
        m_blocked = false;
        m_coalesce.stop();
        plan();
        return;
    }
    m_coalesce.start();
    refresh();
}

void DacEq::onEffectiveChanged(bool effective)
{
    if (effective == m_effective)
        return;
    m_effective = effective;
    m_blocked = false;
    if (!effective) {
        m_cardId.clear();
        m_cardAlsa = -1;
        m_unsupported = false;
        m_noCard.stop();
    } else if (m_started && m_cardId.isEmpty()) {
        m_noCard.start();
    }
    QStringList gone;
    for (const Record &record : std::as_const(m_records)) {
        if (!present(record))
            gone.append(record.id);
    }
    for (const QString &id : std::as_const(gone))
        removeRecord(id, effective ? "gone at disarm" : "gone at rearm");
    m_gate.start();
    plan();
}

void DacEq::onCardChanged(const QString &id, int alsa)
{
    if (id.isEmpty() || alsa < 0)
        return;
    const bool same = id == m_cardId && alsa == m_cardAlsa;
    m_noCard.stop();
    m_cardId = id;
    m_cardAlsa = alsa;
    Record *record = find(id);
    if (same && record)
        return;
    if (!record)
        record = addRecord(alsa, id);
    else
        record->alsa = alsa;
    m_unsupported = !record;
    if (!record)
        log(QStringLiteral("card %1 %2 unsupported").arg(id).arg(alsa));
    m_gate.start();
    plan();
}

void DacEq::onGenerationChanged(const QString &id, quint64, bool present)
{
    if (!m_effective)
        return;
    Record *record = find(id);
    if (!record)
        return;
    if (!present) {
        if (id == m_cardId) {
            m_cardId.clear();
            m_cardAlsa = -1;
            m_unsupported = false;
        }
        removeRecord(id, "card gone");
        return;
    }
    bumpRecord(*record, "output generation");
    plan();
}

void DacEq::onCardGoneChanged(bool gone)
{
    m_cardGone = gone;
    if (gone) {
        const QString id = std::exchange(m_cardId, QString());
        m_cardAlsa = -1;
        m_unsupported = false;
        if (m_effective && find(id)) {
            removeRecord(id, "card gone");
            return;
        }
    }
    plan();
}

void DacEq::onExHeldChanged(bool held)
{
    m_exHeld = held;
    m_gate.start();
}

DacEq::Record *DacEq::find(const QString &id)
{
    for (Record &record : m_records) {
        if (record.id == id)
            return &record;
    }
    return nullptr;
}

const DacEq::Record *DacEq::find(const QString &id) const
{
    for (const Record &record : m_records) {
        if (record.id == id)
            return &record;
    }
    return nullptr;
}

DacEq::Record *DacEq::addRecord(int alsa, const QString &id)
{
    const std::optional<FiioMatch> match = supportedModel(m_paths.sysRoot, alsa);
    if (!match)
        return nullptr;
    Record record;
    record.id = id.isEmpty() ? readId(alsa) : id;
    if (record.id.isEmpty())
        return nullptr;
    record.alsa = alsa;
    record.sysPath = QString::fromStdString(match->devPath);
    record.model = match->model;
    record.generation = ++m_generations;
    record.held = true;
    m_records.append(record);
    log(QStringLiteral("dac %1 card %2 at %3").arg(record.id).arg(alsa).arg(record.sysPath));
    refresh();
    return &m_records.last();
}

void DacEq::removeRecord(QString id, const char *why)
{
    for (qsizetype i = 0; i < m_records.size(); ++i) {
        if (m_records.at(i).id != id)
            continue;
        m_records.removeAt(i);
        log(QStringLiteral("dac %1 %2").arg(id, QLatin1StringView(why)));
        if (m_inflight && m_inflight->want.id == id) {
            m_inflight.reset();
            m_worker->cancel();
        }
        if (m_failB == id)
            m_failB.clear();
        m_releaseFailed.removeAll(id);
        m_failed = false;
        m_blocked = false;
        m_reason.clear();
        finishRelease();
        plan();
        return;
    }
}

void DacEq::bumpRecord(Record &record, const char *why)
{
    record.generation = ++m_generations;
    record.refusals = 0;
    m_blocked = false;
    log(QStringLiteral("dac %1 generation %2, %3").arg(record.id).arg(record.generation).arg(QLatin1StringView(why)));
}

QString DacEq::readId(int alsa) const
{
    if (alsa < 0)
        return {};
    QFile file(QString::fromStdString(m_paths.asoundDir) + QStringLiteral("/card%1/id").arg(alsa));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readLine()).trimmed();
}

bool DacEq::present(const Record &record) const
{
    const std::optional<FiioMatch> match = supportedModel(m_paths.sysRoot, record.alsa);
    return match && QString::fromStdString(match->devPath) == record.sysPath && readId(record.alsa) == record.id;
}

bool DacEq::pcmClosed(int alsa) const
{
    QFile file(QString::fromStdString(m_paths.asoundDir) + QStringLiteral("/card%1/pcm0p/sub0/status").arg(alsa));
    return file.open(QIODevice::ReadOnly) && file.readLine().trimmed() == "closed";
}

bool DacEq::effectiveWithCard() const
{
    return m_effective && !m_cardGone && !m_cardId.isEmpty();
}

bool DacEq::usbParses() const
{
    return m_eq && m_eq->key(usb) != off && !m_eq->missing(usb);
}

bool DacEq::needsRelease() const
{
    for (const Record &record : m_records) {
        if ((record.held || record.known == Known::Slot) && !record.wantReset
            && !m_releaseFailed.contains(record.id))
            return true;
    }
    return false;
}

bool DacEq::switchOnly() const
{
    return m_switching || m_switchRelease;
}

const DacEq::Record *DacEq::primary() const
{
    if (m_effective)
        return find(m_cardId);
    return m_records.isEmpty() ? nullptr : &m_records.first();
}

std::optional<DacEq::Want> DacEq::want() const
{
    if (switchOnly()) {
        for (const Record &record : m_records) {
            if ((record.held || record.known == Known::Slot) && !record.wantReset
                && !m_releaseFailed.contains(record.id))
                return Want{Kind::Bypass, record.id, {}, false};
        }
        return std::nullopt;
    }
    if (m_waitResolve)
        return std::nullopt;
    const Record *failB = find(m_failB);
    if (failB && !failB->wantReset)
        return Want{Kind::Bypass, m_failB, {}, true};
    if (m_blocked)
        return std::nullopt;
    if (m_effective && !m_cardGone && m_cardId.isEmpty())
        return std::nullopt;
    const bool holding = effectiveWithCard();
    if (holding) {
        const Record *held = find(m_cardId);
        if (held && held->model && !held->wantReset) {
            if (usbParses()) {
                const FiioProfileFrames frames = framesFor(*held->model, m_eq->profile(usb));
                if (held->known != Known::Slot || frames.bands != held->frames.bands)
                    return Want{held->known == Known::Slot ? Kind::Update : Kind::Take, held->id, frames, false};
            } else if (held->known != Known::Bypass || held->held) {
                return Want{Kind::Bypass, held->id, {}, false};
            }
        }
    }
    for (const Record &record : m_records) {
        if ((holding && record.id == m_cardId) || record.wantReset)
            continue;
        if (record.known == Known::Slot)
            return Want{Kind::Bypass, record.id, {}, false};
        if (record.known == Known::Unknown || record.held)
            return Want{Kind::Force, record.id, {}, false};
    }
    return std::nullopt;
}

void DacEq::plan()
{
    if (!m_started || m_final) {
        refresh();
        return;
    }
    const std::optional<Want> next = want();
    if (m_inflight) {
        const Want &current = m_inflight->want;
        const bool same = next && next->kind == current.kind && next->id == current.id
                          && (!framed(next->kind) || next->frames.bands == current.frames.bands);
        if (current.kind == Kind::Bypass || current.kind == Kind::Reset || same || !next
            || (current.kind == Kind::Force && next->kind == Kind::Force)) {
            refresh();
            return;
        }
    }
    if (!next || (framed(next->kind) && m_coalesce.isActive())) {
        refresh();
        return;
    }
    post(*next);
}

void DacEq::post(const Want &want)
{
    Record *record = find(want.id);
    if (!record)
        return;
    FiioWorker::Target target;
    target.cardId = record->id;
    target.generation = record->generation;
    target.alsaIndex = record->alsa;
    target.sysPath = record->sysPath;
    target.exHeld = m_exHeld;
    target.kind = want.kind;
    target.frames = want.frames;
    target.seq = ++m_seq;
    if (want.kind != Kind::Reset)
        record->held = true;
    if (want.failB)
        m_failB.clear();
    m_inflight = Inflight{target.seq, want};
    m_worker->post(target);
    refresh();
}

void DacEq::onBatchDone(quint64 seq, bool ok, int reason, const FiioMirror &)
{
    if (m_final || !m_inflight || seq != m_inflight->seq)
        return;
    const Inflight done = *m_inflight;
    m_inflight.reset();
    const FiioReason why = FiioReason(reason);
    Record *record = find(done.want.id);
    if (record && done.want.kind == Kind::Reset && why == FiioReason::Refused
        && ++record->refusals <= m_bounds.refusedRearms) {
        record->wantReset = true;
        log(QStringLiteral("reset refused, rearmed"));
    } else if (record && done.want.kind == Kind::Reset) {
        ++record->resets;
        if (why == FiioReason::ResetDone) {
            m_failed = false;
            m_reason.clear();
            bumpRecord(*record, "reset");
        } else {
            m_failed = true;
            m_blocked = true;
            m_reason = QStringLiteral("device");
            log(QStringLiteral("failed device, reset ended %1").arg(QLatin1StringView(reasonText(why))));
        }
    } else if (record && ok) {
        if (framed(done.want.kind)) {
            record->known = Known::Slot;
            record->frames = done.want.frames;
            m_failed = false;
            m_reason.clear();
        } else {
            record->known = Known::Bypass;
            record->held = false;
            if (!done.want.failB) {
                m_failed = false;
                m_reason.clear();
            }
        }
    } else if (record && why != FiioReason::Abandoned && why != FiioReason::Deadline) {
        fail(*record, done, why);
    }
    finishRelease();
    plan();
    m_gate.start();
}

void DacEq::fail(Record &record, const Inflight &done, FiioReason reason)
{
    log(QStringLiteral("failed %1").arg(QLatin1StringView(reasonText(reason))));
    m_failed = true;
    m_blocked = true;
    m_reason = QString::fromLatin1(reasonText(reason));
    if (switchOnly()) {
        if (done.want.kind == Kind::Bypass)
            m_releaseFailed.append(record.id);
        return;
    }
    if (reason == FiioReason::Overflow && record.resets == 0)
        record.wantReset = true;
    if (record.wantReset && gateOpen(record)) {
        record.wantReset = false;
        log(QStringLiteral("reset %1").arg(record.id));
        post(Want{Kind::Reset, record.id, {}, false});
        return;
    }
    if (done.want.kind != Kind::Bypass && !record.wantReset)
        m_failB = record.id;
}

bool DacEq::gateOpen(const Record &record) const
{
    if (m_exHeld || (m_releasing && m_releasing()))
        return false;
    const bool stopped = !m_player || m_player->playState() == PlayerPort::PlayState::Stopped;
    if (!stopped && m_active == usb)
        return false;
    return pcmClosed(record.alsa);
}

void DacEq::checkGate()
{
    if (!m_started || m_final || switchOnly() || m_inflight)
        return;
    for (Record &record : m_records) {
        if (!record.wantReset)
            continue;
        if (record.resets > 0) {
            record.wantReset = false;
            continue;
        }
        if (!gateOpen(record))
            continue;
        record.wantReset = false;
        log(QStringLiteral("reset %1").arg(record.id));
        post(Want{Kind::Reset, record.id, {}, false});
        return;
    }
}

void DacEq::finishRelease()
{
    if (!m_released || m_inflight || needsRelease())
        return;
    QMetaObject::invokeMethod(this, std::exchange(m_released, nullptr), Qt::QueuedConnection);
}

void DacEq::refresh()
{
    bool held = false;
    bool resetPending = false;
    for (const Record &record : std::as_const(m_records)) {
        held = held || record.held;
        resetPending = resetPending || record.wantReset;
    }
    if (resetPending && !m_final && !m_gateRetry.isActive())
        m_gateRetry.start();
    else if (!resetPending)
        m_gateRetry.stop();
    if (held != m_dacHeld) {
        m_dacHeld = held;
        if (m_eq)
            m_eq->setDacHeld(held);
        emit dacHeldChanged();
    }
    const QString status = computeStatus();
    if (status != m_status) {
        m_status = status;
        emit statusChanged();
    }
    const QString mode = computeMode();
    if (mode != m_mode) {
        m_mode = mode;
        emit modeChanged();
    }
}

QString DacEq::computeStatus() const
{
    if (m_effective && !m_cardGone && m_unsupported)
        return QStringLiteral("unsupported");
    if (m_inflight && framed(m_inflight->want.kind))
        return QStringLiteral("applying");
    if ((m_failed || m_blocked) && !m_records.isEmpty())
        return QStringLiteral("failed");
    if (m_coalesce.isActive() && effectiveWithCard() && find(m_cardId))
        return QStringLiteral("applying");
    const Record *record = primary();
    if (!record)
        return QStringLiteral("none");
    if (record->known == Known::Slot)
        return QStringLiteral("on");
    if (record->known == Known::Bypass)
        return QStringLiteral("flat");
    return QStringLiteral("none");
}

QString DacEq::computeMode() const
{
    if (m_records.isEmpty())
        return QStringLiteral("none");
    if (m_failed)
        return QStringLiteral("failed");
    if (m_inflight && framed(m_inflight->want.kind))
        return QStringLiteral("taking");
    if (m_inflight && m_inflight->want.kind == Kind::Bypass)
        return QStringLiteral("releasing");
    const Record *record = primary();
    if (record && record->known == Known::Slot)
        return QStringLiteral("slot");
    if (record && record->known == Known::Bypass)
        return QStringLiteral("bypass");
    return QStringLiteral("unknown");
}

void DacEq::log(const QString &text) const
{
    qInfo().noquote() << "microdap dac eq:" << text;
}

}
