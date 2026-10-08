#pragma once
#include <Arduino.h>

// =====================================================================
//  HARDWARE PIN MAPPINGS
// =====================================================================
// Relays connected to NPN transistor bases (C945) via 1k resistors
// HIGH = Active (ON), LOW = Inactive (OFF)
#define RELAY1_PIN D5 // GPIO14 (Switch 1)
#define RELAY2_PIN D6 // GPIO12 (Switch 2)

// OLED Configuration (0.91" 128x32 I2C Display)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C // Standard I2C address for SSD1306

// Explicit I2C pins on NodeMCU
#define OLED_SDA D2 // GPIO4
#define OLED_SCL D1 // GPIO5

// =====================================================================
//  DEVICE & PROOF OF POSSESSION (PoP) CONFIGURATION
// =====================================================================
// Unified device identifier - updating this updates hostname, mDNS, splash, and API
extern const char *DEVICE_ID;
extern const char *POP_SECRET;
extern const char *TRANSPORT;
extern const int SWITCH_COUNT;
extern const bool HAS_ENERGY;

// Pre-defined MAC address
extern const uint8_t FIXED_MAC[6];

// Wi-Fi Credentials
extern const char *WIFI_SSID;
extern const char *WIFI_PASS;

// Maximum HTTP payload allowed (protects RAM from heap exhaustion)
const size_t MAX_PAYLOAD_SIZE = 1024;

// =====================================================================
//  EEPROM STORAGE CONFIGURATION
// =====================================================================
#define EEPROM_SIZE 512
#define EEPROM_MAGIC 0x506F5031 // "PoP1"
