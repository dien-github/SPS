#ifndef SPS_UART_PROTOCOL_H
#define SPS_UART_PROTOCOL_H

#include <QByteArray>
#include <cstdint>

namespace SPS::UART {

// Frame structure
constexpr uint8_t HEADER_BYTE_0 = 0xAA;
constexpr uint8_t HEADER_BYTE_1 = 0x55;
constexpr int HEADER_SIZE = 2;
constexpr int LENGTH_SIZE = 1;
constexpr int CMD_ID_SIZE = 1;
constexpr int SEQ_ID_SIZE = 1;
constexpr int CRC_SIZE = 2;
constexpr int MIN_FRAME_SIZE = HEADER_SIZE + LENGTH_SIZE + CMD_ID_SIZE + SEQ_ID_SIZE + CRC_SIZE;
constexpr int MAX_PAYLOAD_SIZE = 255;
constexpr int MAX_FRAME_SIZE = MIN_FRAME_SIZE + MAX_PAYLOAD_SIZE;

// Serial port settings
constexpr int BAUDRATE = 115200;
constexpr int DATA_BITS = 8;
constexpr int STOP_BITS = 1;
constexpr const char* PARITY = "None";
constexpr const char* FLOW_CONTROL = "None";
constexpr const char* DEFAULT_PORT = "/dev/ttyS0";
constexpr int READ_TIMEOUT_MS = 1000;
constexpr int WRITE_TIMEOUT_MS = 500;

enum class CommandId : uint8_t {
    PING_HEARTBEAT = 0x10,
    ACK_ALIVE = 0x11,
    NACK_ERROR = 0x12,

    LIGHT_CONTROL = 0x21,
    CURTAIN_CONTROL = 0x22,
    PROJECTOR_CONTROL = 0x23,
    AC_CONTROL = 0x24,
    AC_TEMP_UP = 0x25,
    AC_TEMP_DOWN = 0x26,

    QUERY_RELAY_STATUS = 0x32,
    PRESENCE_ALERT = 0x41,

    OTA_START = 0x50,
    OTA_DATA_CHUNK = 0x51,
    OTA_END = 0x52,

