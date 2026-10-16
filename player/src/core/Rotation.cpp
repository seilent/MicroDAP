#include "Rotation.h"

#include <microset/client.h>

#include <QDebug>

namespace microdap {

Rotation::Rotation(MicroSet::Client *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_sensorTurn(defaultSensorTurn())
{
    if (!m_client)
        return;
    connect(m_client, &MicroSet::Client::changed, this, &Rotation::onChanged);
    m_client->watch(key());
    MicroSet::Error error;
    const QVariant value = m_client->get(key(), &error);
    setSupported(error.ok());
    if (error.ok())
        setRaw(value.toString());
}

Rotation::~Rotation()
{
    if (m_client && m_wanted)
        m_client->unsubscribe(key());
}

QString Rotation::key()
{
    return QStringLiteral("display.rotation");
}

int Rotation::defaultSensorTurn()
{
    return qEnvironmentVariableIntValue("MICRODAP_SENSOR_TURN") == 3 ? 3 : 1;
}

int Rotation::turnOf(const QString &rotation, int sensorTurn)
{
    if (rotation == QLatin1StringView("portrait"))
        return 0;
    if (rotation == QLatin1StringView("landscape"))
        return sensorTurn;
    if (rotation == QLatin1StringView("portrait-flipped"))
        return 2;
    if (rotation == QLatin1StringView("landscape-flipped"))
        return (sensorTurn + 2) % 4;
    return -1;
}

bool Rotation::supported() const
{
    return m_supported;
}

int Rotation::turn() const
{
    return m_turn;
}

QString Rotation::raw() const
{
    return m_raw;
}

QString Rotation::settledRaw() const
{
    return m_settledRaw;
}

bool Rotation::wanted() const
{
    return m_wanted;
}

void Rotation::setWanted(bool wanted, bool keepTurn)
{
    if (!wanted && !keepTurn)
        setSettledRaw(QString());
    if (m_wanted == wanted)
        return;
    m_wanted = wanted;
    if (!m_client)
        return;
    if (!wanted) {
        m_client->unsubscribe(key());
        setTurn(-1);
        return;
    }
    m_client->subscribe(key());
    MicroSet::Error error;
    const QVariant value = m_client->get(key(), &error);
    if (error.ok())
        setRaw(value.toString());
    take(m_raw);
}

void Rotation::onChanged(const QString &changed, const QVariant &value)
{
    if (changed != key())
        return;
    setSupported(true);
    setRaw(value.toString());
    if (m_wanted)
        take(m_raw);
}

void Rotation::take(const QString &rotation)
{
    const int turn = turnOf(rotation, m_sensorTurn);
    if (turn < 0)
        return;
    setSettledRaw(rotation);
    setTurn(turn);
}

void Rotation::setSupported(bool supported)
{
    if (m_supported == supported)
        return;
    m_supported = supported;
    qInfo() << "microdap rotation: supported" << supported;
    emit supportedChanged();
}

void Rotation::setTurn(int turn)
{
    if (m_turn == turn)
        return;
    m_turn = turn;
    emit turnChanged();
}

void Rotation::setRaw(const QString &raw)
{
    if (m_raw == raw)
        return;
    m_raw = raw;
    emit rawChanged();
}

void Rotation::setSettledRaw(const QString &raw)
{
    if (m_settledRaw == raw)
        return;
    m_settledRaw = raw;
    emit settledRawChanged();
}

}
