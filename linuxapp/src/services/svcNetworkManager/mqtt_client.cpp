#include "mqtt_client.h"
#include <QDebug>

/** Initializes the MQTT client in DISCONNECTED state with default values. */
MqttClient::MqttClient(QObject* parent)
    : QObject(parent), m_state(MQTT::ConnectionState::DISCONNECTED),
      m_brokerPort(MQTT::DEFAULT_BROKER_PORT),
      m_keepAliveInterval(MQTT::MQTT_KEEPALIVE_SECONDS),
      m_autoReconnect(true), m_retryIntervalMs(5000),
      m_lastWillQos(MQTT::QOS_AT_LEAST_ONCE),
      m_mqttClient(nullptr) {
}

/** Disconnects from the broker if currently connected. */
MqttClient::~MqttClient() {
    if (isConnected()) {
        disconnect();
    }
}

/** Stores connection parameters, sets state to CONNECTING, then simulates a successful connection. */
bool MqttClient::connect(const QString& brokerHost, int brokerPort,
                         const QString& clientId, const QString& username,
                         const QString& password) {
    if (m_state == MQTT::ConnectionState::CONNECTED) {
        m_lastError = "Already connected";
        return false;
    }

    QMutexLocker lock(&m_mutex);

    m_brokerHost = brokerHost;
    m_brokerPort = brokerPort;
    m_clientId = clientId;
    m_username = username;
    m_password = password;

    m_state = MQTT::ConnectionState::CONNECTING;

    // In a real implementation, this would initialize the MQTT library and connect
    // For now, we'll emit connected after a short delay (simulated)
    emit debugMessage(QString("Connecting to %1:%2 as %3...")
        .arg(m_brokerHost).arg(m_brokerPort).arg(m_clientId));

    // Simulated successful connection
    m_state = MQTT::ConnectionState::CONNECTED;
    emit connected();

    return true;
}

/** Sets state to DISCONNECTING, clears subscriptions, and emits disconnected signal. */
bool MqttClient::disconnect() {
    if (m_state == MQTT::ConnectionState::DISCONNECTED) {
        return true;
    }

    QMutexLocker lock(&m_mutex);

    m_state = MQTT::ConnectionState::DISCONNECTING;

    // Actual disconnection would happen here
    emit debugMessage("Disconnecting from MQTT broker...");

    m_state = MQTT::ConnectionState::DISCONNECTED;
    m_subscriptions.clear();
    emit disconnected("User disconnect");

    return true;
}

/** Returns true if the connection state is CONNECTED. */
bool MqttClient::isConnected() const {
    return m_state == MQTT::ConnectionState::CONNECTED;
}

/** Adds the topic to the local subscription map and emits a confirmation. */
bool MqttClient::subscribe(const QString& topic, int qos) {
    if (!isConnected()) {
        m_lastError = "Not connected to broker";
        emit publishFailed(topic, m_lastError);
        return false;
    }

    QMutexLocker lock(&m_mutex);

    m_subscriptions[topic] = qos;
    emit subscriptionChanged(topic, true);
    emit debugMessage(QString("Subscribed to: %1 (QoS %2)").arg(topic).arg(qos));

    return true;
}

/** Removes the topic from the subscription map and emits a confirmation. */
bool MqttClient::unsubscribe(const QString& topic) {
    if (m_subscriptions.remove(topic) > 0) {
        emit subscriptionChanged(topic, false);
        emit debugMessage(QString("Unsubscribed from: %1").arg(topic));
        return true;
    }

    return false;
}

/** Publishes a payload to a topic if connected; currently a simulated no-op. */
bool MqttClient::publish(const QString& topic, const QByteArray& payload,
                        int qos, bool retain) {
    if (!isConnected()) {
        m_lastError = "Not connected to broker";
        emit publishFailed(topic, m_lastError);
        return false;
    }

    QMutexLocker lock(&m_mutex);

    emit debugMessage(QString("Publishing to %1: %2 bytes (QoS %3)")
        .arg(topic).arg(payload.size()).arg(qos));

    // In real implementation, would send to broker
    // For now, just emit success
    return true;
}

/** Publishes a status payload using the device status topic template. */
bool MqttClient::publishStatus(const QString& roomId, const QByteArray& payload) {
    QString topic = expandTopic(MQTT::TOPIC_STATUS_DEVICES, roomId);
    return publish(topic, payload, MQTT::QOS_FIRE_AND_FORGET, true);
}

/** Publishes an event payload using the appropriate event topic template. */
bool MqttClient::publishEvent(const QString& roomId, const QString& eventType, 
                              const QByteArray& payload) {
    QString topic;

    if (eventType == "auth") {
        topic = expandTopic(MQTT::TOPIC_EVENT_AUTH, roomId);
    } else if (eventType == "ota") {
        topic = expandTopic(MQTT::TOPIC_EVENT_OTA, roomId);
    } else {
        topic = QString("sps/%1/event/%2").arg(roomId, eventType);
    }

    return publish(topic, payload, MQTT::QOS_AT_LEAST_ONCE);
}

/** Sets the keep-alive interval in seconds for the MQTT connection. */
void MqttClient::setKeepAliveInterval(int seconds) {
    m_keepAliveInterval = seconds;
}

/** Enables or disables auto-reconnect and sets the retry interval. */
void MqttClient::setAutoReconnect(bool autoReconnect, int retryIntervalMs) {
    m_autoReconnect = autoReconnect;
    m_retryIntervalMs = retryIntervalMs;
}

