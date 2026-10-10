#include "Eq.h"
#include "OutputControl.h"
#include "Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <memory>

using namespace casdap;

namespace {

constexpr int waitMs = 5000;

const char *autoEq =
    "Preamp: -6.2 dB\n"
    "Filter 1: ON LSC Fc 105 Hz Gain 6.3 dB Q 0.70\n"
    "Filter 2: ON PK Fc 1000 Hz Gain -3.2 dB Q 1.41\n"
    "Filter 3: ON HSC Fc 10000 Hz Gain 2.0 dB Q 0.70\n"
    "Filter 4: OFF PK Fc 200 Hz Gain 1.0 dB Q 1.00\n"
    "filter 5:   on  pk  fc 300 hz gain 1 db q 2\n";

const char *sinks =
    "47\talsa_output.platform-sound.HiFi__Speaker__sink\tPipeWire\ts24le 2ch 48000Hz\tRUNNING\n"
    "48\talsa_output.platform-sound.HiFi__Headphones__sink\tPipeWire\ts24le 2ch 48000Hz\tSUSPENDED\n";

const char *jackOn =
    "numid=127,iface=CARD,name='Headphone Jack'\n"
    "  ; type=BOOLEAN,access=r-------,values=1\n"
    "  : values=on\n";

bool writeFile(const QString &path, const QString &text, bool executable = false)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(text.toUtf8());
    file.close();
    if (!executable)
        return true;
    return file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
}

QString readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

bool near(double a, double b)
{
    return std::abs(a - b) < 1e-6;
}

QString filters(int count)
{
    QString text;
    for (int i = 1; i <= count; ++i)
        text += QStringLiteral("Filter %1: ON PK Fc %2 Hz Gain 1.0 dB Q 1.0\n").arg(i).arg(i * 100);
    return text;
}

}

class EqTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void parsesTypes();
    void displayNames();
    void fallbackPreamp();
    void keepsFirstTen();
    void skipsMalformed();
    void zeroFiltersFails();
    void clamps();
    void bandwidthClamp();
    void grid();
    void storeRoundTrip();
    void copyOnEdit();
    void steps();
    void cycleProfile();
    void activeOutputPicksKey();
    void followsOutputControl();
    void bypass();
    void missingFile();
    void sinkPoll();

private:
    QString m_bin;
    QString m_dir;
    std::unique_ptr<QTemporaryDir> m_temp;
    QTemporaryDir m_binDir;
};

void EqTest::initTestCase()
{
    QVERIFY(m_binDir.isValid());
    m_bin = m_binDir.path() + QStringLiteral("/bin");
    const QString sinksFile = m_binDir.path() + QStringLiteral("/sinks.txt");
    const QString jackFile = m_binDir.path() + QStringLiteral("/jack.txt");
    QVERIFY(writeFile(sinksFile, QString::fromUtf8(sinks)));
    QVERIFY(writeFile(jackFile, QString::fromUtf8(jackOn)));
    QVERIFY(writeFile(m_bin + QStringLiteral("/pactl"),
                      QStringLiteral("#!/bin/sh\n[ \"$1\" = list ] && cat \"") + sinksFile
                          + QStringLiteral("\"\n[ \"$1\" = get-default-sink ] && echo ")
                          + OutputControl::headphonesSink() + QStringLiteral("\nexit 0\n"),
                      true));
    QVERIFY(writeFile(m_bin + QStringLiteral("/amixer"),
                      QStringLiteral("#!/bin/sh\ncat \"") + jackFile + QStringLiteral("\"\n"), true));
    QVERIFY(writeFile(m_bin + QStringLiteral("/systemctl"), QStringLiteral("#!/bin/sh\nexit 0\n"), true));
    qputenv("CASDAP_PACTL_BIN", (m_bin + QStringLiteral("/pactl")).toUtf8());
    qputenv("CASDAP_AMIXER_BIN", (m_bin + QStringLiteral("/amixer")).toUtf8());
    qputenv("CASDAP_SYSTEMCTL_BIN", (m_bin + QStringLiteral("/systemctl")).toUtf8());
    qputenv("CASDAP_WPCTL_BIN", (m_bin + QStringLiteral("/systemctl")).toUtf8());
}

