#include <Arduino.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <esp_err.h>
#include <esp_http_server.h>
#include <esp_https_server.h>

#include <algorithm>
#include <vector>

#include "AppConfig.h"
#include "BluetoothManager.h"
#include "Connectivity.h"
#include "Heartbeat.h"
#include "RegionalSettings.h"
#include "StatusServer.h"
#include "generated/StatusServerCertPem.h"
#include "generated/StatusServerKeyPem.h"

namespace {

httpd_handle_t serverHandle = nullptr;
bool mdnsStarted = false;
WebServer redirectServer(80);
bool redirectServerStarted = false;
String lastAnnouncedIpAddress;
bool lastAnnouncedMdnsStarted = false;
bool lastAnnouncedStationConnected = false;
bool lastAnnouncedAccessPointActive = false;
bool lastAnnouncedDirectIpTlsState = false;

const IPAddress kFallbackAccessPointIp(192, 168, 4, 1);
constexpr char kApplicationName[] = "ESP32 Setup Console";

String buildHttpsUrlForHost(const String &host, const String &path);
String buildDirectHttpsUrl(const String &path);
String buildHttpsUrl(const String &path);
String buildHttpUrlForHost(const String &host, const String &path);
String buildDirectHttpUrl(const String &path);

String jsonEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);

  for (size_t index = 0; index < value.length(); ++index) {
    const char current = value[index];
    switch (current) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += current;
        break;
    }
  }

  return escaped;
}

String htmlEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 16);

  for (size_t index = 0; index < value.length(); ++index) {
    const char current = value[index];
    switch (current) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      default:
        escaped += current;
        break;
    }
  }

  return escaped;
}

String formatUptime(const unsigned long uptimeMs) {
  const unsigned long totalSeconds = uptimeMs / 1000;
  const unsigned long days = totalSeconds / 86400;
  const unsigned long hours = (totalSeconds % 86400) / 3600;
  const unsigned long minutes = (totalSeconds % 3600) / 60;
  const unsigned long seconds = totalSeconds % 60;

  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%lud %02lu:%02lu:%02lu", days, hours,
           minutes, seconds);
  return String(buffer);
}

bool stationIpConfigEquals(const StationIpConfig &lhs,
                           const StationIpConfig &rhs) {
  return lhs.address == rhs.address && lhs.gateway == rhs.gateway &&
         lhs.subnet == rhs.subnet && lhs.primaryDns == rhs.primaryDns &&
         lhs.secondaryDns == rhs.secondaryDns;
}

bool isConfiguredFixedIpCertificateSupported() {
  return getConfiguredStationIpConfig().address == AppConfig::kStationStaticIp;
}

bool isCurrentDirectIpCertificateSupported() {
  const String currentIpAddress = getIpAddress();

  if (isAccessPointActive() && !isStationConnected()) {
    return currentIpAddress == kFallbackAccessPointIp.toString();
  }

  if (isStationConnected()) {
    return currentIpAddress == AppConfig::kStationStaticIp.toString();
  }

  return false;
}

String buildStationIpConfigJsonObject(const StationIpConfig &config) {
  String json;
  json.reserve(176);
  json += "{";
  json += "\"address\":\"" + jsonEscape(config.address.toString()) + "\",";
  json += "\"gateway\":\"" + jsonEscape(config.gateway.toString()) + "\",";
  json += "\"subnet\":\"" + jsonEscape(config.subnet.toString()) + "\",";
  json += "\"primaryDns\":\"" + jsonEscape(config.primaryDns.toString()) + "\",";
  json +=
      "\"secondaryDns\":\"" + jsonEscape(config.secondaryDns.toString()) + "\"";
  json += "}";
  return json;
}

String buildRegionalStatusJsonFields() {
  String json;
  json.reserve(320);
  json += "\"clockReady\":";
  json += hasSynchronizedClock() ? "true," : "false,";
  json += "\"localTime\":\"" + jsonEscape(getFormattedCurrentTime()) + "\",";
  json += "\"localDate\":\"" + jsonEscape(getFormattedCurrentDate()) + "\",";
  json += "\"timeZoneId\":\"" + jsonEscape(getConfiguredTimeZoneId()) + "\",";
  json += "\"timeZoneLabel\":\"" + jsonEscape(getConfiguredTimeZoneLabel()) +
          "\",";
  json += "\"timeZoneShort\":\"" +
          jsonEscape(getCurrentTimeZoneAbbreviation()) + "\",";
  json += "\"dateFormatId\":\"" +
          jsonEscape(getConfiguredDateFormatId()) + "\",";
  json += "\"dateFormatLabel\":\"" +
          jsonEscape(getConfiguredDateFormatLabel()) + "\",";
  return json;
}

String buildTimeZoneOptionsJsonArray() {
  String json;
  json.reserve(1024);
  json += "[";

  for (size_t index = 0; index < getSupportedTimeZoneCount(); ++index) {
    TimeZoneOption option;
    if (!getSupportedTimeZoneOption(index, &option)) {
      continue;
    }

    if (json.length() > 1) {
      json += ",";
    }

    json += "{";
    json += "\"id\":\"" + jsonEscape(option.id) + "\",";
    json += "\"label\":\"" + jsonEscape(option.label) + "\"";
    json += "}";
  }

  json += "]";
  return json;
}

String buildDateFormatOptionsJsonArray() {
  String json;
  json.reserve(512);
  json += "[";

  for (size_t index = 0; index < getSupportedDateFormatCount(); ++index) {
    DateFormatOption option;
    if (!getSupportedDateFormatOption(index, &option)) {
      continue;
    }

    if (json.length() > 1) {
      json += ",";
    }

    json += "{";
    json += "\"id\":\"" + jsonEscape(option.id) + "\",";
    json += "\"label\":\"" + jsonEscape(option.label) + "\"";
    json += "}";
  }

  json += "]";
  return json;
}

String buildRegionalSettingsJson(const bool ok, const bool includeOptions,
                                 const String &message) {
  String json;
  json.reserve(includeOptions ? 2300 : 700);
  json += "{";
  json += "\"ok\":";
  json += ok ? "true," : "false,";
  json += buildRegionalStatusJsonFields();
  if (includeOptions) {
    json += "\"timeZones\":";
    json += buildTimeZoneOptionsJsonArray();
    json += ",";
    json += "\"dateFormats\":";
    json += buildDateFormatOptionsJsonArray();
    json += ",";
  }
  json += "\"message\":\"" + jsonEscape(message) + "\"";
  json += "}";
  return json;
}

String buildStatusJson() {
  const bool stationConnected = isStationConnected();
  const bool accessPointEnabled = isAccessPointActive();
  const bool staticIpEnabled = isStaticStationIpEnabled();
  const unsigned long uptimeMs = millis();
  const StationIpConfig stationIpConfig = getConfiguredStationIpConfig();
  const String dashboardUrl = buildHttpsUrl("/");
  const String setupUrl = buildHttpsUrl("/setup");
  const String httpBootstrapUrl = buildDirectHttpUrl("/");
  const String apiBaseUrl = buildHttpsUrlForHost(
      (stationConnected && mdnsStarted)
          ? String(AppConfig::kStatusHostName) + ".local"
          : getIpAddress(),
      "");
  const String statusApiUrl = buildHttpsUrl("/api/status");
  const String healthUrl = buildHttpsUrl("/healthz");
  const String regionalSettingsUrl = buildHttpsUrl("/api/regional/settings");

  String json;
  json.reserve(3600);

  json += "{";
  json += "\"device\":\"" + jsonEscape(ESP.getChipModel()) + "\",";
  json += "\"applicationName\":\"" + jsonEscape(String(kApplicationName)) +
          "\",";
  json += "\"applicationCapabilities\":\"" +
          jsonEscape(
              "Wi-Fi setup, fixed or dynamic IP, Bluetooth LE status, and "
              "regional settings") +
          "\",";
  json += "\"dashboardUrl\":\"" + jsonEscape(dashboardUrl) + "\",";
  json += "\"setupUrl\":\"" + jsonEscape(setupUrl) + "\",";
  json += "\"httpBootstrapUrl\":\"" + jsonEscape(httpBootstrapUrl) + "\",";
  json += "\"apiBaseUrl\":\"" + jsonEscape(apiBaseUrl) + "\",";
  json += "\"statusApi\":\"GET /api/status\",";
  json += "\"statusApiUrl\":\"" + jsonEscape(statusApiUrl) + "\",";
  json += "\"healthApi\":\"GET /healthz\",";
  json += "\"healthUrl\":\"" + jsonEscape(healthUrl) + "\",";
  json += "\"regionalApi\":\"GET/POST /api/regional/settings\",";
  json += "\"regionalSettingsUrl\":\"" + jsonEscape(regionalSettingsUrl) +
          "\",";
  json +=
      "\"networkApi\":\"GET /api/network/scan | POST /api/network/connect | "
      "POST /api/network/ip-mode | POST /api/network/ip-config\",";
  json +=
      "\"bluetoothApi\":\"GET/POST /api/bluetooth/scan | POST "
      "/api/bluetooth/connect | POST /api/bluetooth/disconnect\",";
  json += "\"refreshIntervalMs\":" + String(AppConfig::kStatusRefreshMs) + ",";
  json += "\"uptimeMs\":" + String(uptimeMs) + ",";
  json += "\"uptime\":\"" + jsonEscape(formatUptime(uptimeMs)) + "\",";
  json += "\"freeHeapBytes\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"cpuMHz\":" + String(ESP.getCpuFreqMHz()) + ",";
  json += "\"flashBytes\":" + String(ESP.getFlashChipSize()) + ",";
  json += "\"sdkVersion\":\"" + jsonEscape(String(ESP.getSdkVersion())) + "\",";
  json += "\"chipRevision\":" + String(ESP.getChipRevision()) + ",";
  json += buildRegionalStatusJsonFields();
  json += "\"networkMode\":\"" + jsonEscape(getNetworkModeName()) + "\",";
  json += "\"connectionStatus\":\"" + jsonEscape(getConnectionStatusText()) +
          "\",";
  json += "\"ssid\":\"" + jsonEscape(getNetworkName()) + "\",";
  json += "\"configuredStationSsid\":\"" +
          jsonEscape(getConfiguredStationSsid()) + "\",";
  json += "\"ipMode\":\"" + jsonEscape(getIpAssignmentMode()) + "\",";
  json += "\"staticStationIpEnabled\":";
  json += staticIpEnabled ? "true," : "false,";
  json += "\"ipModePending\":";
  json += isIpAssignmentChangePending() ? "true," : "false,";
  json += "\"wifiReconnectPending\":";
  json += isWiFiReconnectPending() ? "true," : "false,";
  json += "\"configuredFixedIpTlsSupported\":";
  json += isConfiguredFixedIpCertificateSupported() ? "true," : "false,";
  json += "\"currentDirectIpTlsSupported\":";
  json += isCurrentDirectIpCertificateSupported() ? "true," : "false,";
  json += "\"stationIpConfig\":";
  json += buildStationIpConfigJsonObject(stationIpConfig);
  json += ",";
  json += "\"ipAddress\":\"" + jsonEscape(getIpAddress()) + "\",";
  json += "\"macAddress\":\"" + jsonEscape(getMacAddress()) + "\",";
  json += "\"wifiConnected\":";
  json += stationConnected ? "true," : "false,";
  json += "\"accessPointActive\":";
  json += accessPointEnabled ? "true," : "false,";
  json += "\"rssiDbm\":";
  if (stationConnected) {
    json += String(getSignalStrengthDbm());
  } else {
    json += "null";
  }
  json += ",";
  json += "\"bluetoothReady\":";
  json += isBluetoothReady() ? "true," : "false,";
  json += "\"bluetoothConnected\":";
  json += isBluetoothConnected() ? "true," : "false,";
  json += "\"bluetoothStatus\":\"" + jsonEscape(getBluetoothStatusText()) +
          "\",";
  json += "\"bluetoothDeviceName\":\"" +
          jsonEscape(getBluetoothConnectedDeviceName()) + "\",";
  json += "\"bluetoothDeviceAddress\":\"" +
          jsonEscape(getBluetoothConnectedDeviceAddress()) + "\",";
  json += "\"bluetoothLastMessage\":\"" +
          jsonEscape(getBluetoothLastMessage()) + "\",";
  json += "\"bluetoothDiscoveredCount\":" +
          String(getBluetoothDiscoveredDeviceCount()) + ",";
  json += "\"ledOn\":";
  json += isHeartbeatLedOn() ? "true," : "false,";
  json += "\"heartbeatIntervalMs\":" + String(getHeartbeatIntervalMs());
  json += "}";

  return json;
}

