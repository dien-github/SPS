#include "network_manager.h"
#include "../common/sps_logger.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QNetworkInterface>
#include <QProcess>

NetworkManager::NetworkManager(QObject* parent)
    : SpsServiceBase("com.sps.network", "/com/sps/network", parent),
      m_mqttPort(1883),
      m_deviceId("sps-pi-001"),
      m_configPath("/opt/sps/config/config.json"),
      m_mqttClient(nullptr),
      m_mqttConnected(false),
      m_mqttReconnectCount(0),
      m_networkConnected(false),
      m_wolSocket(nullptr),
      m_messagesPublished(0),
      m_messagesReceived(0),
      m_mqttErrors(0),
      m_networkStateChanges(0) {

    logInfo("Network Manager service created");
}

NetworkManager::~NetworkManager() {
    shutdown();
}

// Initialize service
bool NetworkManager::initialize() {
    logInfo("Initializing Network Manager service...");

    // Create MQTT client
    m_mqttClient = new MqttClient(this);
    connect(m_mqttClient, &MqttClient::connected, this, &NetworkManager::onMqttConnected);
    connect(m_mqttClient, &MqttClient::disconnected, this, &NetworkManager::onMqttDisconnected);
    connect(m_mqttClient, &MqttClient::messageReceived, this, &NetworkManager::onMqttMessageReceived);
    connect(m_mqttClient, &MqttClient::connectionFailed, this, &NetworkManager::onMqttError);

    // Setup network monitoring
    connect(&m_networkCheckTimer, &QTimer::timeout, this, &NetworkManager::checkNetworkStatus);
    m_networkCheckTimer.start(10000);  // Check every 10 seconds

    // Setup heartbeat
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &NetworkManager::publishHeartbeat);
    m_heartbeatTimer.start(30000);  // Heartbeat every 30 seconds

    // Setup reconnect timer
    connect(&m_reconnectTimer, &QTimer::timeout, this, &NetworkManager::onMqttReconnectTimeout);

    // Create WoL socket
    m_wolSocket = new QUdpSocket(this);

    // Load configuration
    QFile configFile(m_configPath);
    if (configFile.open(QIODevice::ReadOnly)) {
        QByteArray data = configFile.readAll();
        configFile.close();

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            QJsonObject config = doc.object();
            m_mqttBroker = config["mqtt_broker"].toString("localhost");
            m_mqttPort = config["mqtt_port"].toInt(1883);
            m_roomId = config["room_id"].toString("room-001");
            m_deviceId = config["device_id"].toString("sps-pi-001");
        }
    }

    logInfo(QString("Configuration loaded: broker=%1:%2, roomId=%3, deviceId=%4")
        .arg(m_mqttBroker).arg(m_mqttPort).arg(m_roomId).arg(m_deviceId));

    // Register D-Bus service
    if (!registerService()) {
        logError("Failed to register D-Bus service");
        return false;
    }

    setRunning(true);
    logInfo("Network Manager initialized");

    return true;
}

// Shutdown service
void NetworkManager::shutdown() {
    logInfo("Shutting down Network Manager service...");

    m_networkCheckTimer.stop();
    m_heartbeatTimer.stop();
    m_reconnectTimer.stop();

    if (m_mqttClient && m_mqttConnected) {
        m_mqttClient->disconnect();
    }

    SpsServiceBase::shutdown();
}

// Get service status
QString NetworkManager::getStatus() const {
    return QString("Network Status: MQTT=%1, Network=%2 | Pub=%3, Recv=%4, Errors=%5")
        .arg(m_mqttConnected ? "Connected" : "Disconnected")
        .arg(m_networkConnected ? "Online" : "Offline")
        .arg(m_messagesPublished).arg(m_messagesReceived).arg(m_mqttErrors);
}

