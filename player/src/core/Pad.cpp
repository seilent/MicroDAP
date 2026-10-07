#include "Pad.h"

#include <QMetaObject>
#include <QThread>
#include <QtGlobal>

#include <cstdio>
#include <utility>

namespace casdap {

namespace {

constexpr double axisScale = 32767.0;
constexpr double stickEnter = 0.5;
constexpr double stickExit = 0.35;
constexpr double triggerEnter = 0.5;
constexpr double triggerExit = 0.35;

int buttonAction(Uint8 button)
{
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return Pad::A;
    case SDL_GAMEPAD_BUTTON_EAST:
        return Pad::B;
    case SDL_GAMEPAD_BUTTON_WEST:
        return Pad::X;
    case SDL_GAMEPAD_BUTTON_NORTH:
        return Pad::Y;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return Pad::LB;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return Pad::RB;
    case SDL_GAMEPAD_BUTTON_START:
        return Pad::Start;
    case SDL_GAMEPAD_BUTTON_BACK:
        return Pad::Back;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return Pad::Up;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return Pad::Down;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return Pad::Left;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return Pad::Right;
    default:
        return -1;
    }
}

}

Pad::Pad(QObject *parent)
    : PadSource(parent)
    , m_log(qEnvironmentVariableIntValue("CASDAP_PAD_LOG") == 1)
{
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    m_valid = SDL_Init(SDL_INIT_GAMEPAD);
    if (!m_valid) {
        qWarning("casdap pad: SDL_Init failed: %s", SDL_GetError());
        return;
    }
    connect(&m_pump, &QTimer::timeout, this, &Pad::pump);
    m_repeat.setTimerType(Qt::PreciseTimer);
    connect(&m_repeat, &QTimer::timeout, this, &Pad::repeatTick);
    SDL_AddEventWatch(&Pad::eventWatch, this);
    pump();
    updatePump();
}

Pad::~Pad()
{
    if (!m_valid)
        return;
    SDL_RemoveEventWatch(&Pad::eventWatch, this);
    m_pump.stop();
    m_repeat.stop();
    for (SDL_Gamepad *pad : std::as_const(m_pads))
        SDL_CloseGamepad(pad);
    m_pads.clear();
    SDL_Quit();
}

bool SDLCALL Pad::eventWatch(void *userdata, SDL_Event *)
{
    auto *self = static_cast<Pad *>(userdata);
    if (QThread::currentThread() == self->thread() && self->m_pumping)
        return true;
    if (!self->m_pumpQueued.exchange(true))
        QMetaObject::invokeMethod(self, &Pad::pump, Qt::QueuedConnection);
    return true;
}

bool Pad::isValid() const
{
    return m_valid;
}

bool Pad::screenOff() const
{
    return m_screenOff;
}

void Pad::setScreenOff(bool off)
{
    if (m_screenOff == off)
        return;
    if (!off && m_valid)
        pump();
    m_screenOff = off;
    if (off) {
        stopRepeat();
        for (int i = 0; i < ActionCount; ++i) {
            if (!m_down[i])
                continue;
            m_down[i] = false;
            emit released(static_cast<Action>(i));
        }
    }
    updatePump();
    emit screenOffChanged();
}

bool Pad::connected() const
{
    return !m_pads.isEmpty();
}

int Pad::gamepadCount() const
{
    return static_cast<int>(m_pads.size());
}

bool Pad::polling() const
{
    return m_pump.isActive();
}

int Pad::pumpInterval() const
{
    if (m_screenOff)
        return 0;
    return m_pads.isEmpty() ? DetectIntervalMs : PumpIntervalMs;
}

void Pad::updatePump()
{
    if (!m_valid)
        return;
    const int interval = pumpInterval();
    if (interval <= 0) {
        m_pump.stop();
        return;
    }
    if (m_pump.isActive() && m_pump.interval() == interval)
        return;
    m_pump.start(interval);
}

void Pad::pump()
{
    m_pumpQueued.store(false);
    m_pumping = true;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_GAMEPAD_ADDED:
            addGamepad(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            removeGamepad(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            handleButton(event.gbutton.button, event.gbutton.down);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            handleAxis(event.gaxis.axis, event.gaxis.value);
            break;
        default:
            break;
        }
    }
    m_pumping = false;
}

void Pad::repeatTick()
{
    if (m_repeatAction < 0) {
        m_repeat.stop();
        return;
    }
    emitAction(static_cast<Action>(m_repeatAction), true);
    if (m_repeat.interval() != RepeatIntervalMs)
        m_repeat.start(RepeatIntervalMs);
}

