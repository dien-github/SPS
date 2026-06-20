#ifndef ROUTER_COMMAND_NORMALIZER_H
#define ROUTER_COMMAND_NORMALIZER_H

#include <QJsonObject>
#include <QString>
#include <QVariant>
#include "../common/sps_device_models.h"
#include "device_map.h"

namespace SPS::AutoEngine {

enum class RouterCommandKind {
    Control,
    AcTemperatureUp,
    AcTemperatureDown
};

struct NormalizedRouterCommand {
    QString method;
    QVariantList args;
    QString normalizedDevice;
    QString normalizedAction;
    QString normalizedState;
    SPS::Device::Type deviceType = SPS::Device::Type::UNKNOWN;
    RouterCommandKind kind = RouterCommandKind::Control;
    quint8 deviceId = 0;
    bool hasDeviceId = false;
    bool boolValue = false;
    SPS::UART::ControlValue controlValue = SPS::UART::ControlValue::OFF;
};

SPS::Device::Type parseDeviceType(QString value, const QString& fallback = QString());
SPS::Device::State parseDeviceState(QString value);
QString compactCommandJson(const QJsonObject& object);
QString describeRouterCommand(const NormalizedRouterCommand& command);

bool normalizeRouterCommand(const ScenarioCommand& command,
                            const DeviceMap& deviceMap,
                            NormalizedRouterCommand& normalized,
                            QString& error);

} // namespace SPS::AutoEngine

#endif // ROUTER_COMMAND_NORMALIZER_H
