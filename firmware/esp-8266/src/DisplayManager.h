#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Config.h"

class DisplayManager
{
public:
  static void init();
  static void showSplash();
  static void showToast(const char *msg, unsigned long durationMs = 2500);
  static void update();
  static bool isToastActive();
  static bool hasDisplayHardware();

private:
  static Adafruit_SSD1306 display;
  static bool hasDisplay;
  static char currentToast[32];
  static unsigned long toastStartTime;
  static unsigned long toastDuration;
  static unsigned long lastDisplayTick;
};
