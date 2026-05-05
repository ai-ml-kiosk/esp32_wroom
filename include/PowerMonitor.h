#pragma once

#include <Arduino.h>

void initializePowerMonitor();
void updatePowerMonitor();
bool isPowerSenseConfigured();
bool isPowerVoltageAvailable();
uint32_t getPowerVoltageMilliVolts();
float getPowerVoltageVolts();
String getPowerVoltageLabel();
String getPowerVoltageDisplayLine();
