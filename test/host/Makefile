# Host (PC) build of the DSP core for testing. Requires g++ and make.
CXX ?= g++
CXXFLAGS ?= -O2 -std=gnu++17 -Wall -Wextra
INC = -I../../lib/irdsp/src

all: test_convolver test_reverb irtool

test_convolver: test_convolver.cpp ../../lib/irdsp/src/*.h
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_convolver.cpp

test_reverb: test_reverb.cpp ../../lib/irdsp/src/PlateReverb.h
	$(CXX) $(CXXFLAGS) $(INC) -o $@ test_reverb.cpp

irtool: irtool.cpp ../../lib/irdsp/src/IRLoader.cpp ../../lib/irdsp/src/IRLoader.h
	$(CXX) $(CXXFLAGS) $(INC) -o $@ irtool.cpp ../../lib/irdsp/src/IRLoader.cpp

test: all
	./test_convolver
	./test_reverb
	python3 test_loader.py

clean:
	rm -f test_convolver test_reverb irtool

.PHONY: all test clean
