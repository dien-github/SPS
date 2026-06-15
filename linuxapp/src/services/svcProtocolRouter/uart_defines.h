#ifndef UART_DEFINES_H
#define UART_DEFINES_H

#include <cstdint>

// UART Frame Structure Constants
namespace UART {
    // Frame header bytes
    constexpr uint8_t HEADER_BYTE_0 = 0xAA;
    constexpr uint8_t HEADER_BYTE_1 = 0x55;
    constexpr int HEADER_SIZE = 2;
    constexpr int LENGTH_SIZE = 1;
    constexpr int CMD_ID_SIZE = 1;
    constexpr int CRC_SIZE = 2;
    constexpr int MIN_FRAME_SIZE = HEADER_SIZE + LENGTH_SIZE + CMD_ID_SIZE + CRC_SIZE;
    constexpr int MAX_PAYLOAD_SIZE = 256;
    constexpr int MAX_FRAME_SIZE = MIN_FRAME_SIZE + MAX_PAYLOAD_SIZE;

    // Serial port configuration
    constexpr int BAUDRATE = 115200;
    constexpr int DATA_BITS = 8;
    constexpr int STOP_BITS = 1;
    constexpr const char* PARITY = "None";
    constexpr const char* FLOW_CONTROL = "None";

    // UART Port defaults
    constexpr const char* DEFAULT_PORT = "/dev/ttyS0";  // RPi UART
    constexpr int READ_TIMEOUT_MS = 1000;
    constexpr int WRITE_TIMEOUT_MS = 500;

    // Command IDs
    enum class CommandId : uint8_t {
        // System commands
        PING_HEARTBEAT = 0x10,
        ACK_ALIVE = 0x11,
        NACK_ERROR = 0x12,

        // Device control
        LIGHT_CONTROL = 0x21,
        CURTAIN_CONTROL = 0x22,
        PROJECTOR_CONTROL = 0x23,
        AC_CONTROL = 0x24,

        // Query
        QUERY_RELAY_STATUS = 0x32,

        // Events
        PRESENCE_ALERT = 0x41,

        // OTA
        OTA_START = 0x50,
        OTA_DATA_CHUNK = 0x51,
        OTA_END = 0x52,

        // Invalid
        INVALID = 0xFF
    };

    // Error codes (for NACK)
    enum class ErrorCode : uint8_t {
        CRC_ERROR = 0x01,
        INVALID_PARAM = 0x02,
    };

    // Device IDs
    enum class DeviceId : uint8_t {
        LIGHT_PODIUM = 0x01,
        LIGHT_CLASS = 0x02,
        LIGHT_ALL = 0xFF,
        CURTAIN = 0x01,
        SCREEN = 0x02,
        AC_ID = 0x01,
    };

    // Control values
    enum class ControlValue : uint8_t {
        OFF = 0x00,
        ON = 0x01,
        OPEN = 0x01,
        CLOSE = 0x00,
        STOP = 0x02,
    };
}

#endif // UART_DEFINES_H
