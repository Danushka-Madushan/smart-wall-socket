#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Config.h"

enum DisplayMode {
  DISP_MODE_SPLASH,
  DISP_MODE_PROVISIONING,
  DISP_MODE_OPERATIONAL
};

class DisplayManager {
public:
  static void init();
  static void showSplash();
  static void showToast(const char *msg, unsigned long durationMs = 2500);
  static void setMode(DisplayMode mode);
  static void setProvStatus(const char *status);
  static void update();
  static bool isToastActive();
  static bool hasDisplayHardware();

private:
  static Adafruit_SSD1306 display;
  static bool hasDisplay;
  static DisplayMode currentMode;
  static char provStatusMsg[32];
  static char currentToast[32];
  static unsigned long toastStartTime;
  static unsigned long toastDuration;
  static unsigned long lastUpdateTick;

  static void renderProvisioning();
  static void renderOperational(const TelemetryData &d);
  static void renderToast();
};
