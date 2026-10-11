#pragma once

#include "Fiio.h"
#include "FiioWorker.h"
#include "OutputPorts.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>

namespace casdap {

class EqControl;
struct EqProfile;

class DacEq : public QObject, public DacEqPort
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString reason READ reason NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap ranges READ ranges CONSTANT)
    Q_PROPERTY(bool dacHeld READ dacHeld NOTIFY dacHeldChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY modeChanged)

public:
    struct Bounds
    {
        int coalesceMs = 150;
        int firstResolveMs = 3000;
        int gateRetryMs = 2000;
        int refusedRearms = 3;
        FiioWorker::Bounds worker;
    };

    struct Ports
    {
        EqControl *eq = nullptr;
        DacSinkPort *dac = nullptr;
        PlayerPort *player = nullptr;
        std::unique_ptr<UsbIo> usb;
        FiioPaths paths;
        std::function<bool()> releasing;
    };

    DacEq(Bounds bounds, Ports ports, QObject *parent = nullptr);
    ~DacEq() override;

    static FiioProfileFrames framesFor(const FiioModel &model, const EqProfile &profile);

    void start();

    QString status() const;
    QString reason() const;
    QVariantMap ranges() const;
    bool dacHeld() const;
    QString mode() const;

    void startRelease(std::function<void()> released) override;
    void quitAndJoin(int ms) override;

public slots:
    void setLevel(const QString &level);
    void onFirstApplied();
    void onSinkAppeared(quint32 index, const QString &name, int card);
    void onCardRemoved(quint32 cardIndex);
    void onActiveOutputChanged(const QString &active);
    void onPlayStateChanged();
    void onSwitchingChanged(bool switching);
    void onProfileChanged(const QString &output);
    void onEffectiveChanged(bool effective);
    void onCardChanged(const QString &id, int alsa);
    void onGenerationChanged(const QString &id, quint64 generation, bool present);
    void onCardGoneChanged(bool gone);
    void onExHeldChanged(bool held);

signals:
    void statusChanged();
    void dacHeldChanged();
    void modeChanged();

private:
    using Kind = FiioWorker::Kind;
    enum class Known { Unknown, Bypass, Slot };

    struct Record
    {
        QString id;
        int alsa = -1;
        quint32 cardIndex = 0xffffffffu;
        QString sysPath;
        const FiioModel *model = nullptr;
        quint64 generation = 0;
        int resets = 0;
        int refusals = 0;
        Known known = Known::Unknown;
        FiioProfileFrames frames;
        bool held = false;
        bool wantReset = false;
    };

    struct Want
    {
        Kind kind = Kind::Bypass;
        QString id;
        FiioProfileFrames frames;
        bool failB = false;
    };

    struct Inflight
    {
        quint64 seq = 0;
        Want want;
    };

    Record *find(const QString &id);
    const Record *find(const QString &id) const;
    Record *addRecord(int alsa, const QString &id);
    void removeRecord(QString id, const char *why);
    void bumpRecord(Record &record, const char *why);
    QString readId(int alsa) const;
    bool present(const Record &record) const;
    bool pcmClosed(int alsa) const;
    bool effectiveWithCard() const;
    bool usbParses() const;
    bool needsRelease() const;
    bool switchOnly() const;
    std::optional<Want> want() const;
    void plan();
    void post(const Want &want);
    void onBatchDone(quint64 seq, bool ok, int reason, const casdap::FiioMirror &mirror);
    void fail(Record &record, const Inflight &done, FiioReason reason);
    bool gateOpen(const Record &record) const;
    void checkGate();
    void finishRelease();
    const Record *primary() const;
    void refresh();
    QString computeStatus() const;
    QString computeMode() const;
    void log(const QString &text) const;

    Bounds m_bounds;
    EqControl *m_eq;
    DacSinkPort *m_dac;
    PlayerPort *m_player;
    FiioPaths m_paths;
    std::function<bool()> m_releasing;
    std::unique_ptr<FiioWorker> m_worker;
    QList<Record> m_records;
    std::optional<Inflight> m_inflight;
    std::function<void()> m_released;
    QString m_level = QStringLiteral("off");
    QString m_active;
    QString m_cardId;
    int m_cardAlsa = -1;
    QString m_usbKey;
    QString m_failB;
    QStringList m_releaseFailed;
    QString m_reason;
    QString m_status = QStringLiteral("none");
    QString m_mode = QStringLiteral("none");
    quint64 m_seq = 0;
    quint64 m_generations = 0;
    bool m_started = false;
    bool m_final = false;
    bool m_waitResolve = false;
    bool m_effective = false;
    bool m_cardGone = false;
    bool m_exHeld = false;
    bool m_switching = false;
    bool m_switchRelease = false;
    bool m_unsupported = false;
    bool m_failed = false;
    bool m_blocked = false;
    bool m_dacHeld = false;
    QTimer m_coalesce;
    QTimer m_resolve;
    QTimer m_gate;
    QTimer m_gateRetry;
    QTimer m_noCard;
};

}
