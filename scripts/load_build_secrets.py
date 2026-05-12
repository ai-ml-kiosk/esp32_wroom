import os
from pathlib import Path

Import("env")


ENV_VAR_MAPPINGS = {
    "ESP32_WIFI_SSID": ("APP_WIFI_SSID", "string"),
    "ESP32_WIFI_PASSWORD": ("APP_WIFI_PASSWORD", "string"),
    "ESP32_FALLBACK_AP_SSID_PREFIX": ("APP_FALLBACK_AP_SSID_PREFIX", "string"),
    "ESP32_FALLBACK_AP_PASSWORD": ("APP_FALLBACK_AP_PASSWORD", "string"),
    "ESP32_STATUS_HOST_NAME": ("APP_STATUS_HOST_NAME", "string"),
    "ESP32_STATUS_HOST_NAME_APPEND_MAC": (
        "APP_STATUS_HOST_NAME_APPEND_MAC",
        "raw",
    ),
    "ESP32_POWER_SENSE_PIN": ("APP_POWER_SENSE_PIN", "raw"),
    "ESP32_POWER_SENSE_DIVIDER_RATIO": ("APP_POWER_SENSE_DIVIDER_RATIO", "raw"),
    "ESP32_POWER_SENSE_OFFSET_MV": ("APP_POWER_SENSE_OFFSET_MV", "raw"),
    "ESP32_SD_CARD_ENABLED": ("APP_SD_CARD_ENABLED", "raw"),
    "ESP32_SD_CARD_CS_PIN": ("APP_SD_CARD_CS_PIN", "raw"),
    "ESP32_SD_CARD_SCK_PIN": ("APP_SD_CARD_SCK_PIN", "raw"),
    "ESP32_SD_CARD_MISO_PIN": ("APP_SD_CARD_MISO_PIN", "raw"),
    "ESP32_SD_CARD_MOSI_PIN": ("APP_SD_CARD_MOSI_PIN", "raw"),
    "ESP32_SD_CARD_SPI_FREQUENCY_HZ": (
        "APP_SD_CARD_SPI_FREQUENCY_HZ",
        "raw",
    ),
}


def load_dotenv_file(path):
    values = {}
    if not path.exists():
        return values

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue

        if line.startswith("export "):
            line = line[len("export "):].strip()

        if "=" not in line:
            continue

        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if not key:
            continue

        if len(value) >= 2 and value[0] == value[-1] and value[0] in ("'", '"'):
            value = value[1:-1]

        values[key] = value

    return values


def escape_c_string(value):
    return value.replace("\\", "\\\\").replace('"', '\\"')


def append_cpp_define(define_name, value, define_type):
    if define_type == "string":
        env.Append(
            CPPDEFINES=[(define_name, '\\"{}\\"'.format(escape_c_string(value)))]
        )
        return

    env.Append(CPPDEFINES=[(define_name, value)])


project_dir = Path(env.subst("$PROJECT_DIR"))
dotenv_values = load_dotenv_file(project_dir / ".env.local")
loaded = []
for env_name, (define_name, define_type) in ENV_VAR_MAPPINGS.items():
    value = os.getenv(env_name)
    source = "environment"
    if not value:
        value = dotenv_values.get(env_name)
        source = ".env.local"
    if not value:
        continue

    append_cpp_define(define_name, value, define_type)
    loaded.append("{} ({})".format(env_name, source))

if loaded:
    print("Loaded build settings: {}".format(", ".join(loaded)))
else:
    print("No ESP32 secret settings were found in the environment or .env.local; using AppConfig.h defaults.")