esp_err_t sendResponse(httpd_req_t *req, const char *contentType,
                       const String &body) {
  httpd_resp_set_type(req, contentType);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
  return httpd_resp_send(req, body.c_str(), body.length());
}

esp_err_t sendResponse(httpd_req_t *req, const char *contentType,
                       const char *body) {
  httpd_resp_set_type(req, contentType);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
  return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t sendChunkedResponse(httpd_req_t *req, const char *contentType,
                              const char *body,
                              const size_t chunkSize = 512) {
  httpd_resp_set_type(req, contentType);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");

  const size_t totalLength = strlen(body);
  for (size_t offset = 0; offset < totalLength; offset += chunkSize) {
    const size_t remaining = totalLength - offset;
    const size_t currentChunkSize =
        remaining < chunkSize ? remaining : chunkSize;
    const esp_err_t result =
        httpd_resp_send_chunk(req, body + offset, currentChunkSize);
    if (result != ESP_OK) {
      return result;
    }
  }

  return httpd_resp_send_chunk(req, nullptr, 0);
}

String readRequestBody(httpd_req_t *req, const size_t maxLength = 1024) {
  if (req->content_len <= 0 ||
      static_cast<size_t>(req->content_len) > maxLength) {
    return "";
  }

  String body;
  body.reserve(req->content_len);

  int remaining = req->content_len;
  while (remaining > 0) {
    char buffer[65];
    const int chunkSize = min(remaining, static_cast<int>(sizeof(buffer) - 1));
    const int received = httpd_req_recv(req, buffer, chunkSize);
    if (received <= 0) {
      return "";
    }

    buffer[received] = '\0';
    body += buffer;
    remaining -= received;
  }

  return body;
}

int hexDigitToInt(const char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }

  if (value >= 'a' && value <= 'f') {
    return 10 + (value - 'a');
  }

  if (value >= 'A' && value <= 'F') {
    return 10 + (value - 'A');
  }

  return -1;
}

String urlDecode(const String &value) {
  String decoded;
  decoded.reserve(value.length());

  for (size_t index = 0; index < value.length(); ++index) {
    const char current = value[index];
    if (current == '+') {
      decoded += ' ';
      continue;
    }

    if (current == '%' && index + 2 < value.length()) {
      const int upper = hexDigitToInt(value[index + 1]);
      const int lower = hexDigitToInt(value[index + 2]);
      if (upper >= 0 && lower >= 0) {
        decoded += static_cast<char>((upper << 4) | lower);
        index += 2;
        continue;
      }
    }

    decoded += current;
  }

  return decoded;
}

bool getFormField(const String &body, const char *key, String *value) {
  const String keyString(key);
  int start = 0;

  while (start <= body.length()) {
    int end = body.indexOf('&', start);
    if (end < 0) {
      end = body.length();
    }

    const String entry = body.substring(start, end);
    const int separator = entry.indexOf('=');
    const String entryKey =
        urlDecode(separator >= 0 ? entry.substring(0, separator) : entry);
    if (entryKey == keyString) {
      if (value != nullptr) {
        *value =
            separator >= 0 ? urlDecode(entry.substring(separator + 1)) : "";
      }
      return true;
    }

    if (end >= body.length()) {
      break;
    }
    start = end + 1;
  }

  return false;
}

bool parseIpField(const String &body, const char *key,
                  const IPAddress &currentValue, const bool allowBlank,
                  IPAddress *target, String *errorField) {
  if (target == nullptr) {
    return false;
  }

  String value;
  if (!getFormField(body, key, &value)) {
    *target = currentValue;
    return true;
  }

  value.trim();
  if (value.length() == 0) {
    if (allowBlank) {
      *target = IPAddress(0, 0, 0, 0);
      return true;
    }

    if (errorField != nullptr) {
      *errorField = key;
    }
    return false;
  }

  IPAddress parsed;
  if (!parsed.fromString(value)) {
    if (errorField != nullptr) {
      *errorField = key;
    }
    return false;
  }

  *target = parsed;
  return true;
}

String buildBasicResultJson(const bool ok, const String &message) {
  String json;
  json.reserve(128);
  json += "{";
  json += "\"ok\":";
  json += ok ? "true," : "false,";
  json += "\"message\":\"" + jsonEscape(message) + "\"";
  json += "}";
  return json;
}

String buildIpModeUpdateJson(const bool ok, const String &mode,
                             const bool staticIpEnabled, const bool pending,
                             const String &message) {
  String json;
  json.reserve(448);
  json += "{";
  json += "\"ok\":";
  json += ok ? "true," : "false,";
  json += "\"ipMode\":\"" + jsonEscape(mode) + "\",";
  json += "\"staticStationIpEnabled\":";
  json += staticIpEnabled ? "true," : "false,";
  json += "\"ipModePending\":";
  json += pending ? "true," : "false,";
  json += "\"configuredFixedIpTlsSupported\":";
  json += isConfiguredFixedIpCertificateSupported() ? "true," : "false,";
  json += "\"stationIpConfig\":";
  json += buildStationIpConfigJsonObject(getConfiguredStationIpConfig());
  json += ",";
  json += "\"message\":\"" + jsonEscape(message) + "\"";
  json += "}";
  return json;
}

String buildIpConfigUpdateJson(const bool ok, const String &message) {
  String json;
  json.reserve(448);
  json += "{";
  json += "\"ok\":";
  json += ok ? "true," : "false,";
  json += "\"ipMode\":\"" + jsonEscape(getIpAssignmentMode()) + "\",";
  json += "\"staticStationIpEnabled\":";
  json += isStaticStationIpEnabled() ? "true," : "false,";
  json += "\"ipModePending\":";
  json += isIpAssignmentChangePending() ? "true," : "false,";
  json += "\"configuredFixedIpTlsSupported\":";
  json += isConfiguredFixedIpCertificateSupported() ? "true," : "false,";
  json += "\"stationIpConfig\":";
  json += buildStationIpConfigJsonObject(getConfiguredStationIpConfig());
  json += ",";
  json += "\"message\":\"" + jsonEscape(message) + "\"";
  json += "}";
  return json;
}

String buildWiFiConnectJson(const bool ok, const String &message) {
  String json;
  json.reserve(320);
  json += "{";
  json += "\"ok\":";
  json += ok ? "true," : "false,";
  json += "\"configuredStationSsid\":\"" +
          jsonEscape(getConfiguredStationSsid()) + "\",";
  json += "\"wifiReconnectPending\":";
  json += isWiFiReconnectPending() ? "true," : "false,";
  json += "\"message\":\"" + jsonEscape(message) + "\"";
  json += "}";
  return json;
}

String buildWiFiScanJson(const bool ok, const std::vector<WiFiNetworkInfo> &networks,
                         const String &message) {
  std::vector<WiFiNetworkInfo> sortedNetworks = networks;
  std::sort(sortedNetworks.begin(), sortedNetworks.end(),
            [](const WiFiNetworkInfo &lhs, const WiFiNetworkInfo &rhs) {
              return lhs.rssiDbm > rhs.rssiDbm;
            });

  String json;
  json.reserve(256 + sortedNetworks.size() * 128);
  json += "{";
  json += "\"ok\":";
  json += ok ? "true," : "false,";
  json += "\"count\":" + String(sortedNetworks.size()) + ",";
  json += "\"message\":\"" + jsonEscape(message) + "\",";
  json += "\"networks\":[";

  for (size_t index = 0; index < sortedNetworks.size(); ++index) {
    const WiFiNetworkInfo &network = sortedNetworks[index];
    if (index > 0) {
      json += ",";
    }

    json += "{";
    json += "\"ssid\":\"" + jsonEscape(network.ssid) + "\",";
    json += "\"rssiDbm\":" + String(network.rssiDbm) + ",";
    json += "\"channel\":" + String(network.channel) + ",";
    json += "\"authMode\":\"" + jsonEscape(network.authMode) + "\",";
    json += "\"requiresPassword\":";
    json += network.requiresPassword ? "true," : "false,";
    json += "\"hidden\":";
    json += network.hidden ? "true" : "false";
    json += "}";
  }

  json += "]";
  json += "}";
  return json;
}

String buildBluetoothStatusJson(const bool ok, const String &message) {
  String json;
  json.reserve(384);
  json += "{";
  json += "\"ok\":";
  json += ok ? "true," : "false,";
  json += "\"bluetoothReady\":";
  json += isBluetoothReady() ? "true," : "false,";
  json += "\"bluetoothConnected\":";
  json += isBluetoothConnected() ? "true," : "false,";
  json += "\"bluetoothStatus\":\"" + jsonEscape(getBluetoothStatusText()) +
          "\",";
  json += "\"bluetoothDeviceName\":\"" +
          jsonEscape(getBluetoothConnectedDeviceName()) + "\",";
  json += "\"bluetoothDeviceAddress\":\"" +
          jsonEscape(getBluetoothConnectedDeviceAddress()) + "\",";
  json += "\"bluetoothLastMessage\":\"" +
          jsonEscape(getBluetoothLastMessage()) + "\",";
  json += "\"bluetoothDiscoveredCount\":" +
          String(getBluetoothDiscoveredDeviceCount()) + ",";
  json += "\"message\":\"" + jsonEscape(message) + "\"";
  json += "}";
  return json;
}

String buildBluetoothScanJson(const bool ok, const bool scanning,
                              const std::vector<BluetoothDeviceInfo> &devices,
                              const String &message) {
  std::vector<BluetoothDeviceInfo> sortedDevices = devices;
  std::sort(sortedDevices.begin(), sortedDevices.end(),
            [](const BluetoothDeviceInfo &lhs,
               const BluetoothDeviceInfo &rhs) {
              return lhs.rssiDbm > rhs.rssiDbm;
            });

  String json;
  json.reserve(256 + sortedDevices.size() * 112);
  json += "{";
  json += "\"ok\":";
  json += ok ? "true," : "false,";
  json += "\"scanning\":";
  json += scanning ? "true," : "false,";
  json += "\"count\":" + String(sortedDevices.size()) + ",";
  json += "\"message\":\"" + jsonEscape(message) + "\",";
  json += "\"devices\":[";

  for (size_t index = 0; index < sortedDevices.size(); ++index) {
    const BluetoothDeviceInfo &device = sortedDevices[index];
    if (index > 0) {
      json += ",";
    }

    json += "{";
    json += "\"name\":\"" + jsonEscape(device.name) + "\",";
    json += "\"address\":\"" + jsonEscape(device.address) + "\",";
    json += "\"rssiDbm\":" + String(device.rssiDbm);
    json += "}";
  }

  json += "]";
  json += "}";
  return json;
}

