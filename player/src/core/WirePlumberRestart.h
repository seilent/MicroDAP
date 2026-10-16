#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

namespace microdap {

class BluezModel;

class WirePlumberRestart : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool restarting READ restarting NOTIFY restartingChanged)

public:
    explicit WirePlumberRestart(QObject *parent = nullptr);
    ~WirePlumberRestart() override;

    static QString systemctlPath();

    void setBluez(BluezModel *bluez);
    BluezModel *bluez() const;
    bool restarting() const;

    bool restartForNet(const QString &reason);

signals:
    void restartingChanged();
    void restartFinished(bool ok);

private:
    void finish(bool ok);
    void dropProcess();

    BluezModel *m_bluez = nullptr;
    QProcess *m_process = nullptr;
};

}
