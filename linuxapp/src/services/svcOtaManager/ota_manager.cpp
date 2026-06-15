#include "ota_manager.h"
#include "../common/sps_logger.h"
#include "../common/sps_constants.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDBusInterface>
#include <QDBusConnection>
#include <QDBusReply>
#include <QDBusError>
#include <QCoreApplication>

// Service binary names mapped to their install paths
static const QMap<QString, QString> s_serviceMap = {
    {"svcAuthentication",  "/opt/sps/bin/svcAuthentication"},
    {"svcProtocolRouter",  "/opt/sps/bin/svcProtocolRouter"},
    {"svcAutoEngine",      "/opt/sps/bin/svcAutoEngine"},
    {"svcNetworkManager",  "/opt/sps/bin/svcNetworkManager"},
    {"svcOtaManager",      "/opt/sps/bin/svcOtaManager"},
    {"appHmi",             "/opt/sps/bin/appHmi"}
};

OtaManager::OtaManager(QObject* parent)
    : SpsServiceBase("com.sps.otamanager", "/com/sps/otamanager", parent),
      m_configPath("/opt/sps/config/config.json"),
      m_downloadDir("/opt/sps/updates"),
      m_versionFilePath("/opt/sps/config/version.json"),
      m_systemdUnitDir("/etc/systemd/system"),
      m_serviceInstallDir("/opt/sps/bin"),
      m_updateTimeoutMs(300000),
      m_mcuChunkSize(128),
      m_networkManager(nullptr),
      m_currentReply(nullptr),
      m_downloadFile(nullptr),
      m_totalUpdates(0),
      m_successfulUpdates(0),
      m_failedUpdates(0) {

    connect(&m_updateTimer, &QTimer::timeout, this, &OtaManager::onUpdateTimeout);
    m_updateTimer.setSingleShot(true);

    connect(&m_versionCheckTimer, &QTimer::timeout, this, &OtaManager::onVersionCheckTimeout);

    logInfo("OTA Manager service created");
}

OtaManager::~OtaManager() {
    shutdown();
}

// Initialize service
bool OtaManager::initialize() {
    logInfo("Initializing OTA Manager service...");

    m_networkManager = new QNetworkAccessManager(this);

    // Ensure download directory exists
    QDir().mkpath(m_downloadDir);

    // Load configuration
    QFile configFile(m_configPath);
    if (configFile.open(QIODevice::ReadOnly)) {
        QByteArray data = configFile.readAll();
        configFile.close();

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            QJsonObject config = doc.object();
            if (config.contains("download_dir"))
                m_downloadDir = config["download_dir"].toString();
            if (config.contains("update_timeout_ms"))
                m_updateTimeoutMs = config["update_timeout_ms"].toInt(300000);
        }
    }

    logInfo(QString("Download directory: %1").arg(m_downloadDir));

    // Check current version
    checkCurrentVersion();

    // Connect to other services via D-Bus
    connectToNetworkManager();
    connectToProtocolRouter();

    // Periodic version check (every 1 hour)
    m_versionCheckTimer.start(3600000);

    // Register D-Bus service
    if (!registerService()) {
        logError("Failed to register D-Bus service");
        return false;
    }

    setRunning(true);
    logInfo("OTA Manager service initialized");

    return true;
}

// Shutdown service
void OtaManager::shutdown() {
    logInfo("Shutting down OTA Manager service...");

    m_updateTimer.stop();
    m_versionCheckTimer.stop();

    if (m_currentReply) {
        m_currentReply->abort();
    }

    if (m_downloadFile) {
        m_downloadFile->close();
    }

    // Clean up incomplete downloads
    QDir downloadDir(m_downloadDir);
    QStringList filters;
    filters << "*.tmp" << "*.part";
    for (const QString& file : downloadDir.entryList(filters, QDir::Files)) {
        QFile::remove(downloadDir.absoluteFilePath(file));
    }

    SpsServiceBase::shutdown();
}

// Get service status
QString OtaManager::getStatus() const {
    return QString("OTA Status: %1 | Progress: %2% | Total: %3, OK: %4, Fail: %5")
        .arg(stageToString(m_state.stage))
        .arg(m_state.progress)
        .arg(m_totalUpdates)
        .arg(m_successfulUpdates)
        .arg(m_failedUpdates);
}

