#include "ReplayGain.h"

#include "Queue.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace microdap {

namespace {

constexpr double gainCapDb = 12.0;
constexpr double gainLimitDb = 51.0;

std::optional<double> tagged(const std::optional<double> &gain)
{
    if (!gain || !std::isfinite(*gain) || std::abs(*gain) > gainLimitDb)
        return std::nullopt;
    return gain;
}

}

ReplayGainMode replayGainMode(const QString &name)
{
    if (name == QLatin1StringView("track"))
        return ReplayGainMode::Track;
    if (name == QLatin1StringView("album"))
        return ReplayGainMode::Album;
    return ReplayGainMode::Off;
}

double replayGainDb(const QueueEntry &entry, ReplayGainMode mode)
{
    if (mode == ReplayGainMode::Off)
        return 0.0;
    const std::optional<double> track = tagged(entry.trackGainDb);
    const std::optional<double> album = tagged(entry.albumGainDb);
    const bool useAlbum = mode == ReplayGainMode::Album ? album.has_value() : !track.has_value();
    const std::optional<double> gain = useAlbum ? album : track;
    if (!gain)
        return 0.0;
    const std::optional<double> peak = useAlbum ? entry.albumPeak : entry.trackPeak;
    const double full = peak && *peak > 0.0 ? *peak : 1.0;
    return std::min({*gain, gainCapDb, -20.0 * std::log10(full)});
}

double replayGainFactor(const QueueEntry &entry, ReplayGainMode mode)
{
    return std::pow(10.0, replayGainDb(entry, mode) / 20.0);
}

}
