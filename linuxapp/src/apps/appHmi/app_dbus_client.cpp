#include "app_dbus_client.h"
#include "sps_logger.h"
#include <QDBusConnection>
#include <QDBusReply>
#include <QJsonDocument>
#include <QJsonObject>

// Logger use Singleton pattern
#define logInfo(comp, msg) Logger::instance().info(comp, msg)
#define logError(comp, msg) Logger::instance().error(comp, msg)
#define logDebug(comp, msg) Logger::instance().debug(comp, msg)
#define logWarning(comp, msg) Logger::instance().warning(comp, msg)

AppDbusCli::AppDbusCli(QObject* parent)
    : QObject(parent),
      m_authInterface(nullptr),
      m_routerInterface(nullptr),
      m_engineInterface(nullptr),
      m_netMgrInterface(nullptr),
      m_authStatus("UNKNOWN"),
      m_mqttStatus("DISCONNECTED") {
    logInfo("AppDbusCli", "Created");
}

AppDbusCli::~AppDbusCli() {
    if (m_authInterface) delete m_authInterface;
    if (m_routerInterface) delete m_routerInterface;
    if (m_engineInterface) delete m_engineInterface;
    if (m_netMgrInterface) delete m_netMgrInterface;
}

// Initialize and discover all services
bool AppDbusCli::initialize() {
    logInfo("AppDbusCli", "Initializing D-Bus client...");

    // Check D-Bus connection
    if (!QDBusConnection::systemBus().isConnected()) {
        logError("AppDbusCli", "D-Bus system bus not connected");
        return false;
    }

    // Connect to all services
    bool allConnected = true;
    allConnected &= connectToService(m_authInterface, "com.sps.auth", "/com/sps/auth");
    allConnected &= connectToService(m_routerInterface, "com.sps.router", "/com/sps/router");
    allConnected &= connectToService(m_engineInterface, "com.sps.engine", "/com/sps/engine");
    allConnected &= connectToService(m_netMgrInterface, "com.sps.netmgr", "/com/sps/netmgr");

    if (!allConnected) {
        logWarning("AppDbusCli", "Some services not available - application may operate in degraded mode");
    }

    // Setup signal connections
    if (!setupSignalConnections()) {
        logWarning("AppDbusCli", "Failed to setup some signal connections");
    }

    logInfo("AppDbusCli", "Initialization complete");
    return true;
}

// Discover services on D-Bus
bool AppDbusCli::discoverServices() {
    logInfo("AppDbusCli", "Discovering SPS services...");

    QStringList expectedServices;
    expectedServices << "com.sps.auth" << "com.sps.router" << "com.sps.engine" << "com.sps.netmgr";

    QDBusConnection dbus = QDBusConnection::systemBus();
    for (const QString& service : expectedServices) {
        QDBusInterface iface(service, "/", "", dbus);
        if (iface.isValid()) {
            logInfo("AppDbusCli", QString("Found service: %1").arg(service));
        } else {
            logWarning("AppDbusCli", QString("Service not found: %1").arg(service));
        }
    }

    return true;
}

// Verify all services are available
bool AppDbusCli::verifyServiceAvailability() {
    bool authValid = m_authInterface && m_authInterface->isValid();
    bool routerValid = m_routerInterface && m_routerInterface->isValid();
    bool engineValid = m_engineInterface && m_engineInterface->isValid();
    bool netMgrValid = m_netMgrInterface && m_netMgrInterface->isValid();

    QString status = QString("Auth=%1 Router=%2 Engine=%3 Network=%4")
        .arg(authValid ? "OK" : "FAIL")
        .arg(routerValid ? "OK" : "FAIL")
        .arg(engineValid ? "OK" : "FAIL")
        .arg(netMgrValid ? "OK" : "FAIL");

    logInfo("AppDbusCli", QString("Service status: %1").arg(status));
    return authValid && routerValid && engineValid && netMgrValid;
}

// === Authentication Methods ===

bool AppDbusCli::unlockScreen() {
    logInfo("AppDbusCli", "Calling auth.UnlockScreen()");
    QVariant result = callMethod(m_authInterface, "UnlockScreen");
    return result.toBool();
}

bool AppDbusCli::lockScreen() {
    logInfo("AppDbusCli", "Calling auth.LockScreen()");
    QVariant result = callMethod(m_authInterface, "LockScreen");
    return result.toBool();
}

QString AppDbusCli::getAuthenticatedLecturer() {
    QVariant result = callMethod(m_authInterface, "GetAuthenticatedLecturer");
    return result.toString();
}

// === Scenario Engine Methods ===

bool AppDbusCli::executeScenario(const QString& scenarioId) {
    logInfo("AppDbusCli", QString("Calling engine.ExecuteScenario(%1)").arg(scenarioId));
    QVariant result = callMethod(m_engineInterface, "ExecuteScenario", scenarioId);
    bool success = result.toBool();
    if (success) {
        m_currentScenario = scenarioId;
        emit scenarioChanged(scenarioId);
    }
    return success;
}

