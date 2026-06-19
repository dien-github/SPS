#include "mqtt_client.h"

#include <QAbstractSocket>
#include <QDebug>
#include <QStringList>
#include <QtGlobal>

namespace {
constexpr quint8 MQTT_PKT_CONNECT = 0x10;
constexpr quint8 MQTT_PKT_CONNACK = 0x20;
constexpr quint8 MQTT_PKT_PUBLISH = 0x30;
constexpr quint8 MQTT_PKT_PUBACK = 0x40;
constexpr quint8 MQTT_PKT_SUBSCRIBE = 0x82;
constexpr quint8 MQTT_PKT_SUBACK = 0x90;
constexpr quint8 MQTT_PKT_UNSUBSCRIBE = 0xA2;
constexpr quint8 MQTT_PKT_UNSUBACK = 0xB0;
constexpr quint8 MQTT_PKT_PINGREQ = 0xC0;
constexpr quint8 MQTT_PKT_PINGRESP = 0xD0;
constexpr quint8 MQTT_PKT_DISCONNECT = 0xE0;

void appendUInt16(QByteArray& body, quint16 value) {
    body.append(static_cast<char>((value >> 8) & 0xFF));
    body.append(static_cast<char>(value & 0xFF));
}

quint16 readUInt16(const QByteArray& data, int offset) {
    return static_cast<quint16>(
        (static_cast<quint8>(data[offset]) << 8) |
        static_cast<quint8>(data[offset + 1]));
}
} // namespace

/** Initializes the MQTT client in DISCONNECTED state with default values. */
MqttClient::MqttClient(QObject* parent)
    : QObject(parent), m_state(MQTT::ConnectionState::DISCONNECTED),
      m_brokerPort(MQTT::DEFAULT_BROKER_PORT),
      m_keepAliveInterval(MQTT::MQTT_KEEPALIVE_SECONDS),
      m_autoReconnect(true), m_retryIntervalMs(5000),
      m_lastWillQos(MQTT::QOS_AT_LEAST_ONCE),
      m_socket(new QTcpSocket(this)),
      m_nextPacketId(1) {
    QObject::connect(m_socket, &QTcpSocket::connected,
                     this, &MqttClient::onSocketConnected);
    QObject::connect(m_socket, &QTcpSocket::disconnected,
                     this, &MqttClient::onSocketDisconnected);
    QObject::connect(m_socket, &QTcpSocket::readyRead,
                     this, &MqttClient::onSocketReadyRead);
    QObject::connect(m_socket, &QTcpSocket::errorOccurred,
                     this, &MqttClient::onSocketError);
    QObject::connect(&m_keepAliveTimer, &QTimer::timeout,
                     this, &MqttClient::sendPing);
}

/** Disconnects from the broker if currently connected. */
MqttClient::~MqttClient() {
    if (isConnected()) {
        disconnect();
    }
}

/** Starts a TCP connection and sends MQTT CONNECT once the socket is established. */
bool MqttClient::connect(const QString& brokerHost, int brokerPort,
                         const QString& clientId, const QString& username,
                         const QString& password) {
    if (m_state == MQTT::ConnectionState::CONNECTED ||
        m_state == MQTT::ConnectionState::CONNECTING) {
        m_lastError = "Already connected or connecting";
        return false;
    }

    {
        QMutexLocker lock(&m_mutex);
        m_brokerHost = brokerHost;
        m_brokerPort = brokerPort;
        m_clientId = clientId.isEmpty() ? "sps-device" : clientId;
        m_username = username;
        m_password = password;
        m_lastError.clear();
        m_rxBuffer.clear();
        m_state = MQTT::ConnectionState::CONNECTING;
    }

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }

    emit debugMessage(QString("Connecting to MQTT broker %1:%2 as %3")
        .arg(m_brokerHost).arg(m_brokerPort).arg(m_clientId));

    m_socket->connectToHost(m_brokerHost, static_cast<quint16>(m_brokerPort));
    return true;
}

/** Sends MQTT DISCONNECT and closes the TCP socket. */
bool MqttClient::disconnect() {
    if (m_state == MQTT::ConnectionState::DISCONNECTED) {
        return true;
    }

    m_keepAliveTimer.stop();
    m_state = MQTT::ConnectionState::DISCONNECTING;

    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        writePacket(MQTT_PKT_DISCONNECT, QByteArray());
        m_socket->disconnectFromHost();
    } else {
        m_socket->abort();
        m_state = MQTT::ConnectionState::DISCONNECTED;
        emit disconnected("User disconnect");
    }

    return true;
}

