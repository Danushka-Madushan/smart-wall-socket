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

// ======================== CALIBRATION PARAMETERS ========================
// 1. Current Calibration: 30A/1V ratio (tune to 52.2 if your 60W iron measures ~880 ohms)
float I_CAL = 30.0;

// 2. Voltage Calibration: Match against your wall multimeter reading
float V_CAL = 365.0;

// 3. Phase Alignment: Compensates for ADS1115 mux delay and ZMPT101B op-amp phase shift
// Each sample step compensates for ~42 degrees of phase skew.
const int   PHASE_SHIFT_SAMPLES = 2;     // Shifts voltage array by 2 samples (~84 deg)
const bool  INVERT_CT_POLARITY  = true;  // Corrects for tip/sleeve bias swap

// Noise gate threshold
const float CURRENT_NOISE_FLOOR = 0.02; // 20mA threshold

// ======================== SHARED DATA STRUCTURE ========================
struct TelemetryData {
  float vRMS;
  float iRMS;
  float activePower;
  float apparentPower;
  float powerFactor;
  float a0DCBias;
  float a1DCBias;
  bool  relayEngaged;
};

TelemetryData sharedData;
SemaphoreHandle_t dataMutex;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_ADS1115 ads;

// ======================== CORE 1: SAMPLING TASK ========================
void powerSamplingTask(void *pvParameters) {
  const int NUM_SAMPLES = 180; // ~420ms window (approx 21 mains cycles)
  float rawV[NUM_SAMPLES];
  float rawI[NUM_SAMPLES];

  for (;;) {
    float sumV = 0.0;
    float sumI = 0.0;

    // --- Pass 1: Acquire Raw Waveforms ---
    for (int i = 0; i < NUM_SAMPLES; i++) {
      int16_t adc0 = ads.readADC_SingleEnded(0); // A0: Current (SCT-013)
      int16_t adc1 = ads.readADC_SingleEnded(1); // A1: Voltage (ZMPT101B)

      float vVolts = ads.computeVolts(adc1);
      float iVolts = ads.computeVolts(adc0);

      rawV[i] = vVolts;
      rawI[i] = iVolts;

      sumV += vVolts;
      sumI += iVolts;
    }

    // Dynamic DC bias offset extraction
    float vOffset = sumV / NUM_SAMPLES;
    float iOffset = sumI / NUM_SAMPLES;

    // --- Pass 2: Phase-Compensated Math ---
    float sumSqV = 0.0;
    float sumSqI = 0.0;
    float sumP   = 0.0;

    for (int i = 0; i < NUM_SAMPLES; i++) {
      // Align voltage sample with current sample to cancel phase delay
      int vIdx = (i + PHASE_SHIFT_SAMPLES) % NUM_SAMPLES;

      float instV = (rawV[vIdx] - vOffset) * V_CAL;
      float instI = (rawI[i] - iOffset) * I_CAL;

      if (INVERT_CT_POLARITY) {
        instI = -instI;
      }

      sumSqV += (instV * instV);
      sumSqI += (instI * instI);
      sumP   += (instV * instI); // Instantaneous active power
    }

    float calcVRMS = sqrt(sumSqV / NUM_SAMPLES);
    float calcIRMS = sqrt(sumSqI / NUM_SAMPLES);
    float calcP    = sumP / NUM_SAMPLES;

    // Clamp small noise drift
    if (calcIRMS < CURRENT_NOISE_FLOOR) {
      calcIRMS = 0.0;
      calcP    = 0.0;
    }

    float calcS  = calcVRMS * calcIRMS;
    float calcPF = 1.0;

    if (calcS > 0.05) {
      calcPF = calcP / calcS;
      // Constrain power factor bounds
      if (calcPF > 1.0)  calcPF = 1.0;
      if (calcPF < -1.0) calcPF = -1.0;
    }

    // Safety fallback: Real power cannot exceed apparent power
    if (fabs(calcP) > calcS && calcS > 0.0) {
      calcP = calcS;
      calcPF = 1.0;
    }

    // Push metrics safely to Core 0
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      sharedData.vRMS          = calcVRMS;
      sharedData.iRMS          = calcIRMS;
      sharedData.activePower   = fabs(calcP);
      sharedData.apparentPower = calcS;
      sharedData.powerFactor   = fabs(calcPF);
      sharedData.a0DCBias      = iOffset;
      sharedData.a1DCBias      = vOffset;
      xSemaphoreGive(dataMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ======================== OLED DISPLAY ROUTINE ========================
void renderDisplay(const TelemetryData &d) {
  display.clearDisplay();

  // Status Bar
  display.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(3, 2);
  display.print(F("SMART PLUG"));
  display.setCursor(76, 2);
  display.print(d.relayEngaged ? F("RLY: ON") : F("RLY: OFF"));

  display.setTextColor(SSD1306_WHITE);

  // Line 1: Voltage
  display.setCursor(2, 16);
  display.print(F("VAC : "));
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
  display.print(d.activePower, 1);
  display.print(F(" W"));

  // Line 4: Apparent Power & Power Factor
  display.drawLine(0, 51, SCREEN_WIDTH, 51, SSD1306_WHITE);
  display.setCursor(2, 54);
  display.print(F("PF:"));
  display.print(d.powerFactor, 2);
  display.setCursor(50, 54);
  display.print(F("VA:"));
  display.print(d.apparentPower, 0);

  display.setCursor(96, 54);
  display.print(F("C1|C0"));

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
  Wire.setClock(400000); // 400kHz Fast I2C mode

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
}

void loop() {
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

  Serial.printf("[METRICS] VAC: %5.1f V | IAC: %5.2f A | Real PWR: %5.1f W | Apparent: %5.1f VA | PF: %.2f\n",
                snap.vRMS, snap.iRMS, snap.activePower, snap.apparentPower, snap.powerFactor);

  delay(400);
}
