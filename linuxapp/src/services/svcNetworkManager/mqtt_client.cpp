#include "mqtt_client.h"
#include <QDebug>

MqttClient::MqttClient(QObject* parent)
    : QObject(parent), m_state(MQTT::ConnectionState::DISCONNECTED),
      m_brokerPort(MQTT::DEFAULT_BROKER_PORT),
      m_keepAliveInterval(MQTT::MQTT_KEEPALIVE_SECONDS),
      m_autoReconnect(true), m_retryIntervalMs(5000),
      m_lastWillQos(MQTT::QOS_AT_LEAST_ONCE),
      m_mqttClient(nullptr) {
}

MqttClient::~MqttClient() {
    if (isConnected()) {
        disconnect();
    }
}

// Connect to MQTT broker
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

// Disconnect from broker
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

// Check if connected
bool MqttClient::isConnected() const {
    return m_state == MQTT::ConnectionState::CONNECTED;
}

// Subscribe to topic
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

// Unsubscribe from topic
bool MqttClient::unsubscribe(const QString& topic) {
    if (m_subscriptions.remove(topic) > 0) {
        emit subscriptionChanged(topic, false);
        emit debugMessage(QString("Unsubscribed from: %1").arg(topic));
        return true;
    }

    return false;
}

// Publish message
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

// Publish device status
bool MqttClient::publishStatus(const QString& roomId, const QByteArray& payload) {
    QString topic = expandTopic(MQTT::TOPIC_STATUS_DEVICES, roomId);
    return publish(topic, payload, MQTT::QOS_FIRE_AND_FORGET, true);
}

// Publish event
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

// Set keep alive interval
void MqttClient::setKeepAliveInterval(int seconds) {
    m_keepAliveInterval = seconds;
}

// Set auto-reconnect
void MqttClient::setAutoReconnect(bool autoReconnect, int retryIntervalMs) {
    m_autoReconnect = autoReconnect;
    m_retryIntervalMs = retryIntervalMs;
}

// Set Last Will and Testament
void MqttClient::setLastWillAndTestament(const QString& topic, 
                                         const QByteArray& message, int qos) {
    QMutexLocker lock(&m_mutex);

    m_lastWillTopic = topic;
    m_lastWillMessage = message;
    m_lastWillQos = qos;

    emit debugMessage(QString("LWT configured: %1 (QoS %2)")
        .arg(topic).arg(qos));
}

// Get connection state
MQTT::ConnectionState MqttClient::getConnectionState() const {
    return m_state;
}

// Get last error
QString MqttClient::getLastError() const {
    return m_lastError;
}

// Get subscription count
int MqttClient::getSubscriptionCount() const {
    QMutexLocker lock(&m_mutex);
    return m_subscriptions.size();
}

// Dump subscriptions for debugging
void MqttClient::dumpSubscriptions() const {
    QMutexLocker lock(&m_mutex);

    emit debugMessage("=== MQTT Subscriptions ===");

    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
        emit debugMessage(QString("  %1 (QoS %2)").arg(it.key()).arg(it.value()));
    }

    emit debugMessage("========================");
}

// Get string representation
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

bool MqttClient::parseStatusTopic(const QString& topic, QString& roomId) {
    // Parse: sps/{RoomID}/status/...
    QStringList parts = topic.split('/');

    if (parts.size() < 4 || parts[0] != "sps" || parts[2] != "status") {
        return false;
    }

    roomId = parts[1];
    return true;
}

QString MqttClient::expandTopic(const QString& topicTemplate, const QString& roomId) {
    /*
	Problem:
	- `replace` is an non-const function, which will replace the passing argument
	- `topicTemplate` is a const, and must not be changed
	=> discards qualifiers
	Solution: create a copy of the `topicTemplate` - this is `result` variable. And the changes will be implement on this variable.
    */
    QString result = topicTemplate;
    return result.replace("{RoomID}", roomId);
}

// Slot callbacks

void MqttClient::onConnected() {
    m_state = MQTT::ConnectionState::CONNECTED;
    emit connected();
}

void MqttClient::onDisconnected() {
    m_state = MQTT::ConnectionState::DISCONNECTED;
    emit disconnected("Disconnected by broker");
}

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

void MqttClient::onError(const QString& error) {
    m_lastError = error;
    m_state = MQTT::ConnectionState::ERROR;
    emit debugMessage(QString("MQTT Error: %1").arg(error));
    // emit errorOccured(error);
}

void MqttClient::onSubscribed(const QString& topic) {
    emit debugMessage(QString("Subscription confirmed: %1").arg(topic));
}

void MqttClient::onUnsubscribed(const QString& topic) {
    emit debugMessage(QString("Unsubscription confirmed: %1").arg(topic));
}