// Convert stage enum to string
QString OtaManager::stageToString(OtaStage stage) {
    switch (stage) {
        case OtaStage::IDLE:             return "IDLE";
        case OtaStage::DOWNLOADING_MCU:  return "DOWNLOADING_MCU";
        case OtaStage::FLASHING_MCU:     return "FLASHING_MCU";
        case OtaStage::DOWNLOADING_APPS: return "DOWNLOADING_APPS";
        case OtaStage::UPDATING_APPS:    return "UPDATING_APPS";
        case OtaStage::VERIFYING:        return "VERIFYING";
        case OtaStage::COMPLETED:        return "COMPLETED";
        case OtaStage::FAILED:           return "FAILED";
    }
    return "UNKNOWN";
}

// ========== D-Bus Methods ==========

QString OtaManager::GetOtaStatus() const {
    return stageToString(m_state.stage);
}

bool OtaManager::StartMcuFirmwareUpdate(const QString& firmwareUrl, const QString& expectedChecksum) {
    if (m_state.stage != OtaStage::IDLE) {
        logWarning("Update already in progress");
        return false;
    }

    logInfo(QString("Starting MCU firmware update from: %1").arg(firmwareUrl));
    m_totalUpdates++;

    m_state = OtaUpdateState();
    m_state.mcuFirmwareUrl = firmwareUrl;
    m_state.mcuChecksum = expectedChecksum;
    m_state.stage = OtaStage::DOWNLOADING_MCU;
    m_state.progress = 0;

    emit UpdateStatusChanged(stageToString(m_state.stage));

    startNextStage();
    return true;
}

bool OtaManager::StartAppServiceUpdate(const QString& serviceName, const QString& packageUrl, const QString& expectedChecksum) {
    if (m_state.stage != OtaStage::IDLE) {
        logWarning("Update already in progress");
        return false;
    }

    if (!s_serviceMap.contains(serviceName)) {
        logError(QString("Unknown service: %1").arg(serviceName));
        return false;
    }

    logInfo(QString("Starting app service update for %1 from: %2").arg(serviceName, packageUrl));
    m_totalUpdates++;

    m_state = OtaUpdateState();
    m_state.appPackageUrl = packageUrl;
    m_state.appChecksum = expectedChecksum;
    m_state.stage = OtaStage::DOWNLOADING_APPS;
    m_state.progress = 0;
    m_currentServiceName = serviceName;

    emit UpdateStatusChanged(stageToString(m_state.stage));

    startNextStage();
    return true;
}

bool OtaManager::StartFullUpdate(const QString& mcuFirmwareUrl, const QString& mcuChecksum,
                                  const QString& appPackageUrl, const QString& appChecksum) {
    if (m_state.stage != OtaStage::IDLE) {
        logWarning("Update already in progress");
        return false;
    }

    logInfo("Starting full system update (MCU firmware + Linux apps)");
    m_totalUpdates++;

    m_state = OtaUpdateState();
    m_state.mcuFirmwareUrl = mcuFirmwareUrl;
    m_state.mcuChecksum = mcuChecksum;
    m_state.appPackageUrl = appPackageUrl;
    m_state.appChecksum = appChecksum;
    m_state.stage = OtaStage::DOWNLOADING_MCU;
    m_state.progress = 0;

    emit UpdateStatusChanged(stageToString(m_state.stage));

    startNextStage();
    return true;
}

int OtaManager::GetUpdateProgress() const {
    return m_state.progress;
}

QString OtaManager::GetCurrentVersion() const {
    return m_state.currentVersion;
}

bool OtaManager::CancelUpdate() {
    if (m_state.stage == OtaStage::IDLE || m_state.stage == OtaStage::COMPLETED) {
        return false;
    }

    logInfo("Cancelling ongoing update");
    m_state.cancelled = true;

    if (m_currentReply) {
        m_currentReply->abort();
    }

    if (m_downloadFile) {
        m_downloadFile->close();
        m_downloadFile->remove();
    }

    m_updateTimer.stop();
    m_state.stage = OtaStage::IDLE;
    m_state.progress = 0;

    emit UpdateStatusChanged(stageToString(m_state.stage));
    emit UpdateCompleted(false, "Update cancelled by user");

    return true;
}

// ========== D-Bus Signal Handlers ==========

