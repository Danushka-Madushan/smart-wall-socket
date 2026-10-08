#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

// =====================================================================
//  HARDWARE DEFINITIONS
// =====================================================================
// Relays connected to NPN transistor bases (C945) via 1k resistors
// HIGH = Active (ON), LOW = Inactive (OFF)
#define RELAY1_PIN D5 // GPIO14 (Switch 1)
#define RELAY2_PIN D6 // GPIO12 (Switch 2)

// OLED Configuration (0.91" 128x32 I2C Display)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C // Standard I2C address for SSD1306

// I2C explicit pins on NodeMCU
#define OLED_SDA D2 // GPIO4
#define OLED_SCL D1 // GPIO5

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool hasDisplay = false; // Tracks if OLED hardware is present and responding

// =====================================================================
//  DEVICE & PROOF OF POSSESSION (PoP) CONFIGURATION
// =====================================================================
const char *DEVICE_ID = "RELAY_8266_NODE";
const char *POP_SECRET = "NODE_SEC_4102";
const char *TRANSPORT = "lan";
const int SWITCH_COUNT = 2;
const bool HAS_ENERGY = false;

// Pre-defined MAC address
const uint8_t FIXED_MAC[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x01};

// Wi-Fi Credentials
const char *WIFI_SSID = "ESP GATE";
const char *WIFI_PASS = "123123123";

// Maximum HTTP payload allowed (protects 80KB RAM from heap exhaustion)
const size_t MAX_PAYLOAD_SIZE = 1024;

// =====================================================================
//  PERSISTENT STORAGE (EEPROM) WITH INTEGRITY CHECKSUM
// =====================================================================
#define EEPROM_SIZE 512
#define EEPROM_MAGIC 0x506F5031 // "PoP1"

struct DeviceStorage
{
  uint32_t magic;
  bool claimed;
  char masterToken[65];
  uint32_t checksum;
};

DeviceStorage deviceState;

uint32_t calculateChecksum(const DeviceStorage &storage)
{
  uint32_t sum = storage.magic ^ (storage.claimed ? 0xAA55AA55 : 0x55AA55AA);
  for (size_t i = 0; i < sizeof(storage.masterToken); i++)
  {
    sum = ((sum << 5) | (sum >> 27)) ^ (uint8_t)storage.masterToken[i];
  }
  return sum;
}

// =====================================================================
//  GLOBAL RUNTIME STATE
// =====================================================================
bool r1State = false;
bool r2State = false;

char currentToast[32] = "";
unsigned long toastStartTime = 0;
unsigned long toastDuration = 0;

ESP8266WebServer server(80);

// =====================================================================
//  FORWARD DECLARATIONS
// =====================================================================
void loadConfig();
void saveConfig();
void setupWiFi();
void setupMDNS();
void updateMDNSTXT();
void updateDisplay();
void showToast(const char *msg, unsigned long durationMs = 2500);
bool isToastActive();
void setRelays(bool relay1, bool relay2, const char *toastMsg = nullptr);
bool isAuthorized();
void sendResponse(int code, const char *contentType, const String &content);
void handleOptions();

void handleHeartbeat();
void handleInfo();
void handleWifiStatus();
void handleClaim();
void handleVerify();
void handleRelayGet();
void handleRelayPost();
void handleRelay1Post();
void handleRelay2Post();
void handleRelayAllPost();
void handleUnclaim();
void handleNotFound();