/** Returns true if the MQTT connection state is CONNECTED. */
bool MqttClient::isConnected() const {
    return m_state == MQTT::ConnectionState::CONNECTED;
}

/** Sends a SUBSCRIBE packet and stores the requested subscription. */
bool MqttClient::subscribe(const QString& topic, int qos) {
    if (!isConnected()) {
        m_lastError = "Not connected to broker";
        emit publishFailed(topic, m_lastError);
        return false;
    }

    const quint16 packetId = nextPacketId();
    const int requestedQos = qBound(0, qos, 1);

    QByteArray body;
    appendUInt16(body, packetId);
    appendMqttString(body, topic);
    body.append(static_cast<char>(requestedQos));

    if (!writePacket(MQTT_PKT_SUBSCRIBE, body)) {
        return false;
    }

    {
        QMutexLocker lock(&m_mutex);
        m_subscriptions[topic] = requestedQos;
    }

    emit subscriptionChanged(topic, true);
    emit debugMessage(QString("Subscribed to: %1 (QoS %2)").arg(topic).arg(requestedQos));
    return true;
}

/** Sends an UNSUBSCRIBE packet and removes the topic from local tracking. */
bool MqttClient::unsubscribe(const QString& topic) {
    if (!isConnected()) {
        return false;
    }

    const quint16 packetId = nextPacketId();
    QByteArray body;
    appendUInt16(body, packetId);
    appendMqttString(body, topic);

    if (!writePacket(MQTT_PKT_UNSUBSCRIBE, body)) {
        return false;
    }

    {
        QMutexLocker lock(&m_mutex);
        m_subscriptions.remove(topic);
    }

    emit subscriptionChanged(topic, false);
    emit debugMessage(QString("Unsubscribed from: %1").arg(topic));
    return true;
}

