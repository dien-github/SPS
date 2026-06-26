#ifndef AUTH_SERVICE_H
#define AUTH_SERVICE_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QDateTime>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include "../common/sps_service_base.h"
#include "../common/sps_device_models.h"

/** Manages lecturer authentication via RFID reader and GPIO over D-Bus. */
class AuthService : public SpsServiceBase {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.sps.auth")

public:
    /** Constructs the auth service and sets up auto-lock and debounce timers. */
    explicit AuthService(QObject* parent = nullptr);
    /** Destructor - shuts down the service cleanly. */
    ~AuthService();

    /** Initializes the service: loads lecturer database and registers D-Bus. */
    bool initialize() override;
    /** Stops timers, locks the screen, and calls base class shutdown. */
    void shutdown() override;
    /** Returns a summary string with auth status, lecturer, and statistics. */
    QString getStatus() const override;

    /** Returns the current auth status as an integer. */
    int getAuthStatus() const;
    /** Returns the current auth status as a readable string (e.g. "LOCKED"). */
    QString getAuthStatusString() const;
    /** Returns the name of the currently authenticated lecturer. */
    QString getAuthenticatedLecturer() const;

    /** Simulates an RFID read event for testing without hardware. */
    void simulateRfidRead(const QString& rfidData);

signals:
    /** Emitted when the auth status changes (newStatus is an AuthStatus value). */
    void AuthStatusChanged(int newStatus);
    /** Emitted when a lecturer is successfully authenticated. */
    void LecturerAuthenticated(const QString& lecturerName, qlonglong timestamp);
    /** Emitted when authentication fails, with a description of why. */
    void AuthenticationFailed(const QString& reason);

    /** Internal signal: raw RFID data received from the reader. */
    void rfidDataReceived(const QString& data);

    /** Emitted when a room monitoring condition triggers an alert (ROOM_USAGE_OVERRUN, OUT_OF_SCHOOL_HOURS). */
    void RoomMonitorAlert(const QString& alertType, const QString& payloadJson);

    /** Emitted after a successful lecturer list sync with the number of loaded lecturers. */
    void LecturerListUpdated(int lecturerCount);

public slots:
    /** D-Bus callable: returns the current authentication status integer. */
    Q_SCRIPTABLE int GetAuthStatus() const;
    /** D-Bus callable: attempts to unlock the screen with the given RFID data. */
    Q_SCRIPTABLE bool UnlockScreen(const QString& rfidData);
    /** D-Bus callable: immediately locks the screen. */
    Q_SCRIPTABLE bool LockScreen();
    /** D-Bus callable: returns the authenticated lecturer's name. */
    Q_SCRIPTABLE QString GetAuthenticatedLecturer() const;

    /** D-Bus callable: marks the room as active when a scenario executes or device turns on. */
    Q_SCRIPTABLE bool SetRoomActive();

    /** D-Bus callable: replaces the lecturer database with synced data from the server. */
    Q_SCRIPTABLE bool SyncLecturerList(const QByteArray& payload);

    /** Handles an incoming RFID read from the reader hardware. */
    void onRfidRead(const QString& rfidData);
    /** Called when the RFID reader hardware connects. */
    void onRfidReaderConnected();
    /** Called when the RFID reader hardware disconnects. */
    void onRfidReaderDisconnected();
    /** Called when the RFID reader reports an error. */
    void onRfidReaderError(const QString& error);

protected slots:
    /** Locks the screen when the auto-lock idle timer fires. */
    void onLockTimeout();

    /** Periodically checks room usage conditions (school hours, max duration). */
    void onMonitorTimer();

private:
    /** Internal states for the authentication FSM. */
    enum AuthStatus {
        LOCKED = 0,
        UNLOCKING = 1,
        UNLOCKED = 2,
        LOCKING = 3,
        ERROR = 4
    };

    /** Loads lecturer RFID entries from a JSON file on disk. */
    bool loadLecturerDatabase();
    /** Persists the current lecturer database to disk as JSON. */
    bool saveLecturerDatabase();
    /** Looks up an RFID in the database and checks if the lecturer is authorized. */
    bool verifyLecturerRfid(const QString& rfidData, Lecturer& lecturer);
    /** Returns true if the RFID string has valid format (alphanumeric, 1-50 chars). */
    bool isRfidValid(const QString& rfid) const;

    /** Sets a new auth status and emits AuthStatusChanged. */
    void setAuthStatus(AuthStatus newStatus);
    /** Puts the service into error state with the given message. */
    void setError(const QString& error);

    /** Starts the timer that auto-locks after m_lockTimeoutMs of inactivity. */
    void startAutoLockTimer();
    /** Cancels the auto-lock timer. */
    void cancelAutoLockTimer();

    // Configuration
    int m_lockTimeoutMs;
    QString m_databasePath;
    QString m_schoolHoursPath;

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
    QTimer m_monitorTimer;

    // Statistics
    int m_totalAuthAttempts;
    int m_successfulAuths;
    int m_failedAuths;

    // Room monitoring
    static constexpr int MONITOR_INTERVAL_MS = 60000;
    static constexpr int MAX_ROOM_USAGE_MINUTES = 240;
    QDateTime m_activeStartTime;
    bool m_roomActive;
    bool m_alertOverrunSent;
    bool m_alertOutOfHoursSent;

    // School hours
    struct SchoolDayHours {
        int startHour = 7;
        int startMinute = 0;
        int endHour = 18;
        int endMinute = 0;
    };
    QMap<int, SchoolDayHours> m_schoolHours;

    // Room monitoring methods
    void loadSchoolHours();
    bool isWithinSchoolHours() const;
    void publishRoomAlert(const QString& alertType, const QString& reason, int elapsedMinutes);
    void resetRoomMonitoring();
};

#endif // AUTH_SERVICE_H
