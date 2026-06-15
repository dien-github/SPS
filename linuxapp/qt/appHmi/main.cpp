#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "cMockAuthService.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    cMockAuthService a_cAuthService;

    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("authBackend", &a_cAuthService);

    // const QUrl url(QStringLiteral("qrc:appappHmi/Main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("appHmi","Main");

    return app.exec();
}
