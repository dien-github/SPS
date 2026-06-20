#include "mcu_engine.h"
#include "../common/sps_logger.h"
#include "sps_uart_protocol.h"
#include <QDateTime>
#include <QTimer>

namespace {

QString bytesToHex(const QByteArray& data) {
    return QString::fromLatin1(data.toHex(' ').toUpper());
}

QString hexByte(uint8_t value) {
    return QString("0x%1")
        .arg(QString("%1").arg(static_cast<int>(value), 2, 16, QChar('0')).toUpper());
}

QString hexWord(uint16_t value) {
    return QString("0x%1")
        .arg(QString("%1").arg(static_cast<int>(value), 4, 16, QChar('0')).toUpper());
}

QString commandName(UART::CommandId cmdId) {
    switch (cmdId) {
        case UART::CommandId::PING_HEARTBEAT: return "PING_HEARTBEAT";
        case UART::CommandId::ACK_ALIVE: return "ACK_ALIVE";
        case UART::CommandId::NACK_ERROR: return "NACK_ERROR";
        case UART::CommandId::LIGHT_CONTROL: return "LIGHT_CONTROL";
        case UART::CommandId::CURTAIN_CONTROL: return "CURTAIN_CONTROL";
        case UART::CommandId::PROJECTOR_CONTROL: return "PROJECTOR_CONTROL";
        case UART::CommandId::AC_CONTROL: return "AC_CONTROL";
        case UART::CommandId::AC_TEMP_UP: return "AC_TEMP_UP";
        case UART::CommandId::AC_TEMP_DOWN: return "AC_TEMP_DOWN";
        case UART::CommandId::QUERY_RELAY_STATUS: return "QUERY_RELAY_STATUS";
        case UART::CommandId::PRESENCE_ALERT: return "PRESENCE_ALERT";
        case UART::CommandId::OTA_START: return "OTA_START";
        case UART::CommandId::OTA_DATA_CHUNK: return "OTA_DATA_CHUNK";
        case UART::CommandId::OTA_END: return "OTA_END";
        case UART::CommandId::INVALID: return "INVALID";
        default:
            return QString("UNKNOWN_%1").arg(hexByte(static_cast<uint8_t>(cmdId)));
    }
}

QString onOffName(uint8_t value) {
    if (value == SPS::UART::toByte(SPS::UART::ControlValue::ON)) {
        return "ON";
    }
    if (value == SPS::UART::toByte(SPS::UART::ControlValue::OFF)) {
        return "OFF";
    }
    return QString("UNKNOWN_%1").arg(hexByte(value));
}

QString curtainActionName(uint8_t value) {
    if (value == SPS::UART::toByte(SPS::UART::ControlValue::OPEN)) {
        return "OPEN";
    }
    if (value == SPS::UART::toByte(SPS::UART::ControlValue::CLOSE)) {
        return "CLOSE";
    }
    if (value == SPS::UART::toByte(SPS::UART::ControlValue::STOP)) {
        return "STOP";
    }
    return QString("UNKNOWN_%1").arg(hexByte(value));
}

QString lightDeviceName(uint8_t deviceId) {
    switch (static_cast<SPS::UART::DeviceId>(deviceId)) {
        case SPS::UART::DeviceId::LIGHT_PODIUM: return "LIGHT_PODIUM";
        case SPS::UART::DeviceId::LIGHT_CLASS: return "LIGHT_CLASS";
        case SPS::UART::DeviceId::LIGHT_ALL: return "LIGHT_ALL";
        default:
            return QString("LIGHT_%1").arg(hexByte(deviceId));
    }
}

QString curtainDeviceName(uint8_t deviceId) {
    switch (deviceId) {
        case static_cast<uint8_t>(SPS::UART::DeviceId::CURTAIN): return "CURTAIN";
        case static_cast<uint8_t>(SPS::UART::DeviceId::SCREEN): return "SCREEN";
        default:
            return QString("CURTAIN_%1").arg(hexByte(deviceId));
    }
}

QString acDeviceName(uint8_t deviceId) {
    return QString("AC_%1").arg(hexByte(deviceId));
}

bool payloadByte(const QByteArray& payload, int index, uint8_t& value) {
    if (index < 0 || index >= payload.size()) {
        return false;
    }

    value = static_cast<uint8_t>(static_cast<unsigned char>(payload.at(index)));
    return true;
}

QString otaStartAction(const QByteArray& payload) {
    if (payload.size() < SPS::UART::Payload::OTA_SIZE_BYTES) {
        return "OTA_START";
    }

    uint32_t firmwareSize = 0;
    firmwareSize |= static_cast<uint32_t>(SPS::UART::byteAt(payload, 0));
    firmwareSize |= static_cast<uint32_t>(SPS::UART::byteAt(payload, 1)) << 8;
    firmwareSize |= static_cast<uint32_t>(SPS::UART::byteAt(payload, 2)) << 16;
    firmwareSize |= static_cast<uint32_t>(SPS::UART::byteAt(payload, 3)) << 24;
    return QString("OTA_START size=%1").arg(firmwareSize);
}

QString decodedCommandDetails(const UartFrame& frame) {
    const UART::CommandId cmdId = frame.getCommandId();
    const QByteArray payload = frame.getPayload();
    QString device = "MCU";
    QString action = "UNKNOWN";

    uint8_t deviceId = 0;
    uint8_t value = 0;

    switch (cmdId) {
        case UART::CommandId::PING_HEARTBEAT:
            action = "PING";
            break;

        case UART::CommandId::LIGHT_CONTROL:
            if (payloadByte(payload, SPS::UART::Payload::DEVICE_ID_OFFSET, deviceId)) {
                device = lightDeviceName(deviceId);
            } else {
                device = "LIGHT_UNKNOWN";
            }
            if (payloadByte(payload, SPS::UART::Payload::CONTROL_VALUE_OFFSET, value)) {
                action = onOffName(value);
            }
            break;

        case UART::CommandId::CURTAIN_CONTROL:
            if (payloadByte(payload, SPS::UART::Payload::DEVICE_ID_OFFSET, deviceId)) {
                device = curtainDeviceName(deviceId);
            } else {
                device = "CURTAIN_UNKNOWN";
            }
            if (payloadByte(payload, SPS::UART::Payload::CONTROL_VALUE_OFFSET, value)) {
                action = curtainActionName(value);
            }
            break;

        case UART::CommandId::PROJECTOR_CONTROL:
            device = "PROJECTOR";
            if (payloadByte(payload, SPS::UART::Payload::PROJECTOR_VALUE_OFFSET, value)) {
                action = onOffName(value);
            }
            break;

        case UART::CommandId::AC_CONTROL:
            if (payloadByte(payload, SPS::UART::Payload::DEVICE_ID_OFFSET, deviceId)) {
                device = acDeviceName(deviceId);
            } else {
                device = "AC_UNKNOWN";
            }
            if (payloadByte(payload, SPS::UART::Payload::CONTROL_VALUE_OFFSET, value)) {
                action = onOffName(value);
            }
            break;

        case UART::CommandId::AC_TEMP_UP:
            if (payloadByte(payload, SPS::UART::Payload::DEVICE_ID_OFFSET, deviceId)) {
                device = acDeviceName(deviceId);
            } else {
                device = "AC_UNKNOWN";
            }
            action = "TEMP_UP";
            break;

        case UART::CommandId::AC_TEMP_DOWN:
            if (payloadByte(payload, SPS::UART::Payload::DEVICE_ID_OFFSET, deviceId)) {
                device = acDeviceName(deviceId);
            } else {
                device = "AC_UNKNOWN";
            }
            action = "TEMP_DOWN";
            break;

        case UART::CommandId::QUERY_RELAY_STATUS:
            if (payloadByte(payload, SPS::UART::Payload::DEVICE_ID_OFFSET, deviceId)) {
                device = QString("RELAY_%1").arg(hexByte(deviceId));
            } else {
                device = "RELAY_UNKNOWN";
            }
            action = "QUERY_STATUS";
            break;

        case UART::CommandId::OTA_START:
            device = "MCU_OTA";
            action = otaStartAction(payload);
            break;

        case UART::CommandId::OTA_DATA_CHUNK:
            device = "MCU_OTA";
            action = "OTA_DATA_CHUNK";
            break;

        case UART::CommandId::OTA_END:
            device = "MCU_OTA";
            action = "OTA_END";
            break;

        default:
            action = commandName(cmdId);
            break;
    }

    return QString("cmd=%1(%2) device=%3 action=%4 seq=%5 payload=%6")
        .arg(commandName(cmdId))
        .arg(hexByte(static_cast<uint8_t>(cmdId)))
        .arg(device)
        .arg(action)
        .arg(frame.getSequenceId())
        .arg(bytesToHex(payload));
}

bool extractCrcStatus(const QByteArray& frameData, uint16_t& receivedCrc,
                      uint16_t& calculatedCrc, bool& crcOk) {
    if (frameData.size() < UART::MIN_FRAME_SIZE || !UartFrame::isValidHeader(frameData)) {
        return false;
    }

    const uint8_t length = static_cast<uint8_t>(frameData.at(UART::HEADER_SIZE));
    const int crcOffset = UART::HEADER_SIZE + UART::LENGTH_SIZE + UART::CMD_ID_SIZE +
        UART::SEQ_ID_SIZE + length;
    const int expectedSize = crcOffset + UART::CRC_SIZE;

    if (frameData.size() < expectedSize) {
        return false;
    }

    const uint8_t crcLow = static_cast<uint8_t>(frameData.at(crcOffset));
    const uint8_t crcHigh = static_cast<uint8_t>(frameData.at(crcOffset + 1));
    receivedCrc = (static_cast<uint16_t>(crcHigh) << 8) | crcLow;

    const QByteArray crcBytes = frameData.mid(
        UART::HEADER_SIZE,
        UART::LENGTH_SIZE + UART::CMD_ID_SIZE + UART::SEQ_ID_SIZE + length);
    calculatedCrc = UartFrame::calculateCrc16(crcBytes);
    crcOk = receivedCrc == calculatedCrc;
    return true;
}

QString crcStatusText(const QByteArray& frameData) {
    uint16_t receivedCrc = 0;
    uint16_t calculatedCrc = 0;
    bool crcOk = false;

    if (!extractCrcStatus(frameData, receivedCrc, calculatedCrc, crcOk)) {
        return "crc=unavailable crc_ok=false";
    }

    return QString("crc=%1 calculated_crc=%2 crc_ok=%3")
        .arg(hexWord(receivedCrc))
        .arg(hexWord(calculatedCrc))
        .arg(crcOk ? "true" : "false");
}

QString currentTimestamp() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

} // namespace

