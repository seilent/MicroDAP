#pragma once

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <optional>

namespace casdap {

class OutputControl;
class Settings;

struct EqBand
{
    enum Type {
        Peak = 0,
        LowShelf = 1,
        HighShelf = 2,
    };

    bool enabled = true;
    Type type = Peak;
    double freq = 1000.0;
    double gain = 0.0;
    double q = 1.4;
    int freqIndex = -1;

    bool operator==(const EqBand &other) const = default;
};

struct EqProfile
{
    static constexpr int bandCount = 10;

    double preamp = 0.0;
    std::array<EqBand, bandCount> bands;

    static EqProfile flat();
    static EqProfile off();
    double preampVolume() const;
    QVariantMap toMap() const;

    bool operator==(const EqProfile &other) const = default;
};

}

Q_DECLARE_METATYPE(casdap::EqProfile)

namespace casdap {

struct EqGrid
{
    static constexpr int size = 61;
    static constexpr int anchor = 34;

    static double frequency(int index);
    static int step(const EqBand &band, int direction, int count = 1);
};

struct EqParser
{
    static constexpr double minGain = -24.0;
    static constexpr double maxGain = 12.0;
    static constexpr double minFreq = 20.0;
    static constexpr double maxFreq = 20000.0;
    static constexpr double minQ = 0.1;
    static constexpr double maxQ = 10.0;
    static constexpr double minPreamp = -24.0;
    static constexpr double maxPreamp = 0.0;

    static std::optional<EqProfile> parse(const QString &text, QString *error = nullptr);
    static std::optional<EqProfile> parseFile(const QString &path, QString *error = nullptr);
    static double fallbackPreamp(const EqProfile &profile);
};

struct EqStore
{
    static QString typeName(const EqBand &band);
    static QString text(const EqProfile &profile);
    static EqProfile read(const QString &path);
    static bool write(const QString &path, const EqProfile &profile);
};

class EqControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList outputs READ outputs CONSTANT)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)
    Q_PROPERTY(QString activeOutput READ activeOutput NOTIFY activeOutputChanged)
    Q_PROPERTY(QString activeName READ activeName NOTIFY activeProfileChanged)
    Q_PROPERTY(QVariantMap activeProfile READ activeProfileMap NOTIFY activeProfileChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    static constexpr int syncDelayMs = 500;
    static constexpr int sinkTimeoutMs = 1000;
    static constexpr int sinkPollMs = 2000;
    static constexpr double gainFine = 0.1;
    static constexpr double gainCoarse = 1.0;
    static constexpr double qFine = 0.1;
    static constexpr double qCoarse = 1.0;
    static constexpr int freqCoarseSteps = 6;

    explicit EqControl(Settings *settings, QObject *parent = nullptr);
    ~EqControl() override;

    static QStringList outputs();
    static QString configDir();
    static QString sourceDir();
    static QString storePath(const QString &output);
    static QString displayName(const QString &path);
    static QString outputForSink(const QString &sink);
    static QString defaultSinkOutput();

    void setOutputControl(OutputControl *control);
    void startSinkPoll();
    bool sinkPolling() const;

    QVariantList sources() const;
    QString activeOutput() const;
    void setActiveOutput(const QString &output);
    QString activeName() const;
    QVariantMap activeProfileMap() const;
    EqProfile activeProfile() const;
    int revision() const;
    bool bypassed() const;
    void setBypassed(bool on);

    EqProfile profile(const QString &output) const;
    void setCustom(const QString &output, const EqProfile &profile);

    Q_INVOKABLE QString key(const QString &output) const;
    Q_INVOKABLE QString name(const QString &output) const;
    Q_INVOKABLE bool missing(const QString &output) const;
    Q_INVOKABLE bool editable(const QString &output) const;
    Q_INVOKABLE QVariantMap profileMap(const QString &output) const;
    Q_INVOKABLE void relist();
    Q_INVOKABLE void cycleProfile(const QString &output, int direction);
    Q_INVOKABLE void stepPreamp(const QString &output, int direction, bool coarse = false);
    Q_INVOKABLE void stepType(const QString &output, int band, int direction);
    Q_INVOKABLE void stepFrequency(const QString &output, int band, int direction, bool coarse = false);
    Q_INVOKABLE void stepGain(const QString &output, int band, int direction, bool coarse = false);
    Q_INVOKABLE void stepQ(const QString &output, int band, int direction, bool coarse = false);
    Q_INVOKABLE void stepBand(const QString &output, int band, const QString &field, int direction,
                              bool coarse = false);
    Q_INVOKABLE void sync();

signals:
    void sourcesChanged();
    void activeOutputChanged();
    void activeProfileChanged();
    void revisionChanged();
    void profilesChanged();
    void profileChanged(const QString &output);
    void profileApplied(const casdap::EqProfile &profile);

private:
    std::optional<EqProfile> fileProfile(const QString &path) const;
    EqProfile &custom(const QString &output) const;
    void setKey(const QString &output, const QString &value);
    void keyChanged(const QString &output);
    EqProfile *beginEdit(const QString &output);
    EqBand *beginBandEdit(const QString &output, int band);
    void endEdit(const QString &output);
    void changed(const QString &output);
    void applyActive();
    void pollSink();
    void dropSinkProcess();

    Settings *m_settings;
    OutputControl *m_outputControl = nullptr;
    QProcess *m_sinkProcess = nullptr;
    QTimer m_sinkTimer;
    bool m_sinkWarned = false;
    QString m_activeOutput;
    QVariantList m_sources;
    QStringList m_sourcePaths;
    QHash<QString, QString> m_keys;
    mutable QHash<QString, EqProfile> m_custom;
    mutable QHash<QString, std::optional<EqProfile>> m_files;
    QSet<QString> m_dirty;
    QTimer m_syncTimer;
    EqProfile m_applied = EqProfile::off();
    bool m_appliedOnce = false;
    bool m_bypassed = false;
    bool m_editing = false;
    int m_revision = 0;
};

}
