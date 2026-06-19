#include "protocol_router.h"
#include "../common/sps_logger.h"
#include "../common/sps_runtime_config.h"
#include <QThread>
#include <QTimer>

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

ProtocolRouter::~ProtocolRouter() {
    shutdown();
}

// Initialize service
bool ProtocolRouter::initialize() {
    logInfo("Initializing Protocol Router service...");

    // Create UART port
    m_uartPort = new UartPort(this);

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

// Shutdown service
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

// Get service status
QString ProtocolRouter::getStatus() const {
    return QString("Router Status: %1 | Commands: %2/%3/%4 | Retries: %5")
        .arg(getConnectionStatus())
        .arg(m_successfulCommands)
        .arg(m_failedCommands)
        .arg(m_totalCommands)
        .arg(m_totalRetries);
}

// Connect to MCU via UART
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

// Disconnect from MCU
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

// Check if connected
bool ProtocolRouter::isConnected() const {
    return m_isConnected && m_uartPort && m_uartPort->isOpen();
}

// Get connection status string
QString ProtocolRouter::getConnectionStatus() const {
    if (m_isConnected) {
        return "CONNECTED";
    } else if (m_reconnecting) {
        return "RECONNECTING";
    } else {
        return "DISCONNECTED";
    }
}

// Get command queue size
int ProtocolRouter::getCommandQueueSize() const {
    return m_commandQueue.size();
}

// Get pending command count
int ProtocolRouter::getPendingCommandCount() const {
    return m_commandPending ? 1 : 0;
}

// D-Bus Method: SendCommand
bool ProtocolRouter::SendCommand(uchar cmdId, const QByteArray& payload) {
    if (!isConnected()) {
        logWarning("Not connected to MCU");
        return false;
    }

    m_totalCommands++;

    QByteArray cmdPayload = payload;
    return queueCommand(static_cast<UART::CommandId>(cmdId), cmdPayload, m_defaultRetries);
}

// D-Bus Method: GetDeviceStatus
uchar ProtocolRouter::GetDeviceStatus(uchar deviceId) {
    logDebug(QString("Query device status: 0x%1").arg(deviceId, 2, 16, QChar('0')));

    // Send status query command
    QByteArray payload;
    payload.append(static_cast<char>(deviceId));
    SendCommand(static_cast<uchar>(UART::CommandId::QUERY_RELAY_STATUS), payload);

    // Return cached status while waiting for response
    return getCachedDeviceStatus(deviceId);
}

// D-Bus Method: GetConnectionStatus
QString ProtocolRouter::GetConnectionStatus() const {
    return getConnectionStatus();
}

// D-Bus Method: ResetConnection
bool ProtocolRouter::ResetConnection() {
    logInfo("Resetting MCU connection...");
    disconnectMcu();
    QThread::msleep(500);
    return connectMcu(m_uartPortName);
}

// D-Bus Method: StartOTA
bool ProtocolRouter::StartOTA(uint firmwareSize) {
    if (!isConnected()) {
        logError("Not connected for OTA");
        return false;
    }

    logInfo(QString("Starting OTA - firmware size: %1 bytes").arg(firmwareSize));

    QByteArray payload;
    payload.append(static_cast<char>((firmwareSize >> 24) & 0xFF));
    payload.append(static_cast<char>((firmwareSize >> 16) & 0xFF));
    payload.append(static_cast<char>((firmwareSize >> 8) & 0xFF));
    payload.append(static_cast<char>(firmwareSize & 0xFF));

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

// D-Bus Method: SendOTAChunk
bool ProtocolRouter::SendOTAChunk(uchar chunkNumber, const QByteArray& chunkData) {
    if (!m_otaInProgress || chunkData.size() != 128) {
        logError(QString("Invalid OTA chunk - size: %1").arg(chunkData.size()));
        return false;
    }

    QByteArray payload;
    payload.append(static_cast<char>(chunkNumber));
    payload.append(chunkData);

    m_otaBytesReceived += 128;
    int percentage = (m_otaBytesReceived * 100) / m_otaFileSize;

    logDebug(QString("OTA Progress: %1%").arg(percentage));
    emit OTAProgress(percentage);

    return queueCommand(UART::CommandId::OTA_DATA_CHUNK, payload, 3);
}

// D-Bus Method: EndOTA
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

bool ProtocolRouter::ControlLight(uchar lightId, bool on) {
    QByteArray payload;
    payload.append(static_cast<char>(lightId));
    payload.append(on ? 0x01 : 0x00);
    return SendCommand(static_cast<uchar>(UART::CommandId::LIGHT_CONTROL), payload);
}

bool ProtocolRouter::ControlCurtain(uchar curtainId, uchar action) {
    QByteArray payload;
    payload.append(static_cast<char>(curtainId));
    payload.append(static_cast<char>(action));
    return SendCommand(static_cast<uchar>(UART::CommandId::CURTAIN_CONTROL), payload);
}

bool ProtocolRouter::ControlProjector(bool on) {
    QByteArray payload;
    payload.append(on ? 0x01 : 0x00);
    return SendCommand(static_cast<uchar>(UART::CommandId::PROJECTOR_CONTROL), payload);
}

bool ProtocolRouter::ControlAC(uchar acId, bool on) {
    QByteArray payload;
    payload.append(static_cast<char>(acId));
    payload.append(on ? 0x01 : 0x00);
    return SendCommand(static_cast<uchar>(UART::CommandId::AC_CONTROL), payload);
}

// ========== Private Methods ==========

// Queue a command for transmission
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

// Send next command in queue
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

    if (!m_uartPort->sendFrame(frame)) {
        logError("Failed to send frame");
        retryPendingCommand();
        return false;
    }

    // Start timeout timer
    m_retryTimer.start(m_commandTimeoutMs);

    return true;
}

// Retry a failed command
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

// Command succeeded
void ProtocolRouter::commandSucceeded(UART::CommandId cmdId) {
    m_successfulCommands++;
    m_retryTimer.stop();
    m_commandPending = false;

    logDebug(QString("Command succeeded: 0x%1").arg(static_cast<int>(cmdId), 2, 16, QChar('0')));

    processCommandQueue();
}

// Command failed
void ProtocolRouter::commandFailed(UART::CommandId cmdId, const QString& reason) {
    m_failedCommands++;
    m_retryTimer.stop();
    m_commandPending = false;

    logError(QString("Command failed: 0x%1 - %2")
        .arg(static_cast<int>(cmdId), 2, 16, QChar('0')).arg(reason));

    emit CommandError(static_cast<uchar>(cmdId), 0xFF);
}

// Process command queue
void ProtocolRouter::processCommandQueue() {
    if (!isConnected() || m_commandPending) {
        return;
    }

    if (!m_commandQueue.isEmpty()) {
        sendPendingCommand();
    }
}

// Handle MCU response frame
void ProtocolRouter::handleMcuResponse(const UartFrame& frame) {
    UART::CommandId cmdId = frame.getCommandId();

    switch (cmdId) {
        case UART::CommandId::ACK_ALIVE:
            // Extract original command ID from payload
            if (frame.getPayload().size() > 0) {
                uchar originalCmdId = static_cast<uchar>(frame.getPayload()[0]);
                logDebug(QString("ACK received for command: 0x%1").arg(originalCmdId, 2, 16, QChar('0')));
                emit CommandAcknowledged(originalCmdId);
            }
            commandSucceeded(UART::CommandId::ACK_ALIVE);
            break;

        case UART::CommandId::NACK_ERROR: {
            if (frame.getPayload().size() >= 2) {
                uchar errorCmdId = static_cast<uchar>(frame.getPayload()[0]);
                uchar errorCode = static_cast<uchar>(frame.getPayload()[1]);
                logError(QString("NACK: command 0x%1, error 0x%2")
                    .arg(errorCmdId, 2, 16, QChar('0'))
                    .arg(errorCode, 2, 16, QChar('0')));
                emit CommandError(errorCmdId, errorCode);
                retryPendingCommand();
            }
            break;
        }

        case UART::CommandId::QUERY_RELAY_STATUS: {
            if (frame.getPayload().size() >= 2) {
                uchar deviceId = static_cast<uchar>(frame.getPayload()[0]);
                uchar status = static_cast<uchar>(frame.getPayload()[1]);
                updateDeviceStatus(deviceId, status);
                emit DeviceStatusChanged(deviceId, status);
            }
            commandSucceeded(cmdId);
            break;
        }

        case UART::CommandId::PRESENCE_ALERT: {
            if (frame.getPayload().size() > 0) {
                bool isPresent = frame.getPayload()[0] != 0;
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

// Update device status cache
void ProtocolRouter::updateDeviceStatus(uchar deviceId, uchar status) {
    QMutexLocker lock(&m_statusMutex);
    m_deviceStatusCache[deviceId] = status;
}

// Get cached device status
uchar ProtocolRouter::getCachedDeviceStatus(uchar deviceId) const {
    QMutexLocker lock(&m_statusMutex);
    return m_deviceStatusCache.value(deviceId, 0xFF);  // 0xFF = unknown
}

// ========== Signal Handlers ==========

// Slot: Frame received from UART
void ProtocolRouter::onFrameReceived(const UartFrame& frame) {
    logDebug(QString("Frame received: 0x%1, size: %2")
        .arg(static_cast<int>(frame.getCommandId()), 2, 16, QChar('0'))
        .arg(frame.getPayload().size()));

    handleMcuResponse(frame);
}

// Slot: UART port error
void ProtocolRouter::onPortError(const QString& error) {
    logError(QString("UART error: %1").arg(error));
    emit ConnectionStatusChanged("ERROR");
}

// Slot: Connection status changed
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

// Slot: Retry timer timeout
void ProtocolRouter::onRetryTimeout() {
    if (m_commandPending) {
        logWarning("Command timeout - retrying");
        retryPendingCommand();
    }
}
