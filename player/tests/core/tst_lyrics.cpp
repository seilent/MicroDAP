#include "Lyrics.h"

#include <taglib/apetag.h>
#include <taglib/dsffile.h>
#include <taglib/flacfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/mpegfile.h>
#include <taglib/tstring.h>
#include <taglib/unsynchronizedlyricsframe.h>
#include <taglib/vorbisfile.h>
#include <taglib/xiphcomment.h>

#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace microdap;

namespace {

constexpr int loadTimeoutMs = 10000;

TagLib::String tagText(const QString &text)
{
    return TagLib::String(text.toUtf8().constData(), TagLib::String::UTF8);
}

QString linesOf(const LyricsText &text)
{
    QStringList out;
    for (const LyricLine &line : text.lines)
        out.append(QString::number(line.ms) + QLatin1Char(' ') + line.text);
    return out.join(QLatin1Char('|'));
}

bool writeBytes(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size();
}

QString copyFixture(const QString &from, const QString &to)
{
    QFile::remove(to);
    if (!QFile::copy(from, to))
        return {};
    QFile::setPermissions(to, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return to;
}

bool xiphLyrics(const QString &path, const char *key, const QStringList &values)
{
    TagLib::FLAC::File file(QFile::encodeName(path).constData());
    if (!file.isValid())
        return false;
    for (const QString &value : values)
        file.xiphComment(true)->addField(key, tagText(value), false);
    return file.save();
}

bool usltLyrics(TagLib::ID3v2::Tag *tag, const QStringList &values)
{
    if (!tag)
        return false;
    for (const QString &value : values) {
        auto *frame = new TagLib::ID3v2::UnsynchronizedLyricsFrame(TagLib::String::UTF8);
        frame->setLanguage("eng");
        frame->setText(tagText(value));
        tag->addFrame(frame);
    }
    return true;
}

}

class LyricsTest : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void stamps();
    void offsetTag();
    void plainText();
    void bytes();
    void limits();
    void lineAt();
    void beside();
    void flacTags();
    void mp3Tags();
    void otherTags();
    void precedence();
    void storeSlot();
    void storePutAndDel();
    void tracks();
    void refresh();
    void cueSpan();

private:
    QString flac(const QString &name);

    std::unique_ptr<QTemporaryDir> m_dir;
};

void LyricsTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
}

QString LyricsTest::flac(const QString &name)
{
    return copyFixture(QStringLiteral(MICRODAP_FIXTURE_MUSIC "/Album One/01 One.flac"), m_dir->filePath(name));
}

void LyricsTest::stamps()
{
    const LyricsText text = Lyrics::parse(QStringLiteral(
        "[ar:Artist]\n"
        "  [00:05.50]  second  \n"
        "[00:01]first\n"
        "[00:10.5][01:00.123]twice\n"
        "[00:07:25]colon\n"
        "[00:60.00]bad seconds\n"
        "[0:3.07]short\n"
        "no stamp line\n"
        "[00:20.00]\n"));
    QVERIFY(text.timed());
    QVERIFY(text.plain.isEmpty());
    QCOMPARE(linesOf(text), QStringLiteral("1000 first|3070 short|5500 second|7250 colon|10500 twice|20000 |"
                                           "60123 twice"));
}

void LyricsTest::offsetTag()
{
    const LyricsText earlier = Lyrics::parse(QStringLiteral("[offset:+200]\n[00:01.00]a\n[00:00.10]b\n"));
    QCOMPARE(linesOf(earlier), QStringLiteral("0 b|800 a"));
    const LyricsText later = Lyrics::parse(QStringLiteral("[offset:100]\n[00:01.00]a\n[OFFSET: -500]\n"));
    QCOMPARE(linesOf(later), QStringLiteral("1500 a"));
    const LyricsText clamped = Lyrics::parse(QStringLiteral("[offset:-5000000]\n[00:01.00]a\n"));
    QCOMPARE(linesOf(clamped), QStringLiteral("3601000 a"));
    const LyricsText tooLong = Lyrics::parse(QStringLiteral("[offset:99999999]\n[00:01.00]a\n"));
    QCOMPARE(linesOf(tooLong), QStringLiteral("1000 a"));
}

