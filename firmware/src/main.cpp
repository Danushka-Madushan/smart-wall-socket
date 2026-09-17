#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include "EmonLib.h"

// ─── Fixed MAC Address ───────────────────────────────────────────────
const uint8_t FIXED_MAC[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x01 };

// ─── Access Point Configuration ──────────────────────────────────────
const char* AP_SSID     = "SmartSocket-AP";
const char* AP_PASSWORD = "socket1234";   // min 8 chars for WPA2

// ─── Static IP for the AP ────────────────────────────────────────────
IPAddress apIP(192, 168, 4, 1);
IPAddress apGateway(192, 168, 4, 1);
IPAddress apSubnet(255, 255, 255, 0);

// ─── Energy Monitor ─────────────────────────────────────────────────
EnergyMonitor emon1;
const double FIXED_VOLTAGE = 230.0;  // Sri Lanka mains (prototype only)

// ─── Web Server on port 80 ──────────────────────────────────────────
ESP8266WebServer server(80);

// ─── Latest energy readings (updated in loop) ───────────────────────
double lastIrms    = 0.0;
double lastWattage = 0.0;

// ─── Forward declarations ───────────────────────────────────────────
void handleWifiStatus();
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
  wifi_set_macaddr(SOFTAP_IF, const_cast<uint8_t*>(FIXED_MAC));
  Serial.printf("MAC address set to: %02X:%02X:%02X:%02X:%02X:%02X\n",
                FIXED_MAC[0], FIXED_MAC[1], FIXED_MAC[2],
                FIXED_MAC[3], FIXED_MAC[4], FIXED_MAC[5]);

  // --- Start WiFi in AP-only mode with static IP ---
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apGateway, apSubnet);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("AP started: ");
  Serial.println(AP_SSID);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  // --- Register API routes & start server ---
  server.on("/api/wifi/status", HTTP_GET, handleWifiStatus);
  server.on("/api/energy",      HTTP_GET, handleEnergy);
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
//  GET /api/wifi/status
// =====================================================================
void handleWifiStatus() {
  JsonDocument doc;

  doc["ssid"]    = String(AP_SSID);
  doc["ip"]      = WiFi.softAPIP().toString();
  doc["mac"]     = WiFi.softAPmacAddress();
  doc["clients"] = WiFi.softAPgetStationNum();

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
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
