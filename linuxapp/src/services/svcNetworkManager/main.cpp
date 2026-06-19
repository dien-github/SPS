#include <QCoreApplication>
#include <QDBusConnection>
#include "network_manager.h"
#include "../common/sps_logger.h"

/** Entry point: initializes the logger, creates and starts the NetworkManager, and registers it on D-Bus. */
int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // Initialize logger
    Logger::instance().init("/var/log/sps/network_manager.log", LogLevel::DEBUG);
    Logger::instance().info("svcNetworkManager", "Starting Network Manager service...");

    // Create service
    NetworkManager netMgr;

    // Initialize service
    if (!netMgr.initialize()) {
        Logger::instance().error("svcNetworkManager", "Failed to initialize service");
        return 1;
    }

    // Register on D-Bus
    Logger::instance().info("svcNetworkManager", "Registering D-Bus service...");
    QDBusConnection dbus = QDBusConnection::systemBus();
    if (!dbus.isConnected()) {
        Logger::instance().error("svcNetworkManager", "D-Bus not connected");
        return 1;
    }

    // Register service object
    if (!dbus.registerObject("/com/sps/netmgr", &netMgr,
                               QDBusConnection::ExportScriptableSlots |
                               QDBusConnection::ExportAllSignals)) {
        Logger::instance().error("svcNetworkManager", "Failed to register D-Bus object");
        return 1;
    }

    if (!netMgr.isRegistered() && !dbus.registerService("com.sps.netmgr")) {
        Logger::instance().error("svcNetworkManager", "Failed to register D-Bus service");
        return 1;
    }

    Logger::instance().info("svcNetworkManager", 
        "Service registered on D-Bus: com.sps.netmgr at /com/sps/netmgr");

    // Handle signals for graceful shutdown
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &netMgr, [&netMgr]() {
        Logger::instance().info("svcNetworkManager", "Shutting down...");
        netMgr.shutdown();
    });

    Logger::instance().info("svcNetworkManager", "Service started and running");

    int result = app.exec();

    Logger::instance().info("svcNetworkManager", "Service stopped");
    return result;
}
