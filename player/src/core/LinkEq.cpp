#include "LinkEq.h"

#include <algorithm>
#include <cmath>

namespace microdap {

namespace {

namespace bt = fd::proto::val::BAND_TYPE;
namespace md = fd::proto::val::EQ_MODE;
namespace err = fd::proto::err;

std::string utf8(const QString &s)
{
    return s.toStdString();
}

int tenths(double v)
{
    return int(std::lround(v * 10.0));
}

uint8_t wireType(const EqBand &b)
{
    if (!b.enabled)
        return bt::off;
    switch (b.type) {
    case EqBand::LowShelf:
        return bt::lowShelf;
    case EqBand::HighShelf:
        return bt::highShelf;
    case EqBand::BandPass:
        return bt::bandPass;
    case EqBand::LowPass:
        return bt::lowPass;
    case EqBand::HighPass:
        return bt::highPass;
    case EqBand::AllPass:
        return bt::allPass;
    default:
        return bt::peak;
    }
}

bool gainFree(EqBand::Type t)
{
    return t == EqBand::BandPass || t == EqBand::LowPass || t == EqBand::HighPass || t == EqBand::AllPass;
}

bool builtIn(const QString &key, bool graphic)
{
    return key == QLatin1StringView("off") || key == QLatin1StringView("custom")
           || (graphic && key == QLatin1StringView("flat"));
}

uint8_t refusalCode(EqRefusal r)
{
    switch (r) {
    case EqRefusal::None:
        return 0;
    case EqRefusal::Name:
        return err::badValue;
    case EqRefusal::Exists:
        return err::exists;
    case EqRefusal::Full:
        return err::noSpace;
    case EqRefusal::NotFound:
        return err::notFound;
    case EqRefusal::ReadOnly:
        return err::readOnly;
    default:
        return err::refused;
    }
}

}

QByteArray LinkEqPush::sig() const
{
    QByteArray out = key.toUtf8() + '\n';
    out.append(reinterpret_cast<const char *>(ints.data().data()), qsizetype(ints.size()));
    for (const LinkEqName &n : names)
        out += '\n' + n.key.toUtf8() + '\t' + n.name.toUtf8() + (n.preset ? "\t1" : "");
    out += QByteArray(graphic ? "\ng" : "\np") + graphicKey.toUtf8() + '\n';
    if (hasGains)
        out.append(reinterpret_cast<const char *>(gains.data().data()), qsizetype(gains.size()));
    for (const LinkEqName &n : graphicNames)
        out += '\n' + n.key.toUtf8() + '\t' + n.name.toUtf8();
    return out;
}

QStringList LinkEq::slotKeys()
{
    return EqControl::outputs();
}

QString LinkEq::label(const QString &slot)
{
    return slot.toUpper();
}

std::vector<uint8_t> LinkEq::bandTypes()
{
    return {bt::off, bt::peak, bt::lowShelf, bt::highShelf, bt::bandPass, bt::lowPass, bt::highPass, bt::allPass};
}

int LinkEq::graphicGrid(int band)
{
    return EqProfile::flat().bands[band].freqIndex;
}

void LinkEq::profileWire(const EqProfile &p, fd::LinkWriter &w)
{
    namespace pt = fd::proto::tag::Profile;
    namespace bd = fd::proto::tag::Band;
    w.i32(pt::preamp10, tenths(p.preamp));
    for (int i = 0; i < p.count; ++i) {
        const EqBand &b = p.bands[i];
        fd::LinkWriter r;
        r.u8(bd::type, wireType(b))
            .i32(bd::grid, b.freqIndex)
            .i32(bd::milliHz, int(std::lround(b.freq * 1000.0)))
            .i32(bd::gain10, b.enabled && gainFree(b.type) ? 0 : tenths(b.gain))
            .i32(bd::q100, int(std::lround(b.q * 100.0)));
        w.rec(pt::band, r);
    }
}

void LinkEq::gainsWire(const Gains &gains, fd::LinkWriter &w)
{
    namespace gt = fd::proto::tag::Graphic;
    namespace gb = fd::proto::tag::GraphicBand;
    w.i32(gt::preamp10, tenths(EqProfile::graphic(gains).preamp));
    for (int i = 0; i < EqProfile::bandCount; ++i) {
        fd::LinkWriter b;
        b.i32(gb::grid, graphicGrid(i)).i32(gb::gain10, tenths(gains[size_t(i)]));
        w.rec(gt::band, b);
    }
}

bool LinkEq::profileOf(const fd::LinkField &f, EqProfile &out)
{
    const fd::LinkBody b{f.rec};
    EqProfile p = EqProfile::flat();
    if (b.has("preamp10"))
        p.preamp = double(b.i("preamp10")) / 10.0;
    const std::vector<const fd::LinkField *> bands = b.all("band");
    if (bands.empty() || bands.size() > size_t(EqProfile::bandCount))
        return false;
    if (p.preamp < EqParser::minPreamp || p.preamp > EqParser::maxPreamp)
        return false;
    p.count = int(bands.size());
    const std::vector<uint8_t> types = bandTypes();
    for (size_t i = 0; i < bands.size(); ++i) {
        const fd::LinkBody r{bands[i]->rec};
        EqBand o = EqProfile::freshBand();
        if (r.has("type")) {
            const uint64_t t = r.u("type");
            if (std::find(types.begin(), types.end(), t) == types.end())
                return false;
            o.enabled = t != bt::off;
            o.type = t == bt::lowShelf    ? EqBand::LowShelf
                     : t == bt::highShelf ? EqBand::HighShelf
                     : t == bt::bandPass  ? EqBand::BandPass
                     : t == bt::lowPass   ? EqBand::LowPass
                     : t == bt::highPass  ? EqBand::HighPass
                     : t == bt::allPass   ? EqBand::AllPass
                                          : EqBand::Peak;
        }
        if (r.has("grid"))
            o.freqIndex = int(r.i("grid"));
        if (o.freqIndex >= EqGrid::size)
            return false;
        if (o.freqIndex >= 0)
            o.freq = EqGrid::frequency(o.freqIndex);
        if (r.has("milliHz") && (!r.has("grid") || o.freqIndex < 0)) {
            o.freqIndex = -1;
            o.freq = double(r.i("milliHz")) / 1000.0;
        }
        if (o.freqIndex < -1)
            return false;
        if (r.has("gain10"))
            o.gain = double(r.i("gain10")) / 10.0;
        if (r.has("q100"))
            o.q = double(r.i("q100")) / 100.0;
        if (gainFree(o.type))
            o.gain = 0.0;
        if (!std::isfinite(o.freq) || o.freq < EqParser::minFreq || o.freq > EqParser::maxFreq
            || o.gain < EqParser::minGain || o.gain > EqParser::maxGain || o.q < EqParser::minQ || o.q > EqParser::maxQ)
            return false;
        p.bands[i] = o;
    }
    out = p;
    return true;
}

bool LinkEq::gainsOf(const fd::LinkField &f, Gains &out)
{
    const fd::LinkBody b{f.rec};
    const std::vector<const fd::LinkField *> bands = b.all("band");
    if (bands.size() != size_t(EqProfile::bandCount))
        return false;
    Gains g{};
    for (size_t i = 0; i < bands.size(); ++i) {
        const fd::LinkBody r{bands[i]->rec};
        if (r.i("grid", -1) != graphicGrid(int(i)))
            return false;
        g[i] = double(r.i("gain10")) / 10.0;
        if (g[i] < EqParser::minGain || g[i] > EqParser::maxGain)
            return false;
    }
    out = g;
    return true;
}

LinkEqPush LinkEq::push(const EqControl &eq, const QString &output)
{
    LinkEqPush e;
    const EqTarget t = eq.target(output);
    e.key = t.key;
    e.names.append({QStringLiteral("off"), QStringLiteral("OFF")});
    const QStringList presetList = eq.presetKeys(false);
    const QSet<QString> presets(presetList.cbegin(), presetList.cend());
    for (const QString &k : eq.profileKeys())
        e.names.append({k, k, presets.contains(k)});
    e.names.append({QStringLiteral("custom"), QStringLiteral("CUSTOM")});
    profileWire(eq.parametric(output), e.ints);
    e.graphic = t.graphic;
    e.graphicKey = t.preset;
    e.graphicNames.append({QStringLiteral("off"), QStringLiteral("OFF")});
    e.graphicNames.append({QStringLiteral("flat"), QStringLiteral("FLAT")});
    for (const QString &k : eq.graphicKeys())
        e.graphicNames.append({k, k, true});
    e.graphicNames.append({QStringLiteral("custom"), QStringLiteral("CUSTOM")});
    if (const std::optional<Gains> g = eq.presetGains(output)) {
        gainsWire(*g, e.gains);
        e.hasGains = true;
    }
    return e;
}

uint8_t LinkEq::set(EqControl &eq, const QString &output, const fd::LinkBody &b)
{
    if (!EqControl::outputs().contains(output))
        return err::badValue;
    const bool hasKey = b.has("key");
    const bool hasGraphicKey = b.has("graphicKey");
    const bool hasMode = b.has("mode");
    const QString key = QString::fromStdString(b.str("key"));
    const QString graphicKey = QString::fromStdString(b.str("graphicKey"));
    const fd::LinkField *ints = b.get("ints");
    const fd::LinkField *gains = b.get("gains");
    const bool para = hasKey || ints;
    const bool graph = hasGraphicKey || gains;
    if (hasMode ? b.u("mode") > md::graphic : para == graph)
        return err::badValue;
    if (hasKey && !builtIn(key, false) && !eq.profileKeys().contains(key))
        return err::badValue;
    if (hasGraphicKey && !builtIn(graphicKey, true) && !eq.graphicKeys().contains(graphicKey))
        return err::badValue;
    EqProfile p;
    if (ints && !profileOf(*ints, p))
        return err::badValue;
    Gains g{};
    if (gains && !gainsOf(*gains, g))
        return err::badValue;
    EqTarget t = eq.target(output);
    t.graphic = hasMode ? b.u("mode") == md::graphic : graph;
    if (ints) {
        t.key = QStringLiteral("custom");
        t.custom = p;
    }
    if (hasKey)
        t.key = key;
    if (gains) {
        t.gains = g;
        t.preset = QStringLiteral("custom");
    }
    if (hasGraphicKey)
        t.preset = graphicKey;
    eq.apply(output, t);
    return 0;
}

uint8_t LinkEq::preset(EqControl &eq, const QString &output, const fd::LinkBody &b, QString &key)
{
    namespace op = fd::proto::val::EQ_PRESET_OP;
    const uint64_t what = b.u("op", UINT64_MAX);
    const bool save = what == op::save;
    const bool replace = b.u("replace") != 0;
    const bool graphic = b.u("mode") == md::graphic;
    const QString name = QString::fromStdString(b.str("name"));
    const QString path = QString::fromStdString(b.str("key"));
    if (what > op::rename || b.u("mode") > md::graphic)
        return err::badValue;
    if (save ? !EqControl::outputs().contains(output) : !b.has("key") || (what == op::rename && !b.has("name")))
        return err::badValue;
    if ((save && b.has("name") && name.isEmpty()) || b.has(graphic ? "ints" : "gains"))
        return err::badValue;
    EqProfile p = save ? eq.customProfile(output) : EqProfile::flat();
    Gains g = save ? eq.customGraphic(output) : Gains{};
    if (save && !graphic && b.get("ints") && !profileOf(*b.get("ints"), p))
        return err::badValue;
    if (save && graphic && b.get("gains") && !gainsOf(*b.get("gains"), g))
        return err::badValue;
    if (!save && builtIn(path, graphic))
        return err::readOnly;
    if (!save && !(graphic ? eq.graphicKeys() : eq.profileKeys()).contains(path))
        return err::notFound;
    QString out;
    const EqRefusal r = save                ? eq.presetSave(output, graphic, name, p, g, replace, &out)
                        : what == op::rename ? eq.presetRename(path, name, replace, graphic, &out)
                                             : eq.presetRemove(path, graphic);
    if (r != EqRefusal::None)
        return refusalCode(r);
    key = out;
    return 0;
}

}
