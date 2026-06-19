#ifndef SPS_SERVICE_BASE_H
#define SPS_SERVICE_BASE_H

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusAbstractAdaptor>
#include <QString>
#include <memory>
#include "sps_logger.h"

/** Base class for all SPS services, providing common initialization, D-Bus registration, and lifecycle management. */
class SpsServiceBase : public QObject {
    Q_OBJECT

public:
    /** Constructor: create a service with a D-Bus name, object path, and optional parent. */
    explicit SpsServiceBase(const QString& serviceName, 
                            const QString& objectPath,
                            QObject* parent = nullptr);
    /** Destructor: unregisters from D-Bus if still registered. */
    virtual ~SpsServiceBase();

    /** Initialize the service (pure virtual — must be implemented by subclasses). */
    virtual bool initialize() = 0;
    /** Shut down the service and release resources. */
    virtual void shutdown();

    /** Register this service name on the D-Bus system bus. */
    bool registerService();
    /** Register a D-Bus adaptor object at this service's object path. */
    bool registerObject(QDBusAbstractAdaptor* adaptor);
    /** Unregister the service and object from D-Bus. */
    bool unregisterService();

    /** Returns the D-Bus service name. */
    QString getServiceName() const { return m_serviceName; }
    /** Returns the D-Bus object path. */
    QString getObjectPath() const { return m_objectPath; }
    /** Returns whether the service is registered on D-Bus. */
    bool isRegistered() const { return m_isRegistered; }
    /** Returns whether the service is currently running. */
    bool isRunning() const { return m_isRunning; }

    /** Returns a human-readable status description of the service. */
    virtual QString getStatus() const;
    /** Returns the version string of this service. */
    virtual QString getVersion() const { return "1.0.0"; }

    /** Log a message at the given severity level for this service. */
    void log(LogLevel level, const QString& message) {
        Logger::instance().log(level, m_serviceName, message);
    }

    /** Log a debug-level message for this service. */
    void logDebug(const QString& msg) {
        log(LogLevel::DEBUG, msg);
    }

    /** Log an info-level message for this service. */
    void logInfo(const QString& msg) {
        log(LogLevel::INFO, msg);
    }

    /** Log a warning-level message for this service. */
    void logWarning(const QString& msg) {
        log(LogLevel::WARNING, msg);
    }

    /** Log an error-level message for this service. */
    void logError(const QString& msg) {
        log(LogLevel::ERROR, msg);
    }

    /** Log a critical-level message for this service. */
    void logCritical(const QString& msg) {
        log(LogLevel::CRITICAL, msg);
    }

signals:
    /** Emitted after the service has been fully initialized. */
    void initialized();
    /** Emitted when a shutdown has been requested. */
    void shutdownRequested();
    /** Emitted when the service status changes. */
    void statusChanged(const QString& newStatus);
    /** Emitted when an error occurs in the service. */
    void errorOccurred(const QString& error);

public slots:
    /** D-Bus callable: returns the service name. */
    virtual QString GetServiceName() const { return m_serviceName; }
    /** D-Bus callable: returns the current service status. */
    virtual QString GetServiceStatus() const { return getStatus(); }
    /** D-Bus callable: returns the service version. */
    virtual QString GetServiceVersion() const { return getVersion(); }

    /** D-Bus callable: request a graceful shutdown of the service. */
    virtual void RequestShutdown() {
        logInfo("Shutdown requested");
        emit shutdownRequested();
        QCoreApplication::quit();
    }

protected:
    /** Mark the service as running or stopped and emit statusChanged. */
    void setRunning(bool running) {
        m_isRunning = running;
        if (running) {
            emit statusChanged("RUNNING");
        }
    }

    /** Log an error and emit the errorOccurred signal. */
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
