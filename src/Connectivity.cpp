#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

#include "AppConfig.h"
#include "Connectivity.h"

namespace {

bool accessPointActive = false;
String accessPointSsid;
unsigned long lastReconnectAttemptMs = 0;
unsigned long stationConnectedSinceMs = 0;
unsigned long ipAssignmentChangeRequestedAtMs = 0;
bool stationConnectedLogged = false;
wl_status_t lastLoggedStatus = WL_IDLE_STATUS;
uint8_t lastDisconnectReason = 0;
bool wifiEventsRegistered = false;
bool staticStationIpEnabled = AppConfig::kUseStaticStationIp;
bool ipAssignmentChangePending = false;
Preferences preferences;
bool preferencesInitialized = false;
bool preferencesAvailable = false;

constexpr char kPreferencesNamespace[] = "status-net";
constexpr char kStaticIpPreferenceKey[] = "static-ip";

String buildFallbackApSsid() {
  const uint64_t chipId = ESP.getEfuseMac();
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06llX",
           static_cast<unsigned long long>(chipId & 0xFFFFFFULL));

  return String(AppConfig::kFallbackApSsidPrefix) + "-" + suffix;
}

String connectionStatusToString(const wl_status_t status) {
  switch (status) {
    case WL_CONNECTED:
      return "Connected";
    case WL_IDLE_STATUS:
      return "Idle";
    case WL_NO_SSID_AVAIL:
      return "SSID not available";
    case WL_SCAN_COMPLETED:
      return "Scan completed";
    case WL_CONNECT_FAILED:
      return "Connect failed";
    case WL_CONNECTION_LOST:
      return "Connection lost";
    case WL_DISCONNECTED:
      return "Disconnected";
    default:
      return "Unknown";
  }
}

void logConnectedStation() {
  if (stationConnectedLogged) {
    return;
  }

  Serial.print("Wi-Fi connected. IP address: ");
  Serial.println(WiFi.localIP());
  stationConnectedLogged = true;
}

void markStatusLogged(const wl_status_t status) { lastLoggedStatus = status; }

void ensurePreferencesReady() {
  if (preferencesInitialized) {
    return;
  }

  preferencesInitialized = true;
  if (!preferences.begin(kPreferencesNamespace, false)) {
    Serial.println(
        "Preferences unavailable. Falling back to the built-in station IP mode.");
    return;
  }

  preferencesAvailable = true;
  staticStationIpEnabled =
      preferences.getBool(kStaticIpPreferenceKey, AppConfig::kUseStaticStationIp);
  Serial.printf("Station IP mode preference loaded: %s.\n",
                staticStationIpEnabled ? "fixed" : "dynamic");
}

void handleWiFiEvent(const WiFiEvent_t event, const WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("Wi-Fi station obtained IP address %s.\n",
                    WiFi.localIP().toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      lastDisconnectReason = info.wifi_sta_disconnected.reason;
      stationConnectedLogged = false;
      stationConnectedSinceMs = 0;
      Serial.printf("Wi-Fi station disconnected (reason %u).\n",
                    lastDisconnectReason);
      break;
    case ARDUINO_EVENT_WIFI_AP_START:
      Serial.printf("Fallback access point active at %s.\n",
                    WiFi.softAPIP().toString().c_str());
      break;
    default:
      break;
  }
}

void ensureWiFiConfigured() {
  ensurePreferencesReady();

  if (!wifiEventsRegistered) {
    WiFi.onEvent(handleWiFiEvent);
    wifiEventsRegistered = true;
  }

  WiFi.persistent(false);
}

void ensureFallbackAccessPoint() {
  if (accessPointActive) {
    return;
  }

  accessPointSsid = buildFallbackApSsid();
  WiFi.mode(WIFI_AP_STA);
  accessPointActive =
      WiFi.softAP(accessPointSsid.c_str(), AppConfig::kFallbackApPassword);

  if (accessPointActive) {
    Serial.printf(
        "Fallback access point \"%s\" started. Join this SSID, then open http://%s/ or https://%s/.\n",
        accessPointSsid.c_str(), WiFi.softAPIP().toString().c_str(),
        WiFi.softAPIP().toString().c_str());
  } else {
    accessPointSsid = "";
    Serial.println("Fallback access point failed to start.");
  }
}

