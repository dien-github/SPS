#include "scenario_engine.h"
#include "../common/sps_logger.h"
#include "../common/sps_runtime_config.h"
#include "router_command_normalizer.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QVariant>

namespace {
QString jsonString(const QJsonObject& object, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        const QJsonValue value = object.value(QString::fromLatin1(key));
        if (!value.isUndefined() && !value.isNull()) {
            const QString text = value.toVariant().toString().trimmed();
            if (!text.isEmpty()) {
                return text;
            }
        }
    }
    return QString();
}

QString commandTextFromJson(const QJsonObject& object) {
    const QString command = jsonString(object, {"command", "command_type"});
    if (!command.isEmpty()) {
        return command;
    }

    const QString type = jsonString(object, {"type"});
    if (!type.isEmpty() &&
        SPS::AutoEngine::parseDeviceType(type) == SPS::Device::Type::UNKNOWN) {
        return type;
    }

    const QString action = jsonString(object, {"action"});
    if (!action.isEmpty() &&
        SPS::AutoEngine::parseDeviceState(action) == SPS::Device::State::UNKNOWN) {
        return action;
    }

    return QString();
}

QString stateTextFromJson(const QJsonObject& object) {
    QString state = jsonString(object, {"target_state", "state", "value", "requested_state", "status"});
    if (!state.isEmpty()) {
        return state;
    }

    const QString action = jsonString(object, {"action"});
    if (SPS::AutoEngine::parseDeviceState(action) != SPS::Device::State::UNKNOWN) {
        return action;
    }

    return QString();
}

QString deviceTypeTextFromJson(const QJsonObject& object) {
    QString deviceType = jsonString(object, {"device_type", "device_kind"});
    if (!deviceType.isEmpty()) {
        return deviceType;
    }

    const QString type = jsonString(object, {"type"});
    if (SPS::AutoEngine::parseDeviceType(type) != SPS::Device::Type::UNKNOWN) {
        return type;
    }

    return QString();
}
} // namespace

/** Constructor. Initialises execution context, timers, and counters. */
ScenarioEngine::ScenarioEngine(QObject* parent)
    : SpsServiceBase("com.sps.engine", "/com/sps/engine", parent),
      m_scenariosPath(SPS::Runtime::configFile("SPS_SCENARIOS_FILE", "scenarios.json")),
      m_deviceMapPath(SPS::Runtime::configFile("SPS_DEVICES_FILE", "devices.json")),
      m_routerInterface(nullptr),
      m_authInterface(nullptr),
      m_scenariosExecuted(0),
      m_scenariosFailed(0),
      m_totalCommands(0),
      m_autoTriggeredScenarios(0) {

    m_execution.state = IDLE;
    m_execution.currentCommandIndex = 0;

    connect(&m_commandTimer, &QTimer::timeout, this, &ScenarioEngine::executeNextCommand);
    m_commandTimer.setSingleShot(true);

    logInfo("Scenario Engine service created");
}

/** Destructor. Calls shutdown to clean up all resources. */
ScenarioEngine::~ScenarioEngine() {
    shutdown();
}

/** Initialises the service: loads device map, scenarios, connects to ProtocolRouter, and registers D-Bus. */
bool ScenarioEngine::initialize() {
    logInfo("Initializing Scenario Engine service...");

    if (!loadDeviceMap(m_deviceMapPath)) {
        logWarning("Failed to load device map - service will operate with no scenarios");
    }

    if (!loadScenarios(m_scenariosPath)) {
        logWarning("Failed to load scenarios - service will operate with no scenarios");
    }

    if (!connectToRouter()) {
        logWarning("Failed to connect to ProtocolRouter - command execution unavailable");
    }

    if (!connectToNetworkManager()) {
        logWarning("Failed to connect to NetworkManager - remote MQTT commands unavailable");
    }

    if (!registerService()) {
        logError("Failed to register D-Bus service");
        return false;
    }

    setRunning(true);
    logInfo(QString("Scenario Engine initialized with %1 scenarios").arg(m_scenarios.size()));

    return true;
}

