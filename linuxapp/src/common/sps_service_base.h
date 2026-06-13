#ifndef SPS_SERVICE_BASE_H
#define SPS_SERVICE_BASE_H

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusAbstractAdaptor>
#include <QString>
#include <memory>
#include "sps_logger.h"

// Base class for all SPS services
// Provides common initialization, D-Bus registration, and lifecycle management
class SpsServiceBase : public QObject {
    Q_OBJECT

public:
    explicit SpsServiceBase(const QString& serviceName, 
                            const QString& objectPath,
                            QObject* parent = nullptr);
    virtual ~SpsServiceBase();

    // Lifecycle methods
    virtual bool initialize() = 0;
    virtual void shutdown();

    // D-Bus registration
    bool registerService();
    bool registerObject(QDBusAbstractAdaptor* adaptor);
    bool unregisterService();

    // Getters
    QString getServiceName() const { return m_serviceName; }
    QString getObjectPath() const { return m_objectPath; }
    bool isRegistered() const { return m_isRegistered; }
    bool isRunning() const { return m_isRunning; }

    // Status
    virtual QString getStatus() const;
    virtual QString getVersion() const { return "1.0.0"; }

    // Logging
    void log(LogLevel level, const QString& message) {
        Logger::instance().log(level, m_serviceName, message);
    }

    void logDebug(const QString& msg) {
        log(LogLevel::DEBUG, msg);
    }

    void logInfo(const QString& msg) {
        log(LogLevel::INFO, msg);
    }

    void logWarning(const QString& msg) {
        log(LogLevel::WARNING, msg);
    }

    void logError(const QString& msg) {
        log(LogLevel::ERROR, msg);
    }

    void logCritical(const QString& msg) {
        log(LogLevel::CRITICAL, msg);
    }

signals:
    void initialized();
    void shutdownRequested();
    void statusChanged(const QString& newStatus);
    void errorOccurred(const QString& error);

public slots:
    // D-Bus methods that all services should implement
    virtual QString GetServiceName() const { return m_serviceName; }
    virtual QString GetServiceStatus() const { return getStatus(); }
    virtual QString GetServiceVersion() const { return getVersion(); }

    // Graceful shutdown
    virtual void RequestShutdown() {
        logInfo("Shutdown requested");
        emit shutdownRequested();
        QCoreApplication::quit();
    }

protected:
    void setRunning(bool running) {
        m_isRunning = running;
        if (running) {
            emit statusChanged("RUNNING");
        }
    }

    void setError(const QString& error) {
        logError(error);
        emit errorOccurred(error);
    }

private:
    QString m_serviceName;
    QString m_objectPath;
    bool m_isRegistered;
    bool m_isRunning;
    QDBusConnection m_dbusConnection;
};

#endif // SPS_SERVICE_BASE_H
