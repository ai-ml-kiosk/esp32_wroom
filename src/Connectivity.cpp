#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

#include <vector>

#include "AppConfig.h"
#include "Connectivity.h"

namespace {

bool accessPointActive = false;
String accessPointSsid;
unsigned long lastReconnectAttemptMs = 0;
unsigned long stationConnectedSinceMs = 0;
unsigned long reconfigureRequestedAtMs = 0;
bool stationConnectedLogged = false;
wl_status_t lastLoggedStatus = WL_IDLE_STATUS;
uint8_t lastDisconnectReason = 0;
bool wifiEventsRegistered = false;
bool staticStationIpEnabled = AppConfig::kUseStaticStationIp;
bool ipAssignmentChangePending = false;
bool wifiReconnectPending = false;
String configuredStationSsid = AppConfig::kWiFiSsid;
String configuredStationPassword = AppConfig::kWiFiPassword;
StationIpConfig configuredStationIpConfig = {
    AppConfig::kStationStaticIp,
    AppConfig::kStationGateway,
    AppConfig::kStationSubnet,
    AppConfig::kStationPrimaryDns,
    AppConfig::kStationSecondaryDns,
};
Preferences preferences;
bool preferencesInitialized = false;
bool preferencesAvailable = false;

constexpr char kPreferencesNamespace[] = "status-net";
constexpr char kStaticIpPreferenceKey[] = "static-ip";
constexpr char kDynamicIpMigrationPreferenceKey[] = "ipmig-v1";
constexpr char kWiFiSsidPreferenceKey[] = "wifi-ssid";
constexpr char kWiFiPasswordPreferenceKey[] = "wifi-pass";
constexpr char kStationIpPreferenceKey[] = "sta-ip";
constexpr char kGatewayPreferenceKey[] = "sta-gw";
constexpr char kSubnetPreferenceKey[] = "sta-sub";
constexpr char kPrimaryDnsPreferenceKey[] = "sta-dns1";
constexpr char kSecondaryDnsPreferenceKey[] = "sta-dns2";
constexpr char kPlaceholderWiFiSsid[] = "change-me-ssid";
constexpr char kPlaceholderWiFiPassword[] = "change-me-password";

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

String wifiAuthModeToString(const wifi_auth_mode_t authMode) {
  switch (authMode) {
    case WIFI_AUTH_OPEN:
      return "Open";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA-PSK";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2-PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA/WPA2-PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "WPA2-Enterprise";
    case WIFI_AUTH_WPA3_PSK:
      return "WPA3-PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return "WPA2/WPA3-PSK";
    case WIFI_AUTH_WAPI_PSK:
      return "WAPI-PSK";
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

bool parseIpAddressOrFallback(const String &value, const IPAddress &fallback,
                              IPAddress *target) {
  if (target == nullptr) {
    return false;
  }

  if (value.length() == 0) {
    *target = fallback;
    return true;
  }

  IPAddress parsed;
  if (!parsed.fromString(value)) {
    *target = fallback;
    return false;
  }

  *target = parsed;
  return true;
}

bool isZeroAddress(const IPAddress &address) {
  return address[0] == 0 && address[1] == 0 && address[2] == 0 &&
         address[3] == 0;
}

bool stationIpConfigEquals(const StationIpConfig &lhs,
                           const StationIpConfig &rhs) {
  return lhs.address == rhs.address && lhs.gateway == rhs.gateway &&
         lhs.subnet == rhs.subnet && lhs.primaryDns == rhs.primaryDns &&
         lhs.secondaryDns == rhs.secondaryDns;
}

bool shouldReplacePlaceholderStationConfig(const String &ssid,
                                           const String &password) {
  return ssid == kPlaceholderWiFiSsid &&
         password == kPlaceholderWiFiPassword &&
         String(AppConfig::kWiFiSsid) != kPlaceholderWiFiSsid;
}

void migrateSavedIpModeToDynamicDefaultIfNeeded() {
  if (!preferencesAvailable || AppConfig::kUseStaticStationIp) {
    return;
  }

  if (preferences.getBool(kDynamicIpMigrationPreferenceKey, false)) {
    return;
  }

  const bool hadSavedIpMode = preferences.isKey(kStaticIpPreferenceKey);
  const bool savedFixedIpEnabled =
      preferences.getBool(kStaticIpPreferenceKey, AppConfig::kUseStaticStationIp);

  if (hadSavedIpMode && savedFixedIpEnabled) {
    preferences.putBool(kStaticIpPreferenceKey, false);
    Serial.println(
        "Migrated saved station IP mode from fixed to dynamic for this firmware.");
  }

  preferences.putBool(kDynamicIpMigrationPreferenceKey, true);
}

void loadSavedStationConfig() {
  if (!preferencesAvailable) {
    return;
  }

  configuredStationSsid = AppConfig::kWiFiSsid;
  configuredStationPassword = AppConfig::kWiFiPassword;

  if (preferences.isKey(kWiFiSsidPreferenceKey)) {
    configuredStationSsid = preferences.getString(kWiFiSsidPreferenceKey, "");
  }

  // Distinguish a missing password from a deliberately blank one for open Wi-Fi.
  if (preferences.isKey(kWiFiPasswordPreferenceKey)) {
    configuredStationPassword =
        preferences.getString(kWiFiPasswordPreferenceKey, "");
  }

  if (configuredStationSsid.length() == 0) {
    configuredStationSsid = AppConfig::kWiFiSsid;
    configuredStationPassword = AppConfig::kWiFiPassword;
  }

  if (shouldReplacePlaceholderStationConfig(configuredStationSsid,
                                            configuredStationPassword)) {
    configuredStationSsid = AppConfig::kWiFiSsid;
    configuredStationPassword = AppConfig::kWiFiPassword;
    preferences.putString(kWiFiSsidPreferenceKey, configuredStationSsid);
    preferences.putString(kWiFiPasswordPreferenceKey,
                          configuredStationPassword);
    Serial.println(
        "Replaced placeholder saved Wi-Fi credentials with the current build configuration.");
  }

  migrateSavedIpModeToDynamicDefaultIfNeeded();

  staticStationIpEnabled =
      preferences.getBool(kStaticIpPreferenceKey, AppConfig::kUseStaticStationIp);

  parseIpAddressOrFallback(
      preferences.getString(kStationIpPreferenceKey,
                            AppConfig::kStationStaticIp.toString()),
      AppConfig::kStationStaticIp, &configuredStationIpConfig.address);
  parseIpAddressOrFallback(
      preferences.getString(kGatewayPreferenceKey,
                            AppConfig::kStationGateway.toString()),
      AppConfig::kStationGateway, &configuredStationIpConfig.gateway);
  parseIpAddressOrFallback(
      preferences.getString(kSubnetPreferenceKey,
                            AppConfig::kStationSubnet.toString()),
      AppConfig::kStationSubnet, &configuredStationIpConfig.subnet);
  parseIpAddressOrFallback(
      preferences.getString(kPrimaryDnsPreferenceKey,
                            AppConfig::kStationPrimaryDns.toString()),
      AppConfig::kStationPrimaryDns, &configuredStationIpConfig.primaryDns);
  parseIpAddressOrFallback(
      preferences.getString(kSecondaryDnsPreferenceKey,
                            AppConfig::kStationSecondaryDns.toString()),
      AppConfig::kStationSecondaryDns, &configuredStationIpConfig.secondaryDns);
}

void ensurePreferencesReady() {
  if (preferencesInitialized) {
    return;
  }

  preferencesInitialized = true;
  if (!preferences.begin(kPreferencesNamespace, false)) {
    Serial.println(
        "Preferences unavailable. Falling back to the built-in Wi-Fi and IP configuration.");
    return;
  }

  preferencesAvailable = true;
  loadSavedStationConfig();
  if (configuredStationSsid == kPlaceholderWiFiSsid) {
    Serial.println(
        "Station Wi-Fi target is still the placeholder value. The board will stay in fallback AP mode until valid Wi-Fi credentials are provided.");
  } else {
    Serial.printf("Station Wi-Fi target loaded: \"%s\".\n",
                  configuredStationSsid.c_str());
  }
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

  const bool configured =
      WiFi.config(configuredStationIpConfig.address,
                  configuredStationIpConfig.gateway,
                  configuredStationIpConfig.subnet,
                  configuredStationIpConfig.primaryDns,
                  configuredStationIpConfig.secondaryDns);
  if (!configured) {
    Serial.println("Failed to apply the fixed station IP configuration.");
    return false;
  }

  Serial.printf(
      "Using fixed station IP %s with gateway %s and subnet %s.\n",
      configuredStationIpConfig.address.toString().c_str(),
      configuredStationIpConfig.gateway.toString().c_str(),
      configuredStationIpConfig.subnet.toString().c_str());
  return true;
}

void beginStationConnectionAttempt(const bool logAttempt) {
  const bool keepFallbackAccessPoint = accessPointActive;

  WiFi.mode(keepFallbackAccessPoint ? WIFI_AP_STA : WIFI_STA);
  WiFi.setAutoReconnect(true);
  // ESP32 Wi-Fi/BLE coexistence requires modem sleep to stay enabled.
  WiFi.setSleep(true);
  WiFi.setHostname(AppConfig::kStatusHostName);
  WiFi.disconnect(false, false);
  delay(100);
  configureStationNetwork();

  if (logAttempt) {
    Serial.printf("Connecting to Wi-Fi SSID \"%s\"...\n",
                  configuredStationSsid.c_str());
  } else {
    Serial.printf(
        "Retrying Wi-Fi SSID \"%s\" while fallback AP remains available...\n",
        configuredStationSsid.c_str());
  }

  if (configuredStationPassword.length() == 0) {
    WiFi.begin(configuredStationSsid.c_str());
  } else {
    WiFi.begin(configuredStationSsid.c_str(),
               configuredStationPassword.c_str());
  }

  lastReconnectAttemptMs = millis();
  lastDisconnectReason = 0;
  stationConnectedLogged = false;
  markStatusLogged(WiFi.status());
}

void scheduleReconnect(const char *message, const bool ipModeAffected) {
  wifiReconnectPending = true;
  if (ipModeAffected) {
    ipAssignmentChangePending = true;
  }

  reconfigureRequestedAtMs = millis();
  lastReconnectAttemptMs = 0;
  lastDisconnectReason = 0;
  stationConnectedSinceMs = 0;
  stationConnectedLogged = false;
  Serial.println(message);
}

}  // namespace

bool connectWiFi() {
  accessPointActive = false;
  accessPointSsid = "";
  lastReconnectAttemptMs = 0;
  stationConnectedSinceMs = 0;
  reconfigureRequestedAtMs = 0;
  stationConnectedLogged = false;
  lastDisconnectReason = 0;
  ipAssignmentChangePending = false;
  wifiReconnectPending = false;
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
  if (wifiReconnectPending && millis() - reconfigureRequestedAtMs >= 500) {
    wifiReconnectPending = false;
    ipAssignmentChangePending = false;
    Serial.printf("Applying saved network settings using %s station IP mode.\n",
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

String getConfiguredStationSsid() { return configuredStationSsid; }

bool isStaticStationIpEnabled() { return staticStationIpEnabled; }

bool isIpAssignmentChangePending() { return ipAssignmentChangePending; }

bool isWiFiReconnectPending() { return wifiReconnectPending; }

bool setStaticStationIpEnabled(const bool enabled) {
  ensurePreferencesReady();

  if (staticStationIpEnabled == enabled && !wifiReconnectPending) {
    return true;
  }

  staticStationIpEnabled = enabled;
  if (preferencesAvailable) {
    preferences.putBool(kStaticIpPreferenceKey, enabled);
  }

  scheduleReconnect("Station IP mode change requested from the dashboard.",
                    true);
  return true;
}

StationIpConfig getConfiguredStationIpConfig() {
  return configuredStationIpConfig;
}

bool setStationIpConfig(const StationIpConfig &config) {
  ensurePreferencesReady();

  if (isZeroAddress(config.address) || isZeroAddress(config.gateway) ||
      isZeroAddress(config.subnet)) {
    return false;
  }

  const bool changed = !stationIpConfigEquals(configuredStationIpConfig, config);
  configuredStationIpConfig = config;

  if (preferencesAvailable) {
    preferences.putString(kStationIpPreferenceKey, config.address.toString());
    preferences.putString(kGatewayPreferenceKey, config.gateway.toString());
    preferences.putString(kSubnetPreferenceKey, config.subnet.toString());
    preferences.putString(kPrimaryDnsPreferenceKey, config.primaryDns.toString());
    preferences.putString(kSecondaryDnsPreferenceKey,
                          config.secondaryDns.toString());
  }

  if (staticStationIpEnabled && changed) {
    scheduleReconnect(
        "Fixed IP settings updated from the dashboard. Reconnecting Wi-Fi.",
        true);
  }

  return true;
}

bool setStationCredentials(const String &ssid, const String &password) {
  ensurePreferencesReady();

  String trimmedSsid = ssid;
  trimmedSsid.trim();
  if (trimmedSsid.length() == 0) {
    return false;
  }

  configuredStationSsid = trimmedSsid;
  configuredStationPassword = password;

  if (preferencesAvailable) {
    preferences.putString(kWiFiSsidPreferenceKey, configuredStationSsid);
    preferences.putString(kWiFiPasswordPreferenceKey, configuredStationPassword);
  }

  scheduleReconnect(
      "Wi-Fi credentials updated from the dashboard. Reconnecting station Wi-Fi.",
      false);
  return true;
}

bool scanWiFiNetworks(std::vector<WiFiNetworkInfo> *results) {
  if (results == nullptr) {
    return false;
  }

  ensureWiFiConfigured();
  results->clear();

  const int16_t count = WiFi.scanNetworks(false, true, false, 250);
  if (count < 0) {
    Serial.printf("Wi-Fi scan failed with code %d.\n", count);
    return false;
  }

  results->reserve(count);
  for (int16_t index = 0; index < count; ++index) {
    WiFiNetworkInfo network;
    network.ssid = WiFi.SSID(index);
    network.hidden = network.ssid.length() == 0;
    if (network.hidden) {
      network.ssid = "(hidden network)";
    }
    network.rssiDbm = WiFi.RSSI(index);
    network.channel = WiFi.channel(index);
    const wifi_auth_mode_t authMode = WiFi.encryptionType(index);
    network.authMode = wifiAuthModeToString(authMode);
    network.requiresPassword = authMode != WIFI_AUTH_OPEN;
    results->push_back(network);
  }

  WiFi.scanDelete();
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

  if (wifiReconnectPending) {
    return "Reconnecting";
  }

  return status;
}
