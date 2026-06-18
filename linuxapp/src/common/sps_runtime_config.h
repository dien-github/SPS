#ifndef SPS_RUNTIME_CONFIG_H
#define SPS_RUNTIME_CONFIG_H

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QtGlobal>

namespace SPS::Runtime {

inline QString envString(const char* name, const QString& fallback = QString()) {
    const QByteArray value = qgetenv(name);
    if (value.isEmpty()) {
        return fallback;
    }
    return QString::fromLocal8Bit(value);
}

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

inline int envInt(const char* name, int fallback) {
    bool ok = false;
    const int value = envString(name).toInt(&ok);
    return ok ? value : fallback;
}

inline QString configDir() {
    return envString("SPS_CONFIG_DIR", "/opt/sps/config");
}

inline QString configFile(const char* overrideEnv, const QString& fileName) {
    const QString overridePath = envString(overrideEnv);
    if (!overridePath.isEmpty()) {
        return overridePath;
    }
    return QDir(configDir()).filePath(fileName);
}

inline QString logFilePath(const QString& defaultPath) {
    const QString logDir = envString("SPS_LOG_DIR");
    if (logDir.isEmpty()) {
        return defaultPath;
    }

    QDir().mkpath(logDir);
    return QDir(logDir).filePath(QFileInfo(defaultPath).fileName());
}

inline QString fallbackLogFilePath(const QString& defaultPath) {
    const QString logDir = QDir(QDir::currentPath()).filePath("logs");
    QDir().mkpath(logDir);
    return QDir(logDir).filePath(QFileInfo(defaultPath).fileName());
}

} // namespace SPS::Runtime

#endif // SPS_RUNTIME_CONFIG_H
