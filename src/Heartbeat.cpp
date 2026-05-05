#include <Arduino.h>

#include "AppConfig.h"
#include "Heartbeat.h"

namespace {

bool ledState = false;
unsigned long lastToggleMs = 0;
constexpr unsigned long kHeartbeatIntervalMs = 500;

}  // namespace

void initializeHeartbeat() {
  pinMode(AppConfig::kHeartbeatLedPin, OUTPUT);
  ledState = false;
  digitalWrite(AppConfig::kHeartbeatLedPin, LOW);
  lastToggleMs = millis();
}

void updateHeartbeat() {
  const unsigned long now = millis();
  if (now - lastToggleMs < kHeartbeatIntervalMs) {
    return;
  }

  lastToggleMs = now;
  ledState = !ledState;
  digitalWrite(AppConfig::kHeartbeatLedPin, ledState ? HIGH : LOW);
}

bool isHeartbeatLedOn() { return ledState; }

unsigned long getHeartbeatIntervalMs() { return kHeartbeatIntervalMs; }
