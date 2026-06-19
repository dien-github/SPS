#include "protocol_router.h"
#include "../common/sps_logger.h"
#include "../common/sps_runtime_config.h"
#include "sps_uart_protocol.h"
#include <QThread>
#include <QTimer>

/** Constructor. Initializes member variables, connects the retry timer, and logs startup. */
ProtocolRouter::ProtocolRouter(QObject* parent)
    : SpsServiceBase("com.sps.router", "/com/sps/router", parent),
      m_uartPortName(SPS::Runtime::envString("SPS_UART_PORT", UART::DEFAULT_PORT)),
      m_commandTimeoutMs(1000),
      m_defaultRetries(3),
      m_uartPort(nullptr),
      m_isConnected(false),
      m_reconnecting(false),
      m_commandPending(false),
      m_otaInProgress(false),
      m_otaFileSize(0),
      m_otaBytesReceived(0),
      m_otaCurrentChunk(0),
      m_totalCommands(0),
      m_successfulCommands(0),
      m_failedCommands(0),
      m_totalRetries(0) {

    // Setup retry timer
    connect(&m_retryTimer, &QTimer::timeout, this, &ProtocolRouter::onRetryTimeout);
    m_retryTimer.setSingleShot(true);

    logInfo("Protocol Router service created");
}

/** Destructor. Calls shutdown to stop the service and release resources. */
ProtocolRouter::~ProtocolRouter() {
    shutdown();
}

/** Initializes the service: creates UART port, connects signals, and registers D-Bus. */
bool ProtocolRouter::initialize() {
    logInfo("Initializing Protocol Router service...");

    // Create UART port
    m_uartPort = new UartPort(this);
    logInfo(QString("UART config: port=%1, baud=%2, available=[%3]")
        .arg(m_uartPortName)
        .arg(UART::BAUDRATE)
        .arg(UartPort::getAvailablePorts().join(", ")));

    // Connect UART signals
    connect(m_uartPort, &UartPort::frameReceived, this, &ProtocolRouter::onFrameReceived);
    connect(m_uartPort, &UartPort::errorOccurred, this, &ProtocolRouter::onPortError);
    connect(m_uartPort, &UartPort::connectionStatusChanged, 
            this, &ProtocolRouter::onConnectionStatusChanged);

    // Connect MCU
    if (!connectMcu(m_uartPortName)) {
        logWarning("Failed to connect MCU on startup - will retry");
    }

    // Register D-Bus service
    if (!registerService()) {
        logError("Failed to register D-Bus service");
        return false;
    }

    setRunning(true);
    logInfo("Protocol Router service initialized");

    return true;
}

/** Shuts down the service: stops timers, disconnects MCU, and cleans up. */
void ProtocolRouter::shutdown() {
    logInfo("Shutting down Protocol Router service...");

    m_retryTimer.stop();

    if (m_isConnected) {
        disconnectMcu();
    }

    if (m_uartPort) {
        m_uartPort->deleteLater();
        m_uartPort = nullptr;
    }

    SpsServiceBase::shutdown();
}

/** Returns a human-readable status string with connection and command statistics. */
QString ProtocolRouter::getStatus() const {
    return QString("Router Status: %1 | Commands: %2/%3/%4 | Retries: %5")
        .arg(getConnectionStatus())
        .arg(m_successfulCommands)
        .arg(m_failedCommands)
        .arg(m_totalCommands)
        .arg(m_totalRetries);
}

/** Opens a UART connection to the MCU on the specified port and sends a heartbeat. */
bool ProtocolRouter::connectMcu(const QString& portName) {
    m_uartPortName = SPS::Runtime::envString("SPS_UART_PORT", portName);

    if (!m_uartPort) {
        logError("UART port not initialized");
        return false;
    }

    if (!m_uartPort->openPort(m_uartPortName)) {
        logError(QString("Failed to open UART port: %1").arg(m_uartPortName));
        emit ConnectionStatusChanged("DISCONNECTED");
        return false;
    }

    logInfo(QString("Connected to MCU on port: %1").arg(m_uartPortName));

    // Send heartbeat to verify connection
    SendCommand(static_cast<uchar>(UART::CommandId::PING_HEARTBEAT), QByteArray());

    return true;
}

/** Closes the MCU connection and clears the command queue. */
bool ProtocolRouter::disconnectMcu() {
    if (m_uartPort && m_uartPort->isOpen()) {
        m_uartPort->closePort();
        logInfo("MCU disconnected");
    }

    m_isConnected = false;
    m_commandPending = false;
    m_commandQueue.clear();
    m_retryTimer.stop();

    emit ConnectionStatusChanged("DISCONNECTED");
    return true;
}

