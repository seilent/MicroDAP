#include "Pad.h"

#include <QElapsedTimer>
#include <QList>
#include <QSignalSpy>
#include <QtTest>

#include <SDL3/SDL.h>

using namespace microdap;

namespace {

constexpr int waitMs = 3000;
constexpr int quietMs = 150;
constexpr int firstEarlyMs = 20;
constexpr int firstLateMs = 100;
constexpr int gapSlackMs = 10;
constexpr int meanToleranceMs = 25;
constexpr Sint16 axisMax = SDL_JOYSTICK_AXIS_MAX;
constexpr Sint16 axisMin = SDL_JOYSTICK_AXIS_MIN;

Sint16 scaled(double fraction)
{
    return static_cast<Sint16>(fraction * SDL_JOYSTICK_AXIS_MAX);
}

class VirtualPad
{
public:
    ~VirtualPad()
    {
        detach();
    }

    bool attach()
    {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = static_cast<Uint16>(SDL_JOYSTICK_TYPE_GAMEPAD);
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = "microdap virtual pad";
        m_id = SDL_AttachVirtualJoystick(&desc);
        if (m_id == 0)
            return false;
        m_joystick = SDL_OpenJoystick(m_id);
        return m_joystick != nullptr;
    }

    void detach()
    {
        if (m_joystick) {
            SDL_CloseJoystick(m_joystick);
            m_joystick = nullptr;
        }
        if (m_id != 0) {
            SDL_DetachVirtualJoystick(m_id);
            m_id = 0;
        }
    }

    bool button(SDL_GamepadButton button, bool down)
    {
        return m_joystick && SDL_SetJoystickVirtualButton(m_joystick, button, down);
    }

    bool axis(SDL_GamepadAxis axis, Sint16 value)
    {
        return m_joystick && SDL_SetJoystickVirtualAxis(m_joystick, axis, value);
    }

private:
    SDL_JoystickID m_id = 0;
    SDL_Joystick *m_joystick = nullptr;
};

struct Rig
{
    Pad pad;
    VirtualPad vp;
    QSignalSpy actions{&pad, &Pad::action};
    QSignalSpy pressed{&pad, &Pad::pressed};
    QSignalSpy released{&pad, &Pad::released};

    void clear()
    {
        actions.clear();
        pressed.clear();
        released.clear();
    }
};

int count(const QSignalSpy &spy)
{
    return static_cast<int>(spy.count());
}

Pad::Action actionAt(const QSignalSpy &spy, int i)
{
    return spy.at(i).at(0).value<Pad::Action>();
}

bool repeatAt(const QSignalSpy &spy, int i)
{
    return spy.at(i).at(1).toBool();
}

}

#define ATTACH(rig) \
    if (!(rig).pad.isValid()) { \
        QSKIP("SDL gamepad subsystem unavailable"); \
    } \
    QVERIFY((rig).vp.attach()); \
    QTRY_VERIFY_WITH_TIMEOUT((rig).pad.connected(), waitMs)

class PadTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void buttons();
    void guideIgnored();
    void dpadRepeat();
    void stickRepeat();
    void stickDeadzone();
    void triggers();
    void screenOffDrops();
    void screenOffStopsPolling();
    void screenOffReleasesHeld();
    void hotplug();

private:
    void repeatCheck(bool stick);
};

