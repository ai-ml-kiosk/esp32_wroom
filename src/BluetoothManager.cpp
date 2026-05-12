#include <Arduino.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <vector>

#include <NimBLEDevice.h>

#include "AppConfig.h"
#include "BluetoothManager.h"
#include "Connectivity.h"

namespace {

struct CachedBluetoothDevice {
  BluetoothDeviceInfo info;
  uint8_t addressType = BLE_ADDR_PUBLIC;
};

bool bluetoothInitialized = false;
bool bluetoothReady = false;
bool bluetoothConnected = false;
bool bluetoothScanRequested = false;
bool bluetoothScanInProgress = false;
unsigned long bluetoothScanNotBeforeMs = 0;
String connectedDeviceName = "Unavailable";
String connectedDeviceAddress = "Unavailable";
String bluetoothLastMessage =
    "Bluetooth is idle. Use Scan BLE Devices to search for peripherals.";
std::vector<CachedBluetoothDevice> cachedBluetoothDevices;
SemaphoreHandle_t bluetoothStateMutex = nullptr;
TaskHandle_t bluetoothWorkerHandle = nullptr;
NimBLEClient *bleClient = nullptr;

constexpr unsigned long kBluetoothScanQuietPeriodMs = 1200;
constexpr uint32_t kBluetoothScanDurationMs = 3000;
constexpr uint8_t kBluetoothScanMaxResults = 24;

String normalizeBluetoothAddress(const String &value) {
  String normalized = value;
  normalized.trim();
  normalized.toUpperCase();
  return normalized;
}

String getDeviceDisplayName(const BluetoothDeviceInfo &device) {
  if (device.name.length() > 0) {
    return device.name;
  }

  return "Unnamed BLE device";
}

void ensureBluetoothStateMutex() {
  if (bluetoothStateMutex == nullptr) {
    bluetoothStateMutex = xSemaphoreCreateMutex();
  }
}

bool lockBluetoothState(const TickType_t timeout = pdMS_TO_TICKS(50)) {
  ensureBluetoothStateMutex();
  if (bluetoothStateMutex == nullptr) {
    return false;
  }

  return xSemaphoreTake(bluetoothStateMutex, timeout) == pdTRUE;
}

void unlockBluetoothState() {
  if (bluetoothStateMutex != nullptr) {
    xSemaphoreGive(bluetoothStateMutex);
  }
}

void markDisconnectedLocked(const String &message) {
  bluetoothConnected = false;
  connectedDeviceName = "Unavailable";
  connectedDeviceAddress = "Unavailable";
  bluetoothLastMessage = message;
}

void markDisconnected(const String &message) {
  if (!lockBluetoothState()) {
    return;
  }

  markDisconnectedLocked(message);
  unlockBluetoothState();
}

class StatusBleClientCallbacks : public NimBLEClientCallbacks {
 public:
  void onConnect(NimBLEClient *client) override;
  void onConnectFail(NimBLEClient *client, int reason) override;
  void onDisconnect(NimBLEClient *client, int reason) override;
};

StatusBleClientCallbacks bleClientCallbacks;

void StatusBleClientCallbacks::onConnect(NimBLEClient *client) {
  if (!lockBluetoothState()) {
    return;
  }

  bluetoothConnected = client != nullptr && client->isConnected();
  unlockBluetoothState();
}

void StatusBleClientCallbacks::onConnectFail(NimBLEClient *client,
                                             int reason) {
  (void)client;
  markDisconnected("Bluetooth connection failed (reason " + String(reason) +
                   ").");
}

void StatusBleClientCallbacks::onDisconnect(NimBLEClient *client, int reason) {
  (void)client;
  markDisconnected("Bluetooth device disconnected (reason " +
                   String(reason) + ").");
}

BluetoothDeviceInfo makeBluetoothDeviceInfo(
    const NimBLEAdvertisedDevice *device) {
  BluetoothDeviceInfo info;
  if (device == nullptr) {
    info.name = "Unnamed BLE device";
    info.address = "Unavailable";
    info.rssiDbm = 0;
    return info;
  }

  if (device->haveName()) {
    const std::string name = device->getName();
    info.name = String(name.c_str());
  } else {
    info.name = "Unnamed BLE device";
  }

  const std::string address = device->getAddress().toString();
  info.address = String(address.c_str());
  info.address.toUpperCase();
  info.rssiDbm = device->getRSSI();
  return info;
}

bool ensureBleClient() {
  if (bleClient != nullptr) {
    return true;
  }

  bleClient = NimBLEDevice::createClient();
  if (bleClient == nullptr) {
    if (lockBluetoothState()) {
      bluetoothLastMessage = "Could not create a BLE client on the ESP32.";
      unlockBluetoothState();
    }
    return false;
  }

  bleClient->setClientCallbacks(&bleClientCallbacks, false);
  bleClient->setConnectTimeout(5000);
  bleClient->setConnectRetries(0);
  bleClient->setSelfDelete(false, false);
  return true;
}

void releaseBluetoothStackIfIdle(const String &message, bool keepReady) {
  bool shouldRelease = false;
  if (lockBluetoothState()) {
    shouldRelease =
        bluetoothInitialized && !bluetoothConnected && !bluetoothScanRequested &&
        !bluetoothScanInProgress;
    unlockBluetoothState();
  }

  if (!shouldRelease || !NimBLEDevice::isInitialized()) {
    return;
  }

  if (bleClient != nullptr) {
    NimBLEDevice::deleteClient(bleClient);
    bleClient = nullptr;
  }

  const bool released = NimBLEDevice::deinit(true);
  Serial.printf("Bluetooth stack %s. Free heap: %u\n",
                released ? "released" : "release failed",
                static_cast<unsigned int>(ESP.getFreeHeap()));
  if (!released) {
    return;
  }

  if (!lockBluetoothState()) {
    return;
  }

  bluetoothInitialized = false;
  bluetoothReady = keepReady;
  if (message.length() > 0) {
    bluetoothLastMessage = message;
  }
  unlockBluetoothState();
}

void copyCachedBluetoothDevicesLocked(
    std::vector<BluetoothDeviceInfo> *results) {
  if (results == nullptr) {
    return;
  }

  results->clear();
  results->reserve(cachedBluetoothDevices.size());
  for (size_t index = 0; index < cachedBluetoothDevices.size(); ++index) {
    results->push_back(cachedBluetoothDevices[index].info);
  }
}

bool ensureBluetoothInitializedForWorker(String *message) {
  if (lockBluetoothState()) {
    if (bluetoothInitialized) {
      const bool ready = bluetoothReady;
      const String currentMessage = bluetoothLastMessage;
      unlockBluetoothState();
      if (message != nullptr) {
        *message = currentMessage;
      }
      return ready;
    }

    bluetoothLastMessage = "Starting Bluetooth LE...";
    unlockBluetoothState();
  }

  if (!NimBLEDevice::isInitialized()) {
    const String deviceName = getStationHostName();
    const bool initialized = NimBLEDevice::init(deviceName.c_str());
    if (!initialized) {
      if (lockBluetoothState()) {
        bluetoothInitialized = true;
        bluetoothReady = false;
        bluetoothLastMessage =
            "Bluetooth LE could not be initialized on this firmware build.";
        unlockBluetoothState();
      }

      if (message != nullptr) {
        *message =
            "Bluetooth LE could not be initialized on this firmware build.";
      }
      return false;
    }
  }

  if (lockBluetoothState()) {
    bluetoothInitialized = true;
    bluetoothReady = true;
    bluetoothLastMessage =
        "Bluetooth LE is ready. Scan nearby devices from the dashboard.";
    unlockBluetoothState();
  }

  if (message != nullptr) {
    *message = "Bluetooth LE is ready. Scan nearby devices from the dashboard.";
  }

  Serial.printf("NimBLE initialized. Free heap: %u\n",
                static_cast<unsigned int>(ESP.getFreeHeap()));
  return true;
}

bool performBluetoothScan(std::vector<CachedBluetoothDevice> *devices,
                          String *message) {
  if (devices == nullptr) {
    return false;
  }

  devices->clear();

  String initializationMessage;
  if (!ensureBluetoothInitializedForWorker(&initializationMessage)) {
    if (message != nullptr) {
      *message = initializationMessage;
    }
    return false;
  }

  NimBLEScan *scan = NimBLEDevice::getScan();
  if (scan == nullptr) {
    if (message != nullptr) {
      *message = "BLE scan is unavailable on this board session.";
    }
    return false;
  }

  if (scan->isScanning()) {
    scan->stop();
  }

  scan->clearResults();
  scan->setActiveScan(false);
  scan->setInterval(75);
  scan->setWindow(75);
  scan->setDuplicateFilter(1);
  scan->setMaxResults(kBluetoothScanMaxResults);

  NimBLEScanResults scanResults =
      scan->getResults(kBluetoothScanDurationMs, false);
  const int count = scanResults.getCount();
  if (count < 0) {
    if (message != nullptr) {
      *message = "BLE scan failed.";
    }
    scan->clearResults();
    return false;
  }

  devices->reserve(static_cast<size_t>(count));
  for (int index = 0; index < count; ++index) {
    const NimBLEAdvertisedDevice *advertisedDevice =
        scanResults.getDevice(static_cast<uint32_t>(index));
    if (advertisedDevice == nullptr) {
      continue;
    }

    CachedBluetoothDevice cachedDevice;
    cachedDevice.info = makeBluetoothDeviceInfo(advertisedDevice);
    cachedDevice.addressType = advertisedDevice->getAddressType();
    devices->push_back(cachedDevice);
  }

  scan->clearResults();

  if (message != nullptr) {
    *message =
        "Found " + String(devices->size()) + " nearby BLE device(s).";
  }
  return true;
}

void bluetoothWorkerTask(void *parameter) {
  (void)parameter;

  for (;;) {
    bool shouldRunScan = false;
    if (lockBluetoothState()) {
      if (bluetoothScanRequested && !bluetoothScanInProgress &&
          millis() >= bluetoothScanNotBeforeMs) {
        bluetoothScanRequested = false;
        bluetoothScanInProgress = true;
        bluetoothLastMessage = "Scanning nearby BLE devices...";
        shouldRunScan = true;
      }
      unlockBluetoothState();
    }

    if (shouldRunScan) {
      std::vector<CachedBluetoothDevice> scannedDevices;
      String message;
      const bool ok = performBluetoothScan(&scannedDevices, &message);

      if (lockBluetoothState()) {
        cachedBluetoothDevices.swap(scannedDevices);
        bluetoothScanInProgress = false;
        bluetoothReady = bluetoothReady || ok;
        bluetoothLastMessage = message;
        unlockBluetoothState();
      }

      if (ok) {
        releaseBluetoothStackIfIdle(
            message +
                " Results cached; the BLE radio was released to keep HTTPS "
                "responsive.",
            true);
      } else {
        releaseBluetoothStackIfIdle(message, false);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

bool ensureBluetoothWorkerTask() {
  if (bluetoothWorkerHandle != nullptr) {
    return true;
  }

  const BaseType_t createResult = xTaskCreatePinnedToCore(
      bluetoothWorkerTask, "bluetooth-worker", 12288, nullptr, 1,
      &bluetoothWorkerHandle, tskNO_AFFINITY);
  if (createResult == pdPASS) {
    return true;
  }

  bluetoothWorkerHandle = nullptr;
  if (lockBluetoothState()) {
    bluetoothLastMessage = "Bluetooth worker task could not be started.";
    unlockBluetoothState();
  }
  return false;
}

}  // namespace

void initializeBluetoothManager() {
  ensureBluetoothStateMutex();
  ensureBluetoothWorkerTask();

  if (!lockBluetoothState()) {
    return;
  }

  bluetoothLastMessage =
      "Bluetooth is idle. Use Scan BLE Devices to search for peripherals.";
  unlockBluetoothState();
}

void updateBluetoothManager() {
  bool connectedSnapshot = false;
  if (lockBluetoothState()) {
    connectedSnapshot = bluetoothConnected;
    unlockBluetoothState();
  }

  if (bleClient != nullptr && connectedSnapshot && !bleClient->isConnected()) {
    markDisconnected("Bluetooth device disconnected.");
  }
}

bool isBluetoothReady() {
  if (!lockBluetoothState()) {
    return false;
  }

  const bool ready = bluetoothReady;
  unlockBluetoothState();
  return ready;
}

bool isBluetoothConnected() {
  if (!lockBluetoothState()) {
    return false;
  }

  const bool connected = bluetoothConnected;
  unlockBluetoothState();
  return connected;
}

bool isBluetoothScanInProgress() {
  if (!lockBluetoothState()) {
    return false;
  }

  const bool inProgress = bluetoothScanRequested || bluetoothScanInProgress;
  unlockBluetoothState();
  return inProgress;
}

String getBluetoothStatusText() {
  if (!lockBluetoothState()) {
    return "Unavailable";
  }

  const bool initialized = bluetoothInitialized;
  const bool ready = bluetoothReady;
  const bool connected = bluetoothConnected;
  const bool scanBusy = bluetoothScanRequested || bluetoothScanInProgress;
  unlockBluetoothState();

  if (scanBusy) {
    return "Scanning";
  }

  if (connected) {
    return "Connected";
  }

  if (ready) {
    return "Ready";
  }

  if (!initialized) {
    return "On-demand";
  }

  if (!ready) {
    return "Unavailable";
  }

  return "Ready";
}

String getBluetoothConnectedDeviceName() {
  if (!lockBluetoothState()) {
    return "Unavailable";
  }

  const String name = connectedDeviceName;
  unlockBluetoothState();
  return name;
}

String getBluetoothConnectedDeviceAddress() {
  if (!lockBluetoothState()) {
    return "Unavailable";
  }

  const String address = connectedDeviceAddress;
  unlockBluetoothState();
  return address;
}

String getBluetoothLastMessage() {
  if (!lockBluetoothState()) {
    return "Bluetooth state is unavailable.";
  }

  const String message = bluetoothLastMessage;
  unlockBluetoothState();
  return message;
}

size_t getBluetoothDiscoveredDeviceCount() {
  if (!lockBluetoothState()) {
    return 0;
  }

  const size_t count = cachedBluetoothDevices.size();
  unlockBluetoothState();
  return count;
}

bool requestBluetoothScan() {
  if (!ensureBluetoothWorkerTask()) {
    return false;
  }

  if (!lockBluetoothState()) {
    return false;
  }

  if (bluetoothScanRequested || bluetoothScanInProgress) {
    bluetoothLastMessage = "Bluetooth scan is already running.";
    unlockBluetoothState();
    return true;
  }

  cachedBluetoothDevices.clear();
  bluetoothScanRequested = true;
  bluetoothScanNotBeforeMs = millis() + kBluetoothScanQuietPeriodMs;
  bluetoothLastMessage =
      "Bluetooth scan queued. Results will appear shortly.";
  unlockBluetoothState();
  return true;
}

bool getBluetoothDiscoveredDevices(std::vector<BluetoothDeviceInfo> *results) {
  if (results == nullptr) {
    return false;
  }

  if (!lockBluetoothState()) {
    return false;
  }

  copyCachedBluetoothDevicesLocked(results);
  unlockBluetoothState();
  return true;
}

bool scanBluetoothDevices(std::vector<BluetoothDeviceInfo> *results) {
  return getBluetoothDiscoveredDevices(results);
}

bool connectBluetoothDevice(const String &address) {
  const String normalizedAddress = normalizeBluetoothAddress(address);
  if (normalizedAddress.length() == 0) {
    if (lockBluetoothState()) {
      bluetoothLastMessage = "Choose a Bluetooth device address first.";
      unlockBluetoothState();
    }
    return false;
  }

  CachedBluetoothDevice cachedDevice;
  bool scanBusySnapshot = false;
  bool cachedDeviceFound = false;

  if (lockBluetoothState()) {
    scanBusySnapshot = bluetoothScanRequested || bluetoothScanInProgress;
    for (size_t index = 0; index < cachedBluetoothDevices.size(); ++index) {
      if (cachedBluetoothDevices[index].info.address == normalizedAddress) {
        cachedDevice = cachedBluetoothDevices[index];
        cachedDeviceFound = true;
        break;
      }
    }
    unlockBluetoothState();
  }

  if (scanBusySnapshot) {
    if (lockBluetoothState()) {
      bluetoothLastMessage =
          "Wait for the Bluetooth scan to finish before connecting.";
      unlockBluetoothState();
    }
    return false;
  }

  if (!cachedDeviceFound) {
    if (lockBluetoothState()) {
      bluetoothLastMessage =
          "The selected Bluetooth device is no longer in the latest scan "
          "results. Scan again, then retry.";
      unlockBluetoothState();
    }
    return false;
  }

  String initializationMessage;
  if (!ensureBluetoothInitializedForWorker(&initializationMessage)) {
    if (lockBluetoothState()) {
      bluetoothLastMessage = initializationMessage;
      unlockBluetoothState();
    }
    return false;
  }

  if (!ensureBleClient()) {
    return false;
  }

  if (bleClient->isConnected()) {
    bleClient->disconnect();
    delay(150);
  }

  bleClient->setConnectionParams(12, 24, 0, 150);

  if (lockBluetoothState()) {
    bluetoothLastMessage =
        "Connecting to " + getDeviceDisplayName(cachedDevice.info) + "...";
    unlockBluetoothState();
  }

  const NimBLEAddress peerAddress(
      std::string(normalizedAddress.c_str()), cachedDevice.addressType);
  const bool connected = bleClient->connect(peerAddress, true, false, false);
  if (!connected || !bleClient->isConnected()) {
    const int lastError = bleClient->getLastError();
    const String failureMessage = "Bluetooth connection failed (reason " +
                                  String(lastError) + ").";
    markDisconnected(failureMessage);
    releaseBluetoothStackIfIdle(
        failureMessage +
            " The BLE radio was released so the HTTPS dashboard stays "
            "reachable.",
        true);
    return false;
  }

  if (!lockBluetoothState()) {
    return false;
  }

  bluetoothConnected = true;
  connectedDeviceName = getDeviceDisplayName(cachedDevice.info);
  connectedDeviceAddress = cachedDevice.info.address;
  bluetoothLastMessage =
      "Connected to " + connectedDeviceName + " (" + connectedDeviceAddress +
      ").";
  unlockBluetoothState();
  return true;
}

bool disconnectBluetoothDevice() {
  bool initializedSnapshot = false;
  bool readySnapshot = false;

  if (lockBluetoothState()) {
    initializedSnapshot = bluetoothInitialized;
    readySnapshot = bluetoothReady;
    unlockBluetoothState();
  }

  if (!initializedSnapshot) {
    markDisconnected("No Bluetooth device is currently connected.");
    return true;
  }

  if (!readySnapshot) {
    return false;
  }

  if (bleClient == nullptr || !bleClient->isConnected()) {
    markDisconnected("No Bluetooth device is currently connected.");
    return true;
  }

  const String previousDeviceName = getBluetoothConnectedDeviceName();
  bleClient->disconnect();
  markDisconnected("Disconnected from " + previousDeviceName + ".");
  releaseBluetoothStackIfIdle(
      "Disconnected from " + previousDeviceName +
          ". The BLE radio was released to keep HTTPS responsive.",
      true);
  return true;
}