esp_err_t handleDashboard(httpd_req_t *req) {
  static const char page[] = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Status</title>
  <style>
    :root {
      color-scheme: light;
      --bg: #f6f1e8;
      --surface: rgba(255, 252, 247, 0.92);
      --ink: #16202b;
      --muted: #5f6874;
      --accent: #0f766e;
      --accent-soft: rgba(15, 118, 110, 0.14);
      --warn: #b45309;
      --warn-soft: rgba(180, 83, 9, 0.14);
      --border: rgba(22, 32, 43, 0.1);
      --shadow: 0 18px 42px rgba(22, 32, 43, 0.1);
    }

    * {
      box-sizing: border-box;
    }

    body {
      margin: 0;
      min-height: 100vh;
      font-family: "Avenir Next", "Segoe UI", sans-serif;
      color: var(--ink);
      background:
        radial-gradient(circle at top left, rgba(15, 118, 110, 0.16), transparent 32%),
        linear-gradient(180deg, #f8f3eb 0%, #ede3d6 100%);
    }

    .shell {
      max-width: 1120px;
      margin: 0 auto;
      padding: 24px 16px 32px;
    }

    .hero {
      padding: 24px;
      border-radius: 24px;
      background: linear-gradient(135deg, rgba(255, 252, 247, 0.95), rgba(255, 249, 239, 0.84));
      border: 1px solid var(--border);
      box-shadow: var(--shadow);
    }

    .eyebrow {
      margin: 0 0 8px;
      font-size: 0.78rem;
      letter-spacing: 0.18em;
      text-transform: uppercase;
      color: var(--accent);
      font-weight: 700;
    }

    h1 {
      margin: 0;
      font-size: clamp(1.9rem, 4vw, 3.2rem);
      line-height: 1;
    }

    .summary {
      margin: 12px 0 0;
      max-width: 52rem;
      color: var(--muted);
      line-height: 1.55;
    }

    .actions {
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
      margin-top: 18px;
    }

    .button,
    a.button {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      min-height: 42px;
      padding: 0 14px;
      border-radius: 999px;
      border: 0;
      font-weight: 700;
      text-decoration: none;
    }

    .button.primary {
      color: white;
      background: var(--accent);
      box-shadow: 0 10px 20px rgba(15, 118, 110, 0.18);
    }

    .button.secondary {
      color: var(--ink);
      background: rgba(22, 32, 43, 0.08);
    }

    .pill {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      margin-top: 16px;
      padding: 10px 14px;
      border-radius: 999px;
      border: 1px solid var(--border);
      background: rgba(255, 255, 255, 0.72);
      font-weight: 700;
    }

    .pill::before {
      content: "";
      width: 10px;
      height: 10px;
      border-radius: 50%;
      background: currentColor;
      opacity: 0.8;
    }

    .pill.ok {
      color: var(--accent);
      background: var(--accent-soft);
    }

    .pill.warn {
      color: var(--warn);
      background: var(--warn-soft);
    }

    .grid {
      display: grid;
      gap: 16px;
      margin-top: 18px;
      grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
    }

    .card {
      padding: 20px;
      border-radius: 20px;
      background: var(--surface);
      border: 1px solid var(--border);
      box-shadow: var(--shadow);
    }

    .card.wide,
    .card.span-2 {
      grid-column: 1 / -1;
    }

    .card h2 {
      margin: 0 0 14px;
      font-size: 1rem;
      letter-spacing: 0.08em;
      text-transform: uppercase;
      color: var(--muted);
    }

    dl {
      margin: 0;
      display: grid;
      gap: 10px;
    }

    .row {
      display: flex;
      align-items: baseline;
      justify-content: space-between;
      gap: 12px;
      padding-bottom: 10px;
      border-bottom: 1px solid rgba(22, 32, 43, 0.08);
    }

    .row:last-child {
      border-bottom: 0;
      padding-bottom: 0;
    }

    dt {
      color: var(--muted);
      font-size: 0.92rem;
    }

    dd {
      margin: 0;
      text-align: right;
      font-weight: 700;
      word-break: break-word;
    }

    a {
      color: var(--accent);
    }

    .mono {
      font-family: "SFMono-Regular", "Consolas", monospace;
      font-size: 0.92rem;
    }

    .footer {
      margin-top: 18px;
      color: var(--muted);
      font-size: 0.92rem;
      text-align: right;
    }

    @media (max-width: 720px) {
      .shell {
        padding: 16px 14px 24px;
      }

      .hero,
      .card {
        padding: 18px;
      }

      .row {
        flex-direction: column;
        align-items: flex-start;
      }

      dd {
        text-align: left;
      }
    }
  </style>
</head>
<body>
  <main class="shell">
    <section class="hero">
      <p class="eyebrow">ESP32 Status</p>
      <h1 id="deviceName">Loading board status...</h1>
      <p class="summary" id="heroSummary">Connecting to the HTTPS status API now.</p>
      <span class="pill warn" id="connectionPill">Checking board state</span>
      <div class="actions">
        <a class="button primary" href="/setup">Open Setup</a>
        <a class="button secondary" href="/api/status">Status API</a>
        <a class="button secondary" href="/healthz">Health</a>
      </div>
    </section>

    <section class="grid">
      <article class="card">
        <h2>System</h2>
        <dl>
          <div class="row"><dt>Device</dt><dd id="statusDeviceName">-</dd></div>
          <div class="row"><dt>Chip Revision</dt><dd id="chipRevision">-</dd></div>
          <div class="row"><dt>Uptime</dt><dd id="uptime">-</dd></div>
          <div class="row"><dt>Free Heap</dt><dd id="freeHeap">-</dd></div>
          <div class="row"><dt>CPU</dt><dd id="cpu">-</dd></div>
          <div class="row"><dt>Flash</dt><dd id="flash">-</dd></div>
          <div class="row"><dt>SDK</dt><dd id="sdkVersion">-</dd></div>
          <div class="row"><dt>Heartbeat</dt><dd id="heartbeatState">-</dd></div>
        </dl>
      </article>

      <article class="card">
        <h2>Network</h2>
        <dl>
          <div class="row"><dt>Mode</dt><dd id="networkMode">-</dd></div>
          <div class="row"><dt>Status</dt><dd id="connectionStatus">-</dd></div>
          <div class="row"><dt>Active SSID</dt><dd id="networkName">-</dd></div>
          <div class="row"><dt>Saved Hotspot</dt><dd id="savedNetworkName">-</dd></div>
          <div class="row"><dt>IP Mode</dt><dd id="ipMode">-</dd></div>
          <div class="row"><dt>Current IP</dt><dd id="ipAddress">-</dd></div>
          <div class="row"><dt>MAC Address</dt><dd id="macAddress">-</dd></div>
          <div class="row"><dt>Signal</dt><dd id="signalStrength">-</dd></div>
        </dl>
      </article>

      <article class="card">
        <h2>Bluetooth</h2>
        <dl>
          <div class="row"><dt>State</dt><dd id="bluetoothStatus">-</dd></div>
          <div class="row"><dt>Connected Device</dt><dd id="bluetoothDeviceName">-</dd></div>
          <div class="row"><dt>Address</dt><dd id="bluetoothDeviceAddress">-</dd></div>
          <div class="row"><dt>Nearby Devices</dt><dd id="bluetoothDiscoveredCount">-</dd></div>
          <div class="row"><dt>Detail</dt><dd id="bluetoothDetail">-</dd></div>
        </dl>
      </article>

      <article class="card">
        <h2>Application</h2>
        <dl>
          <div class="row"><dt>Console</dt><dd id="applicationName">-</dd></div>
          <div class="row"><dt>HTTPS Dashboard</dt><dd id="dashboardUrl" class="mono">-</dd></div>
          <div class="row"><dt>Setup Page</dt><dd id="setupUrl" class="mono">-</dd></div>
          <div class="row"><dt>HTTP Bootstrap</dt><dd id="httpBootstrapUrl" class="mono">-</dd></div>
          <div class="row"><dt>Clock</dt><dd id="localClock">-</dd></div>
          <div class="row"><dt>Regional</dt><dd id="regionalSummary">-</dd></div>
          <div class="row"><dt>Features</dt><dd id="capabilities">-</dd></div>
          <div class="row"><dt>Refresh</dt><dd id="refreshInterval">-</dd></div>
        </dl>
      </article>

      <article class="card span-2">
        <h2>API</h2>
        <dl>
          <div class="row"><dt>Base URL</dt><dd id="apiBaseUrl" class="mono">-</dd></div>
          <div class="row"><dt>Status</dt><dd id="statusApi" class="mono">-</dd></div>
          <div class="row"><dt>Health</dt><dd id="healthApi" class="mono">-</dd></div>
          <div class="row"><dt>Regional</dt><dd id="regionalApi" class="mono">-</dd></div>
          <div class="row"><dt>Network</dt><dd id="networkApi" class="mono">-</dd></div>
          <div class="row"><dt>Bluetooth</dt><dd id="bluetoothApi" class="mono">-</dd></div>
        </dl>
      </article>
    </section>

    <p class="footer">Last updated <span id="lastUpdated">never</span></p>
  </main>

  <script>
    const refreshMs = 2000;
    let refreshTimerId = null;
    let pageClosing = false;

    function byId(id) {
      return document.getElementById(id);
    }

    function formatBytes(value) {
      if (typeof value !== 'number' || Number.isNaN(value)) {
        return 'n/a';
      }

      const units = ['B', 'KB', 'MB', 'GB'];
      let size = value;
      let index = 0;

      while (size >= 1024 && index < units.length - 1) {
        size /= 1024;
        index += 1;
      }

      const digits = size >= 10 || index === 0 ? 0 : 1;
      return size.toFixed(digits) + ' ' + units[index];
    }

    function safeText(value, fallback) {
      if (value === null || value === undefined || value === '') {
        return fallback;
      }

      return String(value);
    }

    function escapeHtml(value) {
      return String(value)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
    }

    function setText(id, value, fallback) {
      byId(id).textContent = safeText(value, fallback || '-');
    }

    function setLink(id, url, label) {
      const element = byId(id);
      if (!url) {
        element.textContent = '-';
        return;
      }

      element.innerHTML =
        '<a href="' + escapeHtml(url) + '">' +
        escapeHtml(label || url) +
        '</a>';
    }

    function setPill(label, healthy) {
      const element = byId('connectionPill');
      element.textContent = label;
      element.classList.remove('ok', 'warn');
      element.classList.add(healthy ? 'ok' : 'warn');
    }

    function updateStatusUi(data) {
      const online = !!data.wifiConnected || !!data.accessPointActive;
      const summary = data.wifiConnected
        ? ('Wi-Fi connected to ' + safeText(data.ssid, 'the current hotspot') + ' at ' + safeText(data.ipAddress, '-'))
        : (data.accessPointActive
            ? ('Fallback AP active at ' + safeText(data.ipAddress, '192.168.4.1'))
            : 'Board is offline right now.');

      byId('deviceName').textContent = safeText(data.applicationName, 'ESP32 Setup Console');
      byId('heroSummary').textContent = summary;

      setText('networkMode', data.networkMode);
      setText('connectionStatus', data.connectionStatus);
      setText('networkName', data.ssid, 'Unavailable');
      setText('savedNetworkName', data.configuredStationSsid, 'Not set');
      setText('ipMode', data.ipMode);
      setText('ipAddress', data.ipAddress);
      setText('macAddress', data.macAddress);
      setText('signalStrength',
        typeof data.rssiDbm === 'number' ? data.rssiDbm + ' dBm' : 'n/a');

      setText('bluetoothStatus', data.bluetoothStatus);
      setText('bluetoothDeviceName', data.bluetoothDeviceName, 'Unavailable');
      setText('bluetoothDeviceAddress', data.bluetoothDeviceAddress, 'Unavailable');
      setText('bluetoothDiscoveredCount', data.bluetoothDiscoveredCount, '0');
      setText('bluetoothDetail', data.bluetoothLastMessage, '-');

      setText('applicationName', data.applicationName);
      setLink('dashboardUrl', data.dashboardUrl);
      setLink('setupUrl', data.setupUrl);
      setLink('httpBootstrapUrl', data.httpBootstrapUrl);
      setText(
        'localClock',
        data.clockReady
          ? (safeText(data.localDate, '-') + ' • ' + safeText(data.localTime, '-') + ' • ' + safeText(data.timeZoneShort, '-'))
          : 'Waiting for Wi-Fi time sync'
      );
      setText(
        'regionalSummary',
        safeText(data.timeZoneLabel, '-') + ' • ' + safeText(data.dateFormatLabel, '-')
      );
      setText('capabilities', data.applicationCapabilities);
      setText(
        'refreshInterval',
        typeof data.refreshIntervalMs === 'number'
          ? ((data.refreshIntervalMs / 1000).toFixed(1).replace('.0', '') + ' s')
          : '-'
      );

      setLink('apiBaseUrl', data.apiBaseUrl, data.apiBaseUrl);
      setText('statusApi', safeText(data.statusApi, '-') + ' • ' + safeText(data.statusApiUrl, '-'));
      setText('healthApi', safeText(data.healthApi, '-') + ' • ' + safeText(data.healthUrl, '-'));
      setText('regionalApi', data.regionalApi);
      setText('networkApi', data.networkApi);
      setText('bluetoothApi', data.bluetoothApi);

      setText('statusDeviceName', data.device);
      setText('chipRevision', data.chipRevision);
      setText('uptime', data.uptime);
      setText('freeHeap', formatBytes(data.freeHeapBytes));
      setText('cpu', safeText(data.cpuMHz, '-') + ' MHz');
      setText('flash', formatBytes(data.flashBytes));
      setText('sdkVersion', data.sdkVersion);
      setText(
        'heartbeatState',
        (data.ledOn ? 'On' : 'Off') + ' • ' + safeText(data.heartbeatIntervalMs, '-') + ' ms'
      );

      setPill(
        online
          ? (data.wifiConnected ? 'Board online over Wi-Fi' : 'Fallback AP active')
          : 'Board unavailable',
        online
      );
      byId('lastUpdated').textContent = new Date().toLocaleTimeString();
    }

    async function refreshStatus() {
      if (pageClosing) {
        return;
      }

      try {
        const response = await fetch('/api/status', { cache: 'no-store' });
        const data = await response.json();
        if (!response.ok) {
          throw new Error(data.message || ('HTTP ' + response.status));
        }

        updateStatusUi(data);
      } catch (error) {
        if (pageClosing) {
          return;
        }

        byId('lastUpdated').textContent = 'refresh failed';
        setPill('Status unavailable', false);
      }
    }

    window.addEventListener('pagehide', function () {
      pageClosing = true;
      if (refreshTimerId !== null) {
        window.clearInterval(refreshTimerId);
        refreshTimerId = null;
      }
    });

    refreshStatus();
    refreshTimerId = window.setInterval(refreshStatus, refreshMs);
  </script>
</body>
</html>
)HTML";
  return sendChunkedResponse(req, "text/html; charset=utf-8", page);
}

