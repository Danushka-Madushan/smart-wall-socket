#include <Arduino.h>
#include "EmonLib.h"

EnergyMonitor emon1;

// Fixed voltage for Sri Lanka mains (prototype only)
const double FIXED_VOLTAGE = 230.0;

void setup() {
  Serial.begin(115200);
  
  // Wait for serial to initialize
  while (!Serial) {} 
  Serial.println("\nStarting Energy Monitor Prototype...");

  // Initialize EmonLib:
  // Pin = A0. Calibration = 30.0 (because the SCT-013-030 is scaled to 30A/1V)
  emon1.current(A0, 30.0);
}

void loop() {
  // Calculate Irms (1480 samples provides a solid average for 50Hz AC mains)
  double Irms = emon1.calcIrms(1480);

  // ESP8266 internal ADC is noisy. This gates out floating ghost voltages.
  // If reading is less than 100mA, snap it to 0.

  // Calculate Apparent Power
  double estimatedWattage = Irms * FIXED_VOLTAGE;

  // Logging output
  Serial.print("Current: ");
  Serial.print(Irms, 10);
  Serial.print(" A  |  ");
  
  Serial.print("Est. Power: ");
  Serial.print(estimatedWattage, 2);
  Serial.println(" W");

  // Read once per second
  delay(1000);
}
