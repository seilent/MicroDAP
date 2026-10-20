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

quint32 linkCrc32(const QByteArray &data);
bool linkEncodeArt(const QString &path, int px, LinkJpeg &out);

}