void EqTest::init()
{
    m_temp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_temp->isValid());
    m_dir = m_temp->path();
    qputenv("CASDAP_EQ_DIR", (m_dir + QStringLiteral("/config")).toUtf8());
    QCOMPARE(EqControl::sourceDir(), m_dir + QStringLiteral("/config/eq"));
    QCOMPARE(EqControl::storePath(QStringLiteral("usb")), m_dir + QStringLiteral("/config/eq-usb.ini"));
}

void EqTest::parsesTypes()
{
    QString error;
    const std::optional<EqProfile> profile = EqParser::parse(QString::fromUtf8(autoEq), &error);
    QVERIFY(profile);
    QVERIFY(error.isEmpty());
    QVERIFY(near(profile->preamp, -6.2));
    const EqBand &b1 = profile->bands[0];
    QVERIFY(b1.enabled);
    QCOMPARE(b1.type, EqBand::LowShelf);
    QVERIFY(near(b1.freq, 105.0));
    QVERIFY(near(b1.gain, 6.3));
    QVERIFY(near(b1.q, 0.7));
    QCOMPARE(b1.freqIndex, -1);
    QCOMPARE(profile->bands[1].type, EqBand::Peak);
    QVERIFY(near(profile->bands[1].gain, -3.2));
    QCOMPARE(profile->bands[2].type, EqBand::HighShelf);
    QVERIFY(!profile->bands[3].enabled);
    QVERIFY(near(profile->bands[3].freq, 200.0));
    QVERIFY(profile->bands[4].enabled);
    QVERIFY(near(profile->bands[4].q, 2.0));
    for (int i = 5; i < EqProfile::bandCount; ++i)
        QVERIFY(!profile->bands[i].enabled);

    const std::optional<EqProfile> aliases = EqParser::parse(
        QStringLiteral("Filter 1: ON LS Fc 80 Hz Gain 1 dB Q 0.7\nFilter 2: ON HS Fc 8000 Hz Gain 1 dB Q 0.7\n"));
    QVERIFY(aliases);
    QCOMPARE(aliases->bands[0].type, EqBand::LowShelf);
    QCOMPARE(aliases->bands[1].type, EqBand::HighShelf);
}

void EqTest::fallbackPreamp()
{
    const std::optional<EqProfile> sum = EqParser::parse(QStringLiteral(
        "Filter 1: ON PK Fc 100 Hz Gain 3 dB Q 1\n"
        "Filter 2: ON PK Fc 200 Hz Gain 4 dB Q 1\n"
        "Filter 3: ON PK Fc 300 Hz Gain -2 dB Q 1\n"
        "Filter 4: OFF PK Fc 400 Hz Gain 5 dB Q 1\n"));
    QVERIFY(sum);
    QVERIFY(near(sum->preamp, -7.0));

    const std::optional<EqProfile> big = EqParser::parse(QStringLiteral(
        "Filter 1: ON PK Fc 100 Hz Gain 12 dB Q 1\n"
        "Filter 2: ON PK Fc 200 Hz Gain 12 dB Q 1\n"
        "Filter 3: ON PK Fc 300 Hz Gain 12 dB Q 1\n"));
    QVERIFY(big);
    QVERIFY(near(big->preamp, -24.0));

    const std::optional<EqProfile> cut = EqParser::parse(QStringLiteral("Filter 1: ON PK Fc 100 Hz Gain -3 dB Q 1\n"));
    QVERIFY(cut);
    QVERIFY(near(cut->preamp, 0.0));
}

void EqTest::keepsFirstTen()
{
    const std::optional<EqProfile> profile = EqParser::parse(QStringLiteral("Preamp: -1 dB\n") + filters(12));
    QVERIFY(profile);
    for (int i = 0; i < EqProfile::bandCount; ++i) {
        QVERIFY(profile->bands[i].enabled);
        QVERIFY(near(profile->bands[i].freq, (i + 1) * 100.0));
    }
}

