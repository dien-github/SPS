#ifndef APP_D_BUS_CLIENT_H
#define APP_D_BUS_CLIENT_H

#include <QObject>
#include <QString>
#include <QDBusInterface>
#include <QStringList>
#include <QJsonObject>

// D-Bus Client Manager for appHmi
// Provides synchronized access to all SPS services via D-Bus
class AppDbusCli : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString authStatus READ getAuthStatus NOTIFY authStatusChanged)
    Q_PROPERTY(QString currentScenario READ getCurrentScenario NOTIFY scenarioChanged)
    Q_PROPERTY(QString mqttStatus READ getMqttStatus NOTIFY mqttStatusChanged)
    Q_PROPERTY(QStringList availableScenarios READ getAvailableScenarios NOTIFY scenariosUpdated)

public:
    explicit AppDbusCli(QObject* parent = nullptr);
    ~AppDbusCli();

    // Service discovery & initialization
    bool initialize();
    bool discoverServices();
    bool verifyServiceAvailability();

    // Authentication service methods
    Q_INVOKABLE bool unlockScreen();
    Q_INVOKABLE bool lockScreen();
    Q_INVOKABLE QString getAuthenticatedLecturer();
    
    // Scenario engine methods
    Q_INVOKABLE bool executeScenario(const QString& scenarioId);
    Q_INVOKABLE bool stopScenario(const QString& scenarioId);
    Q_INVOKABLE QString getScenarioStatus(const QString& scenarioId);
    Q_INVOKABLE QStringList getAvailableScenarios();

    // Device control methods
    Q_INVOKABLE bool sendDeviceCommand(uchar deviceId, const QString& action);
    Q_INVOKABLE uint getDeviceStatus(uchar deviceId);

    // Network manager methods
    Q_INVOKABLE bool connectToMqtt(const QString& broker, int port);
    Q_INVOKABLE bool disconnectFromMqtt();
    Q_INVOKABLE bool sendWoL(const QString& macAddress);
    Q_INVOKABLE QString getMqttStatus();
    Q_INVOKABLE QString getNetworkStatus();

    // Getters
    QString getAuthStatus() const;
    QString getCurrentScenario() const;
    QString getMqttStatus() const;

signals:
    // Status changes
    void authStatusChanged(const QString& status);
    void scenarioChanged(const QString& scenarioId);
    void mqttStatusChanged(const QString& status);
    void scenariosUpdated(const QStringList& scenarios);

    // Service events
    void lecturerAuthenticated(const QString& lecturerName, qlonglong timestamp);
    void scenarioStarted(const QString& scenarioId);
    void scenarioCompleted(const QString& scenarioId);
    void scenarioError(const QString& scenarioId, const QString& error);
    void commandExecuting(const QString& scenarioId, int commandIndex, const QString& description);
    void contextTriggered(const QString& context, const QString& scenarioId);

    // Device events
    void deviceStatusChanged(uchar deviceId, uint status);
    void commandAck(uchar cmdId);
    void commandError(uchar cmdId, uchar errorCode);

    // Network events
    void mqttConnected();
    void mqttDisconnected(const QString& reason);
    void networkStatusChanged(bool connected);
    void commandReceived(const QString& command, const QJsonObject& payload);

private slots:
    // Signal handlers from services
    void onAuthStatusChanged(int status);
    void onScenarioStarted(const QString& scenarioId);
    void onScenarioCompleted(const QString& scenarioId);
    void onScenarioError(const QString& scenarioId, const QString& error);
    void onContextTriggered(const QString& context, const QString& scenarioId);
    void onMqttConnected();
    void onMqttDisconnected(const QString& reason);
    void onNetworkStatusChanged(bool connected);
    void onCommandReceived(const QString& command, const QJsonObject& payload);
    void onCommandAck(uchar cmdId);
    void onCommandError(uchar cmdId, uchar errorCode);

private:
    // Helper methods
    bool connectToService(QDBusInterface*& iface, const QString& service, const QString& path);
    bool setupSignalConnections();
    QVariant callMethod(QDBusInterface* iface, const QString& method, 
                       const QVariant& arg1 = QVariant(), const QVariant& arg2 = QVariant());

    // D-Bus service interfaces
    QDBusInterface* m_authInterface;
    QDBusInterface* m_routerInterface;
    QDBusInterface* m_engineInterface;
    QDBusInterface* m_netMgrInterface;

    // Cached state
    QString m_authStatus;
    QString m_currentScenario;
    QString m_mqttStatus;
    QStringList m_availableScenarios;
};

#endif // APP_D_BUS_CLIENT_H
