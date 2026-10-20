#include "Eq.h"
#include "EqFit.h"
#include "OutputControl.h"
#include "Settings.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <atomic>
#include <cmath>
#include <memory>

using namespace microdap;

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

QStringList *capturedLines = nullptr;
QtMessageHandler previousHandler = nullptr;

void captureLine(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (capturedLines)
        capturedLines->append(message);
    if (previousHandler)
        previousHandler(type, context, message);
}

struct LogCapture
{
    LogCapture()
    {
        capturedLines = &messages;
        previousHandler = qInstallMessageHandler(captureLine);
    }

    ~LogCapture()
    {
        qInstallMessageHandler(previousHandler);
        capturedLines = nullptr;
    }

    QStringList lines(const QString &prefix) const
    {
        QStringList out;
        for (const QString &line : messages) {
            if (line.startsWith(prefix))
                out.append(line);
        }
        return out;
    }

    QStringList messages;
};

QString graphicEq(double bass)
{
    EqProfile p = EqProfile::flat();
    p.bands[0] = EqBand{true, EqBand::LowShelf, 105, bass, 0.7};
    p.bands[1] = EqBand{true, EqBand::Peak, 2400, -3, 1.4};
    p.count = 2;
    std::vector<double> db(EqFit::points);
    EqFit::response(p, 96000, EqFit::frequencies.data(), EqFit::points, db.data());
    QString text = QStringLiteral("GraphicEQ: ");
    for (int i = 0; i < EqFit::points; ++i)
        text += (i ? QStringLiteral("; ") : QString()) + QString::number(EqFit::frequencies[i]) + QLatin1Char(' ')
                + QString::number(std::round(db[i] * 10) / 10, 'f', 1);
    return text + QLatin1Char('\n');
}

QStringList names(const QVariantList &list)
{
    QStringList out;
    for (const QVariant &v : list)
        out.append(v.toMap().value(QStringLiteral("name")).toString());
    return out;
}

struct Gate
{
    std::atomic<bool> open{false};
    std::atomic<int> waits{0};

    void wait()
    {
        ++waits;
        QElapsedTimer clock;
        clock.start();
        while (!open && clock.elapsed() < waitMs)
            QThread::msleep(1);
    }
};

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
    void apoCodes();
    void apoSkipped();
    void apoOffLines();
    void autoEqUnchanged();
    void storeRoundTripTypes();
    void storeUnknownTypeFlat();
    void preampResonance();
    void clamps();
    void grid();
    void storeRoundTrip();
    void bandCount();
    void oldStoreRestart();
    void liveCountEdit();
    void liveSameCountEdit();
    void addRemoveBand();
    void presetNames();
    void savePreset();
    void deletePresetInUse();
    void renamePreset();
    void typedValues();
    void graphicProfile();
    void graphicMode();
    void graphicDebounce();
    void graphicPresets();
    void graphicGestureDebounce();
    void copyOnEdit();
    void steps();
    void stepTypeEight();
    void gainInertOnGainFree();
    void cycleProfile();
    void activeOutputPicksKey();
    void followsOutputControl();
    void filtersRoundTrip();
    void externalChange();
    void sinkPoll();
    void importList();
    void importSaves();
    void importCardGone();
    void importRefused();
    void importLate();

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
    qputenv("MICRODAP_PACTL_BIN", (m_bin + QStringLiteral("/pactl")).toUtf8());
    qputenv("MICRODAP_AMIXER_BIN", (m_bin + QStringLiteral("/amixer")).toUtf8());
    qputenv("MICRODAP_SYSTEMCTL_BIN", (m_bin + QStringLiteral("/systemctl")).toUtf8());
    qputenv("MICRODAP_WPCTL_BIN", (m_bin + QStringLiteral("/systemctl")).toUtf8());
}

void EqTest::init()
{
    m_temp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_temp->isValid());
    m_dir = m_temp->path();
    qputenv("MICRODAP_EQ_DIR", (m_dir + QStringLiteral("/config")).toUtf8());
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

void EqTest::apoCodes()
{
    const std::optional<EqProfile> profile = EqParser::parse(QStringLiteral(
        "Filter: ON PK Fc 1000 Hz Gain 3 dB Q 1.41\n"
        "Filter 2: ON LSC Fc 105 Hz Gain 6 dB\n"
        "Filter 3: on ls fc 80 hz gain 2 db q 0.9\n"
        "Filter 4: ON HSC Fc 8000 Hz Gain -2 dB\n"
        "Filter 5: ON HS Fc 9000 Hz Gain 1 dB Q 0.5\n"
        "Filter 6: ON LP Fc 15000 Hz\n"
        "Filter 7: ON LPQ Fc 12000 Hz Q 2\n"
        "Filter 8: ON HP Fc 30 Hz Gain 3 dB\n"
        "Filter 9: ON HPQ Fc 40 Hz Q 0.5\n"
        "Filter 10: ON BP Fc 1000 Hz\n"));
    QVERIFY(profile);
    const struct
    {
        EqBand::Type type;
        double freq;
        double gain;
        double q;
    } want[] = {
        {EqBand::Peak, 1000.0, 3.0, 1.41},     {EqBand::LowShelf, 105.0, 6.0, 0.71},
        {EqBand::LowShelf, 80.0, 2.0, 0.9},    {EqBand::HighShelf, 8000.0, -2.0, 0.71},
        {EqBand::HighShelf, 9000.0, 1.0, 0.5}, {EqBand::LowPass, 15000.0, 0.0, 0.71},
        {EqBand::LowPass, 12000.0, 0.0, 2.0},  {EqBand::HighPass, 30.0, 3.0, 0.71},
        {EqBand::HighPass, 40.0, 0.0, 0.5},    {EqBand::BandPass, 1000.0, 0.0, 0.71},
    };
    for (int i = 0; i < EqProfile::bandCount; ++i) {
        const EqBand &band = profile->bands[i];
        QVERIFY2(band.enabled, QByteArray::number(i).constData());
        QCOMPARE(band.type, want[i].type);
        QVERIFY(near(band.freq, want[i].freq));
        QVERIFY(near(band.gain, want[i].gain));
        QVERIFY(near(band.q, want[i].q));
    }

    const std::optional<EqProfile> allPass =
        EqParser::parse(QStringLiteral("Filter: ON AP Fc 500 Hz Q 1.5\nFilter: ON AP Fc 600 Hz Gain 2 dB Q 0.8\n"));
    QVERIFY(allPass);
    QCOMPARE(allPass->bands[0].type, EqBand::AllPass);
    QVERIFY(near(allPass->bands[0].q, 1.5));
    QCOMPARE(allPass->bands[1].type, EqBand::AllPass);
    QVERIFY(near(allPass->bands[1].gain, 2.0));
    QVERIFY(!allPass->bands[2].enabled);
}

void EqTest::apoSkipped()
{
    const std::optional<EqProfile> profile = EqParser::parse(QStringLiteral(
        "Filter 1: ON NO Fc 1000 Hz Q 2\n"
        "Filter 2: ON NO Fc 1000 Hz\n"
        "Filter 3: ON Modal Fc 100 Hz Gain 3 dB Q 5\n"
        "Filter 4: ON PEQ Fc 100 Hz Gain 3 dB BW Oct 1.0\n"
        "Filter 5: ON PK Fc 100 Hz Gain 3 dB BW Oct 1.0\n"
        "Filter 6: ON LSC 12 dB Fc 300 Hz Gain 5 dB\n"
        "Filter 7: ON LS 6dB Fc 100 Hz Gain 3 dB\n"
        "Filter 8: ON HS 12dB Fc 8000 Hz Gain 3 dB\n"
        "Filter 9: ON IIR Order 2 Coefficients 1 0 0 1 0 0\n"
        "Filter 10: ON PK Fc 100 Hz Q 1\n"
        "Filter 11: ON PK Fc 100 Hz Gain 2 dB\n"
        "Filter 12: ON LSC Fc 100 Hz Q 0.7\n"
        "Filter 13: ON AP Fc 100 Hz\n"
        "Filter 14: ON XX Fc 100 Hz Gain 1 dB Q 1\n"
        "Filter 15: ON PK Fc 250 Hz Gain 1 dB Q 1\n"));
    QVERIFY(profile);
    QVERIFY(profile->bands[0].enabled);
    QCOMPARE(profile->bands[0].type, EqBand::Peak);
    QVERIFY(near(profile->bands[0].freq, 250.0));
    for (int i = 1; i < EqProfile::bandCount; ++i)
        QVERIFY(!profile->bands[i].enabled);

    QVERIFY(!EqParser::parse(QStringLiteral("Filter: ON NO Fc 1000 Hz Q 2\nFilter: ON IIR Order 1\n")));
}

void EqTest::apoOffLines()
{
    const EqProfile defaults = EqProfile::flat();
    const std::optional<EqProfile> profile = EqParser::parse(QStringLiteral(
        "Filter 1: OFF LP Fc 100 Hz\n"
        "Filter 2: OFF NO Fc 100 Hz\n"
        "Filter 3: OFF PK Fc 1000 Hz Q 1\n"
        "Filter 4: OFF IIR Order 2\n"
        "Filter 5: OFF\n"
        "Filter 6: OFF LS 6dB Fc 100 Hz Gain 3 dB\n"
        "Filter: off hpq fc 40 hz q 0.5\n"
        "Filter 8: ON PK Fc 2000 Hz Gain 1 dB Q 1\n"));
    QVERIFY(profile);
    QVERIFY(!profile->bands[0].enabled);
    QCOMPARE(profile->bands[0].type, EqBand::LowPass);
    QVERIFY(near(profile->bands[0].freq, 100.0));
    QVERIFY(!profile->bands[1].enabled);
    QCOMPARE(profile->bands[1].type, EqBand::Peak);
    QCOMPARE(profile->bands[1].freqIndex, defaults.bands[1].freqIndex);
    QVERIFY(!profile->bands[2].enabled);
    QCOMPARE(profile->bands[2].type, EqBand::HighPass);
    QVERIFY(near(profile->bands[2].q, 0.5));
    QVERIFY(profile->bands[3].enabled);
    QVERIFY(near(profile->bands[3].freq, 2000.0));
    for (int i = 4; i < EqProfile::bandCount; ++i)
        QVERIFY(!profile->bands[i].enabled);
}

