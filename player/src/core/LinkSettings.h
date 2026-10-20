#pragma once

#include "SettingsRows.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <cstdint>

namespace microdap {

class Settings;

struct LinkSettingRow
{
    const SettingsRows::Row *row = nullptr;
    QString key;
    QString label;
    QString pivot;
    QString setting;
    uint8_t kind = 0;
    QStringList values;
    QStringList words;
    int min = 0;
    int max = 0;
    int step = 0;
    bool readOnly = false;
    bool restart = false;
    bool boardOnly = false;
};

class LinkSettings
{
public:
    explicit LinkSettings(Settings *settings, QString version = {}, QString role = {});

    QList<LinkSettingRow> rows() const;
    QString text() const;
    QString wire(const LinkSettingRow &row) const;
    uint8_t set(const QString &text, QStringList &kept, QStringList &refused) const;

private:
    bool readOnlyRule(const QString &rule) const;
    bool shown(const QString &rule) const;
    QVariant value(const LinkSettingRow &row, const QString &wire, bool &ok) const;

    Settings *m_settings;
    QString m_version;
    bool m_headphones;
};

}
