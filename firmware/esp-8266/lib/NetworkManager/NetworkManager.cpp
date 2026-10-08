#include "NetworkManager.h"
#include "Storage.h"
#include "DisplayManager.h"

bool NetworkManager::wasConnected = false;

void NetworkManager::onMDNSDynamicServiceTxt(const MDNSResponder::hMDNSService hService)
{
  MDNS.addDynamicServiceTxt(hService, "id", DEVICE_ID);
  MDNS.addDynamicServiceTxt(hService, "type", "node");
  MDNS.addDynamicServiceTxt(hService, "switches", "2");
  MDNS.addDynamicServiceTxt(hService, "energy", "0");
  MDNS.addDynamicServiceTxt(hService, "transport", TRANSPORT);
  MDNS.addDynamicServiceTxt(hService, "claimed", StorageManager::isClaimed() ? "1" : "0");
}

void NetworkManager::setupMDNS()
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

void NetworkManager::updateMDNS()
{
  MDNS.announce();
}

void NetworkManager::init()
{
  // 1. Set Fixed MAC Address
  wifi_set_macaddr(STATION_IF, const_cast<uint8_t *>(FIXED_MAC));
  Serial.printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                FIXED_MAC[0], FIXED_MAC[1], FIXED_MAC[2],
                FIXED_MAC[3], FIXED_MAC[4], FIXED_MAC[5]);

  // 2. Configure Wi-Fi with auto-reconnect
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(DEVICE_ID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  DisplayManager::showToast("Connecting WiFi...", 5000);
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
    wasConnected = true;
  }
  else
  {
    Serial.println(F("Wi-Fi pending. Auto-reconnect active in background."));
    wasConnected = false;
  }

  // 3. Setup mDNS
  setupMDNS();
}

void NetworkManager::update()
{
  MDNS.update();

  // Autonomous Wi-Fi Reconnection & mDNS Recovery Tracker
  bool isConnectedNow = (WiFi.status() == WL_CONNECTED);

  if (isConnectedNow != wasConnected)
  {
    wasConnected = isConnectedNow;
    if (isConnectedNow)
    {
      Serial.print(F("Wi-Fi Reconnected! IP: "));
      Serial.println(WiFi.localIP());
      updateMDNS();
      DisplayManager::showToast("WiFi Connected", 2000);
    }
    else
    {
      Serial.println(F("Wi-Fi connection lost. Waiting for auto-reconnect..."));
      DisplayManager::update();
    }
  }
}

bool NetworkManager::isConnected()
{
  return (WiFi.status() == WL_CONNECTED);
}

IPAddress NetworkManager::getLocalIP()
{
  return WiFi.localIP();
}

int32_t NetworkManager::getRSSI()
{
  return WiFi.RSSI();
}

String NetworkManager::getSSID()
{
  return WiFi.SSID();
}

String NetworkManager::getMAC()
{
  return WiFi.macAddress();
}
