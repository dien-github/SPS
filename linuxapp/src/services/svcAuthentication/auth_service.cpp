#include "auth_service.h"
#include "../common/sps_logger.h"
#include "../common/sps_runtime_config.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QTime>

/** Constructs the auth service, loads config paths, and sets up internal timers. */
AuthService::AuthService(QObject* parent)
    : SpsServiceBase("com.sps.auth", "/com/sps/auth", parent),
      m_lockTimeoutMs(300000),  // 5 minutes default (kept for backward compat)
      m_databasePath(SPS::Runtime::configFile("SPS_LECTURERS_FILE", "lecturers.json")),
      m_schoolHoursPath(SPS::Runtime::configFile("SPS_SCHOOL_HOURS_FILE", "school_hours.json")),
      m_status(LOCKED),
      m_totalAuthAttempts(0),
      m_successfulAuths(0),
      m_failedAuths(0),
      m_roomActive(false),
      m_alertOverrunSent(false),
      m_alertOutOfHoursSent(false) {

    // Setup monitor timer (periodic check for room usage conditions)
    connect(&m_monitorTimer, &QTimer::timeout, this, &AuthService::onMonitorTimer);
    m_monitorTimer.setInterval(MONITOR_INTERVAL_MS);

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

    // Load school hours config
    loadSchoolHours();

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
    m_monitorTimer.stop();
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

    // Start room monitoring session
    resetRoomMonitoring();
    m_monitorTimer.start(MONITOR_INTERVAL_MS);
    logInfo(QString("Room monitoring started - unlock time: %1").arg(m_unlockedAt.toString(Qt::ISODate)));

    return true;
}

/** D-Bus callable: locks the screen, clears the current lecturer, cancels auto-lock. */
bool AuthService::LockScreen() {
    logInfo("Lock request");

    if (m_status == LOCKED) {
        return true;
    }

    cancelAutoLockTimer();
    m_monitorTimer.stop();
    setAuthStatus(LOCKED);
    m_currentLecturer = Lecturer();
    m_lockedAt = QDateTime::currentDateTime();
    resetRoomMonitoring();

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

/** D-Bus callable: marks the room active when a scenario executes or device turns on. */
bool AuthService::SetRoomActive() {
    if (m_status != UNLOCKED) {
        logDebug("SetRoomActive ignored - screen not unlocked");
        return false;
    }
    if (!m_roomActive) {
        m_roomActive = true;
        m_activeStartTime = QDateTime::currentDateTime();
        logInfo(QString("Room activated at %1").arg(m_activeStartTime.toString(Qt::ISODate)));
    }
    return true;
}

/** D-Bus callable: replaces the lecturer database with synced data from the server. */
bool AuthService::SyncLecturerList(const QByteArray& payload) {
    logInfo(QString("SyncLecturerList called with %1 bytes").arg(payload.size()));

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        logError(QString("SyncLecturerList invalid JSON: %1").arg(parseError.errorString()));
        return false;
    }

    QJsonArray lecturers;
    if (doc.isArray()) {
        lecturers = doc.array();
    } else if (doc.isObject()) {
        lecturers = doc.object().value("lecturers").toArray();
    } else {
        logError("SyncLecturerList payload is neither JSON array nor object with lecturers key");
        return false;
    }

    if (lecturers.isEmpty()) {
        logError("SyncLecturerList received empty lecturer array – existing database preserved");
        return false;
    }

    QMap<QString, Lecturer> newLecturers;
    int skipped = 0;

    for (const QJsonValue& val : lecturers) {
        if (!val.isObject()) {
            skipped++;
            continue;
        }

        QJsonObject obj = val.toObject();
        QString id = obj["id"].toVariant().toString();
        QString name = obj["name"].toString();
        QString rfid = obj["rfid"].toString();
        if (rfid.isEmpty()) rfid = obj["code"].toString();
        if (rfid.isEmpty()) rfid = obj["card_id"].toString();

        const bool authorized = obj.contains("authorized")
            ? obj["authorized"].toBool(true)
            : obj["enabled"].toBool(true);

        if (id.isEmpty() || rfid.isEmpty()) {
            logWarning(QString("SyncLecturerList skipping row: missing id or RFID (id='%1', rfid='%2')")
                .arg(id, rfid));
            skipped++;
            continue;
        }

        Lecturer lecturer(id, name, rfid);
        lecturer.authorized = authorized;
        newLecturers[rfid] = lecturer;
    }

    if (newLecturers.isEmpty()) {
        logError("SyncLecturerList: no valid lecturers in payload – existing database preserved");
        return false;
    }

    m_lecturers = newLecturers;

    if (!saveLecturerDatabase()) {
        logWarning("SyncLecturerList: in-memory update succeeded but persist to disk failed");
    }

    const int loaded = m_lecturers.size();
    logInfo(QString("Lecturer list synced: %1 lecturers loaded%2")
        .arg(loaded)
        .arg(skipped > 0 ? QString(", %1 skipped").arg(skipped) : QString()));
    emit LecturerListUpdated(loaded);
    return true;
}

/** Periodically checks room usage conditions and publishes alerts when triggered. */
void AuthService::onMonitorTimer() {
    if (m_status != UNLOCKED) {
        m_monitorTimer.stop();
        return;
    }

    QDateTime now = QDateTime::currentDateTime();
    int elapsedMinutes = 0;
    if (m_roomActive) {
        elapsedMinutes = static_cast<int>(m_activeStartTime.secsTo(now) / 60);
    } else {
        elapsedMinutes = static_cast<int>(m_unlockedAt.secsTo(now) / 60);
    }

    // Check 1: Room usage exceeds maximum duration
    if (m_roomActive && !m_alertOverrunSent && elapsedMinutes >= MAX_ROOM_USAGE_MINUTES) {
        publishRoomAlert("ROOM_USAGE_OVERRUN",
            QString("Room usage exceeded %1 minutes").arg(MAX_ROOM_USAGE_MINUTES),
            elapsedMinutes);
        m_alertOverrunSent = true;
    }

    // Check 2: Current time is outside school operating hours
    if (!m_alertOutOfHoursSent && !isWithinSchoolHours()) {
        publishRoomAlert("OUT_OF_SCHOOL_HOURS",
            "Current time is outside configured school operating hours",
            elapsedMinutes);
        m_alertOutOfHoursSent = true;
    }
}