void PadTest::initTestCase()
{
    qputenv("SDL_JOYSTICK_HIDAPI", "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    qRegisterMetaType<Pad::Action>();
}

void PadTest::buttons()
{
    Rig rig;
    ATTACH(rig);

    const struct {
        SDL_GamepadButton button;
        Pad::Action action;
        const char *name;
    } rows[] = {
        {SDL_GAMEPAD_BUTTON_DPAD_UP, Pad::Up, "up"},
        {SDL_GAMEPAD_BUTTON_DPAD_DOWN, Pad::Down, "down"},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, Pad::Left, "left"},
        {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, Pad::Right, "right"},
        {SDL_GAMEPAD_BUTTON_SOUTH, Pad::A, "a"},
        {SDL_GAMEPAD_BUTTON_EAST, Pad::B, "b"},
        {SDL_GAMEPAD_BUTTON_WEST, Pad::X, "x"},
        {SDL_GAMEPAD_BUTTON_NORTH, Pad::Y, "y"},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, Pad::LB, "lb"},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, Pad::RB, "rb"},
        {SDL_GAMEPAD_BUTTON_START, Pad::Start, "start"},
        {SDL_GAMEPAD_BUTTON_BACK, Pad::Back, "back"},
    };

    for (const auto &row : rows) {
        rig.clear();
        QVERIFY(rig.vp.button(row.button, true));
        QTRY_VERIFY_WITH_TIMEOUT(count(rig.actions) >= 1, waitMs);
        QCOMPARE(actionAt(rig.actions, 0), row.action);
        QCOMPARE(repeatAt(rig.actions, 0), false);
        QCOMPARE(count(rig.pressed), 1);
        QCOMPARE(actionAt(rig.pressed, 0), row.action);
        QVERIFY(rig.vp.button(row.button, false));
        QTRY_VERIFY_WITH_TIMEOUT(count(rig.released) == 1, waitMs);
        QCOMPARE(actionAt(rig.released, 0), row.action);
        if (row.action > Pad::Right)
            QCOMPARE(count(rig.actions), 1);
        QCOMPARE(rig.pad.actionName(row.action), QString::fromLatin1(row.name));
    }
    QCOMPARE(rig.pad.actionName(Pad::LT), QStringLiteral("lt"));
    QCOMPARE(rig.pad.actionName(Pad::RT), QStringLiteral("rt"));
}

void PadTest::guideIgnored()
{
    Rig rig;
    ATTACH(rig);

    QVERIFY(rig.vp.button(SDL_GAMEPAD_BUTTON_GUIDE, true));
    QTest::qWait(quietMs);
    QCOMPARE(count(rig.actions), 0);
    QCOMPARE(count(rig.pressed), 0);
    QVERIFY(rig.vp.button(SDL_GAMEPAD_BUTTON_GUIDE, false));
    QTest::qWait(quietMs);
    QCOMPARE(count(rig.released), 0);
}

void PadTest::repeatCheck(bool stick)
{
    Rig rig;
    ATTACH(rig);

    QList<qint64> stamps;
    QList<bool> repeats;
    QElapsedTimer clock;
    clock.start();
    connect(&rig.pad, &Pad::action, &rig.pad, [&](Pad::Action a, bool repeat) {
        if (a != Pad::Down)
            return;
        stamps.append(clock.elapsed());
        repeats.append(repeat);
    });

    if (stick)
        QVERIFY(rig.vp.axis(SDL_GAMEPAD_AXIS_LEFTY, axisMax));
    else
        QVERIFY(rig.vp.button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true));
    QTRY_VERIFY_WITH_TIMEOUT(stamps.size() >= 6, waitMs);

    if (stick)
        QVERIFY(rig.vp.axis(SDL_GAMEPAD_AXIS_LEFTY, 0));
    else
        QVERIFY(rig.vp.button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false));
    QTRY_VERIFY_WITH_TIMEOUT(count(rig.released) == 1, waitMs);
    QCOMPARE(actionAt(rig.released, 0), Pad::Down);

    const int settled = static_cast<int>(stamps.size());
    QTest::qWait(quietMs);
    QCOMPARE(static_cast<int>(stamps.size()), settled);

    QCOMPARE(repeats.first(), false);
    for (int i = 1; i < repeats.size(); ++i)
        QVERIFY(repeats.at(i));

    const qint64 first = stamps.at(1) - stamps.at(0);
    QVERIFY2(first >= Pad::RepeatInitialMs - firstEarlyMs && first <= Pad::RepeatInitialMs + firstLateMs,
             qPrintable(QStringLiteral("first repeat after %1 ms").arg(first)));
    qint64 sum = 0;
    for (int i = 2; i < stamps.size(); ++i) {
        const qint64 gap = stamps.at(i) - stamps.at(i - 1);
        QVERIFY2(gap >= Pad::RepeatIntervalMs - gapSlackMs,
                 qPrintable(QStringLiteral("repeat %1 after %2 ms").arg(i).arg(gap)));
        sum += gap;
    }
    const double mean = static_cast<double>(sum) / (stamps.size() - 2);
    QVERIFY2(qAbs(mean - Pad::RepeatIntervalMs) <= meanToleranceMs,
             qPrintable(QStringLiteral("mean repeat gap %1 ms").arg(mean)));
}

