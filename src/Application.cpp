#include <Arduino.h>

#include "Application.h"
#include "Connectivity.h"
#include "Heartbeat.h"
#include "StatusDisplay.h"
#include "StatusServer.h"

void setupApplication() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("ESP32 application starting...");

  initializeHeartbeat();
  initializeStatusDisplay();
  connectWiFi();
  initializeStatusServer();
}

void runApplication() {
  updateConnectivity();
  updateHeartbeat();
  updateStatusDisplay();
  handleStatusServer();
}
