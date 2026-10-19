#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace microdap {

class SettingsRows
{
public:
    struct Row
    {
        QString key;
        QString title;
        QString pivot;
        QString type;
        QString kind;
        QString setting;
        QVariantList values;
        double min = 0;
        double max = 0;
        double step = 0;
        QString action;
        QString readOnly;
        QString shown;
        bool bounded = false;
        bool draft = false;
        QString reason;
        QString linkKey;
    };

    static QStringList pivots();
    static QStringList types();
    static QStringList kinds();
    static const QList<Row> &all();
    static const Row *find(const QString &key);
    static QVariantMap map(const Row &row);
    static QVariantList maps();
};

}