/** Returns true if the UART port is open and connected. */
bool ProtocolRouter::isConnected() const {
    return m_isConnected && m_uartPort && m_uartPort->isOpen();
}

/** Returns the current connection status as a string (CONNECTED/RECONNECTING/DISCONNECTED). */
QString ProtocolRouter::getConnectionStatus() const {
    if (m_isConnected) {
        return "CONNECTED";
    } else if (m_reconnecting) {
        return "RECONNECTING";
    } else {
        return "DISCONNECTED";
    }
}

/** Returns the number of commands waiting in the queue. */
int ProtocolRouter::getCommandQueueSize() const {
    return m_commandQueue.size();
}

/** Returns 1 if a command is pending acknowledgment, 0 otherwise. */
int ProtocolRouter::getPendingCommandCount() const {
    return m_commandPending ? 1 : 0;
}

/** D-Bus callable. Sends a command to the MCU with the given ID and payload. */
bool ProtocolRouter::SendCommand(uchar cmdId, const QByteArray& payload) {
    if (!isConnected()) {
        logWarning("Not connected to MCU");
        return false;
    }

    m_totalCommands++;

    QByteArray cmdPayload = payload;
    return queueCommand(static_cast<UART::CommandId>(cmdId), cmdPayload, m_defaultRetries);
}

/** D-Bus callable. Queries a device's status and returns the cached value. */
uchar ProtocolRouter::GetDeviceStatus(uchar deviceId) {
    logDebug(QString("Query device status: 0x%1").arg(deviceId, 2, 16, QChar('0')));

    // Send status query command
    QByteArray payload = SPS::UART::buildQueryRelayStatusPayload(deviceId);
    SendCommand(static_cast<uchar>(UART::CommandId::QUERY_RELAY_STATUS), payload);

    // Return cached status while waiting for response
    return getCachedDeviceStatus(deviceId);
}

/** D-Bus callable. Returns the current connection status string. */
QString ProtocolRouter::GetConnectionStatus() const {
    return getConnectionStatus();
}

/** D-Bus callable. Disconnects and reconnects to the MCU. */
bool ProtocolRouter::ResetConnection() {
    logInfo("Resetting MCU connection...");
    disconnectMcu();
    QThread::msleep(500);
    return connectMcu(m_uartPortName);
}

/** D-Bus callable. Initiates an OTA firmware update with the given total size. */
bool ProtocolRouter::StartOTA(uint firmwareSize) {
    if (!isConnected()) {
        logError("Not connected for OTA");
        return false;
    }

    logInfo(QString("Starting OTA - firmware size: %1 bytes").arg(firmwareSize));

    QByteArray payload = SPS::UART::buildOtaStartPayload(firmwareSize);

    if (!queueCommand(UART::CommandId::OTA_START, payload, 1)) {
        logError("Failed to queue OTA_START command");
        return false;
    }

    m_otaInProgress = true;
    m_otaFileSize = firmwareSize;
    m_otaBytesReceived = 0;
    m_otaCurrentChunk = 0;

    return true;
}

/** D-Bus callable. Sends a single 128-byte OTA data chunk. */
bool ProtocolRouter::SendOTAChunk(uchar chunkNumber, const QByteArray& chunkData) {
    if (!m_otaInProgress || chunkData.size() != SPS::UART::Payload::OTA_CHUNK_DATA_SIZE) {
        logError(QString("Invalid OTA chunk - size: %1").arg(chunkData.size()));
        return false;
    }

    QByteArray payload = SPS::UART::buildOtaDataChunkPayload(chunkNumber, chunkData);

    m_otaBytesReceived += SPS::UART::Payload::OTA_CHUNK_DATA_SIZE;
    int percentage = (m_otaBytesReceived * 100) / m_otaFileSize;

    logDebug(QString("OTA Progress: %1%").arg(percentage));
    emit OTAProgress(percentage);

    return queueCommand(UART::CommandId::OTA_DATA_CHUNK, payload, 3);
}

/** D-Bus callable. Finalizes the OTA update transfer. */
bool ProtocolRouter::EndOTA() {
    if (!m_otaInProgress) {
        logWarning("OTA not in progress");
        return false;
    }

    logInfo("Ending OTA transfer");
    m_otaInProgress = false;

    return queueCommand(UART::CommandId::OTA_END, QByteArray(), 1);
}

// D-Bus Methods: Device control convenience methods

/** Turns a light on (true) or off (false). */
bool ProtocolRouter::ControlLight(uchar lightId, bool on) {
    QByteArray payload = SPS::UART::buildLightControlPayload(lightId, on);
    return SendCommand(static_cast<uchar>(UART::CommandId::LIGHT_CONTROL), payload);
}

