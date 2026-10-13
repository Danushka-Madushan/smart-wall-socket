#pragma once
#include <Arduino.h>
#include "Config.h"

class PowerMonitor {
public:
  static bool init();
  static bool getTelemetry(TelemetryData &out);
  static void setRelayState(bool engaged);

private:
  static TelemetryData sharedData;
  static SemaphoreHandle_t dataMutex;
  static void powerSamplingTask(void *pvParameters);
};
