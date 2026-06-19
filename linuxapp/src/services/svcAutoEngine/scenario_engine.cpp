#include "scenario_engine.h"
#include "../common/sps_logger.h"
#include "../common/sps_runtime_config.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDBusConnection>
#include <QDBusReply>

ScenarioEngine::ScenarioEngine(QObject* parent)
    : SpsServiceBase("com.sps.engine", "/com/sps/engine", parent),
      m_scenariosPath(SPS::Runtime::configFile("SPS_SCENARIOS_FILE", "scenarios.json")),
      m_routerInterface(nullptr),
      m_authInterface(nullptr),
      m_scenariosExecuted(0),
      m_scenariosFailed(0),
      m_totalCommands(0),
      m_autoTriggeredScenarios(0) {

    // Initialize execution context
    m_execution.state = IDLE;
    m_execution.currentCommandIndex = 0;

    // Setup command timer
    connect(&m_commandTimer, &QTimer::timeout, this, &ScenarioEngine::executeNextCommand);
    m_commandTimer.setSingleShot(true);

    logInfo("Scenario Engine service created");
}

ScenarioEngine::~ScenarioEngine() {
    shutdown();
}

// Initialize service
bool ScenarioEngine::initialize() {
    logInfo("Initializing Scenario Engine service...");

    // Load scenarios from configuration
    if (!loadScenarios(m_scenariosPath)) {
        logWarning("Failed to load scenarios - service will operate with no scenarios");
    }

    // Connect to ProtocolRouter
    if (!connectToRouter()) {
        logWarning("Failed to connect to ProtocolRouter - command execution unavailable");
    }

    // Register D-Bus service
    if (!registerService()) {
        logError("Failed to register D-Bus service");
        return false;
    }

    setRunning(true);
    logInfo(QString("Scenario Engine initialized with %1 scenarios").arg(m_scenarios.size()));

    return true;
}

// Shutdown service
void ScenarioEngine::shutdown() {
    logInfo("Shutting down Scenario Engine service...");

    if (m_execution.state != IDLE) {
        stopExecution("Service shutting down");
    }

    m_commandTimer.stop();
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

// Get service status
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

    return QString("Engine Status: %1 | Scenarios: %2 | Executed: %3/%4")
        .arg(stateStr).arg(m_scenarios.size()).arg(m_scenariosExecuted).arg(m_scenariosFailed);
}

// Load scenarios from JSON file
bool ScenarioEngine::loadScenarios(const QString& scenariosPath) {
    m_scenariosPath = scenariosPath;

    QFile file(scenariosPath);
    if (!file.open(QIODevice::ReadOnly)) {
        logWarning(QString("Cannot open scenarios file: %1").arg(scenariosPath));

        // Create sample scenarios for testing
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

        // Load commands
        QJsonArray commands = obj["commands"].toArray();
        for (int i = 0; i < commands.size(); ++i) {
            QJsonObject cmdObj = commands[i].toObject();
            int order = cmdObj["order"].toInt(i);
            QString deviceType = cmdObj["device_type"].toString();
            QString deviceId = cmdObj["device_id"].toString();
            int delayMs = cmdObj["delay_ms"].toInt(0);

            // Map device type string to enum
            SPS::Device::Type type = SPS::Device::Type::UNKNOWN;
            if (deviceType == "light") type = SPS::Device::Type::LIGHT;
            else if (deviceType == "projector") type = SPS::Device::Type::PROJECTOR;
            else if (deviceType == "curtain") type = SPS::Device::Type::CURTAIN;
            else if (deviceType == "ac") type = SPS::Device::Type::AC;

            SPS::Device::State state = SPS::Device::State::UNKNOWN;

            scenario.addCommand(ScenarioCommand(order, type, deviceId, state, delayMs));
        }

        m_scenarios[id] = scenario;
    }

    logInfo(QString("Loaded %1 scenarios").arg(m_scenarios.size()));
    return true;
}