void OtaManager::onOtaCommandReceived(const QString& firmwareUrl) {
    logInfo(QString("OTA command received from NetworkManager: %1").arg(firmwareUrl));

    if (m_state.stage == OtaStage::IDLE) {
        StartMcuFirmwareUpdate(firmwareUrl, QString());
    }
}

void OtaManager::onRouterConnectionStatusChanged(const QString& status) {
    logDebug(QString("Router connection status: %1").arg(status));
}

// ========== Stage Management ==========

void OtaManager::startNextStage() {
    if (m_state.cancelled) return;

    m_updateTimer.start(m_updateTimeoutMs);

    switch (m_state.stage) {
        case OtaStage::DOWNLOADING_MCU: {
            QString destPath = QString("%1/mcu_firmware.bin").arg(m_downloadDir);
            if (downloadFile(m_state.mcuFirmwareUrl, destPath)) {
                m_state.progress = 0;
            } else {
                setError("Failed to start MCU firmware download");
            }
            break;
        }

        case OtaStage::FLASHING_MCU: {
            QString firmwarePath = QString("%1/mcu_firmware.bin").arg(m_downloadDir);
            if (flashMcuFirmware(firmwarePath)) {
                m_state.stage = OtaStage::VERIFYING;
                m_state.progress = 90;
                emit UpdateProgress(m_state.progress, stageToString(m_state.stage));
                startNextStage();
            } else {
                setError("Failed to flash MCU firmware");
            }
            break;
        }

        case OtaStage::DOWNLOADING_APPS: {
            QString destPath = QString("%1/app_update.tar.gz").arg(m_downloadDir);
            if (downloadFile(m_state.appPackageUrl, destPath)) {
                m_state.progress = 50;
            } else {
                setError("Failed to start app package download");
            }
            break;
        }

        case OtaStage::UPDATING_APPS: {
            QString packagePath = QString("%1/app_update.tar.gz").arg(m_downloadDir);
            if (updateAppService(m_currentServiceName, packagePath)) {
                m_state.stage = OtaStage::VERIFYING;
                m_state.progress = 90;
                emit UpdateProgress(m_state.progress, stageToString(m_state.stage));
                startNextStage();
            } else {
                setError("Failed to update app service");
            }
            break;
        }

        case OtaStage::VERIFYING: {
            m_updateTimer.stop();
            m_state.stage = OtaStage::COMPLETED;
            m_state.progress = 100;
            m_successfulUpdates++;

            logInfo("Update completed successfully");
            emit UpdateStatusChanged(stageToString(m_state.stage));
            emit UpdateProgress(100, "COMPLETED");
            emit UpdateCompleted(true, "Update completed successfully");

            m_state.stage = OtaStage::IDLE;
            break;
        }

        default:
            break;
    }
}

// ========== Download Handling ==========

bool OtaManager::downloadFile(const QString& url, const QString& destPath) {
    logInfo(QString("Downloading: %1 -> %2").arg(url, destPath));

    if (url.isEmpty()) {
        logError("Download URL is empty");
        return false;
    }

    if (m_downloadFile) {
        m_downloadFile->close();
        delete m_downloadFile;
    }

    m_downloadFile = new QFile(destPath + ".part", this);
    if (!m_downloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        logError(QString("Cannot open file for writing: %1").arg(destPath + ".part"));
        return false;
    }

    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(m_updateTimeoutMs);

    m_currentReply = m_networkManager->get(request);

    connect(m_currentReply, &QNetworkReply::downloadProgress,
            this, &OtaManager::onDownloadProgress);
    connect(m_currentReply, &QNetworkReply::finished,
            this, &OtaManager::onDownloadFinished);
    connect(m_currentReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_downloadFile && m_currentReply) {
            m_downloadFile->write(m_currentReply->readAll());
        }
    });

    return true;
}

void OtaManager::onDownloadProgress(qint64 received, qint64 total) {
    if (total > 0) {
        int baseProgress = (m_state.stage == OtaStage::DOWNLOADING_MCU) ? 0 : 50;
        int rangeSize = (m_state.stage == OtaStage::DOWNLOADING_MCU) ? 40 : 30;
        m_state.progress = baseProgress + static_cast<int>((received * rangeSize) / total);

        emit UpdateProgress(m_state.progress, stageToString(m_state.stage));
        logDebug(QString("Download progress: %1/%2 bytes (%3%)")
            .arg(received).arg(total).arg(m_state.progress));
    }
}

