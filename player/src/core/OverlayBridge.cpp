#include "OverlayBridge.h"

#include "DbusPad.h"
#include "Intercept.h"
#include "Keep.h"
#include "Player.h"
#include "SleepInhibitor.h"
#include "State.h"

#include <QCoreApplication>
#include <QDebug>
#include <QtGlobal>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstddef>
#include <cstring>
#include <utility>

namespace microdap {

OverlayBridge::OverlayBridge(DbusPad *pad, Player *player, QObject *parent)
    : QObject(parent)
    , m_pad(pad)
    , m_player(player)
    , m_lower([](QWindow *window) { window->lower(); })
    , m_sleep(new SleepInhibitor(QStringLiteral("hide overlay"), this))
{
    m_fade.setStartValue(0.0);
    m_fade.setEndValue(0.0);
    connect(&m_fade, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) { setOpacity(value.toReal()); });
    connect(&m_fade, &QVariantAnimation::finished, this, &OverlayBridge::onFadeFinished);
    connect(&m_fade, &QVariantAnimation::stateChanged, this, &OverlayBridge::onFadeState);
    connect(m_sleep, &SleepInhibitor::prepareForSleep, this, &OverlayBridge::onPrepareForSleep);
    m_sleep->take();
    if (m_pad) {
        Intercept *intercept = m_pad->intercept();
        connect(intercept, &Intercept::holderChanged, this, &OverlayBridge::onHolder);
        connect(intercept, &Intercept::acquired, this, &OverlayBridge::onAcquired);
        connect(intercept, &Intercept::arbiterLost, this, &OverlayBridge::onArbiterLost);
    }

    if (m_player)
        m_player->setPositionPollEnabled(false);

    m_watchdog.setTimerType(Qt::CoarseTimer);
    m_watchdog.setInterval(WatchdogMs);
    connect(&m_watchdog, &QTimer::timeout, this, &OverlayBridge::ping);
    if (!notifySocket().isEmpty())
        m_watchdog.start();

    m_clock.start();
    m_release.setSingleShot(true);
    m_release.setInterval(ReleaseFrameMs);
    connect(&m_release, &QTimer::timeout, this, [this] {
        if (m_phase != Phase::Releasing)
            return;
        step("no frame");
        finishRelease();
    });
    m_closeTimer.setSingleShot(true);
    m_closeTimer.setInterval(CloseTimeoutMs);
    connect(&m_closeTimer, &QTimer::timeout, this, [this] {
        qWarning() << "microdap overlay: close steps timed out after" << CloseTimeoutMs << "ms";
        finishClose();
    });
}

OverlayBridge::~OverlayBridge()
{
    m_fade.stop();
    m_watchdog.stop();
    m_release.stop();
    m_closeTimer.stop();
    dropCloseProcess();
    if (m_pad)
        m_pad->setShown(false);
}

QByteArray OverlayBridge::notifySocket()
{
    return qgetenv("NOTIFY_SOCKET");
}

QString OverlayBridge::btBin()
{
    return qEnvironmentVariable("MICRODAP_BT_BIN", QStringLiteral("/usr/libexec/microdap/microdap-bt"));
}

bool OverlayBridge::notify(const QByteArray &state)
{
    const QByteArray path = notifySocket();
    if (path.isEmpty())
        return false;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= static_cast<qsizetype>(sizeof(address.sun_path)))
        return false;
    std::memcpy(address.sun_path, path.constData(), static_cast<size_t>(path.size()));
    socklen_t length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() + 1);
    if (path.at(0) == '@') {
        address.sun_path[0] = '\0';
        length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size());
    }
    const int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return false;
    const ssize_t sent = sendto(fd, state.constData(), static_cast<size_t>(state.size()), MSG_NOSIGNAL,
                                reinterpret_cast<const sockaddr *>(&address), length);
    close(fd);
    return sent == state.size();
}

DbusPad *OverlayBridge::pad() const
{
    return m_pad;
}

Player *OverlayBridge::player() const
{
    return m_player;
}

QWindow *OverlayBridge::window() const
{
    return m_window;
}

