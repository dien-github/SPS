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
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

// Service binary names mapped to their install paths
static const QMap<QString, QString> s_serviceMap = {
    {"svcAuthentication",  "/opt/sps/bin/svcAuthentication"},
    {"svcProtocolRouter",  "/opt/sps/bin/svcProtocolRouter"},
    {"svcAutoEngine",      "/opt/sps/bin/svcAutoEngine"},
    {"svcNetworkManager",  "/opt/sps/bin/svcNetworkManager"},
    {"svcOtaManager",      "/opt/sps/bin/svcOtaManager"},
    {"appHmi",             "/opt/sps/bin/appHmi"}
};

static const QMap<QString, QString> s_serviceUnitMap = {
    {"svcAuthentication",  "sps-authentication.service"},
    {"svcProtocolRouter",  "sps-protocol-router.service"},
    {"svcAutoEngine",      "sps-auto-engine.service"},
    {"svcNetworkManager",  "sps-network-manager.service"},
    {"svcOtaManager",      "sps-ota-manager.service"},
    {"appHmi",             "sps-app-hmi.service"}
};

/** Constructor. Initializes member variables, connects timers, and logs creation. */
OtaManager::OtaManager(QObject* parent)
    : SpsServiceBase("com.sps.otamanager", "/com/sps/otamanager", parent),
      m_configPath("/opt/sps/config/config.json"),
      m_downloadDir("/opt/sps/updates"),
      m_versionFilePath("/opt/sps/config/version.json"),
      m_systemdUnitDir("/etc/systemd/system"),
      m_serviceInstallDir("/opt/sps/bin"),
      m_updaterHelperPath("/opt/sps/libexec/sps-updater"),
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

/** Destructor. Calls shutdown to clean up resources. */
OtaManager::~OtaManager() {
    shutdown();
}

/** Initializes the service: loads config, checks version, connects D-Bus, starts timers. */
bool OtaManager::initialize() {
    logInfo("Initializing OTA Manager service...");

    m_networkManager = new QNetworkAccessManager(this);
    const QByteArray helperEnv = qgetenv("SPS_UPDATER_HELPER");
    if (!helperEnv.isEmpty()) {
        m_updaterHelperPath = QString::fromUtf8(helperEnv);
    }

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
            if (config.contains("updater_helper"))
                m_updaterHelperPath = config["updater_helper"].toString();
        }
    }

    QDir().mkpath(m_downloadDir);
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

/** Shuts down the service: stops timers, aborts downloads, cleans temp files. */
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

/** Returns a human-readable status string with current stage, progress, and update counts. */
QString OtaManager::getStatus() const {
    return QString("OTA Status: %1 | Progress: %2% | Total: %3, OK: %4, Fail: %5")
        .arg(stageToString(m_state.stage))
        .arg(m_state.progress)
        .arg(m_totalUpdates)
        .arg(m_successfulUpdates)
        .arg(m_failedUpdates);
}

/** Converts an OtaStage enum value to a human-readable string. */
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

/** Returns the current OTA stage as a string (D-Bus callable). */
QString OtaManager::GetOtaStatus() const {
    return stageToString(m_state.stage);
}

/** Starts an MCU firmware update from a URL. Returns false if an update is already in progress. */
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
    m_state.packageType = "mcu_firmware";
    m_state.target = "mcu";
    m_state.component = "mcu";
    m_state.stage = OtaStage::DOWNLOADING_MCU;
    m_state.progress = 0;

    emitUpdateStatus("validating", 0);

    startNextStage();
    return true;
}

