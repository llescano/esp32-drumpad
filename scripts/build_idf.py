"""Build helper: runs idf.py from Git Bash on Windows.

ESP-IDF refuses MSys/Mingw environments (MSYSTEM env var), but Git Bash
re-injects MSYSTEM at process spawn level, so `unset` from the shell is not
enough: this wrapper deletes the variable inside the Python process, exports
the IDF toolchain environment (idf_tools.py export) and then runs idf.py.

Usage (with the IDF python env interpreter):
    <idf-python> scripts/build_idf.py build
    <idf-python> scripts/build_idf.py -p COM24 flash monitor
    <idf-python> scripts/build_idf.py menuconfig

Environment overrides:
    IDF_PATH              defaults to I:\\esp32\\v5.5.1\\esp-idf
    IDF_TOOLS_PATH_BUILD  defaults to C:\\Users\\Luis\\.espressif
"""

import os
import runpy
import subprocess
import sys

DEFAULT_IDF_PATH = r"I:\esp32\v5.5.1\esp-idf"
# IDF 5.5.1 tools/python env live here (the global IDF_TOOLS_PATH points at
# the separate IDF 6.x install in I:\Espressif). Flip both when migrating.
DEFAULT_TOOLS_PATH = r"C:\Users\Luis\.espressif"

os.environ.pop("MSYSTEM", None)  # idf.py refuses MSys/Mingw environments
os.environ.setdefault("IDF_PATH", DEFAULT_IDF_PATH)
os.environ["IDF_TOOLS_PATH"] = os.environ.get("IDF_TOOLS_PATH_BUILD", DEFAULT_TOOLS_PATH)
IDF_PYTHON_ENV = os.path.join(os.environ["IDF_TOOLS_PATH"], "python_env",
                              "idf5.5_py3.13_env")
os.environ.setdefault("IDF_PYTHON_ENV_PATH", IDF_PYTHON_ENV)

IDF_TOOLS = os.path.join(os.environ["IDF_PATH"], "tools", "idf_tools.py")
IDF_PY = os.path.join(os.environ["IDF_PATH"], "tools", "idf.py")

# Export toolchain environment (cmake, ninja, xtensa toolchain -> PATH)
exp = subprocess.run(
    [sys.executable, IDF_TOOLS, "export", "--format", "key-value"],
    capture_output=True, text=True, check=True)
for line in exp.stdout.splitlines():
    line = line.strip()
    if "=" in line and not line.startswith("#"):
        key, _, value = line.partition("=")
        if key in ("IDF_PATH", "IDF_TOOLS_PATH") and os.environ.get(key):
            continue  # keep our resolved paths
        os.environ[key] = value

sys.path.insert(0, os.path.dirname(IDF_PY))  # idf.py imports siblings

if len(sys.argv) < 2:
    sys.argv = [IDF_PY, "build"]
else:
    sys.argv = [IDF_PY] + sys.argv[1:]

runpy.run_path(IDF_PY, run_name="__main__")