void OverlayBridge::setWindow(QWindow *window, WindowPrepare prepare)
{
    if (m_window && m_frames)
        disconnect(m_window.data(), SIGNAL(frameSwapped()), this, SLOT(onFrameSwapped()));
    m_window = window;
    m_frames = m_window && m_window->metaObject()->indexOfSignal("frameSwapped()") >= 0;
    if (m_frames)
        connect(m_window.data(), SIGNAL(frameSwapped()), this, SLOT(onFrameSwapped()));
    m_prepare = prepare ? std::move(prepare) : WindowPrepare([](QWindow *target) { target->create(); });
    if (m_window && !m_window->handle())
        m_prepare(m_window);
    notifyReady();
}

QWindow *OverlayBridge::toastWindow() const
{
    return m_toast;
}

void OverlayBridge::setToastWindow(QWindow *window, WindowPrepare prepare)
{
    if (m_toast)
        disconnect(m_toast.data(), &QWindow::visibleChanged, this, nullptr);
    m_toast = window;
    m_prepareToast = prepare ? std::move(prepare) : WindowPrepare([](QWindow *target) { target->create(); });
    if (m_toast)
        connect(m_toast.data(), &QWindow::visibleChanged, this, [this](bool visible) {
            if (visible && m_toast)
                m_lower(m_toast);
        });
}

void OverlayBridge::setLower(WindowPrepare lower)
{
    m_lower = std::move(lower);
}

State *OverlayBridge::state() const
{
    return m_state;
}

void OverlayBridge::setState(State *state)
{
    if (m_state == state)
        return;
    if (m_state)
        disconnect(m_state, nullptr, this, nullptr);
    m_state = state;
    if (m_state)
        connect(m_state, &QObject::destroyed, this, [this] { m_state = nullptr; });
}

SleepInhibitor *OverlayBridge::sleep() const
{
    return m_sleep;
}

bool OverlayBridge::ready() const
{
    return m_ready;
}

bool OverlayBridge::shown() const
{
    return m_shown;
}

bool OverlayBridge::buried() const
{
    if (!m_pad || !m_shown || !m_inStack || m_acquiring)
        return false;
    const QString holder = m_pad->intercept()->holder();
    return !holder.isEmpty() && holder != m_pad->intercept()->name();
}

bool OverlayBridge::mapped() const
{
    return m_mapped;
}

qreal OverlayBridge::opacity() const
{
    return m_opacity;
}

bool OverlayBridge::fading() const
{
    return m_fading;
}

bool OverlayBridge::sleeping() const
{
    return m_sleeping;
}

bool OverlayBridge::releasing() const
{
    return m_phase == Phase::Releasing;
}

int OverlayBridge::mapCount() const
{
    return m_mapCount;
}

int OverlayBridge::toastMapCount() const
{
    return m_toastMapCount;
}

bool OverlayBridge::watchdogActive() const
{
    return m_watchdog.isActive();
}

void OverlayBridge::show()
{
    if (m_shown) {
        if (buried())
            raise();
        return;
    }
    m_clock.start();
    m_release.stop();
    m_releaseArmed = false;
    m_phase = Phase::Showing;
    m_shown = true;
    qInfo() << "microdap overlay: show requested";
    emit shownChanged();
    m_acquiring = true;
    if (m_pad && m_pad->shown())
        m_pad->intercept()->acquire();
    else if (m_pad)
        m_pad->setShown(true);
    mapWindow();
    setMapped(true);
    if (m_player)
        m_player->setPositionPollEnabled(true);
    fadeTo(1.0, ShowMs, QEasingCurve::Linear);
}

void OverlayBridge::hide()
{
    if (!m_shown)
        return;
    m_clock.start();
    m_phase = Phase::Hiding;
    m_shown = false;
    m_acquiring = false;
    m_inStack = false;
    qInfo() << "microdap overlay: hide requested";
    emit shownChanged();
    if (m_pad)
        m_pad->setShown(false);
    if (!m_window || !m_window->handle()) {
        m_fade.stop();
        setOpacity(0.0);
        finishRelease();
        return;
    }
    fadeTo(0.0, HideMs, QEasingCurve::OutExpo);
}

void OverlayBridge::toggle()
{
    if (!m_shown)
        show();
    else if (buried())
        raise();
    else
        hide();
}

void OverlayBridge::raise()
{
    qInfo() << "microdap overlay: back to the top";
    m_clock.start();
    m_acquiring = true;
    m_pad->intercept()->acquire();
}

