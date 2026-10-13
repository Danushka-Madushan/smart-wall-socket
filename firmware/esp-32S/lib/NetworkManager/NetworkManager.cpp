#include "NetworkManager.h"
#include "StorageManager.h"
#include "DisplayManager.h"

bool NetworkManager::wasConnected = false;

void NetworkManager::setupMDNS() {
  MDNS.end();
  String host = String(getDeviceId());
  host.toLowerCase();
  host.replace('_', '-');

  if (MDNS.begin(host.c_str())) {
    MDNS.setInstanceName(getDeviceId());
    MDNS.addService("http", "tcp", 80);
    updateMDNS();
    Serial.printf("[mDNS] Responder started: %s.local (Service: %s)\n", host.c_str(), getDeviceId());
  } else {
    Serial.println(F("[mDNS] Error starting responder."));
  }
}

void NetworkManager::updateMDNS() {
  MDNS.addServiceTxt("http", "tcp", "id", getDeviceId());
  MDNS.addServiceTxt("http", "tcp", "type", "node");
  MDNS.addServiceTxt("http", "tcp", "switches", "1");
  MDNS.addServiceTxt("http", "tcp", "energy", "1");
  MDNS.addServiceTxt("http", "tcp", "transport", TRANSPORT);
  MDNS.addServiceTxt("http", "tcp", "claimed", StorageManager::isClaimed() ? "1" : "0");
}

void NetworkManager::init() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(getDeviceId());
  WiFi.setAutoReconnect(true);

  Serial.printf("[NETWORK] Connecting to Wi-Fi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) {
    delay(400);
    Serial.print(F("."));
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wasConnected = true;
    Serial.print(F("[WIFI] Connected! IP: "));
    Serial.println(WiFi.localIP());
  } else {
    wasConnected = false;
    Serial.println(F("[WIFI] Wi-Fi pending. Auto-reconnect active in background."));
  }

  setupMDNS();
}

void NetworkManager::update() {
  bool isConnectedNow = (WiFi.status() == WL_CONNECTED);

  if (isConnectedNow != wasConnected) {
    wasConnected = isConnectedNow;
    if (isConnectedNow) {
      Serial.print(F("[WIFI] Reconnected! IP: "));
      Serial.println(WiFi.localIP());
      setupMDNS();
    } else {
      Serial.println(F("[WIFI] Connection lost. Reconnecting..."));
    }
  }
}

void NetworkManager::resetProvisioning() {
  Serial.println(F("[NETWORK] Resetting Wi-Fi and rebooting..."));
  WiFi.disconnect(true, true);
  delay(300);
  ESP.restart();
}

bool NetworkManager::isConnected() {
  return (WiFi.status() == WL_CONNECTED);
}

IPAddress NetworkManager::getLocalIP() {
  return WiFi.localIP();
}

int32_t NetworkManager::getRSSI() {
  return WiFi.RSSI();
}

String NetworkManager::getSSID() {
  return WiFi.SSID();
}

String NetworkManager::getMAC() {
  return WiFi.macAddress();
}

