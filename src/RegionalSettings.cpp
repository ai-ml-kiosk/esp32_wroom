#include <Arduino.h>
#include <Preferences.h>
#include <time.h>

#include "AppConfig.h"
#include "RegionalSettings.h"

namespace {

struct TimeZoneOptionInternal {
  const char *id;
  const char *label;
  const char *posix;
  const char *fallbackAbbreviation;
};

struct DateFormatOptionInternal {
  const char *id;
  const char *label;
  const char *strftimePattern;
  bool uppercase;
  const char *placeholder;
};

constexpr time_t kMinimumValidEpoch = 946684800;

const TimeZoneOptionInternal kTimeZoneOptions[] = {
    {"australia_sydney", "Australia/Sydney", AppConfig::kTimeZone, "AEST"},
    {"australia_brisbane", "Australia/Brisbane", "AEST-10", "AEST"},
    {"australia_adelaide", "Australia/Adelaide",
     "ACST-9:30ACDT,M10.1.0/2,M4.1.0/3", "ACST"},
    {"australia_perth", "Australia/Perth", "AWST-8", "AWST"},
    {"utc", "UTC", "UTC0", "UTC"},
    {"asia_singapore", "Asia/Singapore", "SGT-8", "SGT"},
    {"asia_tokyo", "Asia/Tokyo", "JST-9", "JST"},
    {"europe_london", "Europe/London", "GMT0BST,M3.5.0/1,M10.5.0/2",
     "GMT"},
    {"europe_berlin", "Europe/Berlin", "CET-1CEST,M3.5.0/2,M10.5.0/3",
     "CET"},
    {"america_los_angeles", "America/Los_Angeles",
     "PST8PDT,M3.2.0/2,M11.1.0/2", "PST"},
    {"america_denver", "America/Denver", "MST7MDT,M3.2.0/2,M11.1.0/2",
     "MST"},
    {"america_chicago", "America/Chicago", "CST6CDT,M3.2.0/2,M11.1.0/2",
     "CST"},
    {"america_new_york", "America/New_York", "EST5EDT,M3.2.0/2,M11.1.0/2",
     "EST"},
    {"pacific_auckland", "Pacific/Auckland", "NZST-12NZDT,M9.5.0/2,M4.1.0/3",
     "NZST"},
};

const DateFormatOptionInternal kDateFormatOptions[] = {
    {"dd-mon-yyyy", "DD MON YYYY", "%d %b %Y", true, "-- --- ----"},
    {"dd/mm/yyyy", "DD/MM/YYYY", "%d/%m/%Y", false, "--/--/----"},
    {"mm/dd/yyyy", "MM/DD/YYYY", "%m/%d/%Y", false, "--/--/----"},
    {"yyyy-mm-dd", "YYYY-MM-DD", "%Y-%m-%d", false, "----/--/--"},
    {"dd.mm.yyyy", "DD.MM.YYYY", "%d.%m.%Y", false, "--.--.----"},
};

Preferences preferences;
bool preferencesInitialized = false;
bool preferencesAvailable = false;
const TimeZoneOptionInternal *configuredTimeZone = &kTimeZoneOptions[0];
const DateFormatOptionInternal *configuredDateFormat = &kDateFormatOptions[0];

constexpr char kPreferencesNamespace[] = "status-reg";
constexpr char kTimeZonePreferenceKey[] = "tz-id";
constexpr char kDateFormatPreferenceKey[] = "date-fmt";

const TimeZoneOptionInternal *findTimeZoneOptionById(const String &id) {
  for (const TimeZoneOptionInternal &option : kTimeZoneOptions) {
    if (id == option.id) {
      return &option;
    }
  }

  return nullptr;
}

const DateFormatOptionInternal *findDateFormatOptionById(const String &id) {
  for (const DateFormatOptionInternal &option : kDateFormatOptions) {
    if (id == option.id) {
      return &option;
    }
  }

  return nullptr;
}

void applyConfiguredTimeZone() {
  setenv("TZ", configuredTimeZone->posix, 1);
  tzset();
}

void ensurePreferencesReady() {
  if (preferencesInitialized) {
    return;
  }

  preferencesInitialized = true;

  const TimeZoneOptionInternal *defaultTimeZone =
      findTimeZoneOptionById(AppConfig::kDefaultTimeZoneId);
  if (defaultTimeZone != nullptr) {
    configuredTimeZone = defaultTimeZone;
  }

  const DateFormatOptionInternal *defaultDateFormat =
      findDateFormatOptionById(AppConfig::kDefaultDateFormatId);
  if (defaultDateFormat != nullptr) {
    configuredDateFormat = defaultDateFormat;
  }

  if (!preferences.begin(kPreferencesNamespace, false)) {
    Serial.println(
        "Regional settings preferences unavailable. Using built-in timezone and date format.");
    applyConfiguredTimeZone();
    return;
  }

  preferencesAvailable = true;

  const String savedTimeZoneId =
      preferences.getString(kTimeZonePreferenceKey, AppConfig::kDefaultTimeZoneId);
  const TimeZoneOptionInternal *savedTimeZone =
      findTimeZoneOptionById(savedTimeZoneId);
  if (savedTimeZone != nullptr) {
    configuredTimeZone = savedTimeZone;
  }

  const String savedDateFormatId = preferences.getString(
      kDateFormatPreferenceKey, AppConfig::kDefaultDateFormatId);
  const DateFormatOptionInternal *savedDateFormat =
      findDateFormatOptionById(savedDateFormatId);
  if (savedDateFormat != nullptr) {
    configuredDateFormat = savedDateFormat;
  }

  applyConfiguredTimeZone();
  Serial.printf("Regional settings loaded: timezone=%s, dateFormat=%s.\n",
                configuredTimeZone->label, configuredDateFormat->label);
}

String formatDateWithOption(const DateFormatOptionInternal *option,
                            const struct tm &timeInfo) {
  char buffer[24];
  strftime(buffer, sizeof(buffer), option->strftimePattern, &timeInfo);
  String formatted(buffer);
  if (option->uppercase) {
    formatted.toUpperCase();
  }

  return formatted;
}

}  // namespace

