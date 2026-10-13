#include "DisplayManager.h"
#include "StorageManager.h"
#include "PowerMonitor.h"
#include <WiFi.h>

Adafruit_SSD1306 DisplayManager::display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool DisplayManager::hasDisplay = false;
DisplayMode DisplayManager::currentMode = DISP_MODE_SPLASH;
char DisplayManager::provStatusMsg[32] = "Waiting for app...";
char DisplayManager::currentToast[32] = "";
unsigned long DisplayManager::toastStartTime = 0;
unsigned long DisplayManager::toastDuration = 0;
unsigned long DisplayManager::lastUpdateTick = 0;

void DisplayManager::init() {
  hasDisplay = false;
  Serial.println(F("[DISPLAY] Headless mode: OLED disabled due to hardware bus fault."));
}

void DisplayManager::showSplash() {
  if (!hasDisplay) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(14, 6);
  display.println(F("SMART WALL SOCKET"));

  display.drawFastHLine(0, 18, SCREEN_WIDTH, SSD1306_WHITE);

  display.setCursor(30, 26);
  display.println(F("ESP32-S EDITION"));

  const char *id = getDeviceId();
  int16_t xPos = (SCREEN_WIDTH - (strlen(id) * 6)) / 2;
  if (xPos < 0) xPos = 0;
  display.setCursor(xPos, 42);
  display.println(id);

  display.setCursor(22, 54);
  display.println(F("BLE Provisioning"));

  display.display();
  delay(1500);
}

void DisplayManager::showToast(const char *msg, unsigned long durationMs) {
  strncpy(currentToast, msg, sizeof(currentToast) - 1);
  currentToast[sizeof(currentToast) - 1] = '\0';
  toastStartTime = millis();
  toastDuration = durationMs;
  update();
}

bool DisplayManager::isToastActive() {
  return (strlen(currentToast) > 0 && (millis() - toastStartTime < toastDuration));
}

bool DisplayManager::hasDisplayHardware() {
  return hasDisplay;
}

void DisplayManager::setMode(DisplayMode mode) {
  currentMode = mode;
  update();
}

void DisplayManager::setProvStatus(const char *status) {
  strncpy(provStatusMsg, status, sizeof(provStatusMsg) - 1);
  provStatusMsg[sizeof(provStatusMsg) - 1] = '\0';
  update();
}

void DisplayManager::renderProvisioning() {
  display.clearDisplay();

  // Header Banner
  display.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(8, 2);
  display.print(F("BLE PAIRING ACTIVE"));

  display.setTextColor(SSD1306_WHITE);

  // Row 1: Device Name
  display.setCursor(2, 16);
  display.print(F("Device: "));
  display.print(getDeviceId());

  // Row 2: PoP PIN
  display.setCursor(2, 28);
  display.print(F("PoP PIN: "));
  display.print(POP_SECRET);

  // Row 3: Status
  display.setCursor(2, 40);
  display.print(F("Status: "));
  display.print(provStatusMsg);

  // Divider
  display.drawLine(0, 51, SCREEN_WIDTH, 51, SSD1306_WHITE);

  // Row 4: Socket Status & Tech
  display.setCursor(2, 54);
  TelemetryData t;
  PowerMonitor::getTelemetry(t);
  display.print(t.relayEngaged ? F("Pwr:ON") : F("Pwr:OFF"));

  display.setCursor(54, 54);
  display.print(F("Security-1"));

  display.setCursor(102, 54);
  display.print(F("BLE"));

  display.display();
}

void DisplayManager::renderOperational(const TelemetryData &d) {
  display.clearDisplay();

  // Header Bar (Y=0..11)
  if (StorageManager::isClaimed()) {
    display.fillRect(0, 0, 52, 11, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(4, 2);
    display.print(F("CLAIMED"));
  } else {
    display.drawRect(0, 0, 60, 11, SSD1306_WHITE);
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(4, 2);
    display.print(F("UNCLAIMED"));
  }

  // Socket Power status on right of header
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(68, 2);
  display.print(d.relayEngaged ? F("[SOCKET ON]") : F("[PWR CUT]"));

  // Line 2: Wi-Fi / IP Address (Y=14)
  display.setCursor(2, 14);
  if (WiFi.status() == WL_CONNECTED) {
    display.print(F("IP: "));
    display.print(WiFi.localIP());
  } else {
    display.print(F("WiFi: Connecting..."));
  }

  // Line 3: Voltage (Y=25)
  display.setCursor(2, 25);
  display.print(F("Voltage : "));
  display.print(d.vRMS, 1);
  display.print(F(" V"));

  // Line 4: Current (Y=36)
  display.setCursor(2, 36);
  display.print(F("Current : "));
  display.print(d.iRMS, 2);
  display.print(F(" A"));

  // Line 5: Power (Y=47)
  display.setCursor(2, 47);
  display.print(F("Power   : "));
  display.print(d.activePower, 1);
  display.print(F(" W"));

  // Footer (Y=56)
  display.drawLine(0, 55, SCREEN_WIDTH, 55, SSD1306_WHITE);
  display.setCursor(2, 57);
  display.print(F("PF:"));
  display.print(d.powerFactor, 2);

  display.setCursor(54, 57);
  display.print(F("VA:"));
  display.print(d.apparentPower, 0);

  display.setCursor(98, 57);
  display.print(F("ESP32"));

  display.display();
}

void DisplayManager::renderToast() {
  display.clearDisplay();
  display.drawRect(4, 12, SCREEN_WIDTH - 8, 40, SSD1306_WHITE);
  display.fillRect(6, 14, SCREEN_WIDTH - 12, 36, SSD1306_BLACK);

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  int16_t xPos = (SCREEN_WIDTH - (strlen(currentToast) * 6)) / 2;
  if (xPos < 8) xPos = 8;
  display.setCursor(xPos, 28);
  display.print(currentToast);

  display.display();
}

void DisplayManager::update() {
  if (!hasDisplay) return;

  // Handle Toast
  if (isToastActive()) {
    renderToast();
    return;
  }

  if (currentMode == DISP_MODE_PROVISIONING) {
    renderProvisioning();
  } else {
    TelemetryData snap;
    PowerMonitor::getTelemetry(snap);
    renderOperational(snap);
  }
}
