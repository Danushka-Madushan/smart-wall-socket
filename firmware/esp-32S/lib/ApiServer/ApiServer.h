#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "Config.h"

class ApiServer {
public:
  static void init();
  static void handleClient();

private:
  static WebServer server;

  static void sendResponse(int code, const char *contentType, const String &content);
  static void handleOptions();
  static bool isAuthorized();

  static void handleHeartbeat();
  static void handleInfo();
  static void handleWifiStatus();
  static void handleClaim();
  static void handleVerify();
  static void handleRelayGet();
  static void handleRelayPost();
  static void handleRelay1Post();
  static void handleRelayAllPost();
  static void handleEnergy();
  static void handleUnclaim();
  static void handleNotFound();
};