void OtaManager::onDownloadFinished() {
    if (!m_currentReply || !m_downloadFile) return;

    m_updateTimer.stop();

    if (m_currentReply->error() != QNetworkReply::NoError) {
        QString errorMsg = QString("Download failed: %1").arg(m_currentReply->errorString());
        logError(errorMsg);
        m_downloadFile->close();
        m_downloadFile->remove();
        setError(errorMsg);
        return;
    }

    // Finish writing
    m_downloadFile->write(m_currentReply->readAll());
    m_downloadFile->flush();
    m_downloadFile->close();

    // Rename .part to final name
    QString finalPath = m_downloadFile->fileName();
    if (finalPath.endsWith(".part")) {
        QString newPath = finalPath.left(finalPath.length() - 5);
        QFile::remove(newPath);
        if (!m_downloadFile->rename(newPath)) {
            logError(QString("Failed to rename download: %1 -> %2").arg(finalPath, newPath));
            setError("Failed to finalize downloaded file");
            return;
        }
    }

    logInfo(QString("Download completed: %1").arg(m_downloadFile->fileName()));

    // Verify checksum if provided
    if (!m_state.mcuChecksum.isEmpty() &&
        m_state.stage == OtaStage::DOWNLOADING_MCU) {
        if (!verifyChecksum(m_downloadFile->fileName(), m_state.mcuChecksum)) {
            setError("MCU firmware checksum mismatch");
            return;
        }
        logInfo("MCU firmware checksum verified");
    }

    if (!m_state.appChecksum.isEmpty() &&
        m_state.stage == OtaStage::DOWNLOADING_APPS) {
        if (!verifyChecksum(m_downloadFile->fileName(), m_state.appChecksum)) {
            setError("App package checksum mismatch");
            return;
        }
        logInfo("App package checksum verified");
    }

    // Move to next stage
    if (m_state.stage == OtaStage::DOWNLOADING_MCU) {
        m_state.stage = OtaStage::FLASHING_MCU;
        emit UpdateStatusChanged(stageToString(m_state.stage));
        startNextStage();
    } else if (m_state.stage == OtaStage::DOWNLOADING_APPS) {
        m_state.stage = OtaStage::UPDATING_APPS;
        emit UpdateStatusChanged(stageToString(m_state.stage));
        startNextStage();
    }
}

bool OtaManager::verifyChecksum(const QString& filePath, const QString& expectedChecksum) {
    if (expectedChecksum.isEmpty()) {
        logInfo("No checksum provided, skipping verification");
        return true;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        logError(QString("Cannot open file for checksum: %1").arg(filePath));
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        logError("Failed to read file for checksum");
        return false;
    }

    QString actualChecksum = hash.result().toHex();
    file.close();

    logDebug(QString("Expected: %1, Actual: %2").arg(expectedChecksum, actualChecksum));

    if (actualChecksum != expectedChecksum) {
        logError(QString("Checksum mismatch! Expected: %1, Got: %2")
            .arg(expectedChecksum, actualChecksum));
        return false;
    }

    return true;
}

QString OtaManager::getDefaultDownloadPath() const {
    return m_downloadDir;
}

// ========== MCU Firmware Update ==========

bool OtaManager::flashMcuFirmware(const QString& firmwarePath) {
    logInfo(QString("Flashing MCU firmware: %1").arg(firmwarePath));

    // Check if protocol router is connected via D-Bus
    QDBusInterface routerIface(SPS::DBus::SERVICE_ROUTER,
                                SPS::DBus::PATH_ROUTER,
                                SPS::DBus::IFACE_ROUTER,
                                QDBusConnection::systemBus());

    if (!routerIface.isValid()) {
        logError("Protocol Router D-Bus interface not available");
        return false;
    }

    // Get connection status
    QDBusReply<QString> connReply = routerIface.call("GetConnectionStatus");
    if (!connReply.isValid() || connReply.value() != "CONNECTED") {
        logError("MCU not connected via UART");
        return false;
    }

    return sendOtaViaDbus(firmwarePath);
}