/** Stops execution, clears scenarios, and releases D-Bus interfaces. */
void ScenarioEngine::shutdown() {
    logInfo("Shutting down Scenario Engine service...");

    if (m_execution.state != IDLE) {
        stopExecution("Service shutting down");
    }

    m_commandTimer.stop();
    m_deviceMap = DeviceMap();
    m_scenarios.clear();
    m_contextTriggers.clear();

    if (m_routerInterface) {
        delete m_routerInterface;
        m_routerInterface = nullptr;
    }

    if (m_authInterface) {
        delete m_authInterface;
        m_authInterface = nullptr;
    }

    SpsServiceBase::shutdown();
}

/** Returns a human-readable status string with engine state and scenario counts. */
QString ScenarioEngine::getStatus() const {
    QString stateStr;
    switch (m_execution.state) {
        case IDLE:      stateStr = "IDLE"; break;
        case RUNNING:   stateStr = "RUNNING"; break;
        case PAUSED:    stateStr = "PAUSED"; break;
        case COMPLETED: stateStr = "COMPLETED"; break;
        case ERROR:     stateStr = "ERROR"; break;
        default:        stateStr = "UNKNOWN"; break;
    }

    return QString("Engine Status: %1 | Devices: %2 | Scenarios: %3 | Executed: %4/%5")
        .arg(stateStr).arg(m_deviceMap.size()).arg(m_scenarios.size()).arg(m_scenariosExecuted).arg(m_scenariosFailed);
}

/** Loads scenarios from a JSON file. Falls back to sample scenarios if the file is missing. */
bool ScenarioEngine::loadScenarios(const QString& scenariosPath) {
    m_scenariosPath = scenariosPath;

    QFile file(scenariosPath);
    if (!file.open(QIODevice::ReadOnly)) {
        logWarning(QString("Cannot open scenarios file: %1").arg(scenariosPath));

        Scenario startup("scenario-startup", "Startup");
        startup.description = "Turn on lights and projector";
        startup.addCommand(ScenarioCommand(1, SPS::Device::Type::LIGHT, "light-class", SPS::Device::State::ON, 0));
        startup.addCommand(ScenarioCommand(2, SPS::Device::Type::PROJECTOR, "proj-1", SPS::Device::State::ON, 500));
        m_scenarios[startup.id] = startup;

        Scenario shutdown("scenario-shutdown", "Shutdown");
        shutdown.description = "Turn off lights and projector";
        shutdown.addCommand(ScenarioCommand(1, SPS::Device::Type::PROJECTOR, "proj-1", SPS::Device::State::OFF, 0));
        shutdown.addCommand(ScenarioCommand(2, SPS::Device::Type::LIGHT, "light-class", SPS::Device::State::OFF, 500));
        m_scenarios[shutdown.id] = shutdown;

        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) {
        logError("Scenarios file is not a JSON array");
        return false;
    }

    QJsonArray array = doc.array();

    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }

        QJsonObject obj = value.toObject();
        QString id = obj["id"].toString();
        QString name = obj["name"].toString();
        QString description = obj["description"].toString();

        if (id.isEmpty() || name.isEmpty()) {
            continue;
        }

        Scenario scenario(id, name);
        scenario.description = description;

        QJsonArray commands = obj["commands"].toArray();
        for (int i = 0; i < commands.size(); ++i) {
            if (!commands[i].isObject()) {
                continue;
            }

            QJsonObject cmdObj = commands[i].toObject();
            int order = cmdObj["order"].toInt(i);
            QString deviceType = deviceTypeTextFromJson(cmdObj);
            QString deviceId = jsonString(cmdObj, {"device_id", "device", "id"});
            QString channel = jsonString(cmdObj, {"channel", "device_channel", "hardware_id"});
            if (deviceId.isEmpty()) {
                deviceId = channel;
            }
            int delayMs = cmdObj["delay_ms"].toInt(0);
            QString commandType = commandTextFromJson(cmdObj);
            QString targetState = stateTextFromJson(cmdObj);

            const SPS::Device::Type type = SPS::AutoEngine::parseDeviceType(deviceType, deviceId);
            const SPS::Device::State state = SPS::AutoEngine::parseDeviceState(targetState);

            scenario.addCommand(ScenarioCommand(
                order,
                type,
                deviceId,
                state,
                delayMs,
                commandType,
                targetState,
                channel,
                SPS::AutoEngine::compactCommandJson(cmdObj)));
        }

        m_scenarios[id] = scenario;
    }

    logInfo(QString("Loaded %1 scenarios").arg(m_scenarios.size()));
    return true;
}

