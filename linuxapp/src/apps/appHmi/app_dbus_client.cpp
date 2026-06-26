#include "app_dbus_client.h"
#include "sps_logger.h"
#include "sps_runtime_config.h"
#include "sps_uart_protocol.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QJsonDocument>
#include <QJsonObject>

// Logger use Singleton pattern
#define logInfo(comp, msg) Logger::instance().info(comp, msg)
#define logError(comp, msg) Logger::instance().error(comp, msg)
#define logDebug(comp, msg) Logger::instance().debug(comp, msg)
#define logWarning(comp, msg) Logger::instance().warning(comp, msg)

namespace {
QString normalizeKey(QString value) {
    value = value.trimmed().toLower();
    value.remove('-');
    value.remove('_');
    value.remove(' ');
    return value;
}

bool actionToControlValue(const QString& action, SPS::UART::ControlValue& value) {
    const QString normalized = action.trimmed().toLower();
    if (normalized == "on" || normalized == "open" || normalized == "up") {
        value = SPS::UART::ControlValue::ON;
        return true;
    }
    if (normalized == "off" || normalized == "close" || normalized == "down") {
        value = SPS::UART::ControlValue::OFF;
        return true;
    }
    return false;
}

bool isOnValue(SPS::UART::ControlValue value) {
    return value == SPS::UART::ControlValue::ON;
}
}

/** Constructor. Initializes D-Bus interface pointers, cached state, and reads PC control config from environment. */
AppDbusCli::AppDbusCli(QObject* parent)
    : QObject(parent),
      m_authInterface(nullptr),
      m_routerInterface(nullptr),
      m_engineInterface(nullptr),
      m_netMgrInterface(nullptr),
      m_authStatus("UNKNOWN"),
      m_mqttStatus("DISCONNECTED"),
      m_networkConnected(false),
      m_pcControlEnabled(SPS::Runtime::envBool("SPS_ENABLE_PC_CONTROL", false)),
      m_pcMacAddress(SPS::Runtime::envString("SPS_PC_MAC", "")) {
    logInfo("AppDbusCli", "Created");
}

/** Destructor. Deletes all D-Bus interface pointers. */
AppDbusCli::~AppDbusCli() {
    if (m_authInterface) delete m_authInterface;
    if (m_routerInterface) delete m_routerInterface;
    if (m_engineInterface) delete m_engineInterface;
    if (m_netMgrInterface) delete m_netMgrInterface;
}

/** Connects to all SPS D-Bus services, sets up signal handlers, and refreshes PC control config. */
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
    refreshPcControlConfig();
    const QString networkStatus = getNetworkStatus().trimmed().toUpper();
    m_networkConnected = (networkStatus == "ONLINE" || networkStatus == "CONNECTED");
    emit networkStatusChanged(m_networkConnected);

    logInfo("AppDbusCli", "Initialization complete");
    return true;
}

/** Scans the D-Bus system bus for expected SPS services and logs which are found or missing. */
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

/** Checks that all four D-Bus service interfaces (auth, router, engine, netmgr) are valid. */
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

/** Sends an unlock request to the authentication D-Bus service with the given RFID data. */
bool AppDbusCli::unlockScreen(const QString& rfidData) {
    const QString uid = rfidData.trimmed().isEmpty() ? QStringLiteral("RFID001") : rfidData.trimmed();
    logInfo("AppDbusCli", QString("Calling auth.UnlockScreen(%1)").arg(uid));
    QVariant result = callMethod(m_authInterface, "UnlockScreen", uid);
    return result.toBool();
}

/** Sends a lock request to the authentication D-Bus service. */
bool AppDbusCli::lockScreen() {
    logInfo("AppDbusCli", "Calling auth.LockScreen()");
    QVariant result = callMethod(m_authInterface, "LockScreen");
    return result.toBool();
}

/** Returns the name of the currently authenticated lecturer from the auth service. */
QString AppDbusCli::getAuthenticatedLecturer() {
    QVariant result = callMethod(m_authInterface, "GetAuthenticatedLecturer");
    return result.toString();
}

