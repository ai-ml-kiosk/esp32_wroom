import os
from pathlib import Path

Import("env")


ENV_VAR_MAPPINGS = {
    "ESP32_WIFI_SSID": "APP_WIFI_SSID",
    "ESP32_WIFI_PASSWORD": "APP_WIFI_PASSWORD",
    "ESP32_FALLBACK_AP_SSID_PREFIX": "APP_FALLBACK_AP_SSID_PREFIX",
    "ESP32_FALLBACK_AP_PASSWORD": "APP_FALLBACK_AP_PASSWORD",
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


project_dir = Path(env.subst("$PROJECT_DIR"))
dotenv_values = load_dotenv_file(project_dir / ".env.local")
loaded = []
for env_name, define_name in ENV_VAR_MAPPINGS.items():
    value = os.getenv(env_name)
    source = "environment"
    if not value:
        value = dotenv_values.get(env_name)
        source = ".env.local"
    if not value:
        continue

    env.Append(CPPDEFINES=[(define_name, '\\"{}\\"'.format(escape_c_string(value)))])
    loaded.append("{} ({})".format(env_name, source))

if loaded:
    print("Loaded build settings: {}".format(", ".join(loaded)))
else:
    print("No ESP32 secret settings were found in the environment or .env.local; using AppConfig.h defaults.")
