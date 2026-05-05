#pragma once

#include <Arduino.h>

#include <vector>

struct BluetoothDeviceInfo {
  String name;
  String address;
  int rssiDbm;
};

void initializeBluetoothManager();
void updateBluetoothManager();
bool isBluetoothReady();
bool isBluetoothConnected();
bool isBluetoothScanInProgress();
String getBluetoothStatusText();
String getBluetoothConnectedDeviceName();
String getBluetoothConnectedDeviceAddress();
String getBluetoothLastMessage();
size_t getBluetoothDiscoveredDeviceCount();
bool requestBluetoothScan();
bool getBluetoothDiscoveredDevices(std::vector<BluetoothDeviceInfo> *results);
bool scanBluetoothDevices(std::vector<BluetoothDeviceInfo> *results);
bool connectBluetoothDevice(const String &address);
bool disconnectBluetoothDevice();
