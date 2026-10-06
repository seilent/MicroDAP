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
    Q_PROPERTY(bool jackPresent READ jackPresent NOTIFY jackPresentChanged)
    Q_PROPERTY(bool headphonesDefault READ headphonesDefault NOTIFY headphonesDefaultChanged)
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
    static QString sinkFor(const QString &output);
    static QString wpctlPath();
    static QString pwMetadataPath();
    static QString amixerPath();
    static QString systemctlPath();
    static int sinkId(const QString &status, const QString &name);
    static QString defaultSink(const QString &status);
    static bool parseJack(const QString &text);
    static QString unitFor(const QString &word);

    QString output() const;
    bool jackPresent() const;
    bool headphonesDefault() const;
    bool busy() const;
    bool active() const;
    void setActive(bool active);
    QString hpGain() const;
    QString hpAmp() const;
    int spkGain() const;

    Q_INVOKABLE void apply(const QString &output);
    Q_INVOKABLE void refreshJack();
    Q_INVOKABLE void setHeadphone(const QString &gain, const QString &amp);
    Q_INVOKABLE void setSpkGain(int step);

signals:
    void outputChanged();
    void jackPresentChanged();
    void headphonesDefaultChanged();
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
    void finish(bool ok);
    void setJackPresent(bool present);
    void refreshDefault();
    void applyHeadphone();
    void startWords(const QStringList &words);
    void nextWord();
    void updateBusy();
    void setHeadphonesDefault(bool headphones);

    QString m_output;
    bool m_jackPresent = false;
    bool m_jackRead = false;
    bool m_headphonesDefault = false;
    bool m_busy = false;
    bool m_applying = false;
    bool m_active = false;
    QString m_hpGain;
    QString m_hpAmp;
    int m_spkGain = 0;
    QStringList m_words;
    QTimer m_jackTimer;
    QProcess *m_apply = nullptr;
    QProcess *m_jack = nullptr;
    QProcess *m_status = nullptr;
    QProcess *m_word = nullptr;
};

}
