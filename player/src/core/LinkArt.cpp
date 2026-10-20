#include "LinkArt.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>

#include <algorithm>
#include <array>

namespace microdap {

namespace {

constexpr int jpegQuality = 85;

std::array<quint32, 256> crcTable()
{
    std::array<quint32, 256> table{};
    for (quint32 i = 0; i < 256; ++i) {
        quint32 c = i;
        for (int k = 0; k < 8; ++k)
            c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        table[i] = c;
    }
    return table;
}

}

quint32 linkCrc32(const char *data, qsizetype size, quint32 seed)
{
    static const std::array<quint32, 256> table = crcTable();
    quint32 c = ~seed;
    for (qsizetype i = 0; i < size; ++i)
        c = table[(c ^ quint8(data[i])) & 0xff] ^ (c >> 8);
    return ~c;
}

quint32 linkCrc32(const QByteArray &data, quint32 seed)
{
    return linkCrc32(data.constData(), data.size(), seed);
}

bool linkEncodeArt(const QString &path, int px, LinkJpeg &out)
{
    if (path.isEmpty())
        return false;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (size.isValid()) {
        const int shortSide = std::min(size.width(), size.height());
        if (shortSide > px * 2)
            reader.setScaledSize(size * (double(px * 2) / shortSide));
    }
    QImage image = reader.read();
    if (image.isNull())
        return false;
    const int square = std::min(image.width(), image.height());
    image = image.copy((image.width() - square) / 2, (image.height() - square) / 2, square, square);
    const int side = std::min(px, square);
    if (side != square)
        image = image.scaled(side, side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    image = image.convertToFormat(QImage::Format_RGB888);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "JPEG", jpegQuality))
        return false;
    out.bytes = bytes;
    out.crc = linkCrc32(bytes);
    out.side = side;
    return true;
}

}
