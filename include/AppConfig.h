#pragma once

#include <Arduino.h>
#include <IPAddress.h>

#ifndef APP_WIFI_SSID
#define APP_WIFI_SSID "change-me-ssid"
#endif

#ifndef APP_WIFI_PASSWORD
#define APP_WIFI_PASSWORD "change-me-password"
#endif

#ifndef APP_FALLBACK_AP_SSID_PREFIX
#define APP_FALLBACK_AP_SSID_PREFIX "esp32-status"
#endif

#ifndef APP_FALLBACK_AP_PASSWORD
#define APP_FALLBACK_AP_PASSWORD "change-me-ap"
#endif

#ifndef APP_STATUS_HOST_NAME
#define APP_STATUS_HOST_NAME "esp32-status"
#endif

#ifndef APP_STATUS_HOST_NAME_APPEND_MAC
#define APP_STATUS_HOST_NAME_APPEND_MAC true
#endif

#ifndef APP_POWER_SENSE_PIN
#define APP_POWER_SENSE_PIN -1
#endif

#ifndef APP_POWER_SENSE_DIVIDER_RATIO
#define APP_POWER_SENSE_DIVIDER_RATIO 1.0f
#endif

#ifndef APP_POWER_SENSE_OFFSET_MV
#define APP_POWER_SENSE_OFFSET_MV 0
#endif

#ifndef APP_SD_CARD_ENABLED
#define APP_SD_CARD_ENABLED true
#endif

#ifndef APP_SD_CARD_CS_PIN
#define APP_SD_CARD_CS_PIN 5
#endif

#ifndef APP_SD_CARD_SCK_PIN
#define APP_SD_CARD_SCK_PIN 18
#endif

#ifndef APP_SD_CARD_MISO_PIN
#define APP_SD_CARD_MISO_PIN 19
#endif

#ifndef APP_SD_CARD_MOSI_PIN
#define APP_SD_CARD_MOSI_PIN 23
#endif

#ifndef APP_SD_CARD_SPI_FREQUENCY_HZ
#define APP_SD_CARD_SPI_FREQUENCY_HZ 4000000
#endif

namespace AppConfig {

constexpr char kWiFiSsid[] = APP_WIFI_SSID;
constexpr char kWiFiPassword[] = APP_WIFI_PASSWORD;
constexpr char kFallbackApSsidPrefix[] = APP_FALLBACK_AP_SSID_PREFIX;
constexpr char kFallbackApPassword[] = APP_FALLBACK_AP_PASSWORD;
constexpr char kStatusHostName[] = APP_STATUS_HOST_NAME;
constexpr bool kAppendMacToStatusHostName = APP_STATUS_HOST_NAME_APPEND_MAC;
constexpr char kDefaultTimeZoneId[] = "australia_sydney";
constexpr char kTimeZone[] = "AEST-10AEDT,M10.1.0/2,M4.1.0/3";
constexpr char kDefaultDateFormatId[] = "dd.mm.yyyy";
constexpr char kNtpServerPrimary[] = "pool.ntp.org";
constexpr char kNtpServerSecondary[] = "time.google.com";
constexpr char kNtpServerTertiary[] = "time.cloudflare.com";
constexpr bool kUseStaticStationIp = false;
const IPAddress kStationStaticIp(192, 168, 1, 176);
const IPAddress kStationGateway(192, 168, 1, 1);
const IPAddress kStationSubnet(255, 255, 255, 0);
const IPAddress kStationPrimaryDns(1, 1, 1, 1);
const IPAddress kStationSecondaryDns(8, 8, 8, 8);
constexpr uint16_t kStatusServerPort = 443;
constexpr unsigned long kStatusRefreshMs = 2000;
constexpr unsigned long kPowerStabilizationDelayMs = 2000;
constexpr unsigned long kWiFiConnectTimeoutMs = 15000;
constexpr unsigned long kWiFiRetryIntervalMs = 15000;
constexpr unsigned long kWiFiConnectPollMs = 250;
constexpr unsigned long kFallbackApShutdownDelayMs = 10000;
constexpr unsigned long kClockSyncRetryMs = 30000;
constexpr uint8_t kDisplaySclPin = 22;
constexpr uint8_t kDisplaySdaPin = 21;
constexpr uint8_t kDisplayPrimaryI2cAddress = 0x3C;
constexpr uint8_t kDisplaySecondaryI2cAddress = 0x3D;
constexpr unsigned long kDisplayRefreshMs = 1000;
constexpr unsigned long kDisplayPageDurationMs = 4000;
constexpr int kPowerSensePin = APP_POWER_SENSE_PIN;
constexpr float kPowerSenseDividerRatio = APP_POWER_SENSE_DIVIDER_RATIO;
constexpr int32_t kPowerSenseOffsetMilliVolts = APP_POWER_SENSE_OFFSET_MV;
constexpr unsigned long kPowerSampleIntervalMs = 2000;
constexpr uint8_t kPowerSampleCount = 8;
constexpr uint32_t kPowerStableWindowMilliVolts = 120;
constexpr bool kSDCardEnabled = APP_SD_CARD_ENABLED;
constexpr uint8_t kSDCardCsPin = APP_SD_CARD_CS_PIN;
constexpr uint8_t kSDCardSckPin = APP_SD_CARD_SCK_PIN;
constexpr uint8_t kSDCardMisoPin = APP_SD_CARD_MISO_PIN;
constexpr uint8_t kSDCardMosiPin = APP_SD_CARD_MOSI_PIN;
constexpr uint32_t kSDCardSpiFrequencyHz = APP_SD_CARD_SPI_FREQUENCY_HZ;
constexpr unsigned long kSDCardDetectIntervalMs = 5000;
constexpr size_t kSDCardMaxDirectoryEntries = 80;

#ifndef LED_BUILTIN
constexpr uint8_t kHeartbeatLedPin = 2;
#else
constexpr uint8_t kHeartbeatLedPin = LED_BUILTIN;
#endif

static_assert(sizeof(kFallbackApPassword) > 8,
              "Fallback AP password must be at least 8 characters long.");
static_assert(kPowerSenseDividerRatio > 0.0f,
              "Power sense divider ratio must be greater than zero.");
static_assert(kPowerSampleCount > 0,
              "Power sample count must be greater than zero.");
static_assert(kSDCardSpiFrequencyHz > 0,
              "SD card SPI frequency must be greater than zero.");

}  // namespace AppConfig
