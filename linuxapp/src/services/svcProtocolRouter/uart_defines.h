#ifndef UART_DEFINES_H
#define UART_DEFINES_H

#include <cstdint>
#include "sps_uart_protocol.h"

// UART Frame Structure Constants
namespace UART {
    // Frame header bytes
    constexpr uint8_t HEADER_BYTE_0 = SPS::UART::HEADER_BYTE_0;
    constexpr uint8_t HEADER_BYTE_1 = SPS::UART::HEADER_BYTE_1;
    constexpr int HEADER_SIZE = SPS::UART::HEADER_SIZE;
    constexpr int LENGTH_SIZE = SPS::UART::LENGTH_SIZE;
    constexpr int CMD_ID_SIZE = SPS::UART::CMD_ID_SIZE;
    constexpr int SEQ_ID_SIZE = SPS::UART::SEQ_ID_SIZE;
    constexpr int CRC_SIZE = SPS::UART::CRC_SIZE;
    constexpr int MIN_FRAME_SIZE = SPS::UART::MIN_FRAME_SIZE;
    constexpr int MAX_PAYLOAD_SIZE = SPS::UART::MAX_PAYLOAD_SIZE;
    constexpr int MAX_FRAME_SIZE = SPS::UART::MAX_FRAME_SIZE;

    // Serial port configuration
    constexpr int BAUDRATE = SPS::UART::BAUDRATE;
    constexpr int DATA_BITS = SPS::UART::DATA_BITS;
    constexpr int STOP_BITS = SPS::UART::STOP_BITS;
    constexpr const char* PARITY = SPS::UART::PARITY;
    constexpr const char* FLOW_CONTROL = SPS::UART::FLOW_CONTROL;

    // UART Port defaults
    constexpr const char* DEFAULT_PORT = SPS::UART::DEFAULT_PORT;  // RPi UART
    constexpr int READ_TIMEOUT_MS = SPS::UART::READ_TIMEOUT_MS;
    constexpr int WRITE_TIMEOUT_MS = SPS::UART::WRITE_TIMEOUT_MS;

    // Command IDs
    using CommandId = SPS::UART::CommandId;

    // Error codes (for NACK)
    using ErrorCode = SPS::UART::ErrorCode;

    // Device IDs
    using DeviceId = SPS::UART::DeviceId;

    // Control values
    using ControlValue = SPS::UART::ControlValue;
}

#endif // UART_DEFINES_H