/** Controls a curtain: 0=close, 1=open, 2=stop. */
bool ProtocolRouter::ControlCurtain(uchar curtainId, uchar action) {
    QByteArray payload = SPS::UART::buildCurtainControlPayload(
        curtainId, static_cast<SPS::UART::ControlValue>(action));
    return SendCommand(static_cast<uchar>(UART::CommandId::CURTAIN_CONTROL), payload);
}

/** Turns the projector on (true) or off (false). */
bool ProtocolRouter::ControlProjector(bool on) {
    QByteArray payload = SPS::UART::buildProjectorControlPayload(on);
    return SendCommand(static_cast<uchar>(UART::CommandId::PROJECTOR_CONTROL), payload);
}

/** Turns an air conditioner on (true) or off (false). */
bool ProtocolRouter::ControlAC(uchar acId, bool on) {
    QByteArray payload = SPS::UART::buildAcControlPayload(acId, on);
    return SendCommand(static_cast<uchar>(UART::CommandId::AC_CONTROL), payload);
}

// ========== Private Methods ==========

/** Adds a command to the transmit queue and starts processing if idle. */
bool ProtocolRouter::queueCommand(UART::CommandId cmdId, const QByteArray& payload, int maxRetries) {
    if (!isConnected()) {
        logError("Not connected - cannot queue command");
        return false;
    }

    PendingCommand cmd;
    cmd.cmdId = cmdId;
    cmd.payload = payload;
    cmd.retryCount = 0;
    cmd.maxRetries = maxRetries;
    cmd.timestamp = QDateTime::currentMSecsSinceEpoch();

    m_commandQueue.enqueue(cmd);

    logDebug(QString("Command queued: 0x%1 (queue size: %2)")
        .arg(static_cast<int>(cmdId), 2, 16, QChar('0')).arg(m_commandQueue.size()));

    // Process queue if not busy
    if (!m_commandPending) {
        processCommandQueue();
    }

    return true;
}

/** Transmits the next command from the queue over UART and starts the timeout timer. */
bool ProtocolRouter::sendPendingCommand() {
    if (m_commandQueue.isEmpty()) {
        m_commandPending = false;
        return true;
    }

    m_currentCommand = m_commandQueue.dequeue();
    m_commandPending = true;

    QByteArray frame = UartFrame::buildFrame(m_currentCommand.cmdId, m_currentCommand.payload);

    logDebug(QString("Sending command: 0x%1 (size: %2)")
        .arg(static_cast<int>(m_currentCommand.cmdId), 2, 16, QChar('0'))
        .arg(frame.size()));
    logDebug(QString("UART TX: %1").arg(QString::fromLatin1(frame.toHex(' ').toUpper())));

    if (!m_uartPort->sendFrame(frame)) {
        logError("Failed to send frame");
        retryPendingCommand();
        return false;
    }

    // Start timeout timer
    m_retryTimer.start(m_commandTimeoutMs);

    return true;
}

/** Retries the current pending command up to maxRetries, then marks it as failed. */
void ProtocolRouter::retryPendingCommand() {
    if (m_currentCommand.retryCount < m_currentCommand.maxRetries) {
        m_currentCommand.retryCount++;
        m_totalRetries++;

        logWarning(QString("Retrying command: 0x%1 (attempt %2/%3)")
            .arg(static_cast<int>(m_currentCommand.cmdId), 2, 16, QChar('0'))
            .arg(m_currentCommand.retryCount)
            .arg(m_currentCommand.maxRetries));

        m_commandQueue.prepend(m_currentCommand);
        m_commandPending = false;
        processCommandQueue();
    } else {
        commandFailed(m_currentCommand.cmdId, "Max retries exceeded");
        m_commandPending = false;
        processCommandQueue();
    }
}

/** Marks the current command as successful and processes the next in queue. */
void ProtocolRouter::commandSucceeded(UART::CommandId cmdId) {
    m_successfulCommands++;
    m_retryTimer.stop();
    m_commandPending = false;

    logDebug(QString("Command succeeded: 0x%1").arg(static_cast<int>(cmdId), 2, 16, QChar('0')));

    processCommandQueue();
}

/** Marks the current command as failed and emits a CommandError signal. */
void ProtocolRouter::commandFailed(UART::CommandId cmdId, const QString& reason) {
    m_failedCommands++;
    m_retryTimer.stop();
    m_commandPending = false;

    logError(QString("Command failed: 0x%1 - %2")
        .arg(static_cast<int>(cmdId), 2, 16, QChar('0')).arg(reason));

    emit CommandError(static_cast<uchar>(cmdId), 0xFF);
}

