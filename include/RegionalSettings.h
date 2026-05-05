#pragma once

#include <Arduino.h>
#include <time.h>

struct TimeZoneOption {
  const char *id;
  const char *label;
};

struct DateFormatOption {
  const char *id;
  const char *label;
};

void initializeRegionalSettings();

size_t getSupportedTimeZoneCount();
bool getSupportedTimeZoneOption(size_t index, TimeZoneOption *option);
size_t getSupportedDateFormatCount();
bool getSupportedDateFormatOption(size_t index, DateFormatOption *option);

String getConfiguredTimeZoneId();
String getConfiguredTimeZoneLabel();
String getConfiguredTimeZonePosix();
String getConfiguredDateFormatId();
String getConfiguredDateFormatLabel();
String getConfiguredDatePlaceholder();

bool setConfiguredTimeZoneById(const String &id);
bool setConfiguredDateFormatById(const String &id);

bool hasSynchronizedClock();
bool getCurrentLocalTimeInfo(struct tm *timeInfo);
String getCurrentTimeZoneAbbreviation();
String getFormattedCurrentTime();
String getFormattedCurrentDate();
String formatConfiguredDate(const struct tm &timeInfo);