/** Starts a generic update from dashboard JSON metadata. */
bool OtaManager::StartUpdate(const QString& updateJson) {
    if (m_state.stage != OtaStage::IDLE) {
        logWarning("Update already in progress");
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(updateJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        logError(QString("Invalid update metadata JSON: %1").arg(parseError.errorString()));
        return false;
    }

    const QJsonObject obj = doc.object();
    const QString packageType = obj["package_type"].toString("mcu_firmware").trimmed();
    const QString target = obj["target"].toString(packageType == "mcu_firmware" ? "mcu" : "sbc").trimmed();
    const QString url = obj["url"].toString().trimmed();
    const QString checksum = obj["checksum"].toString().trimmed();
    const QString component = obj["component"].toString().trimmed();

    if (url.isEmpty()) {
        logError("Update URL is required");
        return false;
    }

    if (packageType == "mcu_firmware") {
        if (target != "mcu") {
            logError("mcu_firmware package must target mcu");
            return false;
        }
        return StartMcuFirmwareUpdate(url, checksum);
    }

    if (target != "sbc") {
        logError(QString("%1 package must target sbc").arg(packageType));
        return false;
    }

    if (packageType == "linuxapp_binary" && (component.isEmpty() || !s_serviceMap.contains(component))) {
        logError(QString("Unknown LinuxApp component: %1").arg(component));
        return false;
    }

    if (packageType != "linuxapp_binary" &&
        packageType != "config_update" &&
        packageType != "release_bundle") {
        logError(QString("Unsupported package_type: %1").arg(packageType));
        return false;
    }

    logInfo(QString("Starting update type=%1 target=%2 component=%3 url=%4")
        .arg(packageType, target, component, url));
    m_totalUpdates++;

    m_state = OtaUpdateState();
    m_state.packageType = packageType;
    m_state.target = target;
    m_state.component = component;
    m_state.version = obj["version"].toString().trimmed();
    m_state.architecture = obj["architecture"].toString().trimmed();
    m_state.appPackageUrl = url;
    m_state.appChecksum = checksum;
    m_state.stage = OtaStage::DOWNLOADING_APPS;
    m_state.progress = 0;
    m_currentServiceName = component;

    emitUpdateStatus("validating", 0);
    startNextStage();
    return true;
}

/** Starts an app service binary update from a URL. Validates that the service name is known. */
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
    m_state.packageType = "linuxapp_binary";
    m_state.target = "sbc";
    m_state.component = serviceName;
    m_state.appPackageUrl = packageUrl;
    m_state.appChecksum = expectedChecksum;
    m_state.stage = OtaStage::DOWNLOADING_APPS;
    m_state.progress = 0;
    m_currentServiceName = serviceName;

    emitUpdateStatus("validating", 0);

    startNextStage();
    return true;
}

/** Starts a combined MCU firmware + app services update sequence. */
bool OtaManager::StartFullUpdate(const QString& mcuFirmwareUrl, const QString& mcuChecksum,
                                  const QString& appPackageUrl, const QString& appChecksum) {
    if (m_state.stage != OtaStage::IDLE) {
        logWarning("Update already in progress");
        return false;
    }

    logInfo("Starting full system update (MCU firmware + Linux apps)");
    m_totalUpdates++;

    m_state = OtaUpdateState();
    m_state.packageType = "mcu_firmware";
    m_state.target = "mcu";
    m_state.mcuFirmwareUrl = mcuFirmwareUrl;
    m_state.mcuChecksum = mcuChecksum;
    m_state.appPackageUrl = appPackageUrl;
    m_state.appChecksum = appChecksum;
    m_state.stage = OtaStage::DOWNLOADING_MCU;
    m_state.progress = 0;

    emitUpdateStatus("validating", 0);

    startNextStage();
    return true;
}

/** Returns the current update progress as a percentage (0-100). */
int OtaManager::GetUpdateProgress() const {
    return m_state.progress;
}

/** Returns the current system version string. */
QString OtaManager::GetCurrentVersion() const {
    return m_state.currentVersion;
}

/** Cancels the ongoing update: aborts download, cleans up, and emits completion signal. */
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

/** Handles an OTA/update command signal from NetworkManager. */
void OtaManager::onOtaCommandReceived(const QByteArray& updateJson) {
    logInfo(QString("Update command received from NetworkManager: %1 bytes").arg(updateJson.size()));

    if (m_state.stage == OtaStage::IDLE) {
        StartUpdate(QString::fromUtf8(updateJson));
    }
}

