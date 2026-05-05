#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <vector>

struct StationIpConfig {
  IPAddress address;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress primaryDns;
  IPAddress secondaryDns;
};

struct WiFiNetworkInfo {
  String ssid;
  int32_t rssiDbm;
  uint8_t channel;
  String authMode;
  bool requiresPassword;
  bool hidden;
};

bool connectWiFi();
void updateConnectivity();
bool isStationConnected();
bool isAccessPointActive();
bool isNetworkReady();
String getNetworkModeName();
String getNetworkName();
String getAccessPointName();
String getConfiguredStationSsid();
bool isStaticStationIpEnabled();
bool isIpAssignmentChangePending();
bool isWiFiReconnectPending();
bool setStaticStationIpEnabled(bool enabled);
StationIpConfig getConfiguredStationIpConfig();
bool setStationIpConfig(const StationIpConfig &config);
bool setStationCredentials(const String &ssid, const String &password);
bool scanWiFiNetworks(std::vector<WiFiNetworkInfo> *results);
String getIpAssignmentMode();
String getIpAddress();
String getMacAddress();
long getSignalStrengthDbm();
String getConnectionStatusText();
