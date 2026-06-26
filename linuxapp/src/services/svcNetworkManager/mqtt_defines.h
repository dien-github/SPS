#ifndef MQTT_DEFINES_H
#define MQTT_DEFINES_H

#include <QString>

/** Contains MQTT constants, topic templates, QoS levels, and enum types. */
namespace MQTT {
    /** Default MQTT broker hostname. */
    constexpr const char* DEFAULT_BROKER_HOST = "localhost";
    /** Default MQTT broker port. */
    constexpr int DEFAULT_BROKER_PORT = 1883;
    /** Timeout in milliseconds for MQTT connection attempts. */
    constexpr int MQTT_CONNECT_TIMEOUT_MS = 5000;
    /** Keep-alive interval in seconds for the MQTT connection. */
    constexpr int MQTT_KEEPALIVE_SECONDS = 60;

    /** Topic template for publishing device status. */
    const QString TOPIC_STATUS_DEVICES = "sps/{RoomID}/status/devices";
    /** Topic template for authentication events. */
    const QString TOPIC_EVENT_AUTH = "sps/{RoomID}/event/auth";
    /** Topic template for projector commands. */
    const QString TOPIC_CMD_PROJECTOR = "sps/{RoomID}/cmd/projector";
    /** Topic template for relay commands. */
    const QString TOPIC_CMD_RELAY = "sps/{RoomID}/cmd/relay";
    /** Topic template for AC commands. */
    const QString TOPIC_CMD_AC = "sps/{RoomID}/cmd/ac";
    /** Topic template for sync commands. */
    const QString TOPIC_CMD_SYNC = "sps/{RoomID}/cmd/sync";
    /** Topic template for OTA commands. */
    const QString TOPIC_CMD_OTA = "sps/{RoomID}/cmd/ota";
    /** Topic template for OTA events. */
    const QString TOPIC_EVENT_OTA = "sps/{RoomID}/event/ota";
    /** Topic template for connection status updates. */
    const QString TOPIC_STATUS_CONNECTION = "sps/{RoomID}/status/connection";

    /** QoS 0: At most once delivery (fire and forget). */
    constexpr int QOS_FIRE_AND_FORGET = 0;
    /** QoS 1: At least once delivery (acknowledged). */
    constexpr int QOS_AT_LEAST_ONCE = 1;
    /** QoS 2: Exactly once delivery (guaranteed). */
    constexpr int QOS_EXACTLY_ONCE = 2;

    /** Categorizes an MQTT message by its purpose. */
    enum class MessageType {
        PUBLISH,    /**< A published data message. */
        SUBSCRIBE,  /**< A subscription request. */
        COMMAND,    /**< A device command. */
        STATUS,     /**< A status update. */
        EVENT,      /**< An event notification. */
        UNKNOWN     /**< Unrecognized message type. */
    };

    /** Represents the current state of the MQTT connection. */
    enum class ConnectionState {
        DISCONNECTED,  /**< Not connected to the broker. */
        CONNECTING,    /**< Connection attempt in progress. */
        CONNECTED,     /**< Successfully connected to the broker. */
        DISCONNECTING, /**< Disconnection in progress. */
        ERROR          /**< An error occurred on the connection. */
    };
}

#endif // MQTT_DEFINES_H