// Connect to MQTT broker
bool NetworkManager::connectToMqtt(const QString& broker, int port) {
    m_mqttBroker = broker;
    m_mqttPort = port;

    if (!m_mqttClient) {
        logError("MQTT client not initialized");
        return false;
    }

    logInfo(QString("Connecting to MQTT broker: %1:%2").arg(broker).arg(port));

    if (!m_mqttClient->connect(broker, port, m_deviceId)) {
        logError("Failed to initiate MQTT connection");
        return false;
    }

    return true;
}

// Subscribe to topic
bool NetworkManager::subscribeTopic(const QString& topic, int qos) {
    if (!m_mqttClient || !m_mqttConnected) {
        logWarning("MQTT not connected, cannot subscribe");
        return false;
    }

    if (!m_subscribedTopics.contains(topic)) {
        m_subscribedTopics.append(topic);
    }

    return m_mqttClient->subscribe(topic, qos);
}

// Publish device status
bool NetworkManager::publishStatus(const QString& roomId, const QString& deviceStatus) {
    if (!m_mqttClient || !m_mqttConnected) {
        logWarning("MQTT not connected, cannot publish");
        return false;
    }

    QString topic = QString("sps/%1/status/devices").arg(roomId);

    QJsonObject payload;
    payload["device_id"] = m_deviceId;
    payload["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    payload["status"] = deviceStatus;

    QJsonDocument doc(payload);
    bool result = m_mqttClient->publish(topic, doc.toJson(), 0, true);

    if (result) {
        m_messagesPublished++;
        logDebug(QString("Published to %1").arg(topic));
    }

    return result;
}

// Publish event
bool NetworkManager::publishEvent(const QString& roomId, const QString& eventType, const QJsonObject& eventData) {
    if (!m_mqttClient || !m_mqttConnected) {
        logWarning("MQTT not connected, cannot publish");
        return false;
    }

    QString topic = QString("sps/%1/event/%2").arg(roomId, eventType);

    QJsonObject payload = eventData;
    if (!payload.contains("device_id")) {
        payload["device_id"] = m_deviceId;
    }
    if (!payload.contains("timestamp")) {
        payload["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    }

    QJsonDocument doc(payload);
    bool result = m_mqttClient->publish(topic, doc.toJson(), 1, false);

    if (result) {
        m_messagesPublished++;
        logDebug(QString("Published event to %1").arg(topic));
    }

    return result;
}

// Publish OTA progress
bool NetworkManager::publishOtaProgress(const QString& roomId, int percentage, const QString& status) {
    if (!m_mqttClient || !m_mqttConnected) {
        return false;
    }

    QString topic = QString("sps/%1/event/ota").arg(roomId);

    QJsonObject payload;
    payload["device_id"] = m_deviceId;
    payload["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    payload["progress"] = percentage;
    payload["status"] = status;

    QJsonDocument doc(payload);
    return m_mqttClient->publish(topic, doc.toJson(), 1, false);
}

// Broadcast Wake-on-LAN packet
bool NetworkManager::broadcastWoL(const QString& macAddress, const QString& broadcastAddr, int port) {
    if (!m_wolSocket) {
        logError("WoL socket not initialized");
        return false;
    }

    // Parse MAC address
    QStringList parts = macAddress.split(":");
    if (parts.size() != 6) {
        logError("Invalid MAC address format");
        return false;
    }

    // Create WoL magic packet: 6x 0xFF followed by 16x MAC address
    QByteArray magicPacket;

    // Header: 6 bytes of 0xFF
    for (int i = 0; i < 6; ++i) {
        magicPacket.append(static_cast<char>(0xFF));
    }

    // Repeat MAC address 16 times
    for (int i = 0; i < 16; ++i) {
        for (const QString& part : parts) {
            bool ok;
            uchar byte = part.toUShort(&ok, 16);
            magicPacket.append(static_cast<char>(byte));
        }
    }

    // Send broadcast packet
    QHostAddress broadcast(broadcastAddr);
    qint64 bytesSent = m_wolSocket->writeDatagram(magicPacket, broadcast, port);

    if (bytesSent != magicPacket.size()) {
        logError(QString("Failed to send WoL packet (sent %1/%2 bytes)").arg(bytesSent).arg(magicPacket.size()));
        return false;
    }

    logInfo(QString("WoL packet sent to %1 on %2:%3").arg(macAddress, broadcastAddr).arg(port));
    return true;
}

// Check network connectivity
bool NetworkManager::checkNetworkConnectivity(const QString& host, int timeout) {
    // Use ping command to check connectivity
    QProcess ping;
    ping.start("ping", QStringList() << "-c" << "1" << "-W" << QString::number(timeout / 1000) << host);

    if (!ping.waitForFinished(timeout)) {
        ping.kill();
        return false;
    }

    bool connected = ping.exitCode() == 0;

    if (connected != m_networkConnected) {
        m_networkConnected = connected;
        m_networkStateChanges++;
        emit NetworkStatusChanged(connected);
        logInfo(QString("Network status changed: %1").arg(connected ? "Online" : "Offline"));
    }

    return connected;
}

// Getters
QString NetworkManager::getMqttStatus() const {
    return m_mqttConnected ? "CONNECTED" : "DISCONNECTED";
}

QString NetworkManager::getMqttBroker() const {
    return m_mqttBroker;
}

int NetworkManager::getMqttPort() const {
    return m_mqttPort;
}

bool NetworkManager::isNetworkConnected() const {
    return m_networkConnected;
}

// D-Bus Method: GetMqttStatus
QString NetworkManager::GetMqttStatus() const {
    return getMqttStatus();
}

// D-Bus Method: GetNetworkStatus
QString NetworkManager::GetNetworkStatus() const {
    return m_networkConnected ? "ONLINE" : "OFFLINE";
}

// D-Bus Method: ConnectToMqtt
bool NetworkManager::ConnectToMqtt(const QString& broker, int port) {
    return connectToMqtt(broker, port);
}

// D-Bus Method: DisconnectFromMqtt
bool NetworkManager::DisconnectFromMqtt() {
    if (!m_mqttClient) {
        return false;
    }

    m_mqttClient->disconnect();
    return true;
}

// D-Bus Method: PublishDeviceStatus
bool NetworkManager::PublishDeviceStatus(const QString& roomId, const QString& deviceId, const QString& status) {
    QJsonObject eventData;
    eventData["device_id"] = deviceId;
    eventData["status"] = status;
    return publishEvent(roomId, "status", eventData);
}

// D-Bus Method: PublishEvent
bool NetworkManager::PublishEvent(const QString& roomId, const QString& eventType, const QString& eventJson) {
    QJsonDocument doc = QJsonDocument::fromJson(eventJson.toLatin1());
    if (!doc.isObject()) {
        logError("Invalid JSON in PublishEvent");
        return false;
    }

    return publishEvent(roomId, eventType, doc.object());
}

// D-Bus Method: SendWoL
bool NetworkManager::SendWoL(const QString& macAddress) {
    return broadcastWoL(macAddress);
}

// D-Bus Method: GetRoomId
QString NetworkManager::GetRoomId() const {
    return m_roomId;
}

// D-Bus Method: SetRoomId
bool NetworkManager::SetRoomId(const QString& roomId) {
    m_roomId = roomId;
    logInfo(QString("Room ID set to: %1").arg(roomId));
    return true;
}

// Slot: MQTT connected
void NetworkManager::onMqttConnected() {
    m_mqttConnected = true;
    m_mqttReconnectCount = 0;
    m_lastMqttConnection = QDateTime::currentDateTime();
    m_reconnectTimer.stop();

    logInfo("MQTT connected successfully");
    emit MqttConnected();

    // Subscribe to control topics
    QString baseTopics = QString("sps/%1/cmd/").arg(m_roomId);
    subscribeTopic(baseTopics + "projector", 1);
    subscribeTopic(baseTopics + "relay", 1);
    subscribeTopic(baseTopics + "ac", 1);
    subscribeTopic(baseTopics + "sync", 1);
    subscribeTopic(baseTopics + "ota", 1);
    subscribeTopic(QString("sps/%1/status/connection").arg(m_roomId), 0);

    // Publish LWT
    publishStatus(m_roomId, "connected");
}

// Slot: MQTT disconnected
void NetworkManager::onMqttDisconnected() {
    m_mqttConnected = false;
    logWarning("MQTT disconnected");
    emit MqttDisconnected("Connection lost");

    // Schedule reconnect
    m_reconnectTimer.start(5000);
}

// Slot: MQTT message received
void NetworkManager::onMqttMessageReceived(const QString& topic, const QByteArray& message) {
    m_messagesReceived++;
    logDebug(QString("Message received on %1").arg(topic));
    processTopicMessage(topic, message);
}

// Slot: MQTT error
void NetworkManager::onMqttError(const QString& error) {
    m_mqttErrors++;
    logError(QString("MQTT error: %1").arg(error));
    emit MqttError(error);
}

// Slot: Check network status
void NetworkManager::checkNetworkStatus() {
    m_lastNetworkCheck = QDateTime::currentDateTime();
    checkNetworkConnectivity("8.8.8.8", 5000);
}

// Slot: Publish heartbeat
void NetworkManager::publishHeartbeat() {
    if (m_mqttConnected) {
        publishStatus(m_roomId, "heartbeat");
    }
}

// Slot: MQTT reconnect timeout
void NetworkManager::onMqttReconnectTimeout() {
    if (!m_mqttConnected && m_mqttReconnectCount < 10) {
        m_mqttReconnectCount++;
        logInfo(QString("Attempting MQTT reconnect %1/10...").arg(m_mqttReconnectCount));
        connectToMqtt(m_mqttBroker, m_mqttPort);
    }
}

// Slot: Auth status changed
void NetworkManager::onAuthStatusChanged(const QString& status) {
    logDebug(QString("Auth status changed: %1").arg(status));
    publishStatus(m_roomId, status);
}

// Process incoming MQTT topic message
void NetworkManager::processTopicMessage(const QString& topic, const QByteArray& message) {
    QJsonDocument doc = QJsonDocument::fromJson(message);
    if (!doc.isObject()) {
        logWarning("Received non-JSON message");
        return;
    }

    QJsonObject payload = doc.object();

    // Extract room ID from topic
    QStringList parts = topic.split("/");
    if (parts.size() < 3) return;

    // Route based on command type
    if (topic.contains("/cmd/projector")) {
        handleProjectorCommand(parts[1], payload);
    } else if (topic.contains("/cmd/relay")) {
        handleRelayCommand(parts[1], payload);
    } else if (topic.contains("/cmd/ac")) {
        handleAcCommand(parts[1], payload);
    } else if (topic.contains("/cmd/sync")) {
        handleSyncCommand(parts[1], payload);
    } else if (topic.contains("/cmd/ota")) {
        handleOtaCommand(parts[1], payload);
    }
}

// Command handlers
void NetworkManager::handleProjectorCommand(const QString& roomId, const QJsonObject& data) {
    logInfo(QString("Projector command from %1: %2").arg(roomId).arg(data["action"].toString()));
    emit CommandReceived("projector", data);
}

void NetworkManager::handleRelayCommand(const QString& roomId, const QJsonObject& data) {
    logInfo(QString("Relay command from %1: %2").arg(roomId).arg(data["action"].toString()));
    emit CommandReceived("relay", data);
}

void NetworkManager::handleAcCommand(const QString& roomId, const QJsonObject& data) {
    logInfo(QString("AC command from %1: %2").arg(roomId).arg(data["action"].toString()));
    emit CommandReceived("ac", data);
}

void NetworkManager::handleSyncCommand(const QString& roomId, const QJsonObject& syncData) {
    logInfo(QString("Sync command from %1").arg(roomId));
    emit SyncDataReceived(syncData);
}

void NetworkManager::handleOtaCommand(const QString& roomId, const QJsonObject& otaData) {
    logInfo(QString("OTA command from %1: %2").arg(roomId).arg(otaData["url"].toString()));
    emit OtaCommandReceived(otaData["url"].toString());
}
