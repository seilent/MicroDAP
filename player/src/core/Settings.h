#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

namespace casdap {

class Settings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString output READ output WRITE setOutput NOTIFY outputChanged)
    Q_PROPERTY(QStringList roots READ roots WRITE setRoots NOTIFY rootsChanged)
    Q_PROPERTY(QString accent READ accent WRITE setAccent NOTIFY accentChanged)
    Q_PROPERTY(double scale READ scale WRITE setScale NOTIFY scaleChanged)
    Q_PROPERTY(bool pocketControls READ pocketControls WRITE setPocketControls NOTIFY pocketControlsChanged)
    Q_PROPERTY(int saverSeconds READ saverSeconds WRITE setSaverSeconds NOTIFY saverSecondsChanged)
    Q_PROPERTY(QString path READ path CONSTANT)

public:
    static constexpr double minScale = 0.8;
    static constexpr double maxScale = 1.4;
    static constexpr int minSaverSeconds = 10;
    static constexpr int maxSaverSeconds = 300;
    static constexpr int syncDelayMs = 500;

    explicit Settings(QObject *parent = nullptr);
    explicit Settings(const QString &path, QObject *parent = nullptr);
    ~Settings() override;

    static QString defaultPath();
    static QStringList outputs();
    static QStringList accents();
    static QStringList defaultRoots();

    QString path() const;

    QString output() const;
    void setOutput(const QString &output);
    QStringList roots() const;
    void setRoots(const QStringList &roots);
    QString accent() const;
    void setAccent(const QString &accent);
    double scale() const;
    void setScale(double scale);
    bool pocketControls() const;
    void setPocketControls(bool on);
    int saverSeconds() const;
    void setSaverSeconds(int seconds);

    Q_INVOKABLE void sync();

signals:
    void outputChanged();
    void rootsChanged();
    void accentChanged();
    void scaleChanged();
    void pocketControlsChanged();
    void saverSecondsChanged();

private:
    void load();
    void store(const QString &key, const QVariant &value);

    QSettings m_settings;
    QTimer m_syncTimer;
    QVariantMap m_dirty;
    QString m_output;
    QStringList m_roots;
    QString m_accent;
    double m_scale = 1.0;
    bool m_pocketControls = false;
    int m_saverSeconds = 30;
};

}
