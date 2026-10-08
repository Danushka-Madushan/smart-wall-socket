#include "Config.h"

// Unified device identifier - change here to update everywhere
const char *DEVICE_ID = "RELAY_8266_NODE";
const char *POP_SECRET = "NODE_SEC_4102";
const char *TRANSPORT = "lan";
const int SWITCH_COUNT = 2;
const bool HAS_ENERGY = false;

// Pre-defined MAC address
const uint8_t FIXED_MAC[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x01};

// Wi-Fi Credentials
const char *WIFI_SSID = "ESP GATE";
const char *WIFI_PASS = "123123123";
