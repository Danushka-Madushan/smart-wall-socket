#include "ApiServer.h"
#include "StorageManager.h"
#include "RelayController.h"
#include "NetworkManager.h"
#include "DisplayManager.h"
#include "PowerMonitor.h"

WebServer ApiServer::server(80);

void ApiServer::sendResponse(int code, const char *contentType, const String &content) {
  server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
  server.sendHeader(F("Access-Control-Allow-Methods"), F("GET, POST, OPTIONS"));
  server.sendHeader(F("Access-Control-Allow-Headers"), F("Content-Type, Authorization, X-Master-Token"));
  server.send(code, contentType, content);
}

void ApiServer::handleOptions() {
  server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
  server.sendHeader(F("Access-Control-Allow-Methods"), F("GET, POST, OPTIONS"));
  server.sendHeader(F("Access-Control-Allow-Headers"), F("Content-Type, Authorization, X-Master-Token"));
  server.send(204);
}

bool ApiServer::isAuthorized() {
  if (!StorageManager::isClaimed()) {
    return false;
  }

  const char *savedToken = StorageManager::getMasterToken();

  // 1. Authorization header: "Bearer <token>"
  if (server.hasHeader("Authorization")) {
    String auth = server.header("Authorization");
    if (auth.startsWith("Bearer ")) {
      String token = auth.substring(7);
      token.trim();
      if (token.equals(savedToken)) {
        return true;
      }
    }
  }

  // 2. X-Master-Token header
  if (server.hasHeader("X-Master-Token")) {
    String token = server.header("X-Master-Token");
    token.trim();
    if (token.equals(savedToken)) {
      return true;
    }
  }

  // 3. JSON body token
  if (server.hasArg("plain")) {
    if (server.arg("plain").length() > MAX_PAYLOAD_SIZE) {
      return false;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (!err) {
      if (doc["master_token"].is<const char *>()) {
        const char *t = doc["master_token"];
        if (strcmp(t, savedToken) == 0) return true;
      }
      if (doc["token"].is<const char *>()) {
        const char *t = doc["token"];
        if (strcmp(t, savedToken) == 0) return true;
      }
    }
  }

  return false;
}

void ApiServer::init() {
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
  server.on("/api/relay/all", HTTP_POST, handleRelayAllPost);
  server.on("/api/energy", HTTP_GET, handleEnergy);
  server.on("/api/unclaim", HTTP_POST, handleUnclaim);
  server.on("/api/reset", HTTP_POST, handleUnclaim);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println(F("[HTTP] Server started on port 80"));
}

void ApiServer::handleClient() {
  server.handleClient();
}

// GET /api/heartbeat
void ApiServer::handleHeartbeat() {
  JsonDocument doc;
  doc["ack"] = true;
  doc["id"] = getDeviceId();
  doc["claimed"] = StorageManager::isClaimed();
  doc["uptime_s"] = millis() / 1000;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["rssi"] = NetworkManager::getRSSI();

  JsonObject relays = doc["relays"].to<JsonObject>();
  relays["1"] = RelayController::isPowerOn();

  String output;
  serializeJson(doc, output);
  sendResponse(200, "application/json", output);
}

// GET /api/info
void ApiServer::handleInfo() {
  JsonDocument doc;
  doc["id"] = getDeviceId();
  doc["type"] = "node";
  doc["switches"] = SWITCH_COUNT;
  doc["energy_monitoring"] = HAS_ENERGY;
  doc["claimed"] = StorageManager::isClaimed();
  doc["transport"] = TRANSPORT;

  JsonObject relays = doc["relays"].to<JsonObject>();
  relays["1"] = RelayController::isPowerOn();

  String output;
  serializeJson(doc, output);
  sendResponse(200, "application/json", output);
}

// GET /api/wifi/status
void ApiServer::handleWifiStatus() {
  JsonDocument doc;
  doc["connected"] = NetworkManager::isConnected();
  doc["ssid"] = NetworkManager::getSSID();
  doc["ip"] = NetworkManager::getLocalIP().toString();
  doc["mac"] = NetworkManager::getMAC();
  doc["rssi"] = NetworkManager::getRSSI();
  doc["claimed"] = StorageManager::isClaimed();

  String output;
  serializeJson(doc, output);
  sendResponse(200, "application/json", output);
}

// POST /api/claim
void ApiServer::handleClaim() {
  if (StorageManager::isClaimed()) {
    sendResponse(409, "application/json", "{\"error\":\"Device already claimed\"}");
    return;
  }

  if (!server.hasArg("plain")) {
    sendResponse(400, "application/json", "{\"error\":\"Request body required\"}");
    return;
  }

  if (server.arg("plain").length() > MAX_PAYLOAD_SIZE) {
    sendResponse(413, "application/json", "{\"error\":\"Payload too large (max 1KB)\"}");
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    sendResponse(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  const char *pop = doc["pop"];
  const char *masterToken = doc["master_token"];

  if (!pop || strlen(pop) == 0) {
    sendResponse(400, "application/json", "{\"error\":\"Field 'pop' is required\"}");
    return;
  }

  if (strcmp(pop, POP_SECRET) != 0) {
    Serial.println(F("[CLAIM] Claim rejected: Invalid PoP secret."));
    DisplayManager::showToast("CLAIM REJECTED", 2000);
    sendResponse(403, "application/json", "{\"error\":\"Invalid Proof of Possession (PoP)\"}");
    return;
  }

  if (!masterToken || strlen(masterToken) == 0) {
    sendResponse(400, "application/json", "{\"error\":\"Field 'master_token' is required\"}");
    return;
  }

  // Save to persistent NVS storage
  StorageManager::setClaimed(masterToken);

  // Update mDNS TXT record
  NetworkManager::updateMDNS();

  DisplayManager::showToast("CLAIM SUCCESS!", 3000);
  Serial.println(F("[CLAIM] Device successfully claimed!"));

  sendResponse(200, "application/json", "{\"status\":\"claimed\",\"message\":\"Device successfully claimed\"}");
}

// POST / GET /api/verify
void ApiServer::handleVerify() {
  if (!StorageManager::isClaimed()) {
    sendResponse(401, "application/json", "{\"error\":\"Device unclaimed\"}");
    return;
  }

  if (isAuthorized()) {
    JsonDocument doc;
    doc["verified"] = true;
    doc["id"] = getDeviceId();
    String output;
    serializeJson(doc, output);
    sendResponse(200, "application/json", output);
  } else {
    sendResponse(401, "application/json", "{\"error\":\"Unauthorized\",\"verified\":false}");
  }
}

// GET /api/relay
void ApiServer::handleRelayGet() {
  if (!StorageManager::isClaimed()) {
    sendResponse(401, "application/json", "{\"error\":\"Device unclaimed\"}");
    return;
  }
  if (!isAuthorized()) {
    sendResponse(401, "application/json", "{\"error\":\"Unauthorized\"}");
    return;
  }

  JsonDocument doc;
  JsonObject relays = doc["relays"].to<JsonObject>();
  relays["1"] = RelayController::isPowerOn();

  String output;
  serializeJson(doc, output);
  sendResponse(200, "application/json", output);
}

// POST /api/relay
void ApiServer::handleRelayPost() {
  if (!StorageManager::isClaimed()) {
    sendResponse(401, "application/json", "{\"error\":\"Device unclaimed. Claim device first.\"}");
    return;
  }
  if (!isAuthorized()) {
    sendResponse(401, "application/json", "{\"error\":\"Unauthorized: invalid or missing master_token\"}");
    return;
  }

  if (!server.hasArg("plain")) {
    sendResponse(400, "application/json", "{\"error\":\"Request body required\"}");
    return;
  }

  if (server.arg("plain").length() > MAX_PAYLOAD_SIZE) {
    sendResponse(413, "application/json", "{\"error\":\"Payload too large (max 1KB)\"}");
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    sendResponse(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  bool newState = RelayController::isPowerOn();
  bool modified = false;

  if (doc["state"].is<bool>()) {
    newState = doc["state"].as<bool>();
    modified = true;
  } else if (doc["relay"].is<int>() && doc["state"].is<bool>()) {
    newState = doc["state"].as<bool>();
    modified = true;
  } else if (doc["all"].is<bool>()) {
    newState = doc["all"].as<bool>();
    modified = true;
  }

  if (!modified) {
    sendResponse(400, "application/json", "{\"error\":\"Provide 'state' boolean (e.g. {state: true})\"}");
    return;
  }

  RelayController::setPower(newState);

  JsonDocument resp;
  resp["success"] = true;
  JsonObject relays = resp["relays"].to<JsonObject>();
  relays["1"] = RelayController::isPowerOn();

  String output;
  serializeJson(resp, output);
  sendResponse(200, "application/json", output);
}

// POST /api/relay/1
void ApiServer::handleRelay1Post() {
  if (!StorageManager::isClaimed()) {
    sendResponse(401, "application/json", "{\"error\":\"Device unclaimed\"}");
    return;
  }
  if (!isAuthorized()) {
    sendResponse(401, "application/json", "{\"error\":\"Unauthorized\"}");
    return;
  }

  bool st = !RelayController::isPowerOn(); // default toggle if body empty
  if (server.hasArg("plain")) {
    if (server.arg("plain").length() > MAX_PAYLOAD_SIZE) {
      sendResponse(413, "application/json", "{\"error\":\"Payload too large\"}");
      return;
    }
    JsonDocument doc;
    if (!deserializeJson(doc, server.arg("plain")) && doc["state"].is<bool>()) {
      st = doc["state"].as<bool>();
    }
  }

  RelayController::setPower(st);

  JsonDocument resp;
  resp["success"] = true;
  JsonObject relays = resp["relays"].to<JsonObject>();
  relays["1"] = RelayController::isPowerOn();

  String output;
  serializeJson(resp, output);
  sendResponse(200, "application/json", output);
}

// POST /api/relay/all
void ApiServer::handleRelayAllPost() {
  if (!StorageManager::isClaimed()) {
    sendResponse(401, "application/json", "{\"error\":\"Device unclaimed\"}");
    return;
  }
  if (!isAuthorized()) {
    sendResponse(401, "application/json", "{\"error\":\"Unauthorized\"}");
    return;
  }

  bool st = true;
  if (server.hasArg("plain")) {
    if (server.arg("plain").length() > MAX_PAYLOAD_SIZE) {
      sendResponse(413, "application/json", "{\"error\":\"Payload too large\"}");
      return;
    }
    JsonDocument doc;
    if (!deserializeJson(doc, server.arg("plain")) && doc["state"].is<bool>()) {
      st = doc["state"].as<bool>();
    }
  }

  RelayController::setPower(st);

  JsonDocument resp;
  resp["success"] = true;
  JsonObject relays = resp["relays"].to<JsonObject>();
  relays["1"] = RelayController::isPowerOn();

  String output;
  serializeJson(resp, output);
  sendResponse(200, "application/json", output);
}

// GET /api/energy (Polled by Android app SocketViewModel)
void ApiServer::handleEnergy() {
  TelemetryData snap;
  PowerMonitor::getTelemetry(snap);

  JsonDocument doc;
  doc["voltage_V"] = snap.vRMS;
  doc["current_A"] = snap.iRMS;
  doc["power_W"] = snap.activePower;
  doc["apparent_VA"] = snap.apparentPower;
  doc["power_factor"] = snap.powerFactor;
  doc["relay"] = snap.relayEngaged;

  String output;
  serializeJson(doc, output);
  sendResponse(200, "application/json", output);
}

// POST /api/unclaim or POST /api/reset
void ApiServer::handleUnclaim() {
  bool allowed = false;
  bool clearWifi = false;

  if (isAuthorized()) {
    allowed = true;
  }

  if (server.hasArg("plain")) {
    if (server.arg("plain").length() <= MAX_PAYLOAD_SIZE) {
      JsonDocument doc;
      if (!deserializeJson(doc, server.arg("plain"))) {
        const char *pop = doc["pop"];
        if (pop && strcmp(pop, POP_SECRET) == 0) {
          allowed = true;
        }
        if (doc["factory_reset"].is<bool>() && doc["factory_reset"].as<bool>()) {
          clearWifi = true;
        }
        if (doc["clear_wifi"].is<bool>() && doc["clear_wifi"].as<bool>()) {
          clearWifi = true;
        }
      }
    }
  }

  if (!allowed) {
    sendResponse(401, "application/json",
                 "{\"error\":\"Provide valid master_token or pop to unclaim\"}");
    return;
  }

  StorageManager::reset();
  RelayController::setPower(true, "RESET: UNCLAIMED");
  NetworkManager::updateMDNS();

  Serial.println(F("[UNCLAIM] Device reverted to factory UNCLAIMED state."));

  sendResponse(200, "application/json",
               "{\"status\":\"unclaimed\",\"message\":\"Device reverted to factory unclaimed state\"}");

  if (clearWifi) {
    delay(500);
    NetworkManager::resetProvisioning();
  }
}

// 404 handler with universal CORS OPTIONS
void ApiServer::handleNotFound() {
  if (server.method() == HTTP_OPTIONS) {
    handleOptions();
    return;
  }
  sendResponse(404, "application/json", "{\"error\":\"Endpoint not found\"}");
}
