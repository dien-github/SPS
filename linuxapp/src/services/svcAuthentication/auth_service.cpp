#include "auth_service.h"
#include "../common/sps_logger.h"
#include "../common/sps_runtime_config.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>

/** Constructs the auth service, loads config paths, and sets up internal timers. */
AuthService::AuthService(QObject* parent)
    : SpsServiceBase("com.sps.auth", "/com/sps/auth", parent),
      m_lockTimeoutMs(300000),  // 5 minutes default
      m_databasePath(SPS::Runtime::configFile("SPS_LECTURERS_FILE", "lecturers.json")),
      m_status(LOCKED),
      m_totalAuthAttempts(0),
      m_successfulAuths(0),
      m_failedAuths(0) {

    // Setup auto-lock timer
    connect(&m_autoLockTimer, &QTimer::timeout, this, &AuthService::onLockTimeout);
    m_autoLockTimer.setSingleShot(true);

    // Setup debounce timer (prevent duplicate RFID reads)
    connect(&m_debounceTimer, &QTimer::timeout, this, [this]() {
        m_debounceTimer.stop();
    });
    m_debounceTimer.setSingleShot(true);

    logInfo(QString("Service created: %1").arg(getServiceName()));
}

/** Destructor - calls shutdown to clean up. */
AuthService::~AuthService() {
    shutdown();
}

/** Initializes the service: loads lecturer database and registers the D-Bus interface. */
bool AuthService::initialize() {
    logInfo("Initializing authentication service...");

    // Load lecturer database
    if (!loadLecturerDatabase()) {
        logWarning("Failed to load lecturer database - will operate in demo mode");
    }

    logInfo(QString("Loaded %1 lecturers").arg(m_lecturers.size()));

    // Register D-Bus service and object
    if (!registerService()) {
        logError("Failed to register D-Bus service");
        return false;
    }

    setRunning(true);
    setAuthStatus(LOCKED);
    m_lockedAt = QDateTime::currentDateTime();

    logInfo("Authentication service initialized successfully");
    emit statusChanged("INITIALIZED");

    return true;
}

/** Shuts down the service, stops timers, locks the screen, and calls base shutdown. */
void AuthService::shutdown() {
    logInfo("Shutting down authentication service...");

    m_autoLockTimer.stop();
    m_debounceTimer.stop();

    if (m_status != LOCKED) {
        setAuthStatus(LOCKED);
    }

    SpsServiceBase::shutdown();
    logInfo("Authentication service shut down");
}

/** Returns a human-readable summary of the auth service state and statistics. */
QString AuthService::getStatus() const {
    return QString("Auth Status: %1 | Lecturer: %2 | Attempts: %3/%4")
        .arg(getAuthStatusString())
        .arg(m_currentLecturer.name.isEmpty() ? "None" : m_currentLecturer.name)
        .arg(m_successfulAuths)
        .arg(m_totalAuthAttempts);
}

/** Returns the current auth status as an integer (0=LOCKED, 2=UNLOCKED, etc.). */
int AuthService::getAuthStatus() const {
    return static_cast<int>(m_status);
}

/** Returns the current auth status as a readable string ("LOCKED", "UNLOCKED", etc.). */
QString AuthService::getAuthStatusString() const {
    switch (m_status) {
        case LOCKED:     return "LOCKED";
        case UNLOCKING:  return "UNLOCKING";
        case UNLOCKED:   return "UNLOCKED";
        case LOCKING:    return "LOCKING";
        case ERROR:      return "ERROR";
        default:         return "UNKNOWN";
    }
}

/** Returns the name of the currently authenticated lecturer (may be empty). */
QString AuthService::getAuthenticatedLecturer() const {
    return m_currentLecturer.name;
}

/** Simulates an RFID read by emitting rfidDataReceived and calling onRfidRead. */
void AuthService::simulateRfidRead(const QString& rfidData) {
    emit rfidDataReceived(rfidData);
    onRfidRead(rfidData);
}

