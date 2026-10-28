// DrumPatterns.cpp - the built-in drum styles (see DrumMachine.h for the
// lane format). The table stays in flash on the Teensy.
//
// Lanes:  kick, snare, rim, closed hat, open hat, ride, click, crash, rack
// tom, floor tom
// (a lane left out is empty; crash accents come from setCrashEvery()).
#include <stdlib.h>

#include "DrumMachine.h"
#include "irdsp_port.h"

namespace irdsp {

// Velocities follow a drummer's hands: the hat strongest on 1 and 3, a bit
// less on the "&", softer under the snare on 2 and 4, softest on the 16th
// "e" and "a"; the kick strongest on 1, the snare a touch harder on 4.
// The bass lines (the last string; see DrumStyle) are simple patterns on
// the chord's root in the bass key: driving eighths for rock, a 16th feel
// with dead-note ghosts, the root-fifth-sixth-seventh boogie for the
// shuffle, roots and fifths for the ballad. The metronome has none.
static const DrumStyle IRDSP_PROGMEM kStyles[] = {
    {"metronome", "metronome", 4, 4,
     {"", "", "", "", "", "", "9...6...6...6..."}, ""},
    {"rock", "rock", 4, 4,
     {"9.......8.7.....", "....8.......9...", "", "9.6.5.6.8.6.5.6.", "", "", ""}, "R-R-R-R-R-R-5-O-"},
    // Straight beats for practice: kick on 1 and 3, snare on 2 and 4, the
    // hat on every subdivision except the beat itself.
    {"offbeat8", "offbeat 8ths", 4, 4,
     {"9.......8.......", "....8.......9...", "", "..7...6...7...6.", "", "", ""}, "R-R-R-R-R-R-R-5-"},
    {"offbeat16", "offbeat 16ths", 4, 4,
     {"9.......8.......", "....8.......9...", "", ".474.474.474.474", "", "", ""}, "R-RxR-R-R-RxR-5-"},
    {"triplets", "triplets", 4, 3,
     {"9.....8.....", "...8.....9..", "", ".65.65.65.65", "", "", ""}, "R--R--5--R--"},
    {"pop", "pop", 4, 4,
     {"9.....7.8.......", "....8.......9...", "", "9.6.5.6.8.6.5...", "..............7.", "", ""}, "R-----R-R-----5-"},
    {"shuffle", "blues shuffle", 4, 3,
     {"9.....8.....", "...8.....9..", "", "9.65.68.65.6", "", "", ""}, "R-R5-56-67-6"},
    {"ballad", "6/8 ballad", 2, 3,
     {"9.....", "...9..", "", "966566", "", "", ""}, "R--5--"},
};

IRDSP_COLD int drumStyleCount() { return (int)(sizeof(kStyles) / sizeof(kStyles[0])); }

IRDSP_COLD const DrumStyle& drumStyle(int i) {
  return kStyles[i < 0 || i >= drumStyleCount() ? 0 : i];
}

// Case-insensitive name match, or a 1-based number; the index or -1.
static IRDSP_COLD int findByName(const char* s, int count, const char* (*nameAt)(int)) {
  if (s[0] >= '0' && s[0] <= '9') {
    const int n = atoi(s);
    return n >= 1 && n <= count ? n - 1 : -1;
  }
  for (int i = 0; i < count; i++) {
    const char* a = nameAt(i);
    const char* b = s;
    while (*a && *b && (*a | 0x20) == (*b | 0x20)) a++, b++;
    if (!*a && !*b) return i;
  }
  return -1;
}

IRDSP_COLD int findDrumStyle(const char* s) {
  return findByName(s, drumStyleCount(), [](int i) -> const char* { return kStyles[i].name; });
}

IRDSP_COLD int findDrumSong(const char* s) {
  return findByName(s, drumSongCount(), [](int i) -> const char* { return drumSong(i).name; });
}

IRDSP_COLD int findDrumKit(const char* s) {
  return findByName(s, drumKitCount(), [](int i) -> const char* { return drumKit(i).name; });
}

}  // namespace irdsp