void EqTest::autoEqUnchanged()
{
    const std::optional<EqProfile> profile = EqParser::parse(QString::fromUtf8(autoEq));
    QVERIFY(profile);
    EqProfile want = EqProfile::flat();
    want.preamp = -6.2;
    const EqBand bands[] = {
        {true, EqBand::LowShelf, 105.0, 6.3, 0.70, -1},  {true, EqBand::Peak, 1000.0, -3.2, 1.41, -1},
        {true, EqBand::HighShelf, 10000.0, 2.0, 0.70, -1}, {false, EqBand::Peak, 200.0, 1.0, 1.00, -1},
        {true, EqBand::Peak, 300.0, 1.0, 2.0, -1},
    };
    for (int i = 0; i < 5; ++i)
        want.bands[i] = bands[i];
    for (int i = 5; i < EqProfile::bandCount; ++i)
        want.bands[i].enabled = false;
    want.count = 5;
    QVERIFY(*profile == want);
}

void EqTest::storeRoundTripTypes()
{
    EqProfile profile = EqProfile::flat();
    const EqBand::Type types[] = {EqBand::Peak,    EqBand::LowShelf, EqBand::HighShelf, EqBand::BandPass,
                                  EqBand::LowPass, EqBand::HighPass, EqBand::AllPass};
    const QStringList names = {QStringLiteral("peak"),     QStringLiteral("lowshelf"), QStringLiteral("highshelf"),
                               QStringLiteral("bandpass"), QStringLiteral("lowpass"),  QStringLiteral("highpass"),
                               QStringLiteral("allpass")};
    for (int i = 0; i < 7; ++i) {
        profile.bands[i].type = types[i];
        profile.bands[i].gain = 1.5 * (i + 1);
    }
    profile.bands[7].enabled = false;
    const QString path = m_dir + QStringLiteral("/store/eq-types.ini");
    QVERIFY(EqStore::write(path, profile));
    const QString text = readFile(path);
    for (int i = 0; i < 7; ++i) {
        QCOMPARE(EqStore::typeName(profile.bands[i]), names.at(i));
        QVERIFY(text.contains(QStringLiteral("[band%1]\ntype=%2\n").arg(i + 1).arg(names.at(i))));
    }
    QVERIFY(text.contains(QStringLiteral("[band8]\ntype=off\n")));
    QVERIFY(EqStore::read(path) == profile);
    QCOMPARE(profile.toMap().value(QStringLiteral("bands")).toList().at(4).toMap().value(QStringLiteral("type")),
             QVariant(QStringLiteral("lowpass")));
}

void EqTest::storeUnknownTypeFlat()
{
    EqProfile profile = EqProfile::flat();
    profile.preamp = -3.0;
    profile.bands[0].gain = 4.0;
    const QString path = m_dir + QStringLiteral("/store/eq-unknown.ini");
    QVERIFY(EqStore::write(path, profile));
    QVERIFY(EqStore::read(path) == profile);
    QString text = readFile(path);
    text.replace(QStringLiteral("[band3]\ntype=peak\n"), QStringLiteral("[band3]\ntype=notch\n"));
    QVERIFY(writeFile(path, text));
    QVERIFY(EqStore::read(path) == EqProfile::fresh());
}

void EqTest::preampResonance()
{
    const std::optional<EqProfile> lowPass = EqParser::parse(QStringLiteral("Filter: ON LP Fc 1000 Hz Q 2\n"));
    QVERIFY(lowPass);
    QVERIFY(std::abs(lowPass->preamp + 20.0 * std::log10(2.0)) < 1e-9);
    QVERIFY(std::abs(lowPass->preamp + 6.02) < 0.01);

    const std::optional<EqProfile> highPass =
        EqParser::parse(QStringLiteral("Filter: ON HP Fc 30 Hz Q 2\nFilter: ON HP Fc 40 Hz Q 0.5\n"));
    QVERIFY(highPass);
    QVERIFY(std::abs(highPass->preamp + 20.0 * std::log10(2.0)) < 1e-9);

    const std::optional<EqProfile> neutral = EqParser::parse(QStringLiteral(
        "Filter: ON BP Fc 1000 Hz Gain 6 dB Q 4\nFilter: ON AP Fc 1000 Hz Gain 6 dB Q 4\n"
        "Filter: ON LP Fc 1000 Hz Gain 6 dB Q 0.71\n"));
    QVERIFY(neutral);
    QVERIFY(near(neutral->preamp, 0.0));

    const std::optional<EqProfile> mixed = EqParser::parse(QStringLiteral(
        "Filter: ON PK Fc 100 Hz Gain 3 dB Q 1\nFilter: ON LP Fc 1000 Hz Q 2\nFilter: OFF HP Fc 30 Hz Q 4\n"));
    QVERIFY(mixed);
    QVERIFY(std::abs(mixed->preamp + 3.0 + 20.0 * std::log10(2.0)) < 1e-9);

    const std::optional<EqProfile> clamped = EqParser::parse(QStringLiteral(
        "Filter: ON PK Fc 100 Hz Gain 12 dB Q 1\nFilter: ON PK Fc 200 Hz Gain 12 dB Q 1\n"
        "Filter: ON LP Fc 1000 Hz Q 10\n"));
    QVERIFY(clamped);
    QVERIFY(near(clamped->preamp, -24.0));
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
    profile.count = 6;
    const QString path = m_dir + QStringLiteral("/store/eq-usb.ini");
    QVERIFY(EqStore::write(path, profile));
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".tmp")));
    const QString text = readFile(path);
    QVERIFY(text.contains(QStringLiteral("[band1]\ntype=lowshelf\nfreq=105\n")));
    QVERIFY(text.contains(QStringLiteral("[band4]\ntype=off\n")));
    QVERIFY(text.contains(QStringLiteral("[band6]\ntype=peak\nfreqIndex=40\n")));
    QVERIFY(EqStore::read(path) == profile);

    QVERIFY(text.contains(QStringLiteral("bands=6\n")));
    QVERIFY(text.contains(QStringLiteral("[band7]\ntype=off\n")));
    QVERIFY(text.contains(QStringLiteral("[band10]\ntype=off\n")));

    QVERIFY(EqStore::read(m_dir + QStringLiteral("/none.ini")) == EqProfile::fresh());
    QVERIFY(writeFile(m_dir + QStringLiteral("/bad.ini"), QStringLiteral("[General]\npreamp=x\n")));
    QVERIFY(EqStore::read(m_dir + QStringLiteral("/bad.ini")) == EqProfile::fresh());
    QVERIFY(writeFile(m_dir + QStringLiteral("/count.ini"), QStringLiteral("[General]\npreamp=0\nbands=11\n")));
    QVERIFY(EqStore::read(m_dir + QStringLiteral("/count.ini")) == EqProfile::fresh());
}

void EqTest::bandCount()
{
    const EqProfile fresh = EqProfile::fresh();
    QCOMPARE(fresh.count, 1);
    QCOMPARE(fresh.bands[0].freqIndex, 34);
    QVERIFY(near(fresh.bands[0].freq, 1000.0));
    QVERIFY(fresh.bands[0].enabled);
    QCOMPARE(fresh.bands[0].type, EqBand::Peak);
    QCOMPARE(EqParser::parse(QString::fromUtf8(autoEq))->count, 5);
    QCOMPARE(EqParser::parse(filters(12))->count, 10);
    QCOMPARE(EqFilters::decode({QStringLiteral("preamp -1"), QStringLiteral("peak 100 1 1"),
                                QStringLiteral("peak 200 2 1")}).count, 2);
    EqProfile three = EqProfile::flat();
    three.count = 3;
    three.bands[1].gain = 2.0;
    three.bands[5].gain = 9.0;
    QCOMPARE(EqFilters::encode(three), (QStringList{QStringLiteral("preamp 0"), QStringLiteral("peak 31.25 0 1.4"),
                                                    QStringLiteral("peak 62.5 2 1.4"), QStringLiteral("peak 125 0 1.4")}));
    QCOMPARE(three.toMap().value(QStringLiteral("bands")).toList().size(), 3);
    QCOMPARE(three.toMap().value(QStringLiteral("count")).toInt(), 3);
    EqProfile other = three;
    other.bands[5].gain = -4.0;
    QVERIFY(other == three);

    EqProfile old = EqProfile::flat();
    old.bands[2].gain = 3.0;
    old.bands[6].type = EqBand::LowPass;
    QString text = EqStore::text(old);
    text.replace(QStringLiteral("bands=10\n"), QString());
    const QString path = m_dir + QStringLiteral("/store/eq-old.ini");
    QVERIFY(writeFile(path, text));
    const EqProfile trimmed = EqStore::read(path);
    QCOMPARE(trimmed.count, 7);
    EqProfile sound = old;
    sound.count = 7;
    QVERIFY(trimmed == sound);
    old.bands[6].type = EqBand::Peak;
    text = EqStore::text(old);
    text.replace(QStringLiteral("bands=10\n"), QString());
    const QString shelfless = m_dir + QStringLiteral("/store/eq-old3.ini");
    QVERIFY(writeFile(shelfless, text));
    QCOMPARE(EqStore::read(shelfless).count, 3);
    const QString flatPath = m_dir + QStringLiteral("/store/eq-oldflat.ini");
    QVERIFY(writeFile(flatPath, EqStore::text(EqProfile::flat()).replace(QStringLiteral("bands=10\n"), QString())));
    QCOMPARE(EqStore::read(flatPath).count, 1);
}

void EqTest::oldStoreRestart()
{
    const QString out = QStringLiteral("usb");
    EqProfile old = EqProfile::flat();
    old.bands[2].gain = 3.0;
    old.preamp = -3.0;
    QString text = EqStore::text(old);
    text.replace(QStringLiteral("bands=10\n"), QString());
    QVERIFY(writeFile(EqControl::storePath(out), text));
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    const QStringList oldFilters = EqFilters::encode(old);
    QCOMPARE(oldFilters.size(), 11);
    settings.setValue(EqFilters::key(out, QStringLiteral("filters")), oldFilters);
    settings.setValue(EqFilters::key(out, QStringLiteral("name")), QString());
    settings.setValue(EqFilters::key(out, QStringLiteral("on")), true);
    EqControl eq(&settings);
    QCOMPARE(eq.key(out), QStringLiteral("custom"));
    QCOMPARE(eq.profile(out).count, 3);
    QCOMPARE(eq.profileMap(out).value(QStringLiteral("bands")).toList().size(), 3);
    eq.stepGain(out, 2, 1);
    QCOMPARE(eq.profile(out).count, 3);
    QVERIFY(near(eq.profile(out).bands[2].gain, 3.1));
    eq.sync();
    QCOMPARE(settings.value(EqFilters::key(out, QStringLiteral("filters"))).toStringList().size(), 4);
}

