#include "uart_port.h"
#include <QDebug>
#include <QThread>

UartPort::UartPort(QObject* parent)
    : QObject(parent), m_isOpen(false), m_bytesRead(0), m_bytesWritten(0) {

    // Connect serial port signals
    connect(&m_serialPort, &QSerialPort::errorOccurred, this, &UartPort::onError);
    connect(&m_serialPort, &QSerialPort::readyRead, this, &UartPort::onReadyRead);
}

UartPort::~UartPort() {
    closePort();
}

// Open serial port with default or specified configuration
bool UartPort::openPort(const QString& portName) {
    if (m_isOpen) {
        m_lastError = "Port already open";
        return false;
    }

    m_serialPort.setPortName(portName.isEmpty() ? UART::DEFAULT_PORT : portName);

    if (!m_serialPort.open(QIODevice::ReadWrite)) {
        m_lastError = QString("Failed to open port %1: %2")
            .arg(m_serialPort.portName(), m_serialPort.errorString());
        return false;
    }

    // Apply default configuration
    if (!setConfiguration()) {
        m_serialPort.close();
        return false;
    }

    m_isOpen = true;
    m_receiveBuffer.clear();
    m_frameQueue.clear();
    resetStatistics();

    emit portOpened();
    emit connectionStatusChanged(true);

    return true;
}

// Close serial port
bool UartPort::closePort() {
    if (!m_isOpen) {
        return true;
    }

    m_serialPort.close();
    m_isOpen = false;
    m_receiveBuffer.clear();
    m_frameQueue.clear();

    emit portClosed();
    emit connectionStatusChanged(false);

    return true;
}

// Check if port is open
bool UartPort::isOpen() const {
    return m_isOpen && m_serialPort.isOpen();
}

// Configure port settings
bool UartPort::setConfiguration(int baudRate, int dataBits, int stopBits) {
    if (!m_serialPort.isOpen()) {
        m_lastError = "Port not open";
        return false;
    }

    // Set baud rate
    if (!m_serialPort.setBaudRate(baudRate)) {
        m_lastError = QString("Failed to set baud rate %1: %2")
            .arg(baudRate).arg(m_serialPort.errorString());
        return false;
    }

    // Set data bits
    if (!m_serialPort.setDataBits(static_cast<QSerialPort::DataBits>(dataBits))) {
        m_lastError = QString("Failed to set data bits %1: %2")
            .arg(dataBits).arg(m_serialPort.errorString());
        return false;
    }

    // Set stop bits
    if (!m_serialPort.setStopBits(static_cast<QSerialPort::StopBits>(stopBits))) {
        m_lastError = QString("Failed to set stop bits %1: %2")
            .arg(stopBits).arg(m_serialPort.errorString());
        return false;
    }

    // Set parity to None
    if (!m_serialPort.setParity(QSerialPort::NoParity)) {
        m_lastError = QString("Failed to set parity: %1")
            .arg(m_serialPort.errorString());
        return false;
    }

    // Disable flow control
    if (!m_serialPort.setFlowControl(QSerialPort::NoFlowControl)) {
        m_lastError = QString("Failed to disable flow control: %1")
            .arg(m_serialPort.errorString());
        return false;
    }

    return true;
}

// Send frame data
bool UartPort::sendFrame(const QByteArray& frameData) {
    if (!m_isOpen) {
        m_lastError = "Port not open";
        emit errorOccurred(m_lastError);
        return false;
    }

    qint64 bytesWritten = m_serialPort.write(frameData);

    if (bytesWritten == -1) {
        m_lastError = QString("Write failed: %1")
            .arg(m_serialPort.errorString());
        emit errorOccurred(m_lastError);
        return false;
    }

    if (bytesWritten != frameData.size()) {
        m_lastError = QString("Partial write: %1 of %2 bytes")
            .arg(bytesWritten).arg(frameData.size());
        // Don't fail on partial write - data might still be in TX buffer
    }

    m_bytesWritten += bytesWritten;

    // Wait for bytes to be written
    if (!m_serialPort.waitForBytesWritten(UART::WRITE_TIMEOUT_MS)) {
        m_lastError = QString("Write timeout");
        // Don't fail - data might still be writing
    }

    return true;
}

