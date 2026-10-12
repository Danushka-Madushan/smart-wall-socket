#include "StorageManager.h"
#include <Preferences.h>

static Preferences prefs;
bool StorageManager::claimed = false;
char StorageManager::cachedToken[65] = {0};

void StorageManager::init() {
  prefs.begin("socket", false);
  claimed = prefs.getBool("claimed", false);
  String t = prefs.getString("token", "");
  strncpy(cachedToken, t.c_str(), sizeof(cachedToken) - 1);
  cachedToken[sizeof(cachedToken) - 1] = '\0';
  prefs.end();

  Serial.printf("[STORAGE] Status=%s\n", claimed ? "CLAIMED" : "UNCLAIMED");
}

bool StorageManager::isClaimed() {
  return claimed;
}

const char *StorageManager::getMasterToken() {
  return cachedToken;
}

void StorageManager::setClaimed(const char *masterToken) {
  claimed = true;
  strncpy(cachedToken, masterToken, sizeof(cachedToken) - 1);
  cachedToken[sizeof(cachedToken) - 1] = '\0';

  prefs.begin("socket", false);
  prefs.putBool("claimed", true);
  prefs.putString("token", cachedToken);
  prefs.end();

  Serial.println(F("[STORAGE] Device state saved: CLAIMED"));
}

void StorageManager::reset() {
  claimed = false;
  memset(cachedToken, 0, sizeof(cachedToken));

  prefs.begin("socket", false);
  prefs.clear();
  prefs.end();

  Serial.println(F("[STORAGE] Device reverted to UNCLAIMED (factory reset)"));
}