/** Loads device mappings from a JSON file. Falls back to demo map if file is missing. */
bool ScenarioEngine::loadDeviceMap(const QString& deviceMapPath) {
    m_deviceMapPath = deviceMapPath;

    if (m_deviceMap.load(deviceMapPath)) {
        return true;
    }

    logWarning("Device map file not available, loading demo device map");
    m_deviceMap.loadDemo();

    logInfo(QString("Loaded %1 demo device mappings").arg(m_deviceMap.size()));
    return false;
}

/** Registers a context-to-scenario mapping so a context event auto-triggers a scenario. */
bool ScenarioEngine::registerContextTrigger(const QString& context, const QString& scenarioId) {
    if (!m_scenarios.contains(scenarioId)) {
        logWarning(QString("Scenario not found: %1").arg(scenarioId));
        return false;
    }

    m_contextTriggers[context] = scenarioId;
    logInfo(QString("Context trigger registered: %1 -> %2").arg(context, scenarioId));

    return true;
}

/** Returns a list of all available scenario IDs. */
QStringList ScenarioEngine::getAvailableScenarios() const {
    return m_scenarios.keys();
}

/** Returns the current execution status (RUNNING, PAUSED, etc.) for the given scenario. */
QString ScenarioEngine::getScenarioStatus(const QString& scenarioId) const {
    if (m_execution.scenarioId == scenarioId) {
        switch (m_execution.state) {
            case RUNNING:   return "RUNNING";
            case PAUSED:    return "PAUSED";
            case COMPLETED: return "COMPLETED";
            case ERROR:     return "ERROR";
            default:        break;
        }
    }
    return "IDLE";
}

/** Returns the description text of the specified scenario. */
QString ScenarioEngine::getScenarioDescription(const QString& scenarioId) const {
    if (m_scenarios.contains(scenarioId)) {
        return m_scenarios[scenarioId].description;
    }
    return "";
}

/** D-Bus callable. Returns a list of all available scenario IDs. */
QStringList ScenarioEngine::GetAvailableScenarios() const {
    return getAvailableScenarios();
}

/** D-Bus callable. Returns the engine status string. */
QString ScenarioEngine::GetEngineStatus() const {
    return getStatus();
}

/** D-Bus callable. Returns scenario info including description and command count. */
QString ScenarioEngine::GetScenarioInfo(const QString& scenarioId, QString& description, int& commandCount) const {
    if (!m_scenarios.contains(scenarioId)) {
        description.clear();
        commandCount = 0;
        return QString();
    }

    const Scenario& scenario = m_scenarios[scenarioId];
    description = scenario.description;
    commandCount = scenario.commands.size();
    return scenario.name;
}

/** D-Bus callable. Starts execution of the specified scenario. */
bool ScenarioEngine::ExecuteScenario(const QString& scenarioId) {
    logInfo(QString("Execute scenario request: %1").arg(scenarioId));

    if (!m_scenarios.contains(scenarioId)) {
        logError(QString("Scenario not found: %1").arg(scenarioId));
        emit ScenarioError(scenarioId, "Scenario not found");
        return false;
    }

    if (m_execution.state != IDLE) {
        logWarning("Another scenario is already running");
        emit ScenarioError(scenarioId, "Another scenario is running");
        return false;
    }

    return startExecution(scenarioId);
}

