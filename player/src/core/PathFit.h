#pragma once

#include <QString>
#include <QStringList>

namespace microdap {

template <typename Advance>
QString fitPathParts(const QStringList &parts, const Advance &advance, qreal width)
{
    if (parts.isEmpty())
        return {};
    const QString separator = QStringLiteral(" / ");
    const QString all = parts.join(separator);
    if (advance(all) <= width)
        return all;
    for (qsizetype from = 1; from < parts.size(); ++from) {
        const QString line = QStringLiteral("...") + separator + parts.mid(from).join(separator);
        if (advance(line) <= width)
            return line;
    }
    return parts.last();
}

}
