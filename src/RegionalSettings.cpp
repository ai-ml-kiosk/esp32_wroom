#include <Arduino.h>
#include <Preferences.h>
#include <ctype.h>
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
constexpr char kCustomDateFormatId[] = "custom";
constexpr char kCustomDateFormatLabel[] = "Custom";
constexpr size_t kMaxCustomDateFormatLength = 32;

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
bool configuredDateFormatIsCustom = false;
String configuredCustomDateFormat;

constexpr char kPreferencesNamespace[] = "status-reg";
constexpr char kTimeZonePreferenceKey[] = "tz-id";
constexpr char kDateFormatPreferenceKey[] = "date-fmt";
constexpr char kCustomDateFormatPreferenceKey[] = "date-pat";

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

String normalizeDateFormatToken(const String &token) {
  String normalized = token;
  normalized.toUpperCase();

  if (normalized == "DD" || normalized == "MM" || normalized == "YY" ||
      normalized == "YYYY" || normalized == "MON" ||
      normalized == "MONTH") {
    return normalized;
  }

  if (normalized == "MMM") {
    return "MON";
  }

  return "";
}

bool normalizeCustomDateFormatPattern(const String &pattern,
                                      String *normalizedPattern) {
  String trimmed = pattern;
  trimmed.trim();

  if (trimmed.length() == 0 || trimmed.length() > kMaxCustomDateFormatLength) {
    return false;
  }

  String normalized;
  normalized.reserve(trimmed.length());
  bool hasToken = false;

  for (size_t index = 0; index < trimmed.length();) {
    const unsigned char current =
        static_cast<unsigned char>(trimmed[index]);
    if (isalpha(current)) {
      const size_t tokenStart = index;
      while (index < trimmed.length() &&
             isalpha(static_cast<unsigned char>(trimmed[index]))) {
        ++index;
      }

      const String token = trimmed.substring(tokenStart, index);
      const String normalizedToken = normalizeDateFormatToken(token);
      if (normalizedToken.length() == 0) {
        return false;
      }

      normalized += normalizedToken;
      hasToken = true;
    } else {
      if (current < 32 || current > 126) {
        return false;
      }

      normalized += static_cast<char>(current);
      ++index;
    }

    if (normalized.length() > kMaxCustomDateFormatLength) {
      return false;
    }
  }

  if (!hasToken) {
    return false;
  }

  if (normalizedPattern != nullptr) {
    *normalizedPattern = normalized;
  }

  return true;
}

String formatPatternTokenValue(const String &token, const struct tm &timeInfo) {
  char buffer[24];

  if (token == "DD") {
    snprintf(buffer, sizeof(buffer), "%02d", timeInfo.tm_mday);
    return String(buffer);
  }

  if (token == "MM") {
    snprintf(buffer, sizeof(buffer), "%02d", timeInfo.tm_mon + 1);
    return String(buffer);
  }

  if (token == "YY") {
    strftime(buffer, sizeof(buffer), "%y", &timeInfo);
    return String(buffer);
  }

  if (token == "YYYY") {
    strftime(buffer, sizeof(buffer), "%Y", &timeInfo);
    return String(buffer);
  }

  if (token == "MON") {
    strftime(buffer, sizeof(buffer), "%b", &timeInfo);
    String value(buffer);
    value.toUpperCase();
    return value;
  }

  if (token == "MONTH") {
    strftime(buffer, sizeof(buffer), "%B", &timeInfo);
    String value(buffer);
    value.toUpperCase();
    return value;
  }

  return "";
}

String buildPatternPlaceholder(const String &pattern) {
  String placeholder;
  placeholder.reserve(pattern.length() + 4);

  for (size_t index = 0; index < pattern.length();) {
    const unsigned char current =
        static_cast<unsigned char>(pattern[index]);
    if (isalpha(current)) {
      const size_t tokenStart = index;
      while (index < pattern.length() &&
             isalpha(static_cast<unsigned char>(pattern[index]))) {
        ++index;
      }

      const String token = pattern.substring(tokenStart, index);
      if (token == "DD" || token == "MM" || token == "YY") {
        placeholder += "--";
      } else if (token == "YYYY") {
        placeholder += "----";
      } else if (token == "MON") {
        placeholder += "---";
      } else if (token == "MONTH") {
        placeholder += "-----";
      } else {
        placeholder += "?";
      }
    } else {
      placeholder += static_cast<char>(current);
      ++index;
    }
  }

  return placeholder;
}

