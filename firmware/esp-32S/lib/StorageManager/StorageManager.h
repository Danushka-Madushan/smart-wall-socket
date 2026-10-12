#pragma once
#include <Arduino.h>
#include "Config.h"

class StorageManager {
public:
  static void init();
  static bool isClaimed();
  static const char *getMasterToken();
  static void setClaimed(const char *masterToken);
  static void reset();
  static bool isQuickPowerCycleResetTriggered();
  static void clearQuickPowerCycle();

private:
  static bool claimed;
  static char cachedToken[65];
  static bool quickResetTriggered;
};
