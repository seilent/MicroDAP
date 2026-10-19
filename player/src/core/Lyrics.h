#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>

namespace microdap {

struct LyricLine
{
    qint64 ms = 0;
    QString text;

    bool operator==(const LyricLine &) const = default;
};

struct LyricsText
{
    enum Source { None, Beside, Tag, Store };

    QList<LyricLine> lines;
    QStringList plain;
    Source source = None;

    bool timed() const { return !lines.isEmpty(); }
    bool empty() const { return lines.isEmpty() && plain.isEmpty(); }
};

struct LyricsTrack
{
    QString path;
    qint64 beginMs = 0;
    qint64 endMs = 0;
    bool cue = false;

    QString key() const;
};

class Lyrics : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(QVariantList lines READ lines NOTIFY changed)
    Q_PROPERTY(QStringList plain READ plain NOTIFY changed)

public:
    static constexpr qsizetype cap = 64 * 1024;
    static constexpr qsizetype tagTotal = 256 * 1024;
    static constexpr qsizetype maxLines = 1024;
    static constexpr qint64 maxStampMs = 100LL * 3600 * 1000;
    static constexpr qint64 maxOffsetMs = 3600 * 1000;

    explicit Lyrics(QObject *parent = nullptr);
    ~Lyrics() override;

    static LyricsText parse(const QString &text);
    static LyricsText parseBytes(const QByteArray &bytes);
    static QString besidePath(const QString &audio);
    static LyricsText readBeside(const QString &audio);
    static QStringList tagValues(const QString &audio);
    static LyricsText readTags(const QString &audio);
    static LyricsText readFile(const QString &path, LyricsText::Source source);
    static LyricsText pick(const QList<LyricsText> &sources);
    static LyricsText load(const QString &audio, const QString &storeFile = {});
    static LyricsText load(const LyricsTrack &track, const QString &storeFile = {});
    static LyricsText span(const LyricsText &text, qint64 beginMs, qint64 endMs);
    static int lineAt(const QList<LyricLine> &lines, qint64 ms);

    bool pending() const;
    QVariantList lines() const;
    QStringList plain() const;
    LyricsText current() const;
    Q_INVOKABLE int lineAt(qint64 ms) const;

    void setTracks(const QString &current, const QString &next);
    void setTracks(const LyricsTrack &current, const LyricsTrack &next);
    void refresh();
    void waitForDone();

signals:
    void changed();

private:
    void request(const LyricsTrack &track);
    void loaded(const QString &path, int generation, const LyricsText &text);
    bool publish();

    QString m_current;
    QString m_next;
    LyricsTrack m_currentTrack;
    LyricsTrack m_nextTrack;
    QString m_shown;
    LyricsText m_text;
    QHash<QString, LyricsText> m_cache;
    QHash<QString, int> m_pending;
    int m_generation = 0;
    QThreadPool m_pool;
};

}
