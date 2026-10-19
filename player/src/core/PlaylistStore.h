#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

#include <functional>

namespace microdap {

struct M3uItem
{
    QByteArray text;
    QString path;
    bool entry = false;
    int cue = 0;
};

struct M3u
{
    QByteArray bom;
    QByteArray eol = "\n";
    QList<M3uItem> items;
};

class PlaylistStore
{
public:
    enum class State { Read, Missing, Failed };
    enum class Edit { Done, Unreadable, Unwritten };
    using Change = std::function<bool(M3u &doc)>;

    struct Loaded
    {
        M3u doc;
        State state = State::Missing;
    };

    static constexpr qint64 maxBytes = 1 << 20;

    static M3u parse(const QByteArray &raw, const QString &dir);
    static QByteArray bytes(const M3u &doc);
    static QString entryPath(QByteArray line, const QString &dir);
    static QString relative(const QString &path, const QString &dir);
    static int cueOf(const QByteArray &line);
    static qsizetype unit(const M3u &doc, qsizetype i);
    static qsizetype insert(M3u &doc, qsizetype before, const QString &path, const QString &dir, int cue = 0);
    static void erase(M3u &doc, qsizetype i);
    static int remove(M3u &doc, const QString &path);
    static void swap(M3u &doc, qsizetype a, qsizetype b);
    static QString playlistsDir(const QString &root);
    static QString findFile(const QString &dir, const QString &name);

    Loaded read(const QString &path);
    bool write(const QString &path, const M3u &doc);
    Edit edit(const QString &path, const Change &change);
    bool ownChange(const QString &dir) const;
    void settle(const QString &dir);

private:
    QHash<QString, QByteArray> m_written;
    QHash<QString, QByteArray> m_settled;
};

}