esp_err_t handleSetupPage(httpd_req_t *req) {
  static const char page[] = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Setup</title>
  <style>
    :root {
      color-scheme: light;
      --bg: #f6f1e8;
      --surface: rgba(255, 252, 247, 0.92);
      --ink: #16202b;
      --muted: #5f6874;
      --accent: #0f766e;
      --accent-soft: rgba(15, 118, 110, 0.14);
      --warn: #b45309;
      --warn-soft: rgba(180, 83, 9, 0.14);
      --info-soft: rgba(22, 32, 43, 0.06);
      --border: rgba(22, 32, 43, 0.1);
      --shadow: 0 18px 42px rgba(22, 32, 43, 0.1);
    }

    * {
      box-sizing: border-box;
    }

    body {
      margin: 0;
      min-height: 100vh;
      font-family: "Avenir Next", "Segoe UI", sans-serif;
      color: var(--ink);
      background:
        radial-gradient(circle at top left, rgba(15, 118, 110, 0.16), transparent 32%),
        linear-gradient(180deg, #f8f3eb 0%, #ede3d6 100%);
    }

    .shell {
      max-width: 1120px;
      margin: 0 auto;
      padding: 24px 16px 32px;
    }

    .hero {
      padding: 24px;
      border-radius: 24px;
      background: linear-gradient(135deg, rgba(255, 252, 247, 0.95), rgba(255, 249, 239, 0.84));
      border: 1px solid var(--border);
      box-shadow: var(--shadow);
    }

    .eyebrow {
      margin: 0 0 8px;
      font-size: 0.78rem;
      letter-spacing: 0.18em;
      text-transform: uppercase;
      color: var(--accent);
      font-weight: 700;
    }

    h1 {
      margin: 0;
      font-size: clamp(1.9rem, 4vw, 3rem);
      line-height: 1;
    }

    .summary {
      margin: 12px 0 0;
      max-width: 52rem;
      color: var(--muted);
      line-height: 1.55;
    }

    .actions,
    .button-row {
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
      margin-top: 16px;
    }

    .button,
    a.button,
    button {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      min-height: 42px;
      padding: 0 14px;
      border-radius: 999px;
      border: 0;
      font: inherit;
      font-weight: 700;
      text-decoration: none;
      cursor: pointer;
    }

    .button.primary,
    button {
      color: white;
      background: var(--accent);
      box-shadow: 0 10px 20px rgba(15, 118, 110, 0.18);
    }

    .button.secondary,
    button.secondary {
      color: var(--ink);
      background: rgba(22, 32, 43, 0.08);
      box-shadow: none;
    }

    button.warn {
      background: #b45309;
      box-shadow: 0 10px 20px rgba(180, 83, 9, 0.18);
    }

    button:disabled {
      opacity: 0.6;
      cursor: not-allowed;
      box-shadow: none;
    }

    .grid {
      display: grid;
      gap: 16px;
      margin-top: 18px;
      grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
    }

    .card {
      padding: 20px;
      border-radius: 20px;
      background: var(--surface);
      border: 1px solid var(--border);
      box-shadow: var(--shadow);
    }

    .card.wide {
      grid-column: 1 / -1;
    }

    .card h2 {
      margin: 0 0 12px;
      font-size: 1rem;
      letter-spacing: 0.08em;
      text-transform: uppercase;
      color: var(--muted);
    }

    .note {
      margin: 10px 0 0;
      color: var(--muted);
      line-height: 1.5;
      font-size: 0.92rem;
    }

    .banner {
      display: none;
      margin-top: 14px;
      padding: 12px 14px;
      border-radius: 16px;
      font-size: 0.94rem;
      line-height: 1.45;
    }

    .banner.info,
    .banner.ok,
    .banner.warn {
      display: block;
    }

    .banner.info {
      background: var(--info-soft);
      color: var(--ink);
    }

    .banner.ok {
      background: var(--accent-soft);
      color: #0d5c57;
    }

    .banner.warn {
      background: var(--warn-soft);
      color: #8a4a09;
    }

    .form-grid {
      display: grid;
      gap: 14px;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    }

    label {
      display: grid;
      gap: 7px;
      font-size: 0.9rem;
      color: var(--muted);
      font-weight: 600;
    }

    input,
    select {
      width: 100%;
      min-height: 42px;
      padding: 10px 12px;
      border-radius: 14px;
      border: 1px solid rgba(22, 32, 43, 0.14);
      background: rgba(255, 255, 255, 0.82);
      color: var(--ink);
      font: inherit;
    }

    input:focus,
    select:focus {
      outline: 2px solid rgba(15, 118, 110, 0.28);
      border-color: rgba(15, 118, 110, 0.35);
    }

    .row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
    }

    .switch {
      position: relative;
      display: inline-block;
      width: 52px;
      height: 30px;
      flex: 0 0 auto;
    }

    .switch input {
      opacity: 0;
      width: 0;
      height: 0;
    }

    .slider {
      position: absolute;
      inset: 0;
      border-radius: 999px;
      background: rgba(22, 32, 43, 0.18);
      cursor: pointer;
      transition: background 0.2s ease;
    }

    .slider::before {
      content: "";
      position: absolute;
      width: 22px;
      height: 22px;
      left: 4px;
      top: 4px;
      border-radius: 50%;
      background: #fff9f0;
      box-shadow: 0 4px 10px rgba(22, 32, 43, 0.18);
      transition: transform 0.2s ease;
    }

    .switch input:checked + .slider {
      background: var(--accent);
    }

    .switch input:checked + .slider::before {
      transform: translateX(22px);
    }

    .switch input:disabled + .slider {
      opacity: 0.55;
      cursor: not-allowed;
    }

    .scan-list {
      display: grid;
      gap: 10px;
      margin-top: 16px;
    }

    .scan-item {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
      padding: 12px;
      border-radius: 16px;
      background: rgba(255, 255, 255, 0.68);
      border: 1px solid rgba(22, 32, 43, 0.08);
    }

    .scan-main {
      min-width: 0;
    }

    .scan-title {
      margin: 0;
      font-weight: 700;
      word-break: break-word;
    }

    .scan-meta {
      margin: 4px 0 0;
      color: var(--muted);
      font-size: 0.88rem;
      line-height: 1.45;
      word-break: break-word;
    }

    .empty {
      margin: 0;
      padding: 14px;
      border-radius: 16px;
      background: rgba(255, 255, 255, 0.54);
      border: 1px dashed rgba(22, 32, 43, 0.16);
      color: var(--muted);
    }

    .footer {
      margin-top: 18px;
      color: var(--muted);
      font-size: 0.92rem;
      text-align: right;
    }

    @media (max-width: 720px) {
      .shell {
        padding: 16px 14px 24px;
      }

      .hero,
      .card {
        padding: 18px;
      }

      .scan-item,
      .row {
        flex-direction: column;
        align-items: flex-start;
      }
    }
  </style>
</head>
<body>
  <main class="shell">
    <section class="hero">
      <p class="eyebrow">ESP32 Setup</p>
      <h1>Board Setup</h1>
      <p class="summary" id="setupSummary">Loading the current network and setup state...</p>
      <div class="actions">
        <a class="button primary" href="/" id="backToStatusLink">Back to Status</a>
        <a class="button secondary" href="/api/status">Status API</a>
      </div>
    </section>

    <section class="grid">
      <article class="card wide">
        <h2>Wi-Fi Setup</h2>
        <p class="note" id="wifiSummary">Loading saved hotspot details...</p>
        <div class="form-grid">
          <label>
            <span>Hotspot SSID</span>
            <input id="wifiSsidInput" autocomplete="off" placeholder="Choose from the list or enter manually">
          </label>
          <label>
            <span>Password</span>
            <input id="wifiPasswordInput" type="password" autocomplete="new-password" placeholder="Leave blank for open networks">
          </label>
        </div>
        <div class="button-row">
          <button id="saveWifiButton" type="button">Save & Reconnect</button>
          <button class="secondary" id="scanWifiButton" type="button">Scan Hotspots</button>
        </div>
        <p class="note">Open hotspots plus WPA/WPA2/WPA3 personal authentication are supported from this screen. Enterprise Wi-Fi is not configured here.</p>
        <div class="banner" id="wifiBanner"></div>
        <div class="scan-list" id="wifiList">
          <p class="empty">Run a Wi-Fi scan to pick a hotspot from the list.</p>
        </div>
      </article>

      <article class="card">
        <h2>IP Setup</h2>
        <div class="row">
          <strong>Use fixed IP</strong>
          <label class="switch" aria-label="Toggle fixed IP mode">
            <input id="ipModeToggle" type="checkbox">
            <span class="slider"></span>
          </label>
        </div>
        <p class="note" id="ipModeHint">Loading IP mode...</p>
        <div class="form-grid">
          <label>
            <span>Address</span>
            <input id="ipAddressInput" inputmode="decimal" placeholder="192.168.1.176">
          </label>
          <label>
            <span>Gateway</span>
            <input id="gatewayInput" inputmode="decimal" placeholder="192.168.1.1">
          </label>
          <label>
            <span>Subnet</span>
            <input id="subnetInput" inputmode="decimal" placeholder="255.255.255.0">
          </label>
          <label>
            <span>Primary DNS</span>
            <input id="dns1Input" inputmode="decimal" placeholder="1.1.1.1">
          </label>
          <label>
            <span>Secondary DNS</span>
            <input id="dns2Input" inputmode="decimal" placeholder="8.8.8.8">
          </label>
        </div>
        <div class="button-row">
          <button id="saveIpButton" type="button">Save IP Settings</button>
        </div>
        <p class="note" id="ipConfigNote">Saved fixed IP settings are used whenever fixed IP mode is enabled.</p>
        <div class="banner" id="ipBanner"></div>
      </article>

      <article class="card">
        <h2>Regional Setup</h2>
        <div class="form-grid">
          <label>
            <span>Timezone</span>
            <select id="timezoneSelect"></select>
          </label>
          <label>
            <span>Date Format</span>
            <select id="dateFormatSelect"></select>
          </label>
        </div>
        <div class="button-row">
          <button id="saveRegionalButton" type="button">Save Regional Settings</button>
        </div>
        <p class="note" id="regionalSummary">Current regional settings are loading...</p>
        <p class="note" id="regionalPreview">Board time preview is loading...</p>
        <div class="banner" id="regionalBanner"></div>
      </article>

      <article class="card wide">
        <h2>Bluetooth Setup</h2>
        <p class="note" id="bluetoothSummary">Loading Bluetooth state...</p>
        <div class="button-row">
          <button id="scanBluetoothButton" type="button">Scan BLE Devices</button>
          <button class="secondary warn" id="disconnectBluetoothButton" type="button">Disconnect</button>
        </div>
        <div class="banner" id="bluetoothBanner"></div>
        <div class="scan-list" id="bluetoothList">
          <p class="empty">Run a BLE scan to view nearby peripherals.</p>
        </div>
      </article>
    </section>

    <p class="footer">Last updated <span id="lastUpdated">never</span></p>
  </main>

  <script>
    const refreshMs = 5000;
    const preferredHostName = 'esp32-status.local';
    let ipModeChangeInFlight = false;
    let formsHydrated = false;
    let bluetoothScanInFlight = false;
    let regionalFormsHydrated = false;
    let refreshTimerId = null;
    let pageClosing = false;
    const activeRequests = new Set();

    function byId(id) {
      return document.getElementById(id);
    }

    function safeText(value, fallback) {
      if (value === null || value === undefined || value === '') {
        return fallback;
      }

      return String(value);
    }

    function escapeHtml(value) {
      return String(value)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
    }

    function setBanner(id, text, type) {
      const element = byId(id);
      element.className = 'banner';
      element.textContent = text || '';

      if (!text) {
        return;
      }

      element.classList.add(type || 'info');
    }

    function stopSetupActivity() {
      if (pageClosing) {
        return;
      }

      pageClosing = true;
      bluetoothScanInFlight = false;

      if (refreshTimerId !== null) {
        window.clearInterval(refreshTimerId);
        refreshTimerId = null;
      }

      activeRequests.forEach(function (controller) {
        controller.abort();
      });
      activeRequests.clear();
    }

    async function fetchJson(url, options) {
      const controller = new AbortController();
      const requestOptions = Object.assign({}, options || {}, {
        signal: controller.signal
      });
      activeRequests.add(controller);

      let response;
      let data;
      try {
        response = await fetch(url, requestOptions);
        data = await response.json();
      } finally {
        activeRequests.delete(controller);
      }

      if (!response.ok || (Object.prototype.hasOwnProperty.call(data, 'ok') && !data.ok)) {
        throw new Error(data.message || ('HTTP ' + response.status));
      }

      return data;
    }

    async function postForm(url, values) {
      return fetchJson(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
        body: new URLSearchParams(values).toString()
      });
    }

    function renderSelectOptions(selectId, options) {
      const select = byId(selectId);
      select.innerHTML = (options || []).map(function (option) {
        return '<option value="' + escapeHtml(option.id) + '">' + escapeHtml(option.label) + '</option>';
      }).join('');
    }

    function syncRegionalSummary(data) {
      byId('regionalSummary').textContent =
        'Current: ' + safeText(data.timeZoneLabel, '-') + ' • ' + safeText(data.dateFormatLabel, '-');
      byId('regionalPreview').textContent = data.clockReady
        ? ('Preview: ' + safeText(data.localDate, '-') + ' • ' + safeText(data.localTime, '-') + ' • ' + safeText(data.timeZoneShort, '-'))
        : 'Preview unavailable until Wi-Fi time sync completes.';
    }

    function hydrateRegionalForms(data) {
      if (Array.isArray(data.timeZones) && data.timeZones.length) {
        renderSelectOptions('timezoneSelect', data.timeZones);
      }

      if (Array.isArray(data.dateFormats) && data.dateFormats.length) {
        renderSelectOptions('dateFormatSelect', data.dateFormats);
      }

      if (byId('timezoneSelect').options.length) {
        byId('timezoneSelect').value = safeText(data.timeZoneId, byId('timezoneSelect').value);
      }

      if (byId('dateFormatSelect').options.length) {
        byId('dateFormatSelect').value = safeText(data.dateFormatId, byId('dateFormatSelect').value);
      }

      regionalFormsHydrated = true;
      syncRegionalSummary(data);
    }

    function hydrateForms(data) {
      const ipConfig = data.stationIpConfig || {};
      byId('wifiSsidInput').value = safeText(data.configuredStationSsid, '');
      byId('wifiPasswordInput').value = '';
      byId('ipAddressInput').value = safeText(ipConfig.address, '');
      byId('gatewayInput').value = safeText(ipConfig.gateway, '');
      byId('subnetInput').value = safeText(ipConfig.subnet, '');
      byId('dns1Input').value = safeText(ipConfig.primaryDns, '');
      byId('dns2Input').value =
        ipConfig.secondaryDns && ipConfig.secondaryDns !== '0.0.0.0'
          ? ipConfig.secondaryDns
          : '';
      formsHydrated = true;
    }

    function syncIpModeControls(data) {
      const fixedMode = !!data.staticStationIpEnabled;
      const toggle = byId('ipModeToggle');

      if (!ipModeChangeInFlight) {
        toggle.checked = fixedMode;
      }

      if (ipModeChangeInFlight) {
        toggle.disabled = true;
        return;
      }

      toggle.disabled = !!data.ipModePending;

      let hint;
      if (data.ipModePending || data.wifiReconnectPending) {
        hint = 'Applying network changes now. Reopen via https://' + preferredHostName + '/ if the current IP changes.';
      } else if (data.ipMode === 'AP local') {
        hint = 'Fallback AP is active. The saved station preference is ' +
          (fixedMode ? 'fixed IP' : 'dynamic IP') +
          ' and it will apply when the station reconnects.';
      } else if (fixedMode && !data.configuredFixedIpTlsSupported) {
        hint = 'Fixed IP is enabled, but the certificate no longer matches the custom direct IP. Use https://' +
          preferredHostName + '/ after reconnect.';
      } else if (fixedMode) {
        hint = 'Fixed IP is active. Direct-IP HTTPS is certificate-valid only when the board is on the build-time fixed IP.';
      } else {
        hint = 'Dynamic IP is active. Toggle on to use the saved fixed IP settings.';
      }

      byId('ipModeHint').textContent = hint;
      byId('ipConfigNote').textContent = fixedMode
        ? (data.configuredFixedIpTlsSupported
            ? 'Saved fixed IP settings will apply immediately when changed.'
            : 'Saved fixed IP settings will apply, but the .local hostname is safer for HTTPS because the certificate no longer matches the custom IP.')
        : 'Saved fixed IP settings are stored now and will apply the next time fixed IP mode is enabled.';
    }

    function renderWifiNetworks(networks) {
      const container = byId('wifiList');
      if (!networks || !networks.length) {
        container.innerHTML = '<p class="empty">No hotspots were returned by the last scan.</p>';
        return;
      }

      container.innerHTML = networks.map(function (network) {
        const encodedSsid = encodeURIComponent(network.ssid);
        return (
          '<div class="scan-item">' +
            '<div class="scan-main">' +
              '<p class="scan-title">' + escapeHtml(network.ssid) + '</p>' +
              '<p class="scan-meta">' +
                escapeHtml(network.authMode) + ' • Ch ' + network.channel + ' • ' + network.rssiDbm + ' dBm' +
                (network.hidden ? ' • hidden' : '') +
              '</p>' +
            '</div>' +
            '<button class="secondary" type="button" data-role="wifi-select" data-ssid="' + encodedSsid + '" data-password="' +
              (network.requiresPassword ? 'true' : 'false') + '">Use</button>' +
          '</div>'
        );
      }).join('');

      Array.prototype.forEach.call(
        container.querySelectorAll('button[data-role="wifi-select"]'),
        function (button) {
          button.addEventListener('click', function () {
            const ssid = decodeURIComponent(button.dataset.ssid || '');
            const requiresPassword = button.dataset.password === 'true';
            byId('wifiSsidInput').value = ssid === '(hidden network)' ? '' : ssid;
            byId('wifiPasswordInput').value = '';
            setBanner(
              'wifiBanner',
              requiresPassword
                ? 'Selected ' + ssid + '. Enter the hotspot password, then save and reconnect.'
                : 'Selected open hotspot ' + ssid + '. Leave the password blank, then save and reconnect.',
              'info'
            );
          });
        }
      );
    }

    function renderBluetoothDevices(devices) {
      const container = byId('bluetoothList');
      if (!devices || !devices.length) {
        container.innerHTML = '<p class="empty">No nearby BLE peripherals were returned by the last scan.</p>';
        return;
      }

      container.innerHTML = devices.map(function (device) {
        const encodedAddress = encodeURIComponent(device.address);
        return (
          '<div class="scan-item">' +
            '<div class="scan-main">' +
              '<p class="scan-title">' + escapeHtml(safeText(device.name, 'Unnamed BLE device')) + '</p>' +
              '<p class="scan-meta">' + escapeHtml(device.address) + ' • ' + device.rssiDbm + ' dBm</p>' +
            '</div>' +
            '<button class="secondary" type="button" data-role="bluetooth-connect" data-address="' + encodedAddress + '">Connect</button>' +
          '</div>'
        );
      }).join('');

      Array.prototype.forEach.call(
        container.querySelectorAll('button[data-role="bluetooth-connect"]'),
        function (button) {
          button.addEventListener('click', function () {
            connectBluetoothDevice(decodeURIComponent(button.dataset.address || ''));
          });
        }
      );
    }

    function updateStatusUi(data) {
      if (pageClosing) {
        return;
      }

      if (!formsHydrated) {
        hydrateForms(data);
      }

      byId('setupSummary').textContent =
        safeText(data.connectionStatus, '-') + ' • ' +
        safeText(data.ipAddress, '-') + ' • ' +
        safeText(data.ipMode, '-') + ' IP';
      byId('wifiSummary').textContent =
        'Saved hotspot: ' + safeText(data.configuredStationSsid, 'Not set') +
        ' • Current: ' + safeText(data.ssid, 'Unavailable');
      byId('bluetoothSummary').textContent =
        safeText(data.bluetoothStatus, '-') + ' • Device: ' +
        safeText(data.bluetoothDeviceName, 'Unavailable');
      byId('disconnectBluetoothButton').disabled = !data.bluetoothConnected;

      syncRegionalSummary(data);
      syncIpModeControls(data);

      byId('lastUpdated').textContent = new Date().toLocaleTimeString();
    }

    async function refreshStatus() {
      if (pageClosing || bluetoothScanInFlight) {
        return;
      }

      try {
        const data = await fetchJson('/api/status', { cache: 'no-store' });
        updateStatusUi(data);
      } catch (error) {
        if (pageClosing) {
          return;
        }

        byId('lastUpdated').textContent = 'refresh failed';
      }
    }

    async function loadRegionalSettings() {
      if (pageClosing) {
        return;
      }

      try {
        const data = await fetchJson('/api/regional/settings', { cache: 'no-store' });
        hydrateRegionalForms(data);
        setBanner('regionalBanner', '', 'info');
      } catch (error) {
        if (pageClosing) {
          return;
        }

        setBanner('regionalBanner', error.message || 'Regional settings could not be loaded.', 'warn');
      }
    }

    function delayMs(ms) {
      return new Promise(function (resolve) {
        window.setTimeout(resolve, ms);
      });
    }

    async function updateIpMode(useFixed) {
      const toggle = byId('ipModeToggle');
      ipModeChangeInFlight = true;
      toggle.disabled = true;
      byId('ipModeHint').textContent = 'Saving IP mode and preparing a Wi-Fi reconnect...';

      try {
        const result = await postForm('/api/network/ip-mode', {
          mode: useFixed ? 'fixed' : 'dynamic'
        });

        ipModeChangeInFlight = false;
        formsHydrated = false;
        toggle.checked = !!result.staticStationIpEnabled;
        toggle.disabled = !!result.ipModePending;
        byId('ipModeHint').textContent = result.message;
        setBanner('ipBanner', result.message, 'ok');
        await refreshStatus();
      } catch (error) {
        toggle.checked = !useFixed;
        toggle.disabled = false;
        ipModeChangeInFlight = false;
        byId('ipModeHint').textContent = 'Updating the IP mode failed. Please retry from setup.';
        setBanner('ipBanner', error.message || 'Updating the IP mode failed.', 'warn');
      }
    }

    async function scanWifiNetworks() {
      byId('scanWifiButton').disabled = true;
      setBanner('wifiBanner', 'Scanning nearby Wi-Fi hotspots...', 'info');

      try {
        const result = await fetchJson('/api/network/scan', { cache: 'no-store' });
        renderWifiNetworks(result.networks || []);
        setBanner('wifiBanner', result.message, 'ok');
      } catch (error) {
        setBanner('wifiBanner', error.message || 'Wi-Fi scan failed.', 'warn');
      } finally {
        byId('scanWifiButton').disabled = false;
      }
    }

    async function saveWifiSettings() {
      const ssid = byId('wifiSsidInput').value.trim();
      const password = byId('wifiPasswordInput').value;

      if (!ssid) {
        setBanner('wifiBanner', 'Enter an SSID or choose a hotspot from the list first.', 'warn');
        return;
      }

      byId('saveWifiButton').disabled = true;
      setBanner('wifiBanner', 'Saving hotspot settings and scheduling a Wi-Fi reconnect...', 'info');

      try {
        const result = await postForm('/api/network/connect', {
          ssid: ssid,
          password: password
        });

        formsHydrated = false;
        setBanner('wifiBanner', result.message, 'ok');
        await refreshStatus();
      } catch (error) {
        setBanner('wifiBanner', error.message || 'Saving the hotspot failed.', 'warn');
      } finally {
        byId('saveWifiButton').disabled = false;
      }
    }

    async function saveIpSettings() {
      byId('saveIpButton').disabled = true;
      setBanner('ipBanner', 'Saving fixed IP settings...', 'info');

      try {
        const result = await postForm('/api/network/ip-config', {
          address: byId('ipAddressInput').value.trim(),
          gateway: byId('gatewayInput').value.trim(),
          subnet: byId('subnetInput').value.trim(),
          dns1: byId('dns1Input').value.trim(),
          dns2: byId('dns2Input').value.trim()
        });

        formsHydrated = false;
        setBanner('ipBanner', result.message, 'ok');
        await refreshStatus();
      } catch (error) {
        setBanner('ipBanner', error.message || 'Saving the IP settings failed.', 'warn');
      } finally {
        byId('saveIpButton').disabled = false;
      }
    }

    async function saveRegionalSettings() {
      if (!regionalFormsHydrated) {
        setBanner('regionalBanner', 'Regional settings are still loading. Try again in a moment.', 'warn');
        return;
      }

      byId('saveRegionalButton').disabled = true;
      setBanner('regionalBanner', 'Saving timezone and date format...', 'info');

      try {
        const result = await postForm('/api/regional/settings', {
          timezone: byId('timezoneSelect').value,
          dateFormat: byId('dateFormatSelect').value
        });

        byId('timezoneSelect').value = safeText(result.timeZoneId, byId('timezoneSelect').value);
        byId('dateFormatSelect').value = safeText(result.dateFormatId, byId('dateFormatSelect').value);
        syncRegionalSummary(result);
        setBanner('regionalBanner', result.message, 'ok');
        await refreshStatus();
      } catch (error) {
        setBanner('regionalBanner', error.message || 'Saving regional settings failed.', 'warn');
      } finally {
        byId('saveRegionalButton').disabled = false;
      }
    }

    async function scanBluetoothDevices() {
      byId('scanBluetoothButton').disabled = true;
      bluetoothScanInFlight = true;
      setBanner('bluetoothBanner', 'Scheduling a BLE scan on the board...', 'info');

      try {
        const startResult = await postForm('/api/bluetooth/scan', {});
        setBanner('bluetoothBanner', startResult.message, 'info');
        await delayMs(1500);

        let result = null;
        for (let attempt = 0; attempt < 10; ++attempt) {
          result = await fetchJson('/api/bluetooth/scan', { cache: 'no-store' });
          renderBluetoothDevices(result.devices || []);

          if (!result.scanning) {
            setBanner('bluetoothBanner', result.message, result.ok ? 'ok' : 'warn');
            await refreshStatus();
            return;
          }

          setBanner('bluetoothBanner', result.message, 'info');
          await delayMs(1500);
        }

        throw new Error('Bluetooth scan is taking longer than expected. Refresh the page in a few seconds to check whether results arrived.');
      } catch (error) {
        setBanner('bluetoothBanner', error.message || 'Bluetooth scan failed.', 'warn');
      } finally {
        bluetoothScanInFlight = false;
        byId('scanBluetoothButton').disabled = false;
        await refreshStatus();
      }
    }

    async function connectBluetoothDevice(address) {
      if (!address) {
        setBanner('bluetoothBanner', 'Choose a Bluetooth device first.', 'warn');
        return;
      }

      setBanner('bluetoothBanner', 'Connecting to BLE device ' + address + '...', 'info');

      try {
        const result = await postForm('/api/bluetooth/connect', { address: address });
        setBanner('bluetoothBanner', result.message, 'ok');
        await refreshStatus();
      } catch (error) {
        setBanner('bluetoothBanner', error.message || 'Bluetooth connection failed.', 'warn');
      }
    }

    async function disconnectBluetooth() {
      byId('disconnectBluetoothButton').disabled = true;
      setBanner('bluetoothBanner', 'Disconnecting Bluetooth device...', 'info');

      try {
        const result = await postForm('/api/bluetooth/disconnect', {});
        setBanner('bluetoothBanner', result.message, 'ok');
        await refreshStatus();
      } catch (error) {
        setBanner('bluetoothBanner', error.message || 'Bluetooth disconnect failed.', 'warn');
      } finally {
        byId('disconnectBluetoothButton').disabled = false;
      }
    }

    byId('ipModeToggle').addEventListener('change', function (event) {
      updateIpMode(event.target.checked);
    });
    byId('scanWifiButton').addEventListener('click', scanWifiNetworks);
    byId('saveWifiButton').addEventListener('click', saveWifiSettings);
    byId('saveIpButton').addEventListener('click', saveIpSettings);
    byId('saveRegionalButton').addEventListener('click', saveRegionalSettings);
    byId('scanBluetoothButton').addEventListener('click', scanBluetoothDevices);
    byId('disconnectBluetoothButton').addEventListener('click', disconnectBluetooth);
    byId('backToStatusLink').addEventListener('click', function (event) {
      event.preventDefault();
      stopSetupActivity();
      window.location.assign('/');
    });
    window.addEventListener('pagehide', stopSetupActivity);
    window.addEventListener('beforeunload', stopSetupActivity);

    loadRegionalSettings().finally(function () {
      if (pageClosing) {
        return;
      }

      refreshStatus();
      refreshTimerId = window.setInterval(refreshStatus, refreshMs);
    });
  </script>