bool OtaManager::sendOtaViaDbus(const QString& firmwarePath) {
    QFile firmware(firmwarePath);
    if (!firmware.open(QIODevice::ReadOnly)) {
        logError(QString("Cannot open firmware file: %1").arg(firmwarePath));
        return false;
    }

    QByteArray firmwareData = firmware.readAll();
    firmware.close();

    uint firmwareSize = firmwareData.size();
    logInfo(QString("Firmware size: %1 bytes, sending via D-Bus to ProtocolRouter")
        .arg(firmwareSize));

    // Create D-Bus interface to ProtocolRouter
    QDBusInterface routerIface(SPS::DBus::SERVICE_ROUTER,
                                SPS::DBus::PATH_ROUTER,
                                SPS::DBus::IFACE_ROUTER,
                                QDBusConnection::systemBus());

    if (!routerIface.isValid()) {
        logError("Protocol Router D-Bus interface not available");
        return false;
    }

    // Connect to OTAProgress signal from router
    QDBusConnection::systemBus().connect(
        SPS::DBus::SERVICE_ROUTER,
        SPS::DBus::PATH_ROUTER,
        SPS::DBus::IFACE_ROUTER,
        "OTAProgress",
        this, SLOT(onMcuFlashProgress(int)));

    // Start OTA
    QDBusReply<bool> startReply = routerIface.call("StartOTA", firmwareSize);
    if (!startReply.isValid() || !startReply.value()) {
        logError("Failed to start OTA on ProtocolRouter");
        return false;
    }
    logInfo("OTA Start command sent successfully");

    // Send chunks
    int totalChunks = (firmwareSize + m_mcuChunkSize - 1) / m_mcuChunkSize;
    for (int i = 0; i < totalChunks; i++) {
        if (m_state.cancelled) {
            logWarning("OTA cancelled during chunk transfer");
            return false;
        }

        QByteArray chunk = firmwareData.mid(i * m_mcuChunkSize, m_mcuChunkSize);
        if (chunk.size() < m_mcuChunkSize) {
            // Pad with 0xFF for last chunk
            chunk.append(QByteArray(m_mcuChunkSize - chunk.size(), static_cast<char>(0xFF)));
        }

        QDBusReply<bool> chunkReply = routerIface.call(
            "SendOTAChunk",
            static_cast<uchar>(i),
            QByteArray(chunk));

        if (!chunkReply.isValid() || !chunkReply.value()) {
            logError(QString("Failed to send OTA chunk %1/%2").arg(i).arg(totalChunks));
            return false;
        }

        // Update progress (MCU flashing is 40%-85% of total)
        int progress = 40 + static_cast<int>((i * 45) / totalChunks);
        m_state.progress = progress;
        emit UpdateProgress(m_state.progress, stageToString(m_state.stage));
    }

    // End OTA
    QDBusReply<bool> endReply = routerIface.call("EndOTA");
    if (!endReply.isValid() || !endReply.value()) {
        logError("Failed to end OTA on ProtocolRouter");
        return false;
    }

    logInfo("MCU firmware flashed successfully via UART");
    return true;
}

// ========== Linux App Service Update ==========

bool OtaManager::updateAppService(const QString& serviceName, const QString& packagePath) {
    logInfo(QString("Updating app service: %1 from %2").arg(serviceName, packagePath));

    if (!s_serviceMap.contains(serviceName)) {
        logError(QString("Unknown service: %1").arg(serviceName));
        return false;
    }

    QString binaryPath = s_serviceMap[serviceName];
    QString backupPath = getServiceBackupPath(serviceName);

    // Stop service
    if (!stopService(serviceName)) {
        logError(QString("Failed to stop service: %1").arg(serviceName));
        return false;
    }

    // Backup old binary
    if (QFile::exists(binaryPath)) {
        QFile::remove(backupPath);
        if (!QFile::copy(binaryPath, backupPath)) {
            logWarning(QString("Failed to backup service binary: %1").arg(serviceName));
        } else {
            m_backupFiles.append(backupPath);
            logInfo(QString("Backed up %1 to %2").arg(binaryPath, backupPath));
        }
    }

    // Replace binary
    if (!replaceServiceBinary(serviceName, packagePath)) {
        logError(QString("Failed to replace service binary: %1").arg(serviceName));
        // Restore backup
        if (QFile::exists(backupPath)) {
            QFile::copy(backupPath, binaryPath);
            QFile::setPermissions(binaryPath,
                QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                QFile::ReadGroup | QFile::ExeGroup |
                QFile::ReadOther | QFile::ExeOther);
        }
        startService(serviceName);
        return false;
    }

    // Set executable permissions
    QFile::setPermissions(binaryPath,
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
        QFile::ReadGroup | QFile::ExeGroup |
        QFile::ReadOther | QFile::ExeOther);

    // Start service
    if (!startService(serviceName)) {
        logError(QString("Failed to start service after update: %1").arg(serviceName));
        return false;
    }

    logInfo(QString("Service %1 updated successfully").arg(serviceName));
    return true;
}