/** D-Bus callable. Returns the execution status of the given scenario. */
QString ScenarioEngine::GetScenarioStatus(const QString& scenarioId) const {
    return getScenarioStatus(scenarioId);
}

/** D-Bus callable. Stops the currently running scenario. */
bool ScenarioEngine::StopScenario(const QString& scenarioId) {
    if (m_execution.scenarioId != scenarioId) {
        logWarning(QString("Scenario not currently running: %1").arg(scenarioId));
        return false;
    }

    stopExecution("User stopped");
    return true;
}

/** D-Bus callable. Sends a direct control command to a device. */
bool ScenarioEngine::ControlDevice(uchar deviceId, const QString& action) {
    logInfo(QString("Direct device control: id=%1, action=%2").arg(deviceId).arg(action));

    const SPS::Device::State state = SPS::AutoEngine::parseDeviceState(action);
    QJsonObject raw;
    raw["device_id"] = static_cast<int>(deviceId);
    raw["action"] = action;
    ScenarioCommand command(0,
                            SPS::Device::Type::UNKNOWN,
                            QString::number(deviceId),
                            state,
                            0,
                            state == SPS::Device::State::UNKNOWN ? action : QString(),
                            state == SPS::Device::State::UNKNOWN ? QString() : action,
                            QString(),
                            SPS::AutoEngine::compactCommandJson(raw));

    return sendControlCommand(command, "direct-control", 0);
}

/** D-Bus callable. Registers a context trigger for automatic scenario execution. */
bool ScenarioEngine::RegisterContextTrigger(const QString& context, const QString& scenarioId) {
    return registerContextTrigger(context, scenarioId);
}

// Private Methods

/** Starts execution of the specified scenario. Returns false if scenario is not found. */
bool ScenarioEngine::startExecution(const QString& scenarioId) {
    if (!m_scenarios.contains(scenarioId)) {
        return false;
    }

    m_execution.scenarioId = scenarioId;
    m_execution.currentCommandIndex = 0;
    m_execution.startTime = QDateTime::currentDateTime();
    m_execution.lastError.clear();

    setExecutionState(RUNNING);
    m_scenariosExecuted++;

    logInfo(QString("Executing scenario: %1").arg(scenarioId));
    emit ScenarioStarted(scenarioId);

    executeNextCommand();

    return true;
}

/** Stops the currently running scenario with the given reason. */
void ScenarioEngine::stopExecution(const QString& reason) {
    if (m_execution.state == IDLE) {
        return;
    }

    QString scenarioId = m_execution.scenarioId;

    m_commandTimer.stop();
    setExecutionState(IDLE);

    logInfo(QString("Scenario stopped: %1 - %2").arg(scenarioId, reason));
    emit ScenarioError(scenarioId, reason);
}

/** Updates the execution state and emits relevant signals. */
void ScenarioEngine::setExecutionState(ExecutionState newState) {
    m_execution.state = newState;

    switch (newState) {
        case IDLE:
            logDebug("State: IDLE");
            break;
        case RUNNING:
            logDebug("State: RUNNING");
            break;
        case COMPLETED:
            logInfo(QString("Scenario completed: %1").arg(m_execution.scenarioId));
            emit ScenarioCompleted(m_execution.scenarioId);
            m_execution.scenarioId.clear();
            break;
        case ERROR:
            logError(QString("Scenario error: %1").arg(m_execution.lastError));
            break;
        default:
            break;
    }
}

