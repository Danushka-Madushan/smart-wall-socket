#pragma once
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include "Config.h"

class NetworkManager
{
public:
  static void init();
  static void setupMDNS();
  static void updateMDNS();
  static void update();

  static bool isConnected();
  static IPAddress getLocalIP();
  static int32_t getRSSI();
  static String getSSID();
  static String getMAC();

private:
  static bool wasConnected;
  static void onMDNSDynamicServiceTxt(const MDNSResponder::hMDNSService hService);
};
