#ifndef APP_D_BUS_CLIENT_H
#define APP_D_BUS_CLIENT_H

#include <QObject>
#include <QString>
#include <QDBusInterface>
#include <QByteArray>
#include <QStringList>
#include <QJsonObject>
#include <QVariantList>

/** Manages all D-Bus communication for the HMI application. */
class AppDbusCli : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString authStatus READ getAuthStatus NOTIFY authStatusChanged)
    Q_PROPERTY(QString currentScenario READ getCurrentScenario NOTIFY scenarioChanged)
    Q_PROPERTY(QString mqttStatus READ getMqttStatus NOTIFY mqttStatusChanged)
    Q_PROPERTY(bool networkConnected READ isNetworkConnected NOTIFY networkStatusChanged)
    Q_PROPERTY(QStringList availableScenarios READ getAvailableScenarios NOTIFY scenariosUpdated)
    Q_PROPERTY(bool pcControlEnabled READ isPcControlEnabled NOTIFY pcControlConfigChanged)
    Q_PROPERTY(QString pcMacAddress READ getPcMacAddress NOTIFY pcControlConfigChanged)

public:
    /** Constructor. Initializes D-Bus interface pointers and reads PC control config from environment. */
    explicit AppDbusCli(QObject* parent = nullptr);
    /** Destructor. Deletes all D-Bus interface pointers. */
    ~AppDbusCli();

    /** Connects to all SPS D-Bus services and sets up signal handlers. */
    bool initialize();
    /** Scans the D-Bus system bus for available SPS services. */
    bool discoverServices();
    /** Checks whether all required D-Bus service interfaces are valid. */
    bool verifyServiceAvailability();

    /** Sends an unlock request to the authentication service with optional RFID data. */
    Q_INVOKABLE bool unlockScreen(const QString& rfidData = QString());
    /** Sends a lock request to the authentication service. */
    Q_INVOKABLE bool lockScreen();
    /** Returns the name of the currently authenticated lecturer, or an empty string. */
    Q_INVOKABLE QString getAuthenticatedLecturer();
    
    /** Sends a request to start the given scenario. */
    Q_INVOKABLE bool executeScenario(const QString& scenarioId);
    /** Sends a request to stop a running scenario. */
    Q_INVOKABLE bool stopScenario(const QString& scenarioId);
    /** Queries the current status of a scenario from the engine. */
    Q_INVOKABLE QString getScenarioStatus(const QString& scenarioId);
    /** Retrieves the list of available scenarios and emits scenariosUpdated. */
    Q_INVOKABLE QStringList getAvailableScenarios();

    /** Sends a typed device command via the protocol router. */
    Q_INVOKABLE bool sendDeviceCommand(uchar commandId, const QString& action);
    /** Controls a classroom device using UI-level names instead of raw UART command IDs. */
    Q_INVOKABLE bool controlClassroomDevice(const QString& deviceKey, const QString& action);
    /** Queries the status of a device from the protocol router. */
    Q_INVOKABLE uint getDeviceStatus(uchar deviceId);
    /** Converts a scenario ID into a touch-friendly display name. */
    Q_INVOKABLE QString getScenarioDisplayName(const QString& scenarioId) const;

    /** Connects to an MQTT broker via the network manager service. */
    Q_INVOKABLE bool connectToMqtt(const QString& broker, int port);
    /** Disconnects from the MQTT broker. */
    Q_INVOKABLE bool disconnectFromMqtt();
    /** Sends a Wake-on-LAN packet to the given MAC address (uses config default if empty). */
    Q_INVOKABLE bool sendWoL(const QString& macAddress);
    /** Returns the current MQTT connection status. */
    Q_INVOKABLE QString getMqttStatus();
    /** Returns the current network status. */
    Q_INVOKABLE QString getNetworkStatus();
    /** Refreshes PC control config from environment and D-Bus; emits pcControlConfigChanged on change. */
    Q_INVOKABLE bool refreshPcControlConfig();

    /** Returns the cached authentication status string. */
    QString getAuthStatus() const;
    /** Returns the ID of the currently executing scenario. */
    QString getCurrentScenario() const;
    /** Returns the cached MQTT status string. */
    QString getMqttStatus() const;
    /** Returns whether generic network connectivity is currently online. */
    bool isNetworkConnected() const;
    /** Returns whether PC Wake-on-LAN control is enabled. */
    bool isPcControlEnabled() const;
    /** Returns the configured PC MAC address for Wake-on-LAN. */
    QString getPcMacAddress() const;

