#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C  // Default I2C address for 0.91" 128x32 OLED

// Relay Control Pins (Connected to C945 Transistor Base via 1k resistors)
#define RELAY1_PIN D5  // GPIO14
#define RELAY2_PIN D6  // GPIO12

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Track current states
bool r1State = false;
bool r2State = false;

// Function to refresh the 128x32 OLED display
void updateDisplay(const char* stepName) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Top header bar
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("MODE: "));
  display.print(stepName);
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // Relay 1 status badge
  display.setCursor(4, 18);
  display.print(F("R1: ["));
  display.print(r1State ? F("ON ") : F("OFF"));
  display.print(F("]"));

  // Relay 2 status badge
  display.setCursor(68, 18);
  display.print(F("R2: ["));
  display.print(r2State ? F("ON ") : F("OFF"));
  display.print(F("]"));

  display.display();
}

// Function to set hardware pins and update display
void setRelays(bool relay1, bool relay2, const char* label) {
  r1State = relay1;
  r2State = relay2;

  // With NPN low-side drivers: HIGH activates the relay, LOW turns it off
  digitalWrite(RELAY1_PIN, r1State ? HIGH : LOW);
  digitalWrite(RELAY2_PIN, r2State ? HIGH : LOW);

  updateDisplay(label);
  
  // Output to Serial Monitor for verification
  Serial.print(F("State -> R1: "));
  Serial.print(r1State ? "ON  " : "OFF ");
  Serial.print(F(" | R2: "));
  Serial.println(r2State ? "ON" : "OFF");
}

void setup() {
  Serial.begin(115200);

  // Configure Relay Pins
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  digitalWrite(RELAY1_PIN, LOW);
  digitalWrite(RELAY2_PIN, LOW);

  // Initialize I2C explicitly on NodeMCU D2 (SDA) and D1 (SCL)
  Wire.begin(D2, D1);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("Error: OLED not detected. Check I2C wiring (D1/D2) and power."));
    while (true) {
      delay(100);
    }
  }

  // Welcome Splash
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(14, 4);
  display.println(F("RELAY & OLED TEST"));
  display.setCursor(24, 18);
  display.println(F("Starting..."));
  display.display();
  delay(2000);
}

void loop() {
  // Test Sequence 1: Both Relays OFF
  setRelays(false, false, "ALL OFF");
  delay(2500);

  // Test Sequence 2: Relay 1 ON, Relay 2 OFF
  setRelays(true, false, "CH 1 ON");
  delay(2500);

  // Test Sequence 3: Relay 1 OFF, Relay 2 ON
  setRelays(false, true, "CH 2 ON");
  delay(2500);

  // Test Sequence 4: Both Relays ON
  setRelays(true, true, "ALL ON");
  delay(2500);
}
