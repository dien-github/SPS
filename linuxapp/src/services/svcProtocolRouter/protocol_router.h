#ifndef PROTOCOL_ROUTER_H
#define PROTOCOL_ROUTER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QQueue>
#include <QMutex>
#include <QTimer>
#include "../common/sps_service_base.h"
#include "uart_port.h"

// Command Response pair
struct PendingCommand {
    UART::CommandId cmdId;
    QByteArray payload;
    int retryCount;
    int maxRetries;
    qint64 timestamp;
};

// Protocol Router Service
// Manages UART communication with MCU
// Handles command routing, retry logic, and device state
class ProtocolRouter : public SpsServiceBase {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.sps.router")

public:
    explicit ProtocolRouter(QObject* parent = nullptr);
    ~ProtocolRouter();

    // Service lifecycle
    bool initialize() override;
    void shutdown() override;
    QString getStatus() const override;

    // Connection management
    bool connectMcu(const QString& portName = "/dev/ttyS0");
    bool disconnectMcu();
    bool isConnected() const;

    // Getters
    QString getConnectionStatus() const;
    int getCommandQueueSize() const;
    int getPendingCommandCount() const;

signals:
    // D-Bus signals
    void ConnectionStatusChanged(const QString& status);
    void CommandAcknowledged(uchar cmdId);
    void CommandError(uchar cmdId, uchar errorCode);
    void DeviceStatusChanged(uchar deviceId, uchar newStatus);
    void PresenceDetected(bool isPresent);
    void OTAProgress(int percentage);

public slots:
    // D-Bus methods
    Q_SCRIPTABLE bool SendCommand(uchar cmdId, const QByteArray& payload);
    Q_SCRIPTABLE uchar GetDeviceStatus(uchar deviceId);
    Q_SCRIPTABLE QString GetConnectionStatus() const;
    Q_SCRIPTABLE bool ResetConnection();
    Q_SCRIPTABLE bool StartOTA(uint firmwareSize);
    Q_SCRIPTABLE bool SendOTAChunk(uchar chunkNumber, const QByteArray& chunkData);
    Q_SCRIPTABLE bool EndOTA();

    // Command convenience methods
    Q_SCRIPTABLE bool ControlLight(uchar lightId, bool on);
    Q_SCRIPTABLE bool ControlCurtain(uchar curtainId, uchar action);  // 0=close, 1=open, 2=stop
    Q_SCRIPTABLE bool ControlProjector(bool on);
    Q_SCRIPTABLE bool ControlAC(uchar acId, bool on);

protected slots:
    // UART port signal handlers
    void onFrameReceived(const UartFrame& frame);
    void onPortError(const QString& error);
    void onConnectionStatusChanged(bool connected);

    // Internal timers and processing
    void onRetryTimeout();
    void processCommandQueue();
    void handleMcuResponse(const UartFrame& frame);

private:
    // Command handling
    bool queueCommand(UART::CommandId cmdId, const QByteArray& payload, int maxRetries = 3);
    bool sendPendingCommand();
    void retryPendingCommand();
    void commandSucceeded(UART::CommandId cmdId);
    void commandFailed(UART::CommandId cmdId, const QString& reason);

    // Device state tracking
    void updateDeviceStatus(uchar deviceId, uchar status);
    uchar getCachedDeviceStatus(uchar deviceId) const;

    // OTA handling
    bool startOtaTransfer(uint firmwareSize);
    void endOtaTransfer();
    void sendNextOtaChunk();

    // Configuration
    QString m_uartPortName;
    int m_commandTimeoutMs;
    int m_defaultRetries;

    // UART communication
    UartPort* m_uartPort;
    bool m_isConnected;
    bool m_reconnecting;

    // Command queue and retry
    QQueue<PendingCommand> m_commandQueue;
    PendingCommand m_currentCommand;
    bool m_commandPending;
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