signals:
    /** Emitted when the authentication status changes. */
    void authStatusChanged(const QString& status);
    /** Emitted when the currently active scenario changes. */
    void scenarioChanged(const QString& scenarioId);
    /** Emitted when the MQTT connection status changes. */
    void mqttStatusChanged(const QString& status);
    /** Emitted when the list of available scenarios is updated. */
    void scenariosUpdated(const QStringList& scenarios);
    /** Emitted when the PC control configuration changes. */
    void pcControlConfigChanged();

    /** Emitted when a lecturer is successfully authenticated. */
    void lecturerAuthenticated(const QString& lecturerName, qlonglong timestamp);
    /** Emitted when a scenario starts execution. */
    void scenarioStarted(const QString& scenarioId);
    /** Emitted when a scenario completes execution. */
    void scenarioCompleted(const QString& scenarioId);
    /** Emitted when a scenario encounters an error. */
    void scenarioError(const QString& scenarioId, const QString& error);
    /** Emitted during scenario execution to report command progress. */
    void commandExecuting(const QString& scenarioId, int commandIndex, const QString& description);
    /** Emitted when a context trigger activates a scenario. */
    void contextTriggered(const QString& context, const QString& scenarioId);

    /** Emitted when a device's status changes. */
    void deviceStatusChanged(uchar deviceId, uint status);
    /** Emitted when a command is acknowledged by a device. */
    void commandAck(uchar cmdId);
    /** Emitted when a command fails with an error code. */
    void commandError(uchar cmdId, uchar errorCode);

    /** Emitted when the MQTT connection is established. */
    void mqttConnected();
    /** Emitted when the MQTT connection is lost. */
    void mqttDisconnected(const QString& reason);
    /** Emitted when the network connectivity status changes. */
    void networkStatusChanged(bool connected);
    /** Emitted when a command is received over MQTT. */
    void commandReceived(const QString& command, const QJsonObject& payload);

private slots:
    /** Handles AuthStatusChanged signal from the authentication D-Bus service. */
    void onAuthStatusChanged(int status);
    /** Handles LecturerAuthenticated signal from the authentication D-Bus service. */
    void onLecturerAuthenticated(const QString& lecturerName, qlonglong timestamp);
    /** Handles ScenarioStarted signal from the scenario engine D-Bus service. */
    void onScenarioStarted(const QString& scenarioId);
    /** Handles ScenarioCompleted signal from the scenario engine D-Bus service. */
    void onScenarioCompleted(const QString& scenarioId);
    /** Handles ScenarioError signal from the scenario engine D-Bus service. */
    void onScenarioError(const QString& scenarioId, const QString& error);
    /** Handles ContextTriggered signal from the scenario engine D-Bus service. */
    void onContextTriggered(const QString& context, const QString& scenarioId);
    /** Handles MqttConnected signal from the network manager D-Bus service. */
    void onMqttConnected();
    /** Handles MqttDisconnected signal from the network manager D-Bus service. */
    void onMqttDisconnected(const QString& reason);
    /** Handles NetworkStatusChanged signal from the network manager D-Bus service. */
    void onNetworkStatusChanged(bool connected);
    /** Handles CommandReceived signal from the network manager D-Bus service. */
    void onCommandReceived(const QString& command, const QJsonObject& payload);
    /** Handles CommandAcknowledged signal from the protocol router D-Bus service. */
    void onCommandAck(uchar cmdId);
    /** Handles CommandError signal from the protocol router D-Bus service. */
    void onCommandError(uchar cmdId, uchar errorCode);

private:
    /** Connects to a D-Bus service and stores the interface pointer. */
    bool connectToService(QDBusInterface*& iface, const QString& service, const QString& path);
    /** Connects all D-Bus service signals to their corresponding private slots. */
    bool setupSignalConnections();
    /** Calls a D-Bus method with up to two optional arguments and returns the result. */
    QVariant callMethod(QDBusInterface* iface, const QString& method, 
                       const QVariant& arg1 = QVariant(), const QVariant& arg2 = QVariant());
    /** Calls a typed boolean command on the ProtocolRouter service. */
    bool callRouterBool(const QString& method, const QVariantList& args = QVariantList());

    // D-Bus service interfaces
    QDBusInterface* m_authInterface;
    QDBusInterface* m_routerInterface;
    QDBusInterface* m_engineInterface;
    QDBusInterface* m_netMgrInterface;

    // Cached state
    QString m_authStatus;
    QString m_currentScenario;
    QString m_mqttStatus;
    bool m_networkConnected;
    QStringList m_availableScenarios;
    bool m_pcControlEnabled;
    QString m_pcMacAddress;
};

#endif // APP_D_BUS_CLIENT_H
