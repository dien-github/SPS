#ifndef MCU_ENGINE_H
#define MCU_ENGINE_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include "uart_frame.h"
#include "uart_port.h"

class IMcuEngine : public QObject {
    Q_OBJECT

public:
    explicit IMcuEngine(QObject* parent = nullptr) : QObject(parent) {}
    ~IMcuEngine() override = default;

    virtual bool open(const QString& portName) = 0;
    virtual bool close() = 0;
    virtual bool isOpen() const = 0;
    virtual bool sendFrame(const QByteArray& frameData) = 0;
    virtual QString name() const = 0;
    virtual QString lastError() const = 0;

signals:
    void frameReceived(const UartFrame& frame);
    void errorOccurred(const QString& error);
    void connectionStatusChanged(bool connected);
};

class UartMcuEngine : public IMcuEngine {
    Q_OBJECT

public:
    explicit UartMcuEngine(QObject* parent = nullptr);

    bool open(const QString& portName) override;
    bool close() override;
    bool isOpen() const override;
    bool sendFrame(const QByteArray& frameData) override;
    QString name() const override;
    QString lastError() const override;

    static QStringList getAvailablePorts();

private:
    UartPort* m_uartPort;
    QString m_lastError;
};

class VirtualMcuEngine : public IMcuEngine {
    Q_OBJECT

public:
    explicit VirtualMcuEngine(QObject* parent = nullptr);

    bool open(const QString& portName) override;
    bool close() override;
    bool isOpen() const override;
    bool sendFrame(const QByteArray& frameData) override;
    QString name() const override;
    QString lastError() const override;

private:
    bool m_isOpen;
    QString m_lastError;
};

#endif // MCU_ENGINE_H
