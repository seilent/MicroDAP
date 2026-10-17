#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariant>

#include <optional>

namespace MicroSet {
class Client;
}

namespace microdap {

class RgbControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)
    Q_PROPERTY(bool probed READ probed NOTIFY probedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(int hue READ hue NOTIFY hueChanged)
    Q_PROPERTY(int level READ level NOTIFY levelChanged)
    Q_PROPERTY(bool screenOff READ screenOff NOTIFY screenOffChanged)
    Q_PROPERTY(bool saver READ saver NOTIFY saverChanged)

public:
    static constexpr int hueDelayMs = 500;
    static constexpr int levels = 4;

    explicit RgbControl(MicroSet::Client *client, QObject *parent = nullptr);
    ~RgbControl() override;

    static QString enabledKey();
    static QString hueKey();
    static QString saturationKey();
    static int saturationFor(int level);

    bool supported() const;
    bool probed() const;
    bool busy() const;
    QString mode() const;
    int hue() const;
    int level() const;
    bool screenOff() const;
    bool saver() const;
    std::optional<QVariant> sent(const QString &key) const;

    Q_INVOKABLE void start(const QString &mode, int hue, int level = 0);
    Q_INVOKABLE void setMode(const QString &mode);
    Q_INVOKABLE void setHue(int hue);
    Q_INVOKABLE void setColor(int hue, int level);
    Q_INVOKABLE void setScreenOff(bool off);
    Q_INVOKABLE void setSaver(bool on);

signals:
    void supportedChanged();
    void probedChanged();
    void busyChanged();
    void modeChanged();
    void hueChanged();
    void levelChanged();
    void screenOffChanged();
    void saverChanged();
    void applied(const QString &key, bool ok);

private:
    std::optional<QVariant> wanted(const QString &key) const;
    void apply();
    void send(const QString &key);
    void finished(const QString &key, bool ok);
    void updateBusy();

    MicroSet::Client *m_client;
    bool m_supported = false;
    bool m_probed = false;
    bool m_busy = false;
    bool m_screenOff = false;
    bool m_saver = false;
    QString m_mode;
    int m_hue = 350;
    int m_level = 0;
    int m_inflight = 0;
    QHash<QString, QVariant> m_sent;
    QTimer m_hueTimer;
};

}
