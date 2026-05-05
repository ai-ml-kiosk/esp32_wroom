#pragma once

#include <Arduino.h>

void initializeHeartbeat();
void updateHeartbeat();
bool isHeartbeatLedOn();
unsigned long getHeartbeatIntervalMs();