/** Stores the Last Will and Testament topic, message, and QoS for the next connection. */
void MqttClient::setLastWillAndTestament(const QString& topic, 
                                         const QByteArray& message, int qos) {
    QMutexLocker lock(&m_mutex);

    m_lastWillTopic = topic;
    m_lastWillMessage = message;
    m_lastWillQos = qos;

    emit debugMessage(QString("LWT configured: %1 (QoS %2)")
        .arg(topic).arg(qos));
}

/** Returns the current connection state enum value. */
MQTT::ConnectionState MqttClient::getConnectionState() const {
    return m_state;
}

/** Returns the last error message string. */
QString MqttClient::getLastError() const {
    return m_lastError;
}

/** Returns the number of topics currently subscribed to. */
int MqttClient::getSubscriptionCount() const {
    QMutexLocker lock(&m_mutex);
    return m_subscriptions.size();
}

/** Emits all current subscriptions via the debugMessage signal. */
void MqttClient::dumpSubscriptions() const {
    QMutexLocker lock(&m_mutex);

    emit debugMessage("=== MQTT Subscriptions ===");

    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
        emit debugMessage(QString("  %1 (QoS %2)").arg(it.key()).arg(it.value()));
    }

    emit debugMessage("========================");
}

/** Returns a formatted string with broker address, state, and subscription count. */
QString MqttClient::toString() const {
    QString state;

    switch (m_state) {
        case MQTT::ConnectionState::DISCONNECTED: state = "DISCONNECTED"; break;
        case MQTT::ConnectionState::CONNECTING: state = "CONNECTING"; break;
        case MQTT::ConnectionState::CONNECTED: state = "CONNECTED"; break;
        case MQTT::ConnectionState::DISCONNECTING: state = "DISCONNECTING"; break;
        case MQTT::ConnectionState::ERROR: state = "ERROR"; break;
    }

    return QString("MqttClient: %1:%2 (%3) - Subscriptions: %4")
        .arg(m_brokerHost).arg(m_brokerPort).arg(state)
        .arg(m_subscriptions.size());
}

// Private helper methods

/** Parses "sps/{RoomID}/cmd/{commandType}" topic format into its components. */
bool MqttClient::parseCommandTopic(const QString& topic, QString& roomId, QString& commandType) {
    // Parse: sps/{RoomID}/cmd/{commandType}
    QStringList parts = topic.split('/');

    if (parts.size() != 4 || parts[0] != "sps" || parts[2] != "cmd") {
        return false;
    }

    roomId = parts[1];
    commandType = parts[3];
    return true;
}

/** Parses "sps/{RoomID}/event/{eventType}" topic format into its components. */
bool MqttClient::parseEventTopic(const QString& topic, QString& roomId, QString& eventType) {
    // Parse: sps/{RoomID}/event/{eventType}
    QStringList parts = topic.split('/');

    if (parts.size() != 4 || parts[0] != "sps" || parts[2] != "event") {
        return false;
    }

    roomId = parts[1];
    eventType = parts[3];
    return true;
}

/** Parses a "sps/{RoomID}/status/..." topic and extracts the room ID. */
bool MqttClient::parseStatusTopic(const QString& topic, QString& roomId) {
    // Parse: sps/{RoomID}/status/...
    QStringList parts = topic.split('/');

    if (parts.size() < 4 || parts[0] != "sps" || parts[2] != "status") {
        return false;
    }

    roomId = parts[1];
    return true;
}

/** Replaces the "{RoomID}" placeholder in a topic template with the actual room ID. */
QString MqttClient::expandTopic(const QString& topicTemplate, const QString& roomId) {
    QString result = topicTemplate;
    return result.replace("{RoomID}", roomId);
}

// Slot callbacks

/** Sets state to CONNECTED and emits the connected signal. */
void MqttClient::onConnected() {
    m_state = MQTT::ConnectionState::CONNECTED;
    emit connected();
}

/** Sets state to DISCONNECTED and emits disconnected with a default reason. */
void MqttClient::onDisconnected() {
    m_state = MQTT::ConnectionState::DISCONNECTED;
    emit disconnected("Disconnected by broker");
}

/** Emits messageReceived and routes to command/status signals based on topic. */
void MqttClient::onMessageReceived(const QString& topic, const QByteArray& message) {
    emit messageReceived(topic, message);

    // Route to specific signals based on topic
    QString roomId, commandType;

    if (parseCommandTopic(topic, roomId, commandType)) {
        emit commandReceived(commandType, message);
    } else if (parseStatusTopic(topic, roomId)) {
        emit statusReceived(topic, message);
    }
}

/** Stores the error, sets state to ERROR, and emits a debug message. */
void MqttClient::onError(const QString& error) {
    m_lastError = error;
    m_state = MQTT::ConnectionState::ERROR;
    emit debugMessage(QString("MQTT Error: %1").arg(error));
}

/** Emits a debug message confirming the subscription. */
void MqttClient::onSubscribed(const QString& topic) {
    emit debugMessage(QString("Subscription confirmed: %1").arg(topic));
}

/** Emits a debug message confirming the unsubscription. */
void MqttClient::onUnsubscribed(const QString& topic) {
    emit debugMessage(QString("Unsubscription confirmed: %1").arg(topic));
}
