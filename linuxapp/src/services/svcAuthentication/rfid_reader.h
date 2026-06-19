#ifndef RFID_READER_H
#define RFID_READER_H

#include <QObject>
#include <QString>
#include <QSerialPort>
#include <QTimer>

/** Handles RFID reader hardware (GPIO or serial) and emits signals when cards are detected. */
class RfidReader : public QObject {
    Q_OBJECT

public:
    /** Constructs the RFID reader and sets up the debounce timer. */
    explicit RfidReader(QObject* parent = nullptr);
    /** Destructor - disconnects from hardware and cleans up serial port. */
    ~RfidReader();

    /** Tries GPIO connection first, then falls back to serial. Returns true if connected. */
    bool connectHardware(const QString& gpioPin = "GPIO17");
    /** Disconnects from the RFID reader hardware. */
    bool disconnect();
    /** Returns true if the RFID reader hardware is currently connected. */
    bool isConnected() const { return m_isConnected; }

    /** Sets the read timeout in milliseconds. */
    void setReadTimeout(int msecs) { m_readTimeoutMs = msecs; }
    /** Sets the debounce interval in milliseconds to filter out noise. */
    void setDebounceInterval(int msecs) { m_debounceMs = msecs; }

    /** Connects to the RFID reader via a serial port at the given baud rate. */
    bool connectSerial(const QString& portName, int baudRate = 9600);
    /** Configures a GPIO pin as an input with rising-edge detection. */
    bool setupGpioInput(const QString& gpioPin);

signals:
    /** Emitted when RFID data has been successfully read and validated. */
    void rfidRead(const QString& rfidData);
    /** Emitted when the RFID reader hardware connects. */
    void readerConnected();
    /** Emitted when the RFID reader hardware disconnects. */
    void readerDisconnected();
    /** Emitted when the RFID reader encounters an error. */
    void readerError(const QString& error);

    /** Emitted when the reader status changes (e.g. "Connected", "Reading..."). */
    void statusChanged(const QString& status);

public slots:
    /** Begins reading RFID data from the connected hardware. */
    void startReading();
    /** Stops reading RFID data and clears the debounce timer. */
    void stopReading();

    /** Slot: called when serial data is available from the RFID reader. */
    void onSerialReadyRead();
    /** Slot: called when a serial port error occurs. */
    void onSerialError();
    /** Slot: called when the debounce timer expires (ready for next read). */
    void onDebounceTimeout();

protected slots:
    /** Slot: called when a GPIO rising edge interrupt is detected. */
    void onGpioInterrupt();

private:
    /** Configures GPIO + interrupt handling for the given pin. */
    bool setupGpioInterrupt(const QString& gpioPin);
    /** Writes a string value to a sysfs GPIO file. */
    bool writeGpioFile(const QString& path, const QString& value);
    /** Reads a string value from a sysfs GPIO file. */
    QString readGpioFile(const QString& path);

    /** Validates and processes raw RFID data, emits rfidRead on success. */
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
