#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

class QScreen;

namespace microdap {

class OutputWatch : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool internal READ internal NOTIFY internalChanged)

public:
    static constexpr int pollMs = 2000;
    static constexpr int settleShortMs = 250;
    static constexpr int settleLongMs = 1000;

    explicit OutputWatch(QObject *parent = nullptr);
    explicit OutputWatch(const QString &root, QObject *parent = nullptr);

    static QString defaultRoot();
    QString root() const;
    bool internal() const;

    Q_INVOKABLE void refresh();
    void setScreenOff(bool off);
    void scheduleReads();

signals:
    void internalChanged();

private:
    void watchScreen(QScreen *screen);
    bool readInternal() const;

    QString m_root;
    bool m_internal = true;
    bool m_screenOff = false;
    QTimer m_poll;
    QTimer m_settleShort;
    QTimer m_settleLong;
};

}
