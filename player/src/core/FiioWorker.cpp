#include "FiioWorker.h"

#include <QDeadlineTimer>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <thread>

namespace microdap {

namespace {

constexpr int guiWaitMs = 500;

void logLine(const std::string &line)
{
    qInfo().noquote() << "microdap dac eq:" << QString::fromStdString(line);
}

const FiioModel *modelAt(const FiioPaths &paths, const QString &sysPath)
{
    const QString wanted = QFileInfo(sysPath).canonicalFilePath();
    if (wanted.isEmpty())
        return nullptr;
    for (const FiioMatch &match : usbDevicesByModel(paths.sysRoot)) {
        if (QFileInfo(QString::fromStdString(match.devPath)).canonicalFilePath() == wanted)
            return match.model;
    }
    return nullptr;
}

FiioReason retrySleep(int ms, std::optional<int64_t> deadline, const FiioBatch::Abandoned &abandoned)
{
    int64_t end = fiioNowMs() + std::max(ms, 0);
    if (deadline)
        end = std::min(end, *deadline);
    for (;;) {
        if (abandoned())
            return FiioReason::Abandoned;
        const int64_t now = fiioNowMs();
        if (deadline && now >= *deadline)
            return FiioReason::Deadline;
        if (now >= end)
            return FiioReason::None;
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min<int64_t>(10, end - now)));
    }
}

QString interfaceDir(const QString &sysPath, const FiioModel &model)
{
    return sysPath + QLatin1Char('/') + QFileInfo(sysPath).fileName() + QStringLiteral(":1.")
           + QString::number(model.interface);
}

}

FiioWorker::FiioWorker(std::unique_ptr<UsbIo> usb, Bounds bounds, FiioPaths paths, QObject *parent)
    : QObject(parent)
    , m_thread(new QThread)
    , m_runner(new FiioRunner(std::move(usb), bounds, std::move(paths)))
{
    m_thread->setObjectName(QStringLiteral("microdap-fiio"));
    m_runner->moveToThread(m_thread);
    connect(m_runner, &FiioRunner::done, this, &FiioWorker::batchDone, Qt::QueuedConnection);
    m_thread->start();
}

FiioWorker::~FiioWorker()
{
    if (!m_quitting)
        quitAndJoin(guiWaitMs);
    else
        m_thread->wait(QDeadlineTimer(guiWaitMs));
    if (m_thread->isRunning()) {
        qWarning() << "microdap dac eq: worker did not stop, left running";
        return;
    }
    delete m_runner;
    delete m_thread;
}

void FiioWorker::post(const Target &target)
{
    if (m_quitting)
        return;
    m_runner->latestSeq.store(target.seq);
    QMetaObject::invokeMethod(m_runner, [runner = m_runner, target] {
        runner->run(target);
    }, Qt::QueuedConnection);
}

void FiioWorker::postFinal(const Target &target, int ms)
{
    if (m_quitting)
        return;
    m_runner->quitDeadline.store(fiioNowMs() + std::max(ms, 0));
    m_runner->quitSeq.store(target.seq);
    post(target);
    quit();
}

void FiioWorker::cancel()
{
    m_runner->latestSeq.fetch_add(1);
}

bool FiioWorker::quitAndJoin(int ms)
{
    if (!m_quitting) {
        m_runner->latestSeq.fetch_add(1);
        quit();
    }
    if (m_thread->wait(QDeadlineTimer(ms)))
        return true;
    qWarning() << "microdap dac eq: worker still running" << ms << "ms after quit";
    return false;
}

void FiioWorker::quit()
{
    m_quitting = true;
    QMetaObject::invokeMethod(m_runner, [runner = m_runner] {
        runner->thread()->quit();
    }, Qt::QueuedConnection);
}

FiioRunner::FiioRunner(std::unique_ptr<UsbIo> usb, FiioWorker::Bounds bounds, FiioPaths paths)
    : m_usb(std::move(usb))
    , m_bounds(bounds)
    , m_paths(std::move(paths))
{
}

FiioMirror &FiioRunner::mirrorFor(const FiioWorker::Target &target)
{
    const std::string id = target.cardId.toStdString();
    FiioMirror &mirror = m_mirrors[id];
    if (mirror.cardId != id || mirror.generation != target.generation) {
        mirror = FiioMirror{};
        mirror.cardId = id;
        mirror.generation = target.generation;
    }
    return mirror;
}