/** Marks the room as active by calling auth.SetRoomActive() over D-Bus. */
bool AppDbusCli::setRoomActive() {
    if (!m_authInterface || !m_authInterface->isValid()) {
        return false;
    }
    QDBusReply<bool> reply = m_authInterface->call("SetRoomActive");
    return reply.isValid() && reply.value();
}

// === Scenario Engine Methods ===

/** Sends a request to start the given scenario and updates the cached current scenario on success. */
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

/** Sends a request to stop the given scenario. */
bool AppDbusCli::stopScenario(const QString& scenarioId) {
    logInfo("AppDbusCli", QString("Calling engine.StopScenario(%1)").arg(scenarioId));
    QVariant result = callMethod(m_engineInterface, "StopScenario", scenarioId);
    return result.toBool();
}

/** Queries the current status of a scenario from the engine D-Bus service. */
QString AppDbusCli::getScenarioStatus(const QString& scenarioId) {
    QVariant result = callMethod(m_engineInterface, "GetScenarioStatus", scenarioId);
    return result.toString();
}

/** Retrieves the list of available scenarios from the engine and emits scenariosUpdated. */
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

/** Sends a typed device command via the protocol router D-Bus service. */
bool AppDbusCli::sendDeviceCommand(uchar commandId, const QString& action) {
    logDebug("AppDbusCli", QString("Sending device command: command=0x%1, action=%2")
        .arg(commandId, 2, 16, QChar('0')).arg(action));

    if (!m_routerInterface || !m_routerInterface->isValid()) {
        logWarning("AppDbusCli", "ProtocolRouter interface is not available");
        return false;
    }

    const QString normalizedAction = action.trimmed().toLower();
    switch (static_cast<SPS::UART::CommandId>(commandId)) {
        case SPS::UART::CommandId::LIGHT_CONTROL: {
            SPS::UART::ControlValue value;
            if (!actionToControlValue(normalizedAction, value)) {
                logWarning("AppDbusCli", QString("Unsupported light action: %1").arg(action));
                return false;
            }
            return callRouterBool("ControlLight", {
                QVariant::fromValue(static_cast<quint8>(SPS::UART::toByte(SPS::UART::DeviceId::LIGHT_ALL))),
                isOnValue(value)
            });
        }
        case SPS::UART::CommandId::CURTAIN_CONTROL: {
            QString curtainAction = normalizedAction;
            uchar targetDevice = SPS::UART::toByte(SPS::UART::DeviceId::CURTAIN);

            if (curtainAction.startsWith("screen-")) {
                targetDevice = SPS::UART::toByte(SPS::UART::DeviceId::SCREEN);
                curtainAction = curtainAction.mid(QStringLiteral("screen-").size());
            } else if (curtainAction.startsWith("curtain-")) {
                targetDevice = SPS::UART::toByte(SPS::UART::DeviceId::CURTAIN);
                curtainAction = curtainAction.mid(QStringLiteral("curtain-").size());
            }

            SPS::UART::ControlValue value;
            if (curtainAction == "stop") {
                value = SPS::UART::ControlValue::STOP;
            } else if (!actionToControlValue(curtainAction, value)) {
                logWarning("AppDbusCli", QString("Unsupported curtain/screen action: %1").arg(action));
                return false;
            }

            return callRouterBool("ControlCurtain", {
                QVariant::fromValue(static_cast<quint8>(targetDevice)),
                QVariant::fromValue(static_cast<quint8>(SPS::UART::toByte(value)))
            });
        }
        case SPS::UART::CommandId::PROJECTOR_CONTROL: {
            SPS::UART::ControlValue value;
            if (!actionToControlValue(normalizedAction, value)) {
                logWarning("AppDbusCli", QString("Unsupported projector action: %1").arg(action));
                return false;
            }
            return callRouterBool("ControlProjector", { isOnValue(value) });
        }
        case SPS::UART::CommandId::AC_CONTROL: {
            SPS::UART::ControlValue value;
            if (!actionToControlValue(normalizedAction, value)) {
                logWarning("AppDbusCli", QString("Unsupported AC action: %1").arg(action));
                return false;
            }
            return callRouterBool("ControlAC", {
                QVariant::fromValue(static_cast<quint8>(SPS::UART::toByte(SPS::UART::DeviceId::AC_ID))),
                isOnValue(value)
            });
        }
        default:
            logWarning("AppDbusCli", QString("Unsupported command id: 0x%1")
                .arg(commandId, 2, 16, QChar('0')));
            return false;
    }
}

