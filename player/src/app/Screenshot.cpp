#include "Screenshot.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

namespace casdap {

namespace {

constexpr int frameTimeoutMs = 5000;

void fail(const QString &message)
{
    QTextStream(stderr) << "casdap-player: " << message << '\n';
}

bool parseSize(const QString &size, int &width, int &height)
{
    static const QRegularExpression pattern(QStringLiteral("^(\\d+)x(\\d+)$"));
    const QRegularExpressionMatch match = pattern.match(size);
    if (!match.hasMatch())
        return false;
    width = match.captured(1).toInt();
    height = match.captured(2).toInt();
    return width > 0 && height > 0;
}

void grab(QQuickWindow *window, const QString &out)
{
    const QImage image = window->grabWindow();
    if (image.isNull()) {
        fail(QStringLiteral("grabWindow returned no image"));
        QCoreApplication::exit(1);
        return;
    }
    QDir().mkpath(QFileInfo(out).absolutePath());
    if (!image.save(out, "PNG")) {
        fail(QStringLiteral("cannot write %1").arg(out));
        QCoreApplication::exit(1);
        return;
    }
    QCoreApplication::exit(0);
}

const QStringList &screens()
{
    static const QStringList names{
        QStringLiteral("blank"),
        QStringLiteral("specimen"),
        QStringLiteral("motion"),
        QStringLiteral("components"),
        QStringLiteral("overlays"),
        QStringLiteral("volume"),
    };
    return names;
}

}

int screenshot(const QString &screen, const QString &size, const QString &out, double angle)
{
    if (!screens().contains(screen)) {
        fail(QStringLiteral("unknown screen %1, expected one of %2").arg(screen, screens().join(QLatin1Char(' '))));
        return 2;
    }

    int width = 0;
    int height = 0;
    if (!parseSize(size, width, height)) {
        fail(QStringLiteral("bad size %1, expected WxH").arg(size));
        return 1;
    }
    if (out.isEmpty()) {
        fail(QStringLiteral("--screenshot needs --out"));
        return 1;
    }

    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {QStringLiteral("screenshot"), true},
        {QStringLiteral("shotScreen"), screen},
        {QStringLiteral("shotAngle"), angle},
        {QStringLiteral("shotWidth"), width},
        {QStringLiteral("shotHeight"), height},
    });
    engine.loadFromModule("Casdap", "Main");

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (!window) {
        fail(QStringLiteral("Casdap.Main did not produce a window"));
        return 1;
    }

    QObject::connect(window, &QQuickWindow::frameSwapped, window, [window, out] {
        QTimer::singleShot(0, window, [window, out] { grab(window, out); });
    }, Qt::SingleShotConnection);

    QTimer::singleShot(frameTimeoutMs, window, [] {
        fail(QStringLiteral("no frame within %1 ms").arg(frameTimeoutMs));
        QCoreApplication::exit(1);
    });

    return QCoreApplication::exec();
}

}
