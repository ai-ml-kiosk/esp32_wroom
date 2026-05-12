#include <Arduino.h>

#include "Application.h"
#include "BluetoothManager.h"
#include "Connectivity.h"
#include "Heartbeat.h"
#include "ManageSDCard.h"
#include "PowerMonitor.h"
#include "RegionalSettings.h"
#include "StatusDisplay.h"
#include "StatusServer.h"

void setupApplication() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("ESP32 application starting...");

  initializeRegionalSettings();
  initializeHeartbeat();
  initializePowerMonitor();
  initializeSDCardManager();
  initializeStatusDisplay();
  initializeBluetoothManager();
  connectWiFi();
  initializeStatusServer();
}

void runApplication() {
  updateConnectivity();
  updateBluetoothManager();
  updateHeartbeat();
  updatePowerMonitor();
  updateSDCardManager();
  updateStatusDisplay();
  handleStatusServer();
}
