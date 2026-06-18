#include <QCoreApplication>
#include <QDBusConnection>
#include "auth_service.h"
#include "rfid_reader.h"
#include "../common/sps_logger.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // Initialize logger
    Logger::instance().init("/var/log/sps/auth_service.log", LogLevel::DEBUG);
    Logger::instance().info("svcAuthentication", "Starting authentication service...");

    // Create service
    AuthService service;

    // Create RFID reader
    RfidReader rfidReader;

    // Initialize service
    if (!service.initialize()) {
        Logger::instance().error("svcAuthentication", "Failed to initialize service");
        return 1;
    }

    // Connect RFID reader signals to service slots
    QObject::connect(&rfidReader, &RfidReader::rfidRead,
                     &service, &AuthService::onRfidRead);
    QObject::connect(&rfidReader, &RfidReader::readerConnected,
                     &service, &AuthService::onRfidReaderConnected);
    QObject::connect(&rfidReader, &RfidReader::readerDisconnected,
                     &service, &AuthService::onRfidReaderDisconnected);
    QObject::connect(&rfidReader, &RfidReader::readerError,
                     &service, &AuthService::onRfidReaderError);

    // Connect RFID reader
    Logger::instance().info("svcAuthentication", "Connecting RFID reader...");
    if (!rfidReader.connectHardware("17")) {  // GPIO17 on RPi
        Logger::instance().warning("svcAuthentication", "RFID reader connection failed");
        // Continue anyway - can use D-Bus for testing
    } else {
        rfidReader.startReading();
        Logger::instance().info("svcAuthentication", "RFID reader connected and reading");
    }

    // Register on D-Bus
    Logger::instance().info("svcAuthentication", "Registering D-Bus service...");
    QDBusConnection dbus = QDBusConnection::systemBus();
    if (!dbus.isConnected()) {
        Logger::instance().error("svcAuthentication", "D-Bus not connected");
        return 1;
    }

    // Register service object
    if (!dbus.registerObject("/com/sps/auth", &service,
			     QDBusConnection::ExportScriptableSlots |
			     QDBusConnection::ExportScriptableSignals)) {
        Logger::instance().error("svcAuthentication", "Failed to register D-Bus object");
        return 1;
    }

    if (!service.isRegistered() && !dbus.registerService("com.sps.auth")) {
        Logger::instance().error("svcAuthentication", "Failed to register D-Bus service");
        return 1;
    }

    Logger::instance().info("svcAuthentication", 
        "Service registered on D-Bus: com.sps.auth at /com/sps/auth");

    // Handle signals for graceful shutdown
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &service, [&service, &rfidReader]() {
        Logger::instance().info("svcAuthentication", "Shutting down...");
        rfidReader.stopReading();
        rfidReader.disconnect();
        service.shutdown();
    });

    Logger::instance().info("svcAuthentication", "Service started and running");

    int result = app.exec();

    Logger::instance().info("svcAuthentication", "Service stopped");
    return result;
}