/** Publishes a payload to the broker using MQTT QoS 0 or 1. */
bool MqttClient::publish(const QString& topic, const QByteArray& payload,
                         int qos, bool retain) {
    if (!isConnected()) {
        m_lastError = "Not connected to broker";
        emit publishFailed(topic, m_lastError);
        return false;
    }

    const int actualQos = qBound(0, qos, 1);
    QByteArray body;
    appendMqttString(body, topic);
    if (actualQos > 0) {
        appendUInt16(body, nextPacketId());
    }
    body.append(payload);

    quint8 flags = MQTT_PKT_PUBLISH;
    flags |= static_cast<quint8>(actualQos << 1);
    if (retain) {
        flags |= 0x01;
    }

    const bool ok = writePacket(flags, body);
    if (ok) {
        emit debugMessage(QString("Publishing to %1: %2 bytes (QoS %3)")
            .arg(topic).arg(payload.size()).arg(actualQos));
    } else {
        emit publishFailed(topic, m_lastError);
    }
    return ok;
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

/** Sets the keep-alive interval in seconds for the connection. */
void MqttClient::setKeepAliveInterval(int seconds) {
    m_keepAliveInterval = qMax(5, seconds);
}

/** Enables or disables auto-reconnect metadata used by higher-level services. */
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
    m_lastWillQos = qBound(0, qos, 1);

    emit debugMessage(QString("LWT configured: %1 (QoS %2)")
        .arg(topic).arg(m_lastWillQos));
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

/** Parses "sps/{RoomID}/cmd/{commandType}" topic format into its components. */
bool MqttClient::parseCommandTopic(const QString& topic, QString& roomId, QString& commandType) {
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

/** Writes an MQTT packet to the socket. */
bool MqttClient::writePacket(quint8 packetTypeAndFlags, const QByteArray& body) {
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        m_lastError = "Socket is not connected";
        return false;
    }

    QByteArray packet;
    packet.append(static_cast<char>(packetTypeAndFlags));
    packet.append(encodeRemainingLength(body.size()));
    packet.append(body);

    const qint64 written = m_socket->write(packet);
    if (written != packet.size()) {
        m_lastError = "Failed to write complete MQTT packet";
        return false;
    }

    return true;
}

/** Builds and sends the MQTT CONNECT packet. */
bool MqttClient::sendConnectPacket() {
    QByteArray variableHeader;
    appendMqttString(variableHeader, "MQTT");
    variableHeader.append(static_cast<char>(4)); // MQTT 3.1.1

    quint8 connectFlags = 0x02; // clean session
    if (!m_lastWillTopic.isEmpty()) {
        connectFlags |= 0x04;
        connectFlags |= static_cast<quint8>((m_lastWillQos & 0x03) << 3);
    }
    if (!m_password.isEmpty()) {
        connectFlags |= 0x40;
    }
    if (!m_username.isEmpty()) {
        connectFlags |= 0x80;
    }
    variableHeader.append(static_cast<char>(connectFlags));
    appendUInt16(variableHeader, static_cast<quint16>(m_keepAliveInterval));

    QByteArray payload;
    appendMqttString(payload, m_clientId);
    if (!m_lastWillTopic.isEmpty()) {
        appendMqttString(payload, m_lastWillTopic);
        appendUInt16(payload, static_cast<quint16>(m_lastWillMessage.size()));
        payload.append(m_lastWillMessage);
    }
    if (!m_username.isEmpty()) {
        appendMqttString(payload, m_username);
    }
    if (!m_password.isEmpty()) {
        appendMqttString(payload, m_password);
    }

    return writePacket(MQTT_PKT_CONNECT, variableHeader + payload);
}

/** Encodes MQTT variable length integers. */
QByteArray MqttClient::encodeRemainingLength(int length) const {
    QByteArray encoded;
    do {
        quint8 byte = static_cast<quint8>(length % 128);
        length /= 128;
        if (length > 0) {
            byte |= 0x80;
        }
        encoded.append(static_cast<char>(byte));
    } while (length > 0);
    return encoded;
}

/** Appends an MQTT UTF-8 string field. */
void MqttClient::appendMqttString(QByteArray& body, const QString& value) const {
    const QByteArray utf8 = value.toUtf8();
    appendUInt16(body, static_cast<quint16>(utf8.size()));
    body.append(utf8);
}

/** Reads an MQTT UTF-8 string field. */
bool MqttClient::readMqttString(const QByteArray& data, int& offset, QString& value) const {
    if (offset + 2 > data.size()) {
        return false;
    }

    const quint16 length = readUInt16(data, offset);
    offset += 2;
    if (offset + length > data.size()) {
        return false;
    }

    value = QString::fromUtf8(data.constData() + offset, length);
    offset += length;
    return true;
}

/** Returns and increments a non-zero packet identifier. */
quint16 MqttClient::nextPacketId() {
    if (m_nextPacketId == 0) {
        m_nextPacketId = 1;
    }
    return m_nextPacketId++;
}

/** Handles TCP connected by sending MQTT CONNECT. */
void MqttClient::onSocketConnected() {
    if (!sendConnectPacket()) {
        onError(m_lastError.isEmpty() ? "Failed to send CONNECT" : m_lastError);
        emit connectionFailed(m_lastError);
        m_socket->disconnectFromHost();
    }
}

/** Handles TCP disconnection and emits the client-level signal. */
void MqttClient::onSocketDisconnected() {
    const bool wasUserDisconnect = m_state == MQTT::ConnectionState::DISCONNECTING;
    m_keepAliveTimer.stop();
    m_state = MQTT::ConnectionState::DISCONNECTED;

    if (wasUserDisconnect) {
        emit disconnected("User disconnect");
    } else {
        emit connectionLost("Disconnected by broker");
        emit disconnected("Disconnected by broker");
    }
}

/** Handles socket errors. */
void MqttClient::onSocketError(QAbstractSocket::SocketError socketError) {
    Q_UNUSED(socketError)

    const QString error = m_socket->errorString();
    const bool wasConnecting = m_state == MQTT::ConnectionState::CONNECTING;
    onError(error);

    if (wasConnecting) {
        emit connectionFailed(error);
    } else {
        emit connectionLost(error);
    }
}

/** Reads all pending data and parses complete MQTT packets. */
void MqttClient::onSocketReadyRead() {
    m_rxBuffer.append(m_socket->readAll());
    processReceiveBuffer();
}

/** Sends MQTT PINGREQ for keep-alive. */
void MqttClient::sendPing() {
    if (isConnected()) {
        writePacket(MQTT_PKT_PINGREQ, QByteArray());
    }
}

/** Parses all complete MQTT packets currently buffered. */
void MqttClient::processReceiveBuffer() {
    while (m_rxBuffer.size() >= 2) {
        const quint8 fixedHeader = static_cast<quint8>(m_rxBuffer[0]);
        int multiplier = 1;
        int remainingLength = 0;
        int encodedBytes = 0;

        quint8 encodedByte = 0;
        do {
            const int index = 1 + encodedBytes;
            if (index >= m_rxBuffer.size()) {
                return;
            }

            encodedByte = static_cast<quint8>(m_rxBuffer[index]);
            remainingLength += (encodedByte & 127) * multiplier;
            multiplier *= 128;
            encodedBytes++;

            if (encodedBytes > 4) {
                onError("Malformed MQTT remaining length");
                m_socket->disconnectFromHost();
                return;
            }
        } while ((encodedByte & 128) != 0);

        const int headerSize = 1 + encodedBytes;
        const int packetSize = headerSize + remainingLength;
        if (m_rxBuffer.size() < packetSize) {
            return;
        }

        const QByteArray body = m_rxBuffer.mid(headerSize, remainingLength);
        m_rxBuffer.remove(0, packetSize);
        handlePacket(fixedHeader, body);
    }
}

/** Dispatches one decoded MQTT packet. */
void MqttClient::handlePacket(quint8 packetTypeAndFlags, const QByteArray& body) {
    const quint8 packetType = packetTypeAndFlags & 0xF0;

    switch (packetType) {
        case MQTT_PKT_CONNACK:
            if (body.size() < 2) {
                onError("Malformed CONNACK");
                return;
            }
            if (static_cast<quint8>(body[1]) == 0) {
                onConnected();
            } else {
                m_lastError = QString("MQTT CONNACK rejected with code %1")
                    .arg(static_cast<quint8>(body[1]));
                m_state = MQTT::ConnectionState::ERROR;
                emit connectionFailed(m_lastError);
                m_socket->disconnectFromHost();
            }
            break;
        case MQTT_PKT_PUBLISH:
            handlePublish(packetTypeAndFlags, body);
            break;
        case MQTT_PKT_SUBACK:
            emit debugMessage("MQTT SUBACK received");
            break;
        case MQTT_PKT_UNSUBACK:
            emit debugMessage("MQTT UNSUBACK received");
            break;
        case MQTT_PKT_PINGRESP:
            emit debugMessage("MQTT PINGRESP received");
            break;
        case MQTT_PKT_PUBACK:
            emit debugMessage("MQTT PUBACK received");
            break;
        default:
            emit debugMessage(QString("Unhandled MQTT packet type 0x%1")
                .arg(packetType, 2, 16, QChar('0')));
            break;
    }
}

/** Handles an incoming MQTT PUBLISH packet. */
void MqttClient::handlePublish(quint8 packetTypeAndFlags, const QByteArray& body) {
    int offset = 0;
    QString topic;
    if (!readMqttString(body, offset, topic)) {
        onError("Malformed PUBLISH topic");
        return;
    }

    const int qos = (packetTypeAndFlags >> 1) & 0x03;
    quint16 packetId = 0;
    if (qos > 0) {
        if (offset + 2 > body.size()) {
            onError("Malformed PUBLISH packet identifier");
            return;
        }
        packetId = readUInt16(body, offset);
        offset += 2;
    }

    const QByteArray payload = body.mid(offset);

    if (qos == 1) {
        QByteArray puback;
        appendUInt16(puback, packetId);
        writePacket(MQTT_PKT_PUBACK, puback);
    }

    onMessageReceived(topic, payload);
}

/** Sets state to CONNECTED and emits the connected signal. */
void MqttClient::onConnected() {
    m_state = MQTT::ConnectionState::CONNECTED;
    m_keepAliveTimer.start(m_keepAliveInterval * 1000);
    emit connected();
}

/** Sets state to DISCONNECTED and emits disconnected with a default reason. */
void MqttClient::onDisconnected() {
    onSocketDisconnected();
}

/** Emits messageReceived and routes to command/status/event signals based on topic. */
void MqttClient::onMessageReceived(const QString& topic, const QByteArray& message) {
    emit messageReceived(topic, message);

    QString roomId;
    QString commandType;
    QString eventType;

    if (parseCommandTopic(topic, roomId, commandType)) {
        emit commandReceived(commandType, message);
    } else if (parseStatusTopic(topic, roomId)) {
        emit statusReceived(topic, message);
    } else if (parseEventTopic(topic, roomId, eventType)) {
        emit eventReceived(eventType, message);
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
