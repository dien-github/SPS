#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QTimer>
#include <QUdpSocket>
#include <QHostAddress>
#include <QJsonObject>
#include "../common/sps_service_base.h"
#include "mqtt_client.h"

// Network Manager Service
// Handles MQTT communication with Web Dashboard
// Manages network connectivity and WoL (Wake-on-LAN) broadcasting
class NetworkManager : public SpsServiceBase {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.sps.netmgr")

public:
    explicit NetworkManager(QObject* parent = nullptr);
    ~NetworkManager();

    // Service lifecycle
    bool initialize() override;
    void shutdown() override;
    QString getStatus() const override;

    // MQTT integration
    bool connectToMqtt(const QString& broker, int port = 1883);
    bool subscribeTopic(const QString& topic, int qos = 1);
    bool publishStatus(const QString& roomId, const QString& deviceStatus);
    bool publishEvent(const QString& roomId, const QString& eventType, const QJsonObject& eventData);
    bool publishOtaProgress(const QString& roomId, int percentage, const QString& status);

    // Network utilities
    bool broadcastWoL(const QString& macAddress, const QString& broadcastAddr = "255.255.255.255", int port = 9);
    bool checkNetworkConnectivity(const QString& host = "8.8.8.8", int timeout = 5000);

    // Getters
    QString getMqttStatus() const;
    QString getMqttBroker() const;
    int getMqttPort() const;
    bool isNetworkConnected() const;

signals:
    // D-Bus signals
    void MqttConnected();
    void MqttDisconnected(const QString& reason);
    void MqttMessageReceived(const QString& topic, const QString& message);
    void MqttError(const QString& errorMessage);
    void NetworkStatusChanged(bool connected);
    void DeviceStatusUpdated(const QString& deviceId, const QString& status);
    void CommandReceived(const QString& command, const QJsonObject& payload);
    void SyncDataReceived(const QJsonObject& lecturerData);
    void OtaCommandReceived(const QString& firmwareUrl);

public slots:
    // D-Bus methods
    Q_SCRIPTABLE QString GetMqttStatus() const;
    Q_SCRIPTABLE QString GetNetworkStatus() const;
    Q_SCRIPTABLE bool PublishEvent(const QString& topic, const QByteArray& payload, int qos);
    Q_SCRIPTABLE QString GetRoomId() const;
    Q_SCRIPTABLE bool SetRoomId(const QString& roomId);
    Q_SCRIPTABLE bool SendWakeOnLAN(const QString& macAddress, const QString& broadcastAddr);
    Q_SCRIPTABLE bool SyncLecturerList();
    Q_SCRIPTABLE QString GetConnectionDetails(QString& gateway, QString& dns) const;
    Q_SCRIPTABLE bool RequestOTAUpdate(const QString& firmwareVersion);

    // Compatibility methods used by the current HMI client but not declared in com.sps.netmgr.xml.
    Q_SCRIPTABLE bool ConnectToMqtt(const QString& broker, int port);
    Q_SCRIPTABLE bool DisconnectFromMqtt();
    Q_SCRIPTABLE bool PublishDeviceStatus(const QString& roomId, const QString& deviceId, const QString& status);
    Q_SCRIPTABLE bool PublishEvent(const QString& roomId, const QString& eventType, const QString& eventJson);
    Q_SCRIPTABLE bool SendWoL(const QString& macAddress);

    // Signal handlers from MQTT
    void onMqttConnected();
    void onMqttDisconnected();
    void onMqttMessageReceived(const QString& topic, const QByteArray& message);
    void onMqttError(const QString& error);

protected slots:
    void checkNetworkStatus();
    void processTopicMessage(const QString& topic, const QByteArray& message);
    void onMqttReconnectTimeout();
    void publishHeartbeat();
    void onAuthStatusChanged(const QString& status);

private:
    // MQTT message handlers
    void handleProjectorCommand(const QString& roomId, const QJsonObject& data);
    void handleRelayCommand(const QString& roomId, const QJsonObject& data);
    void handleAcCommand(const QString& roomId, const QJsonObject& data);
    void handleSyncCommand(const QString& roomId, const QJsonObject& syncData);
    void handleOtaCommand(const QString& roomId, const QJsonObject& otaData);

    // Configuration
    QString m_mqttBroker;
    int m_mqttPort;
    QString m_roomId;
    QString m_deviceId;
    QString m_configPath;

    // MQTT client
    MqttClient* m_mqttClient;
    bool m_mqttConnected;
    int m_mqttReconnectCount;

    // Network monitoring
    QTimer m_networkCheckTimer;
    QTimer m_heartbeatTimer;
    QTimer m_reconnectTimer;
    bool m_networkConnected;
    QHostAddress m_lastGateway;

    // WoL broadcaster
    QUdpSocket* m_wolSocket;

    // Statistics
    int m_messagesPublished;
    int m_messagesReceived;
    int m_mqttErrors;
    int m_networkStateChanges;
    QDateTime m_lastMqttConnection;
    QDateTime m_lastNetworkCheck;

    // Topic subscriptions
    QStringList m_subscribedTopics;
};

#endif // NETWORK_MANAGER_H
