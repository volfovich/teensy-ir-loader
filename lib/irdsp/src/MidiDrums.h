// MidiDrums.h - reads the drum part of a Standard MIDI File (format 0 or 1)
// into a DrumSong (DrumSong.h), for playing real songs on the drum machine.
//
//   - the drum notes on MIDI channel 10 (or, when there are none, the track
//     with the most notes in the General MIDI drum range), mapped to lanes:
//       35 36 kick | 38 40 39 snare | 37 rim | 42 44 closed hat | 46 open hat
//       49 52 55 57 crash | 51 53 59 ride | 48 50 47 rack tom | 41 43 45 floor tom
//     other notes are skipped (counted in `ignored`)
//   - timing quantized to kSongPpq (24) ticks per quarter note
//   - velocity 1..127 -> 1..9; two hits on the same tick and lane keep the
//     louder one
//   - the first tempo -> the song's BPM (40..240, 120 if none); time
//     signatures -> the bar table
//
// The file is read through MidiSource, so the firmware can stream it from
// the SD card. All of this runs from flash on the Teensy (IRDSP_COLD).
#pragma once

#include <stdint.h>

#include "DrumSong.h"

namespace irdsp {

class MidiSource {
 public:
  // Reads up to n bytes at the current position; returns how many.
  virtual int read(uint8_t* dst, int n) = 0;
  virtual bool seek(uint32_t pos) = 0;

 protected:
  ~MidiSource() = default;
};

struct MidiResult {
  const char* error = nullptr;  // nullptr: loaded
  uint32_t ignored = 0;         // drum notes with no lane
};

// The GM drum note's lane, or -1.
int midiDrumLane(int note);

// Loads the drum part into events[eventCap] and bars[barCap], and fills
// song's bpm, events, eventCount, bars, barCount and endTick (the caller
// sets the name and title).
MidiResult loadMidiDrums(MidiSource& src, SongEvent* events, uint32_t eventCap, SongBar* bars, uint16_t barCap,
                         DrumSong& song);

}  // namespace irdsp
