#include "rfid_reader.h"
#include <QFile>
#include <QDebug>
#include <QThread>

/** Constructs the RFID reader with default settings and creates the debounce timer. */
RfidReader::RfidReader(QObject* parent)
    : QObject(parent), m_isConnected(false), m_isReading(false),
      m_readTimeoutMs(1000), m_debounceMs(100),
      m_serial(nullptr), m_totalReads(0), m_successfulReads(0) {

    // Setup debounce timer
    connect(&m_debounceTimer, &QTimer::timeout, this, &RfidReader::onDebounceTimeout);
    m_debounceTimer.setSingleShot(true);
}

/** Destructor - disconnects from hardware and deletes the serial port object. */
RfidReader::~RfidReader() {
    if (m_isConnected) {
        disconnect();
    }
    if (m_serial) {
        delete m_serial;
    }
}

/** Tries GPIO connection first; falls back to serial if GPIO fails. */
bool RfidReader::connectHardware(const QString& gpioPin) {
    m_gpioPin = gpioPin;

    // Try GPIO connection first
    if (setupGpioInterrupt(gpioPin)) {
        m_isConnected = true;
        emit readerConnected();
        emit statusChanged("Connected via GPIO");
        return true;
    }

    // Fall back to serial connection
    if (connectSerial("/dev/ttyUSB0", 9600)) {
        m_isConnected = true;
        emit readerConnected();
        emit statusChanged("Connected via Serial");
        return true;
    }

    emit readerError("Failed to connect RFID reader (no GPIO or serial)");
    return false;
}

/** Disconnects the RFID reader: closes serial port, stops timers, emits signals. */
bool RfidReader::disconnect() {
    if (m_serial && m_serial->isOpen()) {
        m_serial->close();
    }

    m_isConnected = false;
    m_isReading = false;
    m_debounceTimer.stop();

    emit readerDisconnected();
    emit statusChanged("Disconnected");
    return true;
}

/** Opens a serial connection to the RFID reader and connects readyRead/error signals. */
bool RfidReader::connectSerial(const QString& portName, int baudRate) {
    if (!m_serial) {
        m_serial = new QSerialPort(this);
    }

    m_serialPort = portName;

    m_serial->setPortName(portName);
    m_serial->setBaudRate(baudRate);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setParity(QSerialPort::NoParity);

    if (!m_serial->open(QIODevice::ReadOnly)) {
        emit readerError(QString("Failed to open serial port: %1").arg(portName));
        return false;
    }

    connect(m_serial, &QSerialPort::errorOccurred, this, &RfidReader::onSerialError);
    connect(m_serial, &QSerialPort::readyRead, this, &RfidReader::onSerialReadyRead);

    return true;
}

/** Exports a GPIO pin, sets direction to input, and enables rising-edge detection. */
bool RfidReader::setupGpioInput(const QString& gpioPin) {
    m_gpioPin = gpioPin;
    m_gpioPath = QString("/sys/class/gpio/gpio%1").arg(gpioPin);
    m_gpioValuePath = m_gpioPath + "/value";
    m_gpioEdgePath = m_gpioPath + "/edge";

    // Check if GPIO already exists
    QFile gpioDir(m_gpioPath);
    if (!gpioDir.exists()) {
        // Export GPIO
        QFile exportFile("/sys/class/gpio/export");
        if (!exportFile.open(QIODevice::WriteOnly)) {
            emit readerError("Failed to export GPIO - check permissions");
            return false;
        }
        exportFile.write(gpioPin.toLatin1());
        exportFile.close();

        // Wait for GPIO to be ready
        QThread::msleep(100);
    }

    // Set direction to input
    if (!writeGpioFile(m_gpioPath + "/direction", "in")) {
        emit readerError("Failed to set GPIO direction");
        return false;
    }

    // Set edge detection
    if (!writeGpioFile(m_gpioEdgePath, "rising")) {
        emit readerError("Failed to set GPIO edge");
        return false;
    }

    return true;
}

/** Sets up GPIO as input (currently polling-based; inotify TODO). */
bool RfidReader::setupGpioInterrupt(const QString& gpioPin) {
    if (!setupGpioInput(gpioPin)) {
        return false;
    }

    // In a real implementation, would use inotify or select() on GPIO value file
    // For now, we'll use polling
    // TODO: Implement inotify-based interrupt

    return true;
}

/** Writes a value to a sysfs GPIO file (e.g. direction, edge). */
bool RfidReader::writeGpioFile(const QString& path, const QString& value) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(value.toLatin1());
    file.close();
    return true;
}

/** Reads a value from a sysfs GPIO file and returns it as a trimmed string. */
QString RfidReader::readGpioFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return "";
    }
    QString value = file.readAll().trimmed();
    file.close();
    return value;
}

/** Starts reading (only works if hardware is connected). */
void RfidReader::startReading() {
    if (!m_isConnected) {
        emit readerError("Reader not connected");
        return;
    }

    m_isReading = true;
    emit statusChanged("Reading...");
}

/** Stops reading and stops the debounce timer. */
void RfidReader::stopReading() {
    m_isReading = false;
    m_debounceTimer.stop();
    emit statusChanged("Stopped");
}

/** Slot: reads available serial data and processes it as an RFID read. */
void RfidReader::onSerialReadyRead() {
    if (!m_isReading) {
        return;
    }

    QByteArray data = m_serial->readAll();

    if (data.isEmpty()) {
        return;
    }

    // Parse RFID data (typically ASCII, newline-terminated)
    QString rfidData = QString::fromLatin1(data).trimmed();

    processRfidData(rfidData);
}

/** Slot: emits readerError with the serial port's error string. */
void RfidReader::onSerialError() {
    if (m_serial) {
        emit readerError(m_serial->errorString());
    }
}

/** Slot: handles a GPIO rising edge, applies debounce, and processes the read. */
void RfidReader::onGpioInterrupt() {
    if (!m_isReading || m_debounceTimer.isActive()) {
        return;
    }

    m_debounceTimer.start(m_debounceMs);

    // Read GPIO value
    QString value = readGpioFile(m_gpioValuePath);

    if (value == "1") {
        // Rising edge detected - RFID present
        processRfidData("GPIO_READ");
    }
}

/** Slot: called when the debounce timer completes (currently a no-op). */
void RfidReader::onDebounceTimeout() {
    // Debounce complete
}

/** Validates RFID data length (4-50 chars) and emits rfidRead on success. */
void RfidReader::processRfidData(const QString& data) {
    if (data.isEmpty()) {
        return;
    }

    m_totalReads++;

    // Validate RFID data (basic check)
    if (data.length() < 4 || data.length() > 50) {
        qWarning() << "Invalid RFID data length:" << data.length();
        return;
    }

    m_successfulReads++;

    qDebug() << "RFID Read:" << data;
    emit rfidRead(data);
}