/** Controls a classroom device using UI-level names instead of raw UART command IDs. */
bool AppDbusCli::controlClassroomDevice(const QString& deviceKey, const QString& action) {
    const QString key = normalizeKey(deviceKey);
    const QString normalizedAction = action.trimmed().toLower();

    if (key == "deskpc" || key == "pc" || key == "computer") {
        if (normalizedAction == "on" || normalizedAction == "wake") {
            return sendWoL(m_pcMacAddress);
        }

        logWarning("AppDbusCli", QString("Desk PC action is not supported: %1").arg(action));
        return false;
    }

    if (key == "roomlights" || key == "lights") {
        return sendDeviceCommand(SPS::UART::toByte(SPS::UART::CommandId::LIGHT_CONTROL), normalizedAction);
    }
    if (key == "curtains" || key == "curtain") {
        return sendDeviceCommand(SPS::UART::toByte(SPS::UART::CommandId::CURTAIN_CONTROL),
                                 QStringLiteral("curtain-%1").arg(normalizedAction));
    }
    if (key == "projectionscreen" || key == "screen") {
        return sendDeviceCommand(SPS::UART::toByte(SPS::UART::CommandId::CURTAIN_CONTROL),
                                 QStringLiteral("screen-%1").arg(normalizedAction));
    }
    if (key == "projector") {
        return sendDeviceCommand(SPS::UART::toByte(SPS::UART::CommandId::PROJECTOR_CONTROL), normalizedAction);
    }
    if (key == "airconditioner" || key == "ac") {
        if (normalizedAction == "tempup" || normalizedAction == "temperatureup" ||
            normalizedAction == "increase" || normalizedAction == "up") {
            return callRouterBool("IncreaseACTemperature", {
                QVariant::fromValue(static_cast<quint8>(SPS::UART::toByte(SPS::UART::DeviceId::AC_ID)))
            });
        }
        if (normalizedAction == "tempdown" || normalizedAction == "temperaturedown" ||
            normalizedAction == "decrease" || normalizedAction == "down") {
            return callRouterBool("DecreaseACTemperature", {
                QVariant::fromValue(static_cast<quint8>(SPS::UART::toByte(SPS::UART::DeviceId::AC_ID)))
            });
        }
        return sendDeviceCommand(SPS::UART::toByte(SPS::UART::CommandId::AC_CONTROL), normalizedAction);
    }

    logWarning("AppDbusCli", QString("Unknown classroom device key: %1").arg(deviceKey));
    return false;
}

/** Queries the status of a device from the protocol router D-Bus service. */
uint AppDbusCli::getDeviceStatus(uchar deviceId) {
    if (!m_routerInterface || !m_routerInterface->isValid()) {
        return 0;
    }

    QDBusReply<uchar> reply = m_routerInterface->call(
        "GetDeviceStatus", QVariant::fromValue(static_cast<quint8>(deviceId)));
    return reply.isValid() ? reply.value() : 0;
}

/** Converts a scenario ID into a touch-friendly display name. */
QString AppDbusCli::getScenarioDisplayName(const QString& scenarioId) const {
    QString displayName = scenarioId.trimmed();
    if (displayName.startsWith("scenario-", Qt::CaseInsensitive)) {
        displayName = displayName.mid(QStringLiteral("scenario-").size());
    }

    displayName.replace('-', ' ');
    displayName.replace('_', ' ');

    QStringList words = displayName.split(' ', Qt::SkipEmptyParts);
    for (QString& word : words) {
        word = word.left(1).toUpper() + word.mid(1).toLower();
    }

    return words.isEmpty() ? scenarioId : words.join(' ');
}

