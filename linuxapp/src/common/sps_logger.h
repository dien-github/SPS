#ifndef SPS_LOGGER_H
#define SPS_LOGGER_H

#include <QString>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QDebug>

// Logging severity levels
enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    CRITICAL = 4
};

// Unified logging system
class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    // Initialize logger with file path
    void init(const QString& logFilePath = "/var/log/sps/sps.log", 
              LogLevel level = LogLevel::INFO) {
        QMutexLocker lock(&m_mutex);
        m_logFile.setFileName(logFilePath);
        m_minLevel = level;

        if (!m_logFile.open(QIODevice::Append | QIODevice::Text)) {
            qWarning() << "Failed to open log file:" << logFilePath;
        }
    }

    // Log message
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

    // Convenience methods
    void debug(const QString& component, const QString& message) {
        log(LogLevel::DEBUG, component, message);
    }

    void info(const QString& component, const QString& message) {
        log(LogLevel::INFO, component, message);
    }

    void warning(const QString& component, const QString& message) {
        log(LogLevel::WARNING, component, message);
    }

    void error(const QString& component, const QString& message) {
        log(LogLevel::ERROR, component, message);
    }

    void critical(const QString& component, const QString& message) {
        log(LogLevel::CRITICAL, component, message);
    }

    // Set minimum log level
    void setMinLevel(LogLevel level) {
        QMutexLocker lock(&m_mutex);
        m_minLevel = level;
    }

    // Close log file
    void close() {
        QMutexLocker lock(&m_mutex);
        if (m_logFile.isOpen()) {
            m_logFile.close();
        }
    }

private:
    Logger() : m_minLevel(LogLevel::INFO) {}

    ~Logger() {
        close();
    }

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
