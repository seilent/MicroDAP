#include "DbusPad.h"

#include <QDebug>
#include <QtGlobal>

#include <cstdio>

namespace casdap {

namespace {

struct NameRow
{
    const char *name;
    int action;
};

constexpr NameRow nameTable[] = {
    {"ui_up", PadSource::Up},
    {"ui_down", PadSource::Down},
    {"ui_left", PadSource::Left},
    {"ui_right", PadSource::Right},
    {"ui_accept", PadSource::A},
    {"ui_back", PadSource::B},
    {"ui_context", PadSource::X},
    {"ui_action", PadSource::Y},
    {"ui_l1", PadSource::LB},
    {"ui_r1", PadSource::RB},
    {"ui_l2", PadSource::LT},
    {"ui_r2", PadSource::RT},
    {"ui_option", PadSource::Start},
    {"ui_select", PadSource::Back},
};

}

DbusPad::DbusPad(const QDBusConnection &bus, QObject *parent)
    : PadSource(parent)
    , m_bus(bus)
    , m_intercept(new Intercept(bus, this))
    , m_log(qEnvironmentVariableIntValue("CASDAP_PAD_LOG") == 1)
{
    m_repeat.setTimerType(Qt::PreciseTimer);
    connect(&m_repeat, &QTimer::timeout, this, &DbusPad::repeatTick);
    connect(m_intercept, &Intercept::targetsChanged, this, &PadSource::connectedChanged);
    if (!m_bus.isConnected()) {
        qWarning() << "casdap dbuspad: bus not connected";
        return;
    }
    if (!m_bus.connect(Intercept::service(), QString(), Intercept::dbusDeviceInterface(),
                       QStringLiteral("InputEvent"), this, SLOT(onInputEvent(QString,double)))) {
        qWarning() << "casdap dbuspad: cannot subscribe to InputEvent" << m_bus.lastError().message();
    }
}

DbusPad::~DbusPad()
{
    m_repeat.stop();
}

int DbusPad::actionFor(const QString &name)
{
    for (const NameRow &row : nameTable) {
        if (name == QLatin1StringView(row.name))
            return row.action;
    }
    return -1;
}

Intercept *DbusPad::intercept() const
{
    return m_intercept;
}

bool DbusPad::shown() const
{
    return m_intercept->shown();
}

void DbusPad::setShown(bool shown)
{
    if (!shown && m_intercept->shown())
        releaseAll();
    m_intercept->setShown(shown);
}

QStringList DbusPad::dropped() const
{
    QStringList list = m_dropped.values();
    list.sort();
    return list;
}

bool DbusPad::screenOff() const
{
    return m_screenOff;
}

void DbusPad::setScreenOff(bool off)
{
    if (m_screenOff == off)
        return;
    m_screenOff = off;
    if (off)
        releaseAll();
    emit screenOffChanged();
}

bool DbusPad::connected() const
{
    return m_intercept->hasTargets();
}

void DbusPad::onInputEvent(const QString &name, double value)
{
    const int mapped = actionFor(name);
    if (mapped < 0) {
        if (!m_dropped.contains(name)) {
            m_dropped.insert(name);
            qInfo() << "casdap dbuspad: dropped" << name;
        }
        return;
    }
    const Action a = static_cast<Action>(mapped);
    const bool on = value >= PressThreshold;
    if (on == m_down[a])
        return;
    if (on) {
        press(a);
        if (isDirection(a) && m_down[a])
            startRepeat(a);
        return;
    }
    release(a);
    if (m_repeatAction != a)
        return;
    stopRepeat();
    for (int i = Up; i <= Right; ++i) {
        if (m_down[i]) {
            startRepeat(static_cast<Action>(i));
            return;
        }
    }
}

void DbusPad::repeatTick()
{
    if (m_repeatAction < 0) {
        m_repeat.stop();
        return;
    }
    emitAction(static_cast<Action>(m_repeatAction), true);
    if (m_repeat.interval() != RepeatIntervalMs)
        m_repeat.start(RepeatIntervalMs);
}

void DbusPad::startRepeat(Action direction)
{
    m_repeatAction = direction;
    m_repeat.start(RepeatInitialMs);
}

void DbusPad::stopRepeat()
{
    m_repeat.stop();
    m_repeatAction = -1;
}

void DbusPad::press(Action a)
{
    if (m_down[a] || !allowed(a))
        return;
    m_down[a] = true;
    emit pressed(a);
    emitAction(a, false);
}

void DbusPad::release(Action a)
{
    if (!m_down[a])
        return;
    m_down[a] = false;
    emit released(a);
}

void DbusPad::emitAction(Action a, bool repeat)
{
    if (!allowed(a))
        return;
    if (m_log)
        std::fprintf(stderr, "pad: %s%s\n", label(a), repeat ? " repeat" : "");
    emit action(a, repeat);
}

bool DbusPad::allowed(Action) const
{
    return !m_screenOff;
}

void DbusPad::releaseAll()
{
    stopRepeat();
    for (int i = 0; i < ActionCount; ++i) {
        if (m_down[i])
            release(static_cast<Action>(i));
    }
}

}
