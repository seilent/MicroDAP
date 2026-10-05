#pragma once

#include <QObject>
#include <QString>

namespace casdap {

class Keep : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool flag READ flag WRITE setFlag NOTIFY flagChanged)

public:
    explicit Keep(QObject *parent = nullptr);

    static QString markerPath();
    static QString bootIdPath();
    static QString bootId();
    static QString marker();
    static bool write();
    static bool clear();

    bool flag() const;
    void setFlag(bool flag);
    bool saveExit() const;

signals:
    void flagChanged();

private:
    bool m_flag = false;
};

}