void OverlayBridge::onHolder()
{
    if (!m_shown || !m_pad)
        return;
    if (m_pad->intercept()->holder() == m_pad->intercept()->name()) {
        if (m_window && m_window->handle() && m_window->isVisible())
            return;
        m_fade.stop();
        setOpacity(1.0);
        m_phase = Phase::Shown;
        mapWindow();
        setMapped(true);
        if (m_player)
            m_player->setPositionPollEnabled(true);
        step("holder again");
        return;
    }
    if (!buried())
        return;
    m_fade.stop();
    m_release.stop();
    setOpacity(1.0);
    m_phase = Phase::Shown;
    destroyWindow();
    setMapped(false);
    if (m_player)
        m_player->setPositionPollEnabled(false);
    step("buried");
}

void OverlayBridge::onAcquired(bool ok)
{
    if (!m_shown || !m_pad->shown())
        return;
    m_acquiring = false;
    if (!ok) {
        qWarning() << "microdap overlay: intercept failed";
        return;
    }
    m_inStack = true;
    onHolder();
}

void OverlayBridge::onArbiterLost()
{
    m_inStack = false;
    if (!m_shown || (m_window && m_window->handle()))
        return;
    qInfo() << "microdap overlay: arbiter gone while buried, hidden";
    hide();
}

void OverlayBridge::closePlayer()
{
    if (m_closing) {
        qInfo() << "microdap overlay: close player already running";
        return;
    }
    qInfo() << "microdap overlay: close player";
    m_closing = true;
    if (m_pad)
        m_pad->setShown(false);
    hide();
    Keep::clear();
    releaseToast();
    if (m_state)
        m_state->save(false);
    m_closeStep = 0;
    m_closeTimer.start();
    runCloseStep();
}

void OverlayBridge::runCloseStep()
{
    if (m_closeDone)
        return;
    dropCloseProcess();
    QString program;
    QStringList arguments;
    if (m_closeStep == 0) {
        program = btBin();
        arguments = {QStringLiteral("--disconnect-sources")};
    } else {
        finishClose();
        return;
    }
    ++m_closeStep;
    auto *process = new QProcess(this);
    m_closeProcess = process;
    process->setProcessChannelMode(QProcess::ForwardedChannels);
    process->setStandardInputFile(QProcess::nullDevice());
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        if (process != m_closeProcess.data())
            return;
        if (status != QProcess::NormalExit || code != 0)
            qWarning().noquote() << "microdap overlay:" << process->program() << "exit"
                                 << (status == QProcess::NormalExit ? code : -1);
        QMetaObject::invokeMethod(this, &OverlayBridge::runCloseStep, Qt::QueuedConnection);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (process != m_closeProcess.data() || error != QProcess::FailedToStart)
            return;
        qWarning().noquote() << "microdap overlay: cannot start" << process->program();
        QMetaObject::invokeMethod(this, &OverlayBridge::runCloseStep, Qt::QueuedConnection);
    });
    qInfo().noquote() << "microdap overlay: close" << program << arguments.join(QLatin1Char(' '));
    process->start(program, arguments);
}

void OverlayBridge::dropCloseProcess()
{
    QProcess *process = m_closeProcess.data();
    if (!process)
        return;
    m_closeProcess = nullptr;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(CloseKillWaitMs);
    }
    process->deleteLater();
}

void OverlayBridge::finishClose()
{
    if (m_closeDone)
        return;
    m_closeDone = true;
    m_closeTimer.stop();
    dropCloseProcess();
    qInfo() << "microdap overlay: close player done";
    emit closed();
    if (m_state) {
        m_state->quit(false);
        return;
    }
    if (QCoreApplication *app = QCoreApplication::instance())
        app->quit();
}

bool OverlayBridge::prepareToast()
{
    if (!m_toast || m_shown)
        return false;
    if (!m_toast->handle())
        m_prepareToast(m_toast);
    ++m_toastMapCount;
    return m_toast->handle() != nullptr;
}

void OverlayBridge::releaseToast()
{
    if (!m_toast || !m_toast->handle())
        return;
    m_toast->setVisible(false);
    m_toast->destroy();
}

void OverlayBridge::fadeTo(qreal to, int ms, QEasingCurve::Type curve)
{
    m_fade.stop();
    m_fade.setStartValue(m_opacity);
    m_fade.setEndValue(to);
    m_fade.setDuration(ms);
    m_fade.setEasingCurve(curve);
    m_fade.start();
}

void OverlayBridge::onFadeFinished()
{
    setOpacity(m_fade.endValue().toReal());
    step("fade done");
    if (m_shown) {
        m_phase = Phase::Shown;
        return;
    }
    if (m_opacity > 0.0)
        return;
    beginRelease();
}

