#ifndef UART_FRAME_H
#define UART_FRAME_H

#include <QByteArray>
#include <QObject>
#include <cstdint>
#include "uart_defines.h"

// UART frame parser and builder
class UartFrame {
public:
    /** Constructor. Initializes frame to defaults and prepares the CRC table. */
    UartFrame();
    /** Destructor. Default. */
    ~UartFrame() = default;

    // Build a frame for transmission
    /** Builds a complete UART frame (header + cmdId + seqId + payload + CRC) for transmission. */
    static QByteArray buildFrame(UART::CommandId cmdId, const QByteArray& payload, uint8_t seqId = 0);

    // Parse incoming frame bytes
    /** Parses raw byte data into a UartFrame, verifying header and CRC. */
    bool parseFrame(const QByteArray& data);

    // CRC-16/CCITT calculation
    /** Calculates CRC-16/CCITT over the given data. */
    static uint16_t calculateCrc16(const QByteArray& data);
    /** Computes CRC-16/CCITT with an optional initial value. */
    static uint16_t crc16(const QByteArray& data, uint16_t initialValue = 0xFFFF);

    // Getters
    /** Returns the command ID from the parsed frame. */
    UART::CommandId getCommandId() const { return m_cmdId; }
    /** Returns the transaction sequence ID from the parsed frame. */
    uint8_t getSequenceId() const { return m_seqId; }
    /** Returns the payload data from the parsed frame. */
    QByteArray getPayload() const { return m_payload; }
    /** Returns the payload length field. */
    uint8_t getLength() const { return m_length; }
    /** Returns the CRC-16 value from the frame. */
    uint16_t getCrc16() const { return m_crc16; }
    /** Returns true if the frame was parsed successfully. */
    bool isValid() const { return m_isValid; }
    /** Returns an error message if the frame is invalid. */
    QString getErrorMessage() const { return m_errorMessage; }

    // Setters
    /** Sets the command ID for building a frame. */
    void setCmdId(UART::CommandId id) { m_cmdId = id; }
    /** Sets the transaction sequence ID for building a frame. */
    void setSequenceId(uint8_t id) { m_seqId = id; }
    /** Sets the payload data for building a frame. */
    void setPayload(const QByteArray& data) { m_payload = data; }

    // Frame validation
    /** Checks if the data starts with a valid frame header. */
    static bool isValidHeader(const QByteArray& data);
    /** Finds the first occurrence of a valid frame header in the data. */
    static int findFrameStart(const QByteArray& data);

    // Helper methods
    /** Converts the frame to a human-readable debug string. */
    QString toString() const;
    /** Converts the frame back to a raw byte array for transmission. */
    QByteArray toByteArray() const;

private:
    // Frame components
    uint8_t m_header[2];
    uint8_t m_length;
    UART::CommandId m_cmdId;
    uint8_t m_seqId;
    QByteArray m_payload;
    uint16_t m_crc16;

    // Status
    bool m_isValid;
    QString m_errorMessage;

    // CRC lookup table for optimization
    static uint16_t crcTable[256];
    static bool crcTableInitialized;
    /** Initializes the CRC-16/CCITT lookup table (called once). */
    static void initCrcTable();
};

#endif // UART_FRAME_H
