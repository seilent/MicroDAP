#pragma once

#include <QString>

namespace casdap {

struct ScreenshotOptions
{
    QString screen;
    QString size;
    QString out;
    double angle = 40;
    QString bluez;
    QString state;
    QString library;
    QString pivot;
    QString orientation;
    bool overlay = false;
};

int screenshot(const ScreenshotOptions &options);

}
