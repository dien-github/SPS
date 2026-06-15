#ifndef CMOCKAUTHSERVICE_H
#define CMOCKAUTHSERVICE_H

#include <QObject>
#include <QDebug>

class cMockAuthService : public QObject
{
    Q_OBJECT
public:
    explicit cMockAuthService(QObject *parent = nullptr) : QObject(parent) {}

signals:
    void vLoginSuccess(QString szUid, QString szTeacherName);

public slots:
    void vSimulateRfidScan();
};

#endif // CMOCKAUTHSERVICE_H
