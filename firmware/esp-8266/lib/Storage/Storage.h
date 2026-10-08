#pragma once
#include <Arduino.h>
#include "Config.h"

struct DeviceStorage
{
  uint32_t magic;
  bool claimed;
  char masterToken[65];
  uint32_t checksum;
};

class StorageManager
{
public:
  static void init();
  static void save();
  static bool isClaimed();
  static const char *getMasterToken();
  static void setClaimed(const char *masterToken);
  static void reset();

private:
  static DeviceStorage deviceState;
  static uint32_t calculateChecksum(const DeviceStorage &storage);
};