void Pad::addGamepad(SDL_JoystickID id)
{
    if (m_pads.contains(id))
        return;
    SDL_Gamepad *pad = SDL_OpenGamepad(id);
    if (!pad) {
        qWarning("casdap pad: open %u failed: %s", static_cast<unsigned>(id), SDL_GetError());
        return;
    }
    m_pads.insert(id, pad);
    if (m_log) {
        const char *name = SDL_GetGamepadName(pad);
        std::fprintf(stderr, "pad: added %u %s\n", static_cast<unsigned>(id), name ? name : "unnamed");
    }
    if (m_pads.size() == 1) {
        updatePump();
        emit connectedChanged();
    }
}

void Pad::removeGamepad(SDL_JoystickID id)
{
    SDL_Gamepad *pad = m_pads.take(id);
    if (!pad)
        return;
    SDL_CloseGamepad(pad);
    if (m_log)
        std::fprintf(stderr, "pad: removed %u\n", static_cast<unsigned>(id));
    resetState();
    if (m_pads.isEmpty()) {
        updatePump();
        emit connectedChanged();
    }
}

void Pad::handleButton(Uint8 button, bool down)
{
    const int mapped = buttonAction(button);
    if (mapped < 0)
        return;
    const Action a = static_cast<Action>(mapped);
    if (isDirection(mapped)) {
        setDirection(a, false, down);
        return;
    }
    if (down)
        press(a);
    else
        release(a);
}

void Pad::handleAxis(Uint8 axis, Sint16 value)
{
    switch (axis) {
    case SDL_GAMEPAD_AXIS_LEFTX:
        updateStick(Left, Right, value);
        break;
    case SDL_GAMEPAD_AXIS_LEFTY:
        updateStick(Up, Down, value);
        break;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
        updateTrigger(LT, value);
        break;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
        updateTrigger(RT, value);
        break;
    default:
        break;
    }
}

void Pad::updateStick(Action negative, Action positive, Sint16 value)
{
    const double v = value / axisScale;
    const bool negativeOn = m_stick[negative] ? -v >= stickExit : -v >= stickEnter;
    const bool positiveOn = m_stick[positive] ? v >= stickExit : v >= stickEnter;
    setDirection(negative, true, negativeOn);
    setDirection(positive, true, positiveOn);
}

void Pad::updateTrigger(Action trigger, Sint16 value)
{
    bool &state = trigger == LT ? m_lt : m_rt;
    const double v = value / axisScale;
    const bool on = state ? v >= triggerExit : v >= triggerEnter;
    if (on == state)
        return;
    state = on;
    if (on)
        press(trigger);
    else
        release(trigger);
}

void Pad::setDirection(Action direction, bool fromStick, bool on)
{
    std::array<bool, 4> &source = fromStick ? m_stick : m_dpad;
    if (source[direction] == on)
        return;
    const bool before = m_dpad[direction] || m_stick[direction];
    source[direction] = on;
    const bool after = m_dpad[direction] || m_stick[direction];
    if (before == after)
        return;
    if (after) {
        press(direction);
        if (m_down[direction])
            startRepeat(direction);
        return;
    }
    release(direction);
    if (m_repeatAction != direction)
        return;
    stopRepeat();
    for (int i = Up; i <= Right; ++i) {
        if (m_down[i]) {
            startRepeat(static_cast<Action>(i));
            return;
        }
    }
}

void Pad::startRepeat(Action direction)
{
    m_repeatAction = direction;
    m_repeat.start(RepeatInitialMs);
}

void Pad::stopRepeat()
{
    m_repeat.stop();
    m_repeatAction = -1;
}

void Pad::press(Action a)
{
    if (m_down[a] || !allowed(a))
        return;
    m_down[a] = true;
    emit pressed(a);
    emitAction(a, false);
}

void Pad::release(Action a)
{
    if (!m_down[a])
        return;
    m_down[a] = false;
    if (allowed(a))
        emit released(a);
}

void Pad::emitAction(Action a, bool repeat)
{
    if (!allowed(a))
        return;
    if (m_log)
        std::fprintf(stderr, "pad: %s%s\n", label(a), repeat ? " repeat" : "");
    emit action(a, repeat);
}

bool Pad::allowed(Action) const
{
    return !m_screenOff;
}

void Pad::resetState()
{
    stopRepeat();
    m_dpad.fill(false);
    m_stick.fill(false);
    m_lt = false;
    m_rt = false;
    for (int i = 0; i < ActionCount; ++i) {
        if (m_down[i])
            release(static_cast<Action>(i));
    }
}

}
