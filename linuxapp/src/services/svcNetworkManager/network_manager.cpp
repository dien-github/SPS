#include "network_manager.h"
#include "../common/sps_logger.h"
#include "../common/sps_runtime_config.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QNetworkInterface>
#include <QProcess>
#include <QRegularExpression>

/** Creates the Network Manager, initializes member defaults, and logs creation. */
NetworkManager::NetworkManager(QObject* parent)
    : SpsServiceBase("com.sps.netmgr", "/com/sps/netmgr", parent),
      m_mqttBroker("localhost"),
      m_mqttPort(1883),
      m_deviceId("sps-pi-001"),
      m_staticIp(""),
      m_declaredStatus("Active"),
      m_configPath(SPS::Runtime::configFile("SPS_CONFIG_FILE", "config.json")),
      m_pcControlEnabled(false),
      m_pcMacAddress(""),
      m_wolBroadcastAddress("255.255.255.255"),
      m_wolPort(9),
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

/** Calls shutdown() to cleanly stop the service on destruction. */
NetworkManager::~NetworkManager() {
    shutdown();
}

/** Creates the MQTT client, sets up timers, loads config, and registers D-Bus. */
bool NetworkManager::initialize() {
    logInfo("Initializing Network Manager service...");

    // Create MQTT client
    m_mqttClient = new MqttClient(this);
    connect(m_mqttClient, &MqttClient::connected, this, &NetworkManager::onMqttConnected);
    connect(m_mqttClient, &MqttClient::disconnected, this, &NetworkManager::onMqttDisconnected);
    connect(m_mqttClient, &MqttClient::messageReceived, this, &NetworkManager::onMqttMessageReceived);
    connect(m_mqttClient, &MqttClient::connectionFailed, this, &NetworkManager::onMqttError);
    connect(m_mqttClient, &MqttClient::debugMessage, this, [this](const QString& message) {
        logDebug(message);
    });

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
            m_staticIp = config["static_ip"].toString("");
            m_declaredStatus = config["declared_status"].toString("Active");
            m_pcControlEnabled = config["enable_pc_control"].toBool(false);
            m_pcMacAddress = config["pc_mac"].toString();
            m_wolBroadcastAddress = config["wol_broadcast"].toString("255.255.255.255");
            m_wolPort = config["wol_port"].toInt(9);
        }
    }

    m_pcControlEnabled = SPS::Runtime::envBool("SPS_ENABLE_PC_CONTROL", m_pcControlEnabled);
    m_pcMacAddress = SPS::Runtime::envString("SPS_PC_MAC", m_pcMacAddress);
    m_wolBroadcastAddress = SPS::Runtime::envString("SPS_WOL_BROADCAST", m_wolBroadcastAddress);
    m_wolPort = SPS::Runtime::envInt("SPS_WOL_PORT", m_wolPort);
    m_staticIp = SPS::Runtime::envString("SPS_STATIC_IP", m_staticIp);
    m_declaredStatus = SPS::Runtime::envString("SPS_DECLARED_STATUS", m_declaredStatus);

    logInfo(QString("Configuration loaded: broker=%1:%2, roomId=%3, deviceId=%4, "
                    "ip=%5, declared=%6, pcControl=%7")
        .arg(m_mqttBroker).arg(m_mqttPort).arg(m_roomId).arg(m_deviceId)
        .arg(m_staticIp).arg(m_declaredStatus)
        .arg(m_pcControlEnabled ? "enabled" : "disabled"));

    // Register D-Bus service
    if (!registerService()) {
        logError("Failed to register D-Bus service");
        return false;
    }

    setRunning(true);
    logInfo("Network Manager initialized");

    QTimer::singleShot(0, this, [this]() {
        connectToMqtt(m_mqttBroker, m_mqttPort);
    });

    return true;
}

/** Stops all timers, disconnects MQTT, and calls the base shutdown. */
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

/** Returns a formatted string with MQTT, network, and message statistics. */
QString NetworkManager::getStatus() const {
    return QString("Network Status: MQTT=%1, Network=%2 | Pub=%3, Recv=%4, Errors=%5")
        .arg(m_mqttConnected ? "Connected" : "Disconnected")
        .arg(m_networkConnected ? "Online" : "Offline")
        .arg(m_messagesPublished).arg(m_messagesReceived).arg(m_mqttErrors);
}