/** D-Bus callable: returns the current auth status integer. */
int AuthService::GetAuthStatus() const {
    return getAuthStatus();
}

/** D-Bus callable: validates the RFID, authenticates the lecturer, and unlocks the screen. */
bool AuthService::UnlockScreen(const QString& rfidData) {
    logInfo(QString("Unlock request with RFID: %1").arg(rfidData));

    if (m_status == UNLOCKED) {
        logWarning("Already unlocked");
        return true;
    }

    m_totalAuthAttempts++;

    // Prevent rapid repeated attempts (debounce)
    if (m_debounceTimer.isActive()) {
        logWarning("Debounce active - ignoring request");
        emit AuthenticationFailed("Too many attempts - please wait");
        m_failedAuths++;
        return false;
    }
    m_debounceTimer.start(500);  // 500ms debounce

    // Verify RFID
    Lecturer lecturer;
    if (!verifyLecturerRfid(rfidData, lecturer)) {
        logWarning(QString("Authentication failed for RFID: %1").arg(rfidData));
        m_failedAuths++;
        setAuthStatus(ERROR);
        emit AuthenticationFailed("RFID not recognized");
        QTimer::singleShot(1000, this, [this]() {
            setAuthStatus(LOCKED);
        });
        return false;
    }

    // Authentication successful
    setAuthStatus(UNLOCKED);
    m_currentLecturer = lecturer;
    m_unlockedAt = QDateTime::currentDateTime();
    m_successfulAuths++;

    logInfo(QString("Screen unlocked for: %1").arg(lecturer.name));
    emit LecturerAuthenticated(lecturer.name, m_unlockedAt.toMSecsSinceEpoch());

    // Start auto-lock timer
    startAutoLockTimer();

    return true;
}

/** D-Bus callable: locks the screen, clears the current lecturer, cancels auto-lock. */
bool AuthService::LockScreen() {
    logInfo("Lock request");

    if (m_status == LOCKED) {
        return true;
    }

    cancelAutoLockTimer();
    setAuthStatus(LOCKED);
    m_currentLecturer = Lecturer();
    m_lockedAt = QDateTime::currentDateTime();

    logInfo("Screen locked");

    return true;
}

/** D-Bus callable: returns the name of the currently authenticated lecturer. */
QString AuthService::GetAuthenticatedLecturer() const {
    return getAuthenticatedLecturer();
}

/** Slot: processes raw RFID data and attempts to unlock the screen. */
void AuthService::onRfidRead(const QString& rfidData) {
    logDebug(QString("RFID read: %1").arg(rfidData));

    if (m_status != LOCKED && m_status != ERROR) {
        logDebug("Not in locked state, ignoring RFID");
        return;
    }

    UnlockScreen(rfidData);
}

/** Slot: triggered when the auto-lock timer expires; locks the screen. */
void AuthService::onLockTimeout() {
    logInfo("Auto-lock timeout triggered");
    LockScreen();
}

/** Slot: logs when the RFID reader hardware connects. */
void AuthService::onRfidReaderConnected() {
    logInfo("RFID reader connected");
}

/** Slot: logs when the RFID reader hardware disconnects. */
void AuthService::onRfidReaderDisconnected() {
    logWarning("RFID reader disconnected");
}

/** Slot: logs the RFID reader error and sets the service to error state. */
void AuthService::onRfidReaderError(const QString& error) {
    logError(QString("RFID reader error: %1").arg(error));
    setAuthStatus(ERROR);
}

