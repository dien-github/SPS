#ifndef MQTT_DEFINES_H
#define MQTT_DEFINES_H

#include <QString>

namespace MQTT {
    // Broker configuration
    constexpr const char* DEFAULT_BROKER_HOST = "localhost";
    constexpr int DEFAULT_BROKER_PORT = 1883;
    constexpr int MQTT_CONNECT_TIMEOUT_MS = 5000;
    constexpr int MQTT_KEEPALIVE_SECONDS = 60;

    // Topic templates
    const QString TOPIC_STATUS_DEVICES = "sps/{RoomID}/status/devices";
    const QString TOPIC_EVENT_AUTH = "sps/{RoomID}/event/auth";
    const QString TOPIC_CMD_PROJECTOR = "sps/{RoomID}/cmd/projector";
    const QString TOPIC_CMD_RELAY = "sps/{RoomID}/cmd/relay";
    const QString TOPIC_CMD_AC = "sps/{RoomID}/cmd/ac";
    const QString TOPIC_CMD_SYNC = "sps/{RoomID}/cmd/sync";
    const QString TOPIC_CMD_OTA = "sps/{RoomID}/cmd/ota";
    const QString TOPIC_EVENT_OTA = "sps/{RoomID}/event/ota";
    const QString TOPIC_STATUS_CONNECTION = "sps/{RoomID}/status/connection";

    // QoS levels
    constexpr int QOS_FIRE_AND_FORGET = 0;       // At most once
    constexpr int QOS_AT_LEAST_ONCE = 1;         // At least once
    constexpr int QOS_EXACTLY_ONCE = 2;          // Exactly once

    // MQTT Message callback types
    enum class MessageType {
        PUBLISH,
        SUBSCRIBE,
        COMMAND,
        STATUS,
        EVENT,
        UNKNOWN
    };

    // Connection states
    enum class ConnectionState {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        DISCONNECTING,
        ERROR
    };
}

#endif // MQTT_DEFINES_H
