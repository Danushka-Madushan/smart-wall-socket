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

// Relay logic (active-LOW)
#define RELAY_STATE_ON  LOW
#define RELAY_STATE_OFF HIGH

// ======================== SENSOR CALIBRATION ========================
// 1. SCT-013-030: Rated 30A RMS input -> 1.0V RMS output (30A/V ratio)
const float I_CAL = 30.0;

// 2. ZMPT101B: Adjust this constant to match your digital multimeter reading
// Formula: V_CAL = (Actual Multimeter Voltage) / (Measured Sensor RMS Voltage)
float V_CAL = 365.0;

// Noise gate threshold: Suppress ghost wattage when no load is attached
const float CURRENT_NOISE_FLOOR = 0.05; // 50mA

// ======================== SHARED DATA STRUCTURE ========================
struct TelemetryData {
  float vRMS;
  float iRMS;
  float activePower;
  float apparentPower;
  float powerFactor;
  float a0DCBias;
  float a1DCBias;
  float vPeakToPeak;
  bool  relayEngaged;
  uint32_t sampleCount;
};

TelemetryData sharedData;
SemaphoreHandle_t dataMutex;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_ADS1115 ads;

// ======================== CORE 1: SAMPLING TASK ========================
// Pinned to Core 1: runs without interruption from Wi-Fi or screen rendering
void powerSamplingTask(void *pvParameters) {
  const int SAMPLES_PER_CYCLE = 180; // ~400-500ms sampling window (20-25 AC cycles)
  float rawV[SAMPLES_PER_CYCLE];
  float rawI[SAMPLES_PER_CYCLE];

  for (;;) {
    float sumV = 0.0;
    float sumI = 0.0;
    float minV = 5.0, maxV = 0.0;

    // --- Pass 1: High-Speed Burst Sampling ---
    for (int i = 0; i < SAMPLES_PER_CYCLE; i++) {
      // Single-ended conversion on ADS1115
      int16_t adc0 = ads.readADC_SingleEnded(0); // A0: Current (SCT-013)
      int16_t adc1 = ads.readADC_SingleEnded(1); // A1: Voltage (ZMPT101B)

      float vVolts = ads.computeVolts(adc1);
      float iVolts = ads.computeVolts(adc0);

      rawV[i] = vVolts;
      rawI[i] = iVolts;

      sumV += vVolts;
      sumI += iVolts;

      if (vVolts < minV) minV = vVolts;
      if (vVolts > maxV) maxV = vVolts;
    }

    // Dynamic DC bias removal
    float vOffset = sumV / SAMPLES_PER_CYCLE;
    float iOffset = sumI / SAMPLES_PER_CYCLE;

    // --- Pass 2: RMS and Instantaneous Power Math ---
    float sumSqV = 0.0;
    float sumSqI = 0.0;
    float sumP   = 0.0;

    for (int i = 0; i < SAMPLES_PER_CYCLE; i++) {
      // Remove DC offset and scale to physical engineering units
      float instV = (rawV[i] - vOffset) * V_CAL;
      float instI = (rawI[i] - iOffset) * I_CAL;

      sumSqV += (instV * instV);
      sumSqI += (instI * instI);
      sumP   += (instV * instI); // Instantaneous real power
    }

    float calcVRMS = sqrt(sumSqV / SAMPLES_PER_CYCLE);
    float calcIRMS = sqrt(sumSqI / SAMPLES_PER_CYCLE);
    float calcP    = fabs(sumP / SAMPLES_PER_CYCLE); // Active Power

    // Apply low-load noise floor
    if (calcIRMS < CURRENT_NOISE_FLOOR) {
      calcIRMS = 0.0;
      calcP    = 0.0;
    }

    float calcS  = calcVRMS * calcIRMS;               // Apparent Power (VA)
    float calcPF = (calcS > 0.0) ? (calcP / calcS) : 1.0;
    if (calcPF > 1.0) calcPF = 1.0;

    // Update shared memory safely using FreeRTOS Mutex
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      sharedData.vRMS          = calcVRMS;
      sharedData.iRMS          = calcIRMS;
      sharedData.activePower   = calcP;
      sharedData.apparentPower = calcS;
      sharedData.powerFactor   = calcPF;
      sharedData.a0DCBias      = iOffset;
      sharedData.a1DCBias      = vOffset;
      sharedData.vPeakToPeak   = (maxV - minV);
      sharedData.sampleCount   = SAMPLES_PER_CYCLE;
      xSemaphoreGive(dataMutex);
    }

    // Brief yield to prevent watchdog timeout on Core 1
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ======================== OLED DISPLAY ROUTINE ========================
void renderDisplay(const TelemetryData &d) {
  display.clearDisplay();

  // Top Status Bar (Inverted header)
  display.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(3, 2);
  display.print(F("SMART PLUG"));
  display.setCursor(72, 2);
  display.print(d.relayEngaged ? F("[RELAY ON]") : F("[RLY OFF]"));

  display.setTextColor(SSD1306_WHITE);

  // Line 1: Voltage
  display.setCursor(2, 16);
  display.print(F("VAC : "));
  display.setTextSize(1);
  display.print(d.vRMS, 1);
  display.print(F(" V"));

  // Line 2: Current
  display.setCursor(2, 28);
  display.print(F("IAC : "));
  display.print(d.iRMS, 2);
  display.print(F(" A"));

  // Line 3: Active Power
  display.setCursor(2, 40);
  display.print(F("PWR : "));
  display.setTextSize(1);
  display.print(d.activePower, 1);
  display.print(F(" W"));

  // Line 4: Power Factor & Apparent Power
  display.drawLine(0, 51, SCREEN_WIDTH, 51, SSD1306_WHITE);
  display.setCursor(2, 54);
  display.print(F("PF:"));
  display.print(d.powerFactor, 2);
  display.setCursor(56, 54);
  display.print(F("VA:"));
  display.print(d.apparentPower, 0);

  // Core Indicator
  display.setCursor(102, 54);
  display.print(F("C1|C0"));

  display.display();
}

// ======================== CORE 0: SETUP & DISPATCH ========================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n=========================================="));
  Serial.println(F(" ESP32 DUAL-CORE SMART PLUG TELEMETRY     "));
  Serial.println(F("=========================================="));

  // 1. Relay GPIO Configuration
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_STATE_ON); // Default ON to power the load socket
  sharedData.relayEngaged = true;

  // 2. I2C Bus & Screen Init
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println(F("[ERROR] SSD1306 not found at 0x3C!"));
    while (1) delay(500);
  }

  // 3. ADS1115 Init
  if (!ads.begin(ADS_I2C_ADDR)) {
    Serial.println(F("[ERROR] ADS1115 not found at 0x48!"));
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 20);
    display.println(F("ADS1115 NOT FOUND!"));
    display.display();
    while (1) delay(500);
  }

  // Set gain to GAIN_ONE (+/- 4.096V range, 1 LSB = 0.125mV)
  ads.setGain(GAIN_ONE);
  ads.setDataRate(RATE_ADS1115_860SPS);

  // 4. Create Mutex for thread-safe memory exchange
  dataMutex = xSemaphoreCreateMutex();

  // 5. Pin Sampling Task to Core 1
  xTaskCreatePinnedToCore(
    powerSamplingTask,   // Task function
    "PowerSamplingTask", // Task name
    4096,                // Stack size in words
    NULL,                // Parameters
    2,                   // Priority (High)
    NULL,                // Task handle
    1                    // Core ID (Core 1)
  );

  Serial.println(F("[OK] Sampling Task successfully allocated to Core 1."));
  Serial.println(F("[INFO] Send '1' to turn Relay ON, '0' to turn Relay OFF."));
}