/** Executes the next pending command in the current scenario. */
void ScenarioEngine::executeNextCommand() {
    if (m_execution.state != RUNNING) {
        return;
    }

    if (!m_scenarios.contains(m_execution.scenarioId)) {
        stopExecution("Scenario not found");
        return;
    }

    Scenario& scenario = m_scenarios[m_execution.scenarioId];

    if (m_execution.currentCommandIndex >= scenario.commands.size()) {
        setExecutionState(COMPLETED);
        setExecutionState(IDLE);
        return;
    }

    const int commandIndex = m_execution.currentCommandIndex;
    ScenarioCommand& cmd = scenario.commands[commandIndex];

    logDebug(QString("Executing command %1/%2 of scenario %3")
        .arg(m_execution.currentCommandIndex + 1)
        .arg(scenario.commands.size())
        .arg(m_execution.scenarioId));

    emit CommandExecuting(m_execution.scenarioId, m_execution.currentCommandIndex, 
                         QString("Command %1").arg(cmd.order));

    if (!executeCommand(cmd, m_execution.scenarioId, commandIndex)) {
        stopExecution("Command execution failed");
        return;
    }

    m_totalCommands++;
    m_execution.currentCommandIndex++;

    int delay = (m_execution.currentCommandIndex < scenario.commands.size()) 
        ? scenario.commands[m_execution.currentCommandIndex].delayMs 
        : 0;

    m_commandTimer.start(delay);
}

/** Sends a single command to a device through the ProtocolRouter. */
bool ScenarioEngine::executeCommand(const ScenarioCommand& cmd,
                                    const QString& scenarioId,
                                    int commandIndex) {
    return sendControlCommand(cmd, scenarioId, commandIndex);
}

/** Normalizes and sends a typed control command via the ProtocolRouter D-Bus interface. */
bool ScenarioEngine::sendControlCommand(const ScenarioCommand& cmd,
                                        const QString& scenarioId,
                                        int commandIndex) {
    if (!m_routerInterface || !m_routerInterface->isValid()) {
        logError("ProtocolRouter not connected");
        return false;
    }

    SPS::AutoEngine::NormalizedRouterCommand routerCommand;
    QString error;
    const QString rawJson = cmd.rawJson.isEmpty()
        ? QString("{device_id=%1,type=%2,state=%3,command=%4}")
            .arg(cmd.deviceId)
            .arg(static_cast<int>(cmd.deviceType))
            .arg(static_cast<int>(cmd.targetState))
            .arg(cmd.commandType)
        : cmd.rawJson;

    if (!SPS::AutoEngine::normalizeRouterCommand(cmd, m_deviceMap, routerCommand, error)) {
        logError(QString("Scenario command normalization failed: scenario_id=%1, command_index=%2, "
                         "raw=%3, normalized_device=%4, normalized_action=%5, normalized_state=%6, error=%7")
            .arg(scenarioId)
            .arg(commandIndex)
            .arg(rawJson)
            .arg(routerCommand.normalizedDevice)
            .arg(routerCommand.normalizedAction)
            .arg(routerCommand.normalizedState)
            .arg(error));
        return false;
    }

    logInfo(QString("Scenario command dispatch: scenario_id=%1, command_index=%2, raw_id=%3 -> "
                    "normalized_device=%4, normalized_action=%5, normalized_state=%6 -> router call %7; raw=%8")
        .arg(scenarioId)
        .arg(commandIndex)
        .arg(cmd.deviceId)
        .arg(routerCommand.normalizedDevice)
        .arg(routerCommand.normalizedAction)
        .arg(routerCommand.normalizedState)
        .arg(SPS::AutoEngine::describeRouterCommand(routerCommand))
        .arg(rawJson));

    QDBusMessage reply = m_routerInterface->callWithArgumentList(
        QDBus::Block, routerCommand.method, routerCommand.args);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        logError(QString("%1 D-Bus call failed: scenario_id=%2, command_index=%3, raw=%4, error=%5")
            .arg(routerCommand.method)
            .arg(scenarioId)
            .arg(commandIndex)
            .arg(rawJson)
            .arg(reply.errorMessage()));
        return false;
    }

    if (reply.arguments().isEmpty() || !reply.arguments().at(0).toBool()) {
        logError(QString("%1 returned false: scenario_id=%2, command_index=%3, raw=%4, normalized=%5")
            .arg(routerCommand.method)
            .arg(scenarioId)
            .arg(commandIndex)
            .arg(rawJson)
            .arg(SPS::AutoEngine::describeRouterCommand(routerCommand)));
        return false;
    }

    return true;
}

