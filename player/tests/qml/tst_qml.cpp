#include <QQmlEngine>
#include <QtQuickTest>

class Setup : public QObject
{
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->addImportPath(QStringLiteral("qrc:/qt/qml"));
    }
};

QUICK_TEST_MAIN_WITH_SETUP(qml, Setup)
#include "tst_qml.moc"
