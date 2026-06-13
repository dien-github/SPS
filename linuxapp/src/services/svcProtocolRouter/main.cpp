#include <QCoreApplication>
#include <QDBusConnection>
#include "protocol_router.h"
#include "../common/sps_logger.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // Initialize logger
    Logger::instance().init("/var/log/sps/protocol_router.log", LogLevel::DEBUG);
    Logger::instance().info("svcProtocolRouter", "Starting Protocol Router service...");

    // Create service
    ProtocolRouter router;

    // Initialize service
    if (!router.initialize()) {
        Logger::instance().error("svcProtocolRouter", "Failed to initialize service");
        return 1;
    }

    // Register on D-Bus
    Logger::instance().info("svcProtocolRouter", "Registering D-Bus service...");
    QDBusConnection dbus = QDBusConnection::systemBus();
    if (!dbus.isConnected()) {
        Logger::instance().error("svcProtocolRouter", "D-Bus not connected");
        return 1;
    }

    // Register service object
    if (!dbus.registerObject("/com/sps/router", &router,
			     QDBusConnection::ExportScriptableSlots |
                             QDBusConnection::ExportScriptableSignals)) {
        Logger::instance().error("svcProtocolRouter", "Failed to register D-Bus object");
        return 1;
    }

    if (!dbus.registerService("com.sps.router")) {
        Logger::instance().error("svcProtocolRouter", "Failed to register D-Bus service");
        return 1;
    }

    Logger::instance().info("svcProtocolRouter", 
        "Service registered on D-Bus: com.sps.router at /com/sps/router");

    // Handle signals for graceful shutdown
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &router, [&router]() {
        Logger::instance().info("svcProtocolRouter", "Shutting down...");
        router.shutdown();
    });

    Logger::instance().info("svcProtocolRouter", "Service started and running");

    int result = app.exec();

    Logger::instance().info("svcProtocolRouter", "Service stopped");
    return result;
}