/** Creates and validates the D-Bus interface to the ProtocolRouter service. */
bool ScenarioEngine::connectToRouter() {
    m_routerInterface = new QDBusInterface("com.sps.router", "/com/sps/router", 
                                           "com.sps.router", 
                                           QDBusConnection::systemBus());

    if (!m_routerInterface->isValid()) {
        logWarning("ProtocolRouter D-Bus interface not available yet");
        return false;
    }

    QDBusConnection dbus = QDBusConnection::systemBus();
    dbus.connect(SPS::DBus::SERVICE_ROUTER,
                 SPS::DBus::PATH_ROUTER,
                 SPS::DBus::IFACE_ROUTER,
                 "CommandAcknowledged",
                 this,
                 SLOT(onRouterCommandAck(uchar)));
    dbus.connect(SPS::DBus::SERVICE_ROUTER,
                 SPS::DBus::PATH_ROUTER,
                 SPS::DBus::IFACE_ROUTER,
                 "CommandError",
                 this,
                 SLOT(onRouterCommandError(uchar,uchar)));
    dbus.connect(SPS::DBus::SERVICE_ROUTER,
                 SPS::DBus::PATH_ROUTER,
                 SPS::DBus::IFACE_ROUTER,
                 "PresenceDetected",
                 this,
                 SLOT(onPresenceDetected(bool)));

    logInfo("Connected to ProtocolRouter via D-Bus");
    return true;
}

/** Subscribes to NetworkManager's remote command D-Bus signal. */
bool ScenarioEngine::connectToNetworkManager() {
    const bool ok = QDBusConnection::systemBus().connect(
        SPS::DBus::SERVICE_NETMGR,
        SPS::DBus::PATH_NETMGR,
        SPS::DBus::IFACE_NETMGR,
        "CommandReceived",
        this,
        SLOT(onRemoteCommandReceived(QString,QByteArray)));

    if (ok) {
        logInfo("Connected to NetworkManager remote command signal");
    }

    return ok;
}

/** Calls a method on the ProtocolRouter D-Bus interface with optional arguments. */
bool ScenarioEngine::callRouterMethod(const QString& method, const QVariant& arg1, const QVariant& arg2) {
    if (!m_routerInterface) {
        return false;
    }

    QDBusReply<bool> reply;
    if (arg2.isValid()) {
        reply = m_routerInterface->call(method, arg1, arg2);
    } else if (arg1.isValid()) {
        reply = m_routerInterface->call(method, arg1);
    } else {
        reply = m_routerInterface->call(method);
    }

    return reply.value();
}

// Slot handlers

/** Handles a context event and triggers the associated scenario if registered. */
void ScenarioEngine::onContextEvent(const QString& context) {
    logDebug(QString("Context event: %1").arg(context));

    if (m_contextTriggers.contains(context)) {
        QString scenarioId = m_contextTriggers[context];
        logInfo(QString("Auto-triggering scenario: %1").arg(scenarioId));
        m_autoTriggeredScenarios++;
        emit ContextTriggered(context, scenarioId);
        ExecuteScenario(scenarioId);
    }
}

/** Called when a scenario step completes; triggers the next command. */
void ScenarioEngine::onScenarioStepCompleted() {
    executeNextCommand();
}

/** Handles a command failure by stopping the scenario with an error. */
void ScenarioEngine::onCommandFailed(const QString& error) {
    logError(QString("Command failed: %1").arg(error));
    stopExecution(error);
}