bool AppDbusCli::stopScenario(const QString& scenarioId) {
    logInfo("AppDbusCli", QString("Calling engine.StopScenario(%1)").arg(scenarioId));
    QVariant result = callMethod(m_engineInterface, "StopScenario", scenarioId);
    return result.toBool();
}

QString AppDbusCli::getScenarioStatus(const QString& scenarioId) {
    QVariant result = callMethod(m_engineInterface, "GetScenarioStatus", scenarioId);
    return result.toString();
}

QStringList AppDbusCli::getAvailableScenarios() {
    if (!m_engineInterface || !m_engineInterface->isValid()) {
        return QStringList();
    }

    QDBusReply<QStringList> reply = m_engineInterface->call("GetAvailableScenarios");
    if (reply.isValid()) {
        m_availableScenarios = reply.value();
        emit scenariosUpdated(m_availableScenarios);
    }
    return m_availableScenarios;
}

// === Device Control Methods ===

bool AppDbusCli::sendDeviceCommand(uchar deviceId, const QString& action) {
    logDebug("AppDbusCli", QString("Sending device command: id=0x%1, action=%2")
        .arg(deviceId, 2, 16, QChar('0')).arg(action));

    if (!m_routerInterface || !m_routerInterface->isValid()) {
        return false;
    }

    // TODO: Map action string to state value and call SendCommand

    return true;
}

uint AppDbusCli::getDeviceStatus(uchar deviceId) {
    if (!m_routerInterface || !m_routerInterface->isValid()) {
        return 0;
    }

    QDBusReply<uint> reply = m_routerInterface->call("GetDeviceStatus", deviceId);
    return reply.isValid() ? reply.value() : 0;
}

// === Network Manager Methods ===

bool AppDbusCli::connectToMqtt(const QString& broker, int port) {
    logInfo("AppDbusCli", QString("Calling network.ConnectToMqtt(%1:%2)").arg(broker).arg(port));
    QVariant result = callMethod(m_netMgrInterface, "ConnectToMqtt", broker, port);
    return result.toBool();
}

bool AppDbusCli::disconnectFromMqtt() {
    logInfo("AppDbusCli", "Calling network.DisconnectFromMqtt()");
    QVariant result = callMethod(m_netMgrInterface, "DisconnectFromMqtt");
    return result.toBool();
}

bool AppDbusCli::sendWoL(const QString& macAddress) {
    logInfo("AppDbusCli", QString("Sending WoL to %1").arg(macAddress));
    QVariant result = callMethod(m_netMgrInterface, "SendWakeOnLAN", macAddress, "255.255.255.255");
    return result.toBool();
}

QString AppDbusCli::getMqttStatus() {
    QVariant result = callMethod(m_netMgrInterface, "GetMqttStatus");
    return result.toString();
}

QString AppDbusCli::getNetworkStatus() {
    QVariant result = callMethod(m_netMgrInterface, "GetNetworkStatus");
    return result.toString();
}

// === Getters ===

QString AppDbusCli::getAuthStatus() const {
    return m_authStatus;
}

QString AppDbusCli::getCurrentScenario() const {
    return m_currentScenario;
}

QString AppDbusCli::getMqttStatus() const {
    return m_mqttStatus;
}

// === Private Methods ===

bool AppDbusCli::connectToService(QDBusInterface*& iface, const QString& service, const QString& path) {
    iface = new QDBusInterface(service, path, service, QDBusConnection::systemBus());

    if (!iface->isValid()) {
        logWarning("AppDbusCli", QString("Failed to connect to %1").arg(service));
        return false;
    }

    logInfo("AppDbusCli", QString("Connected to service: %1").arg(service));
    return true;
}

bool AppDbusCli::setupSignalConnections() {
    bool ok = true;

    // Auth service signals
    if (m_authInterface) {
        ok = ok && connect(m_authInterface, SIGNAL(AuthStatusChanged(int)),
                      this, SLOT(onAuthStatusChanged(int)));
    }

    // Scenario engine signals
    if (m_engineInterface) {
        ok = ok && connect(m_engineInterface, SIGNAL(ScenarioStarted(QString)),
                      this, SLOT(onScenarioStarted(QString)));
        ok = ok && connect(m_engineInterface, SIGNAL(ScenarioCompleted(QString)),
                      this, SLOT(onScenarioCompleted(QString)));
        ok = ok && connect(m_engineInterface, SIGNAL(ScenarioError(QString, QString)),
                      this, SLOT(onScenarioError(QString, QString)));
        ok = ok && connect(m_engineInterface, SIGNAL(ContextTriggered(QString, QString)),
                      this, SLOT(onContextTriggered(QString, QString)));
    }

    // Protocol router signals
    if (m_routerInterface) {
        ok = ok && connect(m_routerInterface, SIGNAL(CommandAck(uchar)),
                      this, SLOT(onCommandAck(uchar)));
        ok = ok && connect(m_routerInterface, SIGNAL(CommandError(uchar, uchar)),
                      this, SLOT(onCommandError(uchar, uchar)));
    }

    // Network manager signals
    if (m_netMgrInterface) {
        ok = ok && connect(m_netMgrInterface, SIGNAL(MqttConnected()),
                      this, SLOT(onMqttConnected()));
        ok = ok && connect(m_netMgrInterface, SIGNAL(MqttDisconnected(QString)),
                      this, SLOT(onMqttDisconnected(QString)));
        ok = ok && connect(m_netMgrInterface, SIGNAL(NetworkStatusChanged(bool)),
                      this, SLOT(onNetworkStatusChanged(bool)));
        ok = ok && connect(m_netMgrInterface, SIGNAL(CommandReceived(QString, QJsonObject)),
                      this, SLOT(onCommandReceived(QString, QJsonObject)));
    }

    return ok;
}