void initializeRegionalSettings() { ensurePreferencesReady(); }

size_t getSupportedTimeZoneCount() {
  return sizeof(kTimeZoneOptions) / sizeof(kTimeZoneOptions[0]);
}

bool getSupportedTimeZoneOption(size_t index, TimeZoneOption *option) {
  if (option == nullptr || index >= getSupportedTimeZoneCount()) {
    return false;
  }

  option->id = kTimeZoneOptions[index].id;
  option->label = kTimeZoneOptions[index].label;
  return true;
}

size_t getSupportedDateFormatCount() {
  return sizeof(kDateFormatOptions) / sizeof(kDateFormatOptions[0]);
}

bool getSupportedDateFormatOption(size_t index, DateFormatOption *option) {
  if (option == nullptr || index >= getSupportedDateFormatCount()) {
    return false;
  }

  option->id = kDateFormatOptions[index].id;
  option->label = kDateFormatOptions[index].label;
  return true;
}

String getConfiguredTimeZoneId() {
  ensurePreferencesReady();
  return String(configuredTimeZone->id);
}

String getConfiguredTimeZoneLabel() {
  ensurePreferencesReady();
  return String(configuredTimeZone->label);
}

String getConfiguredTimeZonePosix() {
  ensurePreferencesReady();
  return String(configuredTimeZone->posix);
}

String getConfiguredDateFormatId() {
  ensurePreferencesReady();
  return String(configuredDateFormat->id);
}

String getConfiguredDateFormatLabel() {
  ensurePreferencesReady();
  return String(configuredDateFormat->label);
}

String getConfiguredDatePlaceholder() {
  ensurePreferencesReady();
  return String(configuredDateFormat->placeholder);
}

bool setConfiguredTimeZoneById(const String &id) {
  ensurePreferencesReady();

  const TimeZoneOptionInternal *option = findTimeZoneOptionById(id);
  if (option == nullptr) {
    return false;
  }

  if (configuredTimeZone != option) {
    configuredTimeZone = option;
    applyConfiguredTimeZone();
    if (preferencesAvailable) {
      preferences.putString(kTimeZonePreferenceKey, option->id);
    }
    Serial.printf("Timezone updated to %s.\n", option->label);
  }

  return true;
}

bool setConfiguredDateFormatById(const String &id) {
  ensurePreferencesReady();

  const DateFormatOptionInternal *option = findDateFormatOptionById(id);
  if (option == nullptr) {
    return false;
  }

  if (configuredDateFormat != option) {
    configuredDateFormat = option;
    if (preferencesAvailable) {
      preferences.putString(kDateFormatPreferenceKey, option->id);
    }
    Serial.printf("Date format updated to %s.\n", option->label);
  }

  return true;
}

bool hasSynchronizedClock() {
  time_t now;
  time(&now);
  return now >= kMinimumValidEpoch;
}

bool getCurrentLocalTimeInfo(struct tm *timeInfo) {
  if (timeInfo == nullptr || !hasSynchronizedClock()) {
    return false;
  }

  return getLocalTime(timeInfo, 0);
}

String getCurrentTimeZoneAbbreviation() {
  ensurePreferencesReady();

  struct tm timeInfo;
  if (getCurrentLocalTimeInfo(&timeInfo)) {
    char buffer[16];
    if (strftime(buffer, sizeof(buffer), "%Z", &timeInfo) > 0 &&
        buffer[0] != '\0') {
      return String(buffer);
    }
  }

  return String(configuredTimeZone->fallbackAbbreviation);
}

String getFormattedCurrentTime() {
  struct tm timeInfo;
  if (!getCurrentLocalTimeInfo(&timeInfo)) {
    return "--:--";
  }

  char buffer[8];
  strftime(buffer, sizeof(buffer), "%H:%M", &timeInfo);
  return String(buffer);
}

String formatConfiguredDate(const struct tm &timeInfo) {
  ensurePreferencesReady();
  return formatDateWithOption(configuredDateFormat, timeInfo);
}

String getFormattedCurrentDate() {
  struct tm timeInfo;
  if (!getCurrentLocalTimeInfo(&timeInfo)) {
    return getConfiguredDatePlaceholder();
  }

  return formatConfiguredDate(timeInfo);
}
