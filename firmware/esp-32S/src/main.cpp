#include <Arduino.h>
#include "Config.h"
#include "StorageManager.h"
#include "PowerMonitor.h"
#include "RelayController.h"
#include "DisplayManager.h"
#include "NetworkManager.h"
#include "ApiServer.h"

// =====================================================================
//  SETUP - Subsystem Initialization & Orchestration
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println(F("\n========================================"));
  Serial.println(F("   Smart Wall Socket - ESP32-S Node     "));
  Serial.println(F("========================================"));

  // 1. Initialize Relay (Inverted Hardware: Glitch-free default ON)
  RelayController::init();

  // 2. Initialize OLED Display & Show Splash
  DisplayManager::init();
  DisplayManager::showSplash();

  // 3. Initialize Persistent NVS Storage & Check Quick Power-Cycle Reset
  StorageManager::init();
  if (StorageManager::isQuickPowerCycleResetTriggered()) {
    Serial.println(F("[RESET] 3 quick power-cycles detected! Factory resetting to unclaimed state..."));
    StorageManager::reset();
    NetworkManager::resetProvisioning();
  }

  // 4. Initialize Core 1 Power Sampling (Synthetic Real Power Engine)
  if (!PowerMonitor::init()) {
    Serial.println(F("[WARNING] Power monitor failed to initialize!"));
  }

  // 5. Initialize Network Subsystem (LAN Wi-Fi with mDNS)
  NetworkManager::init();

  // 6. Initialize Web Server & REST API Routes
  ApiServer::init();

  // 7. Initial Display Refresh
  DisplayManager::update();

  Serial.println(F("[READY] Smart Wall Socket running. Send '1' for Power ON, '0' for Power CUT, 'r' for Reset."));
}

// =====================================================================
//  LOOP - Continuous Core 0 Event Dispatching
// =====================================================================
void loop() {
  // 1. Handle incoming HTTP client requests
  ApiServer::handleClient();

  // 2. Monitor Wi-Fi state & update mDNS services
  NetworkManager::update();

  // 3. Refresh display / headless upkeep
  static unsigned long lastDisplayTick = 0;
  if (millis() - lastDisplayTick >= 250) {
    lastDisplayTick = millis();
    DisplayManager::update();
  }

  // 4. Serial Command Support for Hardware Testing
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == '1') {
      RelayController::setPower(true, "POWER ON");
    } else if (cmd == '0') {
      RelayController::setPower(false, "POWER CUT");
    } else if (cmd == 'r') {
      Serial.println(F("[CMD] Manual reset requested via Serial"));
      StorageManager::reset();
      NetworkManager::resetProvisioning();
    }
  }

  // 5. Clear quick power-cycle reset counter after 8s continuous uptime
  StorageManager::clearQuickPowerCycle();

  // Yield to FreeRTOS Core 0 IDLE task
  delay(10);
}
