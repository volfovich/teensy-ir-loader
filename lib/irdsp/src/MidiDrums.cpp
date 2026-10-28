// MidiDrums.cpp - see MidiDrums.h
#include "MidiDrums.h"

#include "DrumKit.h"
#include "PluckBass.h"
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

// Shell sort of packed events (SongEvent, BassEvent: tick in the high
// bits). They come mostly in order already; this stays out of RAM1, unlike
// std::sort or a template.
IRDSP_COLD void sortPacked(uint32_t* e, uint32_t n) {
  static const uint32_t IRDSP_PROGMEM kGaps[] = {701, 301, 132, 57, 23, 10, 4, 1};
  for (uint32_t gap : kGaps)
    for (uint32_t i = gap; i < n; i++) {
      const uint32_t v = e[i];
      uint32_t j = i;
      for (; j >= gap && e[j - gap] > v; j -= gap) e[j] = e[j - gap];
      e[j] = v;
    }
}

// A MIDI note moved by octaves into the bass's range.
IRDSP_COLD int foldBass(int note) {
  while (note < PluckBass::kMinNote) note += 12;
  while (note > PluckBass::kMaxNote) note -= 12;
  return note;
}

IRDSP_COLD int midiVel(int v) { return (v * 9 + 126) / 127; }

struct Meter { uint32_t tick; uint8_t beats, unit; };

struct Track { uint32_t start, length, otherDrumNotes; };

// A channel's notes, for finding the bass part: its program (-1 none), its
// notes and those below C3.
struct Chan { int program = -1; uint32_t notes = 0, low = 0; };

// What one pass collects.
struct Collect {
  SongEvent* events;
  uint32_t cap, count = 0, ignored = 0;
  bool overflow = false;
  int onlyTrack = -1;         // pass 2: this track, any channel
  uint32_t tempoUs = 0;       // first tempo
  Meter meters[kMaxMeters];
  int meterCount = 0;
  Chan chans[16];
  // The bass pass: the notes on and off (velocity 0) of bassChan.
  int bassChan = -1;
  BassEvent* bass = nullptr;
  uint32_t bassCap = 0, bassCount = 0;
  bool bassOverflow = false;
};

// t (in the file's ticks) to our ticks, rounded. 32-bit only: a 64-bit
// division would pull a library routine into RAM1.
IRDSP_COLD uint32_t quantize(uint32_t t, uint32_t ppq) {
  return (t / ppq) * kSongPpq + ((t % ppq) * kSongPpq + ppq / 2) / ppq;
}

// Parses one track. In pass 1 (onlyTrack < 0) it takes channel-10 notes,
// counts the other channels' notes in the drum range, and notes each
// channel's program and pitches; in pass 2 it takes every note of the
// chosen track. In the bass pass (bassChan >= 0) it only collects that
// channel's notes on and off.
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
        if (c.bassChan < 0 && c.meterCount < kMaxMeters && num > 0 && den <= 4) {
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
    if (c.bassChan >= 0) {
      if (chan != c.bassChan || (kind != 0x90 && kind != 0x80)) continue;
      const uint32_t q = quantize(t, ppq);
      if (q > kBassMaxTick) continue;
      if (c.bassCount >= c.bassCap) {
        c.bassOverflow = true;
        continue;
      }
      const bool on = kind == 0x90 && d2 > 0;
      c.bass[c.bassCount++] = bassEvent(q, d1, on ? midiVel(d2) : 0);
      continue;
    }
    if (kind == 0xC0 && c.onlyTrack < 0) c.chans[chan].program = d1;
    if (kind != 0x90 || d2 == 0) continue;  // note-ons only (velocity 0 = off)
    if (c.onlyTrack < 0 && chan != 9) {
      c.chans[chan].notes++;
      if (d1 < 48) c.chans[chan].low++;
    }
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
    c.events[c.count++] = songEvent(quantize(t, ppq), lane, midiVel(d2));
  }
}

