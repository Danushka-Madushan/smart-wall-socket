#include "PowerMonitor.h"

TelemetryData PowerMonitor::sharedData = {230.0f, 0.0f, 0.0f, 0.0f, 1.0f, true};
SemaphoreHandle_t PowerMonitor::dataMutex = nullptr;

bool PowerMonitor::init() {
  dataMutex = xSemaphoreCreateMutex();
  if (!dataMutex) {
    Serial.println(F("[POWER] Failed to create dataMutex!"));
    return false;
  }

  // Pin synthetic telemetry generator to Core 1
  xTaskCreatePinnedToCore(
    powerSamplingTask,
    "PowerSamplingTask",
    4096,
    NULL,
    2,
    NULL,
    1 // Pin strictly to Core 1
  );

  Serial.println(F("[POWER] Synthetic power telemetry engine initialized & pinned to Core 1."));
  return true;
}

bool PowerMonitor::getTelemetry(TelemetryData &out) {
  if (!dataMutex) return false;
  if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    out = sharedData;
    xSemaphoreGive(dataMutex);
    return true;
  }
  return false;
}

void PowerMonitor::setRelayState(bool engaged) {
  if (!dataMutex) return;
  if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    sharedData.relayEngaged = engaged;
    xSemaphoreGive(dataMutex);
  }
}

void PowerMonitor::powerSamplingTask(void *pvParameters) {
  float smoothedPower = 325.0f;
  float smoothedVoltage = 230.0f;

  for (;;) {
    bool relayOn = true;
    if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      relayOn = sharedData.relayEngaged;
      xSemaphoreGive(dataMutex);
    }

    float currentV = 230.0f;
    float currentI = 0.0f;
    float currentP = 0.0f;
    float currentVA = 0.0f;
    float currentPF = 1.0f;

    // Mains voltage micro-fluctuation (229.6V to 230.4V)
    float rawV = 230.0f + (float)(random(-4, 5)) * 0.1f;
    smoothedVoltage = (smoothedVoltage * 0.80f) + (rawV * 0.20f);
    currentV = smoothedVoltage;

    if (relayOn) {
      // Float active power between 300.0W and 350.0W with low-pass smoothing
      float targetP = 300.0f + (float)(random(0, 501)) * 0.1f;
      smoothedPower = (smoothedPower * 0.85f) + (targetP * 0.15f);
      currentP = smoothedPower;

      // Realistic Power Factor for smart board electronics (~0.972 to 0.982)
      currentPF = 0.972f + (float)(random(0, 11)) * 0.001f;

      // Derived apparent power & true RMS current: VA = P / PF, I = VA / V
      currentVA = currentP / currentPF;
      currentI = currentVA / currentV;
    } else {
      // Immediate zero-power cutoff when relay is disengaged
      smoothedPower = 325.0f;
      currentP = 0.0f;
      currentI = 0.0f;
      currentVA = 0.0f;
      currentPF = 1.0f;
    }

    // Thread-safe update to shared telemetry
    if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      sharedData.vRMS = currentV;
      sharedData.iRMS = currentI;
      sharedData.activePower = currentP;
      sharedData.apparentPower = currentVA;
      sharedData.powerFactor = currentPF;
      xSemaphoreGive(dataMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(150));
  }
}

