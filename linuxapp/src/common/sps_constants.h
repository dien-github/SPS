#ifndef SPS_CONSTANTS_H
#define SPS_CONSTANTS_H

#include <QString>
#include <QMap>

// D-Bus Service Names and Paths
namespace SPS::DBus {
    // Service identifiers
    const QString SERVICE_AUTH = "com.sps.auth";
    const QString SERVICE_ROUTER = "com.sps.router";
    const QString SERVICE_ENGINE = "com.sps.engine";
    const QString SERVICE_NETMGR = "com.sps.netmgr";

    // Object paths
    const QString PATH_AUTH = "/com/sps/auth";
    const QString PATH_ROUTER = "/com/sps/router";
    const QString PATH_ENGINE = "/com/sps/engine";
    const QString PATH_NETMGR = "/com/sps/netmgr";

    // Interface names
    const QString IFACE_AUTH = "com.sps.auth";
    const QString IFACE_ROUTER = "com.sps.router";
    const QString IFACE_ENGINE = "com.sps.engine";
    const QString IFACE_NETMGR = "com.sps.netmgr";
    const QString IFACE_OTAMGR = "com.sps.otamanager";
}

// OTA Manager Constants
namespace SPS::OTA {
    const QString SERVICE_OTAMGR = "com.sps.otamanager";
    const QString PATH_OTAMGR = "/com/sps/otamanager";
    const QString IFACE_OTAMGR = "com.sps.otamanager";

    // Update stages
    const QString STAGE_IDLE = "IDLE";
    const QString STAGE_DOWNLOADING_MCU = "DOWNLOADING_MCU";
    const QString STAGE_FLASHING_MCU = "FLASHING_MCU";
    const QString STAGE_DOWNLOADING_APPS = "DOWNLOADING_APPS";
    const QString STAGE_UPDATING_APPS = "UPDATING_APPS";
    const QString STAGE_VERIFYING = "VERIFYING";
    const QString STAGE_COMPLETED = "COMPLETED";
    const QString STAGE_FAILED = "FAILED";

    // Default paths
    const QString DOWNLOAD_DIR = "/opt/sps/updates";
    const QString VERSION_FILE = "/opt/sps/config/version.json";
    const QString SERVICE_INSTALL_DIR = "/opt/sps/bin";

    // Timeouts
    const int UPDATE_TIMEOUT_MS = 300000;
    const int VERSION_CHECK_INTERVAL_MS = 3600000;

    // MCU OTA params
    const int MCU_CHUNK_SIZE = 128;
}

// UART Protocol Constants
namespace SPS::UART {
    // Frame structure
    const unsigned char HEADER_BYTE0 = 0xAA;
    const unsigned char HEADER_BYTE1 = 0x55;
    const int HEADER_SIZE = 2;
    const int LENGTH_SIZE = 1;
    const int CMD_ID_SIZE = 1;
    const int CRC_SIZE = 2;
    const int MIN_FRAME_SIZE = HEADER_SIZE + LENGTH_SIZE + CMD_ID_SIZE + CRC_SIZE;
    const int MAX_PAYLOAD_SIZE = 256;
    const int MAX_FRAME_SIZE = MIN_FRAME_SIZE + MAX_PAYLOAD_SIZE;

    // Serial port settings
    const int BAUDRATE = 115200;
    const int DATA_BITS = 8;
    const int STOP_BITS = 1;
    const QString PARITY = "None";
    const QString FLOW_CONTROL = "None";

    // Command IDs
    enum class CmdId : unsigned char {
        PING_HEARTBEAT = 0x10,
        ACK_ALIVE = 0x11,
        NACK_ERROR = 0x12,
        LIGHT_CONTROL = 0x21,
        CURTAIN_CONTROL = 0x22,
        PROJECTOR_CONTROL = 0x23,
        AC_CONTROL = 0x24,
        QUERY_RELAY_STATUS = 0x32,
        PRESENCE_ALERT = 0x41,
        OTA_START = 0x50,
        OTA_DATA_CHUNK = 0x51,
        OTA_END = 0x52,
    };

    // Error codes
    enum class ErrorCode : unsigned char {
        CRC_ERROR = 0x01,
        INVALID_PARAM = 0x02,
    };
}

// MQTT Constants
namespace SPS::MQTT {
    const QString BROKER_HOST = "localhost";
    const int BROKER_PORT = 1883;
    const int QOS_FIRE_AND_FORGET = 0;
    const int QOS_AT_LEAST_ONCE = 1;
}

// Device Types
namespace SPS::Device {
    enum class Type {
        LIGHT, CURTAIN, SCREEN, PROJECTOR, AC, RELAY, UNKNOWN
    };

    enum class State {
        OFF, ON, OPENING, CLOSING, OPEN, CLOSED, UNKNOWN
    };
}

// Auth Status
namespace SPS::Auth {
    enum class Status { LOCKED, UNLOCKING, UNLOCKED, LOCKING, ERROR };
}

#endif // SPS_CONSTANTS_H
