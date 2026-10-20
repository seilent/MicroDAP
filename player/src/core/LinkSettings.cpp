#include "LinkSettings.h"

#include "LinkProto.h"
#include "Settings.h"

#include <cmath>

namespace microdap {

namespace {

namespace kind = fd::proto::val::ROW_KIND;

bool boolRow(const SettingsRows::Row &r)
{
    return !r.values.isEmpty() && r.values.constFirst().typeId() == QMetaType::Bool;
}

QString minutes(int seconds)
{
    return seconds < 60 ? QStringLiteral("%1 s").arg(seconds) : QStringLiteral("%1 min").arg(qRound(seconds / 60.0));
}

QString offOr(int value, const QString &unit)
{
    return value <= 0 ? QStringLiteral("off") : QStringLiteral("%1 %2").arg(value).arg(unit);
}

QString word(const QString &key, const QString &v)
{
    const int n = v.toInt();
    if (key == QLatin1StringView("spkGain")) {
        const int db = n - 84;
        return (db > 0 ? QStringLiteral("+%1").arg(db) : QString::number(db)) + QStringLiteral(" db");
    }
    if (key == QLatin1StringView("volumeStep") || key == QLatin1StringView("brightness"))
        return v + QLatin1Char('%');
    if (key == QLatin1StringView("powerOnVolume"))
        return n <= 0 ? QStringLiteral("last") : v;
    if (key == QLatin1StringView("crossfade"))
        return n <= 0 ? QStringLiteral("off") : v + QStringLiteral(" s");
    if (key == QLatin1StringView("crossfeed"))
        return v == QLatin1StringView("chuMoy") ? QStringLiteral("chu moy")
               : v == QLatin1StringView("janMeier") ? QStringLiteral("jan meier")
                                                    : v;
    if (key == QLatin1StringView("level"))
        return v == QLatin1StringView("bitperfect") ? QStringLiteral("on") : v;
    if (key == QLatin1StringView("orientation"))
        return QString(v).replace(QLatin1Char('-'), QLatin1Char(' '));
    if (key == QLatin1StringView("codec"))
        return QString(v).replace(QLatin1Char('_'), QLatin1Char(' '));
    if (key == QLatin1StringView("saver"))
        return minutes(n);
    if (key == QLatin1StringView("screenOff"))
        return offOr(qRound(n / 60.0), QStringLiteral("min"));
    if (key == QLatin1StringView("suspend"))
        return offOr(n, QStringLiteral("min"));
    if (key == QLatin1StringView("wifi"))
        return v == QLatin1StringView("on") ? QStringLiteral("off") : QStringLiteral("keep");
    return v;
}

bool whole(const QString &v, int &out)
{
    if (v.isEmpty())
        return false;
    for (qsizetype i = 0; i < v.size(); ++i) {
        const QChar c = v.at(i);
        if (!(c >= QLatin1Char('0') && c <= QLatin1Char('9')) && !(i == 0 && c == QLatin1Char('-') && v.size() > 1))
            return false;
    }
    bool ok = false;
    out = v.toInt(&ok);
    return ok;
}

bool clean(const QString &line)
{
    for (QChar c : line) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7f)
            return false;
    }
    return line.count(QLatin1Char('=')) == 1;
}

}

LinkSettings::LinkSettings(Settings *settings, QString version, QString role)
    : m_settings(settings)
    , m_version(std::move(version))
    , m_headphones(role == QLatin1StringView("headphones"))
{
}

bool LinkSettings::readOnlyRule(const QString &rule) const
{
    if (rule == QLatin1StringView("bitPerfect"))
        return m_settings->outputLevel() != QLatin1StringView("off");
    if (rule == QLatin1StringView("usbActive"))
        return m_settings->outputSlot() == QLatin1StringView("usb");
    if (rule == QLatin1StringView("slot"))
        return m_settings->outputSlot().isEmpty();
    if (rule == QLatin1StringView("crossfeed"))
        return !Settings::crossfeedSlots().contains(m_settings->outputSlot());
    if (rule == QLatin1StringView("codecRole"))
        return !m_headphones;
    if (rule == QLatin1StringView("brightness"))
        return m_settings->brightnessSteps().isEmpty();
    return false;
}

bool LinkSettings::shown(const QString &rule) const
{
    if (rule == QLatin1StringView("overlay"))
        return false;
    if (rule == QLatin1StringView("companion"))
        return m_settings->companion();
    return true;
}