void disableFallbackAccessPoint() {
  if (!accessPointActive) {
    return;
  }

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  accessPointActive = false;
  accessPointSsid = "";
  Serial.println(
      "Fallback access point stopped after station connectivity recovered.");
}

bool configureStationNetwork() {
  if (!staticStationIpEnabled) {
    const IPAddress dhcpReset(0, 0, 0, 0);
    const bool configured = WiFi.config(dhcpReset, dhcpReset, dhcpReset);
    if (!configured) {
      Serial.println("Failed to restore dynamic station IP via DHCP.");
      return false;
    }

    Serial.println("Using dynamic station IP via DHCP.");
    return true;
  }

  const bool configured = WiFi.config(
      AppConfig::kStationStaticIp, AppConfig::kStationGateway,
      AppConfig::kStationSubnet, AppConfig::kStationPrimaryDns,
      AppConfig::kStationSecondaryDns);
  if (!configured) {
    Serial.println("Failed to apply the fixed station IP configuration.");
    return false;
  }

  Serial.printf(
      "Using fixed station IP %s with gateway %s and subnet %s.\n",
      AppConfig::kStationStaticIp.toString().c_str(),
      AppConfig::kStationGateway.toString().c_str(),
      AppConfig::kStationSubnet.toString().c_str());
  return true;
}

void beginStationConnectionAttempt(const bool logAttempt) {
  const bool keepFallbackAccessPoint = accessPointActive;

  WiFi.mode(keepFallbackAccessPoint ? WIFI_AP_STA : WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.setHostname(AppConfig::kStatusHostName);
  WiFi.disconnect(false, false);
  delay(100);
  configureStationNetwork();

  if (logAttempt) {
    Serial.printf("Connecting to Wi-Fi SSID \"%s\"...\n", AppConfig::kWiFiSsid);
  } else {
    Serial.printf("Retrying Wi-Fi SSID \"%s\" while fallback AP remains available...\n",
                  AppConfig::kWiFiSsid);
  }

  WiFi.begin(AppConfig::kWiFiSsid, AppConfig::kWiFiPassword);
  lastReconnectAttemptMs = millis();
  lastDisconnectReason = 0;
  stationConnectedLogged = false;
  markStatusLogged(WiFi.status());
}

}  // namespace

bool connectWiFi() {
  accessPointActive = false;
  accessPointSsid = "";
  lastReconnectAttemptMs = 0;
  stationConnectedSinceMs = 0;
  ipAssignmentChangeRequestedAtMs = 0;
  stationConnectedLogged = false;
  lastDisconnectReason = 0;
  ipAssignmentChangePending = false;
  markStatusLogged(WL_IDLE_STATUS);

  ensureWiFiConfigured();
  WiFi.mode(WIFI_OFF);
  delay(100);

  if (AppConfig::kPowerStabilizationDelayMs > 0) {
    Serial.printf(
        "Waiting %lu ms for power and Wi-Fi stabilization before networking starts...\n",
        AppConfig::kPowerStabilizationDelayMs);
    delay(AppConfig::kPowerStabilizationDelayMs);
  }

  Serial.println(
      "Starting fallback access point immediately so the board stays reachable while station Wi-Fi is connecting...");
  ensureFallbackAccessPoint();
  beginStationConnectionAttempt(true);
  return true;
}