// Send command with CRC
bool UartPort::sendCommand(UART::CommandId cmdId, const QByteArray& payload) {
    QByteArray frame = UartFrame::buildFrame(cmdId, payload);
    return sendFrame(frame);
}

// Read available data
QByteArray UartPort::readAvailable() {
    if (!m_isOpen) {
        return QByteArray();
    }

    QByteArray data = m_serialPort.readAll();
    m_bytesRead += data.size();

    if (!data.isEmpty()) {
        addToBuffer(data);
        processReceivedData();
    }

    return data;
}

// Wait for data to be ready
bool UartPort::waitForReadyRead(int msecs) {
    if (!m_isOpen) {
        return false;
    }

    return m_serialPort.waitForReadyRead(msecs);
}

// Get next complete frame from queue
bool UartPort::getNextFrame(UartFrame& frame) {
    QMutexLocker lock(&m_mutex);

    if (m_frameQueue.isEmpty()) {
        return false;
    }

    frame = m_frameQueue.dequeue();
    return true;
}

// Get port name
QString UartPort::getPortName() const {
    return m_serialPort.portName();
}

// Get last error message
QString UartPort::getLastError() const {
    return m_lastError;
}

// Get number of complete frames in queue
int UartPort::getFrameCount() const {
    QMutexLocker lock(&m_mutex);
    return m_frameQueue.size();
}

// Get buffer size
int UartPort::getBufferSize() const {
    QMutexLocker lock(&m_mutex);
    return m_receiveBuffer.size();
}

// Reset statistics
void UartPort::resetStatistics() {
    m_bytesRead = 0;
    m_bytesWritten = 0;
}

// Slot: handle serial port ready read signal
void UartPort::onReadyRead() {
    readAvailable();
}

// Slot: handle serial port error
void UartPort::onError(QSerialPort::SerialPortError error) {
    if (error == QSerialPort::NoError) {
        return;
    }

    m_lastError = m_serialPort.errorString();

    if (error == QSerialPort::ResourceError) {
        // Port disconnected
        m_isOpen = false;
        emit connectionStatusChanged(false);
    }

    emit errorOccurred(m_lastError);
}

// Add received data to buffer
void UartPort::addToBuffer(const QByteArray& data) {
    QMutexLocker lock(&m_mutex);
    m_receiveBuffer.append(data);

    // Prevent buffer overflow
    if (m_receiveBuffer.size() > UART::MAX_FRAME_SIZE * 10) {
        m_receiveBuffer.remove(0, m_receiveBuffer.size() - UART::MAX_FRAME_SIZE * 5);
    }
}

// Process data in buffer to extract complete frames
void UartPort::processReceivedData() {
    QMutexLocker lock(&m_mutex);

    while (m_receiveBuffer.size() > 0) {
        // Find frame header
        int headerPos = UartFrame::findFrameStart(m_receiveBuffer);

        if (headerPos == -1) {
            // No complete header found - clear buffer and wait for more data
            m_receiveBuffer.clear();
            return;
        }

        // Remove any junk before header
        if (headerPos > 0) {
            m_receiveBuffer.remove(0, headerPos);
        }

        // Need at least header + length to determine frame size
        if (m_receiveBuffer.size() < 3) {
            return;  // Wait for more data
        }

        uint8_t length = static_cast<uint8_t>(m_receiveBuffer[2]);
        int frameSize = UART::HEADER_SIZE + UART::LENGTH_SIZE + UART::CMD_ID_SIZE + length + UART::CRC_SIZE;

        if (m_receiveBuffer.size() < frameSize) {
            return;  // Wait for complete frame
        }

        // Extract and parse frame
        QByteArray frameData = m_receiveBuffer.left(frameSize);
        m_receiveBuffer.remove(0, frameSize);

        UartFrame frame;
        if (frame.parseFrame(frameData)) {
            m_frameQueue.enqueue(frame);
            emit frameReceived(frame);
            emit frameTransmitted(frame.getCommandId());
        } else {
            m_lastError = frame.getErrorMessage();
            emit errorOccurred(m_lastError);
            // Continue trying to parse next frames
        }
    }
}

// Get list of available serial ports
QStringList UartPort::getAvailablePorts() {
    QStringList ports;

    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
        ports.append(info.portName());
    }

    return ports;
}
