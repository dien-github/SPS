#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QMutex>
#include <QByteArray>
#include <QTimer>
#include <QTcpSocket>
#include <memory>
#include "mqtt_defines.h"

/** Provides a Qt-friendly interface for MQTT communication with pub/sub support. */
class MqttClient : public QObject {
    Q_OBJECT

public:
    /** Creates an MQTT client in the disconnected state. */
    explicit MqttClient(QObject* parent = nullptr);
    /** Destroys the client and disconnects if connected. */
    ~MqttClient();

    /** Connects to the MQTT broker with the given credentials. */
    bool connect(const QString& brokerHost = MQTT::DEFAULT_BROKER_HOST,
                int brokerPort = MQTT::DEFAULT_BROKER_PORT,
                const QString& clientId = "sps-device",
                const QString& username = "",
                const QString& password = "");

    /** Disconnects from the MQTT broker and clears subscriptions. */
    bool disconnect();
    /** Returns true if currently connected to the broker. */
    bool isConnected() const;

    /** Subscribes to a topic at the given QoS level. */
    bool subscribe(const QString& topic, int qos = MQTT::QOS_AT_LEAST_ONCE);
    /** Unsubscribes from a topic. */
    bool unsubscribe(const QString& topic);

    /** Publishes a payload to the given topic with optional retain flag. */
    bool publish(const QString& topic, const QByteArray& payload,
                int qos = MQTT::QOS_AT_LEAST_ONCE, bool retain = false);

    /** Publishes a status payload to the room's device status topic. */
    bool publishStatus(const QString& roomId, const QByteArray& payload);
    /** Publishes an event payload to the room's event topic. */
    bool publishEvent(const QString& roomId, const QString& eventType, const QByteArray& payload);

    /** Sets the keep-alive interval in seconds for the MQTT connection. */
    void setKeepAliveInterval(int seconds);
    /** Configures auto-reconnect with the given retry interval. */
    void setAutoReconnect(bool autoReconnect, int retryIntervalMs = 5000);
    /** Sets the Last Will and Testament message for the MQTT connection. */
    void setLastWillAndTestament(const QString& topic, const QByteArray& message, int qos);

    /** Returns the current connection state enum value. */
    MQTT::ConnectionState getConnectionState() const;
    /** Returns the last error message string. */
    QString getLastError() const;
    /** Returns the number of active topic subscriptions. */
    int getSubscriptionCount() const;

    /** Sets the room identifier used in topic expansion. */
    void setRoomId(const QString& roomId) { m_roomId = roomId; }
    /** Returns the current room identifier. */
    QString getRoomId() const { return m_roomId; }

    /** Logs all current subscriptions to the debug signal. */
    void dumpSubscriptions() const;
    /** Returns a human-readable string representation of this client. */
    QString toString() const;

signals:
    /** Emitted when the connection to the broker is established. */
    void connected();
    /** Emitted when the client disconnects from the broker. */
    void disconnected(const QString& reason = "");
    /** Emitted when the connection attempt fails. */
    void connectionFailed(const QString& error);
    /** Emitted when an established connection is lost. */
    void connectionLost(const QString& reason);

    /** Emitted when a message is received on a subscribed topic. */
    void messageReceived(const QString& topic, const QByteArray& payload);
    /** Emitted when a command-type message is received. */
    void commandReceived(const QString& commandType, const QByteArray& data);
    /** Emitted when a status-type message is received. */
    void statusReceived(const QString& topic, const QByteArray& status);
    /** Emitted when an event-type message is received. */
    void eventReceived(const QString& eventType, const QByteArray& data);

    /** Emitted when a subscription is added or removed. */
    void subscriptionChanged(const QString& topic, bool subscribed);
    /** Emitted when a publish operation fails. */
    void publishFailed(const QString& topic, const QString& reason);

    /** Emitted with a debug message string for logging. */
    void debugMessage(const QString& message) const;

private slots:
    /** Internal: Handles a successful connection event. */
    void onConnected();
    /** Internal: Handles a disconnection event. */
    void onDisconnected();
    /** Internal: Handles an incoming message and routes it. */
    void onMessageReceived(const QString& topic, const QByteArray& message);
    /** Internal: Handles an error event. */
    void onError(const QString& error);
    /** Internal: Handles a subscription confirmation. */
    void onSubscribed(const QString& topic);
    /** Internal: Handles an unsubscription confirmation. */
    void onUnsubscribed(const QString& topic);
    /** Internal: Sends the MQTT CONNECT packet after TCP connects. */
    void onSocketConnected();
    /** Internal: Handles TCP socket disconnection. */
    void onSocketDisconnected();
    /** Internal: Handles TCP socket errors. */
    void onSocketError(QAbstractSocket::SocketError socketError);
    /** Internal: Reads and parses MQTT packets from the TCP socket. */
    void onSocketReadyRead();
    /** Internal: Sends MQTT keep-alive PINGREQ. */
    void sendPing();

private:
    /** Parses a command topic into room ID and command type. */
    bool parseCommandTopic(const QString& topic, QString& roomId, QString& commandType);
    /** Parses an event topic into room ID and event type. */
    bool parseEventTopic(const QString& topic, QString& roomId, QString& eventType);
    /** Parses a status topic and extracts the room ID. */
    bool parseStatusTopic(const QString& topic, QString& roomId);

    /** Replaces "{RoomID}" placeholder in a topic template. */
    QString expandTopic(const QString& topicTemplate, const QString& roomId);
    /** Writes an MQTT control packet with encoded remaining length. */
    bool writePacket(quint8 packetTypeAndFlags, const QByteArray& body);
    /** Builds and sends the MQTT CONNECT packet. */
    bool sendConnectPacket();
    /** Encodes MQTT variable-length remaining length bytes. */
    QByteArray encodeRemainingLength(int length) const;
    /** Appends an MQTT UTF-8 string field to a packet body. */
    void appendMqttString(QByteArray& body, const QString& value) const;
    /** Reads an MQTT UTF-8 string field from a byte array. */
    bool readMqttString(const QByteArray& data, int& offset, QString& value) const;
    /** Parses all complete MQTT packets currently in the receive buffer. */
    void processReceiveBuffer();
    /** Dispatches one decoded MQTT packet. */
    void handlePacket(quint8 packetTypeAndFlags, const QByteArray& body);
    /** Handles an incoming MQTT PUBLISH packet. */
    void handlePublish(quint8 packetTypeAndFlags, const QByteArray& body);
    /** Returns and increments the next MQTT packet identifier. */
    quint16 nextPacketId();

    // Internal state
    MQTT::ConnectionState m_state;
    QString m_brokerHost;
    int m_brokerPort;
    QString m_clientId;
    QString m_username;
    QString m_password;
    QString m_roomId;

    // Topic subscriptions
    QMap<QString, int> m_subscriptions;  // topic -> QoS

    // Configuration
    int m_keepAliveInterval;
    bool m_autoReconnect;
    int m_retryIntervalMs;
    QString m_lastWillTopic;
    QByteArray m_lastWillMessage;
    int m_lastWillQos;

    // Status
    QString m_lastError;
    mutable QMutex m_mutex;

    // MQTT socket implementation
    QTcpSocket* m_socket;
    QTimer m_keepAliveTimer;
    QByteArray m_rxBuffer;
    quint16 m_nextPacketId;
};

#endif // MQTT_CLIENT_H
