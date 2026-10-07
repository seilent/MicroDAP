#pragma once

#include <QString>
#include <QStringList>

namespace casdap {

struct SectionKey
{
    QString letter;
    QString sortKey;
};

SectionKey sectionKey(const QString &title, bool ignoreThe = false);
QString foldKana(const QString &text);
const QStringList &sectionLetters();

}
