#include "Version.h"

#include <SDL3/SDL_version.h>
#include <gst/gst.h>
#include <taglib/tversionnumber.h>

#include <QtGlobal>

namespace casdap {

QString version()
{
    return QStringLiteral(CASDAP_VERSION);
}

QString dependencies()
{
    guint gstMajor = 0;
    guint gstMinor = 0;
    guint gstMicro = 0;
    guint gstNano = 0;
    gst_version(&gstMajor, &gstMinor, &gstMicro, &gstNano);

    const int sdl = SDL_GetVersion();

    const TagLib::VersionNumber taglib = TagLib::runtimeVersion();

    return QStringLiteral("qt %1 gstreamer %2.%3.%4 taglib %5.%6.%7 sdl %8.%9.%10")
        .arg(QLatin1StringView(qVersion()))
        .arg(gstMajor)
        .arg(gstMinor)
        .arg(gstMicro)
        .arg(taglib.majorVersion())
        .arg(taglib.minorVersion())
        .arg(taglib.patchVersion())
        .arg(SDL_VERSIONNUM_MAJOR(sdl))
        .arg(SDL_VERSIONNUM_MINOR(sdl))
        .arg(SDL_VERSIONNUM_MICRO(sdl));
}

}
