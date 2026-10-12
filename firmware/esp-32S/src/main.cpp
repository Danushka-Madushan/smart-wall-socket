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

  // 3. Initialize Persistent NVS Storage
  StorageManager::init();

  // 4. Initialize Core 1 Power Sampling (ADS1115 True-RMS Task)
  if (!PowerMonitor::init()) {
    Serial.println(F("[WARNING] ADS1115 power monitor failed to initialize!"));
  }

  // 5. Initialize Network Subsystem (WiFiProv over BLE with PoP)
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

  // 3. Refresh OLED display (telemetry or BLE provisioning state)
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

  // Yield to FreeRTOS Core 0 IDLE task
  delay(10);
}