void EqTest::liveSameCountEdit()
{
    const QString out = QStringLiteral("speaker");
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    EqProfile stale = EqProfile::fresh();
    stale.bands[0].gain = -1.0;
    eq.setCustom(out, stale);
    eq.sync();
    settings.setValue(EqFilters::key(out, QStringLiteral("on")), false);
    settings.setValue(EqFilters::key(out, QStringLiteral("filters")),
                      QStringList{QStringLiteral("preamp 0"), QStringLiteral("peak 500 -4 1")});
    settings.setValue(EqFilters::key(out, QStringLiteral("on")), true);
    QVERIFY(near(eq.profile(out).bands[0].gain, -4.0));
    eq.stepGain(out, 0, 1);
    QVERIFY(near(eq.profile(out).bands[0].gain, -3.9));
    QVERIFY(near(eq.profile(out).bands[0].freq, 500.0));
}

void EqTest::liveCountEdit()
{
    const QString out = QStringLiteral("speaker");
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    eq.setCustom(out, EqProfile::fresh());
    settings.setValue(EqFilters::key(out, QStringLiteral("filters")),
                      QStringList{QStringLiteral("preamp 0"), QStringLiteral("peak 100 1 1"), QStringLiteral("peak 200 2 1"),
                                  QStringLiteral("peak 300 3 1")});
    QCOMPARE(eq.profile(out).count, 3);
    eq.stepGain(out, 2, 1);
    QCOMPARE(eq.profile(out).count, 3);
    QVERIFY(near(eq.profile(out).bands[2].gain, 3.1));
    QVERIFY(near(eq.profile(out).bands[0].gain, 1.0));
}

void EqTest::addRemoveBand()
{
    const QString file = EqControl::sourceDir() + QStringLiteral("/HD600 ParametricEQ.txt");
    QVERIFY(writeFile(file, QString::fromUtf8(autoEq)));
    const QString out = QStringLiteral("headphones");
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    {
        EqControl eq(&settings);
        QVERIFY(!eq.addBand(out));
        QVERIFY(!eq.removeBand(out));
        eq.cycleProfile(out, 1);
        QCOMPARE(eq.key(out), QStringLiteral("HD600"));
        QCOMPARE(eq.profile(out).count, 5);
        QVERIFY(eq.addBand(out));
        QCOMPARE(eq.key(out), QStringLiteral("custom"));
        const EqProfile added = eq.profile(out);
        QCOMPARE(added.count, 6);
        QVERIFY(added.bands[5] == EqProfile::freshBand());
        QVERIFY(near(added.bands[0].gain, 6.3));
        QVERIFY(eq.removeBand(out));
        QVERIFY(eq.removeBand(out));
        QCOMPARE(eq.profile(out).count, 4);
        while (eq.removeBand(out)) {
        }
        QCOMPARE(eq.profile(out).count, 1);
        while (eq.addBand(out)) {
        }
        QCOMPARE(eq.profile(out).count, EqProfile::bandCount);
        QVERIFY(!eq.addBand(out));
        while (eq.profile(out).count > 3)
            QVERIFY(eq.removeBand(out));
        eq.stepType(out, 2, -1);
        QVERIFY(!eq.profile(out).bands[2].enabled);
        eq.stepBand(out, 3, QStringLiteral("gain"), 1);
        QCOMPARE(eq.profile(out).count, 3);
        eq.sync();
    }
    EqControl eq(&settings);
    QCOMPARE(eq.key(out), QStringLiteral("custom"));
    QCOMPARE(eq.profile(out).count, 3);
    QVERIFY(!eq.profile(out).bands[2].enabled);
}

void EqTest::presetNames()
{
    QVERIFY(EqControl::nameOk(QStringLiteral("Preset 1")));
    QVERIFY(EqControl::nameOk(QStringLiteral("Mein Kopfhörer")));
    QVERIFY(EqControl::nameOk(QString(64, QLatin1Char('a'))));
    QVERIFY(!EqControl::nameOk(QString(65, QLatin1Char('a'))));
    QVERIFY(!EqControl::nameOk(QString()));
    QVERIFY(!EqControl::nameOk(QStringLiteral(" lead")));
    QVERIFY(!EqControl::nameOk(QStringLiteral("trail ")));
    QVERIFY(!EqControl::nameOk(QStringLiteral(".hidden")));
    QVERIFY(!EqControl::nameOk(QStringLiteral("a/b")));
    QVERIFY(!EqControl::nameOk(QStringLiteral("tab\there")));
    QVERIFY(!EqControl::nameOk(QStringLiteral("HD600 ParametricEQ")));
    QVERIFY(!EqControl::nameOk(QStringLiteral("HD600 GraphicEQ")));
    QVERIFY(!EqControl::nameOk(QStringLiteral("Off")));
    QVERIFY(!EqControl::nameOk(QStringLiteral("FLAT")));
    QVERIFY(!EqControl::nameOk(QStringLiteral("custom")));
    QVERIFY(EqControl::nameLess(QStringLiteral("Preset 2"), QStringLiteral("Preset 10")));
    QVERIFY(!EqControl::nameLess(QStringLiteral("Preset 10"), QStringLiteral("Preset 2")));
    QVERIFY(EqControl::nameLess(QStringLiteral("preset 2"), QStringLiteral("Preset 3")));
    QVERIFY(EqControl::nameLess(QStringLiteral("a 007"), QStringLiteral("a 8")));
    QVERIFY(EqControl::nameLess(QStringLiteral("Apple"), QStringLiteral("banana")));
    EqProfile profile = *EqParser::parse(QString::fromUtf8(autoEq));
    profile.bands[1].type = EqBand::LowPass;
    const QString text = EqControl::autoEqText(profile);
    QVERIFY(text.startsWith(QStringLiteral("Preamp: -6.2 dB\nFilter 1: ON LSC Fc 105 Hz Gain 6.3 dB Q 0.7\n")));
    QVERIFY(text.contains(QStringLiteral("Filter 4: OFF PK Fc 200 Hz Gain 1 dB Q 1\n")));
    const std::optional<EqProfile> back = EqParser::parse(text);
    QVERIFY(back);
    QCOMPARE(EqFilters::encode(*back), EqFilters::encode(profile));
    QCOMPARE(back->count, profile.count);
}

void EqTest::savePreset()
{
    QVERIFY(writeFile(EqControl::sourceDir() + QStringLiteral("/Preset 2.txt"), QString::fromUtf8(autoEq)));
    QVERIFY(writeFile(EqControl::presetDir() + QStringLiteral("/Preset 10.txt"), QString::fromUtf8(autoEq)));
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    const QString out = QStringLiteral("usb");
    QCOMPARE(eq.savePreset(out), QStringLiteral("preset not saved"));
    EqProfile custom = EqProfile::fresh();
    custom.bands[0].gain = 3.5;
    custom.preamp = -3.5;
    eq.setCustom(out, custom);
    QVERIFY(!eq.saved(out));
    QVERIFY(!eq.profileMap(out).value(QStringLiteral("saved")).toBool());
    QCOMPARE(eq.savePreset(out), QString());
    const QString first = EqControl::presetDir() + QStringLiteral("/Preset 1.txt");
    QVERIFY(QFileInfo::exists(first));
    QCOMPARE(readFile(first), QStringLiteral("Preamp: -3.5 dB\nFilter 1: ON PK Fc 1000 Hz Gain 3.5 dB Q 1.4\n"));
    QCOMPARE(eq.key(out), QStringLiteral("PRESET 1"));
    QVERIFY(eq.saved(out));
    QCOMPARE(eq.presetName(out), QStringLiteral("Preset 1"));
    QCOMPARE(EqFilters::encode(eq.profile(out)), EqFilters::encode(custom));
    eq.stepGain(out, 0, 1);
    QCOMPARE(eq.key(out), QStringLiteral("custom"));
    QCOMPARE(eq.savePreset(out), QString());
    QCOMPARE(eq.key(out), QStringLiteral("PRESET 3"));
    QVERIFY(QFileInfo::exists(EqControl::presetDir() + QStringLiteral("/Preset 3.txt")));
    QStringList names;
    for (const QVariant &v : eq.sources())
        names.append(v.toMap().value(QStringLiteral("name")).toString());
    QCOMPARE(names, (QStringList{QStringLiteral("PRESET 1"), QStringLiteral("PRESET 3"), QStringLiteral("PRESET 10"),
                                 QStringLiteral("PRESET 2")}));
    eq.cycleProfile(QStringLiteral("headphones"), 1);
    QCOMPARE(eq.key(QStringLiteral("headphones")), QStringLiteral("PRESET 1"));
    eq.cycleProfile(QStringLiteral("headphones"), 1);
    eq.cycleProfile(QStringLiteral("headphones"), 1);
    eq.cycleProfile(QStringLiteral("headphones"), 1);
    QCOMPARE(eq.key(QStringLiteral("headphones")), QStringLiteral("PRESET 2"));
    QVERIFY(!eq.saved(QStringLiteral("headphones")));
}

void EqTest::deletePresetInUse()
{
    const QString path = EqControl::presetDir() + QStringLiteral("/Preset 1.txt");
    QVERIFY(writeFile(path, QString::fromUtf8(autoEq)));
    QVERIFY(writeFile(EqControl::sourceDir() + QStringLiteral("/HD600.txt"), QString::fromUtf8(autoEq)));
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    eq.cycleProfile(QStringLiteral("usb"), 1);
    eq.cycleProfile(QStringLiteral("speaker"), 1);
    eq.cycleProfile(QStringLiteral("headphones"), 1);
    eq.cycleProfile(QStringLiteral("headphones"), 1);
    QCOMPARE(eq.key(QStringLiteral("usb")), QStringLiteral("PRESET 1"));
    QCOMPARE(eq.key(QStringLiteral("speaker")), QStringLiteral("PRESET 1"));
    QCOMPARE(eq.key(QStringLiteral("headphones")), QStringLiteral("HD600"));
    QVERIFY(!eq.deletePreset(QStringLiteral("headphones")));
    QVERIFY(QFileInfo::exists(EqControl::sourceDir() + QStringLiteral("/HD600.txt")));
    QVERIFY(eq.deletePreset(QStringLiteral("usb")));
    QVERIFY(!QFileInfo::exists(path));
    QCOMPARE(eq.key(QStringLiteral("usb")), QStringLiteral("off"));
    QCOMPARE(eq.key(QStringLiteral("speaker")), QStringLiteral("off"));
    QCOMPARE(eq.key(QStringLiteral("headphones")), QStringLiteral("HD600"));
    QCOMPARE(int(eq.sources().size()), 1);
    QCOMPARE(settings.value(EqFilters::key(QStringLiteral("usb"), QStringLiteral("name"))).toString(), QString());
    QCOMPARE(settings.value(EqFilters::key(QStringLiteral("speaker"), QStringLiteral("name"))).toString(), QString());
}

