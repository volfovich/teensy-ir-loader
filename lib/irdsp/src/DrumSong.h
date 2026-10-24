// DrumSong.h - multi-bar drum songs for DrumMachine: a list of hits on a
// 24-ticks-per-quarter grid (16ths, 16th triplets and 32nds all land on a
// tick) and a bar table with each bar's meter.
//
// Built-in songs are made from songs/*.txt by tools/make_songs.py, which
// writes DrumSongData.cpp (in flash on the Teensy). The firmware also loads
// songs from MIDI files on the SD card into the same format (MidiDrums.h).
#pragma once

#include <stdint.h>

namespace irdsp {

constexpr int kSongPpq = 24;  // ticks per quarter note

// One hit, packed in 4 bytes: tick (24 bits, about 97 hours at
// 120 BPM), lane (DrumVoice, 4 bits) and velocity (1..9, 4 bits).
struct SongEvent {
  uint32_t packed;
  uint32_t tick() const { return packed >> 8; }
  uint8_t lane() const { return (packed >> 4) & 15u; }
  uint8_t vel() const { return packed & 15u; }
};
constexpr SongEvent songEvent(uint32_t tick, int lane, int vel) {
  return SongEvent{(tick << 8) | ((uint32_t)lane & 15u) << 4 | ((uint32_t)vel & 15u)};
}

struct SongBar {
  uint32_t tick;        // first tick of the bar
  uint32_t firstEvent;  // index of the bar's first event (or of the next one after it)
  uint8_t beats, beatUnit;  // meter, e.g. 4/4, 6/8
};

struct DrumSong {
  char name[16];   // command name ("sandman")
  char title[24];  // display name
  uint16_t bpm;    // quarter notes per minute
  const SongEvent* events;  // sorted by tick
  uint32_t eventCount;
  const SongBar* bars;
  uint16_t barCount;
  uint32_t endTick;  // the tick after the last bar
};

// Ticks in a bar of the given meter.
inline uint32_t songBarTicks(const SongBar& b) { return (uint32_t)b.beats * kSongPpq * 4u / b.beatUnit; }

// The built-in songs (DrumSongData.cpp).
int drumSongCount();
const DrumSong& drumSong(int i);
// The song named (case-insensitive) or numbered 1..count; -1 if none.
int findDrumSong(const char* nameOrNumber);

}  // namespace irdsp
