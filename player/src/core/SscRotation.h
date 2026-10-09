#pragma once

#include "SscWire.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

#include <optional>

namespace casdap {

class SleepInhibitor;
class SscLink;

class SscRotation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)
    Q_PROPERTY(int turn READ turn NOTIFY turnChanged)
    Q_PROPERTY(QString raw READ raw NOTIFY rawChanged)
    Q_PROPERTY(QString settledRaw READ settledRaw NOTIFY settledRawChanged)

public:
    static constexpr int settleMs = 500;
    static constexpr int reclaimMs = 60000;
    static constexpr int retryMs = 3000;
    static constexpr int maxRetryMs = 60000;
    static constexpr int sleepReleaseMs = 500;

    explicit SscRotation(QObject *parent = nullptr);
    SscRotation(SscLink *link, SleepInhibitor *sleep = nullptr, QObject *parent = nullptr);
    ~SscRotation() override;

    static int defaultReclaimMs();
    static int defaultRetryMs();
    static int turnOf(int value, int sensorTurn);

    bool supported() const;
    int turn() const;
    QString raw() const;
    QString settledRaw() const;
    bool wanted() const;
    bool claimed() const;
    int sensorTurn() const;

    Q_INVOKABLE void setWanted(bool wanted, bool keepTurn = false);

signals:
    void supportedChanged();
    void turnChanged();
    void rawChanged();
    void settledRawChanged();

private:
    enum class Request { Suid, Enable, Flush, Disable };

    void watchSleep();
    void onReceived(quint32 node, quint32 port, const QByteArray &bytes);
    void onLinkFailed();
    void onReopen();
    void onPrepareForSleep(bool start);
    void handleControl(const QByteArray &bytes);
    void handleResponse(const ssc::Message &message);
    void handleReport(const ssc::Report &report);
    void handleSuid(const QByteArray &payload);
    void handleValue(const QByteArray &payload);
    void sendLookup();
    quint16 send(Request kind, const QByteArray &request);
    void requestSuid();
    void scheduleRetry();
    void maybeEnable();
    quint16 disable();
    void dropServer();
    void releaseSleep();
    void lose();
    void take(const QString &raw);
    void settle();
    void updateSupported();
    void setTurn(int turn);
    void setRaw(const QString &raw);
    void setSettledRaw(const QString &raw);

    SscLink *m_link = nullptr;
    SleepInhibitor *m_sleep = nullptr;
    QTimer m_settle;
    QTimer m_hold;
    QTimer m_retry;
    QTimer m_enableRetry;
    QTimer m_reopen;
    QTimer m_sleepRelease;
    QHash<quint16, Request> m_requests;
    std::optional<ssc::Control> m_server;
    std::optional<ssc::Suid> m_suid;
    int m_retryBase = retryMs;
    int m_retryDelay = retryMs;
    int m_sensorTurn = 1;
    quint16 m_txn = 0;
    quint16 m_enableTxn = 0;
    quint16 m_sleepTxn = 0;
    bool m_holding = false;
    bool m_supported = false;
    bool m_wanted = false;
    bool m_claimed = false;
    bool m_asleep = false;
    int m_turn = -1;
    int m_last = -1;
    int m_pending = -1;
    QString m_raw;
    QString m_pendingRaw;
    QString m_settledRaw;
};

}
