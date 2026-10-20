#pragma once

#include <QByteArray>
#include <QString>

namespace microdap {

struct LinkJpeg
{
    QByteArray bytes;
    quint32 crc = 0;
    int side = 0;
};

quint32 linkCrc32(const char *data, qsizetype size, quint32 seed = 0);
quint32 linkCrc32(const QByteArray &data, quint32 seed = 0);
bool linkEncodeArt(const QString &path, int px, LinkJpeg &out);

}