void LyricsTest::plainText()
{
    const LyricsText text = Lyrics::parse(QStringLiteral(
        "\n[ti:Title]\n[ar:Someone]\n  First line  \n\n[Chorus]\nla la\n[offset:0]\n\n"));
    QVERIFY(!text.timed());
    QCOMPARE(text.plain, QStringList({QStringLiteral("First line"), QString(), QStringLiteral("[Chorus]"),
                                      QStringLiteral("la la")}));
    QVERIFY(Lyrics::parse(QStringLiteral("[ti:x]\n[ar:y]\n\n")).empty());
    QVERIFY(Lyrics::parse(QString()).empty());
}

void LyricsTest::bytes()
{
    const LyricsText bom = Lyrics::parseBytes(QByteArrayLiteral("\xEF\xBB\xBF[00:01.00]caf\xC3\xA9\r\n[00:02.00]b\rc\n"));
    QCOMPARE(linesOf(bom), QStringLiteral("1000 café|2000 b"));
    const LyricsText latin = Lyrics::parseBytes(QByteArrayLiteral("[00:01.00]caf\xE9\n"));
    QCOMPARE(linesOf(latin), QStringLiteral("1000 café"));
    QVERIFY(Lyrics::parseBytes(QByteArrayLiteral("\xFF\xFE[\x00" "0\x00")).empty());
    QVERIFY(Lyrics::parseBytes(QByteArray("[\x00" "0\x00" "0\x00", 6)).empty());
}

void LyricsTest::limits()
{
    QByteArray big(Lyrics::cap + 1, 'a');
    QVERIFY(Lyrics::parseBytes(big).empty());

    QString many;
    for (int i = 0; i < Lyrics::maxLines + 10; ++i)
        many += QStringLiteral("[00:%1.00]x\n").arg(i % 60, 2, 10, QLatin1Char('0'));
    QCOMPARE(Lyrics::parse(many).lines.size(), Lyrics::maxLines);

    const QString words(1000, QLatin1Char('w'));
    QString wide;
    for (int i = 0; i < 100; ++i)
        wide += QStringLiteral("[00:01.00]") + words + QLatin1Char('\n');
    QCOMPARE(Lyrics::parse(wide).lines.size(), qsizetype(Lyrics::cap / 1000));
}

void LyricsTest::lineAt()
{
    const QList<LyricLine> lines{{1000, QStringLiteral("a")}, {2000, QStringLiteral("b")}, {2000, QStringLiteral("c")}};
    QCOMPARE(Lyrics::lineAt(lines, 0), -1);
    QCOMPARE(Lyrics::lineAt(lines, 999), -1);
    QCOMPARE(Lyrics::lineAt(lines, 1000), 0);
    QCOMPARE(Lyrics::lineAt(lines, 1999), 0);
    QCOMPARE(Lyrics::lineAt(lines, 2000), 2);
    QCOMPARE(Lyrics::lineAt({}, 5000), -1);
}

void LyricsTest::beside()
{
    const QString audio = flac(QStringLiteral("song.v2.flac"));
    QVERIFY(!audio.isEmpty());
    QVERIFY(Lyrics::besidePath(audio).isEmpty());
    QVERIFY(Lyrics::readBeside(audio).empty());

    QVERIFY(writeBytes(m_dir->filePath(QStringLiteral("song.v2.LRC")), QByteArrayLiteral("[00:01.00]upper\n")));
    QCOMPARE(Lyrics::besidePath(audio), m_dir->filePath(QStringLiteral("song.v2.LRC")));
    const LyricsText upper = Lyrics::readBeside(audio);
    QCOMPARE(upper.source, LyricsText::Beside);
    QCOMPARE(linesOf(upper), QStringLiteral("1000 upper"));

    QVERIFY(writeBytes(m_dir->filePath(QStringLiteral("song.v2.lrc")), QByteArrayLiteral("plain\n")));
    QCOMPARE(Lyrics::besidePath(audio), m_dir->filePath(QStringLiteral("song.v2.lrc")));
    QCOMPARE(Lyrics::readBeside(audio).plain, QStringList{QStringLiteral("plain")});

    QVERIFY(writeBytes(m_dir->filePath(QStringLiteral("song.v2.lrc")), QByteArray(Lyrics::cap + 1, 'a')));
    QVERIFY(Lyrics::readBeside(audio).empty());
}

