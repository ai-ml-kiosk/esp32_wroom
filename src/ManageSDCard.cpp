#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#include <algorithm>
#include <vector>

#include "AppConfig.h"
#include "ManageSDCard.h"

namespace {

#if defined(VSPI)
SPIClass sdCardSpi(VSPI);
#else
SPIClass sdCardSpi;
#endif

enum class SDCardMountState {
  NotChecked,
  Mounted,
  Ejected,
  Removed,
  Error,
  Disabled,
};

bool sdCardMounted = false;
bool sdCardSpiStarted = false;
SDCardMountState sdCardMountState = SDCardMountState::NotChecked;
String sdCardStatus = "SD card has not been read yet.";
String sdCardType = "Not checked";
unsigned long lastDetectAttemptMs = 0;

String getMountStateName(const SDCardMountState state) {
  switch (state) {
    case SDCardMountState::NotChecked:
      return "Not checked";
    case SDCardMountState::Mounted:
      return "Mounted";
    case SDCardMountState::Ejected:
      return "Ejected";
    case SDCardMountState::Removed:
      return "Removed";
    case SDCardMountState::Error:
      return "Error";
    case SDCardMountState::Disabled:
      return "Disabled";
    default:
      return "Unknown";
  }
}

String getCardTypeName(const uint8_t cardType) {
  switch (cardType) {
    case CARD_MMC:
      return "MMC";
    case CARD_SD:
      return "SDSC";
    case CARD_SDHC:
      return "SDHC/SDXC";
    case CARD_NONE:
      return "None";
    default:
      return "Unknown";
  }
}

bool hasParentTraversal(const String &path) {
  return path == ".." || path.startsWith("../") || path.endsWith("/..") ||
         path.indexOf("/../") >= 0;
}

String baseNameFromPath(const String &path) {
  const int slashIndex = path.lastIndexOf('/');
  if (slashIndex < 0 || slashIndex + 1 >= path.length()) {
    return path;
  }

  return path.substring(slashIndex + 1);
}

String childPathFor(const String &parentPath, const String &name) {
  if (name.startsWith("/")) {
    return name;
  }

  if (parentPath == "/") {
    return "/" + name;
  }

  return parentPath + "/" + name;
}

void releaseSDCardBus() {
  if (sdCardMounted || sdCardSpiStarted) {
    SD.end();
    sdCardSpi.end();
  }

  sdCardMounted = false;
  sdCardSpiStarted = false;
  lastDetectAttemptMs = 0;
}

void markSDCardUnmounted(const SDCardMountState state, const String &cardType,
                         const String &status) {
  releaseSDCardBus();
  sdCardMountState = state;
  sdCardType = cardType;
  sdCardStatus = status;
}

bool ensureMounted(String *message = nullptr) {
  if (sdCardMounted) {
    return true;
  }

  if (message != nullptr) {
    if (sdCardMountState == SDCardMountState::Ejected) {
      *message = "SD card is ejected. Use Read Card to mount it again.";
    } else if (sdCardStatus.length() > 0) {
      *message = sdCardStatus + " Use Read Card to mount the SD card.";
    } else {
      *message = "SD card is not mounted. Use Read Card before browsing files.";
    }
  }
  return false;
}

}  // namespace

void initializeSDCardManager() {
  markSDCardUnmounted(SDCardMountState::NotChecked, "Not checked",
                      "SD card has not been read yet. Use Read Card to mount it.");

  if (!AppConfig::kSDCardEnabled) {
    markSDCardUnmounted(SDCardMountState::Disabled, "Disabled",
                        "SD card manager is disabled in AppConfig.");
    Serial.println(sdCardStatus);
    return;
  }

  Serial.printf(
      "SD card manager ready on SPI pins CS=%u, SCK=%u, MISO=%u, MOSI=%u. "
      "Card checks are manual from the SD page.\n",
      AppConfig::kSDCardCsPin, AppConfig::kSDCardSckPin,
      AppConfig::kSDCardMisoPin, AppConfig::kSDCardMosiPin);
}

void updateSDCardManager() {
  if (!AppConfig::kSDCardEnabled) {
    return;
  }

  const unsigned long nowMs = millis();
  if (lastDetectAttemptMs != 0 &&
      nowMs - lastDetectAttemptMs < AppConfig::kSDCardDetectIntervalMs) {
    return;
  }

  if (!sdCardMounted) {
    return;
  }

  lastDetectAttemptMs = nowMs;
  const uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    markSDCardUnmounted(SDCardMountState::Removed, "None",
                        "SD card was removed or is no longer responding.");
    Serial.println(sdCardStatus);
  }
}