QList<LinkSettingRow> LinkSettings::rows() const
{
    QList<LinkSettingRow> out;
    for (const QString &pivot : SettingsRows::pivots()) {
        for (const SettingsRows::Row &r : SettingsRows::all()) {
            if (r.pivot != pivot || !shown(r.shown))
                continue;
            LinkSettingRow lr;
            lr.row = &r;
            lr.key = r.linkKey;
            lr.label = r.title.toUpper();
            lr.pivot = r.pivot;
            const bool headphones = m_headphones && !r.headphonesSetting.isEmpty();
            lr.setting = headphones ? r.headphonesSetting : r.setting;
            lr.readOnly = readOnlyRule(r.readOnly);
            QStringList values;
            if (r.type == QLatin1StringView("toggle") || boolRow(r)) {
                lr.kind = kind::toggle;
                values = {QStringLiteral("off"), QStringLiteral("on")};
            } else if (r.type == QLatin1StringView("choice")) {
                lr.kind = kind::choice;
                const QVariantList list = r.key == QLatin1StringView("boot") ? QVariant(m_settings->bootSessions()).toList()
                                          : headphones                       ? r.headphonesValues
                                                                             : r.values;
                for (const QVariant &v : list)
                    values.push_back(v.toString());
            } else if (r.type == QLatin1StringView("steps")) {
                lr.kind = kind::steps;
                const bool brightness = r.key == QLatin1StringView("brightness");
                for (const QVariant &v : brightness ? m_settings->brightnessSteps() : r.values)
                    values.push_back(QString::number(r.key == QLatin1StringView("crossfade") ? v.toInt() / 1000
                                                     : brightness                             ? qRound(v.toInt() / 100.0)
                                                                                              : v.toInt()));
            } else if (r.type == QLatin1StringView("range") && std::floor(r.step) != r.step) {
                lr.kind = kind::choice;
                for (double s = r.min; s <= r.max + 1e-6; s += r.step)
                    values.push_back(QString::number(std::round(s * 10.0) / 10.0, 'f', 1));
            } else if (r.type == QLatin1StringView("range")) {
                lr.kind = kind::range;
                lr.min = int(r.min);
                lr.max = int(r.max);
                lr.step = int(r.step);
            } else if (r.key == QLatin1StringView("about")) {
                lr.kind = kind::info;
                lr.readOnly = true;
                lr.words = {m_version.toUpper()};
            } else {
                lr.kind = kind::action;
                lr.readOnly = r.key != QLatin1StringView("rescan");
            }
            for (const QString &v : std::as_const(values))
                lr.words.push_back(word(r.key, v).toUpper());
            lr.values = values;
            if (r.key == QLatin1StringView("phoneApp")) {
                lr.readOnly = true;
                lr.boardOnly = true;
            }
            lr.restart = r.key == QLatin1StringView("codec") && !headphones;
            out.push_back(lr);
        }
    }
    return out;
}

QString LinkSettings::wire(const LinkSettingRow &row) const
{
    const QVariant v = m_settings->property(row.setting.toLatin1().constData());
    if (row.kind == kind::toggle)
        return v.toBool() ? QStringLiteral("on") : QStringLiteral("off");
    if (row.row->key == QLatin1StringView("crossfade"))
        return QString::number(v.toInt() / 1000);
    if (row.row->key == QLatin1StringView("brightness"))
        return QString::number(qRound(v.toInt() / 100.0));
    if (row.row->key == QLatin1StringView("scale"))
        return QString::number(std::round(v.toDouble() * 10.0) / 10.0, 'f', 1);
    return v.toString();
}

QString LinkSettings::text() const
{
    QString out;
    for (const LinkSettingRow &row : rows()) {
        if (row.kind == kind::choice || row.kind == kind::steps || row.kind == kind::range || row.kind == kind::toggle)
            out += row.key + QLatin1Char('=') + wire(row) + QLatin1Char('\n');
    }
    return out;
}

QVariant LinkSettings::value(const LinkSettingRow &row, const QString &wire, bool &ok) const
{
    ok = false;
    int n = 0;
    switch (row.kind) {
    case kind::toggle:
        ok = row.values.contains(wire);
        return wire == QLatin1StringView("on");
    case kind::choice:
        ok = row.values.contains(wire);
        if (row.row->key == QLatin1StringView("scale"))
            return wire.toDouble();
        return wire;
    case kind::steps: {
        if (!whole(wire, n) || row.values.isEmpty())
            return {};
        int best = row.values.constFirst().toInt();
        for (const QString &v : row.values) {
            if (std::abs(v.toInt() - n) < std::abs(best - n))
                best = v.toInt();
        }
        ok = true;
        if (row.row->key == QLatin1StringView("brightness"))
            return best * 100;
        return row.row->key == QLatin1StringView("crossfade") ? best * 1000 : best;
    }
    case kind::range:
        if (!whole(wire, n))
            return {};
        ok = true;
        return qBound(row.min, n, row.max);
    default:
        return {};
    }
}

uint8_t LinkSettings::set(const QString &text, QStringList &kept, QStringList &refused) const
{
    const QList<LinkSettingRow> all = rows();
    QList<std::pair<const LinkSettingRow *, QVariant>> writes;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (line.isEmpty())
            continue;
        const QString key = line.section(QLatin1Char('='), 0, 0);
        const QString wireValue = line.section(QLatin1Char('='), 1);
        const LinkSettingRow *row = nullptr;
        for (const LinkSettingRow &r : all) {
            if (r.key == key)
                row = &r;
        }
        bool ok = clean(line) && row && !row->readOnly
                  && (row->kind == kind::choice || row->kind == kind::steps || row->kind == kind::range
                      || row->kind == kind::toggle);
        QVariant v;
        if (ok)
            v = value(*row, wireValue, ok);
        if (!ok) {
            refused.push_back(key);
            continue;
        }
        kept.push_back(key);
        writes.push_back({row, v});
    }
    if (kept.isEmpty())
        return fd::proto::err::refused;
    for (const auto &[row, v] : std::as_const(writes)) {
        if (row->row->key == QLatin1StringView("brightness"))
            m_settings->confirmBrightness(v.toInt());
        else
            m_settings->setProperty(row->setting.toLatin1().constData(), v);
    }
    return 0;
}

}