// === Network Manager Methods ===

/** Connects to an MQTT broker via the network manager D-Bus service. */
bool AppDbusCli::connectToMqtt(const QString& broker, int port) {
    logInfo("AppDbusCli", QString("Calling network.ConnectToMqtt(%1:%2)").arg(broker).arg(port));
    QVariant result = callMethod(m_netMgrInterface, "ConnectToMqtt", broker, port);
    return result.toBool();
}

/** Disconnects from the MQTT broker via the network manager D-Bus service. */
bool AppDbusCli::disconnectFromMqtt() {
    logInfo("AppDbusCli", "Calling network.DisconnectFromMqtt()");
    QVariant result = callMethod(m_netMgrInterface, "DisconnectFromMqtt");
    return result.toBool();
}

/** Sends a WoL packet to the given MAC address; falls back to configured address if empty. Skips if PC control is disabled. */
bool AppDbusCli::sendWoL(const QString& macAddress) {
    if (!m_pcControlEnabled) {
        logWarning("AppDbusCli", "Wake-on-LAN skipped because PC control is disabled");
        return false;
    }

    const QString targetMac = macAddress.trimmed().isEmpty() ? m_pcMacAddress : macAddress.trimmed();
    logInfo("AppDbusCli", QString("Sending WoL to %1").arg(targetMac));
    QVariant result = callMethod(m_netMgrInterface, "SendWakeOnLAN", targetMac,
                                 SPS::Runtime::envString("SPS_WOL_BROADCAST", "255.255.255.255"));
    return result.toBool();
}

/** Returns the current MQTT connection status from the network manager. */
QString AppDbusCli::getMqttStatus() {
    QVariant result = callMethod(m_netMgrInterface, "GetMqttStatus");
    return result.toString();
}

/** Returns the current network status from the network manager. */
QString AppDbusCli::getNetworkStatus() {
    QVariant result = callMethod(m_netMgrInterface, "GetNetworkStatus");
    return result.toString();
}

// === Getters ===

/** Returns the cached authentication status string. */
QString AppDbusCli::getAuthStatus() const {
    return m_authStatus;
}

/** Returns the ID of the currently executing scenario. */
QString AppDbusCli::getCurrentScenario() const {
    return m_currentScenario;
}

/** Returns the cached MQTT status string. */
QString AppDbusCli::getMqttStatus() const {
    return m_mqttStatus;
}

/** Returns whether generic network connectivity is currently online. */
bool AppDbusCli::isNetworkConnected() const {
    return m_networkConnected;
}

/** Returns whether PC Wake-on-LAN control is enabled. */
bool AppDbusCli::isPcControlEnabled() const {
    return m_pcControlEnabled;
}

/** Returns the configured PC MAC address for Wake-on-LAN. */
QString AppDbusCli::getPcMacAddress() const {
    return m_pcMacAddress;
}

/** Refreshes PC control config from environment variables and D-Bus; emits pcControlConfigChanged on change. */
bool AppDbusCli::refreshPcControlConfig() {
    bool changed = false;
    bool enabled = SPS::Runtime::envBool("SPS_ENABLE_PC_CONTROL", m_pcControlEnabled);
    QString macAddress = SPS::Runtime::envString("SPS_PC_MAC", m_pcMacAddress);

    if (m_netMgrInterface && m_netMgrInterface->isValid()) {
        QDBusReply<bool> enabledReply = m_netMgrInterface->call("IsPcControlEnabled");
        if (enabledReply.isValid()) {
            enabled = enabledReply.value();
        }

        QDBusReply<QString> macReply = m_netMgrInterface->call("GetPcMacAddress");
        if (macReply.isValid()) {
            macAddress = macReply.value();
        }
    }

    if (enabled != m_pcControlEnabled || macAddress != m_pcMacAddress) {
        m_pcControlEnabled = enabled;
        m_pcMacAddress = macAddress;
        changed = true;
        emit pcControlConfigChanged();
    }

    logInfo("AppDbusCli", QString("PC control config: enabled=%1, mac=%2")
        .arg(m_pcControlEnabled ? "true" : "false")
        .arg(m_pcMacAddress.isEmpty() ? "<not set>" : m_pcMacAddress));
    return changed;
}