void LyricsTest::flacTags()
{
    const QString audio = flac(QStringLiteral("tags.flac"));
    QVERIFY(!audio.isEmpty());
    QVERIFY(Lyrics::tagValues(audio).isEmpty());
    QVERIFY(xiphLyrics(audio, "UNSYNCEDLYRICS", {QStringLiteral("[00:03.00]unsynced")}));
    QVERIFY(xiphLyrics(audio, "LYRICS", {QStringLiteral("first plain"), QStringLiteral("[00:02.00]stamped")}));
    QCOMPARE(Lyrics::tagValues(audio), QStringList({QStringLiteral("first plain"), QStringLiteral("[00:02.00]stamped"),
                                                    QStringLiteral("[00:03.00]unsynced")}));
    const LyricsText text = Lyrics::readTags(audio);
    QCOMPARE(text.source, LyricsText::Tag);
    QCOMPARE(linesOf(text), QStringLiteral("2000 stamped"));

    const QString huge = flac(QStringLiteral("huge.flac"));
    QVERIFY(xiphLyrics(huge, "LYRICS", {QString(Lyrics::cap + 1, QLatin1Char('x')), QStringLiteral("small")}));
    QCOMPARE(Lyrics::tagValues(huge), QStringList{QStringLiteral("small")});
}

void LyricsTest::mp3Tags()
{
    const QString audio = copyFixture(QStringLiteral(MICRODAP_FIXTURE_MUSIC "/Single/01 Single.mp3"),
                                      m_dir->filePath(QStringLiteral("single.mp3")));
    QVERIFY(!audio.isEmpty());
    {
        TagLib::MPEG::File file(QFile::encodeName(audio).constData());
        QVERIFY(file.isValid());
        QVERIFY(usltLyrics(file.ID3v2Tag(true), {QStringLiteral("uslt plain"), QStringLiteral("[00:04.00]uslt")}));
        file.APETag(true)->addValue("LYRICS", tagText(QStringLiteral("[00:05.00]ape")));
        QVERIFY(file.save());
    }
    QCOMPARE(Lyrics::tagValues(audio), QStringList({QStringLiteral("uslt plain"), QStringLiteral("[00:04.00]uslt"),
                                                    QStringLiteral("[00:05.00]ape")}));
    QCOMPARE(linesOf(Lyrics::readTags(audio)), QStringLiteral("4000 uslt"));
}

void LyricsTest::otherTags()
{
    const QString oga = copyFixture(QStringLiteral(MICRODAP_FIXTURE_FORMATS "/vorbis.oga"),
                                    m_dir->filePath(QStringLiteral("vorbis.oga")));
    QVERIFY(!oga.isEmpty());
    {
        TagLib::Ogg::Vorbis::File file(QFile::encodeName(oga).constData());
        QVERIFY(file.isValid());
        file.tag()->addField("UNSYNCEDLYRICS", tagText(QStringLiteral("ogg words")), false);
        QVERIFY(file.save());
    }
    QCOMPARE(Lyrics::readTags(oga).plain, QStringList{QStringLiteral("ogg words")});

    const QString mp4 = copyFixture(QStringLiteral(MICRODAP_FIXTURE_FORMATS "/aac.mp4"),
                                    m_dir->filePath(QStringLiteral("aac.mp4")));
    QVERIFY(!mp4.isEmpty());
    {
        TagLib::MP4::File file(QFile::encodeName(mp4).constData());
        QVERIFY(file.isValid());
        file.tag()->setItem("\251lyr", TagLib::StringList(tagText(QStringLiteral("[00:06.00]mp4"))));
        QVERIFY(file.save());
    }
    QCOMPARE(linesOf(Lyrics::readTags(mp4)), QStringLiteral("6000 mp4"));

    const QString dsf = copyFixture(QStringLiteral(MICRODAP_FIXTURE_FORMATS "/dsd64.dsf"),
                                    m_dir->filePath(QStringLiteral("dsd64.dsf")));
    QVERIFY(!dsf.isEmpty());
    {
        TagLib::DSF::File file(QFile::encodeName(dsf).constData());
        QVERIFY(file.isValid());
        QVERIFY(usltLyrics(file.tag(), {QStringLiteral("dsf words")}));
        QVERIFY(file.save());
    }
    QCOMPARE(Lyrics::readTags(dsf).plain, QStringList{QStringLiteral("dsf words")});
}

