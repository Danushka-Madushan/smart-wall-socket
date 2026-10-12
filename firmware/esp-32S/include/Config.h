#pragma once
#include <Arduino.h>

// =====================================================================
//  HARDWARE PIN MAPPINGS & BUS CONFIGURATION
// =====================================================================
#define I2C_SDA_PIN     21
#define I2C_SCL_PIN     22

// OLED Display (0.96" 128x64 SSD1306 I2C)
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define OLED_I2C_ADDR   0x3C

// ADS1115 16-Bit ADC (I2C)
#define ADS_I2C_ADDR    0x48

// Relay Output Pin
#define RELAY_PIN       4

// =====================================================================
//  RELAY LOGIC CONFIGURATION (INVERTED HARDWARE)
//  Relay ON (Coil Energized)   = Physical Power CUT OFF
//  Relay OFF (Coil Relaxed)     = Physical Power CONNECTED (ON)
// =====================================================================
#define SOCKET_POWER_ON   LOW   // Coil de-energized -> Socket has power
#define SOCKET_POWER_OFF  HIGH  // Coil energized   -> Socket power cut off

// =====================================================================
//  CALIBRATION CONSTANTS & THRESHOLDS (PRESERVED)
// =====================================================================
extern float V_CAL;
extern float I_CAL;
extern const float CURRENT_NOISE_FLOOR;

// =====================================================================
//  DEVICE & BLE PROVISIONING (PoP) CONFIGURATION
// =====================================================================
extern const char *POP_SECRET;
extern const char *TRANSPORT;
extern const int SWITCH_COUNT;
extern const bool HAS_ENERGY;
extern const size_t MAX_PAYLOAD_SIZE;

// Unified Device ID - static string for label printing & BLE provisioning
extern const char *DEVICE_ID;
const char *getDeviceId();

// =====================================================================
//  SHARED TELEMETRY DATA STRUCTURE
// =====================================================================
struct TelemetryData {
  float vRMS;
  float iRMS;
  float activePower;
  float apparentPower;
  float powerFactor;
  bool  relayEngaged; // true = Socket Powered, false = Socket Cut Off
};
