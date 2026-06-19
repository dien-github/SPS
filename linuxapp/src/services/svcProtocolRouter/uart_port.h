#ifndef UART_PORT_H
#define UART_PORT_H

#include <QObject>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QThread>
#include "uart_frame.h"

// Serial port wrapper for UART communication
class UartPort : public QObject {
    Q_OBJECT

public:
    /** Constructor. Creates the UartPort and connects serial port signals. */
    explicit UartPort(QObject* parent = nullptr);
    /** Destructor. Closes the port if open. */
    ~UartPort();

    // Connection management
    /** Opens the serial port with the given name and applies default configuration. */
    bool openPort(const QString& portName = UART::DEFAULT_PORT);
    /** Closes the serial port and clears buffers. */
    bool closePort();
    /** Returns true if the serial port is currently open. */
    bool isOpen() const;

    // Configure port settings
    /** Configures baud rate, data bits, and stop bits on the open port. */
    bool setConfiguration(int baudRate = UART::BAUDRATE,
                        int dataBits = UART::DATA_BITS,
                        int stopBits = UART::STOP_BITS);

    // Data transmission
    /** Sends raw frame data over the serial port. */
    bool sendFrame(const QByteArray& frameData);
    /** Builds and sends a complete UART command frame with CRC. */
    bool sendCommand(UART::CommandId cmdId, const QByteArray& payload);

    // Data reception (should be called in a loop or connected to signal)
    /** Reads all available data from the serial port and processes it. */
    QByteArray readAvailable();
    /** Blocks until data is ready or the timeout expires. */
    bool waitForReadyRead(int msecs = UART::READ_TIMEOUT_MS);

    // Get available frames from buffer
    /** Dequeues the next complete parsed frame from the frame queue. */
    bool getNextFrame(UartFrame& frame);

    // Status
    /** Returns the serial port device name. */
    QString getPortName() const;
    /** Returns the last error message. */
    QString getLastError() const;
    /** Returns the number of complete frames awaiting processing. */
    int getFrameCount() const;
    /** Returns the size of the receive buffer. */
    int getBufferSize() const;

    // Statistics
    /** Returns the total number of bytes read. */
    qint64 getBytesRead() const { return m_bytesRead; }
    /** Returns the total number of bytes written. */
    qint64 getBytesWritten() const { return m_bytesWritten; }
    /** Resets byte read/write counters to zero. */
    void resetStatistics();

    // Static helpers
    /** Returns a list of available serial port names on the system. */
    static QStringList getAvailablePorts();

signals:
    /** Emitted when the serial port is successfully opened. */
    void portOpened();
    /** Emitted when the serial port is closed. */
    void portClosed();
    /** Emitted when a complete and valid UART frame has been received. */
    void frameReceived(const UartFrame& frame);
    /** Emitted when a UART command frame has been transmitted. */
    void frameTransmitted(UART::CommandId cmdId);
    /** Emitted when a serial port error occurs. */
    void errorOccurred(const QString& error);
    /** Emitted when the connection state changes (connected/disconnected). */
    void connectionStatusChanged(bool connected);

private slots:
    /** Slot. Called when data is available on the serial port. */
    void onReadyRead();
    /** Slot. Called when a serial port error occurs. */
    void onError(QSerialPort::SerialPortError error);

private:
    // Data processing
    /** Scans the receive buffer for complete frames and emits frameReceived. */
    void processReceivedData();
    /** Appends received data to the buffer, preventing overflow. */
    void addToBuffer(const QByteArray& data);

    // Serial port instance
    QSerialPort m_serialPort;

    // Frame buffer
    QByteArray m_receiveBuffer;
    QQueue<UartFrame> m_frameQueue;

    // Synchronization
    mutable QMutex m_mutex;
    QWaitCondition m_dataAvailable;

    // State
    bool m_isOpen;
    QString m_lastError;

    // Statistics
    qint64 m_bytesRead;
    qint64 m_bytesWritten;
    uint8_t m_nextSeqId;
};

#endif // UART_PORT_H