/** Logs router connection status changes from ProtocolRouter. */
void OtaManager::onRouterConnectionStatusChanged(const QString& status) {
    logDebug(QString("Router connection status: %1").arg(status));
}

/** Advances the update to the next stage (download, flash, verify, etc.). */
void OtaManager::startNextStage() {
    if (m_state.cancelled) return;

    m_updateTimer.start(m_updateTimeoutMs);

    switch (m_state.stage) {
        case OtaStage::DOWNLOADING_MCU: {
            QString destPath = stagedDownloadPath(m_state.mcuFirmwareUrl, "mcu_firmware.bin");
            m_state.downloadPath = destPath;
            emitUpdateStatus("uploaded", 0);
            if (downloadFile(m_state.mcuFirmwareUrl, destPath)) {
                m_state.progress = 0;
            } else {
                setError("Failed to start MCU firmware download");
            }
            break;
        }

        case OtaStage::FLASHING_MCU: {
            QString firmwarePath = m_state.downloadPath.isEmpty()
                ? QString("%1/mcu_firmware.bin").arg(m_downloadDir)
                : m_state.downloadPath;
            emitUpdateStatus("installing", 40);
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
            QString fallbackName = m_state.packageType == "linuxapp_binary"
                ? m_state.component
                : QString("%1.tar.gz").arg(m_state.packageType.isEmpty() ? "update" : m_state.packageType);
            QString destPath = stagedDownloadPath(m_state.appPackageUrl, fallbackName);
            m_state.downloadPath = destPath;
            emitUpdateStatus("uploaded", 50);
            if (downloadFile(m_state.appPackageUrl, destPath)) {
                m_state.progress = 50;
            } else {
                setError("Failed to start app package download");
            }
            break;
        }

        case OtaStage::UPDATING_APPS: {
            QString packagePath = m_state.downloadPath.isEmpty()
                ? QString("%1/app_update.tar.gz").arg(m_downloadDir)
                : m_state.downloadPath;
            emitUpdateStatus("installing", 80);
            if (installDownloadedUpdate(packagePath)) {
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
            emitUpdateStatus("success", 100);
            emit UpdateCompleted(true, "Update completed successfully");

            m_state.stage = OtaStage::IDLE;
            break;
        }

        default:
            break;
    }
}

/** Downloads a file from a URL to a temporary .part file, sets up progress/finished handlers. */
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

/** Updates progress percentage and emits UpdateProgress signal during download. */
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

/** Handles download completion: finalizes file, verifies checksum, advances to next stage. */
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

    m_state.downloadPath = m_downloadFile->fileName();
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

    emitUpdateStatus("validating", m_state.stage == OtaStage::DOWNLOADING_MCU ? 40 : 75);
    if (!validateDownloadedUpdate(m_downloadFile->fileName())) {
        setError("Downloaded update validation failed");
        return;
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

/** Verifies a file's SHA-256 checksum against an expected value. Returns true if empty checksum. */
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

/** Returns the configured download directory path. */
QString OtaManager::getDefaultDownloadPath() const {
    return m_downloadDir;
}

/** Emits a user-facing update status and optional progress. */
void OtaManager::emitUpdateStatus(const QString& status, int progress) {
    logInfo(QString("Update status: %1").arg(status));
    emit UpdateStatusChanged(status);
    if (progress >= 0) {
        m_state.progress = progress;
        emit UpdateProgress(progress, status);
    }
}

/** Builds a safe staged download path for an update URL or fallback name. */
QString OtaManager::stagedDownloadPath(const QString& url, const QString& fallbackName) const {
    QString fileName = QUrl(url).fileName();
    if (fileName.isEmpty()) {
        fileName = fallbackName;
    }

    fileName.replace(QRegularExpression("[^A-Za-z0-9._-]"), "-");
    if (fileName.isEmpty()) {
        fileName = "update.bin";
    }

    return QDir(m_downloadDir).filePath(QString("%1-%2")
        .arg(QDateTime::currentDateTime().toString("yyyyMMddHHmmss"), fileName));
}

/** Returns the artifact extension, preserving .tar.gz as a compound extension. */
QString OtaManager::artifactExtension(const QString& filePath) const {
    const QString lower = QFileInfo(filePath).fileName().toLower();
    if (lower.endsWith(".tar.gz")) return ".tar.gz";
    if (lower.endsWith(".tgz")) return ".tgz";
    return QFileInfo(lower).suffix().isEmpty()
        ? QString()
        : QString(".%1").arg(QFileInfo(lower).suffix());
}

/** Returns true if a file starts with the ELF magic bytes. */
bool OtaManager::isElfExecutable(const QString& filePath) const {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray magic = file.read(4);
    return magic.size() == 4 &&
           static_cast<unsigned char>(magic[0]) == 0x7f &&
           magic[1] == 'E' &&
           magic[2] == 'L' &&
           magic[3] == 'F';
}

/** Returns true if a file contains valid JSON. */
bool OtaManager::isValidJsonFile(const QString& filePath, QJsonDocument* document) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        logError(QString("Cannot open JSON file: %1").arg(filePath));
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || doc.isNull()) {
        logError(QString("Invalid JSON file %1: %2").arg(filePath, parseError.errorString()));
        return false;
    }

    if (document) {
        *document = doc;
    }
    return true;
}

