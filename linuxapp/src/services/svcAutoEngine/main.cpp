#include <QCoreApplication>
#include <QDBusConnection>
#include "scenario_engine.h"
#include "../common/sps_logger.h"

/** Application entry point. Creates the service, registers it on D-Bus, and starts the event loop. */
int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    Logger::instance().init("/var/log/sps/scenario_engine.log", LogLevel::DEBUG);
    Logger::instance().info("svcAutoEngine", "Starting Scenario Engine service...");

    ScenarioEngine engine;

    if (!engine.initialize()) {
        Logger::instance().error("svcAutoEngine", "Failed to initialize service");
        return 1;
    }

    Logger::instance().info("svcAutoEngine", "Registering D-Bus service...");
    QDBusConnection dbus = QDBusConnection::systemBus();
    if (!dbus.isConnected()) {
        Logger::instance().error("svcAutoEngine", "D-Bus not connected");
        return 1;
    }

    if (!dbus.registerObject("/com/sps/engine", &engine,
			     QDBusConnection::ExportScriptableSlots |
                             QDBusConnection::ExportAllSignals)) {
        Logger::instance().error("svcAutoEngine", "Failed to register D-Bus object");
        return 1;
    }

    if (!engine.isRegistered() && !dbus.registerService("com.sps.engine")) {
        Logger::instance().error("svcAutoEngine", "Failed to register D-Bus service");
        return 1;
    }

    Logger::instance().info("svcAutoEngine", 
        "Service registered on D-Bus: com.sps.engine at /com/sps/engine");

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &engine, [&engine]() {
        Logger::instance().info("svcAutoEngine", "Shutting down...");
        engine.shutdown();
    });

    Logger::instance().info("svcAutoEngine", "Service started and running");

    int result = app.exec();

    Logger::instance().info("svcAutoEngine", "Service stopped");
    return result;
}