// === Private Methods ===

/** Connects to a D-Bus service at the given bus name and object path, storing the interface pointer. */
bool AppDbusCli::connectToService(QDBusInterface*& iface, const QString& service, const QString& path) {
    iface = new QDBusInterface(service, path, service, QDBusConnection::systemBus());

    if (!iface->isValid()) {
        logWarning("AppDbusCli", QString("Failed to connect to %1").arg(service));
        return false;
    }

    logInfo("AppDbusCli", QString("Connected to service: %1").arg(service));
    return true;
}

/** Wires up all D-Bus service signals to their corresponding private slot handlers. */
bool AppDbusCli::setupSignalConnections() {
    bool ok = true;
    QDBusConnection dbus = QDBusConnection::systemBus();

    auto connectSignal = [&](const QString& service,
                             const QString& path,
                             const QString& interface,
                             const QString& signal,
                             const char* slot) {
        const bool connected = dbus.connect(service, path, interface, signal, this, slot);
        if (!connected) {
            logWarning("AppDbusCli", QString("Failed to connect D-Bus signal %1.%2")
                .arg(interface, signal));
        }
        ok = ok && connected;
    };

    // Auth service signals
    if (m_authInterface) {
        connectSignal("com.sps.auth", "/com/sps/auth", "com.sps.auth",
                      "AuthStatusChanged", SLOT(onAuthStatusChanged(int)));
        connectSignal("com.sps.auth", "/com/sps/auth", "com.sps.auth",
                      "LecturerAuthenticated", SLOT(onLecturerAuthenticated(QString,qlonglong)));
        connectSignal("com.sps.auth", "/com/sps/auth", "com.sps.auth",
                      "RoomMonitorAlert", SLOT(onRoomMonitorAlert(QString,QString)));
    }

    // Scenario engine signals
    if (m_engineInterface) {
        connectSignal("com.sps.engine", "/com/sps/engine", "com.sps.engine",
                      "ScenarioStarted", SLOT(onScenarioStarted(QString)));
        connectSignal("com.sps.engine", "/com/sps/engine", "com.sps.engine",
                      "ScenarioCompleted", SLOT(onScenarioCompleted(QString)));
        connectSignal("com.sps.engine", "/com/sps/engine", "com.sps.engine",
                      "ScenarioError", SLOT(onScenarioError(QString,QString)));
        connectSignal("com.sps.engine", "/com/sps/engine", "com.sps.engine",
                      "ContextTriggered", SLOT(onContextTriggered(QString,QString)));
    }

    // Protocol router signals
    if (m_routerInterface) {
        connectSignal("com.sps.router", "/com/sps/router", "com.sps.router",
                      "CommandAcknowledged", SLOT(onCommandAck(uchar)));
        connectSignal("com.sps.router", "/com/sps/router", "com.sps.router",
                      "CommandError", SLOT(onCommandError(uchar,uchar)));
    }

    // Network manager signals
    if (m_netMgrInterface) {
        connectSignal("com.sps.netmgr", "/com/sps/netmgr", "com.sps.netmgr",
                      "MqttConnected", SLOT(onMqttConnected()));
        connectSignal("com.sps.netmgr", "/com/sps/netmgr", "com.sps.netmgr",
                      "MqttDisconnected", SLOT(onMqttDisconnected(QString)));
        connectSignal("com.sps.netmgr", "/com/sps/netmgr", "com.sps.netmgr",
                      "NetworkStatusChanged", SLOT(onNetworkStatusChanged(bool)));
        connectSignal("com.sps.netmgr", "/com/sps/netmgr", "com.sps.netmgr",
                      "CommandReceived", SLOT(onCommandReceived(QString,QByteArray)));
    }

    return ok;
}

