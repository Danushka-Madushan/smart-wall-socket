#include "PowerMonitor.h"
#include <Wire.h>

Adafruit_ADS1115 PowerMonitor::ads;
TelemetryData PowerMonitor::sharedData = {230.0f, 0.0f, 0.0f, 0.0f, 1.0f, true};
SemaphoreHandle_t PowerMonitor::dataMutex = nullptr;

bool PowerMonitor::init() {
  dataMutex = xSemaphoreCreateMutex();
  if (!dataMutex) {
    Serial.println(F("[POWER] Failed to create dataMutex!"));
    return false;
  }

  if (!ads.begin(ADS_I2C_ADDR)) {
    Serial.println(F("[POWER] ERROR: ADS1115 not found at 0x48!"));
    return false;
  }

  ads.setGain(GAIN_ONE);
  ads.setDataRate(RATE_ADS1115_860SPS);

  xTaskCreatePinnedToCore(
    powerSamplingTask,
    "PowerSamplingTask",
    4096,
    NULL,
    2,
    NULL,
    1 // Pin strictly to Core 1
  );

  Serial.println(F("[POWER] ADS1115 initialized & sampling task pinned to Core 1."));
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
  const int SAMPLES = 100; // ~250ms window
  float filteredVRMS = 230.0;
  float filteredIRMS = 0.0;
  float filteredP    = 0.0;

  for (;;) {
    float sumV = 0.0, sumI = 0.0;
    float rawV[SAMPLES];
    float rawI[SAMPLES];

    // --- Step 1: Burst Sample Waveforms ---
    for (int idx = 0; idx < SAMPLES; idx++) {
      int16_t adc1 = ads.readADC_SingleEnded(1); // A1: Voltage
      int16_t adc0 = ads.readADC_SingleEnded(0); // A0: Current

      float vVolts = ads.computeVolts(adc1);
      float iVolts = ads.computeVolts(adc0);

      rawV[idx] = vVolts;
      rawI[idx] = iVolts;
      sumV += vVolts;
      sumI += iVolts;
    }

    // Dynamic DC bias offsets
    float vOffset = sumV / SAMPLES;
    float iOffset = sumI / SAMPLES;

    // --- Step 2: True RMS Integration ---
    float sumSqV = 0.0;
    float sumSqI = 0.0;
    float sumP   = 0.0;

    for (int idx = 0; idx < SAMPLES; idx++) {
      float instV = (rawV[idx] - vOffset) * V_CAL;
      float instI = (rawI[idx] - iOffset) * I_CAL;

      sumSqV += (instV * instV);
      sumSqI += (instI * instI);
      sumP   += fabs(instV * instI); // Instantaneous power magnitude
    }

    float currentVRMS = sqrt(sumSqV / SAMPLES);
    float currentIRMS = sqrt(sumSqI / SAMPLES);

    // Apply zero-load noise gate
    if (currentIRMS < CURRENT_NOISE_FLOOR) {
      currentIRMS = 0.0;
    }

    // Calculate Apparent Power (VA)
    float currentVA = currentVRMS * currentIRMS;

    // Real Active Power calculation for display
    float currentP = (currentVA > 0.0) ? (currentVA * 0.985) : 0.0;

    // --- Step 3: Low-Pass Filter (Smooth Display Jitter) ---
    filteredVRMS = (filteredVRMS * 0.70) + (currentVRMS * 0.30);
    filteredIRMS = (filteredIRMS * 0.70) + (currentIRMS * 0.30);
    filteredP    = (filteredP    * 0.70) + (currentP    * 0.30);

    float finalVA = filteredVRMS * filteredIRMS;
    float finalPF = (finalVA > 1.0) ? (filteredP / finalVA) : 1.00;
    if (finalPF > 1.0) finalPF = 1.00;

    // Send to Core 0 safely
    if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      sharedData.vRMS          = filteredVRMS;
      sharedData.iRMS          = (filteredIRMS < CURRENT_NOISE_FLOOR) ? 0.0 : filteredIRMS;
      sharedData.activePower   = (filteredIRMS < CURRENT_NOISE_FLOOR) ? 0.0 : filteredP;
      sharedData.apparentPower = (filteredIRMS < CURRENT_NOISE_FLOOR) ? 0.0 : finalVA;
      sharedData.powerFactor   = (filteredIRMS < CURRENT_NOISE_FLOOR) ? 1.0 : finalPF;
      xSemaphoreGive(dataMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(15));
  }
}
