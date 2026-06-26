#include "cMockAuthService.h"

void cMockAuthService::vSimulateRfidScan() {
    qDebug() << "[HW] Catch a signal from Simulate RFID...";
    emit vLoginSuccess("A1B2C3D4", "TS. Nguyen Minh Son");
}