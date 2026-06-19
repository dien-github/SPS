#ifndef SCENARIO_ENGINE_H
#define SCENARIO_ENGINE_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QTimer>
#include <QQueue>
#include <QMutex>
#include <QByteArray>
#include <QDBusInterface>
#include "../common/sps_service_base.h"
#include "../common/sps_device_models.h"
#include "device_map.h"

/** Manages execution of automation scenarios. Coordinates with ProtocolRouter for device control. */
class ScenarioEngine : public SpsServiceBase {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.sps.engine")

public:
    /** Creates the scenario engine with optional parent. Initialises timers and execution state. */
    explicit ScenarioEngine(QObject* parent = nullptr);
    /** Destructor. Calls shutdown to release all resources. */
    ~ScenarioEngine();

    // Service lifecycle
    /** Initialises the service: loads scenarios, connects to ProtocolRouter, and registers D-Bus. */
    bool initialize() override;
    /** Stops execution, clears scenarios, and releases D-Bus interfaces. */
    void shutdown() override;
    /** Returns a human-readable status string with engine state and scenario counts. */
    QString getStatus() const override;

    // Scenario management
    /** Loads scenarios from a JSON file. Falls back to sample scenarios if the file is missing. */
    bool loadScenarios(const QString& scenariosPath = "/opt/sps/config/scenarios.json");
    /** Loads device mapping from a JSON file. Falls back to demo map if file is missing. */
    bool loadDeviceMap(const QString& deviceMapPath = "/opt/sps/config/devices.json");
    /** Registers a context-to-scenario mapping so a context event auto-triggers a scenario. */
    bool registerContextTrigger(const QString& context, const QString& scenarioId);

    // Getters
    /** Returns a list of all available scenario IDs. */
    QStringList getAvailableScenarios() const;
    /** Returns the current execution status (RUNNING, PAUSED, etc.) for the given scenario. */
    QString getScenarioStatus(const QString& scenarioId) const;
    /** Returns the description text of the specified scenario. */
    QString getScenarioDescription(const QString& scenarioId) const;

signals:
    /** Emitted when a scenario begins execution. */
    void ScenarioStarted(const QString& scenarioId);
    /** Emitted when a scenario completes successfully. */
    void ScenarioCompleted(const QString& scenarioId);
    /** Emitted when a scenario encounters an error. */
    void ScenarioError(const QString& scenarioId, const QString& errorMessage);
    /** Emitted when a specific command within a scenario starts executing. */
    void CommandExecuting(const QString& scenarioId, int commandIndex, const QString& commandDescription);
    /** Emitted when a context event triggers a scenario automatically. */
    void ContextTriggered(const QString& context, const QString& scenarioId);

public slots:
    /** D-Bus callable. Returns a list of all available scenario IDs. */
    Q_SCRIPTABLE QStringList GetAvailableScenarios() const;
    /** D-Bus callable. Starts execution of the specified scenario. */
    Q_SCRIPTABLE bool ExecuteScenario(const QString& scenarioId);
    /** D-Bus callable. Returns the execution status of the given scenario. */
    Q_SCRIPTABLE QString GetScenarioStatus(const QString& scenarioId) const;
    /** D-Bus callable. Stops the currently running scenario. */
    Q_SCRIPTABLE bool StopScenario(const QString& scenarioId);
    /** D-Bus callable. Sends a direct control command to a device. */
    Q_SCRIPTABLE bool ControlDevice(uchar deviceId, const QString& action);
    /** D-Bus callable. Registers a context trigger for automatic scenario execution. */
    Q_SCRIPTABLE bool RegisterContextTrigger(const QString& context, const QString& scenarioId);
    /** D-Bus callable. Returns the engine status string. */
    Q_SCRIPTABLE QString GetEngineStatus() const;
    /** D-Bus callable. Returns scenario info including description and command count. */
    Q_SCRIPTABLE QString GetScenarioInfo(const QString& scenarioId, QString& description, int& commandCount) const;

    /** Handles a context event and triggers the associated scenario if registered. */
    void onContextEvent(const QString& context);
    /** Called when a scenario step completes; triggers the next command. */
    void onScenarioStepCompleted();
    /** Handles a command failure by stopping the scenario with an error. */
    void onCommandFailed(const QString& error);

protected slots:
    /** Executes the next pending command in the current scenario. */
    void executeNextCommand();
    /** Handles a command acknowledgment from the ProtocolRouter. */
    void onRouterCommandAck(uchar cmdId);
    /** Handles a command error from the ProtocolRouter. */
    void onRouterCommandError(uchar cmdId, uchar errorCode);
    /** Handles presence detection events to trigger context scenarios. */
    void onPresenceDetected(bool present);
    /** Handles a remote command received by svcNetworkManager over MQTT. */
    void onRemoteCommandReceived(const QString& commandType, const QByteArray& payload);

private:
    // Execution state
    enum ExecutionState {
        IDLE = 0,
        RUNNING = 1,
        PAUSED = 2,
        COMPLETED = 3,
        ERROR = 4
    };

    // Execution context
    struct ExecutionContext {
        QString scenarioId;
        ExecutionState state;
        int currentCommandIndex;
        QDateTime startTime;
        QString lastError;
    };

    /** Starts execution of the specified scenario. Returns false if scenario is not found. */
    bool startExecution(const QString& scenarioId);
    /** Stops the currently running scenario with the given reason. */
    void stopExecution(const QString& reason = "");
    /** Updates the execution state and emits relevant signals. */
    void setExecutionState(ExecutionState newState);

    /** Sends a single command to a device through the ProtocolRouter. */
    bool executeCommand(const ScenarioCommand& cmd);
    /** Sends a typed control command via the ProtocolRouter D-Bus interface. */
    bool sendControlCommand(SPS::Device::Type deviceType, const QString& deviceId, SPS::Device::State state);

    /** Creates and validates the D-Bus interface to the ProtocolRouter service. */
    bool connectToRouter();
    /** Subscribes to NetworkManager's remote command D-Bus signal. */
    bool connectToNetworkManager();
    /** Calls a method on the ProtocolRouter D-Bus interface with optional arguments. */
    bool callRouterMethod(const QString& method, const QVariant& arg1 = QVariant(), 
                         const QVariant& arg2 = QVariant());

    // Configuration
    QString m_scenariosPath;
    QString m_deviceMapPath;

    // Device registry
    DeviceMap m_deviceMap;

    // Scenarios storage (scenarioId -> Scenario)
    QMap<QString, Scenario> m_scenarios;

    // Context triggers (context -> scenarioId)
    QMap<QString, QString> m_contextTriggers;

    // Execution state
    ExecutionContext m_execution;
    QTimer m_commandTimer;
    QTimer m_autoStartTimer;

    // D-Bus interfaces
    QDBusInterface* m_routerInterface;
    QDBusInterface* m_authInterface;

    // Statistics
    int m_scenariosExecuted;
    int m_scenariosFailed;
    int m_totalCommands;
    int m_autoTriggeredScenarios;
};

#endif // SCENARIO_ENGINE_H