/** Calls a D-Bus method on the given interface with up to two optional arguments. Returns the result variant. */
QVariant AppDbusCli::callMethod(QDBusInterface* iface, const QString& method, 
                                const QVariant& arg1, const QVariant& arg2) {
    if (!iface || !iface->isValid()) {
        logWarning("AppDbusCli", QString("Interface not valid for method %1").arg(method));
        return QVariant();
    }

    QDBusMessage reply;
    if (arg2.isValid()) {
        reply = iface->call(method, arg1, arg2);
    } else if (arg1.isValid()) {
        reply = iface->call(method, arg1);
    } else {
        reply = iface->call(method);
    }

    if (reply.type() == QDBusMessage::ErrorMessage) {
        logError("AppDbusCli", QString("Method call failed: %1 - %2")
            .arg(method).arg(reply.errorMessage()));
        return QVariant();
    }

    if (reply.arguments().isEmpty()) {
        return QVariant();
    }

    return reply.arguments().at(0);
}

/** Calls a typed boolean command on the ProtocolRouter service. */
bool AppDbusCli::callRouterBool(const QString& method, const QVariantList& args) {
    if (!m_routerInterface || !m_routerInterface->isValid()) {
        logWarning("AppDbusCli", "ProtocolRouter interface is not available");
        return false;
    }

    logInfo("AppDbusCli", QString("Calling router.%1").arg(method));

    QDBusMessage reply = m_routerInterface->callWithArgumentList(QDBus::Block, method, args);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        logError("AppDbusCli", QString("%1 failed: %2").arg(method, reply.errorMessage()));
        return false;
    }

    return !reply.arguments().isEmpty() && reply.arguments().at(0).toBool();
}

// === Signal Handlers ===

/** Handles AuthStatusChanged from the auth service: maps the int status to a string and emits authStatusChanged. */
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

/** Handles LecturerAuthenticated from the auth service and re-emits it. */
void AppDbusCli::onLecturerAuthenticated(const QString& lecturerName, qlonglong timestamp) {
    logInfo("AppDbusCli", QString("Lecturer authenticated: %1").arg(lecturerName));
    emit lecturerAuthenticated(lecturerName, timestamp);
}

/** Handles ScenarioStarted from the engine service and re-emits it. */
void AppDbusCli::onScenarioStarted(const QString& scenarioId) {
    logInfo("AppDbusCli", QString("Scenario started: %1").arg(scenarioId));
    emit scenarioStarted(scenarioId);
}

/** Handles ScenarioCompleted from the engine service, clears the current scenario, and re-emits it. */
void AppDbusCli::onScenarioCompleted(const QString& scenarioId) {
    logInfo("AppDbusCli", QString("Scenario completed: %1").arg(scenarioId));
    m_currentScenario.clear();
    emit scenarioCompleted(scenarioId);
}

/** Handles ScenarioError from the engine service and re-emits it with the error message. */
void AppDbusCli::onScenarioError(const QString& scenarioId, const QString& error) {
    logError("AppDbusCli", QString("Scenario error: %1 - %2").arg(scenarioId).arg(error));
    emit scenarioError(scenarioId, error);
}

/** Handles ContextTriggered from the engine service and re-emits it. */
void AppDbusCli::onContextTriggered(const QString& context, const QString& scenarioId) {
    logInfo("AppDbusCli", QString("Context triggered: %1 -> %2").arg(context).arg(scenarioId));
    emit contextTriggered(context, scenarioId);
}