// =====================================================================
//  SETUP
// =====================================================================
void setup()
{
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n========================================"));
  Serial.println(F("   Smart Wall Socket - Relay Node"));
  Serial.println(F("========================================"));

  // 1. Hardware Glitch Suppression: Set state LOW BEFORE pinMode OUTPUT
  // Eliminates sub-microsecond floating glitches on transistor bases
  digitalWrite(RELAY1_PIN, LOW);
  pinMode(RELAY1_PIN, OUTPUT);
  digitalWrite(RELAY2_PIN, LOW);
  pinMode(RELAY2_PIN, OUTPUT);

  // 2. Initialize I2C with clock stretch protection
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClockStretchLimit(1500); // Prevents bus hangs if I2C slave is non-responsive

  if (display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    hasDisplay = true;
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
  else
  {
    hasDisplay = false;
    Serial.println(F("Notice: OLED not detected. Operating in headless mode."));
  }

  // 3. Load Storage / Claim State from EEPROM with Checksum Integrity
  loadConfig();

  // 4. Set Fixed MAC Address
  wifi_set_macaddr(STATION_IF, const_cast<uint8_t *>(FIXED_MAC));
  Serial.printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                FIXED_MAC[0], FIXED_MAC[1], FIXED_MAC[2],
                FIXED_MAC[3], FIXED_MAC[4], FIXED_MAC[5]);

  // 5. Connect to Wi-Fi with Auto-Reconnect
  setupWiFi();

  // 6. Start Multicast DNS (mDNS)
  setupMDNS();

  // 7. Configure HTTP Server Routes, Headers, and CORS
  const char *headerkeys[] = {"Authorization", "X-Master-Token"};
  server.collectHeaders(headerkeys, sizeof(headerkeys) / sizeof(char *));

  server.on("/api/heartbeat", HTTP_GET, handleHeartbeat);
  server.on("/api/info", HTTP_GET, handleInfo);
  server.on("/api/wifi/status", HTTP_GET, handleWifiStatus);
  server.on("/api/claim", HTTP_POST, handleClaim);
  server.on("/api/verify", HTTP_POST, handleVerify);
  server.on("/api/verify", HTTP_GET, handleVerify);
  server.on("/api/relay", HTTP_GET, handleRelayGet);
  server.on("/api/relay", HTTP_POST, handleRelayPost);
  server.on("/api/relay/1", HTTP_POST, handleRelay1Post);
  server.on("/api/relay/2", HTTP_POST, handleRelay2Post);
  server.on("/api/relay/all", HTTP_POST, handleRelayAllPost);
  server.on("/api/unclaim", HTTP_POST, handleUnclaim);
  server.on("/api/reset", HTTP_POST, handleUnclaim);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println(F("HTTP server started on port 80"));

  // 8. Refresh Display
  updateDisplay();
}

// =====================================================================
//  LOOP
// =====================================================================
void loop()
{
  server.handleClient();
  MDNS.update();

  // Autonomous Wi-Fi Reconnection & mDNS Recovery Tracker
  static bool wasConnected = false;
  bool isConnected = (WiFi.status() == WL_CONNECTED);

  if (isConnected != wasConnected)
  {
    wasConnected = isConnected;
    if (isConnected)
    {
      Serial.print(F("Wi-Fi Reconnected! IP: "));
      Serial.println(WiFi.localIP());
      updateMDNSTXT();
      showToast("WiFi Connected", 2000);
    }
    else
    {
      Serial.println(F("Wi-Fi connection lost. Waiting for auto-reconnect..."));
      updateDisplay();
    }
  }

  // Millis overflow-safe toast expiration
  static bool hadToast = false;
  if (isToastActive())
  {
    hadToast = true;
  }
  else if (hadToast)
  {
    hadToast = false;
    currentToast[0] = '\0';
    updateDisplay();
  }

  // Periodic display refresh for Wi-Fi status or IP updates
  static unsigned long lastDisplayTick = 0;
  if (millis() - lastDisplayTick > 5000)
  {
    lastDisplayTick = millis();
    updateDisplay();
  }
}

// =====================================================================
//  WIFI & MDNS SETUP
// =====================================================================
void setupWiFi()
{
  WiFi.persistent(false);      // Prevents unnecessary flash writes on boot
  WiFi.setAutoReconnect(true); // Enables background reconnection by SDK
  WiFi.mode(WIFI_STA);
  WiFi.hostname(DEVICE_ID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  showToast("Connecting WiFi...", 5000);
  Serial.printf("Connecting to Wi-Fi: %s", WIFI_SSID);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30)
  {
    delay(500);
    Serial.print(F("."));
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print(F("Wi-Fi Connected! IP: "));
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println(F("Wi-Fi pending. Auto-reconnect active in background."));
  }
}

void onMDNSDynamicServiceTxt(const MDNSResponder::hMDNSService hService)
{
  MDNS.addDynamicServiceTxt(hService, "id", DEVICE_ID);
  MDNS.addDynamicServiceTxt(hService, "type", "node");
  MDNS.addDynamicServiceTxt(hService, "switches", "2");
  MDNS.addDynamicServiceTxt(hService, "energy", "0");
  MDNS.addDynamicServiceTxt(hService, "transport", TRANSPORT);
  MDNS.addDynamicServiceTxt(hService, "claimed", deviceState.claimed ? "1" : "0");
}

