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

// OTA update stages
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

// OTA update state
struct OtaUpdateState {
    OtaStage stage;
    int progress;
    QString mcuFirmwareUrl;
    QString mcuChecksum;
    QString appPackageUrl;
    QString appChecksum;
    QString currentVersion;
    QString errorMessage;
    bool cancelled;

    OtaUpdateState()
        : stage(OtaStage::IDLE), progress(0), cancelled(false) {}
};

// OTA Manager Service
// Orchestrates firmware and application updates
// Downloads firmware from URL, flashes MCU via UART through ProtocolRouter,
// and updates Linux app services by replacing binaries
class OtaManager : public SpsServiceBase {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.sps.otamanager")

public:
    explicit OtaManager(QObject* parent = nullptr);
    ~OtaManager();

    // Service lifecycle
    bool initialize() override;
    void shutdown() override;
    QString getStatus() const override;

    // Update stages
    static QString stageToString(OtaStage stage);

signals:
    // D-Bus signals
    void UpdateStatusChanged(const QString& status);
    void UpdateProgress(int percentage, const QString& stage);
    void UpdateCompleted(bool success, const QString& message);
    void McuFirmwareUpdateRequired(const QString& version, const QString& downloadUrl);

public slots:
    // D-Bus methods
    Q_SCRIPTABLE QString GetOtaStatus() const;
    Q_SCRIPTABLE bool StartMcuFirmwareUpdate(const QString& firmwareUrl, const QString& expectedChecksum);
    Q_SCRIPTABLE bool StartAppServiceUpdate(const QString& serviceName, const QString& packageUrl, const QString& expectedChecksum);
    Q_SCRIPTABLE bool StartFullUpdate(const QString& mcuFirmwareUrl, const QString& mcuChecksum,
                                      const QString& appPackageUrl, const QString& appChecksum);
    Q_SCRIPTABLE int GetUpdateProgress() const;
    Q_SCRIPTABLE QString GetCurrentVersion() const;
    Q_SCRIPTABLE bool CancelUpdate();

    // D-Bus signal handlers from other services
    void onOtaCommandReceived(const QString& firmwareUrl);
    void onRouterConnectionStatusChanged(const QString& status);

protected slots:
    void startNextStage();
    void onDownloadProgress(qint64 received, qint64 total);
    void onDownloadFinished();
    void onMcuFlashProgress(int percentage);
    void onAppUpdateFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onUpdateTimeout();
    void onVersionCheckTimeout();

private:
    // Download handling
    bool downloadFile(const QString& url, const QString& destPath);
    bool verifyChecksum(const QString& filePath, const QString& expectedChecksum);
    QString getDefaultDownloadPath() const;

    // MCU firmware update
    bool flashMcuFirmware(const QString& firmwarePath);
    bool sendOtaViaDbus(const QString& firmwarePath);

    // Linux app service update
    bool updateAppService(const QString& serviceName, const QString& packagePath);
    bool stopService(const QString& serviceName);
    bool startService(const QString& serviceName);
    bool replaceServiceBinary(const QString& serviceName, const QString& newBinaryPath);
    QString getServiceBinaryPath(const QString& serviceName) const;
    QString getServiceBackupPath(const QString& serviceName) const;

    // Version management
    bool checkCurrentVersion();
    bool writeVersionFile(const QString& version);

    // D-Bus helpers
    bool callRouterMethod(const QString& method, const QVariantList& args);
    bool connectToNetworkManager();
    bool connectToProtocolRouter();

    // Configuration
    QString m_configPath;
    QString m_downloadDir;
    QString m_versionFilePath;
    QString m_systemdUnitDir;
    QString m_serviceInstallDir;
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

    // Error handling
    void setError(const QString& message);

    // Backup paths for rollback
    QStringList m_backupFiles;

    // Statistics
    int m_totalUpdates;
    int m_successfulUpdates;
    int m_failedUpdates;
};

#endif // OTA_MANAGER_H