/** Handles a command acknowledgment from the ProtocolRouter. */
void ScenarioEngine::onRouterCommandAck(uchar cmdId) {
    logDebug(QString("Router ACK: 0x%1").arg(cmdId, 2, 16, QChar('0')));
}

/** Handles a command error from the ProtocolRouter. */
void ScenarioEngine::onRouterCommandError(uchar cmdId, uchar errorCode) {
    logError(QString("Router error: cmd=0x%1, code=0x%2")
        .arg(cmdId, 2, 16, QChar('0')).arg(errorCode, 2, 16, QChar('0')));

    if (m_execution.state == RUNNING) {
        stopExecution(QString("Router command failed: cmd=0x%1, code=0x%2")
            .arg(cmdId, 2, 16, QChar('0'))
            .arg(errorCode, 2, 16, QChar('0')));
    }
}

/** Handles presence detection events to trigger context scenarios. */
void ScenarioEngine::onPresenceDetected(bool present) {
    logDebug(QString("Presence: %1").arg(present ? "Yes" : "No"));

    if (present) {
        onContextEvent("presence_detected");
    }
}

/** Handles a remote command received from the server through NetworkManager/MQTT. */
void ScenarioEngine::onRemoteCommandReceived(const QString& commandType, const QByteArray& payload) {
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) {
        logWarning(QString("Ignoring remote %1 command with non-JSON payload").arg(commandType));
        return;
    }

    const QJsonObject object = doc.object();
    const QString normalizedCommand = commandType.trimmed().toLower();

    const QString scenarioId = jsonString(object, {"scenario_id", "scenario", "id"});
    const QString semanticAction = jsonString(object, {"command", "action"});
    if (normalizedCommand == "scenario" || semanticAction == "execute_scenario") {
        if (scenarioId.isEmpty()) {
            logWarning("Remote scenario command missing scenario_id");
            return;
        }
        ExecuteScenario(scenarioId);
        return;
    }

    if (normalizedCommand == "sync" || normalizedCommand == "ota") {
        logDebug(QString("Remote %1 command is handled by another service").arg(normalizedCommand));
        return;
    }

    QString deviceId = jsonString(object, {"device_id", "device", "id"});
    const QString channel = jsonString(object, {"channel", "hardware_id", "device_channel"});
    if (deviceId.isEmpty()) {
        deviceId = channel;
    }

    // Strip room prefix from global device IDs (e.g., "A01.01-ac" → "ac")
    // when room metadata from netmgr matches the prefix portion.
    const QString roomMeta = jsonString(object, {"room_id"});
    if (!roomMeta.isEmpty() && deviceId.contains('-')) {
        const int dashPos = deviceId.indexOf('-');
        const QString possibleRoom = deviceId.left(dashPos);
        const QString possibleDevice = deviceId.mid(dashPos + 1);
        if (possibleRoom == roomMeta &&
            SPS::AutoEngine::parseDeviceType(possibleDevice) != SPS::Device::Type::UNKNOWN) {
            logInfo(QString("Stripped room prefix from device_id: '%1' -> '%2'")
                .arg(deviceId, possibleDevice));
            deviceId = possibleDevice;
        }
    }

    const QString typeText = deviceTypeTextFromJson(object);
    const QString stateText = stateTextFromJson(object);
    const QString remoteCommandType = commandTextFromJson(object);
    ScenarioCommand command(0,
                            SPS::AutoEngine::parseDeviceType(typeText, normalizedCommand),
                            deviceId,
                            SPS::AutoEngine::parseDeviceState(stateText),
                            0,
                            remoteCommandType,
                            stateText,
                            channel,
                            SPS::AutoEngine::compactCommandJson(object));

    const bool ok = sendControlCommand(command, QString("remote:%1").arg(normalizedCommand), 0);
    if (!ok) {
        logError(QString("Failed to execute remote %1 command for device %2: raw_device=%3, room=%4")
            .arg(normalizedCommand, deviceId, jsonString(object, {"device_id", "device", "id"}), roomMeta));
    }
}
