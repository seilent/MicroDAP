#pragma once

#include "Intercept.h"
#include "PadSource.h"

#include <QDBusConnection>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <array>

namespace microdap {

class DbusPad : public PadSource
{
    Q_OBJECT

public:
    static constexpr double PressThreshold = 0.5;

    DbusPad(const QDBusConnection &session, const QDBusConnection &inputPlumber, QObject *parent = nullptr);
    ~DbusPad() override;

    static int actionFor(const QString &name);

    Intercept *intercept() const;
    bool shown() const;
    void setShown(bool shown);
    bool holding() const;
    QStringList dropped() const;

    bool screenOff() const override;
    void setScreenOff(bool off) override;
    bool connected() const override;

private slots:
    void onInputEvent(const QString &name, double value);

private:
    void repeatTick();
    void startRepeat(Action direction);
    void stopRepeat();
    void press(Action a);
    void release(Action a);
    void emitAction(Action a, bool repeat);
    bool allowed(Action a) const;
    void releaseAll();

    QDBusConnection m_bus;
    Intercept *m_intercept;
    bool m_log = false;
    bool m_screenOff = false;
    QTimer m_repeat;
    int m_repeatAction = -1;
    std::array<bool, ActionCount> m_down{};
    QSet<QString> m_dropped;
};

}
