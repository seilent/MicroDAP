#pragma once

#include "OutputPorts.h"

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>

class QSocketNotifier;

namespace microdap {

class OutputControl : public QObject, public HeldOutputPort
{
    Q_OBJECT
    Q_PROPERTY(QString output READ output NOTIFY outputChanged)
    Q_PROPERTY(QString activeOutput READ activeOutput NOTIFY activeOutputChanged)
    Q_PROPERTY(bool jackPresent READ jackPresent NOTIFY jackPresentChanged)
    Q_PROPERTY(QString usbSink READ usbSink NOTIFY usbSinkChanged)
    Q_PROPERTY(bool usbPresent READ usbPresent NOTIFY usbSinkChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

public:
    explicit OutputControl(QObject *parent = nullptr);
    ~OutputControl() override;

    static QStringList outputs();
    static QString speakerSink();
    static QString headphonesSink();
    static QString usbPrefix();
    static QString btPrefix();
    static QString pactlPath();
    static QString amixerPath();
    static QStringList parseSinks(const QString &text);
    static QString usbSinkIn(const QStringList &sinks);
    static bool parseJack(const QString &text);
    static QString jackDeviceName();
    static QString outputOf(const QString &sink);

    QString output() const;
    QString activeOutput() const;
    bool jackPresent() const;
    bool jackEvents() const;
    QString usbSink() const;
    bool usbPresent() const;
    bool busy() const;
    bool active() const;
    void setActive(bool active);
    QString defaultSink() const;
    HeldState held() const;

    void setHeld(HeldState state) override;
    void watchCard(int index, const QString &id) override;
    void unwatchCard() override;
    quint64 readsStarted() const override;

    Q_INVOKABLE void apply(const QString &output);
    Q_INVOKABLE void refresh() override;

signals:
    void outputChanged();
    void activeOutputChanged();
    void jackPresentChanged();
    void usbSinkChanged();
    void busyChanged();
    void activeChanged();
    void applied(const QString &output, bool ok);
    void firstApplied(bool ok);
    void cardGone(int index);
    void sinksRead(const QString &usbSink, quint64 read);
    void routeLost(const QString &output);

private:
    using Handler = std::function<void(bool ok, const QByteArray &out)>;

    void run(QProcess *&slot, const QString &program, const QStringList &args, Handler handler);
    void drop(QProcess *&slot);
    void readJack(Handler done);
    void readSinks(Handler done);
    void readDefault(Handler done);
    void readAll(std::function<void()> done);
    void resolve();
    void finish(bool ok);
    void setJackPresent(bool present);
    void setUsbSink(const QString &sink);
    void setActiveOutput(const QString &output);
    void updateBusy();
    void checkWatchedCard();
    void openJack();
    void closeJack();
    void drainJack();
    bool readJackSwitch(bool &present) const;

    QString m_output;
    QString m_activeOutput;
    bool m_jackPresent = false;
    QString m_usbSink;
    QString m_defaultSink;
    QStringList m_sinks;
    bool m_busy = false;
    bool m_applying = false;
    bool m_reading = false;
    bool m_reread = false;
    bool m_everApplied = false;
    bool m_resolvedOnce = false;
    bool m_active = false;
    HeldState m_held = HeldState::None;
    int m_watchIndex = -1;
    QString m_watchId;
    bool m_watchGone = false;
    quint64 m_readsStarted = 0;
    QTimer m_pollTimer;
    QTimer m_settleTimer;
    QProcess *m_jack = nullptr;
    QProcess *m_sinkList = nullptr;
    QProcess *m_default = nullptr;
    QString m_devRoot;
    QString m_sysRoot;
    int m_jackFd = -1;
    QSocketNotifier *m_jackNotifier = nullptr;
    bool m_jackLogged = false;
};

}
