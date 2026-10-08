#include "DisplayManager.h"
#include "Storage.h"
#include "RelayController.h"
#include <ESP8266WiFi.h>

Adafruit_SSD1306 DisplayManager::display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool DisplayManager::hasDisplay = false;
char DisplayManager::currentToast[32] = "";
unsigned long DisplayManager::toastStartTime = 0;
unsigned long DisplayManager::toastDuration = 0;
unsigned long DisplayManager::lastDisplayTick = 0;

void DisplayManager::init()
{
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClockStretchLimit(1500); // Prevents bus hangs if I2C slave is non-responsive

  if (display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    hasDisplay = true;
  }
  else
  {
    hasDisplay = false;
    Serial.println(F("Notice: OLED not detected. Operating in headless mode."));
  }
}

void DisplayManager::showSplash()
{
  if (!hasDisplay)
    return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(10, 4);
  display.println(F("SMART SWITCH 2CH"));

  int16_t xPos = (SCREEN_WIDTH - (strlen(DEVICE_ID) * 6)) / 2;
  if (xPos < 0)
    xPos = 0;
  display.setCursor(xPos, 18);
  display.println(DEVICE_ID);
  display.display();
  delay(1500);
}

void DisplayManager::showToast(const char *msg, unsigned long durationMs)
{
  strncpy(currentToast, msg, sizeof(currentToast) - 1);
  currentToast[sizeof(currentToast) - 1] = '\0';
  toastStartTime = millis();
  toastDuration = durationMs;
  update();
}

bool DisplayManager::isToastActive()
{
  return (strlen(currentToast) > 0 && (millis() - toastStartTime < toastDuration));
}

bool DisplayManager::hasDisplayHardware()
{
  return hasDisplay;
}

void DisplayManager::update()
{
  if (!hasDisplay)
    return; // Safe headless fallback

  // Handle toast timeout
  static bool hadToast = false;
  if (isToastActive())
  {
    hadToast = true;
  }
  else if (hadToast)
  {
    hadToast = false;
    currentToast[0] = '\0';
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Line 1: Header (Y=0..8)
  if (StorageManager::isClaimed())
  {
    // Inverted solid badge for CLAIMED
    display.fillRect(0, 0, 52, 9, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setCursor(2, 1);
    display.print(F("CLAIMED"));
    display.setTextColor(SSD1306_WHITE);
  }
  else
  {
    // Outline box for UNCLAIMED
    display.drawRect(0, 0, 64, 9, SSD1306_WHITE);
    display.setCursor(2, 1);
    display.print(F("UNCLAIMED"));
  }

  // Right Header: Wi-Fi status indicator
  display.setCursor(76, 1);
  if (WiFi.status() == WL_CONNECTED)
  {
    display.print(F("WiFi:OK"));
  }
  else
  {
    display.print(F("WiFi:--"));
  }

  // Divider Line (Y=10)
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // Line 2: Middle Status or Toast (Y=13)
  display.setCursor(0, 13);
  if (isToastActive())
  {
    display.print(currentToast);
  }
  else
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      display.print(F("IP: "));
      display.print(WiFi.localIP());
    }
    else
    {
      display.print(F("Connecting WiFi..."));
    }
  }

  // Line 3: Relay states (Y=23)
  // Switch 1
  display.setCursor(2, 23);
  display.print(F("SW1:["));
  display.print(RelayController::getRelay1() ? F("ON ") : F("OFF"));
  display.print(F("]"));

  // Switch 2
  display.setCursor(68, 23);
  display.print(F("SW2:["));
  display.print(RelayController::getRelay2() ? F("ON ") : F("OFF"));
  display.print(F("]"));

  display.display();
}