/** Loads school hours from the JSON config file. Falls back to defaults if unavailable. */
void AuthService::loadSchoolHours() {
    QFile file(m_schoolHoursPath);
    if (!file.open(QIODevice::ReadOnly)) {
        logWarning(QString("Cannot open school hours config: %1 - using defaults").arg(m_schoolHoursPath));
        // Default: weekdays 07:00-18:00, no weekend hours
        for (int d = 1; d <= 5; ++d) {
            m_schoolHours[d] = {7, 0, 18, 0};
        }
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        logWarning("School hours config is not a JSON object - using defaults");
        for (int d = 1; d <= 5; ++d) {
            m_schoolHours[d] = {7, 0, 18, 0};
        }
        return;
    }

    QJsonObject root = doc.object();
    QJsonObject hours = root["school_hours"].toObject();
    if (hours.isEmpty()) {
        logWarning("No school_hours key found in config - using defaults");
        for (int d = 1; d <= 5; ++d) {
            m_schoolHours[d] = {7, 0, 18, 0};
        }
        return;
    }

    QMap<QString, int> dayMap;
    dayMap["monday"] = 1;
    dayMap["tuesday"] = 2;
    dayMap["wednesday"] = 3;
    dayMap["thursday"] = 4;
    dayMap["friday"] = 5;
    dayMap["saturday"] = 6;
    dayMap["sunday"] = 7;

    for (auto it = dayMap.constBegin(); it != dayMap.constEnd(); ++it) {
        QJsonValue dayVal = hours[it.key()];
        if (!dayVal.isObject()) continue;

        QJsonObject dayObj = dayVal.toObject();
        SchoolDayHours h;

        QString startStr = dayObj["start"].toString("07:00");
        QString endStr = dayObj["end"].toString("18:00");

        QStringList startParts = startStr.split(":");
        if (startParts.size() == 2) {
            h.startHour = startParts[0].toInt();
            h.startMinute = startParts[1].toInt();
        }

        QStringList endParts = endStr.split(":");
        if (endParts.size() == 2) {
            h.endHour = endParts[0].toInt();
            h.endMinute = endParts[1].toInt();
        }

        m_schoolHours[it.value()] = h;
    }

    logInfo(QString("Loaded school hours for %1 days").arg(m_schoolHours.size()));
}

/** Returns true if the current time falls within configured school hours for today. */
bool AuthService::isWithinSchoolHours() const {
    QDateTime now = QDateTime::currentDateTime();
    int dayOfWeek = now.date().dayOfWeek(); // Qt: Mon=1, Sun=7

    auto it = m_schoolHours.find(dayOfWeek);
    if (it == m_schoolHours.end()) {
        return true;
    }

    const SchoolDayHours& hours = it.value();
    int nowMinutes = QTime::currentTime().hour() * 60 + QTime::currentTime().minute();
    int startMinutes = hours.startHour * 60 + hours.startMinute;
    int endMinutes = hours.endHour * 60 + hours.endMinute;

    // If start time equals end time, no school this day
    if (startMinutes == endMinutes) {
        return false;
    }

    return nowMinutes >= startMinutes && nowMinutes < endMinutes;
}

/** Builds the alert payload and emits RoomMonitorAlert to the HMI. */
void AuthService::publishRoomAlert(const QString& alertType, const QString& reason, int elapsedMinutes) {
    QJsonObject payload;
    payload["alert_type"] = alertType;
    payload["lecturer_id"] = m_currentLecturer.id;
    payload["lecturer_name"] = m_currentLecturer.name;
    payload["unlock_time"] = m_unlockedAt.toString(Qt::ISODate);
    payload["active_start_time"] = m_roomActive ? m_activeStartTime.toString(Qt::ISODate) : "";
    payload["elapsed_minutes"] = elapsedMinutes;
    payload["current_time"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    payload["reason"] = reason;

    QJsonDocument doc(payload);
    QString payloadStr = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));

    logInfo(QString("Room monitor alert: %1 - %2 (elapsed: %3 min)")
        .arg(alertType, reason).arg(elapsedMinutes));
    emit RoomMonitorAlert(alertType, payloadStr);
}

/** Resets all room monitoring state (called on lock or when session ends). */
void AuthService::resetRoomMonitoring() {
    m_roomActive = false;
    m_activeStartTime = QDateTime();
    m_alertOverrunSent = false;
    m_alertOutOfHoursSent = false;
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

/** Persists the current lecturer database to disk as JSON. */
bool AuthService::saveLecturerDatabase() {
    QJsonArray array;
    for (auto it = m_lecturers.constBegin(); it != m_lecturers.constEnd(); ++it) {
        const Lecturer& lec = it.value();
        QJsonObject obj;
        obj["id"] = lec.id;
        obj["name"] = lec.name;
        obj["rfid"] = lec.rfidCard;
        obj["authorized"] = lec.authorized;
        array.append(obj);
    }

    QFile file(m_databasePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        logError(QString("Cannot write lecturer database: %1").arg(m_databasePath));
        return false;
    }

    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    file.close();
    logInfo(QString("Saved %1 lecturers to %2").arg(array.size()).arg(m_databasePath));
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
