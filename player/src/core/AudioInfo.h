#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

namespace casdap {

class AudioInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pcmFormat READ pcmFormat NOTIFY pcmFormatChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

public:
    explicit AudioInfo(QObject *parent = nullptr);

    QString pcmFormat() const;
    bool active() const;
    void setActive(bool active);

    static QString hwParamsPath();
    static QString parse(const QString &text);

    Q_INVOKABLE void refresh();

signals:
    void pcmFormatChanged();
    void activeChanged();

private:
    QString m_pcmFormat;
    bool m_active = false;
    QTimer m_timer;
};

}
