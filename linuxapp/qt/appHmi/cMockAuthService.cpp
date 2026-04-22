#include "cMockAuthService.h"

cMockAuthService::cMockAuthService(QObject *parent)
    : QObject{parent}
{}

cMockAuthService::vSimulateRfidScan() {
    qDebug() << "[HW] Catch a signal from Simulate RFID...";
    emit vLoginSuccess("A1B2C3D4", "TS. Nguyen Minh Son");
}