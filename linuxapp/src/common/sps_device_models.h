#ifndef SPS_DEVICE_MODELS_H
#define SPS_DEVICE_MODELS_H

#include <QString>
#include <QList>
#include <QDateTime>
#include "sps_constants.h"

/** Device information structure. */
struct Device {
    int id;
    QString name;
    SPS::Device::Type type;
    SPS::Device::State state;
    QDateTime lastUpdated;
    QString info;

    /** Default constructor: initializes with UNKNOWN type and state. */
    Device() : id(-1), type(SPS::Device::Type::UNKNOWN), 
               state(SPS::Device::State::UNKNOWN) {}

    /** Constructor: create a device with given ID, name, and type. */
    Device(int deviceId, const QString& deviceName, SPS::Device::Type deviceType)
        : id(deviceId), name(deviceName), type(deviceType),
          state(SPS::Device::State::UNKNOWN), lastUpdated(QDateTime::currentDateTime()) {}
};

/** Room configuration. */
struct Room {
    QString roomId;
    QString roomName;
    int capacity;
    QList<Device> devices;
    QString location;

    /** Default constructor: initializes with capacity 0. */
    Room() : capacity(0) {}
    /** Constructor: create a room with given ID and name. */
    Room(const QString& id, const QString& name)
        : roomId(id), roomName(name), capacity(0) {}
};

/** Lecturer information (for authentication). */
struct Lecturer {
    QString id;
    QString name;
    QString rfidCard;
    bool authorized;
    QDateTime lastAccess;

    /** Default constructor: initializes as not authorized. */
    Lecturer() : authorized(false) {}
    /** Constructor: create a lecturer with ID, name, and RFID card. */
    Lecturer(const QString& lecturerId, const QString& lecturerName, const QString& rfid)
        : id(lecturerId), name(lecturerName), rfidCard(rfid), authorized(true) {}
};

/** Scenario command: an instruction for a device. */
struct ScenarioCommand {
    int order;
    SPS::Device::Type deviceType;
    QString deviceId;
    SPS::Device::State targetState;
    int delayMs;

    /** Default constructor: initializes with order 0 and no delay. */
    ScenarioCommand() : order(0), delayMs(0) {}

    /** Constructor: create a command with sequence, type, device, target state, and optional delay. */
    ScenarioCommand(int seq, SPS::Device::Type type, const QString& dev, 
                   SPS::Device::State state, int delay = 0)
        : order(seq), deviceType(type), deviceId(dev), targetState(state), delayMs(delay) {}
};

/** Scenario: a sequence of device commands. */
struct Scenario {
    QString id;
    QString name;
    QString description;
    QList<ScenarioCommand> commands;
    bool autoStart;
    int priority;

    /** Default constructor: no auto-start and zero priority. */
    Scenario() : autoStart(false), priority(0) {}
    /** Constructor: create a scenario with given ID and name. */
    Scenario(const QString& scenarioId, const QString& scenarioName)
        : id(scenarioId), name(scenarioName), autoStart(false), priority(0) {}

    /** Add a command to this scenario. */
    bool addCommand(const ScenarioCommand& cmd) {
        commands.append(cmd);
        return true;
    }

    /** Remove all commands from this scenario. */
    void clearCommands() {
        commands.clear();
    }
};

/** UART frame data structure. */
struct UartFrame {
    unsigned char header[2];
    unsigned char length;
    unsigned char cmdId;
    QByteArray payload;
    unsigned short crc16;

    /** Default constructor: initializes header to 0xAA, 0x55. */
    UartFrame() : length(0), cmdId(0), crc16(0) {
        header[0] = 0xAA;
        header[1] = 0x55;
    }

    /** Serialize the frame into a QByteArray. */
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

    /** Check if a byte array is a valid UART frame (minimum size + header bytes). */
    static bool isValid(const QByteArray& data) {
        if (data.size() < 6) return false;
        if (data[0] != 0xAA || data[1] != 0x55) return false;
        return true;
    }
};

/** MQTT message structure. */
struct MqttMessage {
    QString topic;
    QByteArray payload;
    int qos;
    bool retain;

    /** Default constructor: QoS 0, not retained. */
    MqttMessage() : qos(0), retain(false) {}
    /** Constructor: create an MQTT message with topic, payload, QoS, and retain flag. */
    MqttMessage(const QString& t, const QByteArray& p, int q = 0, bool r = false)
        : topic(t), payload(p), qos(q), retain(r) {}
};

#endif // SPS_DEVICE_MODELS_H
