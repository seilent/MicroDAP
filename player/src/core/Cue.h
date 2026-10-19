#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <gst/gst.h>

#include <optional>

namespace microdap {

struct CueTrack
{
    int number = 0;
    int startFrame = 0;
    int endFrame = 0;
    QString title;
    QString performer;
    std::optional<double> gain;
    std::optional<double> peak;
};

struct CueSheet
{
    QString file;
    QString title;
    QString performer;
    QString genre;
    int year = 0;
    int disc = 0;
    int files = 0;
    std::optional<double> albumGain;
    std::optional<double> albumPeak;
    QList<CueTrack> tracks;
};

constexpr qsizetype cueCap = 256 * 1024;
constexpr int cueFramesPerSecond = 75;
constexpr double gainLimitDb = 51;

bool parseCue(const QByteArray &bytes, CueSheet &out);
bool parseCueBlock(const QByteArray &block, int rate, CueSheet &out);
std::optional<double> leadingDecimal(const QString &text);
std::optional<double> gainOf(std::optional<double> db);
std::optional<double> peakOf(std::optional<double> peak);
int yearOf(const QString &text);
QByteArray flacCueBlock(const QString &path);
qint64 cueFrameMs(int frames);
GstClockTime cueStartNs(int frames);
GstClockTime cueStopNs(int frames);

}
