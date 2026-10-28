// MidiDrums.cpp - see MidiDrums.h
#include "MidiDrums.h"

#include "DrumKit.h"
#include "irdsp_port.h"

namespace irdsp {

IRDSP_COLD int midiDrumLane(int note) {
  switch (note) {
    case 35: case 36: return kKick;
    case 38: case 39: case 40: return kSnare;
    case 37: return kRim;
    case 42: case 44: return kHatClosed;
    case 46: return kHatOpen;
    case 49: case 52: case 55: case 57: return kCrash;
    case 51: case 53: case 59: return kRide;
    case 47: case 48: case 50: return kTomHi;
    case 41: case 43: case 45: return kTomLo;
    default: return -1;
  }
}

namespace {

constexpr int kMaxTracks = 64, kMaxMeters = 32;

// Buffered big-endian reader over a MidiSource, with a running position.
// (The methods are defined out of the class so they can live in flash.)
struct Reader {
  MidiSource& src;
  uint8_t buf[64];
  int len = 0, at = 0;
  uint32_t pos = 0;
  bool eof = false;
  explicit Reader(MidiSource& s) : src(s) {}
  bool seek(uint32_t p);
  int byte();
  uint32_t be(int n);
  uint32_t vlq();
  void skip(uint32_t n);
};

IRDSP_COLD bool Reader::seek(uint32_t p) {
  len = at = 0;
  eof = false;
  pos = p;
  return src.seek(p);
}

IRDSP_COLD int Reader::byte() {
  if (at >= len) {
    len = src.read(buf, sizeof(buf));
    at = 0;
    if (len <= 0) {
      len = 0;
      eof = true;
      return 0;
    }
  }
  pos++;
  return buf[at++];
}

IRDSP_COLD uint32_t Reader::be(int n) {
  uint32_t v = 0;
  while (n--) v = (v << 8) | (uint32_t)byte();
  return v;
}

IRDSP_COLD uint32_t Reader::vlq() {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) {
    const int b = byte();
    v = (v << 7) | (uint32_t)(b & 0x7F);
    if (!(b & 0x80)) break;
  }
  return v;
}

IRDSP_COLD void Reader::skip(uint32_t n) {
  while (n-- && !eof) byte();
}

// Shell sort by the packed value (tick, then lane). The hits come mostly in
// order already; this stays out of RAM1, unlike std::sort.
IRDSP_COLD void sortEvents(SongEvent* e, uint32_t n) {
  static const uint32_t IRDSP_PROGMEM kGaps[] = {701, 301, 132, 57, 23, 10, 4, 1};
  for (uint32_t gap : kGaps)
    for (uint32_t i = gap; i < n; i++) {
      const SongEvent v = e[i];
      uint32_t j = i;
      for (; j >= gap && e[j - gap].packed > v.packed; j -= gap) e[j] = e[j - gap];
      e[j] = v;
    }
}

struct Meter { uint32_t tick; uint8_t beats, unit; };

struct Track { uint32_t start, length, otherDrumNotes; };

// What one pass collects.
struct Collect {
  SongEvent* events;
  uint32_t cap, count = 0, ignored = 0;
  bool overflow = false;
  int onlyTrack = -1;         // pass 2: this track, any channel
  uint32_t tempoUs = 0;       // first tempo
  Meter meters[kMaxMeters];
  int meterCount = 0;
};

// t (in the file's ticks) to our ticks, rounded. 32-bit only: a 64-bit
// division would pull a library routine into RAM1.
IRDSP_COLD uint32_t quantize(uint32_t t, uint32_t ppq) {
  return (t / ppq) * kSongPpq + ((t % ppq) * kSongPpq + ppq / 2) / ppq;
}

// Parses one track. In pass 1 (onlyTrack < 0) it takes channel-10 notes and
// counts the other channels' notes in the drum range; in pass 2 it takes
// every note of the chosen track.
IRDSP_COLD void parseTrack(Reader& r, Track& tr, int index, uint32_t ppq, Collect& c) {
  r.seek(tr.start);
  const uint32_t end = tr.start + tr.length;
  uint32_t t = 0;
  int status = 0;
  while (r.pos < end && !r.eof) {
    t += r.vlq();
    int b = r.byte();
    if (b < 0x80) {
      // Running status: b is the first data byte.
      if (!status) return;
    } else {
      status = b;
      if (b < 0xF0) b = r.byte();
    }
    if (status == 0xFF) {
      const int type = r.byte();
      const uint32_t len = r.vlq();
      if (type == 0x51 && len == 3 && !c.tempoUs) {
        c.tempoUs = r.be(3);
      } else if (type == 0x58 && len >= 2) {
        const int num = r.byte(), den = r.byte();
        r.skip(len - 2);
        if (c.meterCount < kMaxMeters && num > 0 && den <= 4) {
          c.meters[c.meterCount++] = {quantize(t, ppq), (uint8_t)num, (uint8_t)(1 << den)};
        }
      } else if (type == 0x2F) {
        return;
      } else {
        r.skip(len);
      }
      status = 0;  // meta and sysex cancel running status
      continue;
    }
    if (status == 0xF0 || status == 0xF7) {
      r.skip(r.vlq());
      status = 0;
      continue;
    }
    const int kind = status & 0xF0, chan = status & 0x0F;
    const int d1 = b;
    const int d2 = (kind == 0xC0 || kind == 0xD0) ? 0 : r.byte();
    if (kind != 0x90 || d2 == 0) continue;  // note-ons only (velocity 0 = off)
    const int lane = midiDrumLane(d1);
    const bool drumNote = d1 >= 35 && d1 <= 81;
    const bool take = c.onlyTrack < 0 ? chan == 9 : index == c.onlyTrack;
    if (!take) {
      if (c.onlyTrack < 0 && drumNote) tr.otherDrumNotes++;
      continue;
    }
    if (lane < 0) {
      c.ignored++;
      continue;
    }
    if (c.count >= c.cap) {
      c.overflow = true;
      continue;
    }
    const int vel = (d2 * 9 + 126) / 127;
    c.events[c.count++] = songEvent(quantize(t, ppq), lane, vel);
  }
}

}  // namespace

