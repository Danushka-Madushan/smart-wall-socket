#pragma once
#include <Arduino.h>
#include "Config.h"

class RelayController
{
public:
  static void init();
  static void setRelay1(bool state);
  static void setRelay2(bool state);
  static void setBoth(bool state);
  static void setRelays(bool relay1, bool relay2, const char *toastMsg = nullptr);
  static bool getRelay1();
  static bool getRelay2();

private:
  static bool r1State;
  static bool r2State;
};
