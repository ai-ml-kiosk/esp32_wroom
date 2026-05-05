#pragma once

#include <Arduino.h>

bool connectWiFi();
void updateConnectivity();
bool isStationConnected();
bool isAccessPointActive();
bool isNetworkReady();
String getNetworkModeName();
String getNetworkName();
String getAccessPointName();
bool isStaticStationIpEnabled();
bool isIpAssignmentChangePending();
bool setStaticStationIpEnabled(bool enabled);
String getIpAssignmentMode();
String getIpAddress();
String getMacAddress();
long getSignalStrengthDbm();
String getConnectionStatusText();
