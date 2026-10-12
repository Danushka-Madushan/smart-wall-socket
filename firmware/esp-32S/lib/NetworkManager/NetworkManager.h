#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "Config.h"

class NetworkManager {
public:
  static void init();
  static void setupMDNS();
  static void updateMDNS();
  static void update();
  static void resetProvisioning();

  static bool isConnected();
  static IPAddress getLocalIP();
  static int32_t getRSSI();
  static String getSSID();
  static String getMAC();
  static bool isProvisioningActive();

private:
  static bool wasConnected;
  static bool isProvisioning;
  static unsigned long bootTime;
  static void onSysProvEvent(arduino_event_t *sys_event);
};
