#include "RelayController.h"
#include "PowerMonitor.h"
#include "DisplayManager.h"

bool RelayController::powerState = true;

void RelayController::init() {
  // Glitch-suppressed boot: write state BEFORE setting pinMode OUTPUT
  digitalWrite(RELAY_PIN, SOCKET_POWER_ON);
  pinMode(RELAY_PIN, OUTPUT);
  powerState = true;
  PowerMonitor::setRelayState(true);

  Serial.println(F("[RELAY] Initialized (Boot State: Socket Power ON / Coil LOW)"));
}

void RelayController::setPower(bool powerOn, const char *toastMsg) {
  powerState = powerOn;
  digitalWrite(RELAY_PIN, powerOn ? SOCKET_POWER_ON : SOCKET_POWER_OFF);
  PowerMonitor::setRelayState(powerOn);

  if (toastMsg) {
    DisplayManager::showToast(toastMsg);
  } else {
    DisplayManager::update();
  }

  Serial.printf("[RELAY] Socket Power -> %s (GPIO %d = %s)\n",
                powerState ? "ON" : "OFF",
                RELAY_PIN,
                powerState ? "LOW" : "HIGH");
}

bool RelayController::isPowerOn() {
  return powerState;
}

void RelayController::togglePower() {
  setPower(!powerState);
}
