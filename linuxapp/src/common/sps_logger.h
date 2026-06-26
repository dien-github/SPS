#ifndef SPS_LOGGER_H
#define SPS_LOGGER_H

#include <QString>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include "sps_runtime_config.h"

/** Logging severity levels. */
enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    CRITICAL = 4
};

/** Unified logging system (singleton). */
class Logger {
public:
    /** Returns the singleton Logger instance. */
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    /** Initialize the logger with a file path and minimum log level. */
    void init(const QString& logFilePath = "/var/log/sps/sps.log", 
              LogLevel level = LogLevel::INFO) {
        QMutexLocker lock(&m_mutex);
        QString resolvedPath = SPS::Runtime::logFilePath(logFilePath);
        QDir().mkpath(QFileInfo(resolvedPath).absolutePath());

        m_logFile.setFileName(resolvedPath);
        m_minLevel = level;

        if (!m_logFile.open(QIODevice::Append | QIODevice::Text)) {
            qWarning() << "Failed to open log file:" << resolvedPath;
            resolvedPath = SPS::Runtime::fallbackLogFilePath(logFilePath);
            m_logFile.setFileName(resolvedPath);

            if (!m_logFile.open(QIODevice::Append | QIODevice::Text)) {
                qWarning() << "Failed to open fallback log file:" << resolvedPath;
            }
        }
    }

    /** Log a message at the given severity level for a specific component. */
    void log(LogLevel level, const QString& component, const QString& message) {
        if (level < m_minLevel) {
            return;  // Skip logging below minimum level
        }

        QMutexLocker lock(&m_mutex);

        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
        QString levelStr = levelToString(level);
        QString logLine = QString("[%1] [%2] [%3] %4")
            .arg(timestamp)
            .arg(levelStr)
            .arg(component)
            .arg(message);

        // Also output to debug console
        qDebug() << logLine;

        // Write to file
        if (m_logFile.isOpen()) {
            QTextStream out(&m_logFile);
            out << logLine << "\n";
            out.flush();
        }
    }

    /** Log a debug-level message for a component. */
    void debug(const QString& component, const QString& message) {
        log(LogLevel::DEBUG, component, message);
    }

    /** Log an info-level message for a component. */
    void info(const QString& component, const QString& message) {
        log(LogLevel::INFO, component, message);
    }

    /** Log a warning-level message for a component. */
    void warning(const QString& component, const QString& message) {
        log(LogLevel::WARNING, component, message);
    }

    /** Log an error-level message for a component. */
    void error(const QString& component, const QString& message) {
        log(LogLevel::ERROR, component, message);
    }

    /** Log a critical-level message for a component. */
    void critical(const QString& component, const QString& message) {
        log(LogLevel::CRITICAL, component, message);
    }

    /** Set the minimum log level (messages below this are ignored). */
    void setMinLevel(LogLevel level) {
        QMutexLocker lock(&m_mutex);
        m_minLevel = level;
    }

    /** Close the log file. */
    void close() {
        QMutexLocker lock(&m_mutex);
        if (m_logFile.isOpen()) {
            m_logFile.close();
        }
    }

private:
    /** Private constructor (singleton). */
    Logger() : m_minLevel(LogLevel::INFO) {}

    /** Private destructor: closes the log file. */
    ~Logger() {
        close();
    }

    /** Convert a LogLevel enum to its string representation. */
    QString levelToString(LogLevel level) const {
        switch (level) {
            case LogLevel::DEBUG:    return "DEBUG";
            case LogLevel::INFO:     return "INFO";
            case LogLevel::WARNING:  return "WARN";
            case LogLevel::ERROR:    return "ERROR";
            case LogLevel::CRITICAL: return "CRIT";
            default:                 return "UNKNOWN";
        }
    }

    mutable QMutex m_mutex;
    QFile m_logFile;
    LogLevel m_minLevel;
};

#endif // SPS_LOGGER_H
