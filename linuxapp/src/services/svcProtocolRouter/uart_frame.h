#ifndef UART_FRAME_H
#define UART_FRAME_H

#include <QByteArray>
#include <QObject>
#include <cstdint>
#include "uart_defines.h"

// UART frame parser and builder
class UartFrame {
public:
    UartFrame();
    ~UartFrame() = default;

    // Build a frame for transmission
    static QByteArray buildFrame(UART::CommandId cmdId, const QByteArray& payload);

    // Parse incoming frame bytes
    bool parseFrame(const QByteArray& data);

    // CRC-16/CCITT calculation
    static uint16_t calculateCrc16(const QByteArray& data);
    static uint16_t crc16(const QByteArray& data, uint16_t initialValue = 0xFFFF);

    // Getters
    UART::CommandId getCommandId() const { return m_cmdId; }
    QByteArray getPayload() const { return m_payload; }
    uint8_t getLength() const { return m_length; }
    uint16_t getCrc16() const { return m_crc16; }
    bool isValid() const { return m_isValid; }
    QString getErrorMessage() const { return m_errorMessage; }

    // Setters
    void setCmdId(UART::CommandId id) { m_cmdId = id; }
    void setPayload(const QByteArray& data) { m_payload = data; }

    // Frame validation
    static bool isValidHeader(const QByteArray& data);
    static int findFrameStart(const QByteArray& data);

    // Helper methods
    QString toString() const;
    QByteArray toByteArray() const;

private:
    // Frame components
    uint8_t m_header[2];
    uint8_t m_length;
    UART::CommandId m_cmdId;
    QByteArray m_payload;
    uint16_t m_crc16;

    // Status
    bool m_isValid;
    QString m_errorMessage;

    // CRC lookup table for optimization
    static uint16_t crcTable[256];
    static bool crcTableInitialized;
    static void initCrcTable();
};

#endif // UART_FRAME_H
