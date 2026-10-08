#pragma once
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include "Config.h"

class ApiServer
{
public:
  static void init();
  static void handleClient();

private:
  static ESP8266WebServer server;

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
  static void handleRelay2Post();
  static void handleRelayAllPost();
  static void handleUnclaim();
  static void handleNotFound();
};
