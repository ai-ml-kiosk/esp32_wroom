#include <Arduino.h>

#include "AppConfig.h"
#include "PowerMonitor.h"

namespace {

bool powerSenseConfigured = false;
bool powerVoltageAvailable = false;
bool hasStableReading = false;
unsigned long lastSampleMs = 0;
uint32_t measuredPowerMilliVolts = 0;

bool isAdc1Pin(const int pin) {
  switch (pin) {
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 39:
      return true;
    default:
      return false;
  }
}

String formatVoltageLabel(const uint32_t milliVolts, const bool compact) {
  char buffer[20];
  const unsigned long wholeVolts = milliVolts / 1000;
  const unsigned long hundredths = (milliVolts % 1000) / 10;
  snprintf(buffer, sizeof(buffer), compact ? "%lu.%02luV" : "%lu.%02lu V",
           wholeVolts, hundredths);
  return String(buffer);
}

void setUnavailable() {
  powerVoltageAvailable = false;
}

void samplePowerVoltage() {
  if (!powerSenseConfigured) {
    setUnavailable();
    return;
  }

  uint64_t totalMilliVolts = 0;
  uint32_t minMilliVolts = UINT32_MAX;
  uint32_t maxMilliVolts = 0;

  for (uint8_t sampleIndex = 0; sampleIndex < AppConfig::kPowerSampleCount;
       ++sampleIndex) {
    const uint32_t sampleMilliVolts =
        analogReadMilliVolts(AppConfig::kPowerSensePin);
    totalMilliVolts += sampleMilliVolts;
    if (sampleMilliVolts < minMilliVolts) {
      minMilliVolts = sampleMilliVolts;
    }
    if (sampleMilliVolts > maxMilliVolts) {
      maxMilliVolts = sampleMilliVolts;
    }
    delay(2);
  }

  if (minMilliVolts == UINT32_MAX) {
    setUnavailable();
    return;
  }

  const uint32_t averageMilliVolts =
      totalMilliVolts / AppConfig::kPowerSampleCount;
  const uint32_t spreadMilliVolts = maxMilliVolts - minMilliVolts;

  if (spreadMilliVolts > AppConfig::kPowerStableWindowMilliVolts &&
      !hasStableReading) {
    setUnavailable();
    return;
  }

  const float scaledMilliVoltsFloat =
      (static_cast<float>(averageMilliVolts) *
       AppConfig::kPowerSenseDividerRatio) +
      static_cast<float>(AppConfig::kPowerSenseOffsetMilliVolts);
  const int32_t scaledMilliVolts =
      static_cast<int32_t>(scaledMilliVoltsFloat >= 0.0f
                               ? scaledMilliVoltsFloat + 0.5f
                               : scaledMilliVoltsFloat - 0.5f);

  if (scaledMilliVolts <= 0) {
    setUnavailable();
    return;
  }

  if (spreadMilliVolts <= AppConfig::kPowerStableWindowMilliVolts) {
    if (hasStableReading) {
      measuredPowerMilliVolts =
          (measuredPowerMilliVolts * 3u + static_cast<uint32_t>(scaledMilliVolts)) /
          4u;
    } else {
      measuredPowerMilliVolts = static_cast<uint32_t>(scaledMilliVolts);
    }
    hasStableReading = true;
  }

  powerVoltageAvailable = hasStableReading;
}

}  // namespace

void initializePowerMonitor() {
  lastSampleMs = 0;
  measuredPowerMilliVolts = 0;
  hasStableReading = false;
  powerVoltageAvailable = false;
  powerSenseConfigured = false;

  if (AppConfig::kPowerSensePin < 0) {
    Serial.println(
        "Power monitor disabled. Set APP_POWER_SENSE_PIN to an ADC1 pin to "
        "enable board-voltage sensing.");
    return;
  }

  if (!isAdc1Pin(AppConfig::kPowerSensePin)) {
    Serial.printf(
        "Power monitor disabled because GPIO%d is not an ADC1 input.\n",
        AppConfig::kPowerSensePin);
    return;
  }

  pinMode(AppConfig::kPowerSensePin, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(AppConfig::kPowerSensePin, ADC_11db);
  powerSenseConfigured = true;

  Serial.printf(
      "Power monitor enabled on GPIO%d with divider ratio %.3f and offset "
      "%ld mV.\n",
      AppConfig::kPowerSensePin, AppConfig::kPowerSenseDividerRatio,
      static_cast<long>(AppConfig::kPowerSenseOffsetMilliVolts));

  samplePowerVoltage();
  lastSampleMs = millis();
}

void updatePowerMonitor() {
  if (!powerSenseConfigured) {
    return;
  }

  const unsigned long nowMs = millis();
  if (lastSampleMs != 0 &&
      nowMs - lastSampleMs < AppConfig::kPowerSampleIntervalMs) {
    return;
  }

  lastSampleMs = nowMs;
  samplePowerVoltage();
}

bool isPowerSenseConfigured() { return powerSenseConfigured; }

bool isPowerVoltageAvailable() { return powerVoltageAvailable; }

uint32_t getPowerVoltageMilliVolts() {
  if (!powerVoltageAvailable) {
    return 0;
  }

  return measuredPowerMilliVolts;
}

float getPowerVoltageVolts() {
  if (!powerVoltageAvailable) {
    return 0.0f;
  }

  return static_cast<float>(measuredPowerMilliVolts) / 1000.0f;
}

String getPowerVoltageLabel() {
  if (!powerVoltageAvailable) {
    return powerSenseConfigured ? "Unavailable" : "Not configured";
  }

  return formatVoltageLabel(measuredPowerMilliVolts, false);
}

String getPowerVoltageDisplayLine() {
  if (!powerSenseConfigured) {
    return "V: unavailable";
  }

  if (!powerVoltageAvailable) {
    return "V: stabilizing";
  }

  return "V: " + formatVoltageLabel(measuredPowerMilliVolts, true);
}