/** Handles MqttConnected from the network manager, updates cached status, and re-emits it. */
void AppDbusCli::onMqttConnected() {
    logInfo("AppDbusCli", "MQTT connected");
    m_mqttStatus = "CONNECTED";
    m_networkConnected = true;
    emit mqttStatusChanged(m_mqttStatus);
    emit mqttConnected();
    emit networkStatusChanged(m_networkConnected);
}

/** Handles MqttDisconnected from the network manager, updates cached status, and re-emits it. */
void AppDbusCli::onMqttDisconnected(const QString& reason) {
    logWarning("AppDbusCli", QString("MQTT disconnected: %1").arg(reason));
    m_mqttStatus = "DISCONNECTED";
    m_networkConnected = false;
    emit mqttStatusChanged(m_mqttStatus);
    emit mqttDisconnected(reason);
    emit networkStatusChanged(m_networkConnected);
}

/** Handles NetworkStatusChanged from the network manager and re-emits it. */
void AppDbusCli::onNetworkStatusChanged(bool connected) {
    logInfo("AppDbusCli", QString("Network status: %1").arg(connected ? "ONLINE" : "OFFLINE"));
    m_networkConnected = connected;
    emit networkStatusChanged(connected);
}

/** Handles CommandReceived from the network manager and re-emits it. */
void AppDbusCli::onCommandReceived(const QString& command, const QByteArray& payload) {
    logDebug("AppDbusCli", QString("Command received: %1").arg(command));
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    emit commandReceived(command, doc.isObject() ? doc.object() : QJsonObject());
}

/** Handles CommandAcknowledged from the protocol router and re-emits it. */
void AppDbusCli::onCommandAck(uchar cmdId) {
    logDebug("AppDbusCli", QString("Command ACK: 0x%1").arg(cmdId, 2, 16, QChar('0')));
    emit commandAck(cmdId);
}

/** Handles CommandError from the protocol router and re-emits it with the error code. */
void AppDbusCli::onCommandError(uchar cmdId, uchar errorCode) {
    logError("AppDbusCli", QString("Command error: 0x%1 - 0x%2")
        .arg(cmdId, 2, 16, QChar('0')).arg(errorCode, 2, 16, QChar('0')));
    emit commandError(cmdId, errorCode);
}

/** Handles RoomMonitorAlert from the auth service: forwards to QML and publishes MQTT alert. */
void AppDbusCli::onRoomMonitorAlert(const QString& alertType, const QString& payloadJson) {
    logInfo("AppDbusCli", QString("Room monitor alert: %1").arg(alertType));

    // Forward to QML for warning banner display
    emit roomMonitorAlert(alertType, payloadJson);

    // Publish alert to MQTT via NetworkManager
    if (!m_netMgrInterface || !m_netMgrInterface->isValid()) {
        logWarning("AppDbusCli", "NetworkManager not available - alert not published to MQTT");
        return;
    }

    // Get room ID from NetworkManager
    QDBusReply<QString> roomReply = m_netMgrInterface->call("GetRoomId");
    QString roomId = roomReply.isValid() ? roomReply.value() : "unknown";

    // Augment payload with room_id
    QJsonDocument doc = QJsonDocument::fromJson(payloadJson.toUtf8());
    if (!doc.isObject()) {
        logError("AppDbusCli", "Invalid JSON payload in RoomMonitorAlert");
        return;
    }

    QJsonObject payload = doc.object();
    payload["room_id"] = roomId;
    QByteArray jsonBytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    // Publish to sps/{roomId}/event/alert
    QString topic = QString("sps/%1/event/alert").arg(roomId);
    QVariantList args;
    args << topic << jsonBytes << 1;

    QDBusMessage reply = m_netMgrInterface->callWithArgumentList(QDBus::Block, "PublishEvent", args);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        logWarning("AppDbusCli", QString("Failed to publish alert to MQTT: %1").arg(reply.errorMessage()));
    } else {
        logInfo("AppDbusCli", QString("Published %1 alert to MQTT topic %2").arg(alertType, topic));
    }
}

#include "moc_app_dbus_client.cpp"
