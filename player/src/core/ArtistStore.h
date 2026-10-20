#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <cstdint>

namespace microdap {

class ArtistStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    static constexpr quint8 originLibrary = 1;
    static constexpr quint8 originPhone = 2;
    static constexpr int metaBytes = 256;
    static constexpr int nameBytes = 255;
    static constexpr int focusScale = 1000;
    static constexpr int maxParts = 4;
    static constexpr int originDelayMs = 2000;
    static constexpr qint64 defaultCap = 128LL * 1024 * 1024;

    enum class Code { Ok, Crc, TooBig, BadValue, NoSpace, Busy, WriteError, Refused, NotFound };

    struct Head
    {
        QString name;
        QString mbid;
        QString source;
        QString credit;
        quint32 total = 0;
        quint32 crc = 0;
        int w = 0;
        int h = 0;
        bool wrong = false;
        quint8 origin = 0;
        int focusX = -1;
        int focusY = -1;
    };

    struct Entry
    {
        quint64 key = 0;
        QString name;
        QString mbid;
        QString source;
        QString credit;
        quint32 crc = 0;
        quint32 size = 0;
        qint64 mtime = 0;
        bool wrong = false;
        quint8 origin = 0;
        int focusX = -1;
        int focusY = -1;
    };

    static QString defaultRoot();
    static quint64 key(const QString &name);
    static QString cutMeta(const QString &text);
    static bool jpegFrame(const QByteArray &bytes, int &w, int &h);

    explicit ArtistStore(QString root = defaultRoot(), int side = 960, quint32 maxBytes = 262144,
                         quint32 chunk = 16368, qint64 cap = defaultCap, QObject *parent = nullptr);
    ~ArtistStore() override;

    Code begin(const Head &head, quint32 &offset, quint32 &prefixCrc, quint32 *push = nullptr);
    Code data(quint32 offset, const QByteArray &bytes, quint32 push = 0);
    Code end(quint32 crc, quint32 push = 0);
    bool remove(const QString &name);
    bool tag(const QString &name, quint8 bits);
    int prune(const QSet<quint64> &keep);
    QList<Entry> listed() const;
    quint64 bytes() const;
    qint64 cap() const;
    QString root() const;
    QString imagePath(quint64 key) const;
    quint8 origin(quint64 key) const;
    bool pushing() const;
    bool flushOrigins();
    int revision() const;
    Q_INVOKABLE QUrl photoFor(const QStringList &names) const;
    Q_INVOKABLE QString photoArtist(const QStringList &names) const;
    Q_INVOKABLE QVariantMap photoInfo(const QString &name) const;
    Q_INVOKABLE QPointF photoFocus(const QStringList &names) const;
    Q_INVOKABLE static QStringList splitArtists(const QString &text);
    Q_INVOKABLE void touchName(const QString &name);
    void touch(quint64 key);

signals:
    void changed();

private:
    struct Part
    {
        quint64 key = 0;
        quint64 seq = 0;
        Head head;
        QByteArray data;
        quint32 crc = 0;
    };

    void rebuild();
    void loadOrigins();
    bool writeInfo(const Entry &e) const;
    void addOrigin(quint64 key, quint8 bits);
    void dropOrigin(quint64 key);
    void dropImage(quint64 key);
    void evict(quint64 keep);
    Part *part(quint64 key);
    void dropPart(quint64 key);
    void bump();

    QString m_root;
    int m_side;
    quint32 m_max;
    quint32 m_chunk;
    qint64 m_cap;
    quint64 m_seq = 0;
    quint64 m_cur = 0;
    bool m_active = false;
    quint32 m_push = 0;
    QList<Part> m_parts;
    QList<Entry> m_entries;
    QList<QPair<quint64, quint8>> m_origins;
    QTimer m_originTimer;
    bool m_originsDirty = false;
    int m_revision = 0;
};

}
