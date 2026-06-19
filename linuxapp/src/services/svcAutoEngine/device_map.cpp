#include "device_map.h"
#include "../common/sps_logger.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

bool DeviceMap::load(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        Logger::instance().warning("DeviceMap", QString("Cannot open device map file: %1").arg(path));
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        Logger::instance().error("DeviceMap", "Device map file is not a JSON object");
        return false;
    }

    QJsonObject root = doc.object();
    QJsonArray devices = root["devices"].toArray();
    if (devices.isEmpty()) {
        Logger::instance().warning("DeviceMap", "Device map file has no devices array");
        return false;
    }

    int loaded = 0;
    for (const QJsonValue& value : devices) {
        if (!value.isObject()) continue;

        QJsonObject obj = value.toObject();
        DeviceMapEntry entry;
        entry.id = obj["id"].toString().trimmed();
        if (entry.id.isEmpty()) continue;

        QString typeStr = obj["type"].toString().trimmed().toLower();
        if (typeStr == "light") entry.type = SPS::Device::Type::LIGHT;
        else if (typeStr == "curtain") entry.type = SPS::Device::Type::CURTAIN;
        else if (typeStr == "screen") entry.type = SPS::Device::Type::SCREEN;
        else if (typeStr == "projector") entry.type = SPS::Device::Type::PROJECTOR;
        else if (typeStr == "ac") entry.type = SPS::Device::Type::AC;
        else if (typeStr == "relay") entry.type = SPS::Device::Type::RELAY;
        else {
            Logger::instance().warning("DeviceMap",
                QString("Unknown device type '%1' for device '%2', skipping").arg(typeStr, entry.id));
            continue;
        }

        entry.channel = obj["channel"].toInt(0);
        if (entry.channel <= 0) {
            Logger::instance().warning("DeviceMap",
                QString("Invalid channel %1 for device '%2', skipping").arg(entry.channel).arg(entry.id));
            continue;
        }

        entry.method = obj["method"].toString().trimmed();

        m_entries[entry.id] = entry;
        loaded++;
    }

    Logger::instance().info("DeviceMap", QString("Loaded %1 device mappings from %2").arg(loaded).arg(path));
    return loaded > 0;
}

bool DeviceMap::loadDemo() {
    m_entries.clear();

    auto add = [this](const QString& id, SPS::Device::Type type, int channel, const QString& method) {
        DeviceMapEntry entry;
        entry.id = id;
        entry.type = type;
        entry.channel = channel;
        entry.method = method;
        m_entries[id] = entry;
    };

    add("light-class", SPS::Device::Type::LIGHT, 1, "ControlLight");
    add("light-board", SPS::Device::Type::LIGHT, 2, "ControlLight");
    add("curtain-main", SPS::Device::Type::CURTAIN, 1, "ControlCurtain");
    add("projector-main", SPS::Device::Type::PROJECTOR, 1, "ControlProjector");
    add("ac-main", SPS::Device::Type::AC, 1, "ControlAC");
    add("screen-main", SPS::Device::Type::SCREEN, 1, "ControlCurtain");

    Logger::instance().info("DeviceMap", QString("Loaded %1 demo device mappings").arg(m_entries.size()));
    return true;
}

const DeviceMapEntry* DeviceMap::find(const QString& deviceId) const {
    auto it = m_entries.find(deviceId);
    if (it != m_entries.end()) {
        return &it.value();
    }
    return nullptr;
}

uchar DeviceMap::channel(const QString& deviceId, bool* ok) const {
    const DeviceMapEntry* entry = find(deviceId);
    if (entry) {
        if (ok) *ok = true;
        return static_cast<uchar>(entry->channel);
    }
    if (ok) *ok = false;
    return 0;
}

QString DeviceMap::method(const QString& deviceId) const {
    const DeviceMapEntry* entry = find(deviceId);
    return entry ? entry->method : QString();
}

bool DeviceMap::contains(const QString& deviceId) const {
    return m_entries.contains(deviceId);
}

int DeviceMap::size() const {
    return m_entries.size();
}

bool DeviceMap::isEmpty() const {
    return m_entries.isEmpty();
}

bool DeviceMap::validateAction(SPS::Device::Type type, SPS::Device::State state) {
    switch (type) {
        case SPS::Device::Type::LIGHT:
        case SPS::Device::Type::RELAY:
        case SPS::Device::Type::PROJECTOR:
        case SPS::Device::Type::AC:
            return state == SPS::Device::State::ON || state == SPS::Device::State::OFF;
        case SPS::Device::Type::CURTAIN:
        case SPS::Device::Type::SCREEN:
            return state == SPS::Device::State::ON || state == SPS::Device::State::OFF ||
                   state == SPS::Device::State::OPEN || state == SPS::Device::State::CLOSED ||
                   state == SPS::Device::State::OPENING || state == SPS::Device::State::CLOSING;
        default:
            return false;
    }
}
