#include "RelayController.h"
#include "DisplayManager.h"

bool RelayController::r1State = false;
bool RelayController::r2State = false;

void RelayController::init()
{
  // Hardware Glitch Suppression: Set state LOW BEFORE pinMode OUTPUT
  // Eliminates sub-microsecond floating glitches on transistor bases
  digitalWrite(RELAY1_PIN, LOW);
  pinMode(RELAY1_PIN, OUTPUT);
  digitalWrite(RELAY2_PIN, LOW);
  pinMode(RELAY2_PIN, OUTPUT);
}

void RelayController::setRelays(bool relay1, bool relay2, const char *toastMsg)
{
  r1State = relay1;
  r2State = relay2;

  digitalWrite(RELAY1_PIN, r1State ? HIGH : LOW);
  digitalWrite(RELAY2_PIN, r2State ? HIGH : LOW);

  if (toastMsg)
  {
    DisplayManager::showToast(toastMsg);
  }
  else
  {
    DisplayManager::update();
  }

  Serial.printf("Relay Change -> SW1: %s | SW2: %s\n",
                r1State ? "ON " : "OFF", r2State ? "ON " : "OFF");
}

void RelayController::setRelay1(bool state)
{
  setRelays(state, r2State);
}

void RelayController::setRelay2(bool state)
{
  setRelays(r1State, state);
}

void RelayController::setBoth(bool state)
{
  setRelays(state, state);
}

bool RelayController::getRelay1()
{
  return r1State;
}

bool RelayController::getRelay2()
{
  return r2State;
}
