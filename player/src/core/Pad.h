#pragma once

#include "PadSource.h"

#include <QHash>
#include <QString>
#include <QTimer>

#include <SDL3/SDL.h>

#include <array>
#include <atomic>

namespace casdap {

class Pad : public PadSource
{
    Q_OBJECT

public:
    static constexpr int PumpIntervalMs = 16;
    static constexpr int DetectIntervalMs = 1000;

    explicit Pad(QObject *parent = nullptr);
    ~Pad() override;

    bool isValid() const;
    bool screenOff() const override;
    void setScreenOff(bool off) override;
    bool connected() const override;
    int gamepadCount() const;
    bool polling() const;

private:
    static bool SDLCALL eventWatch(void *userdata, SDL_Event *event);

    void pump();
    int pumpInterval() const;
    void updatePump();
    void repeatTick();
    void addGamepad(SDL_JoystickID id);
    void removeGamepad(SDL_JoystickID id);
    void handleButton(Uint8 button, bool down);
    void handleAxis(Uint8 axis, Sint16 value);
    void updateStick(Action negative, Action positive, Sint16 value);
    void updateTrigger(Action trigger, Sint16 value);
    void setDirection(Action direction, bool fromStick, bool on);
    void startRepeat(Action direction);
    void stopRepeat();
    void press(Action a);
    void release(Action a);
    void emitAction(Action a, bool repeat);
    bool allowed(Action a) const;
    void resetState();

    bool m_valid = false;
    bool m_log = false;
    bool m_screenOff = false;
    QTimer m_pump;
    QTimer m_repeat;
    std::atomic_bool m_pumpQueued{false};
    bool m_pumping = false;
    int m_repeatAction = -1;
    QHash<SDL_JoystickID, SDL_Gamepad *> m_pads;
    std::array<bool, ActionCount> m_down{};
    std::array<bool, 4> m_dpad{};
    std::array<bool, 4> m_stick{};
    bool m_lt = false;
    bool m_rt = false;
};

}
