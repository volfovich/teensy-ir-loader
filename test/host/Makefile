# Host (PC) build of the DSP core for testing. Requires g++ and make.
CXX ?= g++
CXXFLAGS ?= -O2 -std=gnu++17 -Wall -Wextra
INC = -I../../lib/irdsp/src -I../../lib/oledui/src
UI_SRC = $(wildcard ../../lib/oledui/src/*.cpp)
UI_DEPS = $(UI_SRC) $(wildcard ../../lib/oledui/src/*.h) ui_fake.h

all: test_convolver test_fxstage test_reverb80 render_reverb80 test_midi test_studio_delay test_drums render_drums test_bass render_bass test_ui render_ui irtool test_nam test_namfile namtool

test_convolver: test_convolver.cpp ../../lib/irdsp/src/*.h
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_convolver.cpp


REVERB80_SRC = ../../lib/irdsp/src/Reverb80.cpp
REVERB80_DEPS = $(REVERB80_SRC) ../../lib/irdsp/src/Reverb80.h ../../lib/irdsp/src/irdsp_blocks.h

test_reverb80: test_reverb80.cpp $(REVERB80_DEPS)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_reverb80.cpp $(REVERB80_SRC)

test_fxstage: test_fxstage.cpp ../../lib/irdsp/src/FxStage.h ../../lib/irdsp/src/FxSlot.h $(REVERB80_DEPS)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_fxstage.cpp $(REVERB80_SRC)

test_studio_delay: test_studio_delay.cpp ../../lib/irdsp/src/StudioDelay.h ../../lib/irdsp/src/irdsp_blocks.h
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_studio_delay.cpp

BASS_SRC = ../../lib/irdsp/src/PluckBass.cpp
BASS_DEPS = $(BASS_SRC) ../../lib/irdsp/src/PluckBass.h ../../lib/irdsp/src/irdsp_blocks.h

test_bass: test_bass.cpp $(BASS_DEPS)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_bass.cpp $(BASS_SRC)

render_bass: render_bass.cpp $(BASS_DEPS)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ render_bass.cpp $(BASS_SRC)

DRUM_SRC = ../../lib/irdsp/src/DrumMachine.cpp ../../lib/irdsp/src/DrumPatterns.cpp ../../lib/irdsp/src/DrumKitData.cpp \
           ../../lib/irdsp/src/DrumSongData.cpp ../../lib/irdsp/src/PluckBass.cpp
DRUM_DEPS = $(DRUM_SRC) ../../lib/irdsp/src/DrumMachine.h ../../lib/irdsp/src/DrumKit.h ../../lib/irdsp/src/DrumSong.h \
            ../../lib/irdsp/src/PluckBass.h ../../lib/irdsp/src/irdsp_blocks.h

# The sampled kits, generated from kits/*/ (also by the PlatformIO build).
../../lib/irdsp/src/DrumKitData.cpp: ../../tools/make_kits.py $(wildcard ../../kits/*/*)
	python3 ../../tools/make_kits.py --force

# The built-in songs, generated from songs/*.txt (also by the PlatformIO build).
../../lib/irdsp/src/DrumSongData.cpp: ../../tools/make_songs.py ../../tools/make_kits.py $(wildcard ../../songs/*.txt)
	python3 ../../tools/make_songs.py --force

test_drums: test_drums.cpp $(DRUM_DEPS)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_drums.cpp $(DRUM_SRC)

render_drums: render_drums.cpp $(DRUM_DEPS)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ render_drums.cpp $(DRUM_SRC)

render_reverb80: render_reverb80.cpp $(REVERB80_DEPS) $(DRUM_DEPS)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ render_reverb80.cpp $(REVERB80_SRC) $(DRUM_SRC)

test_midi: test_midi.cpp ../../lib/irdsp/src/MidiDrums.cpp ../../lib/irdsp/src/MidiDrums.h ../../lib/irdsp/src/DrumSong.h
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_midi.cpp ../../lib/irdsp/src/MidiDrums.cpp

test_ui: test_ui.cpp $(UI_DEPS)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_ui.cpp $(UI_SRC)

render_ui: render_ui.cpp $(UI_DEPS)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ render_ui.cpp $(UI_SRC) -lz

# Renders every OLED screen to out/*.png.
render: render_ui render_drums render_reverb80 render_bass
	mkdir -p out
	./render_ui out
	./render_drums out
	./render_reverb80 out
	./render_bass out

# Regenerates the repo's screenshots/ from the real UI code.
screenshots: render_ui
	rm -f ../../screenshots/*.png
	./render_ui ../../screenshots

NAM_SRC = ../../lib/irdsp/src/NamFile.cpp
NAM_DEPS = $(NAM_SRC) ../../lib/irdsp/src/NamFile.h ../../lib/irdsp/src/NamA2.h ../../lib/irdsp/src/IRLoader.h

test_nam: test_nam.cpp $(NAM_DEPS)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_nam.cpp $(NAM_SRC)

test_namfile: test_namfile.cpp $(NAM_DEPS)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_namfile.cpp $(NAM_SRC)

namtool: namtool.cpp $(NAM_DEPS)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ namtool.cpp $(NAM_SRC)

irtool: irtool.cpp ../../lib/irdsp/src/IRLoader.cpp ../../lib/irdsp/src/IRLoader.h
	$(CXX) $(CXXFLAGS) $(INC) -o $@ irtool.cpp ../../lib/irdsp/src/IRLoader.cpp

test: all
	./test_convolver
	./test_fxstage
	./test_reverb80
	./test_studio_delay
	./test_drums
	./test_bass
	./test_midi
	./test_ui
	./test_nam
	./test_namfile
	python3 test_loader.py

clean:
	rm -f test_convolver test_fxstage test_reverb80 render_reverb80 test_midi test_studio_delay test_drums render_drums test_bass render_bass test_ui render_ui irtool test_nam test_namfile namtool
	rm -rf out

.PHONY: all test render screenshots clean
