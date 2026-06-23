#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QScreen>
#include <QWindow>
//#include "cMockAuthService.h"
#include "app_dbus_client.h"

/** Application entry point. Initializes the D-Bus client, sets up the QML engine, and runs the HMI in fullscreen kiosk mode. */
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // Create D-Bus client for service communication
    AppDbusCli dbusClient;
    if (!dbusClient.initialize()) {
        qWarning("Failed to initialize D-Bus client");
    }

    // Create mock auth service (fallback if D-Bus unavailable)
    //cMockAuthService authService;

    QQmlApplicationEngine engine;

    // Expose D-Bus client to QML
    engine.rootContext()->setContextProperty("dbusClient", &dbusClient);
    //engine.rootContext()->setContextProperty("authBackend", &authService);

    // Setup fullscreen kiosk mode
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [&app](QObject* root) {
            if (!root) {
                QCoreApplication::exit(-1);
                return;
            }
            // Make window fullscreen and disable exit
            QWindow* window = qobject_cast<QWindow*>(root);
            if (window) {
                const QList<QScreen*> screens = QGuiApplication::screens();

                // Use secondary screen if available, otherwise fallback to primary screen
                QScreen* targetScreen = nullptr;
                if (screens.size() > 1) {
                    targetScreen = screens.at(1);
                    qInfo() << "Using secondary screen:" << targetScreen->name()
                            << targetScreen->geometry();
                } else {
                    targetScreen = QGuiApplication::primaryScreen();
                    qWarning() << "Secondary screen not found. Fallback to primary screen:"
                               << targetScreen->name()
                               << targetScreen->geometry();
                }

                window->setScreen(targetScreen);
                window->setGeometry(targetScreen->geometry());
                window->showFullScreen();
            }
        },
        Qt::QueuedConnection);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.load(QUrl(QStringLiteral("qrc:/SpsHmi/qml/Main.qml")));

    return app.exec();
}
