#pragma once

#include <Arduino.h>
#include <FS.h>

#include <vector>

struct SDCardInfo {
  bool enabled;
  bool mounted;
  String status;
  String mountState;
  String cardType;
  uint64_t totalBytes;
  uint64_t usedBytes;
  uint8_t csPin;
  uint8_t sckPin;
  uint8_t misoPin;
  uint8_t mosiPin;
};

struct SDCardEntry {
  String name;
  String path;
  bool directory;
  uint64_t sizeBytes;
};

void initializeSDCardManager();
void updateSDCardManager();
bool refreshSDCardMount();
bool ejectSDCard(String *message = nullptr);
bool isSDCardMounted();
SDCardInfo getSDCardInfo();
bool normalizeSDCardPath(const String &rawPath, String *normalizedPath,
                         String *message = nullptr);
bool listSDCardDirectory(const String &path, std::vector<SDCardEntry> *entries,
                         String *message = nullptr);
bool openSDCardFile(const String &path, File *file,
                    String *message = nullptr);
