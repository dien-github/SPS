#include "sps_service_base.h"
#include <QCoreApplication>
#include <QDBusError>

/** Constructor: create a service with a D-Bus name, object path, and optional parent. */
SpsServiceBase::SpsServiceBase(const QString& serviceName, 
                               const QString& objectPath,
                               QObject* parent)
    : QObject(parent), m_serviceName(serviceName), m_objectPath(objectPath),
      m_isRegistered(false), m_isRunning(false),
      m_dbusConnection(QDBusConnection::systemBus()) {
}

/** Destructor: unregisters from D-Bus if still registered. */
SpsServiceBase::~SpsServiceBase() {
    if (m_isRegistered) {
        unregisterService();
    }
}

/** Register this service name on the D-Bus system bus. */
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

/** Register a D-Bus adaptor object at this service's object path. */
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

/** Unregister the service and object from D-Bus. */
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

/** Shut down the service and release resources. */
void SpsServiceBase::shutdown() {
    logInfo("Service shutting down...");
    unregisterService();
    setRunning(false);
}

/** Returns a human-readable status description of the service. */
QString SpsServiceBase::getStatus() const {
    if (!m_isRunning) {
        return "STOPPED";
    }
    return "RUNNING";
}
