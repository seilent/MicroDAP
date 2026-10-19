#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>

class QSocketNotifier;

namespace microdap {

struct MixerRange
{
    long min = 0;
    long max = 0;
    double minDb = 0.0;
    double maxDb = 0.0;
};

class MixerBackend
{
public:
    virtual ~MixerBackend() = default;

    virtual int open(const QString &cardId) = 0;
    virtual void close() = 0;
    virtual MixerRange range() const = 0;
    virtual int get(long *value) = 0;
    virtual int set(long value) = 0;
    virtual int dbOf(long value, double *db) = 0;
    virtual QList<int> pollFds() = 0;
    virtual short revents() = 0;
    virtual int handleEvents() = 0;
};

class SndMixer : public MixerBackend
{
public:
    SndMixer();
    ~SndMixer() override;

    int open(const QString &cardId) override;
    void close() override;
    MixerRange range() const override;
    int get(long *value) override;
    int set(long value) override;
    int dbOf(long value, double *db) override;
    QList<int> pollFds() override;
    short revents() override;
    int handleEvents() override;

private:
    struct Handle;
    std::unique_ptr<Handle> m_handle;
};

class AlsaVolume : public QObject
{
    Q_OBJECT

public:
    static constexpr int defaultPacingMs = 250;
    static constexpr int maxRewrites = 8;
    static constexpr int pullWindowMs = 1000;
    static constexpr double linearToleranceDb = 1.0;

    explicit AlsaVolume(std::unique_ptr<MixerBackend> backend, int pacingMs = defaultPacingMs,
                        QObject *parent = nullptr);
    ~AlsaVolume() override;

    static double percentOfDb(double db);
    static double dbOfPercent(double percent);
    static double dbOfStep(int step, const MixerRange &range);
    static int stepFor(int step, int deltaPercent, const MixerRange &range);
    static int capStepOf(int capPercent, const MixerRange &range);

    bool open(const QString &cardId);
    void close();
    bool isOpen() const;
    QString cardId() const;
    int step() const;
    double stepDb() const;
    int hwStep() const;
    double hwDb() const;
    double rangeDb() const;
    void setOwnedStep(int step);
    void setStep(int step);
    void pressed(int deltaPercent);
    void setPercent(int percent, bool report = true);
    bool setCap(int percent);
    int cap() const;
    int capStep() const;
    void watchReturn(int ms);
    void nudge();

signals:
    void levelChanged(int percent);
    void foreignStep(int step);
    void disconnected();

private:
    bool usableRange(MixerRange *range);
    int clamped(int step) const;
    void pullDown();
    void onActivated();
    void write();
    void lose(const char *reason);
    void closeMixer();

    std::unique_ptr<MixerBackend> m_backend;
    int m_pacingMs;
    QList<QSocketNotifier *> m_notifiers;
    QTimer m_send;
    QTimer m_watch;
    QTimer m_pullTimer;
    QElapsedTimer m_clock;
    QString m_cardId;
    MixerRange m_range;
    qint64 m_sentMs;
    qint64 m_pullStartMs = -pullWindowMs;
    long m_hw = -1;
    int m_owned = 0;
    int m_cap = 100;
    int m_rewrites = 0;
    int m_pulls = 0;
    bool m_open = false;
    bool m_pending = false;
};

}
