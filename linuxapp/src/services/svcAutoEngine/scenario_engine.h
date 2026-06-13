#ifndef SCENARIO_ENGINE_H
#define SCENARIO_ENGINE_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QTimer>
#include <QQueue>
#include <QMutex>
#include <QDBusInterface>
#include "../common/sps_service_base.h"
#include "../common/sps_device_models.h"

// Scenario Execution Engine
// Manages execution of automation scenarios
// Coordinates with ProtocolRouter for device control
class ScenarioEngine : public SpsServiceBase {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.sps.engine")

public:
    explicit ScenarioEngine(QObject* parent = nullptr);
    ~ScenarioEngine();

    // Service lifecycle
    bool initialize() override;
    void shutdown() override;
    QString getStatus() const override;

    // Scenario management
    bool loadScenarios(const QString& scenariosPath = "/opt/sps/config/scenarios.json");
    bool registerContextTrigger(const QString& context, const QString& scenarioId);

    // Getters
    QStringList getAvailableScenarios() const;
    QString getScenarioStatus(const QString& scenarioId) const;
    QString getScenarioDescription(const QString& scenarioId) const;

signals:
    // D-Bus signals
    void ScenarioStarted(const QString& scenarioId);
    void ScenarioCompleted(const QString& scenarioId);
    void ScenarioError(const QString& scenarioId, const QString& errorMessage);
    void CommandExecuting(const QString& scenarioId, int commandIndex, const QString& commandDescription);
    void ContextTriggered(const QString& context, const QString& scenarioId);

public slots:
    // D-Bus methods
    Q_SCRIPTABLE QStringList GetAvailableScenarios() const;
    Q_SCRIPTABLE bool ExecuteScenario(const QString& scenarioId);
    Q_SCRIPTABLE QString GetScenarioStatus(const QString& scenarioId) const;
    Q_SCRIPTABLE bool StopScenario(const QString& scenarioId);
    Q_SCRIPTABLE bool ControlDevice(uchar deviceId, const QString& action);
    Q_SCRIPTABLE bool RegisterContextTrigger(const QString& context, const QString& scenarioId);
    Q_SCRIPTABLE QString GetEngineStatus() const;
    Q_SCRIPTABLE QString GetScenarioInfo(const QString& scenarioId, QString& description, int& commandCount) const;

    // Signal handlers
    void onContextEvent(const QString& context);
    void onScenarioStepCompleted();
    void onCommandFailed(const QString& error);

protected slots:
    void executeNextCommand();
    void onRouterCommandAck(uchar cmdId);
    void onRouterCommandError(uchar cmdId, uchar errorCode);
    void onPresenceDetected(bool present);

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

    // Scenario execution
    bool startExecution(const QString& scenarioId);
    void stopExecution(const QString& reason = "");
    void setExecutionState(ExecutionState newState);

    // Command execution
    bool executeCommand(const ScenarioCommand& cmd);
    bool sendControlCommand(SPS::Device::Type deviceType, const QString& deviceId, SPS::Device::State state);

    // D-Bus interface with ProtocolRouter
    bool connectToRouter();
    bool callRouterMethod(const QString& method, const QVariant& arg1 = QVariant(), 
                         const QVariant& arg2 = QVariant());

    // Configuration
    QString m_scenariosPath;

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
