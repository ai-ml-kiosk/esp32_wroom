#include <Arduino.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <esp_err.h>
#include <esp_http_server.h>
#include <esp_https_server.h>

#include "AppConfig.h"
#include "Connectivity.h"
#include "Heartbeat.h"
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

String jsonEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);

  for (size_t i = 0; i < value.length(); ++i) {
    const char current = value[i];
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

String buildStatusJson() {
  const bool stationConnected = isStationConnected();
  const bool accessPointEnabled = isAccessPointActive();
  const bool staticIpEnabled = isStaticStationIpEnabled();
  const unsigned long uptimeMs = millis();

  String json;
  json.reserve(768);

  json += "{";
  json += "\"device\":\"" + jsonEscape(ESP.getChipModel()) + "\",";
  json += "\"uptimeMs\":" + String(uptimeMs) + ",";
  json += "\"uptime\":\"" + jsonEscape(formatUptime(uptimeMs)) + "\",";
  json += "\"freeHeapBytes\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"cpuMHz\":" + String(ESP.getCpuFreqMHz()) + ",";
  json += "\"flashBytes\":" + String(ESP.getFlashChipSize()) + ",";
  json += "\"sdkVersion\":\"" + jsonEscape(String(ESP.getSdkVersion())) + "\",";
  json += "\"chipRevision\":" + String(ESP.getChipRevision()) + ",";
  json += "\"networkMode\":\"" + jsonEscape(getNetworkModeName()) + "\",";
  json += "\"connectionStatus\":\"" + jsonEscape(getConnectionStatusText()) +
          "\",";
  json += "\"ssid\":\"" + jsonEscape(getNetworkName()) + "\",";
  json += "\"ipMode\":\"" + jsonEscape(getIpAssignmentMode()) + "\",";
  json += "\"staticStationIpEnabled\":";
  json += staticIpEnabled ? "true," : "false,";
  json += "\"ipModePending\":";
  json += isIpAssignmentChangePending() ? "true," : "false,";
  json += "\"ipAddress\":\"" + jsonEscape(getIpAddress()) + "\",";
  json += "\"macAddress\":\"" + jsonEscape(getMacAddress()) + "\",";
  json += "\"wifiConnected\":";
  json += stationConnected ? "true," : "false,";
  json += "\"accessPointActive\":";
  json += accessPointEnabled ? "true," : "false,";
  json += "\"ledOn\":";
  json += isHeartbeatLedOn() ? "true," : "false,";
  json += "\"heartbeatIntervalMs\":" + String(getHeartbeatIntervalMs()) + ",";
  json += "\"rssiDbm\":";
  if (stationConnected) {
    json += String(getSignalStrengthDbm());
  } else {
    json += "null";
  }
  json += "}";

  return json;
}

esp_err_t sendResponse(httpd_req_t *req, const char *contentType,
                       const String &body) {
  httpd_resp_set_type(req, contentType);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
  return httpd_resp_send(req, body.c_str(), body.length());
}

String readRequestBody(httpd_req_t *req) {
  if (req->content_len <= 0 || req->content_len > 64) {
    return "";
  }

  String body;
  body.reserve(req->content_len);

  int remaining = req->content_len;
  while (remaining > 0) {
    char buffer[33];
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

String buildIpModeUpdateJson(const bool ok, const String &mode,
                             const bool staticIpEnabled, const bool pending,
                             const String &message) {
  String json;
  json.reserve(256);
  json += "{";
  json += "\"ok\":";
  json += ok ? "true," : "false,";
  json += "\"ipMode\":\"" + jsonEscape(mode) + "\",";
  json += "\"staticStationIpEnabled\":";
  json += staticIpEnabled ? "true," : "false,";
  json += "\"ipModePending\":";
  json += pending ? "true," : "false,";
  json += "\"message\":\"" + jsonEscape(message) + "\"";
  json += "}";
  return json;
}

esp_err_t handleDashboard(httpd_req_t *req) {
  String page = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Board Status</title>
  <style>
    :root {
      color-scheme: light;
      --bg: #f5f1e8;
      --surface: rgba(255, 252, 246, 0.88);
      --surface-strong: #fff9f0;
      --ink: #18212c;
      --muted: #5c6772;
      --accent: #0f766e;
      --accent-soft: rgba(15, 118, 110, 0.14);
      --warn: #b45309;
      --warn-soft: rgba(180, 83, 9, 0.15);
      --shadow: 0 18px 48px rgba(24, 33, 44, 0.12);
      --border: rgba(24, 33, 44, 0.1);
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
        radial-gradient(circle at top left, rgba(15, 118, 110, 0.18), transparent 34%),
        radial-gradient(circle at top right, rgba(180, 83, 9, 0.16), transparent 30%),
        linear-gradient(180deg, #f8f3eb 0%, #efe6d9 100%);
    }

    .shell {
      max-width: 1080px;
      margin: 0 auto;
      padding: 28px 20px 40px;
    }

    .hero {
      padding: 28px;
      border-radius: 28px;
      background: linear-gradient(135deg, rgba(255, 252, 246, 0.92), rgba(255, 248, 236, 0.78));
      border: 1px solid var(--border);
      box-shadow: var(--shadow);
      backdrop-filter: blur(14px);
    }

    .eyebrow {
      margin: 0 0 8px;
      font-size: 0.82rem;
      letter-spacing: 0.18em;
      text-transform: uppercase;
      color: var(--accent);
      font-weight: 700;
    }

    h1 {
      margin: 0;
      font-size: clamp(2rem, 4vw, 3.5rem);
      line-height: 0.98;
    }

    .subtitle {
      margin: 14px 0 0;
      color: var(--muted);
      font-size: 1rem;
      max-width: 48rem;
    }

    .pill-row {
      display: flex;
      flex-wrap: wrap;
      gap: 12px;
      margin-top: 22px;
    }

    .pill {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      padding: 10px 14px;
      border-radius: 999px;
      background: var(--surface-strong);
      border: 1px solid var(--border);
      font-weight: 600;
      color: var(--ink);
    }

    .pill::before {
      content: "";
      width: 10px;
      height: 10px;
      border-radius: 50%;
      background: currentColor;
      opacity: 0.75;
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
      gap: 18px;
      margin-top: 20px;
      grid-template-columns: repeat(auto-fit, minmax(240px, 1fr));
    }

    .card {
      padding: 22px;
      border-radius: 24px;
      background: var(--surface);
      border: 1px solid var(--border);
      box-shadow: var(--shadow);
      backdrop-filter: blur(10px);
    }

    .card h2 {
      margin: 0 0 18px;
      font-size: 1rem;
      letter-spacing: 0.06em;
      text-transform: uppercase;
      color: var(--muted);
    }

    dl {
      margin: 0;
      display: grid;
      gap: 12px;
    }

    .row {
      display: flex;
      align-items: baseline;
      justify-content: space-between;
      gap: 16px;
      border-bottom: 1px solid rgba(24, 33, 44, 0.08);
      padding-bottom: 12px;
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
    }

    .control-value {
      display: inline-flex;
      align-items: center;
      justify-content: flex-end;
      gap: 12px;
      flex-wrap: wrap;
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
      background: rgba(24, 33, 44, 0.18);
      transition: background 0.2s ease;
      cursor: pointer;
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
      box-shadow: 0 4px 10px rgba(24, 33, 44, 0.18);
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

    .mode-note {
      margin: 16px 0 0;
      color: var(--muted);
      font-size: 0.92rem;
      line-height: 1.45;
    }

    .footer {
      margin-top: 18px;
      color: var(--muted);
      font-size: 0.92rem;
      text-align: right;
    }

    @media (max-width: 640px) {
      .shell {
        padding: 16px 14px 26px;
      }

      .hero,
      .card {
        padding: 18px;
        border-radius: 20px;
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
      <p class="eyebrow">ESP32 Board Status</p>
      <h1 id="deviceName">Loading board data...</h1>
      <p class="subtitle">Live dashboard for the currently running ESP32 sketch. This page refreshes in place so you can watch network and heartbeat state without opening the serial monitor.</p>
      <div class="pill-row">
        <span class="pill warn" id="connectionPill">Checking network</span>
        <span class="pill warn" id="heartbeatPill">Checking heartbeat</span>
      </div>
    </section>

    <section class="grid">
      <article class="card">
        <h2>Network</h2>
        <dl>
          <div class="row"><dt>Mode</dt><dd id="networkMode">-</dd></div>
          <div class="row"><dt>Status</dt><dd id="connectionStatus">-</dd></div>
          <div class="row"><dt>SSID</dt><dd id="networkName">-</dd></div>
          <div class="row">
            <dt>IP Mode</dt>
            <dd class="control-value">
              <span id="ipMode">-</span>
              <label class="switch" aria-label="Toggle fixed IP mode">
                <input id="ipModeToggle" type="checkbox">
                <span class="slider"></span>
              </label>
            </dd>
          </div>
          <div class="row"><dt>IP Address</dt><dd id="ipAddress">-</dd></div>
          <div class="row"><dt>MAC Address</dt><dd id="macAddress">-</dd></div>
          <div class="row"><dt>Signal</dt><dd id="signalStrength">-</dd></div>
        </dl>
        <p class="mode-note" id="ipModeHint">Toggle on for the fixed IP setting, or off for DHCP.</p>
      </article>

      <article class="card">
        <h2>System</h2>
        <dl>
          <div class="row"><dt>Uptime</dt><dd id="uptime">-</dd></div>
          <div class="row"><dt>Free Heap</dt><dd id="freeHeap">-</dd></div>
          <div class="row"><dt>CPU</dt><dd id="cpu">-</dd></div>
          <div class="row"><dt>Flash</dt><dd id="flash">-</dd></div>
          <div class="row"><dt>SDK</dt><dd id="sdkVersion">-</dd></div>
          <div class="row"><dt>Chip Revision</dt><dd id="chipRevision">-</dd></div>
        </dl>
      </article>

      <article class="card">
        <h2>Application</h2>
        <dl>
          <div class="row"><dt>Heartbeat LED</dt><dd id="heartbeatState">-</dd></div>
          <div class="row"><dt>Heartbeat Interval</dt><dd id="heartbeatInterval">-</dd></div>
          <div class="row"><dt>Status API</dt><dd>/api/status</dd></div>
          <div class="row"><dt>Health Check</dt><dd>/healthz</dd></div>
        </dl>
      </article>
    </section>

    <p class="footer">Last updated <span id="lastUpdated">never</span></p>
  </main>

  <script>
    const refreshMs = __REFRESH_MS__;
    const preferredHostName = '__PREFERRED_HOSTNAME__.local';
    let ipModeChangeInFlight = false;

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

    function setPill(element, label, healthy) {
      element.textContent = label;
      element.classList.remove('ok', 'warn');
      element.classList.add(healthy ? 'ok' : 'warn');
    }

    function syncIpModeControls(data) {
      const fixedMode = !!data.staticStationIpEnabled;
      byId('ipMode').textContent = data.ipMode;

      const toggle = byId('ipModeToggle');
      if (!ipModeChangeInFlight) {
        toggle.checked = fixedMode;
      }

      if (ipModeChangeInFlight) {
        toggle.disabled = true;
        return;
      }

      if (data.ipModePending) {
        toggle.disabled = true;
        byId('ipModeHint').textContent =
          'Applying the change now. Reopen via https://' + preferredHostName + '/ if the current IP changes.';
        return;
      }

      toggle.disabled = false;

      if (data.ipMode === 'AP local') {
        byId('ipModeHint').textContent =
          'Fallback AP is active. The saved station preference is ' +
          (fixedMode ? 'fixed IP' : 'dynamic IP') +
          ' and it will apply when Wi-Fi client mode reconnects.';
      } else if (fixedMode) {
        byId('ipModeHint').textContent =
          'Fixed IP is active. Toggle off to switch back to DHCP / dynamic IP.';
      } else {
        byId('ipModeHint').textContent =
          'Dynamic IP is active. Toggle on to use the configured fixed IP.';
      }
    }

    async function updateIpMode(useFixed) {
      const toggle = byId('ipModeToggle');
      ipModeChangeInFlight = true;
      toggle.disabled = true;
      byId('ipModeHint').textContent = 'Saving IP mode and preparing a Wi-Fi reconnect...';

      try {
        const response = await fetch('/api/network/ip-mode', {
          method: 'POST',
          headers: { 'Content-Type': 'text/plain' },
          body: useFixed ? 'fixed' : 'dynamic'
        });

        if (!response.ok) {
          throw new Error('HTTP ' + response.status);
        }

        const result = await response.json();
        ipModeChangeInFlight = false;
        toggle.checked = !!result.staticStationIpEnabled;
        toggle.disabled = result.ipModePending;
        byId('ipMode').textContent = result.ipMode;
        byId('ipModeHint').textContent = result.message;
      } catch (error) {
        toggle.checked = !useFixed;
        toggle.disabled = false;
        ipModeChangeInFlight = false;
        byId('ipModeHint').textContent =
          'Updating the IP mode failed. Please retry from the dashboard.';
      }
    }

    async function refreshStatus() {
      try {
        const response = await fetch('/api/status', { cache: 'no-store' });
        if (!response.ok) {
          throw new Error('HTTP ' + response.status);
        }

        const data = await response.json();
        byId('deviceName').textContent = data.device;
        byId('networkMode').textContent = data.networkMode;
        byId('connectionStatus').textContent = data.connectionStatus;
        byId('networkName').textContent = data.ssid;
        syncIpModeControls(data);
        byId('ipAddress').textContent = data.ipAddress;
        byId('macAddress').textContent = data.macAddress;
        byId('signalStrength').textContent = data.rssiDbm === null ? 'n/a' : data.rssiDbm + ' dBm';
        byId('uptime').textContent = data.uptime;
        byId('freeHeap').textContent = formatBytes(data.freeHeapBytes);
        byId('cpu').textContent = data.cpuMHz + ' MHz';
        byId('flash').textContent = formatBytes(data.flashBytes);
        byId('sdkVersion').textContent = data.sdkVersion;
        byId('chipRevision').textContent = 'Rev ' + data.chipRevision;
        byId('heartbeatState').textContent = data.ledOn ? 'On' : 'Off';
        byId('heartbeatInterval').textContent = data.heartbeatIntervalMs + ' ms';
        byId('lastUpdated').textContent = new Date().toLocaleTimeString();

        setPill(
          byId('connectionPill'),
          data.wifiConnected ? 'Wi-Fi Connected' : (data.accessPointActive ? 'Access Point Active' : 'Offline'),
          data.wifiConnected || data.accessPointActive
        );

        setPill(
          byId('heartbeatPill'),
          data.ledOn ? 'Heartbeat LED On' : 'Heartbeat LED Off',
          true
        );
      } catch (error) {
        byId('lastUpdated').textContent = 'refresh failed';
        setPill(byId('connectionPill'), 'Status unavailable', false);
        if (ipModeChangeInFlight) {
          byId('ipModeHint').textContent =
            'The board is reconnecting. Try https://' + preferredHostName + '/ if the address changed.';
        }
      }
    }

    byId('ipModeToggle').addEventListener('change', (event) => {
      updateIpMode(event.target.checked);
    });

    refreshStatus();
    setInterval(refreshStatus, refreshMs);
  </script>
</body>
</html>
)HTML";

  page.replace("__REFRESH_MS__", String(AppConfig::kStatusRefreshMs));
  page.replace("__PREFERRED_HOSTNAME__", String(AppConfig::kStatusHostName));
  return sendResponse(req, "text/html; charset=utf-8", page);
}

esp_err_t handleStatusJson(httpd_req_t *req) {
  return sendResponse(req, "application/json", buildStatusJson());
}

esp_err_t handleIpModeUpdate(httpd_req_t *req) {
  String body = readRequestBody(req);
  body.trim();
  body.toLowerCase();

  bool useFixed = false;
  if (body == "fixed") {
    useFixed = true;
  } else if (body == "dynamic") {
    useFixed = false;
  } else {
    httpd_resp_set_status(req, "400 Bad Request");
    return sendResponse(
        req, "application/json",
        buildIpModeUpdateJson(false, getIpAssignmentMode(),
                              isStaticStationIpEnabled(),
                              isIpAssignmentChangePending(),
                              "Use either fixed or dynamic."));
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
    message = "IP mode already active.";
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
      buildIpModeUpdateJson(true, getIpAssignmentMode(), useFixed, changed,
                            message));
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
  const String directHttpsUrl = buildDirectHttpsUrl("/");

  String certNotes;
  if (isAccessPointActive() && !isStationConnected()) {
    certNotes = "AP mode uses a certificate valid for 192.168.4.1.";
  } else if (isStationConnected() && isStaticStationIpEnabled()) {
    certNotes = "Station mode uses a certificate valid for both esp32-status.local and the configured fixed IP address.";
  } else {
    certNotes = "Station mode certificate is issued for esp32-status.local, so that hostname is the safest choice.";
  }

  String directIpDetails;
  String directIpAction;
  if (isStationConnected() && isStaticStationIpEnabled() &&
      directHttpsUrl != preferredHttpsUrl) {
    directIpDetails =
        "<p>The direct fixed-IP dashboard is also available at <code>" +
        directHttpsUrl + "</code>.</p>";
    directIpAction =
        "<a class=\"button secondary\" href=\"" + directHttpsUrl +
        "\">Open Direct IP</a>";
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
      max-width: 720px;
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
      line-height: 1.5;
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
    <p>The secure dashboard is available at <code>__PREFERRED_HTTPS_URL__</code>.</p>
    <p>__CERT_NOTES__</p>
    __DIRECT_IP_DETAILS__
    <p>If your browser rejects the TLS handshake, download the certificate, trust it on your device, then open the HTTPS URL again.</p>
    <div class="actions">
      <a class="button primary" href="__PREFERRED_HTTPS_URL__">Open HTTPS Dashboard</a>
      __DIRECT_IP_ACTION__
      <a class="button secondary" href="/cert.pem">Download Certificate</a>
    </div>
  </main>
</body>
</html>
)HTML";

  page.replace("__PREFERRED_HTTPS_URL__", preferredHttpsUrl);
  page.replace("__CERT_NOTES__", certNotes);
  page.replace("__DIRECT_IP_DETAILS__", directIpDetails);
  page.replace("__DIRECT_IP_ACTION__", directIpAction);
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

  Serial.printf("HTTPS status dashboard available at %s\n",
                directHttpsUrl.c_str());
  Serial.printf("HTTP bootstrap page available at http://%s/\n",
                getIpAddress().c_str());

  if (isStationConnected()) {
    if (mdnsStarted) {
      Serial.printf("Preferred local URL: https://%s.local/\n",
                    AppConfig::kStatusHostName);
    }

    if (isStaticStationIpEnabled()) {
      Serial.printf("Direct fixed-IP URL is certificate-valid: %s\n",
                    directHttpsUrl.c_str());
    } else if (mdnsStarted) {
      Serial.println(
          "Dynamic-IP station mode certificate is valid for esp32-status.local, not the DHCP IP.");
    }
  } else if (isAccessPointActive()) {
    Serial.println("Fallback AP certificate is valid for https://192.168.4.1/.");
  }

  Serial.println("Handshake error -0x7780 means the client rejected the certificate or hostname.");
}

void maybePrintAccessUrls() {
  if (!isNetworkReady()) {
    lastAnnouncedIpAddress = "";
    lastAnnouncedMdnsStarted = false;
    lastAnnouncedStationConnected = false;
    lastAnnouncedAccessPointActive = false;
    return;
  }

  const bool stationConnected = isStationConnected();
  const bool accessPointActive = isAccessPointActive();
  const String ipAddress = getIpAddress();

  if (ipAddress == lastAnnouncedIpAddress &&
      mdnsStarted == lastAnnouncedMdnsStarted &&
      stationConnected == lastAnnouncedStationConnected &&
      accessPointActive == lastAnnouncedAccessPointActive) {
    return;
  }

  lastAnnouncedIpAddress = ipAddress;
  lastAnnouncedMdnsStarted = mdnsStarted;
  lastAnnouncedStationConnected = stationConnected;
  lastAnnouncedAccessPointActive = accessPointActive;
  printAccessUrls();
}

}  // namespace

void initializeStatusServer() {
  if (serverHandle != nullptr) {
    return;
  }

  httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
  config.port_secure = AppConfig::kStatusServerPort;
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

  const bool dashboardRegistered = registerGetHandler("/", handleDashboard);
  const bool statusRegistered =
      registerGetHandler("/api/status", handleStatusJson);
  const bool healthRegistered =
      registerGetHandler("/healthz", handleHealthCheck);
  const bool ipModeRegistered =
      registerPostHandler("/api/network/ip-mode", handleIpModeUpdate);

  if (!dashboardRegistered || !statusRegistered || !healthRegistered ||
      !ipModeRegistered) {
    Serial.println("HTTPS status server started, but not all routes registered.");
  }

  redirectServer.on("/", handleHttpLanding);
  redirectServer.on("/cert.pem", handleCertDownload);
  redirectServer.on("/secure", handleHttpRedirect);
  redirectServer.on("/api/status", handleHttpRedirect);
  redirectServer.on("/healthz", handleHttpRedirect);
  redirectServer.onNotFound(handleHttpLanding);
  redirectServer.begin();
  redirectServerStarted = true;

  maybeStartMdns();

  if (isNetworkReady()) {
    maybePrintAccessUrls();
  } else {
    Serial.printf("HTTPS status server started on port %u, but no network is active yet.\n",
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