void EqTest::renamePreset()
{
    const QString path = EqControl::presetDir() + QStringLiteral("/Preset 1.txt");
    QVERIFY(writeFile(path, QString::fromUtf8(autoEq)));
    QVERIFY(writeFile(EqControl::presetDir() + QStringLiteral("/Taken.txt"), QString::fromUtf8(autoEq)));
    QVERIFY(writeFile(EqControl::sourceDir() + QStringLiteral("/HD600.txt"), QString::fromUtf8(autoEq)));
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    eq.cycleProfile(QStringLiteral("usb"), 1);
    eq.cycleProfile(QStringLiteral("speaker"), 1);
    QCOMPARE(eq.key(QStringLiteral("usb")), QStringLiteral("PRESET 1"));
    eq.cycleProfile(QStringLiteral("headphones"), -1);
    eq.cycleProfile(QStringLiteral("headphones"), -1);
    QCOMPARE(eq.key(QStringLiteral("headphones")), QStringLiteral("HD600"));
    QCOMPARE(eq.renameRefusal(QStringLiteral("headphones"), QStringLiteral("Mine")), QStringLiteral("name not allowed"));
    QVERIFY(!eq.renamePreset(QStringLiteral("headphones"), QStringLiteral("Mine")));
    QVERIFY(QFileInfo::exists(EqControl::sourceDir() + QStringLiteral("/HD600.txt")));
    QCOMPARE(eq.renameRefusal(QStringLiteral("usb"), QStringLiteral("custom")), QStringLiteral("name not allowed"));
    QCOMPARE(eq.renameRefusal(QStringLiteral("usb"), QStringLiteral("taken")), QStringLiteral("name taken"));
    QCOMPARE(eq.renameRefusal(QStringLiteral("usb"), QStringLiteral("hd600")), QStringLiteral("name taken"));
    QCOMPARE(eq.renameRefusal(QStringLiteral("usb"), QStringLiteral("preset 1")), QString());
    QCOMPARE(eq.renameRefusal(QStringLiteral("usb"), QStringLiteral("Desk")), QString());
    const QStringList filters = settings.value(EqFilters::key(QStringLiteral("usb"), QStringLiteral("filters"))).toStringList();
    QVERIFY(eq.renamePreset(QStringLiteral("usb"), QStringLiteral("Desk")));
    QVERIFY(!QFileInfo::exists(path));
    const QString renamed = EqControl::presetDir() + QStringLiteral("/Desk.txt");
    QCOMPARE(readFile(renamed), QString::fromUtf8(autoEq));
    QCOMPARE(eq.key(QStringLiteral("usb")), QStringLiteral("DESK"));
    QCOMPARE(eq.key(QStringLiteral("speaker")), QStringLiteral("DESK"));
    QCOMPARE(eq.presetName(QStringLiteral("usb")), QStringLiteral("Desk"));
    QCOMPARE(settings.value(EqFilters::key(QStringLiteral("usb"), QStringLiteral("filters"))).toStringList(), filters);
    QVERIFY(eq.saved(QStringLiteral("usb")));
}

void EqTest::typedValues()
{
    const QString file = EqControl::sourceDir() + QStringLiteral("/HD600.txt");
    QVERIFY(writeFile(file, QString::fromUtf8(autoEq)));
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    const QString out = QStringLiteral("headphones");
    QVERIFY(!eq.setPreamp(out, -3.0));
    eq.cycleProfile(out, 1);
    QCOMPARE(eq.key(out), QStringLiteral("HD600"));
    QVERIFY(!eq.setPreamp(out, 0.5));
    QCOMPARE(eq.key(out), QStringLiteral("HD600"));
    QVERIFY(!eq.setPreamp(out, -24.5));
    QVERIFY(eq.setPreamp(out, -3.24));
    QCOMPARE(eq.key(out), QStringLiteral("custom"));
    QVERIFY(near(eq.profile(out).preamp, -3.2));
    QVERIFY(eq.setPreamp(out, 0.0));
    QVERIFY(near(eq.profile(out).preamp, 0.0));
    QVERIFY(eq.setBandValue(out, 1, QStringLiteral("freq"), 1234.4));
    QVERIFY(near(eq.profile(out).bands[1].freq, 1234.0));
    QCOMPARE(eq.profile(out).bands[1].freqIndex, -1);
    QVERIFY(eq.setBandValue(out, 1, QStringLiteral("freq"), 1000.0));
    QCOMPARE(eq.profile(out).bands[1].freqIndex, 34);
    QVERIFY(eq.setBandValue(out, 1, QStringLiteral("freq"), 20000.0));
    QCOMPARE(eq.profile(out).bands[1].freqIndex, EqGrid::size - 1);
    QVERIFY(!eq.setBandValue(out, 1, QStringLiteral("freq"), 19.0));
    QVERIFY(!eq.setBandValue(out, 1, QStringLiteral("freq"), 20001.0));
    QVERIFY(eq.setBandValue(out, 1, QStringLiteral("gain"), -2.26));
    QVERIFY(near(eq.profile(out).bands[1].gain, -2.3));
    QVERIFY(!eq.setBandValue(out, 1, QStringLiteral("gain"), 12.5));
    QVERIFY(eq.setBandValue(out, 1, QStringLiteral("gain"), 12.0));
    QVERIFY(eq.setBandValue(out, 1, QStringLiteral("q"), 0.714));
    QVERIFY(near(eq.profile(out).bands[1].q, 0.71));
    QVERIFY(!eq.setBandValue(out, 1, QStringLiteral("q"), 0.05));
    QVERIFY(!eq.setBandValue(out, 1, QStringLiteral("type"), 1.0));
    QVERIFY(!eq.setBandValue(out, 7, QStringLiteral("gain"), 1.0));
    for (int i = 0; i < 3; ++i)
        eq.stepType(out, 0, 1);
    QCOMPARE(eq.profile(out).bands[0].type, EqBand::LowPass);
    QVERIFY(!eq.setBandValue(out, 0, QStringLiteral("gain"), 1.0));
    eq.sync();
    const EqProfile stored = EqStore::read(EqControl::storePath(out));
    QVERIFY(near(stored.bands[1].freq, 20000.0));
    QVERIFY(near(stored.bands[1].q, 0.71));
}

void EqTest::graphicProfile()
{
    const EqProfile flat = EqProfile::graphic({});
    QCOMPARE(flat.count, 10);
    QVERIFY(near(flat.preamp, 0.0));
    for (int i = 0; i < EqProfile::bandCount; ++i) {
        QCOMPARE(flat.bands[i].type, EqBand::MatchedPeak);
        QCOMPARE(flat.bands[i].freqIndex, 4 + 6 * i);
        QVERIFY(near(flat.bands[i].q, 0.92));
        QVERIFY(flat.bands[i].enabled);
    }
    const std::array<std::array<double, 10>, 6> cases{{
        {12, 12, 12, 12, 12, 12, 12, 12, 12, 12},
        {0, 0, 0, 0, 12, 0, 0, 0, 0, 0},
        {12, -24, 12, -24, 12, -24, 12, -24, 12, -24},
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 12},
        {3, -7, 12, -1, 0, 5, -24, 8, 2, -3},
        {-6, -6, -6, -6, -6, -6, -6, -6, -6, -6},
    }};
    const std::array<double, 6> preamps{-23.9, -12.1, -14.7, -12.1, -12.4, 0.0};
    for (size_t i = 0; i < cases.size(); ++i)
        QVERIFY2(near(EqProfile::graphic(cases[i]).preamp, preamps[i]),
                 qPrintable(QString::number(EqProfile::graphic(cases[i]).preamp)));
    const QStringList items = EqFilters::encode(EqProfile::graphic(cases[3]));
    QCOMPARE(items.size(), 11);
    QCOMPARE(items.first(), QStringLiteral("preamp -12.1"));
    QCOMPARE(items.at(1), QStringLiteral("matchedpeak 31.25 0 0.92"));
    QCOMPARE(items.at(10), QStringLiteral("matchedpeak 16000 12 0.92"));
    const EqProfile back = EqFilters::decode(items);
    QCOMPARE(back.bands[9].type, EqBand::MatchedPeak);
    QVERIFY(back.bands[0].enabled);
    QCOMPARE(EqFilters::encode(back), items);
    QVERIFY(near(EqParser::fallbackPreamp(EqProfile::graphic(cases[1])), -12.0));
    std::array<double, 10> gains{};
    gains[2] = -3.5;
    QVERIFY(EqControl::gainsText(gains).startsWith(QStringLiteral("[General]\ngain1=0.0\ngain2=0.0\ngain3=-3.5\n")));
    const QString path = m_dir + QStringLiteral("/g.ini");
    QVERIFY(writeFile(path, EqControl::gainsText(gains)));
    QVERIFY(EqControl::readGains(path) == std::optional(gains));
    QVERIFY(writeFile(path, QStringLiteral("[General]\ngain4=40\nfoo=1\n")));
    QVERIFY(near((*EqControl::readGains(path))[3], 12.0));
    QVERIFY(writeFile(path, QStringLiteral("[Other]\ngain1=1\n")));
    QVERIFY(!EqControl::readGains(path));
}

