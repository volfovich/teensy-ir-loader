# Host (PC) build of the DSP core for testing. Requires g++ and make.
CXX ?= g++
CXXFLAGS ?= -O2 -std=gnu++17 -Wall -Wextra
INC = -I../../lib/irdsp/src

all: test_convolver test_reverb test_delay test_studio_delay irtool

test_convolver: test_convolver.cpp ../../lib/irdsp/src/*.h
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_convolver.cpp

test_reverb: test_reverb.cpp ../../lib/irdsp/src/PlateReverb.h ../../lib/irdsp/src/irdsp_blocks.h
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_reverb.cpp

test_delay: test_delay.cpp ../../lib/irdsp/src/StereoDelay.h ../../lib/irdsp/src/irdsp_blocks.h
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_delay.cpp

test_studio_delay: test_studio_delay.cpp ../../lib/irdsp/src/StudioDelay.h ../../lib/irdsp/src/irdsp_blocks.h
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_studio_delay.cpp

irtool: irtool.cpp ../../lib/irdsp/src/IRLoader.cpp ../../lib/irdsp/src/IRLoader.h
	$(CXX) $(CXXFLAGS) $(INC) -o $@ irtool.cpp ../../lib/irdsp/src/IRLoader.cpp

test: all
	./test_convolver
	./test_reverb
	./test_delay
	./test_studio_delay
	python3 test_loader.py

clean:
	rm -f test_convolver test_reverb test_delay test_studio_delay irtool

.PHONY: all test clean
