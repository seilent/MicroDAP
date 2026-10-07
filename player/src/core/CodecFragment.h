#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace casdap {

class BluezModel;

class CodecFragment : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString codec READ codec NOTIFY codecChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged)
    Q_PROPERTY(bool restarting READ restarting NOTIFY restartingChanged)

public:
    explicit CodecFragment(QObject *parent = nullptr);
    ~CodecFragment() override;

    static QStringList codecs();
    static QString confDir();
    static QString confPath();
    static QString systemctlPath();
    static QStringList codecsFor(const QString &codec);
    static QString fragmentText(const QString &codec);

    void setBluez(BluezModel *bluez);
    BluezModel *bluez() const;

    QString codec() const;
    bool pending() const;
    bool restarting() const;

    Q_INVOKABLE bool apply(const QString &codec);

signals:
    void codecChanged();
    void pendingChanged();
    void restartingChanged();
    void restartFinished(bool ok);

private:
    void onTransportActiveChanged();
    bool writeFragment(const QString &text);
    bool removeFragment();
    void restart();
    void dropProcess();
    void setPending(bool pending);

    BluezModel *m_bluez = nullptr;
    QString m_codec;
    bool m_pending = false;
    QProcess *m_process = nullptr;
};

}
