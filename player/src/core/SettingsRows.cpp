#include "SettingsRows.h"

namespace microdap {

namespace {

using Row = SettingsRows::Row;

QVariantList words(std::initializer_list<const char *> list)
{
    QVariantList out;
    for (const char *w : list)
        out.push_back(QString::fromLatin1(w));
    return out;
}

QVariantList numbers(std::initializer_list<int> list)
{
    QVariantList out;
    for (int n : list)
        out.push_back(n);
    return out;
}

Row row(const char *pivot, const char *key, const char *title, const char *type, const char *kind, const char *setting)
{
    Row r;
    r.pivot = QString::fromLatin1(pivot);
    r.key = QString::fromLatin1(key);
    r.title = QString::fromLatin1(title);
    r.type = QString::fromLatin1(type);
    r.kind = QString::fromLatin1(kind);
    r.setting = QString::fromLatin1(setting);
    r.linkKey = r.setting;
    return r;
}

Row action(const char *pivot, const char *key, const char *title, const char *link)
{
    Row r = row(pivot, key, title, "action", "action", "");
    r.action = r.key;
    r.linkKey = QString::fromLatin1(link);
    return r;
}

Row choice(const char *pivot, const char *key, const char *title, const char *setting, QVariantList values)
{
    Row r = row(pivot, key, title, "choice", "sheet", setting);
    r.values = std::move(values);
    return r;
}

Row steps(const char *pivot, const char *key, const char *title, const char *setting, QVariantList values)
{
    Row r = row(pivot, key, title, "steps", "sheet", setting);
    r.values = std::move(values);
    return r;
}

Row toggle(const char *pivot, const char *key, const char *title, const char *setting)
{
    Row r = row(pivot, key, title, "toggle", "flip", setting);
    r.values = {false, true};
    return r;
}

Row range(const char *pivot, const char *key, const char *title, const char *kind, const char *setting,
          double min, double max, double step)
{
    Row r = row(pivot, key, title, "range", kind, setting);
    r.min = min;
    r.max = max;
    r.step = step;
    return r;
}

Row &draft(Row &r, const char *reason)
{
    r.draft = true;
    r.reason = QString::fromLatin1(reason);
    return r;
}

Row &bounded(Row &r)
{
    r.bounded = true;
    return r;
}

Row &readOnly(Row &r, const char *rule)
{
    r.readOnly = QString::fromLatin1(rule);
    return r;
}

Row &shown(Row &r, const char *rule)
{
    r.shown = QString::fromLatin1(rule);
    return r;
}

Row &link(Row &r, const char *key)
{
    r.linkKey = QString::fromLatin1(key);
    return r;
}

QList<Row> build()
{
    QList<Row> out;

    Row output = choice("output", "output", "output", "output", words({"auto", "speaker", "headphones", "usb"}));
    draft(output, "switches the route; a browse would play on each output; can raise the level");
    out.push_back(output);

    Row hpGain = choice("output", "hpGain", "headphone gain", "hpGain", words({"low", "normal", "high"}));
    readOnly(bounded(draft(hpGain, "raises the level, applies live")), "usbActive");
    out.push_back(hpGain);

    Row hpAmp = choice("output", "hpAmp", "headphone amp", "hpAmp", words({"lohifi", "ulp", "hifi"}));
    readOnly(bounded(draft(hpAmp, "raises the level; each change runs the microos-audio suspend apply cycle")), "usbActive");
    out.push_back(hpAmp);

    Row spkGain = steps("output", "spkGain", "speaker digital gain", "spkGain", numbers({84, 90, 96, 99, 102, 105, 108}));
    bounded(draft(spkGain, "raises the level, same apply cycle"));
    out.push_back(spkGain);

    out.push_back(steps("output", "volumeStep", "volume step", "volumeStep", numbers({2, 5, 10})));

    Row codec = choice("output", "codec", "bluetooth codec", "btCodec", words({"auto", "aptx", "aac", "ldac", "sbc"}));
    out.push_back(draft(codec, "profile switch"));

    Row level = choice("output", "level", "bit perfect", "outputLevel", words({"off", "bitperfect", "exclusive"}));
    out.push_back(readOnly(draft(level, "chain reconfigure, device reservation"), "overlay"));

    Row rate = choice("output", "rate", "sample rate", "sampleRate",
                      words({"auto", "44100", "48000", "88200", "96000", "176400", "192000"}));
    out.push_back(readOnly(draft(rate, "chain reconfigure"), "rate"));

    Row replaygain = row("output", "replaygain", "replaygain", "info", "sheet", "");
    link(replaygain, "replaygain");
    out.push_back(readOnly(draft(replaygain, "can raise the level"), "always"));

    out.push_back(action("output", "eq", "eq", "eq"));
    out.push_back(action("output", "pair", "pair a phone", "pairPhone"));

    out.push_back(action("library", "folders", "library folders", "folders"));
    out.push_back(action("library", "rescan", "rescan library", "rescan"));

    out.push_back(range("display", "accent", "accent", "edit", "accentHue", 0, 350, 10));
    out.push_back(toggle("display", "accentArt", "accent from art", "accentFromArt"));

    Row orientation = choice("display", "orientation", "orientation", "orientation",
                             words({"portrait", "portrait-flipped", "landscape", "landscape-flipped"}));
    draft(orientation, "the UI rotates under the open sheet");
    out.push_back(shown(readOnly(orientation, "orientation"), "dap"));

    Row autoRotate = toggle("display", "autoRotate", "auto rotate", "autoRotate");
    draft(autoRotate, "the UI rotates under the open sheet");
    out.push_back(shown(readOnly(autoRotate, "sensor"), "dap"));

    Row touch = toggle("display", "touch", "touch", "touch");
    out.push_back(shown(touch, "dap"));

    Row scale = range("display", "scale", "scale", "sheet", "scale", 0.8, 1.4, 0.1);
    out.push_back(bounded(draft(scale, "relayout under the open sheet")));

    out.push_back(steps("display", "saver", "saver", "saverSeconds", numbers({10, 15, 30, 60, 120, 300})));

    Row rgb = choice("display", "rgb", "rgb", "rgb", words({"accent", "off", "system"}));
    out.push_back(shown(readOnly(rgb, "rgb"), "dap"));

    out.push_back(toggle("display", "toasts", "toasts in game", "overlayToasts"));

    out.push_back(steps("power", "screenOff", "screen off after", "screenOffSeconds", numbers({0, 60, 120, 300, 600, 1800})));
    out.push_back(steps("power", "suspend", "suspend after", "suspendMinutes", numbers({0, 5, 10, 30, 60})));
    out.push_back(toggle("power", "wifi", "wi-fi when off", "wifiOffScreenOff"));

    Row boot = choice("power", "boot", "boot into", "bootSession", {});
    out.push_back(shown(readOnly(draft(boot, "boot target write"), "boot"), "boot"));

    Row closePlayer = action("power", "closePlayer", "close player", "closePlayer");
    out.push_back(shown(closePlayer, "overlay"));
    out.push_back(action("power", "about", "about", "about"));

    return out;
}

}

QStringList SettingsRows::pivots()
{
    return {QStringLiteral("output"), QStringLiteral("library"), QStringLiteral("display"), QStringLiteral("power")};
}

QStringList SettingsRows::types()
{
    return {QStringLiteral("choice"), QStringLiteral("steps"), QStringLiteral("range"), QStringLiteral("toggle"),
            QStringLiteral("action"), QStringLiteral("info")};
}

QStringList SettingsRows::kinds()
{
    return {QStringLiteral("sheet"), QStringLiteral("flip"), QStringLiteral("edit"), QStringLiteral("action")};
}

const QList<SettingsRows::Row> &SettingsRows::all()
{
    static const QList<Row> rows = build();
    return rows;
}

const SettingsRows::Row *SettingsRows::find(const QString &key)
{
    for (const Row &r : all()) {
        if (r.key == key)
            return &r;
    }
    return nullptr;
}

QVariantMap SettingsRows::map(const Row &row)
{
    return {
        {QStringLiteral("key"), row.key},
        {QStringLiteral("title"), row.title},
        {QStringLiteral("pivot"), row.pivot},
        {QStringLiteral("type"), row.type},
        {QStringLiteral("kind"), row.kind},
        {QStringLiteral("setting"), row.setting},
        {QStringLiteral("values"), row.values},
        {QStringLiteral("min"), row.min},
        {QStringLiteral("max"), row.max},
        {QStringLiteral("step"), row.step},
        {QStringLiteral("action"), row.action},
        {QStringLiteral("readOnly"), row.readOnly},
        {QStringLiteral("shown"), row.shown},
        {QStringLiteral("bounded"), row.bounded},
        {QStringLiteral("draft"), row.draft},
        {QStringLiteral("reason"), row.reason},
        {QStringLiteral("linkKey"), row.linkKey},
    };
}

QVariantList SettingsRows::maps()
{
    QVariantList out;
    for (const Row &r : all())
        out.push_back(map(r));
    return out;
}

}