// Register context trigger
bool ScenarioEngine::registerContextTrigger(const QString& context, const QString& scenarioId) {
    if (!m_scenarios.contains(scenarioId)) {
        logWarning(QString("Scenario not found: %1").arg(scenarioId));
        return false;
    }

    m_contextTriggers[context] = scenarioId;
    logInfo(QString("Context trigger registered: %1 -> %2").arg(context, scenarioId));

    return true;
}

// Get available scenarios
QStringList ScenarioEngine::getAvailableScenarios() const {
    return m_scenarios.keys();
}

// Get scenario status
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

// Get scenario description
QString ScenarioEngine::getScenarioDescription(const QString& scenarioId) const {
    if (m_scenarios.contains(scenarioId)) {
        return m_scenarios[scenarioId].description;
    }
    return "";
}

// D-Bus Method: GetAvailableScenarios
QStringList ScenarioEngine::GetAvailableScenarios() const {
    return getAvailableScenarios();
}

// D-Bus Method: GetEngineStatus
QString ScenarioEngine::GetEngineStatus() const {
    return getStatus();
}

// D-Bus Method: GetScenarioInfo
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

// D-Bus Method: ExecuteScenario
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

// D-Bus Method: GetScenarioStatus
QString ScenarioEngine::GetScenarioStatus(const QString& scenarioId) const {
    return getScenarioStatus(scenarioId);
}

// D-Bus Method: StopScenario
bool ScenarioEngine::StopScenario(const QString& scenarioId) {
    if (m_execution.scenarioId != scenarioId) {
        logWarning(QString("Scenario not currently running: %1").arg(scenarioId));
        return false;
    }

    stopExecution("User stopped");
    return true;
}

// D-Bus Method: ControlDevice
bool ScenarioEngine::ControlDevice(uchar deviceId, const QString& action) {
    logInfo(QString("Direct device control: id=%1, action=%2").arg(deviceId).arg(action));

    // TODO: Implement direct device control
    // This would map action string to state and call sendControlCommand

    return true;
}

// D-Bus Method: RegisterContextTrigger
bool ScenarioEngine::RegisterContextTrigger(const QString& context, const QString& scenarioId) {
    return registerContextTrigger(context, scenarioId);
}

// Private Methods

// Start scenario execution
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

    // Start first command
    executeNextCommand();

    return true;
}

// Stop scenario execution
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

// Set execution state
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

// Execute next command in scenario
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

    ScenarioCommand& cmd = scenario.commands[m_execution.currentCommandIndex];

    logDebug(QString("Executing command %1/%2 of scenario %3")
        .arg(m_execution.currentCommandIndex + 1)
        .arg(scenario.commands.size())
        .arg(m_execution.scenarioId));

    emit CommandExecuting(m_execution.scenarioId, m_execution.currentCommandIndex, 
                         QString("Command %1").arg(cmd.order));

    // Execute command
    if (!executeCommand(cmd)) {
        stopExecution("Command execution failed");
        return;
    }

    m_totalCommands++;
    m_execution.currentCommandIndex++;

    // Schedule next command with delay
    int delay = (m_execution.currentCommandIndex < scenario.commands.size()) 
        ? scenario.commands[m_execution.currentCommandIndex].delayMs 
        : 0;

    m_commandTimer.start(delay);
}

// Execute a single command
bool ScenarioEngine::executeCommand(const ScenarioCommand& cmd) {
    return sendControlCommand(cmd.deviceType, cmd.deviceId, cmd.targetState);
}

