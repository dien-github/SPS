#include "sps_service_base.h"
#include <QCoreApplication>
#include <QDBusError>

SpsServiceBase::SpsServiceBase(const QString& serviceName, 
                               const QString& objectPath,
                               QObject* parent)
    : QObject(parent), m_serviceName(serviceName), m_objectPath(objectPath),
      m_isRegistered(false), m_isRunning(false),
      m_dbusConnection(QDBusConnection::systemBus()) {
}

SpsServiceBase::~SpsServiceBase() {
    if (m_isRegistered) {
        unregisterService();
    }
}

// Register service on D-Bus
bool SpsServiceBase::registerService() {
    if (!m_dbusConnection.isConnected()) {
        logError("D-Bus system bus not connected");
        return false;
    }

    if (!m_dbusConnection.registerService(m_serviceName)) {
        logError(QString("Failed to register service name: %1")
            .arg(m_dbusConnection.lastError().message()));
        return false;
    }

    logInfo(QString("Service registered: %1").arg(m_serviceName));
    m_isRegistered = true;
    return true;
}

// Register object on D-Bus
bool SpsServiceBase::registerObject(QDBusAbstractAdaptor* adaptor) {
    if (!m_dbusConnection.isConnected()) {
        logError("D-Bus system bus not connected");
        return false;
    }

    if (!m_dbusConnection.registerObject(m_objectPath, adaptor->parent())) {
        logError(QString("Failed to register object: %1")
            .arg(m_objectPath));
        return false;
    }

    logInfo(QString("Object registered: %1").arg(m_objectPath));
    return true;
}

// Unregister service from D-Bus
bool SpsServiceBase::unregisterService() {
    if (!m_isRegistered) {
        return true;
    }

    m_dbusConnection.unregisterService(m_serviceName);
    m_dbusConnection.unregisterObject(m_objectPath);

    logInfo("Service unregistered");
    m_isRegistered = false;
    return true;
}

// Shutdown
void SpsServiceBase::shutdown() {
    logInfo("Service shutting down...");
    unregisterService();
    setRunning(false);
}

// Get service status
QString SpsServiceBase::getStatus() const {
    if (!m_isRunning) {
        return "STOPPED";
    }
    return "RUNNING";
}
