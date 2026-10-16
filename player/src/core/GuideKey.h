#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

class QSocketNotifier;

namespace microdap {

class GuideKey : public QObject
{
    Q_OBJECT

public:
    static constexpr qint64 holdMs = 1000;
    static constexpr qint64 pocketHoldMs = 500;
    static constexpr qint64 doubleMs = 350;
    static constexpr qint64 chordMs = 500;
    static constexpr int chordGraceMs = 40;
    static constexpr int retryMs = 2000;
    static constexpr qint64 staleMs = 60000;
    static constexpr int buzzMs = 15;
    static constexpr int buzzGapMs = 50;

    explicit GuideKey(QObject *parent = nullptr);
    ~GuideKey() override;

    static QString deviceName();
    static qint64 nowMs();

    bool isOpen() const;
    bool screenOff() const;
    void setScreenOff(bool off);

    Q_INVOKABLE bool take();

signals:
    void playPause();
    void next();
    void previous();
    void switchPrompt();
    void buzz(const QList<int> &pattern);

private:
    QString find() const;
    void open();
    void closeNode();
    void drain();
    void onPress(qint64 at);
    void onRelease(qint64 at);
    void onHoldTimer();
    void resolveTap();
    void flushPending();
    qint64 anchor(qint64 eventMs) const;

    QString m_devRoot;
    QString m_sysRoot;
    QString m_name;
    int m_fd = -1;
    QSocketNotifier *m_notifier = nullptr;
    bool m_logged = false;
    bool m_screenOff = false;
    QTimer m_retry;
    QTimer m_hold;
    QTimer m_pending;
    QTimer m_grace;
    bool m_down = false;
    bool m_pocket = false;
    bool m_chord = false;
    bool m_tapDue = false;
    bool m_tapPending = false;
    bool m_holdFired = false;
    qint64 m_pressAt = 0;
    qint64 m_releaseAt = 0;
};

}