QVariant AppDbusCli::callMethod(QDBusInterface* iface, const QString& method, 
                                const QVariant& arg1, const QVariant& arg2) {
    if (!iface || !iface->isValid()) {
        logWarning("AppDbusCli", QString("Interface not valid for method %1").arg(method));
        return QVariant();
    }

    QDBusReply<QVariant> reply;
    if (arg2.isValid()) {
        reply = iface->call(method, arg1, arg2);
    } else if (arg1.isValid()) {
        reply = iface->call(method, arg1);
    } else {
        reply = iface->call(method);
    }

    if (!reply.isValid()) {
        logError("AppDbusCli", QString("Method call failed: %1 - %2")
            .arg(method).arg(reply.error().message()));
        return QVariant();
    }

    return reply.value();
}

// === Signal Handlers ===

void AppDbusCli::onAuthStatusChanged(int status) {
    QString statusStr;
    switch(status) {
        case 0: statusStr = "LOCKED"; break;
        case 1: statusStr = "UNLOCKING"; break;
	case 2: statusStr = "UNLOCKED"; break;
	case 3: statusStr = "LOCKING"; break;
	default: statusStr = "ERROR"; break;
    }

    logDebug("AppDbusCli", QString("Auth status changed: %1").arg(statusStr));
    m_authStatus = statusStr;
    emit authStatusChanged(statusStr);
}

void AppDbusCli::onScenarioStarted(const QString& scenarioId) {
    logInfo("AppDbusCli", QString("Scenario started: %1").arg(scenarioId));
    emit scenarioStarted(scenarioId);
}

void AppDbusCli::onScenarioCompleted(const QString& scenarioId) {
    logInfo("AppDbusCli", QString("Scenario completed: %1").arg(scenarioId));
    m_currentScenario.clear();
    emit scenarioCompleted(scenarioId);
}

void AppDbusCli::onScenarioError(const QString& scenarioId, const QString& error) {
    logError("AppDbusCli", QString("Scenario error: %1 - %2").arg(scenarioId).arg(error));
    emit scenarioError(scenarioId, error);
}

void AppDbusCli::onContextTriggered(const QString& context, const QString& scenarioId) {
    logInfo("AppDbusCli", QString("Context triggered: %1 -> %2").arg(context).arg(scenarioId));
    emit contextTriggered(context, scenarioId);
}

void AppDbusCli::onMqttConnected() {
    logInfo("AppDbusCli", "MQTT connected");
    m_mqttStatus = "CONNECTED";
    emit mqttStatusChanged("CONNECTED");
    emit mqttConnected();
}

void AppDbusCli::onMqttDisconnected(const QString& reason) {
    logWarning("AppDbusCli", QString("MQTT disconnected: %1").arg(reason));
    m_mqttStatus = "DISCONNECTED";
    emit mqttStatusChanged("DISCONNECTED");
    emit mqttDisconnected(reason);
}

void AppDbusCli::onNetworkStatusChanged(bool connected) {
    logInfo("AppDbusCli", QString("Network status: %1").arg(connected ? "ONLINE" : "OFFLINE"));
    emit networkStatusChanged(connected);
}

void AppDbusCli::onCommandReceived(const QString& command, const QJsonObject& payload) {
    logDebug("AppDbusCli", QString("Command received: %1").arg(command));
    emit commandReceived(command, payload);
}

void AppDbusCli::onCommandAck(uchar cmdId) {
    logDebug("AppDbusCli", QString("Command ACK: 0x%1").arg(cmdId, 2, 16, QChar('0')));
    emit commandAck(cmdId);
}

void AppDbusCli::onCommandError(uchar cmdId, uchar errorCode) {
    logError("AppDbusCli", QString("Command error: 0x%1 - 0x%2")
        .arg(cmdId, 2, 16, QChar('0')).arg(errorCode, 2, 16, QChar('0')));
    emit commandError(cmdId, errorCode);
}