void EqTest::skipsMalformed()
{
    const std::optional<EqProfile> profile = EqParser::parse(QStringLiteral(
        "# AutoEQ\n"
        "Preamp: loud\n"
        "Filter 1: ON PK Fc abc Hz Gain 1 dB Q 1\n"
        "Filter 2: ON XX Fc 100 Hz Gain 1 dB Q 1\n"
        "Filter 3: ON PK Fc 250 Hz Gain 2.5 dB Q 1.2\n"
        "Filter 4: ON PK Fc 300 Hz Gain dB Q 1\n"));
    QVERIFY(profile);
    QVERIFY(profile->bands[0].enabled);
    QVERIFY(near(profile->bands[0].freq, 250.0));
    QVERIFY(near(profile->preamp, -2.5));
    for (int i = 1; i < EqProfile::bandCount; ++i)
        QVERIFY(!profile->bands[i].enabled);
}

void EqTest::zeroFiltersFails()
{
    QString error;
    QVERIFY(!EqParser::parse(QStringLiteral("Preamp: -3 dB\nhello\n"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!EqParser::parse(QString(), &error));
    QVERIFY(!EqParser::parseFile(m_dir + QStringLiteral("/absent.txt"), &error));
    QVERIFY(!error.isEmpty());
}

void EqTest::clamps()
{
    const std::optional<EqProfile> profile = EqParser::parse(QStringLiteral(
        "Preamp: 3 dB\n"
        "Filter 1: ON PK Fc 5 Hz Gain 20 dB Q 0.01\n"
        "Filter 2: ON PK Fc 30000 Hz Gain -30 dB Q 50\n"));
    QVERIFY(profile);
    QVERIFY(near(profile->preamp, 0.0));
    QVERIFY(near(profile->bands[0].freq, 20.0));
    QVERIFY(near(profile->bands[0].gain, 12.0));
    QVERIFY(near(profile->bands[0].q, 0.1));
    QVERIFY(near(profile->bands[1].freq, 20000.0));
    QVERIFY(near(profile->bands[1].gain, -24.0));
    QVERIFY(near(profile->bands[1].q, 10.0));

    const std::optional<EqProfile> low = EqParser::parse(
        QStringLiteral("Preamp: -40 dB\nFilter 1: ON PK Fc 100 Hz Gain 1 dB Q 1\n"));
    QVERIFY(low);
    QVERIFY(near(low->preamp, -24.0));
}

void EqTest::bandwidthClamp()
{
    EqBand band;
    band.freq = 20000.0;
    band.q = 0.1;
    QVERIFY(near(EqProfile::bandwidth(band), EqProfile::maxBandwidth));
    band.freq = 1000.0;
    band.q = 2.0;
    QVERIFY(near(EqProfile::bandwidth(band), 500.0));
    EqProfile profile;
    profile.preamp = -6.0;
    QVERIFY(std::abs(profile.preampVolume() - std::pow(10.0, -0.3)) < 1e-9);
}

void EqTest::grid()
{
    QCOMPARE(EqGrid::frequency(0), 20.0);
    QCOMPARE(EqGrid::frequency(34), 1000.0);
    QCOMPARE(EqGrid::frequency(60), 20000.0);
    QVERIFY(near(EqGrid::frequency(4), 31.25));
    QVERIFY(near(EqGrid::frequency(58), 16000.0));
    QVERIFY(EqGrid::frequency(1) > 20.0);
    QVERIFY(EqGrid::frequency(59) < 20000.0);

    EqBand band;
    band.freq = 105.0;
    band.freqIndex = -1;
    QCOMPARE(EqGrid::step(band, 1), 15);
    QCOMPARE(EqGrid::step(band, -1), 14);
    QCOMPARE(EqGrid::step(band, 1, 6), 20);
    QCOMPARE(EqGrid::step(band, -1, 6), 9);
    band.freqIndex = 58;
    QCOMPARE(EqGrid::step(band, 1, 6), 60);
    band.freqIndex = 3;
    QCOMPARE(EqGrid::step(band, -1, 6), 0);
    band.freqIndex = 28;
    QCOMPARE(EqGrid::step(band, 1, 6), 34);
    QCOMPARE(EqGrid::step(band, -1, 6), 22);
    band.freqIndex = 60;
    QCOMPARE(EqGrid::step(band, 1), 60);
    band.freqIndex = 0;
    QCOMPARE(EqGrid::step(band, -1), 0);

    const EqProfile flat = EqProfile::flat();
    const QList<int> indexes{4, 10, 16, 22, 28, 34, 40, 46, 52, 58};
    for (int i = 0; i < EqProfile::bandCount; ++i) {
        QCOMPARE(flat.bands[i].freqIndex, indexes.at(i));
        QVERIFY(flat.bands[i].enabled);
        QCOMPARE(flat.bands[i].type, EqBand::Peak);
        QCOMPARE(flat.bands[i].gain, 0.0);
        QCOMPARE(flat.bands[i].q, 1.4);
    }
}

void EqTest::storeRoundTrip()
{
    EqProfile profile = *EqParser::parse(QString::fromUtf8(autoEq));
    profile.bands[5].freqIndex = 40;
    profile.bands[5].freq = EqGrid::frequency(40);
    profile.bands[5].enabled = true;
    profile.bands[5].gain = -1.5;
    const QString path = m_dir + QStringLiteral("/store/eq-usb.ini");
    QVERIFY(EqStore::write(path, profile));
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".tmp")));
    const QString text = readFile(path);
    QVERIFY(text.contains(QStringLiteral("[band1]\ntype=lowshelf\nfreq=105\n")));
    QVERIFY(text.contains(QStringLiteral("[band4]\ntype=off\n")));
    QVERIFY(text.contains(QStringLiteral("[band6]\ntype=peak\nfreqIndex=40\n")));
    QVERIFY(EqStore::read(path) == profile);

    QVERIFY(EqStore::read(m_dir + QStringLiteral("/none.ini")) == EqProfile::flat());
    QVERIFY(writeFile(m_dir + QStringLiteral("/bad.ini"), QStringLiteral("[General]\npreamp=x\n")));
    QVERIFY(EqStore::read(m_dir + QStringLiteral("/bad.ini")) == EqProfile::flat());
}