void EqTest::graphicMode()
{
    QVERIFY(writeFile(EqControl::sourceDir() + QStringLiteral("/HD600.txt"), QString::fromUtf8(autoEq)));
    const QString out = QStringLiteral("headphones");
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    {
        EqControl eq(&settings);
        eq.cycleProfile(out, 1);
        QCOMPARE(eq.key(out), QStringLiteral("HD600"));
        QVERIFY(!eq.graphic(out));
        QCOMPARE(eq.profileMap(out).value(QStringLiteral("mode")).toString(), QStringLiteral("parametric"));
        eq.cycleMode(out, 1);
        eq.sync();
        QVERIFY(eq.graphic(out));
        QCOMPARE(settings.value(QStringLiteral("microdap.eq_mode_headphones")).toString(), QStringLiteral("graphic"));
        QCOMPARE(eq.profileMap(out).value(QStringLiteral("key")).toString(), QStringLiteral("flat"));
        QCOMPARE(eq.name(out), QStringLiteral("FLAT"));
        QCOMPARE(eq.profileMap(out).value(QStringLiteral("bands")).toList().size(), 10);
        QVERIFY(settings.value(EqFilters::key(out, QStringLiteral("on"))).toBool());
        QCOMPARE(settings.value(EqFilters::key(out, QStringLiteral("filters"))).toStringList(),
                 EqFilters::encode(EqProfile::graphic({})));
        QCOMPARE(settings.value(EqFilters::key(out, QStringLiteral("name"))).toString(), QStringLiteral("HD600"));
        eq.cycleGraphic(out, -1);
        eq.sync();
        QCOMPARE(eq.key(out), QStringLiteral("off"));
        QVERIFY(!settings.value(EqFilters::key(out, QStringLiteral("on"))).toBool());
        QVERIFY(eq.profileMap(out).value(QStringLiteral("bands")).toList().isEmpty());
        eq.cycleGraphic(out, -1);
        eq.sync();
        QCOMPARE(eq.profileMap(out).value(QStringLiteral("key")).toString(), QStringLiteral("custom"));
        eq.stepGraphic(out, 2, 1, true);
        eq.sync();
        QVERIFY(near(eq.profile(out).bands[2].gain, 1.0));
        QCOMPARE(settings.value(QStringLiteral("microdap.eq_graphic_headphones")).toStringList().at(2), QStringLiteral("1"));
        eq.cycleGraphic(out, 1);
        eq.sync();
        eq.cycleGraphic(out, 1);
        eq.sync();
        QCOMPARE(eq.profileMap(out).value(QStringLiteral("key")).toString(), QStringLiteral("flat"));
        eq.stepGraphic(out, 0, -1);
        QCOMPARE(eq.profileMap(out).value(QStringLiteral("key")).toString(), QStringLiteral("custom"));
        QVERIFY(near(eq.profile(out).bands[0].gain, -0.1));
        QVERIFY(near(eq.profile(out).bands[2].gain, 0.0));
        QVERIFY(eq.setGraphicGain(out, 9, 12.0));
        QVERIFY(!eq.setGraphicGain(out, 9, 12.5));
        QVERIFY(near(eq.profile(out).preamp, -12.1));
        eq.stepPreamp(out, 1);
        eq.stepBand(out, 0, QStringLiteral("gain"), 1);
        QVERIFY(!eq.addBand(out));
        QVERIFY(near(eq.profile(out).bands[0].gain, -0.1));
        eq.sync();
    }
    {
        EqControl eq(&settings);
        QVERIFY(eq.graphic(out));
        QVERIFY(near(eq.profile(out).bands[9].gain, 12.0));
        QVERIFY(near(eq.profile(out).bands[0].gain, -0.1));
        const QVariantMap state = eq.snapshot(out);
        eq.stepGraphic(out, 5, 1, true);
        eq.cycleMode(out, 1);
        eq.sync();
        QVERIFY(!eq.graphic(out));
        QCOMPARE(eq.key(out), QStringLiteral("HD600"));
        QVERIFY(settings.value(EqFilters::key(out, QStringLiteral("on"))).toBool());
        QCOMPARE(EqFilters::encode(eq.profile(out)), EqFilters::encode(*EqParser::parse(QString::fromUtf8(autoEq))));
        eq.restore(out, state);
        eq.sync();
        QVERIFY(eq.graphic(out));
        QVERIFY(near(eq.profile(out).bands[5].gain, 0.0));
        QVERIFY(near(eq.profile(out).bands[9].gain, 12.0));
        eq.cycleGraphic(out, 1);
        eq.sync();
        QCOMPARE(eq.key(out), QStringLiteral("off"));
        eq.cycleMode(out, 1);
        eq.sync();
        QCOMPARE(eq.key(out), QStringLiteral("HD600"));
        eq.cycleProfile(out, -1);
        QCOMPARE(eq.key(out), QStringLiteral("off"));
        eq.cycleMode(out, 1);
        eq.sync();
        eq.cycleMode(out, 1);
        eq.sync();
        QCOMPARE(eq.key(out), QStringLiteral("off"));
        QVERIFY(!settings.value(EqFilters::key(out, QStringLiteral("on"))).toBool());
    }
}

void EqTest::graphicDebounce()
{
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    const QString out = QStringLiteral("usb");
    eq.cycleMode(out, 1);
    eq.sync();
    eq.cycleGraphic(out, -1);
    eq.sync();
    eq.cycleGraphic(out, -1);
    eq.sync();
    QSignalSpy values(&settings, &Settings::valueChanged);
    const QString filters = EqFilters::key(out, QStringLiteral("filters"));
    const auto writes = [&values, &filters] {
        int n = 0;
        for (const QList<QVariant> &args : values)
            n += args.first().toString() == filters ? 1 : 0;
        return n;
    };
    for (int i = 0; i < 6; ++i) {
        eq.stepGraphic(out, 4, 1);
        QTest::qWait(EqControl::writeDelayMs / 3);
    }
    QCOMPARE(writes(), 0);
    QVERIFY(near(eq.profile(out).bands[4].gain, 0.6));
    QTRY_COMPARE_WITH_TIMEOUT(writes(), 1, 2000);
    QTest::qWait(EqControl::writeDelayMs * 2);
    QCOMPARE(writes(), 1);
    QCOMPARE(settings.value(filters).toStringList(), EqFilters::encode(eq.profile(out)));
}

