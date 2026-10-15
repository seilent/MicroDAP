#pragma once

#include "ArtColor.h"

#include <QColor>
#include <QObject>
#include <QString>

namespace microdap {

class AccentControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int sliderHue READ sliderHue WRITE setSliderHue NOTIFY sliderHueChanged)
    Q_PROPERTY(bool fromArt READ fromArt WRITE setFromArt NOTIFY fromArtChanged)
    Q_PROPERTY(int artHue READ artHue NOTIFY artHueChanged)
    Q_PROPERTY(int artLevel READ artLevel NOTIFY artHueChanged)
    Q_PROPERTY(int hue READ hue NOTIFY hueChanged)
    Q_PROPERTY(int level READ level NOTIFY hueChanged)
    Q_PROPERTY(QColor color READ color NOTIFY hueChanged)
    Q_PROPERTY(QColor brightColor READ brightColor NOTIFY hueChanged)
    Q_PROPERTY(QString artPath READ artPath NOTIFY artPathChanged)
    Q_PROPERTY(bool phoneActive READ phoneActive NOTIFY phoneActiveChanged)

public:
    static constexpr double accentValue = 0.98;
    static constexpr double minLuminance = 0.149;
    static constexpr double brightScale = 0.84;
    static constexpr double maxSaturation = 0.83;
    static constexpr double minSaturation = 0.40;

    explicit AccentControl(QObject *parent = nullptr);

    static double saturationFor(int hue);
    static QColor hsv(int hue, double saturation, double value);
    static double luminance(const QColor &color);
    static QColor accentFor(int hue);
    static QColor brightFor(int hue);
    static QColor artAccentFor(int hue, const QColor &tone);
    static QColor artBrightFor(int hue, const QColor &tone);

    int sliderHue() const;
    void setSliderHue(int hue);
    bool fromArt() const;
    void setFromArt(bool on);
    int artHue() const;
    int artLevel() const;
    QColor artTone() const;
    int hue() const;
    int level() const;
    QColor color() const;
    QColor brightColor() const;
    QString artPath() const;
    bool phoneActive() const;
    ArtColor *artColor();

    Q_INVOKABLE QColor colorFor(int hue) const;
    Q_INVOKABLE QColor brightColorFor(int hue) const;
    Q_INVOKABLE void setArtPath(const QString &path);
    Q_INVOKABLE void setPhoneActive(bool active);
    Q_INVOKABLE void clearCache();

signals:
    void sliderHueChanged();
    void fromArtChanged();
    void artHueChanged();
    void hueChanged();
    void artPathChanged();
    void phoneActiveChanged();

private:
    void refresh();
    void setArt(const ArtPick &pick);
    void updateHue();
    void onFound(const QString &path, int hue, int level, const QColor &color);
    void logArt(const QString &path, const ArtPick &pick);

    ArtColor m_art;
    int m_sliderHue = 350;
    bool m_fromArt = false;
    int m_artHue = -1;
    int m_artLevel = 0;
    QColor m_artTone;
    int m_hue = 350;
    int m_level = 0;
    QColor m_tone;
    QString m_artPath;
    QString m_loggedPath;
    bool m_phoneActive = false;
};

}