/** Sends the next queued command if none is pending and the MCU is connected. */
void ProtocolRouter::processCommandQueue() {
    if (!isConnected() || m_commandPending) {
        return;
    }

    if (!m_commandQueue.isEmpty()) {
        sendPendingCommand();
    }
}

/** Parses and handles an MCU response frame (ACK, NACK, status, presence, etc.). */
void ProtocolRouter::handleMcuResponse(const UartFrame& frame) {
    UART::CommandId cmdId = frame.getCommandId();

    switch (cmdId) {
        case UART::CommandId::ACK_ALIVE:
            // Extract original command ID from payload
            {
                uint8_t originalCmdId = 0;
                if (!SPS::UART::parseAckPayload(frame.getPayload(), originalCmdId)) {
                    break;
                }
                logDebug(QString("ACK received for command: 0x%1").arg(originalCmdId, 2, 16, QChar('0')));
                emit CommandAcknowledged(originalCmdId);
            }
            commandSucceeded(UART::CommandId::ACK_ALIVE);
            break;

        case UART::CommandId::NACK_ERROR: {
            uint8_t errorCmdId = 0;
            uint8_t errorCode = 0;
            if (!SPS::UART::parseNackPayload(frame.getPayload(), errorCmdId, errorCode)) {
                break;
            }
            logError(QString("NACK: command 0x%1, error 0x%2")
                .arg(errorCmdId, 2, 16, QChar('0'))
                .arg(errorCode, 2, 16, QChar('0')));
            emit CommandError(errorCmdId, errorCode);
            retryPendingCommand();
            break;
        }

        case UART::CommandId::QUERY_RELAY_STATUS: {
            uint8_t deviceId = 0;
            uint8_t status = 0;
            if (SPS::UART::parseDeviceStatusPayload(frame.getPayload(), deviceId, status)) {
                updateDeviceStatus(static_cast<uchar>(deviceId), static_cast<uchar>(status));
                emit DeviceStatusChanged(static_cast<uchar>(deviceId), static_cast<uchar>(status));
            }
            commandSucceeded(cmdId);
            break;
        }

        case UART::CommandId::PRESENCE_ALERT: {
            bool isPresent = false;
            if (SPS::UART::parsePresencePayload(frame.getPayload(), isPresent)) {
                logDebug(QString("Presence: %1").arg(isPresent ? "Yes" : "No"));
                emit PresenceDetected(isPresent);
            }
            break;
        }

        default:
            logWarning(QString("Unknown response: 0x%1").arg(static_cast<int>(cmdId), 2, 16, QChar('0')));
            break;
    }
}

/** Updates the cached status for a device. Thread-safe. */
void ProtocolRouter::updateDeviceStatus(uchar deviceId, uchar status) {
    QMutexLocker lock(&m_statusMutex);
    m_deviceStatusCache[deviceId] = status;
}

/** Returns the cached status for a device, or 0xFF if unknown. Thread-safe. */
uchar ProtocolRouter::getCachedDeviceStatus(uchar deviceId) const {
    QMutexLocker lock(&m_statusMutex);
    return m_deviceStatusCache.value(deviceId, 0xFF);  // 0xFF = unknown
}

// ========== Signal Handlers ==========

/** Slot. Handles an incoming UART frame by passing it to handleMcuResponse. */
void ProtocolRouter::onFrameReceived(const UartFrame& frame) {
    logDebug(QString("Frame received: 0x%1, size: %2")
        .arg(static_cast<int>(frame.getCommandId()), 2, 16, QChar('0'))
        .arg(frame.getPayload().size()));
    logDebug(QString("UART RX: %1").arg(QString::fromLatin1(frame.toByteArray().toHex(' ').toUpper())));

    handleMcuResponse(frame);
}

/** Slot. Handles a UART port error and emits ConnectionStatusChanged(ERROR). */
void ProtocolRouter::onPortError(const QString& error) {
    logError(QString("UART error: %1").arg(error));
    emit ConnectionStatusChanged("ERROR");
}

/** Slot. Updates connection state and emits the appropriate status signal. */
void ProtocolRouter::onConnectionStatusChanged(bool connected) {
    m_isConnected = connected;

    if (connected) {
        logInfo("MCU connection established");
        emit ConnectionStatusChanged("CONNECTED");
    } else {
        logWarning("MCU connection lost");
        m_commandPending = false;
        m_retryTimer.stop();
        emit ConnectionStatusChanged("DISCONNECTED");
    }
}

/** Slot. Called when the retry timer expires; retries or fails the current command. */
void ProtocolRouter::onRetryTimeout() {
    if (m_commandPending) {
        logWarning("Command timeout - retrying");
        retryPendingCommand();
    }
}
