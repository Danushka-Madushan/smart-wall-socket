#include <Arduino.h>
#include "Config.h"
#include "Storage.h"
#include "RelayController.h"
#include "DisplayManager.h"
#include "NetworkManager.h"
#include "ApiServer.h"

// =====================================================================
//  SETUP - Hardware & Subsystem Orchestration
// =====================================================================
void setup()
{
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n========================================"));
  Serial.println(F("   Smart Wall Socket - Relay Node"));
  Serial.println(F("========================================"));

  // 1. Initialize Relays (glitch-suppressed safe boot default: OFF)
  RelayController::init();

  // 2. Initialize OLED Display and show boot splash screen
  DisplayManager::init();
  DisplayManager::showSplash();

  // 3. Initialize EEPROM & load configuration with checksum integrity
  StorageManager::init();

  // 4. Initialize Wi-Fi connection and mDNS discovery
  NetworkManager::init();

  // 5. Initialize Web Server and REST API routes
  ApiServer::init();

  // 6. Refresh Display with initial operational state
  DisplayManager::update();
}

// =====================================================================
//  LOOP - Continuous Event Dispatching
// =====================================================================
void loop()
{
  // 1. Handle incoming HTTP client requests
  ApiServer::handleClient();

  // 2. Update mDNS services and monitor Wi-Fi auto-reconnection
  NetworkManager::update();

  // 3. Refresh OLED display (status badges, toast expiration)
  DisplayManager::update();
}
