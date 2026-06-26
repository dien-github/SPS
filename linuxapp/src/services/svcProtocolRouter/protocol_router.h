#ifndef PROTOCOL_ROUTER_H
#define PROTOCOL_ROUTER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QQueue>
#include <QMutex>
#include <QTimer>
#include "../common/sps_service_base.h"
#include "mcu_engine.h"

// Command Response pair
struct PendingCommand {
    UART::CommandId cmdId;
    uint8_t seqId;
    QByteArray payload;
    int retryCount;
    int maxRetries;
    qint64 timestamp;
};

// Protocol Router Service
// Manages MCU communication through the selected engine
// Handles command routing, retry logic, and device state
class ProtocolRouter : public SpsServiceBase {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.sps.router")

public:
    /** Constructor. Initializes the ProtocolRouter service with default UART and retry settings. */
    explicit ProtocolRouter(QObject* parent = nullptr);
    /** Destructor. Shuts down the service and cleans up resources. */
    ~ProtocolRouter();

    // Service lifecycle
    /** Initializes the service: selects an MCU engine, connects signals, and registers D-Bus. */
    bool initialize() override;
    /** Shuts down the service: stops timers, disconnects MCU, and cleans up. */
    void shutdown() override;
    /** Returns a human-readable status string with connection and command statistics. */
    QString getStatus() const override;

    // Connection management
    /** Opens the configured MCU engine and sends a heartbeat. */
    bool connectMcu(const QString& portName = "/dev/ttyS0");
    /** Closes the MCU connection and clears the command queue. */
    bool disconnectMcu();
    /** Returns true if the selected MCU engine is open and connected. */
    bool isConnected() const;

    // Getters
    /** Returns the current connection status as a string (CONNECTED/RECONNECTING/DISCONNECTED). */
    QString getConnectionStatus() const;
    /** Returns the number of commands waiting in the queue. */
    int getCommandQueueSize() const;
    /** Returns 1 if a command is pending acknowledgment, 0 otherwise. */
    int getPendingCommandCount() const;

signals:
    // D-Bus signals
    /** Emitted when the MCU connection status changes. */
    void ConnectionStatusChanged(const QString& status);
    /** Emitted when the MCU acknowledges a command. */
    void CommandAcknowledged(uchar cmdId);
    /** Emitted when the MCU reports an error for a command. */
    void CommandError(uchar cmdId, uchar errorCode);
    /** Emitted when a device's status changes. */
    void DeviceStatusChanged(uchar deviceId, uchar newStatus);
    /** Emitted when presence is detected or cleared. */
    void PresenceDetected(bool isPresent);
    /** Emitted during OTA transfer to report progress percentage. */
    void OTAProgress(int percentage);

public slots:
    // D-Bus methods
    /** Sends a command to the MCU with the given ID and payload. */
    Q_SCRIPTABLE bool SendCommand(uchar cmdId, const QByteArray& payload);
    /** Queries a device's status and returns the cached value. */
    Q_SCRIPTABLE uchar GetDeviceStatus(uchar deviceId);
    /** Returns the current connection status string (D-Bus callable). */
    Q_SCRIPTABLE QString GetConnectionStatus() const;
    /** Disconnects and reconnects to the MCU. */
    Q_SCRIPTABLE bool ResetConnection();
    /** Initiates an OTA firmware update with the given total size. */
    Q_SCRIPTABLE bool StartOTA(uint firmwareSize);
    /** Sends a single 128-byte OTA data chunk. */
    Q_SCRIPTABLE bool SendOTAChunk(ushort chunkNumber, const QByteArray& chunkData);
    /** Finalizes the OTA update transfer. */
    Q_SCRIPTABLE bool EndOTA();