UartMcuEngine::UartMcuEngine(QObject* parent)
    : IMcuEngine(parent),
      m_uartPort(new UartPort(this)) {
    connect(m_uartPort, &UartPort::frameReceived, this, &UartMcuEngine::frameReceived);
    connect(m_uartPort, &UartPort::connectionStatusChanged,
            this, &UartMcuEngine::connectionStatusChanged);
    connect(m_uartPort, &UartPort::errorOccurred, this, [this](const QString& error) {
        m_lastError = error;
        emit errorOccurred(error);
    });
}

bool UartMcuEngine::open(const QString& portName) {
    Logger::instance().info("MCU_ENGINE", QString("engine=uart port=%1").arg(portName));
    if (!m_uartPort->openPort(portName)) {
        m_lastError = m_uartPort->getLastError();
        return false;
    }

    m_lastError.clear();
    return true;
}

bool UartMcuEngine::close() {
    return m_uartPort->closePort();
}

bool UartMcuEngine::isOpen() const {
    return m_uartPort->isOpen();
}

bool UartMcuEngine::sendFrame(const QByteArray& frameData) {
    const bool sent = m_uartPort->sendFrame(frameData);
    if (!sent) {
        m_lastError = m_uartPort->getLastError();
    }
    return sent;
}

QString UartMcuEngine::name() const {
    return "uart";
}