void LyricsTest::precedence()
{
    const QString audio = flac(QStringLiteral("order.flac"));
    QVERIFY(!audio.isEmpty());
    QVERIFY(xiphLyrics(audio, "LYRICS", {QStringLiteral("[00:02.00]tag")}));
    QVERIFY(writeBytes(m_dir->filePath(QStringLiteral("order.lrc")), QByteArrayLiteral("beside plain\n")));
    const LyricsText tagWins = Lyrics::load(audio);
    QCOMPARE(tagWins.source, LyricsText::Tag);
    QCOMPARE(linesOf(tagWins), QStringLiteral("2000 tag"));

    QVERIFY(writeBytes(m_dir->filePath(QStringLiteral("order.lrc")), QByteArrayLiteral("[00:01.00]beside\n")));
    const LyricsText besideWins = Lyrics::load(audio);
    QCOMPARE(besideWins.source, LyricsText::Beside);
    QCOMPARE(linesOf(besideWins), QStringLiteral("1000 beside"));

    const QString plainTag = flac(QStringLiteral("plain.flac"));
    QVERIFY(xiphLyrics(plainTag, "LYRICS", {QStringLiteral("tag plain")}));
    QVERIFY(writeBytes(m_dir->filePath(QStringLiteral("plain.lrc")), QByteArrayLiteral("beside plain\n")));
    const LyricsText firstPlain = Lyrics::load(plainTag);
    QCOMPARE(firstPlain.source, LyricsText::Beside);
    QCOMPARE(firstPlain.plain, QStringList{QStringLiteral("beside plain")});

    QVERIFY(QFile::remove(m_dir->filePath(QStringLiteral("plain.lrc"))));
    QCOMPARE(Lyrics::load(plainTag).source, LyricsText::Tag);

    const QString none = flac(QStringLiteral("none.flac"));
    const LyricsText empty = Lyrics::load(none);
    QVERIFY(empty.empty());
    QCOMPARE(empty.source, LyricsText::None);
}

void LyricsTest::storeSlot()
{
    const QString audio = flac(QStringLiteral("store.flac"));
    QVERIFY(xiphLyrics(audio, "LYRICS", {QStringLiteral("tag plain")}));
    const QString store = m_dir->filePath(QStringLiteral("store.lrc.stored"));
    QVERIFY(writeBytes(store, QByteArrayLiteral("[00:09.00]stored\n")));
    const LyricsText stored = Lyrics::load(audio, store);
    QCOMPARE(stored.source, LyricsText::Store);
    QCOMPARE(linesOf(stored), QStringLiteral("9000 stored"));

    QVERIFY(writeBytes(store, QByteArrayLiteral("store plain\n")));
    QCOMPARE(Lyrics::load(audio, store).source, LyricsText::Tag);

    const QString bare = flac(QStringLiteral("bare.flac"));
    const LyricsText fromStore = Lyrics::load(bare, store);
    QCOMPARE(fromStore.source, LyricsText::Store);
    QCOMPARE(fromStore.plain, QStringList{QStringLiteral("store plain")});
}

