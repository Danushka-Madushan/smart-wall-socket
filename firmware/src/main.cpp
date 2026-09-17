#include <Arduino.h>

unsigned long counter = 0;

void setup() {
  // Initialize Serial communication at 115200 baud (ESP standard boot baud rate)
  Serial.begin(115200);

  // Small delay to allow the serial monitor to settle
  delay(1000);

  Serial.println();
  Serial.println("==========================================");
  Serial.println(" ESP8266 Smart Wall Socket - Firmware Ready");
  Serial.println(" Baud rate: 115200 bps");
  Serial.println("==========================================");
}

void loop() {
  counter++;
  Serial.printf("[LOG] Heartbeat count: %lu | Uptime: %lu ms\n", counter, millis());

  delay(1000); // 1-second gap
}
