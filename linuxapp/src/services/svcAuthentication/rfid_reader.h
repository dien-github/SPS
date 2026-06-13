#ifndef RFID_READER_H
#define RFID_READER_H

#include <QObject>
#include <QString>
#include <QSerialPort>
#include <QTimer>

// RFID Reader Handler
// Manages GPIO input from RFID reader
// Emits signals when RFID cards are detected
class RfidReader : public QObject {
    Q_OBJECT

public:
    explicit RfidReader(QObject* parent = nullptr);
    ~RfidReader();

    // Connection management
    bool connectHardware(const QString& gpioPin = "GPIO17");  // GPIO pin for RFID interrupt
    bool disconnect();
    bool isConnected() const { return m_isConnected; }

    // Configuration
    void setReadTimeout(int msecs) { m_readTimeoutMs = msecs; }
    void setDebounceInterval(int msecs) { m_debounceMs = msecs; }

    // RFID data (can be serial or GPIO-based)
    bool connectSerial(const QString& portName, int baudRate = 9600);
    bool setupGpioInput(const QString& gpioPin);

signals:
    // RFID signals
    void rfidRead(const QString& rfidData);
    void readerConnected();
    void readerDisconnected();
    void readerError(const QString& error);

    // Status signals
    void statusChanged(const QString& status);

public slots:
    void startReading();
    void stopReading();

    // TODO: Need to refactor
    void onSerialReadyRead();
    void onSerialError();
    //void onGpioInterrupt();
    void onDebounceTimeout();

protected slots:
    // TODO: Need to refactor
    //void onSerialReadyRead();
    //void onSerialError();
    void onGpioInterrupt();
    //void onDebounceTimeout();

private:
    // GPIO handling
    bool setupGpioInterrupt(const QString& gpioPin);
    bool writeGpioFile(const QString& path, const QString& value);
    QString readGpioFile(const QString& path);

    // Data processing
    void processRfidData(const QString& data);

    // Connection state
    bool m_isConnected;
    bool m_isReading;

    // Configuration
    int m_readTimeoutMs;
    int m_debounceMs;
    QString m_gpioPin;
    QString m_serialPort;

    // Serial port (if using serial connection)
    QSerialPort* m_serial;

    // Debounce timer
    QTimer m_debounceTimer;

    // GPIO paths
    QString m_gpioPath;
    QString m_gpioValuePath;
    QString m_gpioEdgePath;

    // Statistics
    int m_totalReads;
    int m_successfulReads;
};

#endif // RFID_READER_H