    // Command convenience methods
    /** Turns a light on (true) or off (false). */
    Q_SCRIPTABLE bool ControlLight(uchar lightId, bool on);
    /** Controls a curtain: 0=close, 1=open, 2=stop. */
    Q_SCRIPTABLE bool ControlCurtain(uchar curtainId, uchar action);
    /** Turns the projector on (true) or off (false). */
    Q_SCRIPTABLE bool ControlProjector(bool on);
    /** Turns an air conditioner on (true) or off (false). */
    Q_SCRIPTABLE bool ControlAC(uchar acId, bool on);
    /** Increases the air conditioner temperature by one step. */
    Q_SCRIPTABLE bool IncreaseACTemperature(uchar acId);
    /** Decreases the air conditioner temperature by one step. */
    Q_SCRIPTABLE bool DecreaseACTemperature(uchar acId);

protected slots:
    // MCU engine signal handlers
    /** Handles an incoming UART frame from the MCU. */
    void onFrameReceived(const UartFrame& frame);
    /** Handles an MCU engine error. */
    void onPortError(const QString& error);
    /** Updates connection state when the MCU engine connects or disconnects. */
    void onConnectionStatusChanged(bool connected);

    // Internal timers and processing
    /** Called when the retry timer expires; retries or fails the current command. */
    void onRetryTimeout();
    /** Sends the next queued command if none is pending. */
    void processCommandQueue();
    /** Parses and handles an MCU response frame (ACK, NACK, status, etc.). */
    void handleMcuResponse(const UartFrame& frame);

private:
    // Command handling
    /** Adds a command to the transmit queue and starts processing. */
    bool queueCommand(UART::CommandId cmdId, const QByteArray& payload, int maxRetries = 3);
    /** Transmits the next command from the queue over UART. */
    bool sendPendingCommand();
    /** Retries the current pending command or marks it as failed. */
    void retryPendingCommand();
    /** Marks the current command as successful and processes the next in queue. */
    void commandSucceeded(UART::CommandId cmdId);
    /** Marks the current command as failed and emits a CommandError signal. */
    void commandFailed(UART::CommandId cmdId, const QString& reason);

    // Device state tracking
    /** Updates the cached status for a device. */
    void updateDeviceStatus(uchar deviceId, uchar status);
    /** Returns the cached status for a device, or 0xFF if unknown. */
    uchar getCachedDeviceStatus(uchar deviceId) const;

    // MCU engine selection
    /** Installs a new MCU engine implementation and connects router signal handlers. */
    void installMcuEngine(IMcuEngine* engine);
    /** Opens the physical UART MCU engine. */
    bool connectUartEngine(const QString& portName);
    /** Opens the virtual MCU engine. */
    bool connectVirtualEngine();

    // OTA handling
    /** Starts an OTA transfer by queueing the OTA_START command. */
    bool startOtaTransfer(uint firmwareSize);
    /** Cleans up after an OTA transfer completes or fails. */
    void endOtaTransfer();
    /** Queues the next OTA data chunk for transmission. */
    void sendNextOtaChunk();

    // Configuration
    QString m_uartPortName;
    QString m_mcuMode;
    int m_commandTimeoutMs;
    int m_defaultRetries;

    // MCU communication
    IMcuEngine* m_mcuEngine;
    bool m_isConnected;
    bool m_reconnecting;

    // Command queue and retry
    QQueue<PendingCommand> m_commandQueue;
    PendingCommand m_currentCommand;
    bool m_commandPending;
    uint8_t m_nextSeqId;
    QTimer m_retryTimer;

    // Device state cache (deviceId -> status)
    QMap<uchar, uchar> m_deviceStatusCache;
    mutable QMutex m_statusMutex;

    // OTA state
    bool m_otaInProgress;
    uint m_otaFileSize;
    uint m_otaBytesReceived;
    QQueue<QByteArray> m_otaChunkQueue;
    int m_otaCurrentChunk;

    // Statistics
    int m_totalCommands;
    int m_successfulCommands;
    int m_failedCommands;
    int m_totalRetries;
};

#endif // PROTOCOL_ROUTER_H