String formatDateWithPattern(const String &pattern, const struct tm &timeInfo) {
  String formatted;
  formatted.reserve(pattern.length() + 8);

  for (size_t index = 0; index < pattern.length();) {
    const unsigned char current =
        static_cast<unsigned char>(pattern[index]);
    if (isalpha(current)) {
      const size_t tokenStart = index;
      while (index < pattern.length() &&
             isalpha(static_cast<unsigned char>(pattern[index]))) {
        ++index;
      }

      const String token = pattern.substring(tokenStart, index);
      formatted += formatPatternTokenValue(token, timeInfo);
    } else {
      formatted += static_cast<char>(current);
      ++index;
    }
  }

  return formatted;
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
  configuredDateFormatIsCustom = false;
  configuredCustomDateFormat = "";

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
  if (savedDateFormatId == kCustomDateFormatId) {
    String savedCustomPattern =
        preferences.getString(kCustomDateFormatPreferenceKey, "");
    String normalizedCustomPattern;
    if (normalizeCustomDateFormatPattern(savedCustomPattern,
                                         &normalizedCustomPattern)) {
      configuredDateFormatIsCustom = true;
      configuredCustomDateFormat = normalizedCustomPattern;
    }
  } else {
    const DateFormatOptionInternal *savedDateFormat =
        findDateFormatOptionById(savedDateFormatId);
    if (savedDateFormat != nullptr) {
      configuredDateFormat = savedDateFormat;
    }
  }

  applyConfiguredTimeZone();
  Serial.printf("Regional settings loaded: timezone=%s, dateFormat=%s.\n",
                configuredTimeZone->label,
                configuredDateFormatIsCustom
                    ? configuredCustomDateFormat.c_str()
                    : configuredDateFormat->label);
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
  if (configuredDateFormatIsCustom) {
    return String(kCustomDateFormatId);
  }
  return String(configuredDateFormat->id);
}

String getConfiguredDateFormatLabel() {
  ensurePreferencesReady();
  if (configuredDateFormatIsCustom) {
    return String(kCustomDateFormatLabel) + " (" + configuredCustomDateFormat +
           ")";
  }
  return String(configuredDateFormat->label);
}

String getConfiguredDatePlaceholder() {
  ensurePreferencesReady();
  if (configuredDateFormatIsCustom) {
    return buildPatternPlaceholder(configuredCustomDateFormat);
  }
  return String(configuredDateFormat->placeholder);
}

String getConfiguredDateFormatPattern() {
  ensurePreferencesReady();
  if (configuredDateFormatIsCustom) {
    return configuredCustomDateFormat;
  }
  return String(configuredDateFormat->label);
}

bool isConfiguredDateFormatCustom() {
  ensurePreferencesReady();
  return configuredDateFormatIsCustom;
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

  if (configuredDateFormatIsCustom || configuredDateFormat != option) {
    configuredDateFormatIsCustom = false;
    configuredCustomDateFormat = "";
    configuredDateFormat = option;
    if (preferencesAvailable) {
      preferences.putString(kDateFormatPreferenceKey, option->id);
      preferences.remove(kCustomDateFormatPreferenceKey);
    }
    Serial.printf("Date format updated to %s.\n", option->label);
  }

  return true;
}

bool setConfiguredCustomDateFormat(const String &pattern) {
  ensurePreferencesReady();

  String normalizedPattern;
  if (!normalizeCustomDateFormatPattern(pattern, &normalizedPattern)) {
    return false;
  }

  if (!configuredDateFormatIsCustom ||
      configuredCustomDateFormat != normalizedPattern) {
    configuredDateFormatIsCustom = true;
    configuredCustomDateFormat = normalizedPattern;
    if (preferencesAvailable) {
      preferences.putString(kDateFormatPreferenceKey, kCustomDateFormatId);
      preferences.putString(kCustomDateFormatPreferenceKey,
                            configuredCustomDateFormat);
    }
    Serial.printf("Date format updated to custom pattern %s.\n",
                  configuredCustomDateFormat.c_str());
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
  if (configuredDateFormatIsCustom) {
    return formatDateWithPattern(configuredCustomDateFormat, timeInfo);
  }
  return formatDateWithOption(configuredDateFormat, timeInfo);
}

String getFormattedCurrentDate() {
  struct tm timeInfo;
  if (!getCurrentLocalTimeInfo(&timeInfo)) {
    return getConfiguredDatePlaceholder();
  }

  return formatConfiguredDate(timeInfo);
}