</body>
</html>
)HTML";
  return sendChunkedResponse(req, "text/html; charset=utf-8", page);
}

esp_err_t handleStatusJson(httpd_req_t *req) {
  const String json = buildStatusJson();
  return sendChunkedResponse(req, "application/json", json.c_str());
}

esp_err_t handleRegionalSettingsGet(httpd_req_t *req) {
  return sendResponse(
      req, "application/json",
      buildRegionalSettingsJson(true, true,
                                "Regional settings loaded from the board."));
}

esp_err_t handleRegionalSettingsUpdate(httpd_req_t *req) {
  const String body = readRequestBody(req);

  String timeZoneId;
  String dateFormatId;
  getFormField(body, "timezone", &timeZoneId);
  getFormField(body, "dateFormat", &dateFormatId);
  timeZoneId.trim();
  dateFormatId.trim();

  if (timeZoneId.length() == 0 && dateFormatId.length() == 0) {
    httpd_resp_set_status(req, "400 Bad Request");
    return sendResponse(
        req, "application/json",
        buildRegionalSettingsJson(
            false, false,
            "Choose a timezone, a date format, or both before saving."));
  }

  const String previousTimeZoneId = getConfiguredTimeZoneId();
  const String previousDateFormatId = getConfiguredDateFormatId();

  if (timeZoneId.length() > 0 && !setConfiguredTimeZoneById(timeZoneId)) {
    httpd_resp_set_status(req, "400 Bad Request");
    return sendResponse(
        req, "application/json",
        buildRegionalSettingsJson(false, false,
                                  "Unsupported timezone selection."));
  }

  if (dateFormatId.length() > 0 &&
      !setConfiguredDateFormatById(dateFormatId)) {
    httpd_resp_set_status(req, "400 Bad Request");
    return sendResponse(
        req, "application/json",
        buildRegionalSettingsJson(false, false,
                                  "Unsupported date format selection."));
  }

  const bool timeZoneChanged =
      timeZoneId.length() > 0 && previousTimeZoneId != getConfiguredTimeZoneId();
  const bool dateFormatChanged = dateFormatId.length() > 0 &&
                                 previousDateFormatId !=
                                     getConfiguredDateFormatId();

  String message;
  if (timeZoneChanged && dateFormatChanged) {
    message = "Timezone and date format saved.";
  } else if (timeZoneChanged) {
    message = "Timezone saved.";
  } else if (dateFormatChanged) {
    message = "Date format saved.";
  } else {
    message = "Regional settings are already using those values.";
  }

  if (!hasSynchronizedClock()) {
    message += " The board will show the new format after Wi-Fi time sync completes.";
  }

  return sendResponse(req, "application/json",
                      buildRegionalSettingsJson(true, false, message));
}