void OverlayBridge::onFrameSwapped()
{
    if (m_firstFrame) {
        m_firstFrame = false;
        step("first frame");
    }
    if (m_phase != Phase::Releasing || !m_releaseArmed)
        return;
    m_releaseArmed = false;
    const quint64 generation = m_releaseGeneration;
    QMetaObject::invokeMethod(this, [this, generation] {
        if (m_phase != Phase::Releasing || generation != m_releaseGeneration)
            return;
        step("last frame");
        finishRelease();
    }, Qt::QueuedConnection);
}

void OverlayBridge::beginRelease()
{
    if (!m_window || !m_window->handle() || !m_window->isVisible() || !m_frames) {
        finishRelease();
        return;
    }
    m_phase = Phase::Releasing;
    m_releaseArmed = false;
    const quint64 generation = ++m_releaseGeneration;
    QMetaObject::invokeMethod(this, [this, generation] {
        if (m_phase == Phase::Releasing && generation == m_releaseGeneration)
            m_releaseArmed = true;
    }, Qt::QueuedConnection);
    m_release.start();
    QMetaObject::invokeMethod(m_window.data(), "update", Qt::DirectConnection);
}

void OverlayBridge::finishRelease()
{
    m_release.stop();
    m_releaseArmed = false;
    m_phase = Phase::Hidden;
    setMapped(false);
    destroyWindow();
    if (m_player)
        m_player->setPositionPollEnabled(false);
    QTimer::singleShot(0, this, &OverlayBridge::saveHidden);
}

void OverlayBridge::saveHidden()
{
    if (m_state) {
        m_state->save(false);
        step("saved");
    }
    if (m_sleeping)
        m_sleep->release();
}

void OverlayBridge::onFadeState()
{
    const bool fading = m_fade.state() == QAbstractAnimation::Running;
    if (fading == m_fading)
        return;
    m_fading = fading;
    emit fadingChanged();
}

void OverlayBridge::onPrepareForSleep(bool start)
{
    if (start) {
        setSleeping(true);
        if (m_shown) {
            hide();
            return;
        }
        if (m_pad)
            m_pad->setShown(false);
        if (m_state)
            m_state->save(false);
        m_sleep->release();
        return;
    }
    setSleeping(false);
    if (m_pad)
        m_pad->setShown(false);
    m_sleep->take();
}

void OverlayBridge::setOpacity(qreal value)
{
    if (qFuzzyCompare(m_opacity + 1.0, value + 1.0))
        return;
    m_opacity = value;
    emit opacityChanged();
}

void OverlayBridge::setMapped(bool mapped)
{
    if (m_mapped == mapped)
        return;
    m_mapped = mapped;
    if (mapped)
        ++m_mapCount;
    emit mappedChanged();
}

void OverlayBridge::setSleeping(bool sleeping)
{
    if (m_sleeping == sleeping)
        return;
    m_sleeping = sleeping;
    emit sleepingChanged();
}

void OverlayBridge::mapWindow()
{
    if (!m_window)
        return;
    if (!m_window->handle()) {
        m_prepare(m_window);
        step("created");
    }
    if (m_window->isVisible())
        return;
    m_firstFrame = m_frames;
    m_window->setVisible(true);
    m_lower(m_window);
    step("mapped");
}

void OverlayBridge::destroyWindow()
{
    if (!m_window || !m_window->handle())
        return;
    m_window->setVisible(false);
    step("unmapped");
    m_window->destroy();
    step("destroyed");
}

void OverlayBridge::step(const char *name) const
{
    qInfo().nospace() << "microdap overlay: " << name << " after " << m_clock.elapsed() << " ms";
}

void OverlayBridge::notifyReady()
{
    if (m_ready)
        return;
    m_ready = true;
    if (notifySocket().isEmpty())
        return;
    if (!notify(QByteArrayLiteral("READY=1\n")))
        qWarning() << "microdap overlay: ready notify failed" << notifySocket();
    ping();
}

void OverlayBridge::ping()
{
    if (notify(QByteArrayLiteral("WATCHDOG=1\n"))) {
        m_pingFailed = false;
        return;
    }
    if (m_pingFailed)
        return;
    m_pingFailed = true;
    qWarning() << "microdap overlay: watchdog ping failed" << notifySocket();
}

}
