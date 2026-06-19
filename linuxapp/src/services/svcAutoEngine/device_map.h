#ifndef DEVICE_MAP_H
#define DEVICE_MAP_H

#include <QString>
#include <QMap>
#include <QList>
#include "../common/sps_constants.h"

struct DeviceMapEntry {
    QString id;
    SPS::Device::Type type;
    int channel;
    QString method;
};

class DeviceMap {
public:
    bool load(const QString& path);
    bool loadDemo();

    const DeviceMapEntry* find(const QString& deviceId) const;
    uchar channel(const QString& deviceId, bool* ok) const;
    QString method(const QString& deviceId) const;

    bool contains(const QString& deviceId) const;
    int size() const;
    bool isEmpty() const;

    static bool validateAction(SPS::Device::Type type, SPS::Device::State state);

private:
    QMap<QString, DeviceMapEntry> m_entries;
};

#endif // DEVICE_MAP_H