esp_err_t handleIpModeUpdate(httpd_req_t *req) {
  const String body = readRequestBody(req);

  String mode;
  getFormField(body, "mode", &mode);
  mode.trim();
  mode.toLowerCase();

  bool useFixed = false;
  if (mode == "fixed") {
    useFixed = true;
  } else if (mode == "dynamic") {
    useFixed = false;
  } else {
    httpd_resp_set_status(req, "400 Bad Request");
    return sendResponse(
        req, "application/json",
        buildIpModeUpdateJson(false, getIpAssignmentMode(),
                              isStaticStationIpEnabled(),
                              isIpAssignmentChangePending(),
                              "Use mode=fixed or mode=dynamic."));
  }

  const bool changed =
      isStaticStationIpEnabled() != useFixed || isIpAssignmentChangePending();
  if (!setStaticStationIpEnabled(useFixed)) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return sendResponse(
        req, "application/json",
        buildIpModeUpdateJson(false, getIpAssignmentMode(),
                              isStaticStationIpEnabled(),
                              isIpAssignmentChangePending(),
                              "Could not update the IP mode."));
  }

  String message;
  if (!changed) {
    message = "IP mode is already using that setting.";
  } else if (useFixed && !isConfiguredFixedIpCertificateSupported()) {
    message =
        "Fixed IP mode saved. The board will reconnect to Wi-Fi. Use https://" +
        String(AppConfig::kStatusHostName) +
        ".local/ after reconnect because the direct-IP certificate no longer "
        "matches the custom fixed IP.";
  } else if (isStationConnected()) {
    message =
        "IP mode saved. The board will reconnect to Wi-Fi. Reopen via https://" +
        String(AppConfig::kStatusHostName) + ".local/ if the IP changes.";
  } else {
    message =
        "IP mode saved. It will apply on the next station Wi-Fi reconnect.";
  }

  return sendResponse(
      req, "application/json",
      buildIpModeUpdateJson(true, getIpAssignmentMode(), useFixed,
                            isIpAssignmentChangePending(), message));
}