bool OtaManager::stopService(const QString& serviceName) {
    logInfo(QString("Stopping service: %1").arg(serviceName));

    QProcess process;
    process.start("systemctl", QStringList() << "stop" << QString("sps-%1.service").arg(serviceName));

    if (!process.waitForFinished(10000)) {
        process.kill();
        logWarning(QString("Timeout stopping service: %1").arg(serviceName));
        return false;
    }

    if (process.exitCode() != 0) {
        logWarning(QString("systemctl stop returned %1: %2")
            .arg(process.exitCode())
            .arg(QString::fromUtf8(process.readAllStandardError())));
        // Non-fatal - service may not be managed by systemd
    }

    return true;
}

bool OtaManager::startService(const QString& serviceName) {
    logInfo(QString("Starting service: %1").arg(serviceName));

    QProcess process;
    process.start("systemctl", QStringList() << "start" << QString("sps-%1.service").arg(serviceName));

    if (!process.waitForFinished(10000)) {
        process.kill();
        logWarning(QString("Timeout starting service: %1").arg(serviceName));
        return false;
    }

    if (process.exitCode() != 0) {
        logError(QString("systemctl start failed for %1: %2")
            .arg(serviceName)
            .arg(QString::fromUtf8(process.readAllStandardError())));
        return false;
    }

    return true;
}

bool OtaManager::replaceServiceBinary(const QString& serviceName, const QString& newBinaryPath) {
    Q_UNUSED(serviceName);

    QString binaryPath = s_serviceMap[serviceName];

    // If the package is a tar.gz archive, extract it
    if (newBinaryPath.endsWith(".tar.gz") || newBinaryPath.endsWith(".tgz")) {
        QString extractDir = m_downloadDir + "/extracted";
        QDir().mkpath(extractDir);

        QProcess tar;
        tar.start("tar", QStringList() << "-xzf" << newBinaryPath << "-C" << extractDir);
        if (!tar.waitForFinished(30000)) {
            tar.kill();
            logError("Failed to extract app package");
            return false;
        }

        // Find the binary in the extracted directory
        QString extractedBinary = QString("%1/%2").arg(extractDir, serviceName);
        if (!QFile::exists(extractedBinary)) {
            // Try to find any executable
            QStringList found;
            QDirIterator it(extractDir, QDir::Files | QDir::Executable, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                found.append(it.next());
            }
            if (found.isEmpty()) {
                logError("No executable found in extracted package");
                return false;
            }
            extractedBinary = found.first();
        }

        // Copy to destination
        QFile::remove(binaryPath);
        if (!QFile::copy(extractedBinary, binaryPath)) {
            logError(QString("Failed to copy extracted binary to %1").arg(binaryPath));
            return false;
        }

        // Cleanup extracted files
        QDir(extractDir).removeRecursively();
    } else {
        // Direct binary replacement
        QFile::remove(binaryPath);
        if (!QFile::copy(newBinaryPath, binaryPath)) {
            logError(QString("Failed to copy new binary to %1").arg(binaryPath));
            return false;
        }
    }

    return true;
}

QString OtaManager::getServiceBinaryPath(const QString& serviceName) const {
    return s_serviceMap.value(serviceName, QString("%1/%2").arg(m_serviceInstallDir, serviceName));
}

QString OtaManager::getServiceBackupPath(const QString& serviceName) const {
    return QString("%1/%2.backup.%3")
        .arg(m_serviceInstallDir, serviceName,
             QDateTime::currentDateTime().toString("yyyyMMddHHmmss"));
}

// ========== Version Management ==========