void FiioRunner::run(const FiioWorker::Target &target)
{
    const quint64 seq = target.seq;
    const FiioBatch::Abandoned abandoned = [this, seq] { return latestSeq.load() != seq; };
    if (abandoned()) {
        emit done(seq, false, int(FiioReason::Abandoned), FiioMirror{});
        return;
    }
    FiioMirror &mirror = mirrorFor(target);
    if (target.kind == FiioWorker::Kind::Reset && target.exHeld) {
        emit done(seq, false, int(FiioReason::Refused), mirror);
        return;
    }
    std::optional<int64_t> deadline;
    if (quitSeq.load() == seq && seq != 0)
        deadline = quitDeadline.load();
    const FiioModel *model = modelAt(m_paths, target.sysPath);
    FiioReason reason = FiioReason::Device;
    if (model) {
        reason = attempt(target, *model, mirror, abandoned, deadline);
        if (reason == FiioReason::Claim || reason == FiioReason::Lock) {
            const FiioReason slept = retrySleep(m_bounds.retryMs, deadline, abandoned);
            reason = slept == FiioReason::None ? attempt(target, *model, mirror, abandoned, deadline) : slept;
        }
    } else {
        logLine("no supported dac at " + target.sysPath.toStdString());
    }
    if (reason == FiioReason::ResetDone) {
        mirror = FiioMirror{};
        mirror.cardId = target.cardId.toStdString();
        mirror.generation = target.generation;
    }
    const bool ok = reason == FiioReason::None || reason == FiioReason::ResetDone;
    emit done(seq, ok, int(reason), mirror);
}

FiioReason FiioRunner::attempt(const FiioWorker::Target &target, const FiioModel &model, FiioMirror &mirror,
                               const FiioBatch::Abandoned &abandoned, std::optional<int64_t> deadline)
{
    const std::string sysPath = target.sysPath.toStdString();
    FiioReason reason = waitDriverFree(sysPath, m_bounds.driverWaitMs, deadline, abandoned);
    if (reason != FiioReason::None)
        return reason;
    FiioLock lock(m_paths);
    if (!lock.acquire(m_bounds.lockMs, deadline, abandoned)) {
        if (abandoned())
            return FiioReason::Abandoned;
        if (deadline && fiioNowMs() >= *deadline)
            return FiioReason::Deadline;
        return FiioReason::Lock;
    }
    if (abandoned())
        return FiioReason::Abandoned;
    FiioDevice device(*m_usb, model, m_bounds.device, m_paths);
    device.setLog(logLine);
    if (target.kind == FiioWorker::Kind::Reset)
        return reset(target, device);
    reason = device.openClaimed(sysPath, deadline);
    if (reason != FiioReason::None)
        return reason;
    FiioResult result;
    switch (target.kind) {
    case FiioWorker::Kind::Take:
        result = FiioBatch::take(device, mirror, m_paths, target.frames, abandoned, deadline);
        break;
    case FiioWorker::Kind::Update:
        result = FiioBatch::update(device, mirror, m_paths, target.frames, abandoned, deadline);
        break;
    case FiioWorker::Kind::Bypass:
        result = FiioBatch::bypass(device, mirror, m_paths, target.frames, abandoned, deadline);
        break;
    case FiioWorker::Kind::Force:
        result = FiioBatch::force(device, mirror, m_paths, target.frames, abandoned, deadline);
        break;
    case FiioWorker::Kind::Reset:
        break;
    }
    device.releaseClose();
    return result.reason;
}

FiioReason FiioRunner::reset(const FiioWorker::Target &target, FiioDevice &device)
{
    const FiioReason opened = device.openClaimedNoDrain(target.sysPath.toStdString());
    if (opened != FiioReason::None)
        return opened;
    QFile status(QString::fromStdString(m_paths.asoundDir)
                 + QStringLiteral("/card%1/pcm0p/sub0/status").arg(target.alsaIndex));
    const QByteArray state = status.open(QIODevice::ReadOnly) ? status.readLine().trimmed() : QByteArray();
    if (state != "closed") {
        logLine("pcm " + (state.isEmpty() ? std::string("unreadable") : state.toStdString()) + ", no reset");
        device.releaseClose();
        return FiioReason::Refused;
    }
    const FiioReason reason = device.resetAndClose();
    if (reason == FiioReason::ResetDone) {
        const bool bound = QFileInfo(interfaceDir(target.sysPath, device.model()) + QStringLiteral("/driver")).isSymLink();
        logLine(bound ? "driver bound after reset" : "driver unbound after reset");
    }
    return reason;
}

}
