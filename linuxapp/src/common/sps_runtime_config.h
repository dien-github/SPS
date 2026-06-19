#ifndef SPS_RUNTIME_CONFIG_H
#define SPS_RUNTIME_CONFIG_H

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QtGlobal>

namespace SPS::Runtime {

/** Read a string from an environment variable, or return the fallback if unset. */
inline QString envString(const char* name, const QString& fallback = QString()) {
    const QByteArray value = qgetenv(name);
    if (value.isEmpty()) {
        return fallback;
    }
    return QString::fromLocal8Bit(value);
}

/** Read a boolean from an environment variable (accepts 1/true/yes/on/enabled). */
inline bool envBool(const char* name, bool fallback = false) {
    const QString value = envString(name).trimmed().toLower();
    if (value.isEmpty()) {
        return fallback;
    }

    if (value == "1" || value == "true" || value == "yes" ||
        value == "on" || value == "enabled") {
        return true;
    }

    if (value == "0" || value == "false" || value == "no" ||
        value == "off" || value == "disabled") {
        return false;
    }

    return fallback;
}

/** Read an integer from an environment variable, or return the fallback if invalid. */
inline int envInt(const char* name, int fallback) {
    bool ok = false;
    const int value = envString(name).toInt(&ok);
    return ok ? value : fallback;
}

/** Returns the SPS configuration directory (from SPS_CONFIG_DIR env or default). */
inline QString configDir() {
    return envString("SPS_CONFIG_DIR", "/opt/sps/config");
}

/** Resolve a config file path, checking an env override first, then using configDir. */
inline QString configFile(const char* overrideEnv, const QString& fileName) {
    const QString overridePath = envString(overrideEnv);
    if (!overridePath.isEmpty()) {
        return overridePath;
    }
    return QDir(configDir()).filePath(fileName);
}

/** Resolve a log file path from SPS_LOG_DIR env, or use the default path. */
inline QString logFilePath(const QString& defaultPath) {
    const QString logDir = envString("SPS_LOG_DIR");
    if (logDir.isEmpty()) {
        return defaultPath;
    }

    QDir().mkpath(logDir);
    return QDir(logDir).filePath(QFileInfo(defaultPath).fileName());
}

/** Return a fallback log file path in the current working directory's logs/ folder. */
inline QString fallbackLogFilePath(const QString& defaultPath) {
    const QString logDir = QDir(QDir::currentPath()).filePath("logs");
    QDir().mkpath(logDir);
    return QDir(logDir).filePath(QFileInfo(defaultPath).fileName());
}

} // namespace SPS::Runtime

#endif // SPS_RUNTIME_CONFIG_H
