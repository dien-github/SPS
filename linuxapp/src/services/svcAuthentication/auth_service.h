#ifndef AUTH_SERVICE_H
#define AUTH_SERVICE_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QDateTime>
#include <QTimer>
#include "../common/sps_service_base.h"
#include "../common/sps_device_models.h"

// Authentication Service
// Manages lecturer authentication via RFID reader and GPIO
// Exposes D-Bus interface: com.sps.auth
class AuthService : public SpsServiceBase {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.sps.auth")

public:
    explicit AuthService(QObject* parent = nullptr);
    ~AuthService();

    // Service lifecycle
    bool initialize() override;
    void shutdown() override;
    QString getStatus() const override;

    // Authentication state
    int getAuthStatus() const;
    QString getAuthStatusString() const;
    QString getAuthenticatedLecturer() const;

    // Test/simulation methods
    void simulateRfidRead(const QString& rfidData);

signals:
    // D-Bus signals
    void AuthStatusChanged(int newStatus);
    void LecturerAuthenticated(const QString& lecturerName, qlonglong timestamp);
    void AuthenticationFailed(const QString& reason);

    // Internal signals
    void rfidDataReceived(const QString& data);

public slots:
    // D-Bus methods
    Q_SCRIPTABLE int GetAuthStatus() const;
    Q_SCRIPTABLE bool UnlockScreen(const QString& rfidData);
    Q_SCRIPTABLE bool LockScreen();
    Q_SCRIPTABLE QString GetAuthenticatedLecturer() const;

    // TODO: Temp need to refactor
    void onRfidRead(const QString& rfidData);
    void onRfidReaderConnected();
    void onRfidReaderDisconnected();
    void onRfidReaderError(const QString& error);

protected slots:
    // Internal slots
    // TODO: Need enhance encapsulation of this module by doing connect function inside initialize() function.
    // void onRfidRead(const QString& rfidData);
    void onLockTimeout();
    // void onRfidReaderConnected();
    // void onRfidReaderDisconnected();
    // void onRfidReaderError(const QString& error);

private:
    // Authentication status enum
    enum AuthStatus {
        LOCKED = 0,
        UNLOCKING = 1,
        UNLOCKED = 2,
        LOCKING = 3,
        ERROR = 4
    };

    // Lecturer database methods
    bool loadLecturerDatabase();
    bool verifyLecturerRfid(const QString& rfidData, Lecturer& lecturer);
    bool isRfidValid(const QString& rfid) const;

    // State management
    void setAuthStatus(AuthStatus newStatus);
    void setError(const QString& error);

    // Auto-lock management
    void startAutoLockTimer();
    void cancelAutoLockTimer();

    // Configuration
    int m_lockTimeoutMs;
    QString m_databasePath;

    // Current state
    AuthStatus m_status;
    Lecturer m_currentLecturer;
    QString m_lastError;
    QDateTime m_lockedAt;
    QDateTime m_unlockedAt;

    // RFID database (map: RFID -> Lecturer)
    QMap<QString, Lecturer> m_lecturers;

    // Timers
    QTimer m_autoLockTimer;
    QTimer m_debounceTimer;

    // Statistics
    int m_totalAuthAttempts;
    int m_successfulAuths;
    int m_failedAuths;
};

#endif // AUTH_SERVICE_H