// Send control command via ProtocolRouter
bool ScenarioEngine::sendControlCommand(SPS::Device::Type deviceType,
                                        const QString& deviceId, SPS::Device::State state) {
    if (!m_routerInterface) {
        logError("ProtocolRouter not connected");
        return false;
    }

    // Map device type to UART command ID
    SPS::UART::CmdId cmdId;
    switch (deviceType) {
        case SPS::Device::Type::LIGHT:     cmdId = SPS::UART::CmdId::LIGHT_CONTROL; break;
        case SPS::Device::Type::CURTAIN:
        case SPS::Device::Type::SCREEN:    cmdId = SPS::UART::CmdId::CURTAIN_CONTROL; break;
        case SPS::Device::Type::PROJECTOR: cmdId = SPS::UART::CmdId::PROJECTOR_CONTROL; break;
        case SPS::Device::Type::AC:        cmdId = SPS::UART::CmdId::AC_CONTROL; break;
        case SPS::Device::Type::RELAY:     cmdId = SPS::UART::CmdId::LIGHT_CONTROL; break;
        default:
            logError(QString("Unknown device type: %1").arg(static_cast<int>(deviceType)));
            return false;
    }

    // Build payload
    QByteArray payload;

    // Devices needing a sub-ID byte before state
    if (deviceType != SPS::Device::Type::PROJECTOR) {
        bool ok = false;
        uchar id = deviceId.toUShort(&ok);
        payload.append(static_cast<char>(ok ? id : 0x01));
    }

    // Map state to control byte
    uchar stateByte;
    switch (state) {
        case SPS::Device::State::ON:
        case SPS::Device::State::OPEN:
        case SPS::Device::State::OPENING:
            stateByte = 0x01;
            break;
        case SPS::Device::State::OFF:
        case SPS::Device::State::CLOSED:
        case SPS::Device::State::CLOSING:
            stateByte = 0x00;
            break;
        default:
            logWarning(QString("Unhandled state %1, defaulting to OFF").arg(static_cast<int>(state)));
            stateByte = 0x00;
            break;
    }
    payload.append(static_cast<char>(stateByte));

    logDebug(QString("Sending command: type=%1, device=%2, state=%3, cmd=0x%4")
        .arg(static_cast<int>(deviceType))
        .arg(deviceId)
        .arg(static_cast<int>(state))
        .arg(static_cast<int>(cmdId), 2, 16, QChar('0')));

    // Call ProtocolRouter::SendCommand via D-Bus
    QDBusReply<bool> reply = m_routerInterface->call(
        "SendCommand", static_cast<uchar>(cmdId), payload);

    if (!reply.isValid()) {
        logError(QString("SendCommand D-Bus call failed: %1")
            .arg(reply.error().message()));
        return false;
    }

    return reply.value();
}

// Connect to ProtocolRouter via D-Bus
bool ScenarioEngine::connectToRouter() {
    m_routerInterface = new QDBusInterface("com.sps.router", "/com/sps/router", 
                                           "com.sps.router", 
                                           QDBusConnection::systemBus());

    if (!m_routerInterface->isValid()) {
        logWarning("ProtocolRouter D-Bus interface not available yet");
        return false;
    }

    logInfo("Connected to ProtocolRouter via D-Bus");
    return true;
}

// Call ProtocolRouter method
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

void ScenarioEngine::onScenarioStepCompleted() {
    executeNextCommand();
}

void ScenarioEngine::onCommandFailed(const QString& error) {
    logError(QString("Command failed: %1").arg(error));
    stopExecution(error);
}

void ScenarioEngine::onRouterCommandAck(uchar cmdId) {
    logDebug(QString("Router ACK: 0x%1").arg(cmdId, 2, 16, QChar('0')));
}

void ScenarioEngine::onRouterCommandError(uchar cmdId, uchar errorCode) {
    logError(QString("Router error: cmd=0x%1, code=0x%2")
        .arg(cmdId, 2, 16, QChar('0')).arg(errorCode, 2, 16, QChar('0')));
}

void ScenarioEngine::onPresenceDetected(bool present) {
    logDebug(QString("Presence: %1").arg(present ? "Yes" : "No"));

    if (present) {
        onContextEvent("presence_detected");
    }
}
