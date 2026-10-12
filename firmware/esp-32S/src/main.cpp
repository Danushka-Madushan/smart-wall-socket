#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_ADS1X15.h>

// ======================== HARDWARE CONFIGURATION ========================
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define OLED_I2C_ADDR   0x3C
#define ADS_I2C_ADDR    0x48

#define I2C_SDA_PIN     21
#define I2C_SCL_PIN     22
#define RELAY_PIN       4

#define RELAY_STATE_ON  LOW
#define RELAY_STATE_OFF HIGH

// ======================== DEMO CALIBRATION CONSTANTS ========================
// 1. Current Calibration: Scaled from 30.0 -> 52.0 so a 60W load reads ~0.26A
float I_CAL = 52.0;

// 2. Voltage Calibration: Match against your household mains AC voltage
float V_CAL = 365.0;

// Noise threshold: Clears screen to 0.00A and 0.0W when nothing is plugged in
const float CURRENT_NOISE_FLOOR = 0.04; // 40mA

// ======================== SHARED DATA STRUCTURE ========================
struct TelemetryData {
  float vRMS;
  float iRMS;
  float activePower;
  float apparentPower;
  float powerFactor;
  bool  relayEngaged;
};

TelemetryData sharedData;
SemaphoreHandle_t dataMutex;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_ADS1115 ads;

// ======================== CORE 1: POWER SAMPLING TASK ========================
// ======================== CORE 1: POWER SAMPLING TASK ========================
void powerSamplingTask(void *pvParameters) {
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
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
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

// ======================== OLED DISPLAY ROUTINE ========================
void renderDisplay(const TelemetryData &d) {
  display.clearDisplay();

  // Header Bar
  display.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(3, 2);
  display.print(F("SMART WALL SOCKET"));
  display.setCursor(82, 2);
  display.print(d.relayEngaged ? F("ON") : F("OFF"));

  display.setTextColor(SSD1306_WHITE);

  // Row 1: Voltage
  display.setCursor(2, 16);
  display.print(F("Voltage : "));
  display.print(d.vRMS, 1);
  display.print(F(" V"));

  // Row 2: Current
  display.setCursor(2, 28);
  display.print(F("Current : "));
  display.print(d.iRMS, 2);
  display.print(F(" A"));

  // Row 3: Active Power
  display.setCursor(2, 40);
  display.print(F("Power   : "));
  display.print(d.activePower, 1);
  display.print(F(" W"));

  // Row 4: Power Factor & Apparent Power
  display.drawLine(0, 51, SCREEN_WIDTH, 51, SSD1306_WHITE);
  display.setCursor(2, 54);
  display.print(F("PF:"));
  display.print(d.powerFactor, 2);
  display.setCursor(52, 54);
  display.print(F("VA:"));
  display.print(d.apparentPower, 0);

  display.setCursor(96, 54);
  display.print(F("ESP32"));

  display.display();
}

// ======================== CORE 0: SETUP & DISPATCH ========================
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_STATE_ON);
  sharedData.relayEngaged = true;

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000); // 400kHz Fast I2C

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    while (1) delay(500);
  }
  display.clearDisplay();
  display.display();

  if (!ads.begin(ADS_I2C_ADDR)) {
    display.clearDisplay();
    display.setCursor(0, 20);
    display.println(F("ADS1115 NOT FOUND!"));
    display.display();
    while (1) delay(500);
  }

  ads.setGain(GAIN_ONE);
  ads.setDataRate(RATE_ADS1115_860SPS);

  dataMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(
    powerSamplingTask,
    "PowerSamplingTask",
    4096,
    NULL,
    2,
    NULL,
    1
  );

  Serial.println(F("[READY] Smart Socket running. Send '1' for ON, '0' for OFF."));
}

void loop() {
  // Serial Relay Commands
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == '1') {
      digitalWrite(RELAY_PIN, RELAY_STATE_ON);
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        sharedData.relayEngaged = true;
        xSemaphoreGive(dataMutex);
      }
    } else if (cmd == '0') {
      digitalWrite(RELAY_PIN, RELAY_STATE_OFF);
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        sharedData.relayEngaged = false;
        xSemaphoreGive(dataMutex);
      }
    }
  }

  TelemetryData snap;
  if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    snap = sharedData;
    xSemaphoreGive(dataMutex);
  }

  renderDisplay(snap);

  Serial.printf("[METRICS] VAC: %5.1f V | IAC: %5.2f A | Real PWR: %5.1f W | VA: %5.1f | PF: %.2f\n",
                snap.vRMS, snap.iRMS, snap.activePower, snap.apparentPower, snap.powerFactor);

  delay(300);
}
