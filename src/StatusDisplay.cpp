#include <Arduino.h>
#include <time.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "AppConfig.h"
#include "Connectivity.h"
#include "Heartbeat.h"
#include "StatusDisplay.h"

namespace {

U8X8_SSD1306_128X64_NONAME_HW_I2C display(U8X8_PIN_NONE);

bool displayReady = false;
uint8_t activeDisplayAddress = 0;
unsigned long lastRefreshMs = 0;
unsigned long lastClockSyncAttemptMs = 0;
unsigned long lastSignalSampleMs = 0;
bool displayCacheValid = false;
String cachedRows[8];
String cachedDoubleHeightText;
uint8_t cachedDoubleHeightRow = UINT8_MAX;
String cachedHeaderDate;
String cachedSignalLine;
const uint8_t *cachedWiFiIcon = nullptr;
const uint8_t *cachedBatteryIcon = nullptr;

constexpr size_t kMaxColumns = 16;
constexpr uint8_t kDisplayRowCount = 8;
constexpr uint8_t kHeaderWifiColumn = 0;
constexpr uint8_t kHeaderDateColumn = 3;
constexpr uint8_t kHeaderBatteryColumn = 14;
constexpr uint8_t kTimeRow = 2;
constexpr uint8_t kAddressRow = 5;
constexpr time_t kMinimumValidEpoch = 946684800;
constexpr unsigned long kSignalRefreshMs = 5000;
constexpr unsigned long kAddressScrollRefreshMs = 350;
constexpr uint8_t kAddressScrollGapColumns = 3;

const uint8_t kWiFiIconReady[] = {
    0x00, 0x00, 0x04, 0x06, 0x12, 0x13, 0x09, 0x69,
    0x69, 0x09, 0x13, 0x12, 0x06, 0x04, 0x00, 0x00,
};

const uint8_t kWiFiIconOffline[] = {
    0x00, 0x00, 0x04, 0x02, 0x12, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x12, 0x02, 0x04, 0x00, 0x00,
};

const uint8_t kBatteryIconIdle[] = {
    0x7E, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
    0x42, 0x42, 0x42, 0x42, 0x7E, 0x18, 0x18, 0x00,
};

const uint8_t kBatteryIconActive[] = {
    0x7E, 0x42, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E,
    0x7E, 0x7E, 0x7E, 0x42, 0x7E, 0x18, 0x18, 0x00,
};

String getDateLine();

void resetDisplayCache() {
  displayCacheValid = false;
  cachedDoubleHeightText = "";
  cachedDoubleHeightRow = UINT8_MAX;
  cachedHeaderDate = "";
  cachedSignalLine = "";
  cachedWiFiIcon = nullptr;
  cachedBatteryIcon = nullptr;

  for (uint8_t row = 0; row < kDisplayRowCount; ++row) {
    cachedRows[row] = "";
  }
}

String fitText(const String &value, const size_t maxColumns) {
  if (value.length() <= maxColumns) {
    return value;
  }

  if (maxColumns <= 3) {
    return value.substring(0, maxColumns);
  }

  return value.substring(0, maxColumns - 3) + "...";
}

void drawLine(const uint8_t row, const String &text) {
  const String fitted = fitText(text, kMaxColumns);
  if (displayCacheValid && cachedRows[row] == fitted) {
    return;
  }

  display.clearLine(row);
  if (!fitted.isEmpty()) {
    display.drawString(0, row, fitted.c_str());
  }
  cachedRows[row] = fitted;
}

void drawRawLine(const uint8_t row, const String &text) {
  if (displayCacheValid && cachedRows[row] == text) {
    return;
  }

  display.clearLine(row);
  if (!text.isEmpty()) {
    display.drawString(0, row, text.c_str());
  }
  cachedRows[row] = text;
}

void drawCenteredLine(const uint8_t row, const String &text) {
  const String fitted = fitText(text, kMaxColumns);
  const uint8_t column = (kMaxColumns - fitted.length()) / 2;
  if (displayCacheValid && cachedRows[row] == fitted) {
    return;
  }

  display.clearLine(row);
  if (!fitted.isEmpty()) {
    display.drawString(column, row, fitted.c_str());
  }
  cachedRows[row] = fitted;
}

void drawDoubleHeightCenteredLine(const uint8_t row, const String &text) {
  const size_t maxCharacters = kMaxColumns / 2;
  String fitted = text;
  if (fitted.length() > maxCharacters) {
    fitted = fitted.substring(0, maxCharacters);
  }

  const uint8_t width = fitted.length() * 2;
  const uint8_t column = width < kMaxColumns ? (kMaxColumns - width) / 2 : 0;
  if (displayCacheValid && cachedDoubleHeightRow == row &&
      cachedDoubleHeightText == fitted) {
    return;
  }

  display.clearLine(row);
  display.clearLine(row + 1);
  if (!fitted.isEmpty()) {
    display.draw2x2String(column, row, fitted.c_str());
  }
  cachedDoubleHeightRow = row;
  cachedDoubleHeightText = fitted;
}

void drawTileIcon(const uint8_t column, const uint8_t row,
                  const uint8_t tileCount, const uint8_t *icon) {
  display.drawTile(column, row, tileCount, const_cast<uint8_t *>(icon));
}

const uint8_t *getWiFiIcon() {
  if (isNetworkReady()) {
    return kWiFiIconReady;
  }

  return kWiFiIconOffline;
}

const uint8_t *getBatteryIcon() {
  if (isHeartbeatLedOn()) {
    return kBatteryIconActive;
  }

  return kBatteryIconIdle;
}

void drawHeaderRow() {
  const uint8_t *wifiIcon = getWiFiIcon();
  const uint8_t *batteryIcon = getBatteryIcon();
  const String dateLine = getDateLine();

  if (displayCacheValid && cachedWiFiIcon == wifiIcon &&
      cachedBatteryIcon == batteryIcon && cachedHeaderDate == dateLine) {
    return;
  }

  display.clearLine(0);
  drawTileIcon(kHeaderWifiColumn, 0, 2, wifiIcon);
  display.drawString(kHeaderDateColumn, 0, dateLine.c_str());
  drawTileIcon(kHeaderBatteryColumn, 0, 2, batteryIcon);
  cachedWiFiIcon = wifiIcon;
  cachedBatteryIcon = batteryIcon;
  cachedHeaderDate = dateLine;
}

bool hasValidClock() {
  time_t now;
  time(&now);
  return now >= kMinimumValidEpoch;
}

void maybeStartClockSync() {
  if (!isStationConnected() || hasValidClock()) {
    return;
  }

  const unsigned long nowMs = millis();
  if (lastClockSyncAttemptMs != 0 &&
      nowMs - lastClockSyncAttemptMs < AppConfig::kClockSyncRetryMs) {
    return;
  }

  configTzTime(AppConfig::kTimeZone, AppConfig::kNtpServerPrimary,
               AppConfig::kNtpServerSecondary, AppConfig::kNtpServerTertiary);
  lastClockSyncAttemptMs = nowMs;
  Serial.printf("Starting NTP time sync using timezone %s.\n",
                AppConfig::kTimeZone);
}

bool getCurrentTimeInfo(struct tm *timeInfo) {
  if (!hasValidClock()) {
    return false;
  }

  return getLocalTime(timeInfo, 0);
}

String getTimeLine() {
  struct tm timeInfo;
  if (!getCurrentTimeInfo(&timeInfo)) {
    return "--:--";
  }

  char buffer[16];
  strftime(buffer, sizeof(buffer), "%H:%M", &timeInfo);
  return String(buffer);
}

String getDateLine() {
  struct tm timeInfo;
  if (!getCurrentTimeInfo(&timeInfo)) {
    return "-- --- ----";
  }

  char buffer[16];
  strftime(buffer, sizeof(buffer), "%d %b %Y", &timeInfo);
  String dateLine(buffer);
  dateLine.toUpperCase();
  return dateLine;
}

void drawCommonTopRows() {
  drawHeaderRow();
  drawLine(1, "");
  drawDoubleHeightCenteredLine(kTimeRow, getTimeLine());
}

bool respondsToI2cAddress(const uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

uint8_t detectDisplayAddress() {
  if (respondsToI2cAddress(AppConfig::kDisplayPrimaryI2cAddress)) {
    return AppConfig::kDisplayPrimaryI2cAddress;
  }

  if (respondsToI2cAddress(AppConfig::kDisplaySecondaryI2cAddress)) {
    return AppConfig::kDisplaySecondaryI2cAddress;
  }

  return 0;
}

String getIdentityLine() {
  if (isStationConnected()) {
    return "Net: " + getNetworkName();
  }

  if (isAccessPointActive()) {
    return "AP: " + getAccessPointName();
  }

  return "WiFi: starting";
}

String getAddressLine() {
  if (!isNetworkReady()) {
    return "waiting...";
  }

  return getIpAddress();
}

String getScrollingWindow(const String &value, const size_t width,
                          const unsigned long nowMs) {
  if (value.length() <= width) {
    return value;
  }

  String padded = value;
  for (uint8_t i = 0; i < kAddressScrollGapColumns; ++i) {
    padded += ' ';
  }

  const size_t offset =
      (nowMs / kAddressScrollRefreshMs) % padded.length();
  const String repeated = padded + padded;
  return repeated.substring(offset, offset + width);
}

bool isAddressLineScrolling() {
  return getAddressLine().length() > kMaxColumns;
}

void drawAddressLine(const uint8_t row, const String &text,
                     const unsigned long nowMs) {
  drawRawLine(row, getScrollingWindow(text, kMaxColumns, nowMs));
}

String getAssignmentLine() {
  return "Addr: " + getIpAssignmentMode();
}

String getSignalLine() {
  const unsigned long nowMs = millis();
  if (isStationConnected()) {
    if (cachedSignalLine.isEmpty() ||
        nowMs - lastSignalSampleMs >= kSignalRefreshMs) {
      cachedSignalLine = "RSSI: " + String(getSignalStrengthDbm()) + "dBm";
      lastSignalSampleMs = nowMs;
    }
    return cachedSignalLine;
  }

  if (isAccessPointActive()) {
    cachedSignalLine = "RSSI: AP mode";
    lastSignalSampleMs = nowMs;
    return cachedSignalLine;
  }

  cachedSignalLine = "RSSI: waiting";
  lastSignalSampleMs = nowMs;
  return cachedSignalLine;
}

void drawBootScreen() {
  display.clearDisplay();
  drawCommonTopRows();
  drawLine(4, "BOOTING");
  drawRawLine(kAddressRow, "Init display...");
  drawLine(6, "WiFi starting");
  drawLine(7, "");
  displayCacheValid = true;
}

void drawStatusPage(const unsigned long nowMs) {
  drawCommonTopRows();
  drawLine(4, getIdentityLine());
  drawAddressLine(kAddressRow, getAddressLine(), nowMs);
  drawLine(6, getAssignmentLine());
  drawLine(7, getSignalLine());
}

void refreshStatusDisplay(const unsigned long nowMs) {
  drawStatusPage(nowMs);
  displayCacheValid = true;
}

}  // namespace

void initializeStatusDisplay() {
  Wire.begin(AppConfig::kDisplaySdaPin, AppConfig::kDisplaySclPin);
  delay(20);

  activeDisplayAddress = detectDisplayAddress();
  if (activeDisplayAddress == 0) {
    Serial.printf(
        "Status display not detected on I2C addresses 0x%02X or 0x%02X.\n",
        AppConfig::kDisplayPrimaryI2cAddress,
        AppConfig::kDisplaySecondaryI2cAddress);
    return;
  }

  display.setI2CAddress(activeDisplayAddress << 1);
  display.begin();
  display.setPowerSave(0);
  display.setFont(u8x8_font_chroma48medium8_r);
  displayReady = true;
  lastRefreshMs = 0;
  lastSignalSampleMs = 0;
  resetDisplayCache();

  Serial.printf("Status display detected on I2C address 0x%02X using SDA=%u, SCL=%u.\n",
                activeDisplayAddress, AppConfig::kDisplaySdaPin,
                AppConfig::kDisplaySclPin);

  drawBootScreen();
}

void updateStatusDisplay() {
  if (!displayReady) {
    return;
  }

  maybeStartClockSync();

  const unsigned long now = millis();
  const unsigned long refreshIntervalMs =
      isAddressLineScrolling() ? kAddressScrollRefreshMs
                               : AppConfig::kDisplayRefreshMs;
  if (lastRefreshMs != 0 &&
      now - lastRefreshMs < refreshIntervalMs) {
    return;
  }

  lastRefreshMs = now;
  refreshStatusDisplay(now);
}

bool isStatusDisplayReady() { return displayReady; }