QString UartMcuEngine::lastError() const {
    return m_lastError.isEmpty() ? m_uartPort->getLastError() : m_lastError;
}

QStringList UartMcuEngine::getAvailablePorts() {
    return UartPort::getAvailablePorts();
}

VirtualMcuEngine::VirtualMcuEngine(QObject* parent)
    : IMcuEngine(parent),
      m_isOpen(false) {
}

bool VirtualMcuEngine::open(const QString& portName) {
    Q_UNUSED(portName);

    if (m_isOpen) {
        return true;
    }

    m_isOpen = true;
    m_lastError.clear();
    Logger::instance().info("MCU_ENGINE", "mode=virtual");
    emit connectionStatusChanged(true);
    return true;
}

bool VirtualMcuEngine::close() {
    if (!m_isOpen) {
        return true;
    }

    m_isOpen = false;
    emit connectionStatusChanged(false);
    return true;
}

bool VirtualMcuEngine::isOpen() const {
    return m_isOpen;
}

bool VirtualMcuEngine::sendFrame(const QByteArray& frameData) {
    if (!m_isOpen) {
        m_lastError = "Virtual MCU engine is not open";
        emit errorOccurred(m_lastError);
        return false;
    }

    UartFrame txFrame;
    const bool parsed = txFrame.parseFrame(frameData);
    const QString crcText = crcStatusText(frameData);
    const QString timestamp = currentTimestamp();

    if (!parsed) {
        m_lastError = txFrame.getErrorMessage();
        Logger::instance().warning(
            "VIRTUAL_MCU",
            QString("TX decoded: timestamp=%1 parse_error=\"%2\" %3")
                .arg(timestamp)
                .arg(m_lastError)
                .arg(crcText));
        Logger::instance().info(
            "VIRTUAL_MCU",
            QString("TX raw: timestamp=%1 bytes=%2").arg(timestamp, bytesToHex(frameData)));
        emit errorOccurred(m_lastError);
        return false;
    }

    Logger::instance().info(
        "VIRTUAL_MCU",
        QString("TX decoded: timestamp=%1 %2 %3")
            .arg(timestamp)
            .arg(decodedCommandDetails(txFrame))
            .arg(crcText));
    Logger::instance().info(
        "VIRTUAL_MCU",
        QString("TX raw: timestamp=%1 bytes=%2").arg(timestamp, bytesToHex(frameData)));

    const uint8_t originalCmdId = static_cast<uint8_t>(txFrame.getCommandId());
    const uint8_t originalSeqId = txFrame.getSequenceId();
    const QByteArray ackPayload = SPS::UART::buildAckPayload(originalCmdId, originalSeqId);
    const QByteArray ackBytes = UartFrame::buildFrame(
        UART::CommandId::ACK_ALIVE,
        ackPayload,
        originalSeqId);
    const QString command = commandName(txFrame.getCommandId());

    QTimer::singleShot(0, this, [this, ackBytes, command, originalSeqId]() {
        if (!m_isOpen) {
            return;
        }

        UartFrame ackFrame;
        if (!ackFrame.parseFrame(ackBytes)) {
            m_lastError = ackFrame.getErrorMessage();
            emit errorOccurred(m_lastError);
            return;
        }

        Logger::instance().info(
            "VIRTUAL_MCU",
            QString("RX fake ACK: seq=%1 status=OK command=%2 raw=%3")
                .arg(originalSeqId)
                .arg(command)
                .arg(bytesToHex(ackBytes)));
        emit frameReceived(ackFrame);
    });

    m_lastError.clear();
    return true;
}

QString VirtualMcuEngine::name() const {
    return "virtual";
}

QString VirtualMcuEngine::lastError() const {
    return m_lastError;
}