IRDSP_COLD MidiResult loadMidiDrums(MidiSource& src, SongEvent* events, uint32_t eventCap, SongBar* bars,
                                    uint16_t barCap, DrumSong& song) {
  MidiResult res;
  Reader r(src);
  r.seek(0);
  if (r.be(4) != 0x4D546864u || r.be(4) < 6) return res.error = "not a MIDI file", res;  // "MThd"
  const int format = (int)r.be(2), ntracks = (int)r.be(2), division = (int)r.be(2);
  if (r.eof || format > 1 || ntracks < 1) return res.error = "unsupported MIDI format", res;
  if (division & 0x8000 || division == 0) return res.error = "SMPTE timing is not supported", res;
  const uint32_t ppq = (uint32_t)division;

  // Find the tracks.
  Track tracks[kMaxTracks];
  int nt = 0;
  uint32_t p = 8 + 6;
  while (nt < kMaxTracks && nt < ntracks) {
    if (!r.seek(p)) break;
    const uint32_t id = r.be(4), len = r.be(4);
    if (r.eof) break;
    if (id == 0x4D54726Bu) tracks[nt++] = {p + 8, len, 0};  // "MTrk"
    p += 8 + len;
  }
  if (!nt) return res.error = "no tracks", res;

  // Pass 1: channel 10. Pass 2 if it had no drums: the track with the most
  // notes in the GM drum range.
  Collect c;
  c.events = events;
  c.cap = eventCap;
  for (int i = 0; i < nt; i++) parseTrack(r, tracks[i], i, ppq, c);
  if (c.count == 0) {
    int best = -1;
    for (int i = 0; i < nt; i++)
      if (tracks[i].otherDrumNotes && (best < 0 || tracks[i].otherDrumNotes > tracks[best].otherDrumNotes)) best = i;
    if (best < 0) return res.error = "no drum notes", res;
    c.onlyTrack = best;
    c.ignored = 0;
    parseTrack(r, tracks[best], best, ppq, c);
  }
  if (c.overflow) return res.error = "too many drum notes", res;
  if (c.count == 0) return res.error = "no drum notes", res;

  // Sort by tick, then lane (the packing's order); on the same tick and
  // lane keep the louder hit.
  sortEvents(events, c.count);
  uint32_t n = 0;
  for (uint32_t i = 0; i < c.count; i++) {
    if (n && (events[n - 1].packed >> 4) == (events[i].packed >> 4)) {
      if (events[i].vel() > events[n - 1].vel()) events[n - 1] = events[i];
    } else {
      events[n++] = events[i];
    }
  }

  // Bars: from tick 0 to past the last hit, in the meters in force.
  for (int i = 1; i < c.meterCount; i++)
    for (int j = i; j > 0 && c.meters[j - 1].tick > c.meters[j].tick; j--) {
      const Meter m = c.meters[j];
      c.meters[j] = c.meters[j - 1];
      c.meters[j - 1] = m;
    }
  const uint32_t last = events[n - 1].tick();
  uint8_t beats = 4, unit = 4;
  int mi = 0;
  uint32_t t = 0, e = 0;
  uint16_t nb = 0;
  while (t <= last) {
    while (mi < c.meterCount && c.meters[mi].tick <= t) beats = c.meters[mi].beats, unit = c.meters[mi].unit, mi++;
    if (nb >= barCap) return res.error = "too many bars", res;
    while (e < n && events[e].tick() < t) e++;
    bars[nb] = {t, e, beats, unit, 0};
    t += songBarTicks(bars[nb]);
    nb++;
  }

  uint32_t bpm = c.tempoUs ? (60000000u + c.tempoUs / 2) / c.tempoUs : 120;
  song.bpm = (uint16_t)(bpm < 40 ? 40 : (bpm > 240 ? 240 : bpm));
  song.events = events;
  song.eventCount = n;
  song.bars = bars;
  song.barCount = nb;
  song.endTick = t;
  res.ignored = c.ignored;
  return res;
}

}  // namespace irdsp
