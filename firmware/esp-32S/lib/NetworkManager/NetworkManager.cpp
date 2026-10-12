#include "NetworkManager.h"
#include "StorageManager.h"
#include "DisplayManager.h"
#include <WiFiProv.h>

bool NetworkManager::wasConnected = false;
bool NetworkManager::isProvisioning = false;
unsigned long NetworkManager::bootTime = 0;

void NetworkManager::onSysProvEvent(arduino_event_t *sys_event) {
  switch (sys_event->event_id) {
    case ARDUINO_EVENT_PROV_START:
      isProvisioning = true;
      DisplayManager::setMode(DISP_MODE_PROVISIONING);
      DisplayManager::setProvStatus("Waiting for app...");
      Serial.println(F("[PROV] Provisioning started with BLE scheme"));
      Serial.printf("[PROV] Device Name: %s | PoP PIN: %s\n", getDeviceId(), POP_SECRET);
      Serial.printf("[PROV] Compact QR Payload: v1;%s;%s;%s\n", getDeviceId(), POP_SECRET, TRANSPORT);
      Serial.printf("[PROV] Legacy JSON (fallback): {\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\"}\n",
                    getDeviceId(), POP_SECRET, TRANSPORT);
      break;

    case ARDUINO_EVENT_PROV_CRED_RECV:
      DisplayManager::setProvStatus("Creds received...");
      DisplayManager::showToast("RECV WIFI CREDS", 2000);
      Serial.println(F("[PROV] Received Wi-Fi credentials via BLE"));
      break;

    case ARDUINO_EVENT_PROV_CRED_FAIL:
      DisplayManager::setProvStatus("Auth Failed!");
      DisplayManager::showToast("WIFI AUTH FAIL", 3000);
      Serial.println(F("[PROV] Wi-Fi authentication failed with given credentials"));
      break;

    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
      DisplayManager::setProvStatus("Joining Wi-Fi...");
      Serial.println(F("[PROV] Wi-Fi credentials validated successfully"));
      break;

    case ARDUINO_EVENT_PROV_END:
      isProvisioning = false;
      Serial.println(F("[PROV] Provisioning finished. BLE stack released."));
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      isProvisioning = false;
      wasConnected = true;
      DisplayManager::setMode(DISP_MODE_OPERATIONAL);
      DisplayManager::showToast("WIFI CONNECTED", 2000);
      Serial.print(F("[WIFI] Connected! IP: "));
      Serial.println(WiFi.localIP());
      setupMDNS();
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println(F("[WIFI] Wi-Fi disconnected"));
      if (!isProvisioning) {
        WiFi.reconnect();
      }
      break;

    default:
      break;
  }
}

void NetworkManager::setupMDNS() {
  MDNS.end();
  if (MDNS.begin("smartsocket")) {
    MDNS.setInstanceName(getDeviceId());
    MDNS.addService("http", "tcp", 80);
    updateMDNS();
    Serial.println(F("[mDNS] Responder started: smartsocket.local"));
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
  WiFi.onEvent(onSysProvEvent);

  bootTime = millis();

  Serial.println(F("[NETWORK] Initializing WiFiProv BLE subsystem..."));

  // Check if provisioned already or start BLE provisioning
  WiFiProv.beginProvision(
    WIFI_PROV_SCHEME_BLE,
    WIFI_PROV_SCHEME_HANDLER_FREE_BTDM,
    WIFI_PROV_SECURITY_1,
    POP_SECRET,
    getDeviceId(),
    NULL,
    NULL,
    false // Do not wipe previous provisioned credentials automatically
  );

  // If already connected immediately from previous credentials
  if (WiFi.status() == WL_CONNECTED) {
    wasConnected = true;
    DisplayManager::setMode(DISP_MODE_OPERATIONAL);
    setupMDNS();
    Serial.print(F("[WIFI] Reconnected from stored NVS! IP: "));
    Serial.println(WiFi.localIP());
  }
}

void NetworkManager::update() {
  bool isConnectedNow = (WiFi.status() == WL_CONNECTED);

  if (isConnectedNow != wasConnected) {
    wasConnected = isConnectedNow;
    if (isConnectedNow) {
      Serial.print(F("[WIFI] Reconnected! IP: "));
      Serial.println(WiFi.localIP());
      setupMDNS();
      DisplayManager::setMode(DISP_MODE_OPERATIONAL);
      DisplayManager::showToast("WiFi Connected", 2000);
    } else {
      Serial.println(F("[WIFI] Connection lost. Reconnecting..."));
    }
  }

  // Autonomous Wi-Fi Connection Timeout Watchdog (prevents unprovisioned dead-ends)
  if (!isConnectedNow && !isProvisioning) {
    unsigned long elapsed = millis() - bootTime;

    // For UNCLAIMED sockets: If cannot connect to stored Wi-Fi within 15 seconds,
    // credentials in NVS are stale/invalid. Auto-reset to start BLE pairing mode.
    if (!StorageManager::isClaimed() && elapsed > 15000) {
      Serial.println(F("[NETWORK] Watchdog: Unclaimed socket cannot reach saved Wi-Fi (15s timeout)."));
      Serial.println(F("[NETWORK] Wiping stale NVS credentials and falling back to BLE Provisioning mode..."));
      DisplayManager::showToast("FALLBACK -> BLE", 3000);
      resetProvisioning();
      return;
    }

    // For CLAIMED sockets: Allow up to 60 seconds (covers home router reboot grace period)
    if (StorageManager::isClaimed() && elapsed > 60000) {
      Serial.println(F("[NETWORK] Watchdog: Claimed socket cannot reach Wi-Fi for 60s."));
      Serial.println(F("[NETWORK] Reverting to BLE Provisioning mode for user reconfiguration..."));
      DisplayManager::showToast("WIFI FAIL -> BLE", 3000);
      resetProvisioning();
      return;
    }
  }
}

void NetworkManager::resetProvisioning() {
  Serial.println(F("[NETWORK] Resetting provisioned Wi-Fi and rebooting..."));
  wifi_prov_mgr_reset_provisioning();
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

bool NetworkManager::isProvisioningActive() {
  return isProvisioning;
}
