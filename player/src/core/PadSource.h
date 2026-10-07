#pragma once

#include <QObject>
#include <QString>

namespace casdap {

class PadSource : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool screenOff READ screenOff WRITE setScreenOff NOTIFY screenOffChanged)
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

    explicit PadSource(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    virtual bool screenOff() const = 0;
    virtual void setScreenOff(bool off) = 0;
    virtual bool connected() const = 0;

    static const char *label(Action action)
    {
        static constexpr const char *labels[ActionCount] = {
            "up", "down", "left", "right", "a", "b", "x", "y", "lb", "rb", "lt", "rt", "start", "back",
        };
        if (action < 0 || action >= ActionCount)
            return "";
        return labels[action];
    }

    static bool isDirection(int action)
    {
        return action >= Up && action <= Right;
    }

    Q_INVOKABLE QString actionName(casdap::PadSource::Action action) const
    {
        if (action < 0 || action >= ActionCount)
            return {};
        return QString::fromLatin1(label(action));
    }

signals:
    void action(casdap::PadSource::Action action, bool repeat);
    void pressed(casdap::PadSource::Action action);
    void released(casdap::PadSource::Action action);
    void screenOffChanged();
    void connectedChanged();
};

}