    INVALID = 0xFF
};

using CmdId = CommandId;

enum class ErrorCode : uint8_t {
    CRC_ERROR = 0x01,
    INVALID_PARAM = 0x02,
    UNSUPPORTED_CMD = 0x03,
    BUSY = 0x04,
    OTA_WRITE_FAILED = 0x05,
    TIMEOUT = 0x06,
};

enum class DeviceId : uint8_t {
    LIGHT_PODIUM = 0x01,
    LIGHT_CLASS = 0x02,
    LIGHT_ALL = 0xFF,
    CURTAIN = 0x01,
    SCREEN = 0x02,
    AC_ID = 0x01,
};

enum class ControlValue : uint8_t {
    OFF = 0x00,
    ON = 0x01,
    OPEN = 0x01,
    CLOSE = 0x00,
    STOP = 0x02,
};

namespace Payload {
constexpr int DEVICE_ID_OFFSET = 0;
constexpr int CONTROL_VALUE_OFFSET = 1;
constexpr int PROJECTOR_VALUE_OFFSET = 0;
constexpr int ACK_COMMAND_ID_OFFSET = 0;
constexpr int ACK_SEQUENCE_ID_OFFSET = 1;
constexpr int NACK_COMMAND_ID_OFFSET = 0;
constexpr int NACK_SEQUENCE_ID_OFFSET = 1;
constexpr int NACK_ERROR_CODE_OFFSET = 2;
constexpr int PRESENCE_VALUE_OFFSET = 0;
constexpr int OTA_SIZE_BYTES = 4;
constexpr int OTA_CHUNK_NUMBER_OFFSET = 0;
constexpr int OTA_CHUNK_INDEX_SIZE = 2;
constexpr int OTA_CHUNK_DATA_OFFSET = OTA_CHUNK_NUMBER_OFFSET + OTA_CHUNK_INDEX_SIZE;
constexpr int OTA_CHUNK_DATA_SIZE = 128;
}

inline uint8_t toByte(CommandId value) {
    return static_cast<uint8_t>(value);
}

inline uint8_t toByte(DeviceId value) {
    return static_cast<uint8_t>(value);
}

inline uint8_t toByte(ControlValue value) {
    return static_cast<uint8_t>(value);
}

inline uint8_t toByte(ErrorCode value) {
    return static_cast<uint8_t>(value);
}

inline uint8_t byteAt(const QByteArray& payload, int index) {
    return static_cast<uint8_t>(static_cast<unsigned char>(payload.at(index)));
}

inline QByteArray buildDeviceControlPayload(uint8_t deviceId, ControlValue value) {
    QByteArray payload;
    payload.append(static_cast<char>(deviceId));
    payload.append(static_cast<char>(toByte(value)));
    return payload;
}

inline QByteArray buildLightControlPayload(uint8_t lightId, bool on) {
    return buildDeviceControlPayload(lightId, on ? ControlValue::ON : ControlValue::OFF);
}

inline QByteArray buildCurtainControlPayload(uint8_t curtainId, ControlValue action) {
    return buildDeviceControlPayload(curtainId, action);
}

inline QByteArray buildProjectorControlPayload(bool on) {
    QByteArray payload;
    payload.append(static_cast<char>(toByte(on ? ControlValue::ON : ControlValue::OFF)));
    return payload;
}

inline QByteArray buildAcControlPayload(uint8_t acId, bool on) {
    return buildDeviceControlPayload(acId, on ? ControlValue::ON : ControlValue::OFF);
}

inline QByteArray buildAcTemperatureStepPayload(uint8_t acId) {
    QByteArray payload;
    payload.append(static_cast<char>(acId));
    return payload;
}

inline QByteArray buildQueryRelayStatusPayload(uint8_t deviceId) {
    QByteArray payload;
    payload.append(static_cast<char>(deviceId));
    return payload;
}

inline QByteArray buildOtaStartPayload(uint32_t firmwareSize) {
    QByteArray payload;
    payload.append(static_cast<char>(firmwareSize & 0xFF));
    payload.append(static_cast<char>((firmwareSize >> 8) & 0xFF));
    payload.append(static_cast<char>((firmwareSize >> 16) & 0xFF));
    payload.append(static_cast<char>((firmwareSize >> 24) & 0xFF));
    return payload;
}

inline QByteArray buildOtaDataChunkPayload(uint16_t chunkNumber, const QByteArray& chunkData) {
    QByteArray payload;
    payload.append(static_cast<char>(chunkNumber & 0xFF));
    payload.append(static_cast<char>((chunkNumber >> 8) & 0xFF));
    payload.append(chunkData);
    return payload;
}

inline QByteArray buildAckPayload(uint8_t originalCmdId, uint8_t originalSeqId) {
    QByteArray payload;
    payload.append(static_cast<char>(originalCmdId));
    payload.append(static_cast<char>(originalSeqId));
    return payload;
}

inline QByteArray buildNackPayload(uint8_t originalCmdId, uint8_t originalSeqId, ErrorCode errorCode) {
    QByteArray payload = buildAckPayload(originalCmdId, originalSeqId);
    payload.append(static_cast<char>(toByte(errorCode)));
    return payload;
}

inline bool parseAckPayload(const QByteArray& payload, uint8_t& originalCmdId, uint8_t& originalSeqId) {
    if (payload.size() <= Payload::ACK_SEQUENCE_ID_OFFSET) {
        return false;
    }
    originalCmdId = byteAt(payload, Payload::ACK_COMMAND_ID_OFFSET);
    originalSeqId = byteAt(payload, Payload::ACK_SEQUENCE_ID_OFFSET);
    return true;
}

inline bool parseNackPayload(const QByteArray& payload, uint8_t& errorCmdId, uint8_t& errorSeqId, uint8_t& errorCode) {
    if (payload.size() <= Payload::NACK_ERROR_CODE_OFFSET) {
        return false;
    }
    errorCmdId = byteAt(payload, Payload::NACK_COMMAND_ID_OFFSET);
    errorSeqId = byteAt(payload, Payload::NACK_SEQUENCE_ID_OFFSET);
    errorCode = byteAt(payload, Payload::NACK_ERROR_CODE_OFFSET);
    return true;
}

inline bool parseDeviceStatusPayload(const QByteArray& payload, uint8_t& deviceId, uint8_t& status) {
    if (payload.size() <= Payload::CONTROL_VALUE_OFFSET) {
        return false;
    }
    deviceId = byteAt(payload, Payload::DEVICE_ID_OFFSET);
    status = byteAt(payload, Payload::CONTROL_VALUE_OFFSET);
    return true;
}

inline bool parsePresencePayload(const QByteArray& payload, bool& isPresent) {
    if (payload.size() <= Payload::PRESENCE_VALUE_OFFSET) {
        return false;
    }
    isPresent = byteAt(payload, Payload::PRESENCE_VALUE_OFFSET) != 0;
    return true;
}

} // namespace SPS::UART

#endif // SPS_UART_PROTOCOL_H
