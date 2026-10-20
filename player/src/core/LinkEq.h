#pragma once

#include "Eq.h"
#include "LinkProto.h"

#include <QByteArray>
#include <QSet>
#include <QList>
#include <QString>
#include <QStringList>

#include <array>
#include <cstdint>
#include <vector>

namespace microdap {

struct LinkEqName
{
    QString key;
    QString name;
    bool preset = false;
};

struct LinkEqPush
{
    QString key;
    QString graphicKey;
    QList<LinkEqName> names;
    QList<LinkEqName> graphicNames;
    fd::LinkWriter ints;
    fd::LinkWriter gains;
    bool hasGains = false;
    bool graphic = false;

    QByteArray sig() const;
};

class LinkEq
{
public:
    using Gains = std::array<double, EqProfile::bandCount>;

    static QStringList slotKeys();
    static QString label(const QString &slot);
    static std::vector<uint8_t> bandTypes();
    static LinkEqPush push(const EqControl &eq, const QString &output);
    static uint8_t set(EqControl &eq, const QString &output, const fd::LinkBody &body);
    static uint8_t preset(EqControl &eq, const QString &output, const fd::LinkBody &body, QString &key);
    static bool profileOf(const fd::LinkField &field, EqProfile &out);
    static bool gainsOf(const fd::LinkField &field, Gains &out);
    static void profileWire(const EqProfile &profile, fd::LinkWriter &w);
    static void gainsWire(const Gains &gains, fd::LinkWriter &w);
    static int graphicGrid(int band);
};

}
