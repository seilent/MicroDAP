#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

#include <SDL3/SDL.h>

#include <array>

namespace casdap {

class Pad : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool screenOff READ screenOff WRITE setScreenOff NOTIFY screenOffChanged)
    Q_PROPERTY(bool pocketControls READ pocketControls WRITE setPocketControls NOTIFY pocketControlsChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)

public:
    enum Action {
        Up,
        Down,
        Left,
        Right,
        A,
        B,
        X,
        Y,
        LB,
        RB,
        LT,
        RT,
        Start,
        Back,
    };
    Q_ENUM(Action)

    static constexpr int ActionCount = Back + 1;
    static constexpr int RepeatInitialMs = 300;
    static constexpr int RepeatIntervalMs = 80;
    static constexpr int PumpIntervalMs = 4;

    explicit Pad(QObject *parent = nullptr);
    ~Pad() override;

    bool isValid() const;
    bool screenOff() const;
    void setScreenOff(bool off);
    bool pocketControls() const;
    void setPocketControls(bool on);
    bool connected() const;
    int gamepadCount() const;
    Q_INVOKABLE QString actionName(casdap::Pad::Action action) const;

signals:
    void action(casdap::Pad::Action action, bool repeat);
    void pressed(casdap::Pad::Action action);
    void released(casdap::Pad::Action action);
    void screenOffChanged();
    void pocketControlsChanged();
    void connectedChanged();

private:
    void pump();
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
    bool m_pocketControls = false;
    QTimer m_pump;
    QTimer m_repeat;
    int m_repeatAction = -1;
    QHash<SDL_JoystickID, SDL_Gamepad *> m_pads;
    std::array<bool, ActionCount> m_down{};
    std::array<bool, 4> m_dpad{};
    std::array<bool, 4> m_stick{};
    bool m_lt = false;
    bool m_rt = false;
};

}