void EqTest::graphicGestureDebounce()
{
    for (const QString &name : {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")})
        QVERIFY(writeFile(EqControl::graphicDir() + QLatin1Char('/') + name + QStringLiteral(".ini"),
                          QStringLiteral("[General]\ngain1=") + QString::number(name.at(0).unicode() - 64) + QLatin1Char('\n')));
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    const QString out = QStringLiteral("usb");
    const QString filters = EqFilters::key(out, QStringLiteral("filters"));
    QSignalSpy values(&settings, &Settings::valueChanged);
    const auto writes = [&values, &filters] {
        int n = 0;
        for (const QList<QVariant> &args : values)
            n += args.first().toString() == filters ? 1 : 0;
        return n;
    };
    eq.cycleMode(out, 1);
    for (int i = 0; i < 9; ++i)
        eq.cycleGraphic(out, 1);
    QCOMPARE(writes(), 0);
    QCOMPARE(eq.profileMap(out).value(QStringLiteral("key")).toString(), QStringLiteral("C"));
    QVERIFY(eq.profileMap(out).value(QStringLiteral("bands")).toList().size() == 10);
    QTRY_COMPARE_WITH_TIMEOUT(writes(), 1, 2000);
    QCOMPARE(settings.value(filters).toStringList(), EqFilters::encode(eq.profile(out)));
    QVERIFY(settings.value(EqFilters::key(out, QStringLiteral("on"))).toBool());
    const QVariantMap state = eq.snapshot(out);
    eq.cycleMode(out, 1);
    eq.cycleMode(out, 1);
    eq.cycleMode(out, 1);
    QVERIFY(!eq.graphic(out));
    eq.restore(out, state);
    QVERIFY(eq.graphic(out));
    QTest::qWait(EqControl::writeDelayMs * 3);
    QCOMPARE(writes(), 1);
    QCOMPARE(settings.value(filters).toStringList(), EqFilters::encode(eq.profile(out)));
}

void EqTest::graphicPresets()
{
    QVERIFY(writeFile(EqControl::graphicDir() + QStringLiteral("/Warm.ini"),
                      QStringLiteral("[General]\ngain1=3\ngain10=-2\n")));
    QVERIFY(writeFile(EqControl::graphicDir() + QStringLiteral("/Flat.ini"), QStringLiteral("[General]\ngain1=5\n")));
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    const QString out = QStringLiteral("speaker");
    eq.cycleMode(out, 1);
    eq.sync();
    eq.cycleGraphic(out, 1);
    eq.sync();
    QCOMPARE(eq.profileMap(out).value(QStringLiteral("key")).toString(), QStringLiteral("WARM"));
    QVERIFY(eq.saved(out));
    QCOMPARE(eq.presetName(out), QStringLiteral("Warm"));
    QVERIFY(near(eq.profile(out).bands[0].gain, 3.0));
    eq.stepGraphic(out, 1, 1);
    QCOMPARE(eq.profileMap(out).value(QStringLiteral("key")).toString(), QStringLiteral("custom"));
    QVERIFY(near(eq.profile(out).bands[0].gain, 3.0));
    QVERIFY(near(eq.profile(out).bands[1].gain, 0.1));
    QCOMPARE(eq.savePreset(out), QString());
    QCOMPARE(eq.profileMap(out).value(QStringLiteral("key")).toString(), QStringLiteral("PRESET 1"));
    const QString saved = EqControl::graphicDir() + QStringLiteral("/Preset 1.ini");
    QVERIFY(readFile(saved).startsWith(QStringLiteral("[General]\ngain1=3.0\ngain2=0.1\n")));
    QCOMPARE(eq.renameRefusal(out, QStringLiteral("warm")), QStringLiteral("name taken"));
    QVERIFY(eq.renamePreset(out, QStringLiteral("Bright")));
    QVERIFY(!QFileInfo::exists(saved));
    QCOMPARE(eq.profileMap(out).value(QStringLiteral("key")).toString(), QStringLiteral("BRIGHT"));
    eq.cycleMode(QStringLiteral("usb"), 1);
    eq.sync();
    eq.cycleGraphic(QStringLiteral("usb"), 1);
    eq.sync();
    QCOMPARE(eq.profileMap(QStringLiteral("usb")).value(QStringLiteral("key")).toString(), QStringLiteral("BRIGHT"));
    QVERIFY(eq.deletePreset(out));
    QVERIFY(!QFileInfo::exists(EqControl::graphicDir() + QStringLiteral("/Bright.ini")));
    QCOMPARE(eq.key(out), QStringLiteral("off"));
    QCOMPARE(eq.key(QStringLiteral("usb")), QStringLiteral("off"));
    QVERIFY(QFileInfo::exists(EqControl::graphicDir() + QStringLiteral("/Warm.ini")));
    QVERIFY(QFile::remove(EqControl::graphicDir() + QStringLiteral("/Warm.ini")));
    eq.cycleGraphic(out, 1);
    eq.sync();
    eq.cycleGraphic(out, 1);
    eq.sync();
    QCOMPARE(eq.profileMap(out).value(QStringLiteral("key")).toString(), QStringLiteral("WARM"));
    eq.relist();
    QCOMPARE(eq.profileMap(out).value(QStringLiteral("key")).toString(), QStringLiteral("WARM"));
    QVERIFY(eq.missing(out));
    QCOMPARE(eq.key(out), QStringLiteral("off"));
    QVERIFY(!settings.value(EqFilters::key(out, QStringLiteral("on"))).toBool());
}

void EqTest::copyOnEdit()
{
    const QString file = EqControl::sourceDir() + QStringLiteral("/HD600 ParametricEQ.txt");
    QVERIFY(writeFile(file, QString::fromUtf8(autoEq)));
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    QCOMPARE(int(eq.sources().size()), 1);
    QCOMPARE(eq.sources().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("HD600"));

    eq.cycleProfile(QStringLiteral("headphones"), 1);
    QCOMPARE(eq.key(QStringLiteral("headphones")), QStringLiteral("HD600"));
    QCOMPARE(eq.name(QStringLiteral("headphones")), QStringLiteral("HD600"));
    QVERIFY(eq.editable(QStringLiteral("headphones")));
    QVERIFY(settings.value(EqFilters::key(QStringLiteral("headphones"), QStringLiteral("on"))).toBool());
    QCOMPARE(settings.value(EqFilters::key(QStringLiteral("headphones"), QStringLiteral("name"))).toString(),
             QStringLiteral("HD600"));
    QCOMPARE(settings.value(EqFilters::key(QStringLiteral("headphones"), QStringLiteral("filters"))).toStringList(),
             EqFilters::encode(*EqParser::parse(QString::fromUtf8(autoEq))));

    eq.stepGain(QStringLiteral("headphones"), 0, 1);
    QCOMPARE(eq.key(QStringLiteral("headphones")), QStringLiteral("custom"));
    QCOMPARE(eq.name(QStringLiteral("headphones")), QStringLiteral("CUSTOM"));
    QCOMPARE(settings.value(EqFilters::key(QStringLiteral("headphones"), QStringLiteral("name"))).toString(), QString());
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
    QCOMPARE(settings.value(EqFilters::key(QStringLiteral("headphones"), QStringLiteral("filters"))).toStringList(),
             EqFilters::encode(eq.profile(QStringLiteral("headphones"))));
    QCOMPARE(readFile(file), QString::fromUtf8(autoEq));

    eq.stepGain(QStringLiteral("speaker"), 0, 1);
    QCOMPARE(eq.key(QStringLiteral("speaker")), QStringLiteral("off"));
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
    QVERIFY(eq.profile(out).bands[1].enabled);
    QCOMPARE(eq.profile(out).bands[1].type, EqBand::BandPass);
    eq.stepType(out, 1, -1);
    eq.stepType(out, 1, -1);
    eq.stepType(out, 1, -1);
    QCOMPARE(eq.profile(out).bands[1].type, EqBand::Peak);
    eq.stepType(out, 1, -1);
    QVERIFY(!eq.profile(out).bands[1].enabled);
}

void EqTest::stepTypeEight()
{
    EqControl eq(nullptr);
    const QString out = QStringLiteral("usb");
    const std::optional<EqBand::Type> cycle[] = {std::nullopt,        EqBand::Peak,     EqBand::LowShelf,
                                                 EqBand::HighShelf,   EqBand::BandPass, EqBand::LowPass,
                                                 EqBand::HighPass,    EqBand::AllPass};
    constexpr int states = 8;
    for (int from = 0; from < states; ++from) {
        for (const int direction : {1, -1}) {
            for (const bool coarse : {false, true}) {
                EqProfile profile = EqProfile::flat();
                EqBand &band = profile.bands[2];
                band.enabled = cycle[from].has_value();
                band.type = cycle[from].value_or(EqBand::Peak);
                band.gain = 4.5;
                eq.setCustom(out, profile);
                eq.stepBand(out, 2, QStringLiteral("type"), direction, coarse);
                const EqBand next = eq.profile(out).bands[2];
                const std::optional<EqBand::Type> want = cycle[((from + direction) % states + states) % states];
                const QByteArray where = QByteArray::number(from) + " " + QByteArray::number(direction);
                QVERIFY2(next.enabled == want.has_value(), where.constData());
                if (want)
                    QVERIFY2(next.type == *want, where.constData());
                QVERIFY2(next.gain == 4.5, where.constData());
            }
        }
    }

    EqProfile profile = EqProfile::flat();
    profile.bands[0].gain = -3.0;
    eq.setCustom(out, profile);
    for (int i = 0; i < 4; ++i)
        eq.stepType(out, 0, 1);
    QCOMPARE(eq.profile(out).bands[0].type, EqBand::LowPass);
    QCOMPARE(eq.profile(out).bands[0].gain, -3.0);
    for (int i = 0; i < 4; ++i)
        eq.stepType(out, 0, 1);
    QCOMPARE(eq.profile(out).bands[0].type, EqBand::Peak);
    QCOMPARE(eq.profile(out).bands[0].gain, -3.0);
}

void EqTest::gainInertOnGainFree()
{
    EqControl eq(nullptr);
    const QString out = QStringLiteral("usb");
    const EqBand::Type types[] = {EqBand::BandPass, EqBand::LowPass, EqBand::HighPass, EqBand::AllPass};
    for (const EqBand::Type type : types) {
        EqProfile profile = EqProfile::flat();
        profile.bands[1].type = type;
        profile.bands[1].gain = 2.5;
        eq.setCustom(out, profile);
        QSignalSpy changed(&eq, &EqControl::profileChanged);
        for (const bool coarse : {false, true}) {
            eq.stepBand(out, 1, QStringLiteral("gain"), 1, coarse);
            eq.stepBand(out, 1, QStringLiteral("gain"), -1, coarse);
            eq.stepGain(out, 1, 1, coarse);
        }
        QCOMPARE(eq.profile(out).bands[1].gain, 2.5);
        QCOMPARE(changed.count(), 0);
    }

    EqProfile profile = EqProfile::flat();
    profile.bands[1].type = EqBand::LowPass;
    profile.bands[1].enabled = false;
    profile.bands[1].gain = 2.5;
    eq.setCustom(out, profile);
    eq.stepBand(out, 1, QStringLiteral("gain"), 1);
    QCOMPARE(eq.profile(out).bands[1].gain, 2.6);

    profile.bands[1].enabled = true;
    profile.bands[1].type = EqBand::HighShelf;
    eq.setCustom(out, profile);
    eq.stepBand(out, 1, QStringLiteral("gain"), 1);
    QCOMPARE(eq.profile(out).bands[1].gain, 2.6);
}

void EqTest::cycleProfile()
{
    const QString a = EqControl::sourceDir() + QStringLiteral("/a.txt");
    const QString b = m_dir + QStringLiteral("/music/EQ/B.txt");
    QVERIFY(writeFile(a, QString::fromUtf8(autoEq)));
    QVERIFY(writeFile(b, QString::fromUtf8(autoEq)));
    QVERIFY(writeFile(m_dir + QStringLiteral("/music/EQ/broken.txt"), QStringLiteral("nothing\n")));
    QVERIFY(writeFile(m_dir + QStringLiteral("/music/EQ/notes.md"), QString::fromUtf8(autoEq)));
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    QCOMPARE(int(eq.sources().size()), 2);
    QCOMPARE(eq.sources().at(0).toMap().value(QStringLiteral("path")).toString(), a);
    QCOMPARE(eq.sources().at(1).toMap().value(QStringLiteral("path")).toString(), b);

    const QString out = QStringLiteral("speaker");
    QCOMPARE(eq.key(out), QStringLiteral("off"));
    eq.cycleProfile(out, 1);
    QCOMPARE(eq.key(out), QStringLiteral("A"));
    eq.cycleProfile(out, 1);
    QCOMPARE(eq.key(out), QStringLiteral("B"));
    eq.cycleProfile(out, 1);
    QCOMPARE(eq.key(out), QStringLiteral("custom"));
    QVERIFY(eq.profile(out) == EqProfile::fresh());
    eq.cycleProfile(out, 1);
    QCOMPARE(eq.key(out), QStringLiteral("off"));
    QVERIFY(!settings.value(EqFilters::key(out, QStringLiteral("on"))).toBool());
    QVERIFY(eq.profile(out) == EqProfile::off());
    eq.cycleProfile(out, -1);
    QCOMPARE(eq.key(out), QStringLiteral("custom"));

    settings.setValue(EqFilters::key(out, QStringLiteral("name")), QStringLiteral("GONE"));
    QCOMPARE(eq.key(out), QStringLiteral("GONE"));
    QVERIFY(!eq.missing(out));
    eq.cycleProfile(out, 1);
    QCOMPARE(eq.key(out), QStringLiteral("off"));
    eq.cycleProfile(out, 1);
    settings.setValue(EqFilters::key(out, QStringLiteral("name")), QStringLiteral("GONE"));
    eq.cycleProfile(out, -1);
    QCOMPARE(eq.key(out), QStringLiteral("custom"));
}

void EqTest::activeOutputPicksKey()
{
    const QString file = EqControl::sourceDir() + QStringLiteral("/hp.txt");
    QVERIFY(writeFile(file, QString::fromUtf8(autoEq)));
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    QSignalSpy active(&eq, &EqControl::activeProfileChanged);
    eq.cycleProfile(QStringLiteral("headphones"), 1);
    QCOMPARE(active.count(), 0);
    EqProfile custom = EqProfile::flat();
    custom.bands[2].gain = 3.0;
    eq.setCustom(QStringLiteral("usb"), custom);
    QCOMPARE(active.count(), 0);

    eq.setActiveOutput(QStringLiteral("headphones"));
    QVERIFY(active.count() > 0);
    QVERIFY(near(eq.activeProfileMap().value(QStringLiteral("preamp")).toDouble(), -6.2));
    QCOMPARE(eq.activeName(), QStringLiteral("HP"));

    eq.setActiveOutput(QStringLiteral("speaker"));
    QCOMPARE(eq.activeProfileMap(), EqProfile::off().toMap());
    QCOMPARE(eq.activeName(), QStringLiteral("OFF"));

    eq.setActiveOutput(QStringLiteral("usb"));
    QCOMPARE(eq.activeProfileMap(), custom.toMap());
    active.clear();
    eq.stepGain(QStringLiteral("usb"), 2, 1);
    QCOMPARE(active.count(), 1);
    QVERIFY(near(eq.profile(QStringLiteral("usb")).bands[2].gain, 3.1));
    eq.cycleProfile(QStringLiteral("usb"), 1);
    QCOMPARE(eq.key(QStringLiteral("usb")), QStringLiteral("off"));
    QCOMPARE(eq.activeProfileMap(), EqProfile::off().toMap());

    eq.setActiveOutput(QStringLiteral("bt"));
    QCOMPARE(eq.activeOutput(), QStringLiteral("bt"));
    QCOMPARE(eq.key(QStringLiteral("bt")), QStringLiteral("off"));
}

void EqTest::filtersRoundTrip()
{
    EqProfile profile = *EqParser::parse(QString::fromUtf8(autoEq));
    const QStringList items = EqFilters::encode(profile);
    QCOMPARE(items, (QStringList{QStringLiteral("preamp -6.2"), QStringLiteral("lowshelf 105 6.3 0.7"),
                                 QStringLiteral("peak 1000 -3.2 1.41"), QStringLiteral("highshelf 10000 2 0.7"),
                                 QStringLiteral("peak 200 0 1"), QStringLiteral("peak 300 1 2")}));
    const EqProfile back = EqFilters::decode(items);
    QVERIFY(near(back.preamp, -6.2));
    for (int i = 0; i < EqProfile::bandCount; ++i)
        QCOMPARE(back.bands[i].enabled, profile.bands[i].enabled);
    QCOMPARE(back.bands[0].type, EqBand::LowShelf);
    QVERIFY(near(back.bands[1].gain, -3.2));
    QVERIFY(near(back.bands[1].q, 1.41));
    QVERIFY(!back.bands[3].enabled);
    QVERIFY(near(back.bands[3].freq, 200.0));
    QCOMPARE(EqFilters::encode(back), items);

    EqProfile flat = EqProfile::flat();
    QCOMPARE(EqFilters::encode(EqProfile::off()), QStringList{QStringLiteral("preamp 0")});
    flat.bands[9].gain = 1.25;
    const QStringList flatItems = EqFilters::encode(flat);
    QCOMPARE(flatItems.size(), 11);
    QCOMPARE(flatItems.at(1), QStringLiteral("peak 31.25 0 1.4"));
    const EqProfile flatBack = EqFilters::decode(flatItems);
    QCOMPARE(flatBack.bands[0].freqIndex, 4);
    QVERIFY(!flatBack.bands[0].enabled);
    QVERIFY(flatBack.bands[9].enabled);
    QCOMPARE(flatBack.bands[9].freqIndex, 58);

    EqProfile wild = EqProfile::flat();
    wild.preamp = 3.0;
    wild.bands[0].freq = 50000.0;
    wild.bands[0].gain = -40.0;
    wild.bands[0].q = 0.0;
    wild.bands[1].type = EqBand::LowPass;
    wild.bands[1].gain = 0.0;
    wild.bands[2].type = EqBand::AllPass;
    wild.bands[2].freq = 1234.56789;
    const QStringList wildItems = EqFilters::encode(wild);
    QCOMPARE(wildItems.at(0), QStringLiteral("preamp 0"));
    QCOMPARE(wildItems.at(1), QStringLiteral("peak 20000 -24 0.1"));
    QCOMPARE(wildItems.at(2), QStringLiteral("lowpass 62.5 0 1.4"));
    QCOMPARE(wildItems.at(3), QStringLiteral("allpass 1234.5679 0 1.4"));
    const QRegularExpression item(QStringLiteral("^(peak|lowshelf|highshelf|bandpass|lowpass|highpass|allpass)"
                                                 " -?[0-9]+(\\.[0-9]+)? -?[0-9]+(\\.[0-9]+)? -?[0-9]+(\\.[0-9]+)?$"));
    for (qsizetype i = 1; i < wildItems.size(); ++i)
        QVERIFY2(item.match(wildItems.at(i)).hasMatch(), qPrintable(wildItems.at(i)));
    QVERIFY(EqFilters::decode(EqFilters::encode(wild)).bands[1].enabled);

    const EqProfile junk = EqFilters::decode({QStringLiteral("preamp x"), QStringLiteral("peak 100"),
                                              QStringLiteral("notch 100 1 1"), QStringLiteral("highpass 80 0 0.7")});
    QVERIFY(near(junk.preamp, 0.0));
    QCOMPARE(junk.bands[0].type, EqBand::HighPass);
    QVERIFY(junk.bands[0].enabled);
    QVERIFY(!junk.bands[1].enabled);
    QVERIFY(EqFilters::decode({}) == EqProfile::off());
    QCOMPARE(EqFilters::key(QStringLiteral("bt"), QStringLiteral("on")), QStringLiteral("audio.eq.bt.on"));
}

void EqTest::externalChange()
{
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    EqProfile custom = EqProfile::flat();
    custom.bands[0].gain = 2.0;
    eq.setCustom(QStringLiteral("usb"), custom);
    QVERIFY(settings.value(EqFilters::key(QStringLiteral("usb"), QStringLiteral("on"))).toBool());
    QCOMPARE(settings.value(EqFilters::key(QStringLiteral("usb"), QStringLiteral("filters"))).toStringList(),
             EqFilters::encode(custom));

    QSignalSpy changed(&eq, &EqControl::profileChanged);
    const QStringList outside{QStringLiteral("preamp -3"), QStringLiteral("peak 1000 6 1")};
    settings.setValue(EqFilters::key(QStringLiteral("usb"), QStringLiteral("filters")), outside);
    QCOMPARE(changed.count(), 1);
    QVERIFY(near(eq.profile(QStringLiteral("usb")).preamp, -3.0));
    QVERIFY(near(eq.profile(QStringLiteral("usb")).bands[0].gain, 6.0));
    eq.sync();
    QVERIFY(near(EqStore::read(EqControl::storePath(QStringLiteral("usb"))).bands[0].gain, 6.0));

    eq.stepGain(QStringLiteral("usb"), 0, 1);
    QCOMPARE(settings.value(EqFilters::key(QStringLiteral("usb"), QStringLiteral("filters"))).toStringList(), outside);
    QTRY_COMPARE_WITH_TIMEOUT(settings.value(EqFilters::key(QStringLiteral("usb"), QStringLiteral("filters")))
                                  .toStringList()
                                  .value(1),
                              QStringLiteral("peak 1000 6.1 1"), waitMs);
}

void EqTest::followsOutputControl()
{
    QCOMPARE(EqControl::outputForSink(OutputControl::headphonesSink()), QStringLiteral("headphones"));
    QCOMPARE(EqControl::outputForSink(OutputControl::speakerSink()), QStringLiteral("speaker"));
    QCOMPARE(EqControl::outputForSink(QStringLiteral("alsa_output.usb-DAC.analog-stereo")), QStringLiteral("usb"));
    QCOMPARE(EqControl::outputForSink(QStringLiteral("bluez_output.00_11_22_33_44_55.1")), QStringLiteral("bt"));
    QCOMPARE(EqControl::defaultSinkOutput(), QStringLiteral("headphones"));

    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    EqProfile custom = EqProfile::flat();
    custom.bands[0].gain = -4.0;
    eq.setCustom(QStringLiteral("headphones"), custom);

    OutputControl control;
    eq.setOutputControl(&control);
    QSignalSpy applied(&control, &OutputControl::applied);
    control.apply(QStringLiteral("speaker"));
    QVERIFY(applied.wait(waitMs));
    QCOMPARE(eq.activeOutput(), QStringLiteral("headphones"));
    QCOMPARE(eq.activeProfileMap(), custom.toMap());
}

void EqTest::sinkPoll()
{
    const QString usbPactl = m_binDir.path() + QStringLiteral("/usb/pactl");
    QVERIFY(writeFile(usbPactl,
                      QStringLiteral("#!/bin/sh\n[ \"$1\" = get-default-sink ] && echo "
                                     "alsa_output.usb-Apple_USB-C_to_3.5mm.analog-stereo\nexit 0\n"),
                      true));
    const QByteArray pactl = qgetenv("MICRODAP_PACTL_BIN");
    qputenv("MICRODAP_PACTL_BIN", usbPactl.toUtf8());
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    eq.setActiveOutput(QStringLiteral("speaker"));
    eq.startSinkPoll();
    QVERIFY(eq.sinkPolling());
    QTRY_COMPARE_WITH_TIMEOUT(eq.activeOutput(), QStringLiteral("usb"), waitMs);
    qputenv("MICRODAP_PACTL_BIN", pactl);

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

void EqTest::importList()
{
    const QString card = m_dir + QStringLiteral("/music/EQ");
    QVERIFY(writeFile(card + QStringLiteral("/Demo GraphicEQ.txt"), graphicEq(5)));
    QVERIFY(writeFile(card + QStringLiteral("/Demo ParametricEQ.txt"), QString::fromUtf8(autoEq)));
    QVERIFY(writeFile(card + QStringLiteral("/junk.txt"), QStringLiteral("GraphicEQ: 20 1; 30 1\n")));
    QVERIFY(writeFile(EqControl::sourceDir() + QStringLiteral("/Mine GraphicEQ.txt"), graphicEq(3)));
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    QCOMPARE(names(eq.imports()), (QStringList{QStringLiteral("Mine"), QStringLiteral("Demo")}));
    QCOMPARE(eq.imports().at(1).toMap().value(QStringLiteral("path")).toString(), card + QStringLiteral("/Demo GraphicEQ.txt"));
    QCOMPARE(names(eq.sources()), QStringList{QStringLiteral("DEMO")});
    QCOMPARE(EqControl::importName(QStringLiteral("/x/A B GraphicEQ.txt")), QStringLiteral("A B"));
    QCOMPARE(EqControl::importName(QStringLiteral("/x/c.TXT")), QStringLiteral("c"));
    QVERIFY(!EqControl::nameOk(QStringLiteral("Demo GraphicEQ")));
    QSignalSpy changed(&eq, &EqControl::importsChanged);
    settings.setRoots({m_dir + QStringLiteral("/other")});
    QCOMPARE(names(eq.imports()), QStringList{QStringLiteral("Mine")});
    QCOMPARE(changed.size(), 1);
}

void EqTest::importSaves()
{
    const QString card = m_dir + QStringLiteral("/music/EQ");
    const QString demo = card + QStringLiteral("/Demo GraphicEQ.txt");
    const QString mine = EqControl::sourceDir() + QStringLiteral("/Mine GraphicEQ.txt");
    QVERIFY(writeFile(demo, graphicEq(5)));
    QVERIFY(writeFile(card + QStringLiteral("/Demo ParametricEQ.txt"), QString::fromUtf8(autoEq)));
    QVERIFY(writeFile(mine, graphicEq(3)));
    Gate gate;
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    const QString out = QStringLiteral("usb");
    eq.cycleProfile(out, 1);
    QCOMPARE(eq.key(out), QStringLiteral("DEMO"));
    const QString headphones = eq.key(QStringLiteral("headphones"));
    eq.fitOptions().onIteration = [&gate](int i) {
        if (i == 1)
            gate.wait();
    };
    QSignalSpy notes(&eq, &EqControl::importNote);
    QSignalSpy importing(&eq, &EqControl::importingChanged);
    QCOMPARE(eq.importGraphic(out, demo), QString());
    QCOMPARE(eq.importing(), demo);
    QCOMPARE(importing.size(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(gate.waits.load(), 1, waitMs);
    QCOMPARE(eq.importGraphic(out, mine), QString());
    QCOMPARE(eq.importGraphic(QStringLiteral("headphones"), mine), QString());
    QCOMPARE(eq.importing(), demo);
    QCOMPARE(importing.size(), 1);
    gate.open = true;
    QTRY_COMPARE_WITH_TIMEOUT(notes.size(), 1, waitMs);
    QCOMPARE(notes.at(0).at(0).toString(), QStringLiteral("imported"));
    QCOMPARE(notes.at(0).at(1).toString(), QStringLiteral("Demo 2"));
    QCOMPARE(eq.importing(), QString());
    QCOMPARE(importing.size(), 2);
    QCOMPARE(gate.waits.load(), 1);
    const QString saved = EqControl::presetDir() + QStringLiteral("/Demo 2.txt");
    QVERIFY(QFileInfo::exists(saved));
    const std::optional<EqProfile> back = EqParser::parseFile(saved);
    QVERIFY(back);
    QCOMPARE(back->count, 10);
    QVERIFY(back->preamp < 0);
    QCOMPARE(eq.key(out), QStringLiteral("DEMO 2"));
    QVERIFY(eq.saved(out));
    QCOMPARE(EqFilters::encode(eq.profile(out)), EqFilters::encode(*back));
    QCOMPARE(eq.key(QStringLiteral("headphones")), headphones);
    QVERIFY(names(eq.sources()).contains(QStringLiteral("DEMO 2")));
    eq.fitOptions().onIteration = nullptr;
    QCOMPARE(eq.importGraphic(out, demo), QString());
    QTRY_COMPARE_WITH_TIMEOUT(notes.size(), 2, waitMs);
    QCOMPARE(notes.at(1).at(1).toString(), QStringLiteral("Demo 3"));
    QVERIFY(QFileInfo::exists(EqControl::presetDir() + QStringLiteral("/Demo 3.txt")));
    QCOMPARE(eq.key(out), QStringLiteral("DEMO 3"));
    gate.open = false;
    gate.waits = 0;
    eq.fitOptions().onIteration = [&gate](int i) {
        if (i == 1)
            gate.wait();
    };
    QCOMPARE(eq.importGraphic(out, mine), QString());
    QTRY_COMPARE_WITH_TIMEOUT(gate.waits.load(), 1, waitMs);
    eq.cycleProfile(out, -1);
    const QString moved = eq.key(out);
    QVERIFY(moved != QStringLiteral("DEMO 3"));
    gate.open = true;
    QTRY_COMPARE_WITH_TIMEOUT(notes.size(), 3, waitMs);
    QCOMPARE(eq.key(out), moved);
    QCOMPARE(notes.at(2).at(1).toString(), QStringLiteral("Mine"));
    QVERIFY(QFileInfo::exists(EqControl::presetDir() + QStringLiteral("/Mine.txt")));
    QCOMPARE(eq.importPresetName(QStringLiteral("Custom")), QStringLiteral("Custom 2"));
    QCOMPARE(eq.importPresetName(QString(70, QLatin1Char('a'))).toUtf8().size(), EqControl::presetNameMax);
    QCOMPARE(eq.importPresetName(QStringLiteral("  Lead")), QStringLiteral("Lead"));
    QString wide;
    for (int i = 0; i < 17; ++i)
        wide += QString::fromUtf8("\xF0\x9F\x8E\xA7");
    const QString cut = eq.importPresetName(wide);
    QCOMPARE(cut.toUtf8().size(), 64);
    QCOMPARE(QString::fromUtf8(cut.toUtf8()), cut);
    QVERIFY(EqControl::nameOk(cut));
}

void EqTest::importCardGone()
{
    const QString card = m_dir + QStringLiteral("/music/EQ");
    const QString demo = card + QStringLiteral("/Demo GraphicEQ.txt");
    QVERIFY(writeFile(demo, graphicEq(5)));
    Gate gate;
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    eq.fitOptions().onIteration = [&gate](int i) {
        if (i == 1)
            gate.wait();
    };
    QSignalSpy notes(&eq, &EqControl::importNote);
    QCOMPARE(eq.importGraphic(QStringLiteral("headphones"), demo), QString());
    QTRY_COMPARE_WITH_TIMEOUT(gate.waits.load(), 1, waitMs);
    QVERIFY(QDir(m_dir + QStringLiteral("/music")).removeRecursively());
    settings.setRoots({m_dir + QStringLiteral("/music"), m_dir + QStringLiteral("/other")});
    QVERIFY(eq.imports().isEmpty());
    gate.open = true;
    QTRY_COMPARE_WITH_TIMEOUT(notes.size(), 1, waitMs);
    QCOMPARE(notes.at(0).at(0).toString(), QStringLiteral("import failed"));
    QCOMPARE(notes.at(0).at(1).toString(), QStringLiteral("Demo"));
    QVERIFY(QDir(EqControl::presetDir()).entryList(QDir::Files).isEmpty());
    QCOMPARE(eq.importing(), QString());

    QVERIFY(writeFile(demo, graphicEq(5)));
    eq.relist();
    gate.open = false;
    gate.waits = 0;
    QCOMPARE(eq.importGraphic(QStringLiteral("headphones"), demo), QString());
    QTRY_COMPARE_WITH_TIMEOUT(gate.waits.load(), 1, waitMs);
    QVERIFY(QFile::remove(demo));
    gate.open = true;
    QTRY_COMPARE_WITH_TIMEOUT(notes.size(), 2, waitMs);
    QCOMPARE(notes.at(1).at(0).toString(), QStringLiteral("import failed"));
    QVERIFY(QDir(EqControl::presetDir()).entryList(QDir::Files).isEmpty());
}

void EqTest::importRefused()
{
    const QString card = m_dir + QStringLiteral("/music/EQ");
    const QString demo = card + QStringLiteral("/Demo GraphicEQ.txt");
    const QString spike = card + QStringLiteral("/Spike GraphicEQ.txt");
    QVERIFY(writeFile(demo, graphicEq(5)));
    QString text = QStringLiteral("GraphicEQ: ");
    for (int i = 0; i < EqFit::points; ++i) {
        const double f = EqFit::frequencies[i];
        text += (i ? QStringLiteral("; ") : QString()) + QString::number(f)
                + (f > 900 && f < 1100 ? QStringLiteral(" 12") : QStringLiteral(" -24"));
    }
    QVERIFY(writeFile(spike, text + QLatin1Char('\n')));
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    EqControl eq(&settings);
    QSignalSpy notes(&eq, &EqControl::importNote);
    QCOMPARE(eq.importGraphic(QStringLiteral("nowhere"), demo), QString());
    QCOMPARE(eq.importGraphic(QStringLiteral("headphones"), card + QStringLiteral("/Other GraphicEQ.txt")), QString());
    QCOMPARE(eq.importing(), QString());
    eq.cycleMode(QStringLiteral("speaker"), 1);
    QVERIFY(eq.graphic(QStringLiteral("speaker")));
    QCOMPARE(eq.importGraphic(QStringLiteral("speaker"), demo), QString());
    QCOMPARE(eq.importing(), QString());
    QCOMPARE(eq.importGraphic(QStringLiteral("headphones"), spike), QString());
    QTRY_COMPARE_WITH_TIMEOUT(notes.size(), 1, waitMs);
    QCOMPARE(notes.at(0).at(0).toString(), QStringLiteral("import failed"));
    QCOMPARE(notes.at(0).at(1).toString(), QStringLiteral("boost over 24 db"));
    QVERIFY(QDir(EqControl::presetDir()).entryList(QDir::Files).isEmpty());
    for (int i = 1; i <= EqControl::presetsMax; ++i)
        QVERIFY(writeFile(EqControl::presetDir() + QStringLiteral("/P%1.txt").arg(i), QString::fromUtf8(autoEq)));
    eq.relist();
    QCOMPARE(eq.importGraphic(QStringLiteral("headphones"), demo), QStringLiteral("too many presets"));
    QCOMPARE(eq.importing(), QString());
    QCOMPARE(notes.size(), 1);
}

void EqTest::importLate()
{
    const QString demo = m_dir + QStringLiteral("/music/EQ/Demo GraphicEQ.txt");
    QVERIFY(writeFile(demo, graphicEq(5)));
    Gate gate;
    Settings settings(nullptr);
    settings.setRoots({m_dir + QStringLiteral("/music")});
    {
        EqControl eq(&settings);
        eq.fitOptions().onIteration = [&gate](int) { gate.wait(); };
        QCOMPARE(eq.importGraphic(QStringLiteral("headphones"), demo), QString());
        QTRY_COMPARE_WITH_TIMEOUT(gate.waits.load(), 1, waitMs);
        gate.open = true;
    }
    QVERIFY(!QFileInfo::exists(EqControl::presetDir()));
    gate.open = false;
    gate.waits = 0;
    EqControl eq(&settings);
    QSignalSpy notes(&eq, &EqControl::importNote);
    eq.fitOptions().onIteration = [&gate](int i) {
        if (i == 1)
            gate.wait();
    };
    QCOMPARE(eq.importGraphic(QStringLiteral("headphones"), demo), QString());
    QTRY_COMPARE_WITH_TIMEOUT(gate.waits.load(), 1, waitMs);
    for (int i = 1; i <= EqControl::presetsMax; ++i)
        QVERIFY(writeFile(EqControl::presetDir() + QStringLiteral("/P%1.txt").arg(i), QString::fromUtf8(autoEq)));
    eq.relist();
    gate.open = true;
    QTRY_COMPARE_WITH_TIMEOUT(notes.size(), 1, waitMs);
    QCOMPARE(notes.at(0).at(0).toString(), QStringLiteral("too many presets"));
    QVERIFY(!QFileInfo::exists(EqControl::presetDir() + QStringLiteral("/Demo.txt")));
    QVERIFY(QDir(EqControl::presetDir()).removeRecursively());
    QVERIFY(writeFile(EqControl::presetDir(), QStringLiteral("not a folder\n")));
    eq.relist();
    eq.fitOptions().onIteration = nullptr;
    QCOMPARE(eq.importGraphic(QStringLiteral("headphones"), demo), QString());
    QTRY_COMPARE_WITH_TIMEOUT(notes.size(), 2, waitMs);
    QCOMPARE(notes.at(1).at(0).toString(), QStringLiteral("preset not saved"));
    QCOMPARE(notes.at(1).at(1).toString(), QStringLiteral("Demo"));
}

QTEST_GUILESS_MAIN(EqTest)
#include "tst_eq.moc"