void updateConnectivity() {
  if (ipAssignmentChangePending &&
      millis() - ipAssignmentChangeRequestedAtMs >= 500) {
    ipAssignmentChangePending = false;
    lastReconnectAttemptMs = 0;
    lastDisconnectReason = 0;
    stationConnectedSinceMs = 0;
    stationConnectedLogged = false;
    Serial.printf("Applying %s station IP mode. Reconnecting Wi-Fi...\n",
                  staticStationIpEnabled ? "fixed" : "dynamic");
    beginStationConnectionAttempt(true);
    return;
  }

  const wl_status_t currentStatus = WiFi.status();
  if (currentStatus != lastLoggedStatus) {
    Serial.printf("Wi-Fi station status: %s\n",
                  connectionStatusToString(currentStatus).c_str());
    markStatusLogged(currentStatus);
  }

  if (currentStatus == WL_CONNECTED) {
    if (stationConnectedSinceMs == 0) {
      stationConnectedSinceMs = millis();
    }

    logConnectedStation();

    if (accessPointActive &&
        millis() - stationConnectedSinceMs >=
            AppConfig::kFallbackApShutdownDelayMs) {
      disableFallbackAccessPoint();
    }

    return;
  }

  stationConnectedSinceMs = 0;

  if (!accessPointActive) {
    ensureFallbackAccessPoint();
  }

  if (lastReconnectAttemptMs != 0 &&
      millis() - lastReconnectAttemptMs < AppConfig::kWiFiRetryIntervalMs) {
    return;
  }

  beginStationConnectionAttempt(false);
}

bool isStationConnected() { return WiFi.status() == WL_CONNECTED; }

bool isAccessPointActive() {
  if (!accessPointActive) {
    return false;
  }

  const wifi_mode_t mode = WiFi.getMode();
  return mode == WIFI_AP || mode == WIFI_AP_STA;
}

bool isNetworkReady() { return isStationConnected() || isAccessPointActive(); }

String getNetworkModeName() {
  if (isStationConnected()) {
    return "Wi-Fi client";
  }

  if (isAccessPointActive()) {
    return "Fallback access point";
  }

  return "Offline";
}

String getNetworkName() {
  if (isStationConnected()) {
    return WiFi.SSID();
  }

  if (isAccessPointActive()) {
    return accessPointSsid;
  }

  return "Unavailable";
}

String getAccessPointName() {
  if (isAccessPointActive()) {
    return accessPointSsid;
  }

  return "Unavailable";
}

bool isStaticStationIpEnabled() { return staticStationIpEnabled; }

bool isIpAssignmentChangePending() { return ipAssignmentChangePending; }

bool setStaticStationIpEnabled(const bool enabled) {
  ensurePreferencesReady();

  if (staticStationIpEnabled == enabled && !ipAssignmentChangePending) {
    return true;
  }

  staticStationIpEnabled = enabled;
  ipAssignmentChangePending = true;
  ipAssignmentChangeRequestedAtMs = millis();

  if (preferencesAvailable) {
    preferences.putBool(kStaticIpPreferenceKey, enabled);
  }

  Serial.printf(
      "Station IP mode change requested from the dashboard: %s.\n",
      enabled ? "fixed" : "dynamic");
  return true;
}

String getIpAssignmentMode() {
  if (isStationConnected()) {
    return staticStationIpEnabled ? "Fixed" : "Dynamic";
  }

  if (isAccessPointActive()) {
    return "AP local";
  }

  return staticStationIpEnabled ? "Fixed" : "Dynamic";
}

String getIpAddress() {
  if (isStationConnected()) {
    return WiFi.localIP().toString();
  }

  if (isAccessPointActive()) {
    return WiFi.softAPIP().toString();
  }

  return "Unavailable";
}

String getMacAddress() {
  if (isAccessPointActive() && !isStationConnected()) {
    return WiFi.softAPmacAddress();
  }

  return WiFi.macAddress();
}

long getSignalStrengthDbm() {
  if (!isStationConnected()) {
    return 0;
  }

  return WiFi.RSSI();
}

String getConnectionStatusText() {
  if (isAccessPointActive() && !isStationConnected()) {
    String status = connectionStatusToString(WiFi.status());
    if (lastDisconnectReason != 0) {
      status += " (reason ";
      status += String(lastDisconnectReason);
      status += ")";
    }

    return "AP active, STA " + status;
  }

  String status = connectionStatusToString(WiFi.status());
  if (!isStationConnected() && lastDisconnectReason != 0) {
    status += " (reason ";
    status += String(lastDisconnectReason);
    status += ")";
  }

  return status;
}
