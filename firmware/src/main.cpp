#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include "EmonLib.h"

// ─── Fixed MAC Address ───────────────────────────────────────────────
const uint8_t FIXED_MAC[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x01 };

// ─── WiFi Credentials ───────────────────────────────────────────────
const char* WIFI_SSID = "ESP GATE";
const char* WIFI_PASS = "123123123";

// ─── Static IP Configuration ────────────────────────────────────────
IPAddress staticIP(192, 168, 4, 100);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

// ─── Energy Monitor ─────────────────────────────────────────────────
EnergyMonitor emon1;
const double FIXED_VOLTAGE = 230.0;  // Sri Lanka mains (prototype only)

// ─── Web Server on port 80 ──────────────────────────────────────────
ESP8266WebServer server(80);

// ─── Latest energy readings (updated in loop) ───────────────────────
double lastIrms    = 0.0;
double lastWattage = 0.0;

// ─── Forward declarations ───────────────────────────────────────────
void handleHeartbeat();
void handleEnergy();
void handleNotFound();

// =====================================================================
//  SETUP
// =====================================================================
void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Serial.println("\nStarting Smart Wall Socket...");

  // --- Apply fixed MAC address ---
  wifi_set_macaddr(STATION_IF, const_cast<uint8_t*>(FIXED_MAC));
  Serial.printf("MAC address set to: %02X:%02X:%02X:%02X:%02X:%02X\n",
                FIXED_MAC[0], FIXED_MAC[1], FIXED_MAC[2],
                FIXED_MAC[3], FIXED_MAC[4], FIXED_MAC[5]);

  // --- Connect to WiFi with static IP ---
  WiFi.mode(WIFI_STA);
  WiFi.config(staticIP, gateway, subnet);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.printf("Connecting to %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());

  // --- Register API routes & start server ---
  server.on("/api/heartbeat", HTTP_GET, handleHeartbeat);
  server.on("/api/energy", HTTP_GET, handleEnergy);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started on port 80");

  // --- Initialize EmonLib ---
  // Pin = A0, Calibration = 30.0 (SCT-013-030 scaled to 30A/1V)
  emon1.current(A0, 30.0);
  Serial.println("Energy monitor initialized.");
}

// =====================================================================
//  LOOP
// =====================================================================
void loop() {
  server.handleClient();

  // ─── Electricity Monitoring (unchanged) ───────────────────────────
  // Calculate Irms (1480 samples provides a solid average for 50Hz AC mains)
  double Irms = emon1.calcIrms(1480);

  // ESP8266 internal ADC is noisy. This gates out floating ghost voltages.
  // If reading is less than 100mA, snap it to 0.

  // Calculate Apparent Power
  double estimatedWattage = Irms * FIXED_VOLTAGE;

  // Store for API access
  lastIrms    = Irms;
  lastWattage = estimatedWattage;

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

// =====================================================================
//  GET /api/heartbeat
// =====================================================================
void handleHeartbeat() {
  server.send(200, "application/json", "{\"ack\":true}");
}

// =====================================================================
//  GET /api/energy
// =====================================================================
void handleEnergy() {
  JsonDocument doc;

  doc["current_A"] = lastIrms;
  doc["power_W"]   = lastWattage;
  doc["voltage_V"] = FIXED_VOLTAGE;

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

// =====================================================================
//  404 handler
// =====================================================================
void handleNotFound() {
  server.send(404, "application/json", "{\"error\":\"Not found\"}");
}
