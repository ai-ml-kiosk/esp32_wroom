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

namespace AppConfig {

constexpr char kWiFiSsid[] = APP_WIFI_SSID;
constexpr char kWiFiPassword[] = APP_WIFI_PASSWORD;
constexpr char kFallbackApSsidPrefix[] = APP_FALLBACK_AP_SSID_PREFIX;
constexpr char kFallbackApPassword[] = APP_FALLBACK_AP_PASSWORD;
constexpr char kStatusHostName[] = "esp32-status";
constexpr char kDefaultTimeZoneId[] = "australia_sydney";
constexpr char kTimeZone[] = "AEST-10AEDT,M10.1.0/2,M4.1.0/3";
constexpr char kDefaultDateFormatId[] = "dd-mon-yyyy";
constexpr char kNtpServerPrimary[] = "pool.ntp.org";
constexpr char kNtpServerSecondary[] = "time.google.com";
constexpr char kNtpServerTertiary[] = "time.cloudflare.com";
constexpr bool kUseStaticStationIp = true;
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

#ifndef LED_BUILTIN
constexpr uint8_t kHeartbeatLedPin = 2;
#else
constexpr uint8_t kHeartbeatLedPin = LED_BUILTIN;
#endif

static_assert(sizeof(kFallbackApPassword) > 8,
              "Fallback AP password must be at least 8 characters long.");

}  // namespace AppConfig