void PadTest::dpadRepeat()
{
    repeatCheck(false);
}

void PadTest::stickRepeat()
{
    repeatCheck(true);
}

void PadTest::stickDeadzone()
{
    Rig rig;
    ATTACH(rig);

    QVERIFY(rig.vp.axis(SDL_GAMEPAD_AXIS_LEFTY, scaled(0.4)));
    QTest::qWait(quietMs);
    QCOMPARE(count(rig.actions), 0);

    QVERIFY(rig.vp.axis(SDL_GAMEPAD_AXIS_LEFTY, scaled(0.6)));
    QTRY_VERIFY_WITH_TIMEOUT(count(rig.actions) >= 1, waitMs);
    QCOMPARE(actionAt(rig.actions, 0), Pad::Down);
    QCOMPARE(count(rig.pressed), 1);

    QVERIFY(rig.vp.axis(SDL_GAMEPAD_AXIS_LEFTY, scaled(0.4)));
    QTest::qWait(quietMs);
    QCOMPARE(count(rig.released), 0);

    QVERIFY(rig.vp.axis(SDL_GAMEPAD_AXIS_LEFTY, scaled(0.3)));
    QTRY_VERIFY_WITH_TIMEOUT(count(rig.released) == 1, waitMs);
    QCOMPARE(actionAt(rig.released, 0), Pad::Down);

    rig.clear();
    QVERIFY(rig.vp.axis(SDL_GAMEPAD_AXIS_LEFTX, scaled(-0.8)));
    QTRY_VERIFY_WITH_TIMEOUT(count(rig.pressed) == 1, waitMs);
    QCOMPARE(actionAt(rig.pressed, 0), Pad::Left);
    QCOMPARE(actionAt(rig.actions, 0), Pad::Left);

    QVERIFY(rig.vp.axis(SDL_GAMEPAD_AXIS_LEFTX, 0));
    QTRY_VERIFY_WITH_TIMEOUT(count(rig.released) == 1, waitMs);
    QCOMPARE(actionAt(rig.released, 0), Pad::Left);
}

void PadTest::triggers()
{
    Rig rig;
    ATTACH(rig);

    QVERIFY(rig.vp.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, axisMax));
    QTRY_VERIFY_WITH_TIMEOUT(count(rig.actions) == 1, waitMs);
    QCOMPARE(actionAt(rig.actions, 0), Pad::LT);
    QCOMPARE(repeatAt(rig.actions, 0), false);
    QCOMPARE(count(rig.pressed), 1);
    QCOMPARE(actionAt(rig.pressed, 0), Pad::LT);

    QVERIFY(rig.vp.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, axisMin));
    QTRY_VERIFY_WITH_TIMEOUT(count(rig.released) == 1, waitMs);
    QCOMPARE(actionAt(rig.released, 0), Pad::LT);

    QVERIFY(rig.vp.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, axisMax));
    QTRY_VERIFY_WITH_TIMEOUT(count(rig.actions) == 2, waitMs);
    QCOMPARE(actionAt(rig.actions, 1), Pad::RT);
    QTest::qWait(Pad::RepeatInitialMs + quietMs);
    QCOMPARE(count(rig.actions), 2);

    QVERIFY(rig.vp.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, axisMin));
    QTRY_VERIFY_WITH_TIMEOUT(count(rig.released) == 2, waitMs);
    QCOMPARE(actionAt(rig.released, 1), Pad::RT);
}

void PadTest::screenOffDrops()
{
    Rig rig;
    ATTACH(rig);

    rig.pad.setScreenOff(true);
    QVERIFY(rig.pad.screenOff());

    QVERIFY(rig.vp.button(SDL_GAMEPAD_BUTTON_SOUTH, true));
    QTest::qWait(quietMs);
    QCOMPARE(count(rig.actions), 0);
    QCOMPARE(count(rig.pressed), 0);
    QVERIFY(rig.vp.button(SDL_GAMEPAD_BUTTON_SOUTH, false));
    QTest::qWait(quietMs);
    QCOMPARE(count(rig.released), 0);

    QVERIFY(rig.vp.button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true));
    QTest::qWait(quietMs);
    QCOMPARE(count(rig.actions), 0);
    QVERIFY(rig.vp.button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false));
    QTest::qWait(quietMs);
    QCOMPARE(count(rig.released), 0);

    rig.pad.setScreenOff(false);
    QVERIFY(rig.vp.button(SDL_GAMEPAD_BUTTON_SOUTH, true));
    QTRY_VERIFY_WITH_TIMEOUT(count(rig.actions) == 1, waitMs);
    QCOMPARE(actionAt(rig.actions, 0), Pad::A);
    QVERIFY(rig.vp.button(SDL_GAMEPAD_BUTTON_SOUTH, false));
    QTRY_VERIFY_WITH_TIMEOUT(count(rig.released) == 1, waitMs);
}

