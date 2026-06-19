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

/** Manages MQTT, network connectivity, and Wake-on-LAN for the SPS system. */
class NetworkManager : public SpsServiceBase {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.sps.netmgr")

public:
    /** Creates the Network Manager service. */
    explicit NetworkManager(QObject* parent = nullptr);
    /** Destroys the Network Manager service. */
    ~NetworkManager();

    /** Initializes MQTT client, timers, and loads configuration. */
    bool initialize() override;
    /** Stops timers, disconnects MQTT, and shuts down the service. */
    void shutdown() override;
    /** Returns a human-readable summary of the current service status. */
    QString getStatus() const override;

    /** Connects to the MQTT broker at the given host and port. */
    bool connectToMqtt(const QString& broker, int port = 1883);
    /** Subscribes to a topic with the specified QoS level. */
    bool subscribeTopic(const QString& topic, int qos = 1);
    /** Publishes device status for a room to the MQTT status topic. */
    bool publishStatus(const QString& roomId, const QString& deviceStatus);
    /** Publishes a JSON event for a room to the MQTT event topic. */
    bool publishEvent(const QString& roomId, const QString& eventType, const QJsonObject& eventData);
    /** Publishes OTA update progress percentage and status for a room. */
    bool publishOtaProgress(const QString& roomId, int percentage, const QString& status);

    /** Broadcasts a Wake-on-LAN magic packet to wake a remote PC. */
    bool broadcastWoL(const QString& macAddress, const QString& broadcastAddr = "255.255.255.255", int port = 9);
    /** Pings a remote host to check basic network connectivity. */
    bool checkNetworkConnectivity(const QString& host = "8.8.8.8", int timeout = 5000);

    /** Returns "CONNECTED" or "DISCONNECTED". */
    QString getMqttStatus() const;
    /** Returns the configured MQTT broker hostname. */
    QString getMqttBroker() const;
    /** Returns the configured MQTT broker port. */
    int getMqttPort() const;
    /** Returns true if the network is currently reachable. */
    bool isNetworkConnected() const;
    /** Returns true if PC control (WoL) is enabled in config. */
    bool isPcControlEnabled() const;
    /** Returns the stored MAC address for Wake-on-LAN. */
    QString getPcMacAddress() const;
    /** Returns the broadcast address used for WoL packets. */
    QString getWolBroadcastAddress() const;
    /** Returns the UDP port used for WoL packets. */
    int getWolPort() const;

signals:
    /** Emitted when the MQTT connection is established. */
    void MqttConnected();
    /** Emitted when the MQTT connection is lost. */
    void MqttDisconnected(const QString& reason);
    /** Emitted when an MQTT message arrives on a subscribed topic. */
    void MqttMessageReceived(const QString& topic, const QString& message);
    /** Emitted when an MQTT error occurs. */
    void MqttError(const QString& errorMessage);
    /** Emitted when the network connectivity state changes. */
    void NetworkStatusChanged(bool connected);
    /** Emitted when a device status update is received. */
    void DeviceStatusUpdated(const QString& deviceId, const QString& status);
    /** Emitted when a command is received via MQTT. */
    void CommandReceived(const QString& command, const QByteArray& payload);
    /** Emitted when lecturer sync data is received. */
    void SyncDataReceived(const QJsonObject& lecturerData);
    /** Emitted when an OTA update command is received. */
    void OtaCommandReceived(const QString& firmwareUrl);

public slots:
    /** D-Bus: Returns the current MQTT connection status string. */
    Q_SCRIPTABLE QString GetMqttStatus() const;
    /** D-Bus: Returns "ONLINE" or "OFFLINE" based on network check. */
    Q_SCRIPTABLE QString GetNetworkStatus() const;
    /** D-Bus: Publishes a raw payload to the given MQTT topic. */
    Q_SCRIPTABLE bool PublishEvent(const QString& topic, const QByteArray& payload, int qos);
    /** D-Bus: Returns the configured room identifier. */
    Q_SCRIPTABLE QString GetRoomId() const;
    /** D-Bus: Sets the room identifier. */
    Q_SCRIPTABLE bool SetRoomId(const QString& roomId);
    /** D-Bus: Sends a Wake-on-LAN magic packet to the given MAC. */
    Q_SCRIPTABLE bool SendWakeOnLAN(const QString& macAddress, const QString& broadcastAddr);
    /** D-Bus: Requests a lecturer list sync via MQTT. */
    Q_SCRIPTABLE bool SyncLecturerList();
    /** D-Bus: Returns the local IP, gateway, and DNS server addresses. */
    Q_SCRIPTABLE QString GetConnectionDetails(QString& gateway, QString& dns) const;
    /** D-Bus: Requests an OTA firmware update for the given version. */
    Q_SCRIPTABLE bool RequestOTAUpdate(const QString& firmwareVersion);
    /** D-Bus: Returns true if PC control is enabled. */
    Q_SCRIPTABLE bool IsPcControlEnabled() const;
    /** D-Bus: Returns the MAC address used for Wake-on-LAN. */
    Q_SCRIPTABLE QString GetPcMacAddress() const;

