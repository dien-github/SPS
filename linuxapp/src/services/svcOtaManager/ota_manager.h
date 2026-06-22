#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QProcess>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QElapsedTimer>
#include <QDirIterator>
#include "../common/sps_service_base.h"

/** OTA update stages. */
enum class OtaStage {
    IDLE,
    DOWNLOADING_MCU,
    FLASHING_MCU,
    DOWNLOADING_APPS,
    UPDATING_APPS,
    VERIFYING,
    COMPLETED,
    FAILED
};

/** Holds the current state and progress of an OTA update. */
struct OtaUpdateState {
    OtaStage stage;
    int progress;
    QString mcuFirmwareUrl;
    QString mcuChecksum;
    QString appPackageUrl;
    QString appChecksum;
    QString currentVersion;
    QString errorMessage;
    QString packageType;
    QString target;
    QString component;
    QString version;
    QString architecture;
    QString downloadPath;
    bool cancelled;

    /** Constructor. Initializes stage to IDLE, progress to 0, cancelled to false. */
    OtaUpdateState()
        : stage(OtaStage::IDLE), progress(0), cancelled(false) {}
};

/** Orchestrates firmware and application OTA updates over D-Bus. */
class OtaManager : public SpsServiceBase {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.sps.otamanager")

public:
    /** Constructor. */
    explicit OtaManager(QObject* parent = nullptr);
    /** Destructor. */
    ~OtaManager();

    /** Initializes the service. */
    bool initialize() override;
    /** Shuts down the service and cleans up. */
    void shutdown() override;
    /** Returns a human-readable status string. */
    QString getStatus() const override;

    /** Converts an OtaStage enum value to a string. */
    static QString stageToString(OtaStage stage);

signals:
    /** Emitted when the overall update status changes. */
    void UpdateStatusChanged(const QString& status);
    /** Emitted to report update progress percentage and current stage. */
    void UpdateProgress(int percentage, const QString& stage);
    /** Emitted when an update completes (success or failure). */
    void UpdateCompleted(bool success, const QString& message);
    /** Emitted when a new MCU firmware version is available. */
    void McuFirmwareUpdateRequired(const QString& version, const QString& downloadUrl);

public slots:
    /** Returns the current OTA stage as a string. */
    Q_SCRIPTABLE QString GetOtaStatus() const;
    /** Starts an MCU firmware update from a URL. */
    Q_SCRIPTABLE bool StartMcuFirmwareUpdate(const QString& firmwareUrl, const QString& expectedChecksum);
    /** Starts a generic update from dashboard JSON metadata. */
    Q_SCRIPTABLE bool StartUpdate(const QString& updateJson);
    /** Starts an app service binary update from a URL. */
    Q_SCRIPTABLE bool StartAppServiceUpdate(const QString& serviceName, const QString& packageUrl, const QString& expectedChecksum);
    /** Starts a combined MCU + app update sequence. */
    Q_SCRIPTABLE bool StartFullUpdate(const QString& mcuFirmwareUrl, const QString& mcuChecksum,
                                      const QString& appPackageUrl, const QString& appChecksum);
    /** Returns the current update progress percentage (0-100). */
    Q_SCRIPTABLE int GetUpdateProgress() const;
    /** Returns the current system version string. */
    Q_SCRIPTABLE QString GetCurrentVersion() const;
    /** Cancels the currently running update, if any. */
    Q_SCRIPTABLE bool CancelUpdate();