void EqTest::copyOnEdit()
{
    const QString file = EqControl::sourceDir() + QStringLiteral("/HD600 ParametricEQ.txt");
    QVERIFY(writeFile(file, QString::fromUtf8(autoEq)));
    Settings settings(m_dir + QStringLiteral("/settings.ini"));
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    QCOMPARE(int(eq.sources().size()), 1);
    QCOMPARE(eq.sources().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("HD600"));

    settings.setEqHeadphones(file);
    QCOMPARE(eq.key(QStringLiteral("headphones")), file);
    QCOMPARE(eq.name(QStringLiteral("headphones")), QStringLiteral("HD600"));
    QVERIFY(eq.editable(QStringLiteral("headphones")));

    eq.stepGain(QStringLiteral("headphones"), 0, 1);
    QCOMPARE(settings.eqHeadphones(), QStringLiteral("custom"));
    QCOMPARE(eq.name(QStringLiteral("headphones")), QStringLiteral("CUSTOM"));
    const EqProfile edited = eq.profile(QStringLiteral("headphones"));
    QVERIFY(near(edited.bands[0].gain, 6.4));
    QVERIFY(near(edited.bands[0].freq, 105.0));
    QCOMPARE(edited.bands[0].freqIndex, -1);
    QVERIFY(near(edited.preamp, -6.2));
    QCOMPARE(readFile(file), QString::fromUtf8(autoEq));

    eq.stepFrequency(QStringLiteral("headphones"), 0, 1);
    QCOMPARE(eq.profile(QStringLiteral("headphones")).bands[0].freqIndex, 15);
    eq.sync();
    const EqProfile stored = EqStore::read(EqControl::storePath(QStringLiteral("headphones")));
    QVERIFY(stored == eq.profile(QStringLiteral("headphones")));
    QCOMPARE(readFile(file), QString::fromUtf8(autoEq));

    settings.setEqSpeaker(QStringLiteral("off"));
    eq.stepGain(QStringLiteral("speaker"), 0, 1);
    QCOMPARE(settings.eqSpeaker(), QStringLiteral("off"));
    QVERIFY(!QFileInfo::exists(EqControl::storePath(QStringLiteral("speaker"))));
}