esp_err_t handleNetworkScan(httpd_req_t *req) {
  std::vector<WiFiNetworkInfo> networks;
  if (!scanWiFiNetworks(&networks)) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return sendResponse(req, "application/json",
                        buildWiFiScanJson(
                            false, networks,
                            "Wi-Fi scan failed. Try again when the radio is "
                            "idle and the board has stable power."));
  }

  return sendResponse(
      req, "application/json",
      buildWiFiScanJson(
          true, networks,
          "Found " + String(networks.size()) +
              " hotspot(s). Pick one from the list or enter an SSID manually."));
}

esp_err_t handleNetworkConnect(httpd_req_t *req) {
  const String body = readRequestBody(req);

  String ssid;
  String password;
  getFormField(body, "ssid", &ssid);
  getFormField(body, "password", &password);
  ssid.trim();

  if (ssid.length() == 0) {
    httpd_resp_set_status(req, "400 Bad Request");
    return sendResponse(
        req, "application/json",
        buildWiFiConnectJson(
            false,
            "Provide an SSID or choose a hotspot from the Wi-Fi scan list."));
  }

  if (!setStationCredentials(ssid, password)) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return sendResponse(
        req, "application/json",
        buildWiFiConnectJson(false,
                             "The board could not save the Wi-Fi settings."));
  }

  String message =
      "Hotspot saved. The board will reconnect using \"" + ssid + "\".";
  if (isStaticStationIpEnabled() && !isConfiguredFixedIpCertificateSupported()) {
    message +=
        " Use https://" + String(AppConfig::kStatusHostName) +
        ".local/ after reconnect because the direct-IP certificate does not "
        "match the custom fixed IP.";
  }

  return sendResponse(req, "application/json",
                      buildWiFiConnectJson(true, message));
}

esp_err_t handleIpConfigUpdate(httpd_req_t *req) {
  const String body = readRequestBody(req);
  const StationIpConfig currentConfig = getConfiguredStationIpConfig();
  StationIpConfig updatedConfig = currentConfig;
  String invalidField;

  if (!parseIpField(body, "address", currentConfig.address, false,
                    &updatedConfig.address, &invalidField) ||
      !parseIpField(body, "gateway", currentConfig.gateway, false,
                    &updatedConfig.gateway, &invalidField) ||
      !parseIpField(body, "subnet", currentConfig.subnet, false,
                    &updatedConfig.subnet, &invalidField) ||
      !parseIpField(body, "dns1", currentConfig.primaryDns, false,
                    &updatedConfig.primaryDns, &invalidField) ||
      !parseIpField(body, "dns2", currentConfig.secondaryDns, true,
                    &updatedConfig.secondaryDns, &invalidField)) {
    httpd_resp_set_status(req, "400 Bad Request");
    return sendResponse(
        req, "application/json",
        buildIpConfigUpdateJson(
            false, "Invalid value supplied for " + invalidField + "."));
  }

  const bool changed = !stationIpConfigEquals(currentConfig, updatedConfig);
  if (!setStationIpConfig(updatedConfig)) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return sendResponse(
        req, "application/json",
        buildIpConfigUpdateJson(false,
                                "The board could not save the IP settings."));
  }

  String message;
  if (!changed) {
    message = "Fixed IP settings are already using those values.";
  } else if (isStaticStationIpEnabled()) {
    message = "Fixed IP settings saved. The board will reconnect to Wi-Fi.";
  } else {
    message =
        "Fixed IP settings saved. They will apply when fixed IP mode is enabled.";
  }

  if (updatedConfig.address != AppConfig::kStationStaticIp) {
    message +=
        " Direct-IP HTTPS will show a certificate warning on the custom IP, "
        "so use https://" +
        String(AppConfig::kStatusHostName) + ".local/ after reconnect.";
  }

  return sendResponse(req, "application/json",
                      buildIpConfigUpdateJson(true, message));
}

esp_err_t handleBluetoothScan(httpd_req_t *req) {
  std::vector<BluetoothDeviceInfo> devices;
  if (!getBluetoothDiscoveredDevices(&devices)) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return sendResponse(
        req, "application/json",
        buildBluetoothScanJson(
            false, isBluetoothScanInProgress(), devices,
            "Bluetooth scan results are unavailable right now."));
  }

  return sendResponse(
      req, "application/json",
      buildBluetoothScanJson(true, isBluetoothScanInProgress(), devices,
                             getBluetoothLastMessage()));
}

esp_err_t handleBluetoothScanStart(httpd_req_t *req) {
  std::vector<BluetoothDeviceInfo> devices;
  getBluetoothDiscoveredDevices(&devices);

  if (!requestBluetoothScan()) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return sendResponse(
        req, "application/json",
        buildBluetoothScanJson(
            false, isBluetoothScanInProgress(), devices,
            "Bluetooth scan could not be scheduled on this board."));
  }

  return sendResponse(
      req, "application/json",
      buildBluetoothScanJson(true, isBluetoothScanInProgress(), devices,
                             getBluetoothLastMessage()));
}

esp_err_t handleBluetoothConnect(httpd_req_t *req) {
  const String body = readRequestBody(req);

  String address;
  getFormField(body, "address", &address);
  address.trim();

  if (address.length() == 0) {
    httpd_resp_set_status(req, "400 Bad Request");
    return sendResponse(
        req, "application/json",
        buildBluetoothStatusJson(
            false, "Choose a Bluetooth device from the scan results first."));
  }

  if (!connectBluetoothDevice(address)) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return sendResponse(req, "application/json",
                        buildBluetoothStatusJson(false, getBluetoothLastMessage()));
  }

  return sendResponse(req, "application/json",
                      buildBluetoothStatusJson(true, getBluetoothLastMessage()));
}

esp_err_t handleBluetoothDisconnect(httpd_req_t *req) {
  if (!disconnectBluetoothDevice()) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return sendResponse(req, "application/json",
                        buildBluetoothStatusJson(false, getBluetoothLastMessage()));
  }

  return sendResponse(req, "application/json",
                      buildBluetoothStatusJson(true, getBluetoothLastMessage()));
}

esp_err_t handleHealthCheck(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
  return httpd_resp_send(req, "ok", 2);
}

String buildHttpsUrlForHost(const String &host, const String &path) {
  String url = "https://" + host;
  if (AppConfig::kStatusServerPort != 443) {
    url += ":" + String(AppConfig::kStatusServerPort);
  }

  if (path.length() == 0) {
    url += "/";
  } else {
    url += path;
  }

  return url;
}

String buildDirectHttpsUrl(const String &path) {
  return buildHttpsUrlForHost(getIpAddress(), path);
}

String buildHttpsUrl(const String &path) {
  if (isStationConnected() && mdnsStarted) {
    return buildHttpsUrlForHost(String(AppConfig::kStatusHostName) + ".local",
                                path);
  }

  return buildDirectHttpsUrl(path);
}

String buildHttpUrlForHost(const String &host, const String &path) {
  String url = "http://" + host;

  if (path.length() == 0) {
    url += "/";
  } else {
    url += path;
  }

  return url;
}

String buildDirectHttpUrl(const String &path) {
  return buildHttpUrlForHost(getIpAddress(), path);
}

void handleHttpRedirect() {
  const String location = buildHttpsUrl(redirectServer.uri());
  redirectServer.sendHeader("Cache-Control", "no-store, max-age=0");
  redirectServer.sendHeader("Location", location, true);
  redirectServer.send(302, "text/plain", "Redirecting to HTTPS");
}

void handleCertDownload() {
  redirectServer.sendHeader("Cache-Control", "no-store, max-age=0");
  redirectServer.send(
      200, "application/x-pem-file",
      reinterpret_cast<const char *>(certs_status_server_cert_pem));
}

