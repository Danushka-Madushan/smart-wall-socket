#include "Config.h"
#include <esp_wifi.h>

// Preserved Calibrations
float V_CAL = 365.0;
float I_CAL = 52.0;
const float CURRENT_NOISE_FLOOR = 0.04; // 40mA gate

// Device Configuration
const char *POP_SECRET = "S3_SEC_9821";
const char *TRANSPORT = "ble";
const int SWITCH_COUNT = 1;
const bool HAS_ENERGY = true;
const size_t MAX_PAYLOAD_SIZE = 1024;

static char deviceIdBuffer[32] = {0};

const char *getDeviceId() {
  if (deviceIdBuffer[0] == '\0') {
    uint8_t mac[6] = {0};
    esp_err_t ret = esp_wifi_get_mac((wifi_interface_t)WIFI_IF_STA, mac);
    if (ret != ESP_OK) {
      esp_read_mac(mac, ESP_MAC_WIFI_STA);
    }
    snprintf(deviceIdBuffer, sizeof(deviceIdBuffer), "PROV_%02X%02X%02X", mac[3], mac[4], mac[5]);
  }
  return deviceIdBuffer;
}
