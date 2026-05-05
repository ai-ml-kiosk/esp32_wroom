import os

Import("env")


ENV_VAR_MAPPINGS = {
    "ESP32_WIFI_SSID": "APP_WIFI_SSID",
    "ESP32_WIFI_PASSWORD": "APP_WIFI_PASSWORD",
    "ESP32_FALLBACK_AP_SSID_PREFIX": "APP_FALLBACK_AP_SSID_PREFIX",
    "ESP32_FALLBACK_AP_PASSWORD": "APP_FALLBACK_AP_PASSWORD",
}


def escape_c_string(value):
    return value.replace("\\", "\\\\").replace('"', '\\"')


loaded = []
for env_name, define_name in ENV_VAR_MAPPINGS.items():
    value = os.getenv(env_name)
    if not value:
        continue

    env.Append(CPPDEFINES=[(define_name, '\\"{}\\"'.format(escape_c_string(value)))])
    loaded.append(env_name)

if loaded:
    print("Loaded build settings from environment: {}".format(", ".join(loaded)))
else:
    print("No ESP32 secret environment variables were found; using AppConfig.h defaults.")
