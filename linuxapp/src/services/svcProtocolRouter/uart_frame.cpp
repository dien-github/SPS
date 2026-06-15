#include "uart_frame.h"
#include <QString>
#include <QDebug>
#include <cstring>

// Static CRC table initialization
uint16_t UartFrame::crcTable[256] = {0};
bool UartFrame::crcTableInitialized = false;

UartFrame::UartFrame()
    : m_length(0), m_cmdId(UART::CommandId::INVALID), m_crc16(0), m_isValid(false) {
    m_header[0] = UART::HEADER_BYTE_0;
    m_header[1] = UART::HEADER_BYTE_1;
    initCrcTable();
}

// Initialize CRC-16/CCITT lookup table
void UartFrame::initCrcTable() {
    if (crcTableInitialized) {
        return;
    }

    const uint16_t POLY = 0x1021;

    for (int i = 0; i < 256; ++i) {
        uint16_t crc = (static_cast<uint16_t>(i) << 8);

        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ POLY;
            } else {
                crc = (crc << 1);
            }
            crc &= 0xFFFF;
        }

        crcTable[i] = crc;
    }

    crcTableInitialized = true;
}

// Calculate CRC-16/CCITT for data
uint16_t UartFrame::calculateCrc16(const QByteArray& data) {
    return crc16(data);
}

uint16_t UartFrame::crc16(const QByteArray& data, uint16_t initialValue) {
    uint16_t crc = initialValue;

    for (uint8_t byte : data) {
        uint8_t idx = (static_cast<uint8_t>(crc >> 8)) ^ byte;
        crc = ((crc << 8) ^ crcTable[idx]) & 0xFFFF;
    }

    return crc;
}

// Build frame for transmission
QByteArray UartFrame::buildFrame(UART::CommandId cmdId, const QByteArray& payload) {
    QByteArray frame;
    uint8_t length = payload.size();

    // Build frame without CRC
    QByteArray frameToCrc;
    frameToCrc.append(static_cast<char>(UART::HEADER_BYTE_0));
    frameToCrc.append(static_cast<char>(UART::HEADER_BYTE_1));
    frameToCrc.append(static_cast<char>(length));
    frameToCrc.append(static_cast<char>(cmdId));
    frameToCrc.append(payload);

    // Calculate CRC
    uint16_t crc = calculateCrc16(frameToCrc);

    // Add header
    frame.append(static_cast<char>(UART::HEADER_BYTE_0));
    frame.append(static_cast<char>(UART::HEADER_BYTE_1));

    // Add length
    frame.append(static_cast<char>(length));

    // Add command ID
    frame.append(static_cast<char>(cmdId));

    // Add payload
    frame.append(payload);

    // Add CRC (big-endian)
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    frame.append(static_cast<char>(crc & 0xFF));

    return frame;
}

// Parse frame from incoming data
bool UartFrame::parseFrame(const QByteArray& data) {
    m_isValid = false;
    m_errorMessage.clear();

    if (data.size() < UART::MIN_FRAME_SIZE) {
        m_errorMessage = QString("Frame too short: %1 bytes").arg(data.size());
        return false;
    }

    int offset = 0;

    // Check header
    if (static_cast<uint8_t>(data[offset]) != UART::HEADER_BYTE_0 ||
        static_cast<uint8_t>(data[offset + 1]) != UART::HEADER_BYTE_1) {
        m_errorMessage = "Invalid frame header";
        return false;
    }
    offset += UART::HEADER_SIZE;

    // Get length
    m_length = static_cast<uint8_t>(data[offset]);
    offset += UART::LENGTH_SIZE;

    // Validate length
    if (m_length > UART::MAX_PAYLOAD_SIZE) {
        m_errorMessage = QString("Payload length exceeds max: %1 > %2")
            .arg(m_length).arg(UART::MAX_PAYLOAD_SIZE);
        return false;
    }

    // Check if complete frame is available
    int expectedSize = UART::HEADER_SIZE + UART::LENGTH_SIZE + UART::CMD_ID_SIZE + m_length + UART::CRC_SIZE;
    if (data.size() < expectedSize) {
        m_errorMessage = QString("Incomplete frame: have %1, need %2")
            .arg(data.size()).arg(expectedSize);
        return false;
    }

    // Get command ID
    m_cmdId = static_cast<UART::CommandId>(data[offset]);
    offset += UART::CMD_ID_SIZE;

    // Get payload
    if (m_length > 0) {
        m_payload = data.mid(offset, m_length);
        offset += m_length;
    } else {
        m_payload.clear();
    }

    // Get CRC
    uint8_t crcHigh = static_cast<uint8_t>(data[offset]);
    uint8_t crcLow = static_cast<uint8_t>(data[offset + 1]);
    m_crc16 = (static_cast<uint16_t>(crcHigh) << 8) | crcLow;

    // Verify CRC
    QByteArray frameToCrc;
    frameToCrc.append(UART::HEADER_BYTE_0);
    frameToCrc.append(UART::HEADER_BYTE_1);
    frameToCrc.append(m_length);
    frameToCrc.append(static_cast<uint8_t>(m_cmdId));
    frameToCrc.append(m_payload);

    uint16_t calculatedCrc = calculateCrc16(frameToCrc);

    if (calculatedCrc != m_crc16) {
        m_errorMessage = QString("CRC mismatch: calculated 0x%1, received 0x%2")
            .arg(calculatedCrc, 4, 16, QChar('0'))
            .arg(m_crc16, 4, 16, QChar('0'));
        return false;
    }

    m_isValid = true;
    return true;
}

// Check if data has valid header
bool UartFrame::isValidHeader(const QByteArray& data) {
    if (data.size() < UART::HEADER_SIZE) {
        return false;
    }
    return static_cast<uint8_t>(data[0]) == UART::HEADER_BYTE_0 &&
           static_cast<uint8_t>(data[1]) == UART::HEADER_BYTE_1;
}

// Find first frame start in buffer
int UartFrame::findFrameStart(const QByteArray& data) {
    for (int i = 0; i < data.size() - 1; ++i) {
        if (static_cast<uint8_t>(data[i]) == UART::HEADER_BYTE_0 &&
            static_cast<uint8_t>(data[i + 1]) == UART::HEADER_BYTE_1) {
            return i;
        }
    }
    return -1;
}

// Convert frame to string for debugging
QString UartFrame::toString() const {
    QString str;
    str += QString("Frame: Header=0x%1%2 Len=%3 CmdId=0x%4 Payload=%5 CRC=0x%6")
        .arg(QString::number(m_header[0], 16), QString::number(m_header[1], 16))
        .arg(m_length)
        .arg(QString::number(static_cast<uint8_t>(m_cmdId), 16), 2, QChar('0'))
        .arg(m_payload.toHex().toUpper().constData())
        .arg(m_crc16, 4, 16, QChar('0'));

    if (!m_isValid && !m_errorMessage.isEmpty()) {
        str += QString(" [Invalid: %1]").arg(m_errorMessage);
    }

    return str;
}

// Convert frame to byte array for transmission
QByteArray UartFrame::toByteArray() const {
    return buildFrame(m_cmdId, m_payload);
}