void setupMDNS()
{
  if (MDNS.begin("smartsocket"))
  {
    MDNS.addService("http", "tcp", 80);
    MDNS.setDynamicServiceTxtCallback(onMDNSDynamicServiceTxt);
    MDNS.announce();
    Serial.println(F("mDNS started: smartsocket.local"));
  }
  else
  {
    Serial.println(F("Error starting mDNS responder."));
  }
}

void updateMDNSTXT()
{
  MDNS.announce();
}

// =====================================================================
//  EEPROM STORAGE WITH CHECKSUM INTEGRITY
// =====================================================================
void loadConfig()
{
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, deviceState);

  uint32_t expectedChecksum = calculateChecksum(deviceState);

  if (deviceState.magic != EEPROM_MAGIC || deviceState.checksum != expectedChecksum)
  {
    Serial.println(F("EEPROM uninitialized or checksum mismatch. Applying factory default."));
    deviceState.magic = EEPROM_MAGIC;
    deviceState.claimed = false;
    memset(deviceState.masterToken, 0, sizeof(deviceState.masterToken));
    saveConfig();
  }
  else
  {
    Serial.printf("Config loaded cleanly: Status=%s\n",
                  deviceState.claimed ? "CLAIMED" : "UNCLAIMED");
  }
}

void saveConfig()
{
  deviceState.checksum = calculateChecksum(deviceState);
  EEPROM.put(0, deviceState);
  EEPROM.commit();
}

// =====================================================================
//  DISPLAY UI (128x32 OLED) WITH FAULT TOLERANCE
// =====================================================================
void showToast(const char *msg, unsigned long durationMs)
{
  strncpy(currentToast, msg, sizeof(currentToast) - 1);
  currentToast[sizeof(currentToast) - 1] = '\0';
  toastStartTime = millis();
  toastDuration = durationMs;
  updateDisplay();
}

bool isToastActive()
{
  return (strlen(currentToast) > 0 && (millis() - toastStartTime < toastDuration));
}

void updateDisplay()
{
  if (!hasDisplay)
    return; // Safe headless fallback if OLED is not installed or disconnected

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Line 1: Header (Y=0..8)
  if (deviceState.claimed)
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
  display.print(r1State ? F("ON ") : F("OFF"));
  display.print(F("]"));

  // Switch 2
  display.setCursor(68, 23);
  display.print(F("SW2:["));
  display.print(r2State ? F("ON ") : F("OFF"));
  display.print(F("]"));

  display.display();
}

// =====================================================================
//  HARDWARE RELAY CONTROL
// =====================================================================
void setRelays(bool relay1, bool relay2, const char *toastMsg)
{
  r1State = relay1;
  r2State = relay2;

  digitalWrite(RELAY1_PIN, r1State ? HIGH : LOW);
  digitalWrite(RELAY2_PIN, r2State ? HIGH : LOW);

  if (toastMsg)
  {
    showToast(toastMsg);
  }
  else
  {
    updateDisplay();
  }

  Serial.printf("Relay Change -> SW1: %s | SW2: %s\n",
                r1State ? "ON " : "OFF", r2State ? "ON " : "OFF");
}

// =====================================================================
//  CORS & AUTHENTICATION HELPERS
// =====================================================================
void sendResponse(int code, const char *contentType, const String &content)
{
  server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
  server.sendHeader(F("Access-Control-Allow-Methods"), F("GET, POST, OPTIONS"));
  server.sendHeader(F("Access-Control-Allow-Headers"), F("Content-Type, Authorization, X-Master-Token"));
  server.send(code, contentType, content);
}

void handleOptions()
{
  server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
  server.sendHeader(F("Access-Control-Allow-Methods"), F("GET, POST, OPTIONS"));
  server.sendHeader(F("Access-Control-Allow-Headers"), F("Content-Type, Authorization, X-Master-Token"));
  server.send(204);
}