void LyricsTest::storePutAndDel()
{
    const QString timedTag = flac(QStringLiteral("timedtag.flac"));
    QVERIFY(xiphLyrics(timedTag, "LYRICS", {QStringLiteral("[00:03.00]tag")}));
    QCOMPARE(Lyrics::storePut(timedTag, QByteArrayLiteral("store plain\n")), Lyrics::Put::Ok);
    QVERIFY(Lyrics::storeFile(timedTag).startsWith(Lyrics::storeRoot() + QLatin1Char('/')));
    QVERIFY(QFileInfo::exists(Lyrics::storeFile(timedTag)));
    QCOMPARE(Lyrics::load(timedTag, Lyrics::storeFile(timedTag)).source, LyricsText::Tag);

    const QString audio = flac(QStringLiteral("put.flac"));
    QCOMPARE(Lyrics::storePut(audio, QByteArrayLiteral("  \n\n")), Lyrics::Put::Empty);
    QCOMPARE(Lyrics::storePut(audio, QByteArrayLiteral("[00:01.00]one\n")), Lyrics::Put::Ok);
    QCOMPARE(Lyrics::storePut(audio, QByteArrayLiteral("plain over synced\n")), Lyrics::Put::Synced);
    QCOMPARE(linesOf(Lyrics::load(audio, Lyrics::storeFile(audio))), QStringLiteral("1000 one"));
    QCOMPARE(Lyrics::storePut(audio, QByteArrayLiteral("[00:02.00]two\n")), Lyrics::Put::Ok);
    QCOMPARE(linesOf(Lyrics::load(audio, Lyrics::storeFile(audio))), QStringLiteral("2000 two"));

    Lyrics lyrics;
    lyrics.setTracks(audio, QString());
    QTRY_VERIFY_WITH_TIMEOUT(!lyrics.pending(), loadTimeoutMs);
    QCOMPARE(lyrics.current().source, LyricsText::Store);
    QVERIFY(Lyrics::storeDel(audio));
    QVERIFY(!QFileInfo::exists(Lyrics::storeFile(audio)));
    QVERIFY(Lyrics::storeDel(audio));
    lyrics.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(lyrics.current().empty(), loadTimeoutMs);
    lyrics.waitForDone();

    {
        QFile old(Lyrics::storeFile(timedTag));
        QVERIFY(old.open(QIODevice::ReadWrite));
        QVERIFY(old.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-60), QFileDevice::FileModificationTime));
    }
    QCOMPARE(Lyrics::storePut(audio, QByteArrayLiteral("[00:02.00]two\n")), Lyrics::Put::Ok);
    const QString third = flac(QStringLiteral("third.flac"));
    QCOMPARE(Lyrics::storePut(third, QByteArrayLiteral("[00:03.00]three\n")), Lyrics::Put::Ok);
    Lyrics::storeTrim(third, QFileInfo(Lyrics::storeFile(audio)).size() + QFileInfo(Lyrics::storeFile(third)).size());
    QVERIFY(!QFileInfo::exists(Lyrics::storeFile(timedTag)));
    QVERIFY(QFileInfo::exists(Lyrics::storeFile(audio)));
    QVERIFY(QFileInfo::exists(Lyrics::storeFile(third)));
    Lyrics::storeTrim(third, 0);
    QVERIFY(!QFileInfo::exists(Lyrics::storeFile(audio)));
    QVERIFY(QFileInfo::exists(Lyrics::storeFile(third)));

    const QString wide = QString(4095, QLatin1Char('a')) + QChar(0x3042);
    const QByteArray cut = Lyrics::cutLine(wide);
    QCOMPARE(cut.size(), 4095);
    QCOMPARE(Lyrics::cutLine(QStringLiteral("short")), QByteArrayLiteral("short"));
}

void LyricsTest::tracks()
{
    const QString one = flac(QStringLiteral("one.flac"));
    const QString two = flac(QStringLiteral("two.flac"));
    const QString three = flac(QStringLiteral("three.flac"));
    QVERIFY(writeBytes(m_dir->filePath(QStringLiteral("one.lrc")), QByteArrayLiteral("[00:01.00]one\n")));
    QVERIFY(writeBytes(m_dir->filePath(QStringLiteral("two.lrc")), QByteArrayLiteral("two words\n")));

    Lyrics lyrics;
    QSignalSpy changed(&lyrics, &Lyrics::changed);
    QVERIFY(!lyrics.pending());

    lyrics.setTracks(one, two);
    QVERIFY(lyrics.pending());
    QTRY_VERIFY_WITH_TIMEOUT(!lyrics.pending(), loadTimeoutMs);
    QCOMPARE(int(lyrics.lines().size()), 1);
    QCOMPARE(lyrics.lines().constFirst().toMap().value(QStringLiteral("text")).toString(), QStringLiteral("one"));
    QCOMPARE(lyrics.lineAt(1500), 0);
    lyrics.waitForDone();
    QCoreApplication::processEvents();

    changed.clear();
    lyrics.setTracks(two, three);
    QVERIFY(!lyrics.pending());
    QCOMPARE(changed.count(), 1);
    QVERIFY(lyrics.lines().isEmpty());
    QCOMPARE(lyrics.plain(), QStringList{QStringLiteral("two words")});

    lyrics.setTracks(three, QString());
    QTRY_VERIFY_WITH_TIMEOUT(!lyrics.pending(), loadTimeoutMs);
    QVERIFY(lyrics.current().empty());

    lyrics.setTracks(QString(), QString());
    QVERIFY(!lyrics.pending());
    QVERIFY(lyrics.current().empty());

    lyrics.setTracks(one, QString());
    lyrics.setTracks(two, QString());
    QTRY_VERIFY_WITH_TIMEOUT(!lyrics.pending(), loadTimeoutMs);
    lyrics.waitForDone();
    QCoreApplication::processEvents();
    QCOMPARE(lyrics.plain(), QStringList{QStringLiteral("two words")});
}

