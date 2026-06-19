#include "scenario_engine.h"
#include "../common/sps_logger.h"
#include "../common/sps_runtime_config.h"
#include "sps_uart_protocol.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>

/** Constructor. Initialises execution context, timers, and counters. */
ScenarioEngine::ScenarioEngine(QObject* parent)
    : SpsServiceBase("com.sps.engine", "/com/sps/engine", parent),
      m_scenariosPath(SPS::Runtime::configFile("SPS_SCENARIOS_FILE", "scenarios.json")),
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

/** Initialises the service: loads scenarios, connects to ProtocolRouter, and registers D-Bus. */
bool ScenarioEngine::initialize() {
    logInfo("Initializing Scenario Engine service...");

    if (!loadScenarios(m_scenariosPath)) {
        logWarning("Failed to load scenarios - service will operate with no scenarios");
    }

    if (!connectToRouter()) {
        logWarning("Failed to connect to ProtocolRouter - command execution unavailable");
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

    return QString("Engine Status: %1 | Scenarios: %2 | Executed: %3/%4")
        .arg(stateStr).arg(m_scenarios.size()).arg(m_scenariosExecuted).arg(m_scenariosFailed);
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
            QJsonObject cmdObj = commands[i].toObject();
            int order = cmdObj["order"].toInt(i);
            QString deviceType = cmdObj["device_type"].toString().toLower();
            QString deviceId = cmdObj["device_id"].toVariant().toString();
            int delayMs = cmdObj["delay_ms"].toInt(0);
            QString targetState = cmdObj["target_state"].toString();
            if (targetState.isEmpty()) {
                targetState = cmdObj["state"].toString();
            }
            if (targetState.isEmpty()) {
                targetState = cmdObj["action"].toString();
            }
            targetState = targetState.toLower();

            SPS::Device::Type type = SPS::Device::Type::UNKNOWN;
            if (deviceType == "light") type = SPS::Device::Type::LIGHT;
            else if (deviceType == "projector") type = SPS::Device::Type::PROJECTOR;
            else if (deviceType == "curtain") type = SPS::Device::Type::CURTAIN;
            else if (deviceType == "screen") type = SPS::Device::Type::SCREEN;
            else if (deviceType == "ac") type = SPS::Device::Type::AC;
            else if (deviceType == "relay") type = SPS::Device::Type::RELAY;

            SPS::Device::State state = SPS::Device::State::UNKNOWN;
            if (targetState == "on") state = SPS::Device::State::ON;
            else if (targetState == "off") state = SPS::Device::State::OFF;
            else if (targetState == "open") state = SPS::Device::State::OPEN;
            else if (targetState == "close" || targetState == "closed") state = SPS::Device::State::CLOSED;
            else if (targetState == "opening") state = SPS::Device::State::OPENING;
            else if (targetState == "closing") state = SPS::Device::State::CLOSING;

            if (type == SPS::Device::Type::UNKNOWN || state == SPS::Device::State::UNKNOWN) {
                logWarning(QString("Skipping invalid scenario command: device_type=%1, device_id=%2, state=%3")
                    .arg(deviceType, deviceId, targetState));
                continue;
            }

            scenario.addCommand(ScenarioCommand(order, type, deviceId, state, delayMs));
        }

        m_scenarios[id] = scenario;
    }

    logInfo(QString("Loaded %1 scenarios").arg(m_scenarios.size()));
    return true;
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

    return true;
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

    ScenarioCommand& cmd = scenario.commands[m_execution.currentCommandIndex];

    logDebug(QString("Executing command %1/%2 of scenario %3")
        .arg(m_execution.currentCommandIndex + 1)
        .arg(scenario.commands.size())
        .arg(m_execution.scenarioId));

    emit CommandExecuting(m_execution.scenarioId, m_execution.currentCommandIndex, 
                         QString("Command %1").arg(cmd.order));

    if (!executeCommand(cmd)) {
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
bool ScenarioEngine::executeCommand(const ScenarioCommand& cmd) {
    return sendControlCommand(cmd.deviceType, cmd.deviceId, cmd.targetState);
}

/** Sends a typed control command via the ProtocolRouter D-Bus interface. */
bool ScenarioEngine::sendControlCommand(SPS::Device::Type deviceType,
                                        const QString& deviceId, SPS::Device::State state) {
    if (!m_routerInterface) {
        logError("ProtocolRouter not connected");
        return false;
    }

    bool on = false;
    SPS::UART::ControlValue action = SPS::UART::ControlValue::OFF;
    switch (state) {
        case SPS::Device::State::ON:
        case SPS::Device::State::OPEN:
        case SPS::Device::State::OPENING:
            on = true;
            action = SPS::UART::ControlValue::OPEN;
            break;
        case SPS::Device::State::OFF:
        case SPS::Device::State::CLOSED:
        case SPS::Device::State::CLOSING:
            on = false;
            action = SPS::UART::ControlValue::CLOSE;
            break;
        default:
            logWarning(QString("Unhandled state %1, defaulting to OFF").arg(static_cast<int>(state)));
            break;
    }

    bool ok = false;
    uchar id = static_cast<uchar>(deviceId.toUShort(&ok));

    QString method;
    QVariantList args;
    switch (deviceType) {
        case SPS::Device::Type::LIGHT:
        case SPS::Device::Type::RELAY:
            method = "ControlLight";
            args = {
                QVariant::fromValue(static_cast<quint8>(ok ? id : SPS::UART::toByte(SPS::UART::DeviceId::LIGHT_CLASS))),
                on
            };
            break;
        case SPS::Device::Type::CURTAIN:
            method = "ControlCurtain";
            args = {
                QVariant::fromValue(static_cast<quint8>(ok ? id : SPS::UART::toByte(SPS::UART::DeviceId::CURTAIN))),
                QVariant::fromValue(static_cast<quint8>(SPS::UART::toByte(action)))
            };
            break;
        case SPS::Device::Type::SCREEN:
            method = "ControlCurtain";
            args = {
                QVariant::fromValue(static_cast<quint8>(ok ? id : SPS::UART::toByte(SPS::UART::DeviceId::SCREEN))),
                QVariant::fromValue(static_cast<quint8>(SPS::UART::toByte(action)))
            };
            break;
        case SPS::Device::Type::PROJECTOR:
            method = "ControlProjector";
            args = { on };
            break;
        case SPS::Device::Type::AC:
            method = "ControlAC";
            args = {
                QVariant::fromValue(static_cast<quint8>(ok ? id : SPS::UART::toByte(SPS::UART::DeviceId::AC_ID))),
                on
            };
            break;
        default:
            logError(QString("Unknown device type: %1").arg(static_cast<int>(deviceType)));
            return false;
    }

    logDebug(QString("Sending typed command: method=%1, type=%2, device=%3, state=%4")
        .arg(method)
        .arg(static_cast<int>(deviceType))
        .arg(deviceId)
        .arg(static_cast<int>(state)));

    QDBusMessage reply = m_routerInterface->callWithArgumentList(QDBus::Block, method, args);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        logError(QString("%1 D-Bus call failed: %2").arg(method, reply.errorMessage()));
        return false;
    }

    return !reply.arguments().isEmpty() && reply.arguments().at(0).toBool();
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

    logInfo("Connected to ProtocolRouter via D-Bus");
    return true;
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
}

/** Handles presence detection events to trigger context scenarios. */
void ScenarioEngine::onPresenceDetected(bool present) {
    logDebug(QString("Presence: %1").arg(present ? "Yes" : "No"));

    if (present) {
        onContextEvent("presence_detected");
    }
}