void loop() {
  // --- 1. Serial Command Handler (Relay Control) ---
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == '1') {
      digitalWrite(RELAY_PIN, RELAY_STATE_ON);
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        sharedData.relayEngaged = true;
        xSemaphoreGive(dataMutex);
      }
      Serial.println(F(">>> RELAY COMMAND: ENERGIZED (ON)"));
    } else if (cmd == '0') {
      digitalWrite(RELAY_PIN, RELAY_STATE_OFF);
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        sharedData.relayEngaged = false;
        xSemaphoreGive(dataMutex);
      }
      Serial.println(F(">>> RELAY COMMAND: DE-ENERGIZED (OFF)"));
    }
  }

  // --- 2. Copy Telemetry Snapshot ---
  TelemetryData snap;
  if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    snap = sharedData;
    xSemaphoreGive(dataMutex);
  }

  // --- 3. Refresh OLED on Core 0 ---
  renderDisplay(snap);

  // --- 4. Print Full Diagnostics to Serial Monitor ---
  Serial.printf("[METRICS] V: %5.1f V | I: %5.2f A | P: %6.1f W | VA: %6.1f | PF: %.2f | Vpp: %.2fV | A0_Bias: %.3fV | A1_Bias: %.3fV | RLY: %s\n",
                snap.vRMS, snap.iRMS, snap.activePower, snap.apparentPower, snap.powerFactor,
                snap.vPeakToPeak, snap.a0DCBias, snap.a1DCBias,
                snap.relayEngaged ? "ON" : "OFF");

  delay(400); // 2.5 Hz UI refresh rate
}
