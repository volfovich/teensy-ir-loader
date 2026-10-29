#!/bin/bash
# Renders A2.nam's Lite network on test_nam's test signal with
# NeuralAmpModelerCore (the reference implementation) into A2_lite_ref.f32.
#   ./make_reference.sh /path/to/NeuralAmpModelerCore
set -e
CORE="$(cd "$1" && pwd)"
HERE="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"
SRC=("$CORE/tools/render.cpp" "$CORE"/NAM/*.cpp "$CORE"/NAM/*/*.cpp "$CORE/Dependencies/AudioDSPTools/dsp/wav.cpp")
INC=(-I"$CORE" -I"$CORE/Dependencies/eigen" -I"$CORE/Dependencies/nlohmann" -I"$CORE/Dependencies/AudioDSPTools/dsp")
clang++ -std=c++20 -O2 -w -DNAM_ENABLE_A2_FAST "${INC[@]}" "${SRC[@]}" -o "$TMP/render"
make -s -C "$HERE/.." test_nam
"$HERE/../test_nam" --write-input "$TMP/input.wav"
# A slimmable size below 0.5 selects the Lite network.
"$TMP/render" --slim 0.25 "$HERE/A2.nam" "$TMP/input.wav" "$TMP/output.wav"
tail -c +45 "$TMP/output.wav" > "$HERE/A2_lite_ref.f32"  # drop the 44-byte WAV header
echo "wrote $HERE/A2_lite_ref.f32 ($(($(wc -c < "$HERE/A2_lite_ref.f32") / 4)) samples)"
rm -rf "$TMP"
