#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace casdap {

struct UsbStream
{
    QStringList formats;
    QList<int> rates;
};

class AudioInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pcmFormat READ pcmFormat NOTIFY pcmFormatChanged)
    Q_PROPERTY(int graphRate READ graphRate NOTIFY graphRateChanged)
    Q_PROPERTY(int hwRate READ hwRate NOTIFY hwRateChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

public:
    explicit AudioInfo(QObject *parent = nullptr);
    ~AudioInfo() override;

    QString pcmFormat() const;
    int graphRate() const;
    int hwRate() const;
    bool active() const;
    void setActive(bool active);

    static QString asoundDir();
    static QString hwParamsPath();
    static QString pwDumpPath();
    static QString parse(const QString &text);
    static QString parseState(const QString &text);
    static int parseGraphRate(const QByteArray &json);
    static UsbStream streamFor(int card);
    static QList<int> ratesFor(int card);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshGraphRate();

signals:
    void pcmFormatChanged();
    void graphRateChanged();
    void hwRateChanged();
    void activeChanged();

private:
    void onDumpFinished(int code, QProcess::ExitStatus status);
    void updateGraphRate();

    QString m_pcmFormat;
    int m_hwRate = 0;
    int m_pwRate = 0;
    int m_graphRate = 0;
    bool m_active = false;
    QTimer m_timer;
    QProcess *m_dump = nullptr;
};

}
