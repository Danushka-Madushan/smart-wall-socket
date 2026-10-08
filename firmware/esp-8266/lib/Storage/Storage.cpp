#include "Storage.h"
#include <EEPROM.h>

DeviceStorage StorageManager::deviceState;

uint32_t StorageManager::calculateChecksum(const DeviceStorage &storage)
{
  uint32_t sum = storage.magic ^ (storage.claimed ? 0xAA55AA55 : 0x55AA55AA);
  for (size_t i = 0; i < sizeof(storage.masterToken); i++)
  {
    sum = ((sum << 5) | (sum >> 27)) ^ (uint8_t)storage.masterToken[i];
  }
  return sum;
}

void StorageManager::init()
{
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, deviceState);

  uint32_t expectedChecksum = calculateChecksum(deviceState);

  if (deviceState.magic != EEPROM_MAGIC || deviceState.checksum != expectedChecksum)
  {
    Serial.println(F("EEPROM uninitialized or checksum mismatch. Applying factory default."));
    reset();
  }
  else
  {
    Serial.printf("Config loaded cleanly: Status=%s\n",
                  deviceState.claimed ? "CLAIMED" : "UNCLAIMED");
  }
}

void StorageManager::save()
{
  deviceState.checksum = calculateChecksum(deviceState);
  EEPROM.put(0, deviceState);
  EEPROM.commit();
}

bool StorageManager::isClaimed()
{
  return deviceState.claimed;
}

const char *StorageManager::getMasterToken()
{
  return deviceState.masterToken;
}

void StorageManager::setClaimed(const char *masterToken)
{
  deviceState.magic = EEPROM_MAGIC;
  deviceState.claimed = true;
  strncpy(deviceState.masterToken, masterToken, sizeof(deviceState.masterToken) - 1);
  deviceState.masterToken[sizeof(deviceState.masterToken) - 1] = '\0';
  save();
}

void StorageManager::reset()
{
  deviceState.magic = EEPROM_MAGIC;
  deviceState.claimed = false;
  memset(deviceState.masterToken, 0, sizeof(deviceState.masterToken));
  save();
}
