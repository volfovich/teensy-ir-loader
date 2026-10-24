# PlatformIO pre-build script: regenerates lib/irdsp/src/DrumKitData.cpp from
# kits/*/ and DrumSongData.cpp from songs/*.txt when they changed (see
# tools/make_kits.py and tools/make_songs.py).
import os
import subprocess

Import("env")  # noqa: F821 (provided by PlatformIO)

for tool in ("make_kits.py", "make_songs.py"):
    subprocess.check_call([env.subst("$PYTHONEXE"), os.path.join(env.subst("$PROJECT_DIR"), "tools", tool)])  # noqa: F821
