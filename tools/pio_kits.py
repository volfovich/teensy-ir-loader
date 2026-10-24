# PlatformIO pre-build script: regenerates lib/irdsp/src/DrumKitData.cpp from
# kits/*/ when a kit changed (see tools/make_kits.py).
import os
import subprocess

Import("env")  # noqa: F821 (provided by PlatformIO)

subprocess.check_call([env.subst("$PYTHONEXE"), os.path.join(env.subst("$PROJECT_DIR"), "tools", "make_kits.py")])  # noqa: F821
