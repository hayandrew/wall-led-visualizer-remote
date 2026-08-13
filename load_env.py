import os
from os.path import exists, join

# SCons variables dynamically injected at runtime by PlatformIO
Import = globals().get("Import")
env = globals().get("env")

Import("env")

# Locate .env file in the project directory
env_file = join(env.get("PROJECT_DIR"), ".env")

if exists(env_file):
    print(f"--- load_env.py: Loading environment variables from {env_file} ---")
    with open(env_file) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                key, val = line.split("=", 1)
                key = key.strip()
                val = val.strip().strip('"').strip("'")
                # Add to os.environ so PlatformIO and child processes can see it
                if key not in os.environ:
                    os.environ[key] = val

# Inject env variables as C++ Preprocessor Macros
for key in ["WIFI_SSID", "WIFI_PASS"]:
    if key in os.environ:
        env.Append(CPPDEFINES=[(key, env.StringifyMacro(os.environ[key]))])
    else:
        print(f"--- load_env.py WARNING: {key} not found in environment or .env file ---")
