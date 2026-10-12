#pragma once
#include <Arduino.h>
#include "Config.h"

class RelayController {
public:
  static void init();
  static void setPower(bool powerOn, const char *toastMsg = nullptr);
  static bool isPowerOn();
  static void togglePower();

  // Single-switch API compatibility aliases
  static bool getRelay1() { return isPowerOn(); }
  static void setRelay1(bool state) { setPower(state); }

private:
  static bool powerState;
};