    /** Compatibility: Connects to the MQTT broker. */
    Q_SCRIPTABLE bool ConnectToMqtt(const QString& broker, int port);
    /** Compatibility: Disconnects from the MQTT broker. */
    Q_SCRIPTABLE bool DisconnectFromMqtt();
    /** Compatibility: Publishes a device status update for a room. */
    Q_SCRIPTABLE bool PublishDeviceStatus(const QString& roomId, const QString& deviceId, const QString& status);
    /** Compatibility: Publishes an event with a JSON string payload. */
    Q_SCRIPTABLE bool PublishEvent(const QString& roomId, const QString& eventType, const QString& eventJson);
    /** Compatibility: Sends Wake-on-LAN using the stored broadcast and port. */
    Q_SCRIPTABLE bool SendWoL(const QString& macAddress);

    /** Handles the MQTT connected signal. */
    void onMqttConnected();
    /** Handles the MQTT disconnected signal. */
    void onMqttDisconnected();
    /** Handles an incoming MQTT message. */
    void onMqttMessageReceived(const QString& topic, const QByteArray& message);
    /** Handles an MQTT error signal. */
    void onMqttError(const QString& error);

protected slots:
    /** Periodically checks network connectivity via ping. */
    void checkNetworkStatus();
    /** Routes an incoming MQTT message to the appropriate command handler. */
    void processTopicMessage(const QString& topic, const QByteArray& message);
    /** Attempts to reconnect to the MQTT broker after a disconnect. */
    void onMqttReconnectTimeout();
    /** Publishes a heartbeat status message to MQTT. */
    void publishHeartbeat();
    /** Handles authentication status changes from the auth service. */
    void onAuthStatusChanged(const QString& status);

private:
    /** Handles a projector command received via MQTT. */
    void handleProjectorCommand(const QString& roomId, const QJsonObject& data);
    /** Handles a relay command received via MQTT. */
    void handleRelayCommand(const QString& roomId, const QJsonObject& data);
    /** Handles an AC command received via MQTT. */
    void handleAcCommand(const QString& roomId, const QJsonObject& data);
    /** Handles a sync command and emits the received lecturer data. */
    void handleSyncCommand(const QString& roomId, const QJsonObject& syncData);
    /** Handles an OTA command and emits the firmware URL. */
    void handleOtaCommand(const QString& roomId, const QJsonObject& otaData);

    // Configuration
    QString m_mqttBroker;
    int m_mqttPort;
    QString m_roomId;
    QString m_deviceId;
    QString m_configPath;
    bool m_pcControlEnabled;
    QString m_pcMacAddress;
    QString m_wolBroadcastAddress;
    int m_wolPort;

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