/** Stores the broker address and initiates an MQTT client connection. */
bool NetworkManager::connectToMqtt(const QString& broker, int port) {
    m_mqttBroker = broker;
    m_mqttPort = port;

    if (!m_mqttClient) {
        logError("MQTT client not initialized");
        return false;
    }

    logInfo(QString("Connecting to MQTT broker: %1:%2").arg(broker).arg(port));

    QJsonObject lwt;
    lwt["device_id"] = m_deviceId;
    lwt["room_id"] = m_roomId;
    lwt["status"] = "offline";
    lwt["ip"] = m_staticIp;
    lwt["declared_status"] = m_declaredStatus;
    lwt["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    m_mqttClient->setLastWillAndTestament(
        QString("sps/%1/status/connection").arg(m_roomId),
        QJsonDocument(lwt).toJson(QJsonDocument::Compact),
        1);

    if (!m_mqttClient->connect(broker, port, m_deviceId)) {
        logError("Failed to initiate MQTT connection");
        return false;
    }

    return true;
}

/** Subscribes to an MQTT topic and tracks it in the local subscription list. */
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

/** Publishes a device status JSON payload to the room's status topic. */
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

/** Publishes a JSON event (with device_id and timestamp) to the room's event topic. */
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

/** Publishes OTA progress percentage and status string for a room. */
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

/** Constructs and sends a WoL magic packet over UDP to wake the target PC. */
bool NetworkManager::broadcastWoL(const QString& macAddress, const QString& broadcastAddr, int port) {
    if (!m_pcControlEnabled) {
        logWarning("Wake-on-LAN skipped because PC control is disabled by config");
        return false;
    }

    if (!m_wolSocket) {
        logError("WoL socket not initialized");
        return false;
    }

    const QString targetMac = macAddress.isEmpty() ? m_pcMacAddress : macAddress;
    const QString targetBroadcast = broadcastAddr.isEmpty() ? m_wolBroadcastAddress : broadcastAddr;
    const int targetPort = port > 0 ? port : m_wolPort;

    // Parse MAC address
    QStringList parts = targetMac.split(":");
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
    QHostAddress broadcast(targetBroadcast);
    qint64 bytesSent = m_wolSocket->writeDatagram(magicPacket, broadcast, targetPort);

    if (bytesSent != magicPacket.size()) {
        logError(QString("Failed to send WoL packet (sent %1/%2 bytes)").arg(bytesSent).arg(magicPacket.size()));
        return false;
    }

    logInfo(QString("WoL packet sent to %1 on %2:%3").arg(targetMac, targetBroadcast).arg(targetPort));
    return true;
}

/** Pings a remote host and updates the internal network connectivity state. */
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

/** Returns "CONNECTED" or "DISCONNECTED" based on MQTT state. */
QString NetworkManager::getMqttStatus() const {
    return m_mqttConnected ? "CONNECTED" : "DISCONNECTED";
}

/** Returns the configured MQTT broker hostname. */
QString NetworkManager::getMqttBroker() const {
    return m_mqttBroker;
}

/** Returns the configured MQTT broker port. */
int NetworkManager::getMqttPort() const {
    return m_mqttPort;
}

/** Returns whether the network was last detected as reachable. */
bool NetworkManager::isNetworkConnected() const {
    return m_networkConnected;
}

/** Returns whether PC control (WoL) is enabled. */
bool NetworkManager::isPcControlEnabled() const {
    return m_pcControlEnabled;
}

/** Returns the MAC address stored for Wake-on-LAN. */
QString NetworkManager::getPcMacAddress() const {
    return m_pcMacAddress;
}

/** Returns the broadcast address used for Wake-on-LAN packets. */
QString NetworkManager::getWolBroadcastAddress() const {
    return m_wolBroadcastAddress;
}

/** Returns the UDP port used for Wake-on-LAN packets. */
int NetworkManager::getWolPort() const {
    return m_wolPort;
}

/** D-Bus: Delegates to getMqttStatus(). */
QString NetworkManager::GetMqttStatus() const {
    return getMqttStatus();
}

/** D-Bus: Returns "ONLINE" or "OFFLINE" based on network state. */
QString NetworkManager::GetNetworkStatus() const {
    return m_networkConnected ? "ONLINE" : "OFFLINE";
}