void PadTest::screenOffStopsPolling()
{
    Rig rig;
    ATTACH(rig);
    QVERIFY(rig.pad.polling());

    rig.pad.setScreenOff(true);
    QVERIFY(!rig.pad.polling());

    const SDL_GamepadButton buttons[] = {
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
        SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
        SDL_GAMEPAD_BUTTON_START,
    };
    for (const SDL_GamepadButton button : buttons) {
        QVERIFY(rig.vp.button(button, true));
        QTest::qWait(quietMs);
        QVERIFY(rig.vp.button(button, false));
        QTest::qWait(quietMs);
    }
    QCOMPARE(count(rig.actions), 0);
    QCOMPARE(count(rig.pressed), 0);
    QCOMPARE(count(rig.released), 0);
    QVERIFY(!rig.pad.polling());

    rig.pad.setScreenOff(false);
    QVERIFY(rig.pad.polling());
    QTest::qWait(quietMs);
    QCOMPARE(count(rig.actions), 0);
}

void PadTest::screenOffReleasesHeld()
{
    Rig rig;
    ATTACH(rig);

    QVERIFY(rig.vp.button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true));
    QTRY_VERIFY_WITH_TIMEOUT(count(rig.actions) >= 1, waitMs);
    QCOMPARE(actionAt(rig.actions, 0), Pad::Down);

    rig.pad.setScreenOff(true);
    QCOMPARE(count(rig.released), 1);
    QCOMPARE(actionAt(rig.released, 0), Pad::Down);

    const int frozen = count(rig.actions);
    QTest::qWait(Pad::RepeatInitialMs + quietMs);
    QCOMPARE(count(rig.actions), frozen);

    QVERIFY(rig.vp.button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false));
    QTest::qWait(quietMs);
    QCOMPARE(count(rig.released), 1);

    rig.pad.setScreenOff(false);
    QTest::qWait(quietMs);
    QCOMPARE(count(rig.released), 1);
    QCOMPARE(count(rig.actions), frozen);
}

void PadTest::hotplug()
{
    Rig rig;
    ATTACH(rig);
    QCOMPARE(rig.pad.gamepadCount(), 1);

    QVERIFY(rig.vp.button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true));
    QTRY_VERIFY_WITH_TIMEOUT(count(rig.pressed) == 1, waitMs);
    QCOMPARE(actionAt(rig.pressed, 0), Pad::LB);

    rig.vp.detach();
    QTRY_VERIFY_WITH_TIMEOUT(!rig.pad.connected(), waitMs);
    QCOMPARE(rig.pad.gamepadCount(), 0);
    QCOMPARE(count(rig.released), 1);
    QCOMPARE(actionAt(rig.released, 0), Pad::LB);

    rig.clear();
    QVERIFY(rig.vp.attach());
    QTRY_VERIFY_WITH_TIMEOUT(rig.pad.connected(), waitMs);
    QCOMPARE(rig.pad.gamepadCount(), 1);

    QVERIFY(rig.vp.button(SDL_GAMEPAD_BUTTON_SOUTH, true));
    QTRY_VERIFY_WITH_TIMEOUT(count(rig.actions) == 1, waitMs);
    QCOMPARE(actionAt(rig.actions, 0), Pad::A);
    QVERIFY(rig.vp.button(SDL_GAMEPAD_BUTTON_SOUTH, false));
    QTRY_VERIFY_WITH_TIMEOUT(count(rig.released) == 1, waitMs);
    QCOMPARE(actionAt(rig.released, 0), Pad::A);
}

QTEST_GUILESS_MAIN(PadTest)
#include "tst_pad.moc"
