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
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

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
    static int sinkId(const QString &status, const QString &name);
    static bool parseJack(const QString &text);

    QString output() const;
    bool jackPresent() const;
    bool busy() const;
    bool active() const;
    void setActive(bool active);

    Q_INVOKABLE void apply(const QString &output);
    Q_INVOKABLE void refreshJack();

signals:
    void outputChanged();
    void jackPresentChanged();
    void busyChanged();
    void activeChanged();
    void applied(const QString &output, bool ok);

private:
    using Handler = std::function<void(bool ok, const QByteArray &out)>;

    void run(QProcess *&slot, const QString &program, const QStringList &args, Handler handler);
    void drop(QProcess *&slot);
    void finish(bool ok);
    void setJackPresent(bool present);

    QString m_output;
    bool m_jackPresent = false;
    bool m_busy = false;
    bool m_active = false;
    QTimer m_jackTimer;
    QProcess *m_apply = nullptr;
    QProcess *m_jack = nullptr;
};

}