bool refreshSDCardMount() {
  if (!AppConfig::kSDCardEnabled) {
    markSDCardUnmounted(SDCardMountState::Disabled, "Disabled",
                        "SD card manager is disabled in AppConfig.");
    return false;
  }

  releaseSDCardBus();
  sdCardSpi.begin(AppConfig::kSDCardSckPin, AppConfig::kSDCardMisoPin,
                  AppConfig::kSDCardMosiPin, AppConfig::kSDCardCsPin);
  sdCardSpiStarted = true;
  lastDetectAttemptMs = millis();

  if (!SD.begin(AppConfig::kSDCardCsPin, sdCardSpi,
                AppConfig::kSDCardSpiFrequencyHz)) {
    markSDCardUnmounted(
        SDCardMountState::Error, "None",
        "No SD card detected. Check card seating, 3.3V power, and SPI wiring.");
    Serial.println(sdCardStatus);
    return false;
  }

  const uint8_t cardType = SD.cardType();
  sdCardType = getCardTypeName(cardType);
  if (cardType == CARD_NONE) {
    markSDCardUnmounted(SDCardMountState::Error, "None",
                        "SD interface started, but no card was detected.");
    Serial.println(sdCardStatus);
    return false;
  }

  sdCardMounted = true;
  sdCardMountState = SDCardMountState::Mounted;
  sdCardStatus = "SD card mounted.";
  Serial.printf("SD card mounted: %s, total=%llu bytes, used=%llu bytes.\n",
                sdCardType.c_str(),
                static_cast<unsigned long long>(SD.totalBytes()),
                static_cast<unsigned long long>(SD.usedBytes()));
  return true;
}

bool ejectSDCard(String *message) {
  if (!AppConfig::kSDCardEnabled) {
    markSDCardUnmounted(SDCardMountState::Disabled, "Disabled",
                        "SD card manager is disabled in AppConfig.");
    if (message != nullptr) {
      *message = sdCardStatus;
    }
    return false;
  }

  if (sdCardMounted) {
    markSDCardUnmounted(SDCardMountState::Ejected, "Ejected",
                        "SD card ejected. It is safe to remove the card.");
    Serial.println("SD card ejected. It is safe to remove the card.");
  } else {
    markSDCardUnmounted(
        SDCardMountState::Ejected, "Ejected",
        "SD card is already unmounted. It is safe to remove the card.");
  }

  if (message != nullptr) {
    *message = sdCardStatus;
  }
  return true;
}

bool isSDCardMounted() { return sdCardMounted; }

SDCardInfo getSDCardInfo() {
  SDCardInfo info = {
      AppConfig::kSDCardEnabled,
      sdCardMounted,
      sdCardStatus,
      getMountStateName(sdCardMountState),
      sdCardType,
      0,
      0,
      AppConfig::kSDCardCsPin,
      AppConfig::kSDCardSckPin,
      AppConfig::kSDCardMisoPin,
      AppConfig::kSDCardMosiPin,
  };

  if (sdCardMounted) {
    info.totalBytes = SD.totalBytes();
    info.usedBytes = SD.usedBytes();
  }

  return info;
}

bool normalizeSDCardPath(const String &rawPath, String *normalizedPath,
                         String *message) {
  if (normalizedPath == nullptr) {
    return false;
  }

  String path = rawPath;
  path.trim();
  if (path.length() == 0) {
    path = "/";
  }

  path.replace("\\", "/");
  while (path.indexOf("//") >= 0) {
    path.replace("//", "/");
  }

  if (!path.startsWith("/")) {
    path = "/" + path;
  }

  while (path.length() > 1 && path.endsWith("/")) {
    path.remove(path.length() - 1);
  }

  if (path.length() > 160 || hasParentTraversal(path)) {
    if (message != nullptr) {
      *message = "Unsupported SD card path.";
    }
    return false;
  }

  *normalizedPath = path;
  return true;
}

bool listSDCardDirectory(const String &path, std::vector<SDCardEntry> *entries,
                         String *message) {
  if (entries == nullptr) {
    return false;
  }

  entries->clear();

  String normalizedPath;
  if (!normalizeSDCardPath(path, &normalizedPath, message)) {
    return false;
  }

  if (!ensureMounted(message)) {
    return false;
  }

  File directory = SD.open(normalizedPath);
  if (!directory) {
    if (message != nullptr) {
      *message = "SD card path was not found.";
    }
    return false;
  }

  if (!directory.isDirectory()) {
    directory.close();
    if (message != nullptr) {
      *message = "SD card path is not a directory.";
    }
    return false;
  }

  while (true) {
    File child = directory.openNextFile();
    if (!child) {
      break;
    }

    const String rawName = child.name();
    const String childPath = childPathFor(normalizedPath, rawName);
    SDCardEntry entry = {
        baseNameFromPath(childPath),
        childPath,
        child.isDirectory(),
        child.isDirectory() ? 0 : child.size(),
    };
    entries->push_back(entry);
    child.close();

    if (entries->size() >= AppConfig::kSDCardMaxDirectoryEntries) {
      break;
    }
  }

  directory.close();
  std::sort(entries->begin(), entries->end(),
            [](const SDCardEntry &lhs, const SDCardEntry &rhs) {
              if (lhs.directory != rhs.directory) {
                return lhs.directory && !rhs.directory;
              }
              return lhs.name < rhs.name;
            });

  if (message != nullptr) {
    *message = "SD card directory loaded.";
  }
  return true;
}

bool openSDCardFile(const String &path, File *file, String *message) {
  if (file == nullptr) {
    return false;
  }

  String normalizedPath;
  if (!normalizeSDCardPath(path, &normalizedPath, message)) {
    return false;
  }

  if (!ensureMounted(message)) {
    return false;
  }

  File openedFile = SD.open(normalizedPath, FILE_READ);
  if (!openedFile) {
    if (message != nullptr) {
      *message = "SD card file was not found.";
    }
    return false;
  }

  if (openedFile.isDirectory()) {
    openedFile.close();
    if (message != nullptr) {
      *message = "SD card path is a directory, not a file.";
    }
    return false;
  }

  *file = openedFile;
  if (message != nullptr) {
    *message = "SD card file opened.";
  }
  return true;
}
