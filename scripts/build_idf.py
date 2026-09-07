"""Build helper: runs idf.py from Git Bash on Windows.

ESP-IDF refuses MSys/Mingw environments (MSYSTEM env var), but Git Bash
re-injects MSYSTEM at process spawn level, so `unset` from the shell is not
enough: this wrapper deletes the variable inside the Python process, exports
the IDF toolchain environment (idf_tools.py export) and then runs idf.py.

Project baseline: ESP-IDF 6.0.2 (tools in I:\\Espressif, symlinked as
C:\\Espressif because the eim installer requires that path).

Run with the matching interpreter:
    C:\\Espressif\\python_env\\idf6.0_py3.11_env\\Scripts\\python.exe scripts/build_idf.py build
    ... scripts/build_idf.py -p COM24 flash monitor

IDF 5.5.1 fallback (previous install, tools in C:\\Users\\Luis\\.espressif):
    IDF_PATH_BUILD=I:\\esp32\\v5.5.1\\esp-idf
    IDF_TOOLS_PATH_BUILD=C:\\Users\\Luis\\.espressif
    IDF_PYTHON_ENV_PATH=C:\\Users\\Luis\\.espressif\\python_env\\idf5.5_py3.13_env
    C:\\Users\\Luis\\.espressif\\python_env\\idf5.5_py3.13_env\\Scripts\\python.exe scripts/build_idf.py build

Environment overrides (win over the defaults):
    IDF_PATH_BUILD, IDF_TOOLS_PATH_BUILD, IDF_PYTHON_ENV_BUILD
"""

import os
import runpy
import subprocess
import sys

DEFAULT_IDF_PATH = r"I:\esp32\v6.0.2\esp-idf"
DEFAULT_TOOLS_PATH = r"C:\Espressif"
DEFAULT_PYTHON_ENV = r"C:\Espressif\python_env\idf6.0_py3.11_env"

os.environ.pop("MSYSTEM", None)  # idf.py refuses MSys/Mingw environments
# The shell profile may export an ambient IDF_PATH (e.g. the old 5.5.1), so
# this helper always decides the install itself; the *_BUILD vars are the
# explicit per-invocation overrides.
os.environ["IDF_PATH"] = os.environ.get("IDF_PATH_BUILD", DEFAULT_IDF_PATH)
os.environ["IDF_TOOLS_PATH"] = os.environ.get("IDF_TOOLS_PATH_BUILD", DEFAULT_TOOLS_PATH)
os.environ["IDF_PYTHON_ENV_PATH"] = os.environ.get("IDF_PYTHON_ENV_BUILD", DEFAULT_PYTHON_ENV)

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
