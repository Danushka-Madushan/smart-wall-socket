#include "Config.h"

// Preserved Calibrations
float V_CAL = 365.0;
float I_CAL = 52.0;
const float CURRENT_NOISE_FLOOR = 0.04; // 40mA gate

// Unified Device Identity - Static constant for physical QR label printing & LAN pairing
// Compact QR code payload: v1;RELAY_32S_NODE;S3_SEC_9821;lan
// Change here if printing a different device code on your enclosure
const char *DEVICE_ID = "RELAY_32S_NODE";
const char *POP_SECRET = "S3_SEC_9821";
const char *TRANSPORT = "lan";
const int SWITCH_COUNT = 1;
const bool HAS_ENERGY = true;
const size_t MAX_PAYLOAD_SIZE = 1024;

// Hardcoded Wi-Fi credentials (same as ESP8266)
const char *WIFI_SSID = "ESP GATE";
const char *WIFI_PASS = "123123123";

const char *getDeviceId() {
  return DEVICE_ID;
}
