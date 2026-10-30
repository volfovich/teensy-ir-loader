# PlatformIO pre-build script: regenerates lib/irdsp/src/DrumKitData.cpp from
# kits/*/ and DrumSongData.cpp from songs/*.txt when they changed (see
# tools/make_kits.py and tools/make_songs.py).
import os
import re
import subprocess

Import("env")  # noqa: F821 (provided by PlatformIO)

# The build's sample rate (the Teensy core's default unless platformio.ini
# sets AUDIO_SAMPLE_RATE_EXACT): the kits must match it.
flags = env.GetProjectOption("build_flags", "")  # noqa: F821
flags = " ".join(flags) if isinstance(flags, (list, tuple)) else flags
m = re.search(r"AUDIO_SAMPLE_RATE_EXACT=([0-9.]+)", flags)
rate = int(float(m.group(1))) if m else 44100

tools = os.path.join(env.subst("$PROJECT_DIR"), "tools")  # noqa: F821
subprocess.check_call([env.subst("$PYTHONEXE"), os.path.join(tools, "make_kits.py"), "--rate", str(rate)])  # noqa: F821
subprocess.check_call([env.subst("$PYTHONEXE"), os.path.join(tools, "make_songs.py")])  # noqa: F821