// The bass part's channel: a GM bass program (33-40, 0-based 32-39) with
// the most notes; else, unless the drums came from a whole track (which
// may hold low notes of its own), the channel with the largest share of
// notes below C3, if over half. -1: none.
IRDSP_COLD int pickBassChannel(const Collect& c) {
  int best = -1;
  for (int ch = 0; ch < 16; ch++) {
    const Chan& k = c.chans[ch];
    if (ch != 9 && k.notes && k.program >= 32 && k.program <= 39 && (best < 0 || k.notes > c.chans[best].notes)) best = ch;
  }
  if (best >= 0 || c.onlyTrack >= 0) return best;
  for (int ch = 0; ch < 16; ch++) {
    const Chan& k = c.chans[ch];
    if (ch == 9 || k.low * 2 <= k.notes) continue;
    if (best < 0 || (uint64_t)k.low * c.chans[best].notes > (uint64_t)c.chans[best].low * k.notes) best = ch;
  }
  return best;
}

// Reduces the collected notes on and off (sorted) to one bass line, in
// place: per tick, the lowest new note plays (folded into range); a note-off
// of the sounding note mutes it. Returns the events kept.
IRDSP_COLD uint32_t reduceBass(BassEvent* e, uint32_t count) {
  uint32_t n = 0;
  int sounding = -1;  // the MIDI note playing, before folding
  for (uint32_t i = 0; i < count;) {
    const uint32_t tick = e[i].tick();
    uint32_t j = i;
    int low = -1, lowVel = 0;
    bool offSounding = false, offLow = false;
    for (; j < count && e[j].tick() == tick; j++) {
      const int note = e[j].note();
      if (e[j].vel() == 0) {
        if (note == sounding) offSounding = true;
      } else if (low < 0 || note < low) {
        low = note;
        lowVel = e[j].vel();
      }
    }
    // An off of the new pitch on the same tick ends it at once, unless the
    // same pitch was already sounding (then the off was the old note's).
    if (low >= 0)
      for (uint32_t k = i; k < j; k++) offLow = offLow || (e[k].vel() == 0 && e[k].note() == low && low != sounding);
    if (low >= 0) {
      e[n++] = bassEvent(tick, foldBass(low), lowVel);
      sounding = low;
      if (offLow) {
        e[n++] = bassEvent(tick + 1, 0, 0);
        sounding = -1;
      }
    } else if (offSounding) {
      e[n++] = bassEvent(tick, 0, 0);
      sounding = -1;
    }
    i = j;
  }
  return n;
}

}  // namespace

IRDSP_COLD MidiResult loadMidiDrums(MidiSource& src, SongEvent* events, uint32_t eventCap, SongBar* bars,
                                    uint16_t barCap, DrumSong& song, BassEvent* bass, uint32_t bassCap) {
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
  sortPacked(&events[0].packed, c.count);
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
  // The bass part: a pass over every track for its channel's notes.
  uint32_t nBass = 0;
  const int bassChan = bass && bassCap ? pickBassChannel(c) : -1;
  if (bassChan >= 0) {
    c.bassChan = bassChan;
    c.bass = bass;
    c.bassCap = bassCap;
    for (int i = 0; i < nt; i++) parseTrack(r, tracks[i], i, ppq, c);
    if (!c.bassOverflow && c.bassCount) {
      sortPacked(&bass[0].packed, c.bassCount);
      nBass = reduceBass(bass, c.bassCount);
      res.bassChannel = bassChan + 1;
    }
  }

  const uint32_t last = events[n - 1].tick();
  uint8_t beats = 4, unit = 4;
  int mi = 0;
  uint32_t t = 0, e = 0, eb = 0;
  uint16_t nb = 0;
  while (t <= last) {
    while (mi < c.meterCount && c.meters[mi].tick <= t) beats = c.meters[mi].beats, unit = c.meters[mi].unit, mi++;
    if (nb >= barCap) return res.error = "too many bars", res;
    while (e < n && events[e].tick() < t) e++;
    while (eb < nBass && bass[eb].tick() < t) eb++;
    bars[nb] = {t, e, beats, unit, eb};
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
  song.bass = nBass ? bass : nullptr;
  song.bassCount = nBass;
  res.ignored = c.ignored;
  return res;
}

}  // namespace irdsp
