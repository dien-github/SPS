#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QMutex>
#include <memory>
#include "mqtt_defines.h"

// MQTT Client wrapper
// This provides a Qt-friendly interface for MQTT communication with the server
// Supports pub/sub pattern for topic-based messaging
class MqttClient : public QObject {
    Q_OBJECT

public:
    explicit MqttClient(QObject* parent = nullptr);
    ~MqttClient();

    // Connection management
    bool connect(const QString& brokerHost = MQTT::DEFAULT_BROKER_HOST,
                int brokerPort = MQTT::DEFAULT_BROKER_PORT,
                const QString& clientId = "sps-device",
                const QString& username = "",
                const QString& password = "");

    bool disconnect();
    bool isConnected() const;

    // Subscribe to topics
    bool subscribe(const QString& topic, int qos = MQTT::QOS_AT_LEAST_ONCE);
    bool unsubscribe(const QString& topic);

    // Publish messages
    bool publish(const QString& topic, const QByteArray& payload,
                int qos = MQTT::QOS_AT_LEAST_ONCE, bool retain = false);

    bool publishStatus(const QString& roomId, const QByteArray& payload);
    bool publishEvent(const QString& roomId, const QString& eventType, const QByteArray& payload);

    // Configuration
    void setKeepAliveInterval(int seconds);
    void setAutoReconnect(bool autoReconnect, int retryIntervalMs = 5000);
    void setLastWillAndTestament(const QString& topic, const QByteArray& message, int qos);

    // Status queries
    MQTT::ConnectionState getConnectionState() const;
    QString getLastError() const;
    int getSubscriptionCount() const;

    // Room ID management
    void setRoomId(const QString& roomId) { m_roomId = roomId; }
    QString getRoomId() const { return m_roomId; }

    // Debug
    void dumpSubscriptions() const;
    QString toString() const;

signals:
    // Connection signals
    void connected();
    void disconnected(const QString& reason = "");
    void connectionFailed(const QString& error);
    void connectionLost(const QString& reason);
    // void errorOccured(const QString& error);

    // Message signals
    void messageReceived(const QString& topic, const QByteArray& payload);
    void commandReceived(const QString& commandType, const QByteArray& data);
    void statusReceived(const QString& topic, const QByteArray& status);
    void eventReceived(const QString& eventType, const QByteArray& data);

    // Status signals
    void subscriptionChanged(const QString& topic, bool subscribed);
    void publishFailed(const QString& topic, const QString& reason);

    // Debug signal
    void debugMessage(const QString& message) const;

private slots:
    void onConnected();
    void onDisconnected();
    void onMessageReceived(const QString& topic, const QByteArray& message);
    void onError(const QString& error);
    void onSubscribed(const QString& topic);
    void onUnsubscribed(const QString& topic);

private:
    // Helper methods
    bool parseCommandTopic(const QString& topic, QString& roomId, QString& commandType);
    bool parseEventTopic(const QString& topic, QString& roomId, QString& eventType);
    bool parseStatusTopic(const QString& topic, QString& roomId);

    // Expand topic templates
    QString expandTopic(const QString& topicTemplate, const QString& roomId);

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

    // MQTT client implementation
    // This would use paho-mqtt-c or Qt MQTT addon in real implementation
    // For now, this provides the interface
    void* m_mqttClient;  // Placeholder for actual MQTT client instance
};

#endif // MQTT_CLIENT_H
