#include <QCoreApplication>
#include <QDBusConnection>
#include "scenario_engine.h"
#include "../common/sps_logger.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // Initialize logger
    Logger::instance().init("/var/log/sps/scenario_engine.log", LogLevel::DEBUG);
    Logger::instance().info("svcAutoEngine", "Starting Scenario Engine service...");

    // Create service
    ScenarioEngine engine;

    // Initialize service
    if (!engine.initialize()) {
        Logger::instance().error("svcAutoEngine", "Failed to initialize service");
        return 1;
    }

    // Register on D-Bus
    Logger::instance().info("svcAutoEngine", "Registering D-Bus service...");
    QDBusConnection dbus = QDBusConnection::systemBus();
    if (!dbus.isConnected()) {
        Logger::instance().error("svcAutoEngine", "D-Bus not connected");
        return 1;
    }

    // Register service object
    if (!dbus.registerObject("/com/sps/engine", &engine,
			     QDBusConnection::ExportScriptableSlots |
                             QDBusConnection::ExportScriptableSignals)) {
        Logger::instance().error("svcAutoEngine", "Failed to register D-Bus object");
        return 1;
    }

    if (!dbus.registerService("com.sps.engine")) {
        Logger::instance().error("svcAutoEngine", "Failed to register D-Bus service");
        return 1;
    }

    Logger::instance().info("svcAutoEngine", 
        "Service registered on D-Bus: com.sps.engine at /com/sps/engine");

    // Handle signals for graceful shutdown
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &engine, [&engine]() {
        Logger::instance().info("svcAutoEngine", "Shutting down...");
        engine.shutdown();
    });

    Logger::instance().info("svcAutoEngine", "Service started and running");

    int result = app.exec();

    Logger::instance().info("svcAutoEngine", "Service stopped");
    return result;
}
