#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

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

    explicit RgbControl(QObject *parent = nullptr);
    ~RgbControl() override;

    static QString rgbPath();
    static QString systemctlPath();
    static QString hueWord(int hue);
    static QString colorWord(int hue, int level);
    static QString unitFor(const QString &word);

    bool supported() const;
    bool probed() const;
    bool busy() const;
    QString mode() const;
    int hue() const;
    int level() const;
    bool screenOff() const;
    bool saver() const;
    int sentHue() const;
    QString sentWord() const;

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
    void applied(const QString &word, bool ok);

private:
    void probe();
    void applyMode();
    void sendHue();
    void queueHue();
    void sleepLights();
    void startWords(const QStringList &words);
    void nextWord();
    void updateBusy();
    void dropProcess(QProcess *&slot);
    bool ready() const;

    bool m_supported = false;
    bool m_probed = false;
    bool m_busy = false;
    bool m_screenOff = false;
    bool m_saver = false;
    bool m_asleep = false;
    QString m_mode;
    int m_hue = 350;
    int m_level = 0;
    QString m_sentWord;
    QStringList m_words;
    QTimer m_hueTimer;
    QProcess *m_probe = nullptr;
    QProcess *m_word = nullptr;
};

}