void EqTest::steps()
{
    EqControl eq(nullptr);
    const QString out = QStringLiteral("usb");
    eq.setCustom(out, EqProfile::flat());
    QCOMPARE(eq.key(out), QStringLiteral("custom"));

    eq.stepPreamp(out, 1);
    QVERIFY(near(eq.profile(out).preamp, 0.0));
    eq.stepPreamp(out, -1);
    QVERIFY(near(eq.profile(out).preamp, -0.1));
    eq.stepPreamp(out, -1, true);
    QVERIFY(near(eq.profile(out).preamp, -1.0));
    eq.stepPreamp(out, -1);
    QVERIFY(near(eq.profile(out).preamp, -1.1));
    eq.stepPreamp(out, 1, true);
    QVERIFY(near(eq.profile(out).preamp, -1.0));
    EqProfile odd = EqProfile::flat();
    odd.bands[0].gain = 2.1;
    odd.bands[0].q = 0.75;
    odd.bands[2].gain = -3.37;
    odd.bands[2].q = 9.95;
    odd.preamp = -24.0;
    eq.setCustom(out, odd);
    eq.stepPreamp(out, -1);
    QVERIFY(near(eq.profile(out).preamp, -24.0));
    eq.stepPreamp(out, -1, true);
    QVERIFY(near(eq.profile(out).preamp, -24.0));
    eq.stepGain(out, 0, 1);
    QVERIFY(near(eq.profile(out).bands[0].gain, 2.2));
    eq.stepGain(out, 0, -1);
    QVERIFY(near(eq.profile(out).bands[0].gain, 2.1));
    eq.stepGain(out, 0, 1, true);
    QVERIFY(near(eq.profile(out).bands[0].gain, 3.0));
    eq.stepGain(out, 0, -1, true);
    QVERIFY(near(eq.profile(out).bands[0].gain, 2.0));
    eq.stepGain(out, 2, 1);
    QCOMPARE(eq.profile(out).bands[2].gain, -3.3);
    eq.stepGain(out, 2, -1, true);
    QCOMPARE(eq.profile(out).bands[2].gain, -4.0);
    for (int i = 0; i < 20; ++i)
        eq.stepGain(out, 2, 1, true);
    QCOMPARE(eq.profile(out).bands[2].gain, EqParser::maxGain);
    eq.stepQ(out, 0, 1);
    QCOMPARE(eq.profile(out).bands[0].q, 0.8);
    eq.stepQ(out, 0, -1);
    QCOMPARE(eq.profile(out).bands[0].q, 0.7);
    eq.stepQ(out, 0, 1, true);
    QCOMPARE(eq.profile(out).bands[0].q, 1.0);
    eq.stepQ(out, 0, 1, true);
    QCOMPARE(eq.profile(out).bands[0].q, 2.0);
    eq.stepQ(out, 0, -1, true);
    QCOMPARE(eq.profile(out).bands[0].q, 1.0);
    eq.stepQ(out, 0, -1, true);
    QCOMPARE(eq.profile(out).bands[0].q, EqParser::minQ);
    eq.stepQ(out, 0, -1);
    QCOMPARE(eq.profile(out).bands[0].q, EqParser::minQ);
    eq.stepQ(out, 2, 1);
    QCOMPARE(eq.profile(out).bands[2].q, 10.0);
    eq.stepQ(out, 2, 1, true);
    QCOMPARE(eq.profile(out).bands[2].q, EqParser::maxQ);
    eq.stepQ(out, 2, -1);
    QCOMPARE(eq.profile(out).bands[2].q, 9.9);

    eq.stepFrequency(out, 3, 1);
    QCOMPARE(eq.profile(out).bands[3].freqIndex, 23);
    eq.stepFrequency(out, 3, 1, true);
    QCOMPARE(eq.profile(out).bands[3].freqIndex, 29);
    eq.stepFrequency(out, 3, -1, true);
    QCOMPARE(eq.profile(out).bands[3].freqIndex, 23);
    eq.stepBand(out, 3, QStringLiteral("freq"), -1, true);
    QCOMPARE(eq.profile(out).bands[3].freqIndex, 17);
    QCOMPARE(eq.profile(out).bands[3].freq, EqGrid::frequency(17));
    eq.stepBand(out, 3, QStringLiteral("gain"), 1, true);
    QCOMPARE(eq.profile(out).bands[3].gain, 1.0);
    eq.stepBand(out, 3, QStringLiteral("gain"), 1);
    QCOMPARE(eq.profile(out).bands[3].gain, 1.1);
    eq.stepBand(out, 3, QStringLiteral("q"), 1, true);
    QCOMPARE(eq.profile(out).bands[3].q, 2.0);
    eq.stepBand(out, 3, QStringLiteral("q"), -1);
    QCOMPARE(eq.profile(out).bands[3].q, 1.9);
    eq.stepBand(out, 3, QStringLiteral("type"), 1, true);
    QCOMPARE(eq.profile(out).bands[3].type, EqBand::LowShelf);
    eq.stepBand(out, 3, QStringLiteral("type"), -1);
    QCOMPARE(eq.profile(out).bands[3].type, EqBand::Peak);
    QVERIFY(EqStore::text(eq.profile(out)).contains(QStringLiteral("[band4]\ntype=peak\nfreqIndex=17\ngain=1.1\nq=1.9\n")));

    eq.stepType(out, 1, 1);
    QCOMPARE(eq.profile(out).bands[1].type, EqBand::LowShelf);
    eq.stepType(out, 1, 1);
    QCOMPARE(eq.profile(out).bands[1].type, EqBand::HighShelf);
    eq.stepType(out, 1, 1);
    QVERIFY(!eq.profile(out).bands[1].enabled);
    eq.stepType(out, 1, 1);
    QVERIFY(eq.profile(out).bands[1].enabled);
    QCOMPARE(eq.profile(out).bands[1].type, EqBand::Peak);
    eq.stepType(out, 1, -1);
    QVERIFY(!eq.profile(out).bands[1].enabled);
}

