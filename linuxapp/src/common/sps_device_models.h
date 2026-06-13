#ifndef SPS_DEVICE_MODELS_H
#define SPS_DEVICE_MODELS_H

#include <QString>
#include <QList>
#include <QDateTime>
#include "sps_constants.h"

// Device information structure
struct Device {
    int id;
    QString name;
    SPS::Device::Type type;
    SPS::Device::State state;
    QDateTime lastUpdated;
    QString info;  // Additional device-specific info

    Device() : id(-1), type(SPS::Device::Type::UNKNOWN), 
               state(SPS::Device::State::UNKNOWN) {}

    Device(int deviceId, const QString& deviceName, SPS::Device::Type deviceType)
        : id(deviceId), name(deviceName), type(deviceType),
          state(SPS::Device::State::UNKNOWN), lastUpdated(QDateTime::currentDateTime()) {}
};

// Room configuration
struct Room {
    QString roomId;
    QString roomName;
    int capacity;
    QList<Device> devices;
    QString location;

    Room() : capacity(0) {}
    Room(const QString& id, const QString& name)
        : roomId(id), roomName(name), capacity(0) {}
};

// Lecturer information (for authentication)
struct Lecturer {
    QString id;
    QString name;
    QString rfidCard;
    bool authorized;
    QDateTime lastAccess;

    Lecturer() : authorized(false) {}
    Lecturer(const QString& lecturerId, const QString& lecturerName, const QString& rfid)
        : id(lecturerId), name(lecturerName), rfidCard(rfid), authorized(true) {}
};

// Scenario command (instruction for devices)
struct ScenarioCommand {
    int order;
    SPS::Device::Type deviceType;
    QString deviceId;
    SPS::Device::State targetState;
    int delayMs;  // Delay before executing this command

    ScenarioCommand() : order(0), delayMs(0) {}

    ScenarioCommand(int seq, SPS::Device::Type type, const QString& dev, 
                   SPS::Device::State state, int delay = 0)
        : order(seq), deviceType(type), deviceId(dev), targetState(state), delayMs(delay) {}
};

// Scenario (sequence of commands)
struct Scenario {
    QString id;
    QString name;
    QString description;
    QList<ScenarioCommand> commands;
    bool autoStart;
    int priority;

    Scenario() : autoStart(false), priority(0) {}
    Scenario(const QString& scenarioId, const QString& scenarioName)
        : id(scenarioId), name(scenarioName), autoStart(false), priority(0) {}

    bool addCommand(const ScenarioCommand& cmd) {
        commands.append(cmd);
        return true;
    }

    void clearCommands() {
        commands.clear();
    }
};

// UART Frame data
struct UartFrame {
    unsigned char header[2];      // 0xAA, 0x55
    unsigned char length;
    unsigned char cmdId;
    QByteArray payload;
    unsigned short crc16;

    UartFrame() : length(0), cmdId(0), crc16(0) {
        header[0] = 0xAA;
        header[1] = 0x55;
    }

    QByteArray toByteArray() const {
        QByteArray frame;
        frame.append(header[0]);
        frame.append(header[1]);
        frame.append(length);
        frame.append(cmdId);
        frame.append(payload);
        frame.append(static_cast<char>((crc16 >> 8) & 0xFF));
        frame.append(static_cast<char>(crc16 & 0xFF));
        return frame;
    }

    static bool isValid(const QByteArray& data) {
        if (data.size() < 6) return false;  // Min frame size
        if (data[0] != 0xAA || data[1] != 0x55) return false;
        // CRC validation will be done in protocol handler
        return true;
    }
};

// MQTT message
struct MqttMessage {
    QString topic;
    QByteArray payload;
    int qos;
    bool retain;

    MqttMessage() : qos(0), retain(false) {}
    MqttMessage(const QString& t, const QByteArray& p, int q = 0, bool r = false)
        : topic(t), payload(p), qos(q), retain(r) {}
};

#endif // SPS_DEVICE_MODELS_H
