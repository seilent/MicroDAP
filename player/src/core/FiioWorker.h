#pragma once

#include "Fiio.h"

#include <QMetaType>
#include <QObject>
#include <QString>

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <string>

class QThread;

Q_DECLARE_METATYPE(casdap::FiioMirror)

namespace casdap {

class FiioRunner;

class FiioWorker : public QObject
{
    Q_OBJECT

public:
    enum class Kind { Take, Update, Bypass, Force, Reset };

    struct Target
    {
        QString cardId;
        quint64 generation = 0;
        int alsaIndex = -1;
        QString sysPath;
        bool exHeld = false;
        Kind kind = Kind::Bypass;
        FiioProfileFrames frames;
        quint64 seq = 0;
    };

    struct Bounds
    {
        FiioDevice::Bounds device;
        int driverWaitMs = 2000;
        int lockMs = 1000;
        int retryMs = 500;
    };

    FiioWorker(std::unique_ptr<UsbIo> usb, Bounds bounds, FiioPaths paths, QObject *parent = nullptr);
    ~FiioWorker() override;

    void post(const Target &target);
    void postFinal(const Target &target, int ms);
    void cancel();
    bool quitAndJoin(int ms);

signals:
    void batchDone(quint64 seq, bool ok, int reason, const casdap::FiioMirror &mirror);

private:
    void quit();

    QThread *m_thread;
    FiioRunner *m_runner;
    bool m_quitting = false;
};

class FiioRunner : public QObject
{
    Q_OBJECT

public:
    FiioRunner(std::unique_ptr<UsbIo> usb, FiioWorker::Bounds bounds, FiioPaths paths);

    void run(const FiioWorker::Target &target);

    std::atomic<quint64> latestSeq{0};
    std::atomic<quint64> quitSeq{0};
    std::atomic<int64_t> quitDeadline{0};

signals:
    void done(quint64 seq, bool ok, int reason, const casdap::FiioMirror &mirror);

private:
    FiioReason attempt(const FiioWorker::Target &target, const FiioModel &model, FiioMirror &mirror,
                       const FiioBatch::Abandoned &abandoned, std::optional<int64_t> deadline);
    FiioReason reset(const FiioWorker::Target &target, FiioDevice &device);
    FiioMirror &mirrorFor(const FiioWorker::Target &target);

    std::unique_ptr<UsbIo> m_usb;
    FiioWorker::Bounds m_bounds;
    FiioPaths m_paths;
    std::map<std::string, FiioMirror> m_mirrors;
};

}
