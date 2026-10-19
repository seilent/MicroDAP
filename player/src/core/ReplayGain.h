#pragma once

#include <QString>

namespace microdap {

struct QueueEntry;

enum class ReplayGainMode {
    Off,
    Track,
    Album,
};

ReplayGainMode replayGainMode(const QString &name);
double replayGainDb(const QueueEntry &entry, ReplayGainMode mode);
double replayGainFactor(const QueueEntry &entry, ReplayGainMode mode);

}
