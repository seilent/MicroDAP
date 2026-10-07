#pragma once

#include "ArtColor.h"

#include <QColor>
#include <QObject>
#include <QString>

namespace casdap {

class AccentControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int sliderHue READ sliderHue WRITE setSliderHue NOTIFY sliderHueChanged)
    Q_PROPERTY(bool fromArt READ fromArt WRITE setFromArt NOTIFY fromArtChanged)
    Q_PROPERTY(int artHue READ artHue NOTIFY artHueChanged)
    Q_PROPERTY(int hue READ hue NOTIFY hueChanged)
    Q_PROPERTY(QColor color READ color NOTIFY hueChanged)
    Q_PROPERTY(QColor brightColor READ brightColor NOTIFY hueChanged)
    Q_PROPERTY(QString artPath READ artPath NOTIFY artPathChanged)
    Q_PROPERTY(bool phoneActive READ phoneActive NOTIFY phoneActiveChanged)

public:
    static constexpr double accentValue = 0.98;
    static constexpr double minLuminance = 0.149;
    static constexpr double brightScale = 0.84;

    explicit AccentControl(QObject *parent = nullptr);

    static double saturationFor(int hue);
    static QColor hsv(int hue, double saturation, double value);
    static double luminance(const QColor &color);
    static QColor accentFor(int hue);
    static QColor brightFor(int hue);

    int sliderHue() const;
    void setSliderHue(int hue);
    bool fromArt() const;
    void setFromArt(bool on);
    int artHue() const;
    int hue() const;
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
    void setArtHue(int hue);
    void updateHue();
    void onFound(const QString &path, int hue);

    ArtColor m_art;
    int m_sliderHue = 350;
    bool m_fromArt = false;
    int m_artHue = -1;
    int m_hue = 350;
    QString m_artPath;
    bool m_phoneActive = false;
};

}