bool isAuthorized()
{
  if (!deviceState.claimed)
  {
    return false;
  }

  // 1. Check Authorization header: "Bearer <token>"
  if (server.hasHeader("Authorization"))
  {
    String auth = server.header("Authorization");
    if (auth.startsWith("Bearer "))
    {
      String token = auth.substring(7);
      token.trim();
      if (token.equals(deviceState.masterToken))
      {
        return true;
      }
    }
  }

  // 2. Check X-Master-Token header
  if (server.hasHeader("X-Master-Token"))
  {
    String token = server.header("X-Master-Token");
    token.trim();
    if (token.equals(deviceState.masterToken))
    {
      return true;
    }
  }

  // 3. Check JSON body
  if (server.hasArg("plain"))
  {
    if (server.arg("plain").length() > MAX_PAYLOAD_SIZE)
    {
      return false;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (!err)
    {
      if (doc["master_token"].is<const char *>())
      {
        const char *t = doc["master_token"];
        if (strcmp(t, deviceState.masterToken) == 0)
          return true;
      }
      if (doc["token"].is<const char *>())
      {
        const char *t = doc["token"];
        if (strcmp(t, deviceState.masterToken) == 0)
          return true;
      }
    }
  }

  return false;
}

// =====================================================================
//  API ENDPOINTS
// =====================================================================

// GET /api/heartbeat
void handleHeartbeat()
{
  JsonDocument doc;
  doc["ack"] = true;
  doc["id"] = DEVICE_ID;
  doc["claimed"] = deviceState.claimed;
  doc["uptime_s"] = millis() / 1000;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["rssi"] = WiFi.RSSI();

  JsonObject relays = doc["relays"].to<JsonObject>();
  relays["1"] = r1State;
  relays["2"] = r2State;

  String output;
  serializeJson(doc, output);
  sendResponse(200, "application/json", output);
}

// GET /api/info
void handleInfo()
{
  JsonDocument doc;
  doc["id"] = DEVICE_ID;
  doc["type"] = "node";
  doc["switches"] = SWITCH_COUNT;
  doc["energy_monitoring"] = HAS_ENERGY;
  doc["claimed"] = deviceState.claimed;
  doc["transport"] = TRANSPORT;

  JsonObject relays = doc["relays"].to<JsonObject>();
  relays["1"] = r1State;
  relays["2"] = r2State;

  String output;
  serializeJson(doc, output);
  sendResponse(200, "application/json", output);
}

// GET /api/wifi/status (Compatibility endpoint)
void handleWifiStatus()
{
  JsonDocument doc;
  doc["connected"] = (WiFi.status() == WL_CONNECTED);
  doc["ssid"] = WiFi.SSID();
  doc["ip"] = WiFi.localIP().toString();
  doc["mac"] = WiFi.macAddress();
  doc["rssi"] = WiFi.RSSI();
  doc["claimed"] = deviceState.claimed;

  String output;
  serializeJson(doc, output);
  sendResponse(200, "application/json", output);
}

// POST /api/claim
void handleClaim()
{
  if (deviceState.claimed)
  {
    sendResponse(409, "application/json", "{\"error\":\"Device already claimed\"}");
    return;
  }

  if (!server.hasArg("plain"))
  {
    sendResponse(400, "application/json", "{\"error\":\"Request body required\"}");
    return;
  }

  if (server.arg("plain").length() > MAX_PAYLOAD_SIZE)
  {
    sendResponse(413, "application/json", "{\"error\":\"Payload too large (max 1KB)\"}");
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err)
  {
    sendResponse(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  const char *pop = doc["pop"];
  const char *masterToken = doc["master_token"];

  if (!pop || strlen(pop) == 0)
  {
    sendResponse(400, "application/json", "{\"error\":\"Field 'pop' is required\"}");
    return;
  }

  if (strcmp(pop, POP_SECRET) != 0)
  {
    Serial.println(F("Claim rejected: Invalid PoP secret."));
    showToast("CLAIM REJECTED", 2000);
    sendResponse(403, "application/json", "{\"error\":\"Invalid Proof of Possession (PoP)\"}");
    return;
  }

  if (!masterToken || strlen(masterToken) == 0)
  {
    sendResponse(400, "application/json", "{\"error\":\"Field 'master_token' is required\"}");
    return;
  }

  // Save to persistent storage with checksum
  deviceState.magic = EEPROM_MAGIC;
  deviceState.claimed = true;
  strncpy(deviceState.masterToken, masterToken, sizeof(deviceState.masterToken) - 1);
  deviceState.masterToken[sizeof(deviceState.masterToken) - 1] = '\0';
  saveConfig();

  // Update mDNS TXT records
  updateMDNSTXT();

  showToast("CLAIM SUCCESS!", 3000);
  Serial.println(F("Device successfully claimed!"));

  sendResponse(200, "application/json", "{\"status\":\"claimed\",\"message\":\"Device successfully claimed\"}");
}

// POST / GET /api/verify
void handleVerify()
{
  if (!deviceState.claimed)
  {
    sendResponse(401, "application/json", "{\"error\":\"Device unclaimed\"}");
    return;
  }

  if (isAuthorized())
  {
    JsonDocument doc;
    doc["verified"] = true;
    doc["id"] = DEVICE_ID;
    String output;
    serializeJson(doc, output);
    sendResponse(200, "application/json", output);
  }
  else
  {
    sendResponse(401, "application/json", "{\"error\":\"Unauthorized\",\"verified\":false}");
  }
}

// GET /api/relay
void handleRelayGet()
{
  if (!deviceState.claimed)
  {
    sendResponse(401, "application/json", "{\"error\":\"Device unclaimed\"}");
    return;
  }
  if (!isAuthorized())
  {
    sendResponse(401, "application/json", "{\"error\":\"Unauthorized\"}");
    return;
  }

  JsonDocument doc;
  JsonObject relays = doc["relays"].to<JsonObject>();
  relays["1"] = r1State;
  relays["2"] = r2State;

  String output;
  serializeJson(doc, output);
  sendResponse(200, "application/json", output);
}

// POST /api/relay
void handleRelayPost()
{
  if (!deviceState.claimed)
  {
    sendResponse(401, "application/json", "{\"error\":\"Device unclaimed. Claim device first.\"}");
    return;
  }

  if (!isAuthorized())
  {
    sendResponse(401, "application/json", "{\"error\":\"Unauthorized: invalid or missing master_token\"}");
    return;
  }

  if (!server.hasArg("plain"))
  {
    sendResponse(400, "application/json", "{\"error\":\"Request body required\"}");
    return;
  }

  if (server.arg("plain").length() > MAX_PAYLOAD_SIZE)
  {
    sendResponse(413, "application/json", "{\"error\":\"Payload too large (max 1KB)\"}");
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err)
  {
    sendResponse(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  bool newR1 = r1State;
  bool newR2 = r2State;
  bool modified = false;

  // Pattern 1: {"relay": 1|2|"all", "state": true|false}
  if (doc["relay"].is<int>())
  {
    int ch = doc["relay"].as<int>();
    if (doc["state"].is<bool>())
    {
      bool st = doc["state"].as<bool>();
      if (ch == 1)
      {
        newR1 = st;
        modified = true;
      }
      else if (ch == 2)
      {
        newR2 = st;
        modified = true;
      }
    }
  }
  else if (doc["relay"].is<const char *>())
  {
    const char *rStr = doc["relay"];
    if (strcasecmp(rStr, "all") == 0 || strcasecmp(rStr, "both") == 0)
    {
      if (doc["state"].is<bool>())
      {
        bool st = doc["state"].as<bool>();
        newR1 = st;
        newR2 = st;
        modified = true;
      }
    }
  }

  // Pattern 2: {"all": true|false}
  if (doc["all"].is<bool>())
  {
    bool st = doc["all"].as<bool>();
    newR1 = st;
    newR2 = st;
    modified = true;
  }

  // Pattern 3: {"relay1": bool, "relay2": bool}
  if (doc["relay1"].is<bool>())
  {
    newR1 = doc["relay1"].as<bool>();
    modified = true;
  }
  if (doc["relay2"].is<bool>())
  {
    newR2 = doc["relay2"].as<bool>();
    modified = true;
  }

  if (!modified)
  {
    sendResponse(400, "application/json",
                 "{\"error\":\"Provide 'relay' and 'state', 'all', or 'relay1'/'relay2'\"}");
    return;
  }

  setRelays(newR1, newR2);

  JsonDocument resp;
  resp["success"] = true;
  JsonObject relays = resp["relays"].to<JsonObject>();
  relays["1"] = r1State;
  relays["2"] = r2State;

  String output;
  serializeJson(resp, output);
  sendResponse(200, "application/json", output);
}

// POST /api/relay/1
void handleRelay1Post()
{
  if (!deviceState.claimed)
  {
    sendResponse(401, "application/json", "{\"error\":\"Device unclaimed\"}");
    return;
  }
  if (!isAuthorized())
  {
    sendResponse(401, "application/json", "{\"error\":\"Unauthorized\"}");
    return;
  }

  bool st = !r1State; // default toggle if no body
  if (server.hasArg("plain"))
  {
    if (server.arg("plain").length() > MAX_PAYLOAD_SIZE)
    {
      sendResponse(413, "application/json", "{\"error\":\"Payload too large\"}");
      return;
    }
    JsonDocument doc;
    if (!deserializeJson(doc, server.arg("plain")) && doc["state"].is<bool>())
    {
      st = doc["state"].as<bool>();
    }
  }

  setRelays(st, r2State);

  JsonDocument resp;
  resp["success"] = true;
  JsonObject relays = resp["relays"].to<JsonObject>();
  relays["1"] = r1State;
  relays["2"] = r2State;

  String output;
  serializeJson(resp, output);
  sendResponse(200, "application/json", output);
}

// POST /api/relay/2
void handleRelay2Post()
{
  if (!deviceState.claimed)
  {
    sendResponse(401, "application/json", "{\"error\":\"Device unclaimed\"}");
    return;
  }
  if (!isAuthorized())
  {
    sendResponse(401, "application/json", "{\"error\":\"Unauthorized\"}");
    return;
  }

  bool st = !r2State;
  if (server.hasArg("plain"))
  {
    if (server.arg("plain").length() > MAX_PAYLOAD_SIZE)
    {
      sendResponse(413, "application/json", "{\"error\":\"Payload too large\"}");
      return;
    }
    JsonDocument doc;
    if (!deserializeJson(doc, server.arg("plain")) && doc["state"].is<bool>())
    {
      st = doc["state"].as<bool>();
    }
  }

  setRelays(r1State, st);

  JsonDocument resp;
  resp["success"] = true;
  JsonObject relays = resp["relays"].to<JsonObject>();
  relays["1"] = r1State;
  relays["2"] = r2State;

  String output;
  serializeJson(resp, output);
  sendResponse(200, "application/json", output);
}

// POST /api/relay/all
void handleRelayAllPost()
{
  if (!deviceState.claimed)
  {
    sendResponse(401, "application/json", "{\"error\":\"Device unclaimed\"}");
    return;
  }
  if (!isAuthorized())
  {
    sendResponse(401, "application/json", "{\"error\":\"Unauthorized\"}");
    return;
  }

  bool st = true;
  if (server.hasArg("plain"))
  {
    if (server.arg("plain").length() > MAX_PAYLOAD_SIZE)
    {
      sendResponse(413, "application/json", "{\"error\":\"Payload too large\"}");
      return;
    }
    JsonDocument doc;
    if (!deserializeJson(doc, server.arg("plain")) && doc["state"].is<bool>())
    {
      st = doc["state"].as<bool>();
    }
  }

  setRelays(st, st);

  JsonDocument resp;
  resp["success"] = true;
  JsonObject relays = resp["relays"].to<JsonObject>();
  relays["1"] = r1State;
  relays["2"] = r2State;

  String output;
  serializeJson(resp, output);
  sendResponse(200, "application/json", output);
}

// POST /api/unclaim or POST /api/reset
void handleUnclaim()
{
  bool allowed = false;

  if (isAuthorized())
  {
    allowed = true;
  }

  if (!allowed && server.hasArg("plain"))
  {
    if (server.arg("plain").length() <= MAX_PAYLOAD_SIZE)
    {
      JsonDocument doc;
      if (!deserializeJson(doc, server.arg("plain")))
      {
        const char *pop = doc["pop"];
        if (pop && strcmp(pop, POP_SECRET) == 0)
        {
          allowed = true;
        }
      }
    }
  }

  if (!allowed)
  {
    sendResponse(401, "application/json",
                 "{\"error\":\"Provide valid master_token or pop to unclaim\"}");
    return;
  }

  // Reset EEPROM with fresh checksum
  deviceState.magic = EEPROM_MAGIC;
  deviceState.claimed = false;
  memset(deviceState.masterToken, 0, sizeof(deviceState.masterToken));
  saveConfig();

  setRelays(false, false, "RESET: UNCLAIMED");
  updateMDNSTXT();

  Serial.println(F("Device reverted to factory UNCLAIMED state."));
  sendResponse(200, "application/json",
               "{\"status\":\"unclaimed\",\"message\":\"Device reverted to factory unclaimed state\"}");
}

// 404 handler with universal CORS OPTIONS support
void handleNotFound()
{
  if (server.method() == HTTP_OPTIONS)
  {
    handleOptions();
    return;
  }
  sendResponse(404, "application/json", "{\"error\":\"Endpoint not found\"}");
}
