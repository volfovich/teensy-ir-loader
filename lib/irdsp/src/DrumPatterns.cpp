// DrumPatterns.cpp - the built-in drum styles (see DrumMachine.h for the
// lane format). The table stays in flash on the Teensy.
//
// Lanes:  kick, snare, rim, closed hat, open hat, ride, click
#include <stdlib.h>

#include "DrumMachine.h"
#include "irdsp_port.h"

namespace irdsp {

static const DrumStyle IRDSP_PROGMEM kStyles[] = {
    {"metronome", "metronome", 4, 4,
     {"", "", "", "", "", "", "X...x...x...x..."}},
    {"rock", "rock", 4, 4,
     {"x.......x.x.....", "....x.......x...", "", "X.x.X.x.X.x.X.x.", "", "", ""}},
    {"halftime", "half-time", 4, 4,
     {"x......x..x.....", "........X.......", "", "x.x.x.x.x.x.x.x.", "", "", ""}},
    {"shuffle", "blues shuffle", 4, 3,
     {"x.....x.....", "...x.....x..", "", "X.xx.xX.xx.x", "", "", ""}},
    {"funk", "funk 16ths", 4, 4,
     {"x..x..x...x.....", "....X..g.g..X..g", "", "XgxgXgxgXgxgXgxg", "", "", ""}},
    {"metal", "metal", 4, 4,
     {"xxxxxxxxxxxxxxxx", "....X.......X...", "", "", "", "X...x...x...x...", ""}},
    {"pop", "pop", 4, 4,
     {"x.....x.x.......", "....x.......x...", "", "x.x.x.x.x.x.x...", "..............x.", "", ""}},
    {"ballad", "6/8 ballad", 2, 3,
     {"x.....", "...x..", "", "XxxXxx", "", "", ""}},
    {"reggae", "reggae", 4, 4,
     {"........X.......", "", "........x.......", "..x...x...x...x.", "", "", ""}},
    {"disco", "disco", 4, 4,
     {"x...x...x...x...", "....x.......x...", "", "x...x...x...x...", "..x...x...x...x.", "", ""}},
};

IRDSP_COLD int drumStyleCount() { return (int)(sizeof(kStyles) / sizeof(kStyles[0])); }

IRDSP_COLD const DrumStyle& drumStyle(int i) {
  return kStyles[i < 0 || i >= drumStyleCount() ? 0 : i];
}

IRDSP_COLD int findDrumStyle(const char* s) {
  if (s[0] >= '0' && s[0] <= '9') {
    const int n = atoi(s);
    return n >= 1 && n <= drumStyleCount() ? n - 1 : -1;
  }
  for (int i = 0; i < drumStyleCount(); i++) {
    const char* a = kStyles[i].name;
    const char* b = s;
    while (*a && *b && (*a | 0x20) == (*b | 0x20)) a++, b++;
    if (!*a && !*b) return i;
  }
  return -1;
}

}  // namespace irdsp