void EqTest::cycleProfile()
{
    const QString a = EqControl::sourceDir() + QStringLiteral("/a.txt");
    const QString b = m_dir + QStringLiteral("/music/EQ/B.txt");
    QVERIFY(writeFile(a, QString::fromUtf8(autoEq)));
    QVERIFY(writeFile(b, QString::fromUtf8(autoEq)));
    QVERIFY(writeFile(m_dir + QStringLiteral("/music/EQ/broken.txt"), QStringLiteral("nothing\n")));
    QVERIFY(writeFile(m_dir + QStringLiteral("/music/EQ/notes.md"), QString::fromUtf8(autoEq)));
    Settings settings(m_dir + QStringLiteral("/settings.ini"));
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    QCOMPARE(int(eq.sources().size()), 2);
    QCOMPARE(eq.sources().at(0).toMap().value(QStringLiteral("path")).toString(), a);
    QCOMPARE(eq.sources().at(1).toMap().value(QStringLiteral("path")).toString(), b);

    const QString out = QStringLiteral("speaker");
    QCOMPARE(eq.key(out), QStringLiteral("off"));
    eq.cycleProfile(out, 1);
    QCOMPARE(eq.key(out), a);
    eq.cycleProfile(out, 1);
    QCOMPARE(eq.key(out), b);
    eq.cycleProfile(out, 1);
    QCOMPARE(eq.key(out), QStringLiteral("custom"));
    eq.cycleProfile(out, 1);
    QCOMPARE(eq.key(out), QStringLiteral("off"));
    eq.cycleProfile(out, -1);
    QCOMPARE(eq.key(out), QStringLiteral("custom"));

    settings.setEqSpeaker(m_dir + QStringLiteral("/gone.txt"));
    QVERIFY(eq.missing(out));
    eq.cycleProfile(out, 1);
    QCOMPARE(eq.key(out), QStringLiteral("off"));
    settings.setEqSpeaker(m_dir + QStringLiteral("/gone.txt"));
    QVERIFY(eq.missing(out));
    eq.cycleProfile(out, -1);
    QCOMPARE(eq.key(out), QStringLiteral("custom"));
}