    /** Handles an OTA command received from NetworkManager. */
    void onOtaCommandReceived(const QByteArray& updateJson);
    /** Handles connection status changes from ProtocolRouter. */
    void onRouterConnectionStatusChanged(const QString& status);

protected slots:
    /** Advances to the next stage of the update process. */
    void startNextStage();
    /** Called when download bytes are received. */
    void onDownloadProgress(qint64 received, qint64 total);
    /** Called when the current download completes. */
    void onDownloadFinished();
    /** Called when MCU flash progress is reported by ProtocolRouter. */
    void onMcuFlashProgress(int percentage);
    /** Called when an app service process finishes updating. */
    void onAppUpdateFinished(int exitCode, QProcess::ExitStatus exitStatus);
    /** Called when the update timer expires. */
    void onUpdateTimeout();
    /** Called periodically to check the current version. */
    void onVersionCheckTimeout();

private:
    /** Downloads a file from a URL to a local path. */
    bool downloadFile(const QString& url, const QString& destPath);
    /** Verifies a SHA-256 checksum of a file. */
    bool verifyChecksum(const QString& filePath, const QString& expectedChecksum);
    /** Returns the default download directory path. */
    QString getDefaultDownloadPath() const;
    /** Emits a user-facing update status message and optional progress. */
    void emitUpdateStatus(const QString& status, int progress = -1);
    /** Builds a safe staged download path for an update URL or component. */
    QString stagedDownloadPath(const QString& url, const QString& fallbackName) const;
    /** Returns the normalized artifact extension, including compound .tar.gz. */
    QString artifactExtension(const QString& filePath) const;
    /** Validates the downloaded artifact for the current package type. */
    bool validateDownloadedUpdate(const QString& filePath);
    /** Installs the downloaded artifact using the current package type dispatcher. */
    bool installDownloadedUpdate(const QString& filePath);
    /** Installs a LinuxApp binary through the privileged updater helper. */
    bool installLinuxAppBinary(const QString& component, const QString& filePath);
    /** Installs a JSON config update through the privileged updater helper. */
    bool installConfigUpdate(const QString& component, const QString& filePath);
    /** Installs a release bundle through the privileged updater helper. */
    bool installReleaseBundle(const QString& bundlePath);
    /** Reads manifest.json from a tar/tar.gz artifact. */
    bool readBundleManifest(const QString& bundlePath, QJsonObject* manifest, QStringList* entries = nullptr);
    /** Extracts a release bundle into a staging directory. */
    bool extractBundle(const QString& bundlePath, const QString& extractDir);
    /** Calls the limited privileged updater helper. */
    bool runUpdaterHelper(const QStringList& args, QString* output = nullptr);
    /** Returns the mapped systemd service for a component. */
    QString serviceNameForComponent(const QString& component) const;
    /** Returns true if a file starts with the ELF magic bytes. */
    bool isElfExecutable(const QString& filePath) const;
    /** Returns true if a file contains valid JSON. */
    bool isValidJsonFile(const QString& filePath, QJsonDocument* document = nullptr);

    /** Flashes firmware to the MCU via ProtocolRouter. */
    bool flashMcuFirmware(const QString& firmwarePath);
    /** Sends firmware chunks to the MCU over D-Bus. */
    bool sendOtaViaDbus(const QString& firmwarePath);

    /** Updates a Linux app service binary from a package. */
    bool updateAppService(const QString& serviceName, const QString& packagePath);
    /** Stops a systemd service by name. */
    bool stopService(const QString& serviceName);
    /** Starts a systemd service by name. */
    bool startService(const QString& serviceName);
    /** Replaces a service binary (supports direct copy or tar.gz extraction). */
    bool replaceServiceBinary(const QString& serviceName, const QString& newBinaryPath);
    /** Returns the install path for a given service. */
    QString getServiceBinaryPath(const QString& serviceName) const;
    /** Returns a timestamped backup path for a service binary. */
    QString getServiceBackupPath(const QString& serviceName) const;

    /** Reads and stores the current version from the version file. */
    bool checkCurrentVersion();
    /** Writes the given version string to the version file. */
    bool writeVersionFile(const QString& version);

    /** Calls a method on the ProtocolRouter D-Bus interface. */
    bool callRouterMethod(const QString& method, const QVariantList& args);
    /** Connects to NetworkManager D-Bus signals. */
    bool connectToNetworkManager();
    /** Connects to ProtocolRouter D-Bus signals. */
    bool connectToProtocolRouter();

    // Configuration
    QString m_configPath;
    QString m_downloadDir;
    QString m_versionFilePath;
    QString m_systemdUnitDir;
    QString m_serviceInstallDir;
    QString m_updaterHelperPath;
    int m_updateTimeoutMs;
    int m_mcuChunkSize;

    // Network
    QNetworkAccessManager* m_networkManager;
    QNetworkReply* m_currentReply;

    // File handling
    QFile* m_downloadFile;

    // Update state
    OtaUpdateState m_state;
    QTimer m_updateTimer;
    QTimer m_versionCheckTimer;

    // Current app service being updated
    QString m_currentServiceName;

    /** Sets error state and emits failure signals. */
    void setError(const QString& message);

    // Backup paths for rollback
    QStringList m_backupFiles;

    // Statistics
    int m_totalUpdates;
    int m_successfulUpdates;
    int m_failedUpdates;
};

#endif // OTA_MANAGER_H
