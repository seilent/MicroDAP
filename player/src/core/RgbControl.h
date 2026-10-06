#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace casdap {

class RgbControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)
    Q_PROPERTY(bool probed READ probed NOTIFY probedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(QString accent READ accent NOTIFY accentChanged)

public:
    explicit RgbControl(QObject *parent = nullptr);
    ~RgbControl() override;

    static QString rgbPath();
    static QString systemctlPath();
    static QStringList accentHexes();
    static QStringList accentNames();
    static QString accentName(const QString &hex);
    static QString unitFor(const QString &word);

    bool supported() const;
    bool probed() const;
    bool busy() const;
    QString mode() const;
    QString accent() const;

    Q_INVOKABLE void start(const QString &mode, const QString &accent);
    Q_INVOKABLE void setMode(const QString &mode);
    Q_INVOKABLE void setAccent(const QString &accent);

signals:
    void supportedChanged();
    void probedChanged();
    void busyChanged();
    void modeChanged();
    void accentChanged();
    void applied(const QString &word, bool ok);

private:
    void probe();
    void applyMode();
    void startWords(const QStringList &words);
    void nextWord();
    void updateBusy();
    void dropProcess(QProcess *&slot);

    bool m_supported = false;
    bool m_probed = false;
    bool m_busy = false;
    QString m_mode;
    QString m_accent;
    QStringList m_words;
    QProcess *m_probe = nullptr;
    QProcess *m_word = nullptr;
};

}