void EqTest::activeOutputPicksKey()
{
    const QString file = EqControl::sourceDir() + QStringLiteral("/hp.txt");
    QVERIFY(writeFile(file, QString::fromUtf8(autoEq)));
    Settings settings(m_dir + QStringLiteral("/settings.ini"));
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    EqProfile last;
    int count = 0;
    connect(&eq, &EqControl::profileApplied, this, [&last, &count](const EqProfile &profile) {
        last = profile;
        ++count;
    });
    settings.setEqHeadphones(file);
    QCOMPARE(count, 0);
    EqProfile custom = EqProfile::flat();
    custom.bands[2].gain = 3.0;
    eq.setCustom(QStringLiteral("usb"), custom);
    QCOMPARE(count, 0);

    eq.setActiveOutput(QStringLiteral("headphones"));
    QCOMPARE(count, 1);
    QVERIFY(near(last.preamp, -6.2));
    QCOMPARE(last.bands[0].type, EqBand::LowShelf);
    QCOMPARE(eq.activeName(), QStringLiteral("HP"));

    eq.setActiveOutput(QStringLiteral("speaker"));
    QCOMPARE(count, 2);
    QVERIFY(last == EqProfile::off());

    eq.setActiveOutput(QStringLiteral("usb"));
    QCOMPARE(count, 3);
    QVERIFY(last == custom);

    settings.setEqHeadphones(QStringLiteral("off"));
    QCOMPARE(count, 3);
    eq.stepGain(QStringLiteral("usb"), 2, 1);
    QCOMPARE(count, 4);
    QVERIFY(near(last.bands[2].gain, 3.1));
    settings.setEqUsb(QStringLiteral("off"));
    QCOMPARE(count, 5);
    QVERIFY(last == EqProfile::off());
}

void EqTest::followsOutputControl()
{
    QCOMPARE(EqControl::outputForSink(OutputControl::headphonesSink()), QStringLiteral("headphones"));
    QCOMPARE(EqControl::outputForSink(OutputControl::speakerSink()), QStringLiteral("speaker"));
    QCOMPARE(EqControl::outputForSink(QStringLiteral("alsa_output.usb-DAC.analog-stereo")), QStringLiteral("usb"));
    QCOMPARE(EqControl::defaultSinkOutput(), QStringLiteral("headphones"));

    Settings settings(m_dir + QStringLiteral("/settings.ini"));
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    EqProfile custom = EqProfile::flat();
    custom.bands[0].gain = -4.0;
    eq.setCustom(QStringLiteral("speaker"), custom);
    EqProfile last;
    connect(&eq, &EqControl::profileApplied, this, [&last](const EqProfile &profile) { last = profile; });

    OutputControl control;
    eq.setOutputControl(&control);
    QSignalSpy applied(&control, &OutputControl::applied);
    control.apply(QStringLiteral("speaker"));
    QVERIFY(applied.wait(waitMs));
    QCOMPARE(eq.activeOutput(), QStringLiteral("speaker"));
    QVERIFY(last == custom);

    control.apply(QStringLiteral("auto"));
    QTRY_COMPARE_WITH_TIMEOUT(eq.activeOutput(), QStringLiteral("headphones"), waitMs);
    QVERIFY(last == EqProfile::off());
}

void EqTest::bypass()
{
    Settings settings(m_dir + QStringLiteral("/settings.ini"));
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    EqProfile custom = EqProfile::flat();
    custom.bands[1].gain = 2.0;
    eq.setCustom(QStringLiteral("usb"), custom);
    EqProfile last;
    int count = 0;
    connect(&eq, &EqControl::profileApplied, this, [&last, &count](const EqProfile &profile) {
        last = profile;
        ++count;
    });
    QVERIFY(!eq.bypassed());
    eq.setBypassed(true);
    QVERIFY(eq.bypassed());
    eq.setBypassed(false);
    QCOMPARE(count, 0);

    eq.setActiveOutput(QStringLiteral("usb"));
    QCOMPARE(count, 1);
    QVERIFY(last == custom);

    eq.setBypassed(true);
    eq.setBypassed(true);
    QCOMPARE(count, 1);
    eq.stepGain(QStringLiteral("usb"), 1, 1);
    eq.setActiveOutput(QStringLiteral("speaker"));
    eq.setActiveOutput(QStringLiteral("usb"));
    QCOMPARE(count, 1);
    QVERIFY(near(eq.activeProfile().bands[1].gain, 2.1));

    eq.setBypassed(false);
    QVERIFY(!eq.bypassed());
    QCOMPARE(count, 2);
    QVERIFY(near(last.bands[1].gain, 2.1));
    eq.setBypassed(false);
    QCOMPARE(count, 2);

    eq.setBypassed(true);
    eq.setBypassed(false);
    QCOMPARE(count, 3);
    QVERIFY(last == eq.activeProfile());
}