/** Loads lecturer entries from a JSON file; falls back to demo lecturers if file is missing. */
bool AuthService::loadLecturerDatabase() {
    QFile file(m_databasePath);

    if (!file.open(QIODevice::ReadOnly)) {
        logWarning(QString("Cannot open database file: %1").arg(m_databasePath));

        // Create sample database for testing
        m_lecturers["RFID001"] = Lecturer("L001", "Dr. Smith", "RFID001");
        m_lecturers["RFID002"] = Lecturer("L002", "Prof. Johnson", "RFID002");
        m_lecturers["RFID003"] = Lecturer("L003", "Dr. Williams", "RFID003");

        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) {
        logError("Database file is not a JSON array");
        return false;
    }

    QJsonArray array = doc.array();

    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }

        QJsonObject obj = value.toObject();
        QString id = obj["id"].toVariant().toString();
        QString name = obj["name"].toString();
        QString rfid = obj["rfid"].toString();

        if (rfid.isEmpty()) {
            rfid = obj["code"].toString();
        }
        if (rfid.isEmpty()) {
            rfid = obj["card_id"].toString();
        }

        const bool authorized = obj.contains("authorized")
            ? obj["authorized"].toBool(true)
            : obj["enabled"].toBool(true);

        if (!id.isEmpty() && !rfid.isEmpty()) {
            Lecturer lecturer(id, name, rfid);
            lecturer.authorized = authorized;
            m_lecturers[rfid] = lecturer;
        }
    }

    if (m_lecturers.isEmpty()) {
        logWarning("Lecturer database loaded but no valid RFID entries were found; using demo lecturers");
        m_lecturers["RFID001"] = Lecturer("L001", "Dr. Smith", "RFID001");
        m_lecturers["RFID002"] = Lecturer("L002", "Prof. Johnson", "RFID002");
        m_lecturers["RFID003"] = Lecturer("L003", "Dr. Williams", "RFID003");
        return false;
    }

    logInfo(QString("Loaded %1 lecturers from database").arg(m_lecturers.size()));
    return true;
}

/** Looks up an RFID in the database and returns the lecturer if authorized. */
bool AuthService::verifyLecturerRfid(const QString& rfidData, Lecturer& lecturer) {
    // Normalize RFID data (trim whitespace)
    QString normalizedRfid = rfidData.trimmed();

    if (!m_lecturers.contains(normalizedRfid)) {
        logWarning(QString("RFID not found in database: %1").arg(normalizedRfid));
        return false;
    }

    lecturer = m_lecturers[normalizedRfid];

    if (!lecturer.authorized) {
        logWarning(QString("Lecturer not authorized: %1").arg(lecturer.name));
        return false;
    }

    return true;
}

/** Checks if the RFID string is non-empty, ≤50 chars, and alphanumeric (with _ or -). */
bool AuthService::isRfidValid(const QString& rfid) const {
    if (rfid.isEmpty() || rfid.length() > 50) {
        return false;
    }

    // Allow alphanumeric only
    for (QChar c : rfid) {
        if (!c.isLetterOrNumber() && c != '_' && c != '-') {
            return false;
        }
    }

    return true;
}

/** Sets the auth status and emits AuthStatusChanged (no-op if status is unchanged). */
void AuthService::setAuthStatus(AuthStatus newStatus) {
    if (m_status == newStatus) {
        return;
    }

    m_status = newStatus;
    logInfo(QString("Auth status changed to: %1").arg(getAuthStatusString()));
    emit AuthStatusChanged(static_cast<int>(newStatus));
}

/** Sets the service to error state with the given message and emits AuthenticationFailed. */
void AuthService::setError(const QString& error) {
    m_lastError = error;
    logError(error);
    setAuthStatus(ERROR);
    emit AuthenticationFailed(error);
}

/** Starts the auto-lock timer that will trigger onLockTimeout after m_lockTimeoutMs. */
void AuthService::startAutoLockTimer() {
    m_autoLockTimer.start(m_lockTimeoutMs);
    logDebug(QString("Auto-lock timer started: %1ms").arg(m_lockTimeoutMs));
}

/** Stops the auto-lock timer so the screen will not lock automatically. */
void AuthService::cancelAutoLockTimer() {
    m_autoLockTimer.stop();
}
