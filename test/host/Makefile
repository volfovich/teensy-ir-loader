# Host (PC) build of the DSP core for testing. Requires g++ and make.
CXX ?= g++
CXXFLAGS ?= -O2 -std=gnu++17 -Wall -Wextra
INC = -I../../lib/irdsp/src -I../../lib/oledui/src
UI_SRC = $(wildcard ../../lib/oledui/src/*.cpp)
UI_DEPS = $(UI_SRC) $(wildcard ../../lib/oledui/src/*.h) ui_fake.h

all: test_convolver test_reverb test_delay test_studio_delay test_ui render_ui irtool

test_convolver: test_convolver.cpp ../../lib/irdsp/src/*.h
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_convolver.cpp

test_reverb: test_reverb.cpp ../../lib/irdsp/src/PlateReverb.h ../../lib/irdsp/src/irdsp_blocks.h
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_reverb.cpp

test_delay: test_delay.cpp ../../lib/irdsp/src/StereoDelay.h ../../lib/irdsp/src/irdsp_blocks.h
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_delay.cpp

test_studio_delay: test_studio_delay.cpp ../../lib/irdsp/src/StudioDelay.h ../../lib/irdsp/src/irdsp_blocks.h
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_studio_delay.cpp

test_ui: test_ui.cpp $(UI_DEPS)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_ui.cpp $(UI_SRC)

render_ui: render_ui.cpp $(UI_DEPS)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ render_ui.cpp $(UI_SRC)

# Renders every OLED screen to out/*.png.
render: render_ui
	mkdir -p out
	./render_ui out

# Regenerates the repo's screenshots/ from the real UI code.
screenshots: render_ui
	rm -f ../../screenshots/*.png
	./render_ui ../../screenshots

irtool: irtool.cpp ../../lib/irdsp/src/IRLoader.cpp ../../lib/irdsp/src/IRLoader.h
	$(CXX) $(CXXFLAGS) $(INC) -o $@ irtool.cpp ../../lib/irdsp/src/IRLoader.cpp

test: all
	./test_convolver
	./test_reverb
	./test_delay
	./test_studio_delay
	./test_ui
	python3 test_loader.py

clean:
	rm -f test_convolver test_reverb test_delay test_studio_delay test_ui render_ui irtool
	rm -rf out

.PHONY: all test render screenshots clean