/** D-Bus: Delegates to connectToMqtt(). */
bool NetworkManager::ConnectToMqtt(const QString& broker, int port) {
    return connectToMqtt(broker, port);
}

/** D-Bus: Disconnects from the MQTT broker if the client exists. */
bool NetworkManager::DisconnectFromMqtt() {
    if (!m_mqttClient) {
        return false;
    }

    m_mqttClient->disconnect();
    return true;
}

/** D-Bus: Builds an event with device ID and status, then publishes it. */
bool NetworkManager::PublishDeviceStatus(const QString& roomId, const QString& deviceId, const QString& status) {
    QJsonObject eventData;
    eventData["device_id"] = deviceId;
    eventData["status"] = status;
    return publishEvent(roomId, "status", eventData);
}

/** D-Bus: Publishes a raw payload directly to the given MQTT topic. */
bool NetworkManager::PublishEvent(const QString& topic, const QByteArray& payload, int qos) {
    if (!m_mqttClient || !m_mqttConnected) {
        logWarning("MQTT not connected, cannot publish");
        return false;
    }

    const bool result = m_mqttClient->publish(topic, payload, qos, false);
    if (result) {
        m_messagesPublished++;
        logDebug(QString("Published raw event to %1").arg(topic));
    }

    return result;
}

/** D-Bus: Parses a JSON string and publishes it as an event for the room. */
bool NetworkManager::PublishEvent(const QString& roomId, const QString& eventType, const QString& eventJson) {
    QJsonDocument doc = QJsonDocument::fromJson(eventJson.toLatin1());
    if (!doc.isObject()) {
        logError("Invalid JSON in PublishEvent");
        return false;
    }

    return publishEvent(roomId, eventType, doc.object());
}

/** Compatibility: Delegates to broadcastWoL() using stored broadcast and port. */
bool NetworkManager::SendWoL(const QString& macAddress) {
    return broadcastWoL(macAddress.isEmpty() ? m_pcMacAddress : macAddress,
                        m_wolBroadcastAddress,
                        m_wolPort);
}

/** D-Bus: Delegates to broadcastWoL() with explicit MAC and broadcast address. */
bool NetworkManager::SendWakeOnLAN(const QString& macAddress, const QString& broadcastAddr) {
    return broadcastWoL(macAddress.isEmpty() ? m_pcMacAddress : macAddress,
                        broadcastAddr.isEmpty() ? m_wolBroadcastAddress : broadcastAddr,
                        m_wolPort);
}

/** D-Bus: Publishes a sync_lecturer_list request to the MQTT command topic. */
bool NetworkManager::SyncLecturerList() {
    if (!m_mqttClient || !m_mqttConnected) {
        logWarning("MQTT not connected, cannot request lecturer sync");
        return false;
    }

    QJsonObject payload;
    payload["device_id"] = m_deviceId;
    payload["room_id"] = m_roomId;
    payload["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    payload["action"] = "sync_lecturer_list";

    const QString topic = QString("sps/%1/cmd/sync").arg(m_roomId);
    const bool result = m_mqttClient->publish(topic, QJsonDocument(payload).toJson(QJsonDocument::Compact), 1, false);
    if (result) {
        m_messagesPublished++;
        logInfo("Lecturer list sync requested");
    }

    return result;
}

/** D-Bus: Returns the local IP address and fills gateway and DNS references. */
QString NetworkManager::GetConnectionDetails(QString& gateway, QString& dns) const {
    QString ipAddress;

    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& interface : interfaces) {
        if (!(interface.flags() & QNetworkInterface::IsUp) ||
            !(interface.flags() & QNetworkInterface::IsRunning) ||
            (interface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }

        const auto addresses = interface.addressEntries();
        for (const QNetworkAddressEntry& address : addresses) {
            if (address.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                ipAddress = address.ip().toString();
                break;
            }
        }

        if (!ipAddress.isEmpty()) {
            break;
        }
    }

    QProcess ipRoute;
    ipRoute.start("ip", QStringList() << "route" << "show" << "default");
    if (ipRoute.waitForFinished(1000) && ipRoute.exitCode() == 0) {
        const QString output = QString::fromLocal8Bit(ipRoute.readAllStandardOutput());
        const QRegularExpression re("\\bvia\\s+(\\S+)");
        const QRegularExpressionMatch match = re.match(output);
        if (match.hasMatch()) {
            gateway = match.captured(1);
        }
    }

    QFile resolvConf("/etc/resolv.conf");
    if (resolvConf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!resolvConf.atEnd()) {
            const QString line = QString::fromUtf8(resolvConf.readLine()).trimmed();
            if (line.startsWith("nameserver ")) {
                dns = line.section(QRegularExpression("\\s+"), 1, 1);
                break;
            }
        }
    }

    return ipAddress;
}

