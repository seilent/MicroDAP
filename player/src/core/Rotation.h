#pragma once

#include <QObject>
#include <QString>
#include <QVariant>

namespace MicroSet {
class Client;
}

namespace microdap {

class Rotation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)
    Q_PROPERTY(int turn READ turn NOTIFY turnChanged)
    Q_PROPERTY(QString raw READ raw NOTIFY rawChanged)
    Q_PROPERTY(QString settledRaw READ settledRaw NOTIFY settledRawChanged)

public:
    explicit Rotation(MicroSet::Client *client, QObject *parent = nullptr);
    ~Rotation() override;

    static QString key();
    static int defaultSensorTurn();
    static int turnOf(const QString &rotation, int sensorTurn);

    bool supported() const;
    int turn() const;
    QString raw() const;
    QString settledRaw() const;
    bool wanted() const;

    Q_INVOKABLE void setWanted(bool wanted, bool keepTurn = false);

signals:
    void supportedChanged();
    void turnChanged();
    void rawChanged();
    void settledRawChanged();

private:
    void onChanged(const QString &key, const QVariant &value);
    void take(const QString &rotation);
    void setSupported(bool supported);
    void setTurn(int turn);
    void setRaw(const QString &raw);
    void setSettledRaw(const QString &raw);

    MicroSet::Client *m_client;
    int m_sensorTurn;
    bool m_supported = false;
    bool m_wanted = false;
    int m_turn = -1;
    QString m_raw;
    QString m_settledRaw;
};

}
