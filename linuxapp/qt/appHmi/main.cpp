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

    const QUrl url(QStringLiteral("qrc:/main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
