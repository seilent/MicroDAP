#pragma once

#include <QByteArray>
#include <QEasingCurve>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantAnimation>
#include <QWindow>

#include <functional>

namespace casdap {

class DbusPad;
class Player;
class SleepInhibitor;
class State;

class OverlayBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)
    Q_PROPERTY(bool mapped READ mapped NOTIFY mappedChanged)
    Q_PROPERTY(qreal opacity READ opacity NOTIFY opacityChanged)
    Q_PROPERTY(bool fading READ fading NOTIFY fadingChanged)
    Q_PROPERTY(bool sleeping READ sleeping NOTIFY sleepingChanged)

public:
    static constexpr int ShowMs = 200;
    static constexpr int HideMs = 150;
    static constexpr int WatchdogMs = 7000;

    using WindowPrepare = std::function<void(QWindow *)>;

    OverlayBridge(DbusPad *pad, Player *player, QObject *parent = nullptr);
    ~OverlayBridge() override;

    static QByteArray notifySocket();
    static bool notify(const QByteArray &state);

    DbusPad *pad() const;
    Player *player() const;
    QWindow *window() const;
    void setWindow(QWindow *window, WindowPrepare prepare = {});
    QWindow *toastWindow() const;
    void setToastWindow(QWindow *window, WindowPrepare prepare = {});
    State *state() const;
    void setState(State *state);
    SleepInhibitor *sleep() const;
    bool ready() const;
    bool shown() const;
    bool mapped() const;
    qreal opacity() const;
    bool fading() const;
    bool sleeping() const;
    int mapCount() const;
    int toastMapCount() const;
    bool watchdogActive() const;

    Q_INVOKABLE void show();
    Q_INVOKABLE void hide();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void closePlayer();
    Q_INVOKABLE bool prepareToast();
    Q_INVOKABLE void releaseToast();

signals:
    void shownChanged();
    void mappedChanged();
    void opacityChanged();
    void fadingChanged();
    void sleepingChanged();

private:
    void fadeTo(qreal to, int ms, QEasingCurve::Type curve);
    void onFadeFinished();
    void onFadeState();
    void onPrepareForSleep(bool start);
    void setOpacity(qreal value);
    void setMapped(bool mapped);
    void setSleeping(bool sleeping);
    void mapWindow();
    void destroyWindow();
    void notifyReady();
    void ping();

    DbusPad *m_pad;
    Player *m_player;
    QPointer<QWindow> m_window;
    QPointer<QWindow> m_toast;
    WindowPrepare m_prepare;
    WindowPrepare m_prepareToast;
    State *m_state = nullptr;
    SleepInhibitor *m_sleep;
    QVariantAnimation m_fade;
    QTimer m_watchdog;
    bool m_ready = false;
    bool m_shown = false;
    bool m_mapped = false;
    bool m_fading = false;
    bool m_sleeping = false;
    bool m_pingFailed = false;
    qreal m_opacity = 0.0;
    int m_mapCount = 0;
    int m_toastMapCount = 0;
};

}
