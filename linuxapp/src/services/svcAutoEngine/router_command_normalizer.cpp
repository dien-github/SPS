#include "router_command_normalizer.h"
#include <QJsonDocument>
#include <QStringList>

namespace {

constexpr const char* kControlLight = "ControlLight";
constexpr const char* kControlCurtain = "ControlCurtain";
constexpr const char* kControlProjector = "ControlProjector";
constexpr const char* kControlAc = "ControlAC";
constexpr const char* kIncreaseAcTemperature = "IncreaseACTemperature";
constexpr const char* kDecreaseAcTemperature = "DecreaseACTemperature";

QString key(QString value) {
    value = value.trimmed().toLower();
    value.remove('-');
    value.remove('_');
    value.remove(' ');
    return value;
}

bool parseByte(const QString& value, quint8& out) {
    bool ok = false;
    const int number = value.trimmed().toInt(&ok, 0);
    if (!ok || number < 0 || number > 255) {
        return false;
    }
    out = static_cast<quint8>(number);
    return true;
}

bool isPositiveStateToken(const QString& token) {
    return token == "on" || token == "open" || token == "up" ||
           token == "enable" || token == "enabled" || token == "active" ||
           token == "true" || token == "1" || token == "running" ||
           token == "start" || token == "started" || token == "wake";
}

bool isNegativeStateToken(const QString& token) {
    return token == "off" || token == "close" || token == "closed" ||
           token == "down" || token == "disable" || token == "disabled" ||
           token == "inactive" || token == "false" || token == "0" ||
           token == "stopped";
}

bool isStateToken(const QString& token) {
    return isPositiveStateToken(token) || isNegativeStateToken(token) || token == "stop";
}

bool isPositiveState(SPS::Device::State state) {
    return state == SPS::Device::State::ON ||
           state == SPS::Device::State::OPEN ||
           state == SPS::Device::State::OPENING;
}

bool isNegativeState(SPS::Device::State state) {
    return state == SPS::Device::State::OFF ||
           state == SPS::Device::State::CLOSED ||
           state == SPS::Device::State::CLOSING;
}

bool isComputerAlias(const QString& raw) {
    const QString token = key(raw);
    return token == "computer" || token == "pc" || token == "deskpc";
}

bool isSemanticLightAlias(const QString& raw) {
    const QString token = key(raw);
    return token == "light" || token == "lights" ||
           token == "roomlights" || token == "alllights";
}

bool typesCompatible(SPS::Device::Type expected, SPS::Device::Type actual) {
    if (expected == SPS::Device::Type::UNKNOWN ||
        actual == SPS::Device::Type::UNKNOWN ||
        expected == actual) {
        return true;
    }

    if ((expected == SPS::Device::Type::CURTAIN && actual == SPS::Device::Type::SCREEN) ||
        (expected == SPS::Device::Type::SCREEN && actual == SPS::Device::Type::CURTAIN)) {
        return true;
    }

    if ((expected == SPS::Device::Type::RELAY && actual == SPS::Device::Type::LIGHT) ||
        (expected == SPS::Device::Type::LIGHT && actual == SPS::Device::Type::RELAY)) {
        return true;
    }

    return false;
}

QString typeName(SPS::Device::Type type) {
    switch (type) {
        case SPS::Device::Type::LIGHT: return "light";
        case SPS::Device::Type::CURTAIN: return "curtain";
        case SPS::Device::Type::SCREEN: return "screen";
        case SPS::Device::Type::PROJECTOR: return "projector";
        case SPS::Device::Type::AC: return "ac";
        case SPS::Device::Type::RELAY: return "relay";
        default: return "unknown";
    }
}

QString commandKindName(SPS::AutoEngine::RouterCommandKind kind) {
    switch (kind) {
        case SPS::AutoEngine::RouterCommandKind::Control: return "control";
        case SPS::AutoEngine::RouterCommandKind::AcTemperatureUp: return "temperature_up";
        case SPS::AutoEngine::RouterCommandKind::AcTemperatureDown: return "temperature_down";
    }
    return "unknown";
}

QString stateName(SPS::Device::State state) {
    switch (state) {
        case SPS::Device::State::OFF: return "off";
        case SPS::Device::State::ON: return "on";
        case SPS::Device::State::OPENING: return "opening";
        case SPS::Device::State::CLOSING: return "closing";
        case SPS::Device::State::OPEN: return "open";
        case SPS::Device::State::CLOSED: return "closed";
        default: return "unknown";
    }
}

bool normalizeCommandKind(const QString& raw,
                          SPS::AutoEngine::RouterCommandKind& kind,
                          QString& error) {
    const QString token = key(raw);
    if (token.isEmpty() || token == "0" || token == "control" ||
        token == "power" || token == "state" || token == "set" ||
        token == "setstate" || token == "switch" || isStateToken(token)) {
        kind = SPS::AutoEngine::RouterCommandKind::Control;
        return true;
    }

    if (token == "tempup" || token == "temperatureup" ||
        token == "increase" || token == "increaseactemperature") {
        kind = SPS::AutoEngine::RouterCommandKind::AcTemperatureUp;
        return true;
    }

    if (token == "tempdown" || token == "temperaturedown" ||
        token == "decrease" || token == "decreaseactemperature") {
        kind = SPS::AutoEngine::RouterCommandKind::AcTemperatureDown;
        return true;
    }

    quint8 numeric = 0;
    if (parseByte(raw, numeric)) {
        error = QString("Unsupported command type '%1'; protocol-router typed control maps only legacy type=0").arg(raw);
    } else {
        error = QString("Unsupported command/action '%1'").arg(raw);
    }
    return false;
}

SPS::Device::Type globalLegacyType(quint8 id) {
    switch (id) {
        case 1: return SPS::Device::Type::LIGHT;
        case 2: return SPS::Device::Type::SCREEN;
        case 3: return SPS::Device::Type::CURTAIN;
        case 4: return SPS::Device::Type::PROJECTOR;
        case 5: return SPS::Device::Type::AC;
        default: return SPS::Device::Type::UNKNOWN;
    }
}

quint8 lightIdFromRaw(const QString& rawDevice, const QString& rawChannel) {
    if (isSemanticLightAlias(rawDevice) && rawChannel.trimmed().isEmpty()) {
        return SPS::UART::toByte(SPS::UART::DeviceId::LIGHT_ALL);
    }

    quint8 id = 0;
    if (parseByte(rawChannel, id)) {
        return id;
    }
    if (parseByte(rawDevice, id)) {
        return id;
    }
    return SPS::UART::toByte(SPS::UART::DeviceId::LIGHT_ALL);
}

quint8 curtainIdFromRaw(const QString& rawDevice,
                        const QString& rawChannel,
                        SPS::Device::Type& type) {
    quint8 id = 0;
    if (parseByte(rawChannel, id) || parseByte(rawDevice, id)) {
        if (id == 2) {
            type = SPS::Device::Type::SCREEN;
            return SPS::UART::toByte(SPS::UART::DeviceId::SCREEN);
        }
        if (id == 3 || id == 1) {
            type = SPS::Device::Type::CURTAIN;
            return SPS::UART::toByte(SPS::UART::DeviceId::CURTAIN);
        }
    }

    if (type == SPS::Device::Type::SCREEN) {
        return SPS::UART::toByte(SPS::UART::DeviceId::SCREEN);
    }

    const QString token = key(rawDevice);
    if (token == "screen" || token == "projectionscreen") {
        type = SPS::Device::Type::SCREEN;
        return SPS::UART::toByte(SPS::UART::DeviceId::SCREEN);
    }

    type = SPS::Device::Type::CURTAIN;
    return SPS::UART::toByte(SPS::UART::DeviceId::CURTAIN);
}

quint8 acIdFromRaw(const QString& rawDevice, const QString& rawChannel) {
    quint8 id = 0;
    if (parseByte(rawChannel, id)) {
        return id;
    }
    if (parseByte(rawDevice, id)) {
        return id;
    }
    return SPS::UART::toByte(SPS::UART::DeviceId::AC_ID);
}

bool applyDevice(const ScenarioCommand& command,
                 const DeviceMap& deviceMap,
                 SPS::AutoEngine::NormalizedRouterCommand& normalized,
                 QString& error) {
    const QString rawDevice = command.deviceId.trimmed();
    const QString rawChannel = command.channel.trimmed();
    const DeviceMapEntry* entry = rawDevice.isEmpty() ? nullptr : deviceMap.find(rawDevice);
    if (entry) {
        if (!typesCompatible(command.deviceType, entry->type)) {
            error = QString("Device type mismatch for '%1': scenario type=%2, mapped type=%3")
                .arg(rawDevice, typeName(command.deviceType), typeName(entry->type));
            normalized.normalizedDevice = rawDevice;
            return false;
        }

        normalized.deviceType = entry->type;
        normalized.method = entry->method;
        normalized.deviceId = static_cast<quint8>(entry->channel);
        normalized.hasDeviceId = normalized.method != kControlProjector;
        normalized.normalizedDevice = QString("%1:%2").arg(entry->id).arg(entry->channel);
        return true;
    }

    SPS::Device::Type deviceType = command.deviceType;
    const SPS::Device::Type semanticType = SPS::AutoEngine::parseDeviceType(rawDevice);
    if (semanticType != SPS::Device::Type::UNKNOWN && typesCompatible(deviceType, semanticType)) {
        deviceType = semanticType;
    }

    quint8 numericDevice = 0;
    if (deviceType == SPS::Device::Type::UNKNOWN && parseByte(rawDevice, numericDevice)) {
        deviceType = globalLegacyType(numericDevice);
    }

    if (deviceType == SPS::Device::Type::UNKNOWN && (isComputerAlias(rawDevice) || numericDevice == 6)) {
        normalized.normalizedDevice = "computer";
        error = QString("Device '%1' has no single-device ProtocolRouter control method").arg(rawDevice);
        return false;
    }

    if (deviceType == SPS::Device::Type::UNKNOWN) {
        normalized.normalizedDevice = rawDevice.isEmpty() ? "<missing>" : rawDevice;
        error = QString("Unknown device_id '%1'").arg(rawDevice);
        return false;
    }

    normalized.deviceType = deviceType;
    switch (deviceType) {
        case SPS::Device::Type::LIGHT:
        case SPS::Device::Type::RELAY:
            normalized.method = kControlLight;
            normalized.deviceId = lightIdFromRaw(rawDevice, rawChannel);
            normalized.hasDeviceId = true;
            break;
        case SPS::Device::Type::CURTAIN:
        case SPS::Device::Type::SCREEN:
            normalized.method = kControlCurtain;
            normalized.deviceId = curtainIdFromRaw(rawDevice, rawChannel, normalized.deviceType);
            normalized.hasDeviceId = true;
            break;
        case SPS::Device::Type::PROJECTOR:
            normalized.method = kControlProjector;
            normalized.hasDeviceId = false;
            break;
        case SPS::Device::Type::AC:
            normalized.method = kControlAc;
            normalized.deviceId = acIdFromRaw(rawDevice, rawChannel);
            normalized.hasDeviceId = true;
            break;
        default:
            error = QString("Device type '%1' is not supported by ProtocolRouter control methods")
                .arg(typeName(deviceType));
            return false;
    }

    normalized.normalizedDevice = normalized.hasDeviceId
        ? QString("%1:%2").arg(typeName(normalized.deviceType)).arg(normalized.deviceId)
        : typeName(normalized.deviceType);
    return true;
}

bool applyControlState(const ScenarioCommand& command,
                       SPS::AutoEngine::NormalizedRouterCommand& normalized,
                       QString& error) {
    QString rawState = command.rawState.trimmed();
    if (rawState.isEmpty() && isStateToken(key(command.commandType))) {
        rawState = command.commandType;
    }

    const QString rawStateKey = key(rawState);
    if (normalized.method == kControlCurtain && rawStateKey == "stop") {
        normalized.controlValue = SPS::UART::ControlValue::STOP;
        normalized.normalizedState = "stop";
    } else {
        SPS::Device::State state = command.targetState;
        if (state == SPS::Device::State::UNKNOWN) {
            state = SPS::AutoEngine::parseDeviceState(rawState);
        }

        if (state == SPS::Device::State::UNKNOWN) {
            error = QString("Unknown target state '%1'").arg(rawState);
            normalized.normalizedState = rawState.isEmpty() ? "<missing>" : rawState;
            return false;
        }

        if (!isPositiveState(state) && !isNegativeState(state)) {
            error = QString("State '%1' cannot be sent through ProtocolRouter control methods")
                .arg(stateName(state));
            normalized.normalizedState = stateName(state);
            return false;
        }

        if (normalized.method == kControlCurtain) {
            normalized.controlValue = isPositiveState(state)
                ? SPS::UART::ControlValue::OPEN
                : SPS::UART::ControlValue::CLOSE;
            normalized.normalizedState = isPositiveState(state) ? "open" : "close";
        } else {
            normalized.boolValue = isPositiveState(state);
            normalized.normalizedState = normalized.boolValue ? "on" : "off";
        }
    }

    if (normalized.method == kControlLight) {
        normalized.args = {
            QVariant::fromValue(normalized.deviceId),
            normalized.normalizedState == "on"
        };
    } else if (normalized.method == kControlCurtain) {
        normalized.args = {
            QVariant::fromValue(normalized.deviceId),
            QVariant::fromValue(SPS::UART::toByte(normalized.controlValue))
        };
    } else if (normalized.method == kControlProjector) {
        normalized.args = { normalized.normalizedState == "on" };
    } else if (normalized.method == kControlAc) {
        normalized.args = {
            QVariant::fromValue(normalized.deviceId),
            normalized.normalizedState == "on"
        };
    } else {
        error = QString("Unknown router control method '%1'").arg(normalized.method);
        return false;
    }

    return true;
}

} // namespace