bool OtaManager::checkCurrentVersion() {
    QFile versionFile(m_versionFilePath);
    if (!versionFile.open(QIODevice::ReadOnly)) {
        logWarning("Version file not found, using default");
        m_state.currentVersion = "0.0.0";
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(versionFile.readAll());
    versionFile.close();

    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        m_state.currentVersion = obj["version"].toString("0.0.0");
        logInfo(QString("Current system version: %1").arg(m_state.currentVersion));
        return true;
    }

    m_state.currentVersion = "0.0.0";
    return false;
}

bool OtaManager::writeVersionFile(const QString& version) {
    QJsonObject obj;
    obj["version"] = version;
    obj["updated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonDocument doc(obj);

    QFile versionFile(m_versionFilePath);
    if (!versionFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        logError(QString("Cannot write version file: %1").arg(m_versionFilePath));
        return false;
    }

    versionFile.write(doc.toJson(QJsonDocument::Indented));
    versionFile.close();

    m_state.currentVersion = version;
    logInfo(QString("Version updated to: %1").arg(version));
    return true;
}

// ========== D-Bus Helpers ==========

bool OtaManager::callRouterMethod(const QString& method, const QVariantList& args) {
    QDBusInterface iface(SPS::DBus::SERVICE_ROUTER,
                          SPS::DBus::PATH_ROUTER,
                          SPS::DBus::IFACE_ROUTER,
                          QDBusConnection::systemBus());

    if (!iface.isValid()) {
        logError("ProtocolRouter D-Bus interface not available");
        return false;
    }

    QDBusReply<QVariant> reply = iface.callWithArgumentList(
        QDBus::Block, method, args);

    if (!reply.isValid()) {
        logError(QString("D-Bus call %1 failed: %2")
            .arg(method, reply.error().message()));
        return false;
    }

    return reply.value().toBool();
}

bool OtaManager::connectToNetworkManager() {
    QDBusConnection bus = QDBusConnection::systemBus();

    bool ok = bus.connect(
        SPS::DBus::SERVICE_NETMGR,
        SPS::DBus::PATH_NETMGR,
        SPS::DBus::IFACE_NETMGR,
        "OtaCommandReceived",
        this, SLOT(onOtaCommandReceived(const QString&)));

    if (!ok) {
        logWarning("Failed to connect to NetworkManager OtaCommandReceived signal");
    } else {
        logInfo("Connected to NetworkManager OTA signals");
    }

    return ok;
}

bool OtaManager::connectToProtocolRouter() {
    QDBusConnection bus = QDBusConnection::systemBus();

    bool ok = bus.connect(
        SPS::DBus::SERVICE_ROUTER,
        SPS::DBus::PATH_ROUTER,
        SPS::DBus::IFACE_ROUTER,
        "ConnectionStatusChanged",
        this, SLOT(onRouterConnectionStatusChanged(const QString&)));

    if (!ok) {
        logWarning("Failed to connect to ProtocolRouter ConnectionStatusChanged signal");
    }

    return ok;
}

// ========== Timers ==========

void OtaManager::onUpdateTimeout() {
    logError("Update timed out");
    setError("Update timed out after " + QString::number(m_updateTimeoutMs / 1000) + " seconds");
}

void OtaManager::onVersionCheckTimeout() {
    logInfo("Periodic version check");
    checkCurrentVersion();
}

void OtaManager::onMcuFlashProgress(int percentage) {
    logDebug(QString("MCU flash progress from router: %1%").arg(percentage));
    // Progress is tracked locally in sendOtaViaDbus
}

void OtaManager::onAppUpdateFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    Q_UNUSED(exitCode);
    Q_UNUSED(exitStatus);
    // Handled in the service update flow
}

// ========== Error Handling ==========

void OtaManager::setError(const QString& message) {
    m_state.stage = OtaStage::FAILED;
    m_state.errorMessage = message;
    m_failedUpdates++;
    m_updateTimer.stop();

    logError(message);
    emit UpdateStatusChanged(stageToString(m_state.stage));
    emit UpdateCompleted(false, message);

    // Clean up
    if (m_downloadFile) {
        m_downloadFile->close();
        m_downloadFile->remove();
    }

    m_state.stage = OtaStage::IDLE;
    m_state.progress = 0;
}