/** Reads manifest.json and optionally lists entries from a tar/tar.gz bundle. */
bool OtaManager::readBundleManifest(const QString& bundlePath, QJsonObject* manifest, QStringList* entries) {
    const QString extension = artifactExtension(bundlePath);
    if (extension != ".tar" && extension != ".tar.gz" && extension != ".tgz") {
        logError(QString("Unsupported package archive: %1").arg(bundlePath));
        return false;
    }

    const bool compressed = extension == ".tar.gz" || extension == ".tgz";
    QProcess listProcess;
    listProcess.start("tar", QStringList() << (compressed ? "-tzf" : "-tf") << bundlePath);
    if (!listProcess.waitForFinished(30000) || listProcess.exitCode() != 0) {
        logError(QString("Failed to list package archive: %1")
            .arg(QString::fromUtf8(listProcess.readAllStandardError())));
        return false;
    }

    const QStringList archiveEntries = QString::fromUtf8(listProcess.readAllStandardOutput())
        .split('\n', Qt::SkipEmptyParts);
    for (const QString& entry : archiveEntries) {
        if (entry.startsWith("/") || entry.split('/').contains("..")) {
            logError(QString("Package archive contains unsafe path: %1").arg(entry));
            return false;
        }
    }

    if (entries) {
        *entries = archiveEntries;
    }

    QString manifestName;
    for (const QString& entry : archiveEntries) {
        if (entry == "manifest.json" || entry.endsWith("/manifest.json")) {
            manifestName = entry;
            break;
        }
    }

    if (manifestName.isEmpty()) {
        logError("Package archive missing manifest.json");
        return false;
    }

    QProcess manifestProcess;
    manifestProcess.start("tar", QStringList() << (compressed ? "-xOzf" : "-xOf") << bundlePath << manifestName);
    if (!manifestProcess.waitForFinished(30000) || manifestProcess.exitCode() != 0) {
        logError(QString("Failed to read manifest.json: %1")
            .arg(QString::fromUtf8(manifestProcess.readAllStandardError())));
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(manifestProcess.readAllStandardOutput(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        logError(QString("Invalid manifest.json: %1").arg(parseError.errorString()));
        return false;
    }

    if (manifest) {
        *manifest = doc.object();
    }
    return true;
}

/** Extracts a tar/tar.gz bundle into a staging directory. */
bool OtaManager::extractBundle(const QString& bundlePath, const QString& extractDir) {
    QDir().mkpath(extractDir);
    const QString extension = artifactExtension(bundlePath);
    const bool compressed = extension == ".tar.gz" || extension == ".tgz";

    QProcess tar;
    tar.start("tar", QStringList() << (compressed ? "-xzf" : "-xf") << bundlePath << "-C" << extractDir);
    if (!tar.waitForFinished(60000) || tar.exitCode() != 0) {
        logError(QString("Failed to extract bundle: %1").arg(QString::fromUtf8(tar.readAllStandardError())));
        return false;
    }
    return true;
}

/** Validates the downloaded artifact for the active package type. */
bool OtaManager::validateDownloadedUpdate(const QString& filePath) {
    const QString extension = artifactExtension(filePath);

    if (m_state.packageType == "mcu_firmware") {
        if (extension != ".bin") {
            logError("MCU firmware must be a .bin file");
            return false;
        }
        return true;
    }

    if (m_state.packageType == "linuxapp_binary") {
        if (!extension.isEmpty() && extension != ".elf") {
            logError("LinuxApp binary must be .elf or a no-extension ELF executable");
            return false;
        }
        if (!isElfExecutable(filePath)) {
            logError("LinuxApp binary is not an ELF executable");
            return false;
        }
        return true;
    }

    if (m_state.packageType == "config_update") {
        if (extension == ".json") {
            return isValidJsonFile(filePath);
        }
        QJsonObject manifest;
        return readBundleManifest(filePath, &manifest);
    }

    if (m_state.packageType == "release_bundle") {
        if (extension != ".tar.gz" && extension != ".tgz") {
            logError("Release bundle must be .tar.gz or .tgz");
            return false;
        }

        QJsonObject manifest;
        QStringList entries;
        if (!readBundleManifest(filePath, &manifest, &entries)) {
            return false;
        }

        const QStringList requiredRoots = {"bin/", "config/", "systemd/"};
        for (const QString& root : requiredRoots) {
            bool found = false;
            for (const QString& entry : entries) {
                if (entry.startsWith(root) || entry.contains(QString("/%1").arg(root))) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                logError(QString("Release bundle missing %1").arg(root));
                return false;
            }
        }
        return true;
    }

    logError(QString("Unsupported package type: %1").arg(m_state.packageType));
    return false;
}

/** Calls the limited privileged updater helper. */
bool OtaManager::runUpdaterHelper(const QStringList& args, QString* output) {
    if (!QFileInfo::exists(m_updaterHelperPath)) {
        logError(QString("Updater helper not found: %1").arg(m_updaterHelperPath));
        return false;
    }

    QString program = "sudo";
    QStringList processArgs;
    processArgs << "-n" << m_updaterHelperPath;
    processArgs.append(args);

    if (qEnvironmentVariable("SPS_UPDATER_DIRECT") == "1") {
        program = m_updaterHelperPath;
        processArgs = args;
    }

    QProcess process;
    process.start(program, processArgs);
    if (!process.waitForFinished(120000)) {
        process.kill();
        logError("Updater helper timed out");
        return false;
    }

    const QString combinedOutput = QString::fromUtf8(process.readAllStandardOutput())
        + QString::fromUtf8(process.readAllStandardError());
    if (output) {
        *output = combinedOutput.trimmed();
    }
    if (!combinedOutput.trimmed().isEmpty()) {
        logInfo(QString("Updater helper output: %1").arg(combinedOutput.trimmed()));
    }
    if (combinedOutput.contains("rollback_done")) {
        emitUpdateStatus("rollback_done", m_state.progress);
    }

    return process.exitCode() == 0;
}

/** Returns the mapped systemd unit for a component. */
QString OtaManager::serviceNameForComponent(const QString& component) const {
    return s_serviceUnitMap.value(component);
}

/** Installs a LinuxApp binary through the privileged updater helper. */
bool OtaManager::installLinuxAppBinary(const QString& component, const QString& filePath) {
    if (!s_serviceMap.contains(component)) {
        logError(QString("Unknown LinuxApp component: %1").arg(component));
        return false;
    }

    emitUpdateStatus("installing", 82);
    QString output;
    const bool ok = runUpdaterHelper(
        QStringList() << "install-binary" << component << filePath << serviceNameForComponent(component),
        &output);
    if (ok) {
        emitUpdateStatus("restarting", 88);
    }
    return ok;
}

/** Installs a config update through the privileged updater helper. */
bool OtaManager::installConfigUpdate(const QString& component, const QString& filePath) {
    const QString extension = artifactExtension(filePath);
    if (extension == ".json") {
        const QString configComponent = component.isEmpty() ? QFileInfo(filePath).completeBaseName() : component;
        emitUpdateStatus("installing", 82);
        return runUpdaterHelper(
            QStringList() << "install-config" << configComponent << filePath << serviceNameForComponent(configComponent));
    }

    QJsonObject manifest;
    if (!readBundleManifest(filePath, &manifest)) {
        return false;
    }

    const QString extractDir = QDir(m_downloadDir).filePath(QString("config-%1")
        .arg(QDateTime::currentDateTime().toString("yyyyMMddHHmmss")));
    if (!extractBundle(filePath, extractDir)) {
        return false;
    }

    bool ok = true;
    QJsonArray configs = manifest["configs"].toArray();
    if (configs.isEmpty()) {
        QDirIterator it(QDir(extractDir).filePath("config"), QStringList() << "*.json",
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString configPath = it.next();
            const QString configComponent = QFileInfo(configPath).completeBaseName();
            ok = runUpdaterHelper(QStringList() << "install-config" << configComponent << configPath
                                  << serviceNameForComponent(configComponent)) && ok;
        }
    } else {
        for (const QJsonValue& value : configs) {
            const QJsonObject item = value.toObject();
            const QString configComponent = item["component"].toString(component);
            const QString relativePath = item["path"].toString(QString("config/%1.json").arg(configComponent));
            const QString service = item["service"].toString(serviceNameForComponent(configComponent));
            ok = runUpdaterHelper(QStringList() << "install-config" << configComponent
                                  << QDir(extractDir).filePath(relativePath) << service) && ok;
        }
    }

    QDir(extractDir).removeRecursively();
    return ok;
}

/** Installs a release bundle through the privileged updater helper. */
bool OtaManager::installReleaseBundle(const QString& bundlePath) {
    QJsonObject manifest;
    if (!readBundleManifest(bundlePath, &manifest)) {
        return false;
    }

    const QString extractDir = QDir(m_downloadDir).filePath(QString("release-%1")
        .arg(QDateTime::currentDateTime().toString("yyyyMMddHHmmss")));
    if (!extractBundle(bundlePath, extractDir)) {
        return false;
    }

    bool ok = true;
    const QJsonArray components = manifest["components"].toArray();
    if (components.isEmpty()) {
        QDirIterator it(QDir(extractDir).filePath("bin"), QDir::Files);
        while (it.hasNext()) {
            const QString binaryPath = it.next();
            const QString component = QFileInfo(binaryPath).fileName();
            ok = installLinuxAppBinary(component, binaryPath) && ok;
        }
    } else {
        for (const QJsonValue& value : components) {
            const QJsonObject item = value.toObject();
            const QString type = item["type"].toString("linuxapp_binary");
            const QString component = item["component"].toString();
            const QString relativePath = item["path"].toString(QString("bin/%1").arg(component));
            if (type == "linuxapp_binary") {
                ok = installLinuxAppBinary(component, QDir(extractDir).filePath(relativePath)) && ok;
            } else if (type == "config_update") {
                ok = installConfigUpdate(component, QDir(extractDir).filePath(relativePath)) && ok;
            }
        }
    }

    const QJsonArray configs = manifest["configs"].toArray();
    for (const QJsonValue& value : configs) {
        const QJsonObject item = value.toObject();
        const QString component = item["component"].toString();
        const QString relativePath = item["path"].toString(QString("config/%1.json").arg(component));
        ok = installConfigUpdate(component, QDir(extractDir).filePath(relativePath)) && ok;
    }

    emitUpdateStatus("restarting", 88);
    const QJsonArray services = manifest["services"].toArray();
    for (const QJsonValue& value : services) {
        const QString service = value.isObject()
            ? value.toObject()["name"].toString()
            : value.toString();
        if (!service.isEmpty()) {
            ok = runUpdaterHelper(QStringList() << "restart-service" << service) && ok;
        }
    }

    QDir(extractDir).removeRecursively();
    return ok;
}

/** Installs the downloaded artifact using the active package type dispatcher. */
bool OtaManager::installDownloadedUpdate(const QString& filePath) {
    if (m_state.packageType == "linuxapp_binary") {
        return installLinuxAppBinary(m_state.component, filePath);
    }
    if (m_state.packageType == "config_update") {
        return installConfigUpdate(m_state.component, filePath);
    }
    if (m_state.packageType == "release_bundle") {
        return installReleaseBundle(filePath);
    }

    logError(QString("No SBC installer for package type: %1").arg(m_state.packageType));
    return false;
}

/** Flashes firmware binary to the MCU via ProtocolRouter D-Bus interface. */
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

/** Sends firmware data in chunks to the MCU over D-Bus via ProtocolRouter. */
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
            static_cast<ushort>(i),
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

/** Updates a Linux app service: stops, backs up, replaces binary, restarts. */
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

/** Stops a systemd service by name using systemctl. */
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

/** Starts a systemd service by name using systemctl. */
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

/** Replaces a service binary, extracting from tar.gz archive if needed. */
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

/** Returns the install path for the given service name. */
QString OtaManager::getServiceBinaryPath(const QString& serviceName) const {
    return s_serviceMap.value(serviceName, QString("%1/%2").arg(m_serviceInstallDir, serviceName));
}

/** Returns a timestamped backup file path for the given service binary. */
QString OtaManager::getServiceBackupPath(const QString& serviceName) const {
    return QString("%1/%2.backup.%3")
        .arg(m_serviceInstallDir, serviceName,
             QDateTime::currentDateTime().toString("yyyyMMddHHmmss"));
}

/** Reads the version file and stores the current system version. */
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

/** Writes the given version string to the version file with a timestamp. */
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

/** Calls a method on the ProtocolRouter D-Bus interface with the given arguments. */
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

/** Connects to NetworkManager's OtaCommandReceived D-Bus signal. */
bool OtaManager::connectToNetworkManager() {
    QDBusConnection bus = QDBusConnection::systemBus();

    bool ok = bus.connect(
        SPS::DBus::SERVICE_NETMGR,
        SPS::DBus::PATH_NETMGR,
        SPS::DBus::IFACE_NETMGR,
        "OtaCommandReceived",
        this, SLOT(onOtaCommandReceived(const QByteArray&)));

    if (!ok) {
        logWarning("Failed to connect to NetworkManager OtaCommandReceived signal");
    } else {
        logInfo("Connected to NetworkManager OTA signals");
    }

    return ok;
}

/** Connects to ProtocolRouter's ConnectionStatusChanged D-Bus signal. */
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

/** Called when the update timer expires; sets error state for timeout. */
void OtaManager::onUpdateTimeout() {
    logError("Update timed out");
    setError("Update timed out after " + QString::number(m_updateTimeoutMs / 1000) + " seconds");
}

/** Called periodically to re-read and log the current system version. */
void OtaManager::onVersionCheckTimeout() {
    logInfo("Periodic version check");
    checkCurrentVersion();
}

/** Called when the ProtocolRouter reports MCU flash progress. */
void OtaManager::onMcuFlashProgress(int percentage) {
    logDebug(QString("MCU flash progress from router: %1%").arg(percentage));
    // Progress is tracked locally in sendOtaViaDbus
}

/** Called when an app service update process finishes (currently unused). */
void OtaManager::onAppUpdateFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    Q_UNUSED(exitCode);
    Q_UNUSED(exitStatus);
    // Handled in the service update flow
}

/** Sets the error state, emits failure signals, and cleans up partial downloads. */
void OtaManager::setError(const QString& message) {
    m_state.stage = OtaStage::FAILED;
    m_state.errorMessage = message;
    m_failedUpdates++;
    m_updateTimer.stop();

    logError(message);
    emitUpdateStatus("failed", m_state.progress);
    emit UpdateCompleted(false, message);

    // Clean up
    if (m_downloadFile) {
        m_downloadFile->close();
        m_downloadFile->remove();
    }

    m_state.stage = OtaStage::IDLE;
    m_state.progress = 0;
}