namespace SPS::AutoEngine {

SPS::Device::Type parseDeviceType(QString value, const QString& fallback) {
    QString token = key(value);
    if (token.isEmpty()) {
        token = key(fallback);
    }

    if (token == "light" || token == "lights" || token == "roomlights" || token == "alllights") {
        return SPS::Device::Type::LIGHT;
    }
    if (token == "relay") {
        return SPS::Device::Type::RELAY;
    }
    if (token == "curtain" || token == "curtains") {
        return SPS::Device::Type::CURTAIN;
    }
    if (token == "screen" || token == "projectionscreen") {
        return SPS::Device::Type::SCREEN;
    }
    if (token == "projector") {
        return SPS::Device::Type::PROJECTOR;
    }
    if (token == "ac" || token == "aircon" || token == "airconditioner" || token == "hvac") {
        return SPS::Device::Type::AC;
    }
    return SPS::Device::Type::UNKNOWN;
}

SPS::Device::State parseDeviceState(QString value) {
    const QString token = key(value);
    if (token == "open" || token == "up" || token == "opening") {
        return SPS::Device::State::OPEN;
    }
    if (token == "close" || token == "closed" || token == "down" || token == "closing") {
        return SPS::Device::State::CLOSED;
    }
    if (isPositiveStateToken(token)) {
        return SPS::Device::State::ON;
    }
    if (isNegativeStateToken(token)) {
        return SPS::Device::State::OFF;
    }
    return SPS::Device::State::UNKNOWN;
}

QString compactCommandJson(const QJsonObject& object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString describeRouterCommand(const NormalizedRouterCommand& command) {
    QStringList args;
    for (const QVariant& arg : command.args) {
        if (arg.typeId() == QMetaType::Bool) {
            args << (arg.toBool() ? "true" : "false");
        } else {
            args << arg.toString();
        }
    }
    return QString("%1(%2)").arg(command.method, args.join(", "));
}

bool normalizeRouterCommand(const ScenarioCommand& command,
                            const DeviceMap& deviceMap,
                            NormalizedRouterCommand& normalized,
                            QString& error) {
    normalized = NormalizedRouterCommand();
    error.clear();

    if (!normalizeCommandKind(command.commandType, normalized.kind, error)) {
        normalized.normalizedAction = command.commandType.trimmed();
        return false;
    }
    normalized.normalizedAction = commandKindName(normalized.kind);

    if (!applyDevice(command, deviceMap, normalized, error)) {
        return false;
    }

    if (normalized.kind == RouterCommandKind::AcTemperatureUp ||
        normalized.kind == RouterCommandKind::AcTemperatureDown) {
        if (normalized.deviceType != SPS::Device::Type::AC) {
            error = QString("AC temperature command cannot target device type '%1'")
                .arg(typeName(normalized.deviceType));
            return false;
        }

        normalized.method = normalized.kind == RouterCommandKind::AcTemperatureUp
            ? kIncreaseAcTemperature
            : kDecreaseAcTemperature;
        normalized.args = { QVariant::fromValue(normalized.deviceId) };
        normalized.normalizedState = normalized.kind == RouterCommandKind::AcTemperatureUp
            ? "temperature_up"
            : "temperature_down";
        return true;
    }

    return applyControlState(command, normalized, error);
}

} // namespace SPS::AutoEngine