/** D-Bus: Publishes a request_ota_update message for the given firmware version. */
bool NetworkManager::RequestOTAUpdate(const QString& firmwareVersion) {
    if (!m_mqttClient || !m_mqttConnected) {
        logWarning("MQTT not connected, cannot request OTA update");
        return false;
    }

    QJsonObject payload;
    payload["device_id"] = m_deviceId;
    payload["room_id"] = m_roomId;
    payload["current_version"] = firmwareVersion;
    payload["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    payload["action"] = "request_ota_update";

    const QString topic = QString("sps/%1/cmd/ota").arg(m_roomId);
    const bool result = m_mqttClient->publish(topic, QJsonDocument(payload).toJson(QJsonDocument::Compact), 1, false);
    if (result) {
        m_messagesPublished++;
        logInfo(QString("OTA update requested for version %1").arg(firmwareVersion));
    }

    return result;
}

/** D-Bus: Delegates to isPcControlEnabled(). */
bool NetworkManager::IsPcControlEnabled() const {
    return isPcControlEnabled();
}

/** D-Bus: Delegates to getPcMacAddress(). */
QString NetworkManager::GetPcMacAddress() const {
    return getPcMacAddress();
}

/** D-Bus: Returns the current room identifier. */
QString NetworkManager::GetRoomId() const {
    return m_roomId;
}

/** D-Bus: Sets a new room identifier and logs the change. */
bool NetworkManager::SetRoomId(const QString& roomId) {
    m_roomId = roomId;
    logInfo(QString("Room ID set to: %1").arg(roomId));
    return true;
}

/** Sets m_mqttConnected, stops reconnect timer, subscribes to control topics, and publishes "connected". */
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

    QJsonObject payload;
    payload["device_id"] = m_deviceId;
    payload["room_id"] = m_roomId;
    payload["status"] = "connected";
    payload["ip"] = m_staticIp;
    payload["declared_status"] = m_declaredStatus;
    payload["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    if (m_mqttClient->publish(QString("sps/%1/status/connection").arg(m_roomId),
                              QJsonDocument(payload).toJson(QJsonDocument::Compact),
                              0, true)) {
        m_messagesPublished++;
    }
}

/** Marks MQTT as disconnected, emits the signal, and starts the reconnect timer. */
void NetworkManager::onMqttDisconnected() {
    m_mqttConnected = false;
    logWarning("MQTT disconnected");
    emit MqttDisconnected("Connection lost");

    // Schedule reconnect
    m_reconnectTimer.start(5000);
}

/** Increments the received counter and routes the message to processTopicMessage(). */
void NetworkManager::onMqttMessageReceived(const QString& topic, const QByteArray& message) {
    m_messagesReceived++;
    logDebug(QString("Message received on %1").arg(topic));
    processTopicMessage(topic, message);
}

/** Increments the error counter, logs, and emits the MqttError signal. */
void NetworkManager::onMqttError(const QString& error) {
    m_mqttErrors++;
    logError(QString("MQTT error: %1").arg(error));
    emit MqttError(error);
}

/** Records the check timestamp and delegates to checkNetworkConnectivity(). */
void NetworkManager::checkNetworkStatus() {
    m_lastNetworkCheck = QDateTime::currentDateTime();
    checkNetworkConnectivity("8.8.8.8", 5000);
}

/** Publishes a "heartbeat" status if MQTT is currently connected. */
void NetworkManager::publishHeartbeat() {
    if (m_mqttConnected) {
        QJsonObject payload;
        payload["device_id"] = m_deviceId;
        payload["room_id"] = m_roomId;
        payload["status"] = "online";
        payload["ip"] = m_staticIp;
        payload["declared_status"] = m_declaredStatus;
        payload["uptime"] = QString::number(QDateTime::currentSecsSinceEpoch());
        payload["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        if (m_mqttClient->publish(QString("sps/%1/status/connection").arg(m_roomId),
                                  QJsonDocument(payload).toJson(QJsonDocument::Compact),
                                  0, true)) {
            m_messagesPublished++;
        }
    }
}

