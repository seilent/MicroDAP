#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>

namespace casdap {

class OutputControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString output READ output NOTIFY outputChanged)
    Q_PROPERTY(QString activeOutput READ activeOutput NOTIFY activeOutputChanged)
    Q_PROPERTY(bool jackPresent READ jackPresent NOTIFY jackPresentChanged)
    Q_PROPERTY(QString usbSink READ usbSink NOTIFY usbSinkChanged)
    Q_PROPERTY(bool usbPresent READ usbPresent NOTIFY usbSinkChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QString hpGain READ hpGain NOTIFY hpGainChanged)
    Q_PROPERTY(QString hpAmp READ hpAmp NOTIFY hpAmpChanged)
    Q_PROPERTY(int spkGain READ spkGain NOTIFY spkGainChanged)

public:
    explicit OutputControl(QObject *parent = nullptr);
    ~OutputControl() override;

    static QStringList outputs();
    static QString speakerSink();
    static QString headphonesSink();
    static QString usbPrefix();
    static QString wpctlPath();
    static QString pactlPath();
    static QString amixerPath();
    static QString systemctlPath();
    static QStringList parseSinks(const QString &text);
    static QString usbSinkIn(const QStringList &sinks);
    static bool parseJack(const QString &text);
    static QString resolve(const QString &output, bool jack, bool usb);
    static QString unitFor(const QString &word);

    QString output() const;
    QString activeOutput() const;
    bool jackPresent() const;
    QString usbSink() const;
    bool usbPresent() const;
    bool busy() const;
    bool active() const;
    void setActive(bool active);
    QString hpGain() const;
    QString hpAmp() const;
    int spkGain() const;

    Q_INVOKABLE void apply(const QString &output);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setHeadphone(const QString &gain, const QString &amp);
    Q_INVOKABLE void setSpkGain(int step);

signals:
    void outputChanged();
    void activeOutputChanged();
    void jackPresentChanged();
    void usbSinkChanged();
    void busyChanged();
    void activeChanged();
    void hpGainChanged();
    void hpAmpChanged();
    void spkGainChanged();
    void applied(const QString &output, bool ok);

private:
    using Handler = std::function<void(bool ok, const QByteArray &out)>;

    void run(QProcess *&slot, const QString &program, const QStringList &args, Handler handler);
    void drop(QProcess *&slot);
    void readJack(Handler done);
    void readSinks(Handler done);
    void readBoth(std::function<void()> done);
    void resolveAndSet();
    void finish(bool ok);
    void setJackPresent(bool present);
    void setUsbSink(const QString &sink);
    void setActiveOutput(const QString &output);
    void applyHeadphone();
    void startWords(const QStringList &words);
    void nextWord();
    void updateBusy();

    QString m_output;
    QString m_activeOutput;
    bool m_jackPresent = false;
    QString m_usbSink;
    QStringList m_sinks;
    bool m_busy = false;
    bool m_applying = false;
    bool m_reading = false;
    bool m_everApplied = false;
    bool m_active = false;
    QString m_hpGain;
    QString m_hpAmp;
    int m_spkGain = 0;
    QStringList m_words;
    QTimer m_pollTimer;
    QProcess *m_apply = nullptr;
    QProcess *m_jack = nullptr;
    QProcess *m_sinkList = nullptr;
    QProcess *m_word = nullptr;
};

}