void EqTest::missingFile()
{
    const QString file = EqControl::sourceDir() + QStringLiteral("/Later.txt");
    Settings settings(m_dir + QStringLiteral("/settings.ini"));
    settings.setRoots({m_dir + QStringLiteral("/music")});
    settings.setEqHeadphones(file);
    EqControl eq(&settings);
    EqProfile last;
    int count = 0;
    connect(&eq, &EqControl::profileApplied, this, [&last, &count](const EqProfile &profile) {
        last = profile;
        ++count;
    });
    eq.setActiveOutput(QStringLiteral("headphones"));
    QCOMPARE(eq.name(QStringLiteral("headphones")), QStringLiteral("LATER (MISSING)"));
    const QVariantMap map = eq.profileMap(QStringLiteral("headphones"));
    QCOMPARE(map.value(QStringLiteral("name")).toString(), QStringLiteral("LATER"));
    QVERIFY(map.value(QStringLiteral("missing")).toBool());
    QVERIFY(eq.missing(QStringLiteral("headphones")));
    QVERIFY(!eq.editable(QStringLiteral("headphones")));
    QCOMPARE(eq.key(QStringLiteral("headphones")), file);
    QVERIFY(last == EqProfile::off());
    eq.stepGain(QStringLiteral("headphones"), 0, 1);
    QCOMPARE(eq.key(QStringLiteral("headphones")), file);
    QVERIFY(!QFileInfo::exists(EqControl::storePath(QStringLiteral("headphones"))));

    QVERIFY(writeFile(file, QString::fromUtf8(autoEq)));
    const int before = count;
    QSignalSpy sources(&eq, &EqControl::sourcesChanged);
    eq.relist();
    QCOMPARE(int(sources.count()), 1);
    QCOMPARE(eq.name(QStringLiteral("headphones")), QStringLiteral("LATER"));
    QVERIFY(!eq.missing(QStringLiteral("headphones")));
    QCOMPARE(count, before + 1);
    QVERIFY(near(last.preamp, -6.2));
}

void EqTest::sinkPoll()
{
    const QString usbPactl = m_binDir.path() + QStringLiteral("/usb/pactl");
    QVERIFY(writeFile(usbPactl,
                      QStringLiteral("#!/bin/sh\n[ \"$1\" = get-default-sink ] && echo "
                                     "alsa_output.usb-Apple_USB-C_to_3.5mm.analog-stereo\nexit 0\n"),
                      true));
    const QByteArray pactl = qgetenv("CASDAP_PACTL_BIN");
    qputenv("CASDAP_PACTL_BIN", usbPactl.toUtf8());
    Settings settings(m_dir + QStringLiteral("/settings.ini"));
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    eq.setActiveOutput(QStringLiteral("speaker"));
    eq.startSinkPoll();
    QVERIFY(eq.sinkPolling());
    QTRY_COMPARE_WITH_TIMEOUT(eq.activeOutput(), QStringLiteral("usb"), waitMs);
    qputenv("CASDAP_PACTL_BIN", pactl);

    OutputControl control;
    eq.setOutputControl(&control);
    QVERIFY(!eq.sinkPolling());
}

void EqTest::displayNames()
{
    QCOMPARE(EqControl::displayName(QStringLiteral("/eq/Sennheiser HD 600 ParametricEQ.txt")), QStringLiteral("SENNHEISER HD 600"));
    QCOMPARE(EqControl::displayName(QStringLiteral("/eq/HD 600.txt")), QStringLiteral("HD 600"));
    QCOMPARE(EqControl::displayName(QStringLiteral("/eq/ParametricEQ.txt")), QStringLiteral("PARAMETRICEQ"));
}

QTEST_GUILESS_MAIN(EqTest)
#include "tst_eq.moc"
