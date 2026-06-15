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
    explicit UartPort(QObject* parent = nullptr);
    ~UartPort();

    // Connection management
    bool openPort(const QString& portName = UART::DEFAULT_PORT);
    bool closePort();
    bool isOpen() const;

    // Configure port settings
    bool setConfiguration(int baudRate = UART::BAUDRATE,
                        int dataBits = UART::DATA_BITS,
                        int stopBits = UART::STOP_BITS);

    // Data transmission
    bool sendFrame(const QByteArray& frameData);
    bool sendCommand(UART::CommandId cmdId, const QByteArray& payload);

    // Data reception (should be called in a loop or connected to signal)
    QByteArray readAvailable();
    bool waitForReadyRead(int msecs = UART::READ_TIMEOUT_MS);

    // Get available frames from buffer
    bool getNextFrame(UartFrame& frame);

    // Status
    QString getPortName() const;
    QString getLastError() const;
    int getFrameCount() const;
    int getBufferSize() const;

    // Statistics
    qint64 getBytesRead() const { return m_bytesRead; }
    qint64 getBytesWritten() const { return m_bytesWritten; }
    void resetStatistics();

    // Static helpers
    static QStringList getAvailablePorts();

signals:
    void portOpened();
    void portClosed();
    void frameReceived(const UartFrame& frame);
    void frameTransmitted(UART::CommandId cmdId);
    void errorOccurred(const QString& error);
    void connectionStatusChanged(bool connected);

private slots:
    void onReadyRead();
    void onError(QSerialPort::SerialPortError error);

private:
    // Data processing
    void processReceivedData();
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
};

#endif // UART_PORT_H