void LyricsTest::refresh()
{
    const QString audio = flac(QStringLiteral("fresh.flac"));
    QVERIFY(writeBytes(m_dir->filePath(QStringLiteral("fresh.lrc")), QByteArrayLiteral("old\n")));
    Lyrics lyrics;
    lyrics.setTracks(audio, QString());
    QTRY_VERIFY_WITH_TIMEOUT(!lyrics.pending(), loadTimeoutMs);
    QCOMPARE(lyrics.plain(), QStringList{QStringLiteral("old")});

    QVERIFY(writeBytes(m_dir->filePath(QStringLiteral("fresh.lrc")), QByteArrayLiteral("[00:01.00]new\n")));
    lyrics.refresh();
    QVERIFY(!lyrics.pending());
    QCOMPARE(lyrics.plain(), QStringList{QStringLiteral("old")});
    QTRY_COMPARE_WITH_TIMEOUT(int(lyrics.lines().size()), 1, loadTimeoutMs);
    QVERIFY(lyrics.plain().isEmpty());
}

void LyricsTest::cueSpan()
{
    const LyricsText all = Lyrics::parse(QStringLiteral("[00:01.00]one\n[00:04.00]four\n[00:05.00]five\n"
                                                        "[00:09.00]nine\n"));
    QCOMPARE(linesOf(Lyrics::span(all, 0, 4500)), QStringLiteral("1000 one|4000 four"));
    QCOMPARE(linesOf(Lyrics::span(all, 4500, 9000)), QStringLiteral("0 four|500 five"));
    QCOMPARE(linesOf(Lyrics::span(all, 5000, 9000)), QStringLiteral("0 five"));
    QCOMPARE(linesOf(Lyrics::span(all, 6000, 0)), QStringLiteral("0 five|3000 nine"));
    QVERIFY(Lyrics::span(all, 0, 500).empty());
    QCOMPARE(Lyrics::span(all, 0, 500).source, LyricsText::None);
    const LyricsText plain = Lyrics::parse(QStringLiteral("just words\n"));
    QCOMPARE(Lyrics::span(plain, 1000, 2000).plain, QStringList{QStringLiteral("just words")});

    const QString audio = flac(QStringLiteral("image.flac"));
    QVERIFY(!audio.isEmpty());
    QVERIFY(xiphLyrics(audio, "LYRICS", {QStringLiteral("[00:00.50]tag line")}));
    QVERIFY(writeBytes(m_dir->filePath(QStringLiteral("image.lrc")), QByteArrayLiteral("[00:01.00]a\n[00:03.00]b\n")));
    QCOMPARE(linesOf(Lyrics::load(LyricsTrack{audio, 2000, 0, true})), QStringLiteral("0 a|1000 b"));
    QCOMPARE(linesOf(Lyrics::load(LyricsTrack{audio, 0, 0, false})), QStringLiteral("1000 a|3000 b"));
    QVERIFY(QFile::remove(m_dir->filePath(QStringLiteral("image.lrc"))));
    QVERIFY(Lyrics::load(LyricsTrack{audio, 2000, 0, true}).empty());
    QCOMPARE(linesOf(Lyrics::load(LyricsTrack{audio, 0, 0, false})), QStringLiteral("500 tag line"));

    QVERIFY(writeBytes(m_dir->filePath(QStringLiteral("image.lrc")), QByteArrayLiteral("[00:01.00]a\n[00:03.00]b\n")));
    Lyrics lyrics;
    QSignalSpy changed(&lyrics, &Lyrics::changed);
    lyrics.setTracks(LyricsTrack{audio, 0, 2000, true}, LyricsTrack{audio, 2000, 0, true});
    QTRY_VERIFY(!lyrics.pending());
    QCOMPARE(linesOf(lyrics.current()), QStringLiteral("1000 a"));
    lyrics.setTracks(LyricsTrack{audio, 2000, 0, true}, LyricsTrack{});
    QTRY_VERIFY(!lyrics.pending());
    QCOMPARE(linesOf(lyrics.current()), QStringLiteral("0 a|1000 b"));
    lyrics.waitForDone();
}

QTEST_GUILESS_MAIN(LyricsTest)
#include "tst_lyrics.moc"