void handleHttpLanding() {
  const String preferredHttpsUrl = buildHttpsUrl("/");
  const String preferredSetupUrl = buildHttpsUrl("/setup");
  const String directHttpsUrl = buildDirectHttpsUrl("/");

  String certNotes;
  if (isAccessPointActive() && !isStationConnected()) {
    certNotes =
        "Fallback AP mode uses a certificate valid for https://192.168.4.1/.";
  } else if (isStationConnected() && !isConfiguredFixedIpCertificateSupported()) {
    certNotes =
        "A custom fixed IP is configured. The self-signed certificate still "
        "matches esp32-status.local, not the custom direct IP address.";
  } else if (isStationConnected() && isCurrentDirectIpCertificateSupported()) {
    certNotes =
        "The current direct IP also matches the self-signed certificate, "
        "although esp32-status.local is still the easier long-term URL.";
  } else {
    certNotes =
        "The self-signed certificate is issued for esp32-status.local, so that "
        "hostname is the safest choice for HTTPS access.";
  }

  String directIpDetails =
      "<p>Direct IP URL: <code>" + htmlEscape(directHttpsUrl) + "</code>.</p>";
  if (!isCurrentDirectIpCertificateSupported()) {
    directIpDetails +=
        "<p>If you open the direct IP in HTTPS, your browser may show a "
        "hostname or certificate warning. The .local hostname avoids that when "
        "mDNS is available.</p>";
  }

  String page = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 HTTPS Setup</title>
  <style>
    body {
      margin: 0;
      min-height: 100vh;
      display: grid;
      place-items: center;
      padding: 24px;
      font-family: "Avenir Next", "Segoe UI", sans-serif;
      color: #18212c;
      background:
        radial-gradient(circle at top left, rgba(15, 118, 110, 0.18), transparent 34%),
        linear-gradient(180deg, #f8f3eb 0%, #efe6d9 100%);
    }

    .card {
      max-width: 760px;
      padding: 28px;
      border-radius: 24px;
      background: rgba(255, 252, 246, 0.92);
      border: 1px solid rgba(24, 33, 44, 0.1);
      box-shadow: 0 18px 48px rgba(24, 33, 44, 0.12);
    }

    h1 {
      margin: 0 0 12px;
      font-size: clamp(1.8rem, 4vw, 3rem);
      line-height: 1;
    }

    p {
      margin: 0 0 14px;
      color: #46515b;
      line-height: 1.55;
    }

    code {
      padding: 0.1rem 0.3rem;
      border-radius: 0.35rem;
      background: rgba(15, 118, 110, 0.1);
    }

    .actions {
      display: flex;
      flex-wrap: wrap;
      gap: 12px;
      margin-top: 18px;
    }

    a.button {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      min-height: 44px;
      padding: 0 16px;
      border-radius: 999px;
      text-decoration: none;
      font-weight: 700;
    }

    a.primary {
      color: white;
      background: #0f766e;
    }

    a.secondary {
      color: #18212c;
      background: rgba(15, 118, 110, 0.12);
    }
  </style>
</head>
<body>
  <main class="card">
    <h1>HTTPS Setup</h1>
    <p>The secure status dashboard is available at <code>__PREFERRED_HTTPS_URL__</code>.</p>
    <p>The secure setup page is available at <code>__PREFERRED_SETUP_URL__</code>.</p>
    <p>__CERT_NOTES__</p>
    __DIRECT_IP_DETAILS__
    <p>The status page shows live board and API details, while the setup page handles Wi-Fi, IP, regional, and Bluetooth controls.</p>
    <p>If your browser rejects the TLS handshake, download the certificate, trust it on your device, then open the HTTPS URL again.</p>
    <div class="actions">
      <a class="button primary" href="__PREFERRED_HTTPS_URL__">Open Status Page</a>
      <a class="button secondary" href="__PREFERRED_SETUP_URL__">Open Setup Page</a>
      <a class="button secondary" href="/cert.pem">Download Certificate</a>
    </div>
  </main>
</body>
</html>
)HTML";

  page.replace("__PREFERRED_HTTPS_URL__", htmlEscape(preferredHttpsUrl));
  page.replace("__PREFERRED_SETUP_URL__", htmlEscape(preferredSetupUrl));
  page.replace("__CERT_NOTES__", htmlEscape(certNotes));
  page.replace("__DIRECT_IP_DETAILS__", directIpDetails);
  redirectServer.sendHeader("Cache-Control", "no-store, max-age=0");
  redirectServer.send(200, "text/html; charset=utf-8", page);
}

bool registerGetHandler(const char *uri,
                        esp_err_t (*handler)(httpd_req_t *request)) {
  httpd_uri_t uriConfig = {};
  uriConfig.uri = uri;
  uriConfig.method = HTTP_GET;
  uriConfig.handler = handler;
  uriConfig.user_ctx = nullptr;
  return httpd_register_uri_handler(serverHandle, &uriConfig) == ESP_OK;
}

bool registerPostHandler(const char *uri,
                         esp_err_t (*handler)(httpd_req_t *request)) {
  httpd_uri_t uriConfig = {};
  uriConfig.uri = uri;
  uriConfig.method = HTTP_POST;
  uriConfig.handler = handler;
  uriConfig.user_ctx = nullptr;
  return httpd_register_uri_handler(serverHandle, &uriConfig) == ESP_OK;
}

void maybeStartMdns() {
  if (mdnsStarted || !isStationConnected()) {
    return;
  }

  if (!MDNS.begin(AppConfig::kStatusHostName)) {
    Serial.println("mDNS setup failed.");
    return;
  }

  MDNS.addService("https", "tcp", AppConfig::kStatusServerPort);
  mdnsStarted = true;
  Serial.printf("mDNS hostname available at https://%s.local/\n",
                AppConfig::kStatusHostName);
}

void printAccessUrls() {
  const String directHttpsUrl = buildDirectHttpsUrl("/");

  Serial.printf("HTTPS setup console available at %s\n",
                directHttpsUrl.c_str());
  Serial.printf("HTTP bootstrap page available at http://%s/\n",
                getIpAddress().c_str());

  if (isStationConnected()) {
    if (mdnsStarted) {
      Serial.printf("Preferred local URL: https://%s.local/\n",
                    AppConfig::kStatusHostName);
    }

    if (isCurrentDirectIpCertificateSupported()) {
      Serial.printf("Current direct-IP certificate matches %s\n",
                    directHttpsUrl.c_str());
    } else if (!isConfiguredFixedIpCertificateSupported()) {
      Serial.printf(
          "Custom fixed IP %s is saved. Use https://%s.local/ because the "
          "self-signed certificate still matches the build-time fixed IP %s.\n",
          getConfiguredStationIpConfig().address.toString().c_str(),
          AppConfig::kStatusHostName,
          AppConfig::kStationStaticIp.toString().c_str());
    } else if (mdnsStarted) {
      Serial.println(
          "Direct-IP HTTPS may show a hostname warning. Prefer esp32-status.local.");
    }
  } else if (isAccessPointActive()) {
    Serial.println("Fallback AP certificate is valid for https://192.168.4.1/.");
  }

  Serial.println(
      "Handshake error -0x7780 usually means the client rejected the "
      "certificate or hostname.");
}

void maybePrintAccessUrls() {
  if (!isNetworkReady()) {
    lastAnnouncedIpAddress = "";
    lastAnnouncedMdnsStarted = false;
    lastAnnouncedStationConnected = false;
    lastAnnouncedAccessPointActive = false;
    lastAnnouncedDirectIpTlsState = false;
    return;
  }

  const bool stationConnected = isStationConnected();
  const bool accessPointEnabled = isAccessPointActive();
  const bool directIpTlsSupported = isCurrentDirectIpCertificateSupported();
  const String ipAddress = getIpAddress();

  if (ipAddress == lastAnnouncedIpAddress &&
      mdnsStarted == lastAnnouncedMdnsStarted &&
      stationConnected == lastAnnouncedStationConnected &&
      accessPointEnabled == lastAnnouncedAccessPointActive &&
      directIpTlsSupported == lastAnnouncedDirectIpTlsState) {
    return;
  }

  lastAnnouncedIpAddress = ipAddress;
  lastAnnouncedMdnsStarted = mdnsStarted;
  lastAnnouncedStationConnected = stationConnected;
  lastAnnouncedAccessPointActive = accessPointEnabled;
  lastAnnouncedDirectIpTlsState = directIpTlsSupported;
  printAccessUrls();
}

}  // namespace

void initializeStatusServer() {
  if (serverHandle != nullptr) {
    return;
  }

  httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
  config.port_secure = AppConfig::kStatusServerPort;
  config.httpd.stack_size = 16384;
  config.httpd.max_open_sockets = 2;
  config.httpd.max_uri_handlers = 18;
  config.httpd.max_resp_headers = 12;
  config.cacert_pem = certs_status_server_cert_pem;
  config.cacert_len = certs_status_server_cert_pem_len;
  config.prvtkey_pem = certs_status_server_key_pem;
  config.prvtkey_len = certs_status_server_key_pem_len;

  const esp_err_t startResult = httpd_ssl_start(&serverHandle, &config);
  if (startResult != ESP_OK) {
    Serial.printf("Failed to start HTTPS status server: %s\n",
                  esp_err_to_name(startResult));
    serverHandle = nullptr;
    return;
  }

  Serial.printf("HTTPS status server started on port %u.\n",
                AppConfig::kStatusServerPort);

  const bool dashboardRegistered = registerGetHandler("/", handleDashboard);
  const bool setupPageRegistered = registerGetHandler("/setup", handleSetupPage);
  const bool statusRegistered =
      registerGetHandler("/api/status", handleStatusJson);
  const bool regionalSettingsRegistered =
      registerGetHandler("/api/regional/settings", handleRegionalSettingsGet);
  const bool networkScanRegistered =
      registerGetHandler("/api/network/scan", handleNetworkScan);
  const bool bluetoothScanRegistered =
      registerGetHandler("/api/bluetooth/scan", handleBluetoothScan);
  const bool bluetoothScanStartRegistered =
      registerPostHandler("/api/bluetooth/scan", handleBluetoothScanStart);
  const bool healthRegistered =
      registerGetHandler("/healthz", handleHealthCheck);
  const bool ipModeRegistered =
      registerPostHandler("/api/network/ip-mode", handleIpModeUpdate);
  const bool wifiConnectRegistered =
      registerPostHandler("/api/network/connect", handleNetworkConnect);
  const bool ipConfigRegistered =
      registerPostHandler("/api/network/ip-config", handleIpConfigUpdate);
  const bool regionalUpdateRegistered = registerPostHandler(
      "/api/regional/settings", handleRegionalSettingsUpdate);
  const bool bluetoothConnectRegistered =
      registerPostHandler("/api/bluetooth/connect", handleBluetoothConnect);
  const bool bluetoothDisconnectRegistered = registerPostHandler(
      "/api/bluetooth/disconnect", handleBluetoothDisconnect);

  if (!dashboardRegistered || !setupPageRegistered || !statusRegistered ||
      !regionalSettingsRegistered || !networkScanRegistered ||
      !bluetoothScanRegistered || !bluetoothScanStartRegistered ||
      !healthRegistered || !ipModeRegistered || !wifiConnectRegistered ||
      !ipConfigRegistered || !regionalUpdateRegistered ||
      !bluetoothConnectRegistered || !bluetoothDisconnectRegistered) {
    Serial.println("HTTPS setup server started, but not all routes registered.");
  }

  redirectServer.on("/", handleHttpLanding);
  redirectServer.on("/setup", handleHttpRedirect);
  redirectServer.on("/secure", handleHttpRedirect);
  redirectServer.on("/cert.pem", handleCertDownload);
  redirectServer.on("/api/status", handleHttpRedirect);
  redirectServer.on("/api/regional/settings", handleHttpRedirect);
  redirectServer.on("/api/network/scan", handleHttpRedirect);
  redirectServer.on("/api/bluetooth/scan", handleHttpRedirect);
  redirectServer.on("/healthz", handleHttpRedirect);
  redirectServer.onNotFound(handleHttpLanding);
  redirectServer.begin();
  redirectServerStarted = true;
  Serial.println("HTTP bootstrap server started on port 80.");

  maybeStartMdns();

  if (isNetworkReady()) {
    maybePrintAccessUrls();
  } else {
    Serial.printf(
        "HTTPS setup server started on port %u, but no network is active yet.\n",
        AppConfig::kStatusServerPort);
  }
}

void handleStatusServer() {
  if (serverHandle == nullptr) {
    return;
  }

  maybeStartMdns();
  maybePrintAccessUrls();

  if (redirectServerStarted) {
    redirectServer.handleClient();
  }
}