/** Attempts to reconnect to the MQTT broker, up to 10 retries. */
void NetworkManager::onMqttReconnectTimeout() {
    if (!m_mqttConnected && m_mqttReconnectCount < 10) {
        m_mqttReconnectCount++;
        logInfo(QString("Attempting MQTT reconnect %1/10...").arg(m_mqttReconnectCount));
        connectToMqtt(m_mqttBroker, m_mqttPort);
    }
}

/** Logs the auth status change and publishes it to MQTT. */
void NetworkManager::onAuthStatusChanged(const QString& status) {
    logDebug(QString("Auth status changed: %1").arg(status));
    publishStatus(m_roomId, status);
}

/** Parses the JSON message and routes it to the correct command handler based on the topic. */
void NetworkManager::processTopicMessage(const QString& topic, const QByteArray& message) {
    QJsonDocument doc = QJsonDocument::fromJson(message);
    if (!doc.isObject()) {
        logWarning("Received non-JSON message");
        return;
    }

    QJsonObject payload = doc.object();

    // Extract roomId and deviceKey from topic: sps/<roomId>/cmd/<deviceKey>
    QStringList parts = topic.split("/");
    if (parts.size() < 4) return;

    const QString topicRoomId = parts[1];
    const QString deviceKey = parts[3];

    // Validate room ID if local room is configured
    if (!m_roomId.isEmpty() && topicRoomId != m_roomId) {
        logWarning(QString("Room mismatch: topic=%1, topic_room=%2, local_room=%3")
            .arg(topic, topicRoomId, m_roomId));
        return;
    }

    // Attach room_id metadata for downstream validation
    payload["room_id"] = topicRoomId;

    if (deviceKey == "projector") {
        handleProjectorCommand(topicRoomId, deviceKey, payload);
    } else if (deviceKey == "relay") {
        handleRelayCommand(topicRoomId, deviceKey, payload);
    } else if (deviceKey == "ac") {
        handleAcCommand(topicRoomId, deviceKey, payload);
    } else if (deviceKey == "sync") {
        handleSyncCommand(topicRoomId, payload);
    } else if (deviceKey == "ota") {
        handleOtaCommand(topicRoomId, payload);
    } else {
        logInfo(QString("Remote command topic=%1, room=%2, device=%3")
            .arg(topic, topicRoomId, deviceKey));
        emit CommandReceived(deviceKey, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    }
}

/** Logs the projector command and emits a generic CommandReceived signal. */
void NetworkManager::handleProjectorCommand(const QString& roomId, const QString& deviceKey, const QJsonObject& data) {
    logInfo(QString("Projector command room=%1 device=%2: %3").arg(roomId, deviceKey, data["action"].toString()));
    emit CommandReceived(deviceKey, QJsonDocument(data).toJson(QJsonDocument::Compact));
}

/** Logs the relay command and emits a generic CommandReceived signal. */
void NetworkManager::handleRelayCommand(const QString& roomId, const QString& deviceKey, const QJsonObject& data) {
    logInfo(QString("Relay command room=%1 device=%2: %3").arg(roomId, deviceKey, data["action"].toString()));
    emit CommandReceived(deviceKey, QJsonDocument(data).toJson(QJsonDocument::Compact));
}

/** Logs the AC command and emits a generic CommandReceived signal. */
void NetworkManager::handleAcCommand(const QString& roomId, const QString& deviceKey, const QJsonObject& data) {
    logInfo(QString("AC command room=%1 device=%2: %3").arg(roomId, deviceKey, data["action"].toString()));
    emit CommandReceived(deviceKey, QJsonDocument(data).toJson(QJsonDocument::Compact));
}

/** Logs the sync command and emits the SyncDataReceived signal with the data. */
void NetworkManager::handleSyncCommand(const QString& roomId, const QJsonObject& syncData) {
    logInfo(QString("Sync command from %1").arg(roomId));
    emit SyncDataReceived(syncData);
}

/** Logs the OTA command and emits the OtaCommandReceived signal with the firmware URL. */
void NetworkManager::handleOtaCommand(const QString& roomId, const QJsonObject& otaData) {
    logInfo(QString("OTA command from %1: %2").arg(roomId).arg(otaData["url"].toString()));
    emit OtaCommandReceived(otaData["url"].toString());
}
