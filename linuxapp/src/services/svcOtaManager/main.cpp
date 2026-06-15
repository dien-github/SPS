#include <QCoreApplication>
#include <QDBusConnection>
#include "ota_manager.h"
#include "../common/sps_logger.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // Initialize logger
    Logger::instance().init("/var/log/sps/ota_manager.log", LogLevel::DEBUG);
    Logger::instance().info("svcOtaManager", "Starting OTA Manager service...");

    // Create service
    OtaManager otaMgr;

    // Initialize service
    if (!otaMgr.initialize()) {
        Logger::instance().error("svcOtaManager", "Failed to initialize service");
        return 1;
    }

    // Register on D-Bus
    Logger::instance().info("svcOtaManager", "Registering D-Bus service...");
    QDBusConnection dbus = QDBusConnection::systemBus();
    if (!dbus.isConnected()) {
        Logger::instance().error("svcOtaManager", "D-Bus not connected");
        return 1;
    }

    // Register service object
    if (!dbus.registerObject("/com/sps/otamanager", &otaMgr,
                             QDBusConnection::ExportScriptableSlots |
                             QDBusConnection::ExportScriptableSignals)) {
        Logger::instance().error("svcOtaManager", "Failed to register D-Bus object");
        return 1;
    }

    if (!dbus.registerService("com.sps.otamanager")) {
        Logger::instance().error("svcOtaManager", "Failed to register D-Bus service");
        return 1;
    }

    Logger::instance().info("svcOtaManager",
        "Service registered on D-Bus: com.sps.otamanager at /com/sps/otamanager");

    // Handle signals for graceful shutdown
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &otaMgr, [&otaMgr]() {
        Logger::instance().info("svcOtaManager", "Shutting down...");
        otaMgr.shutdown();
    });

    Logger::instance().info("svcOtaManager", "Service started and running");

    int result = app.exec();

    Logger::instance().info("svcOtaManager", "Service stopped");
    return result;
}
