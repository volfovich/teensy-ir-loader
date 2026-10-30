#include "Params.h"

#include <Arduino.h>
#include <Audio.h>

#include "AmpLibrary.h"
#include "AudioGraph.h"
#include "DrumKit.h"
#include "DrumMachine.h"
#include "DrumSongs.h"
#include "Settings.h"
#include "config.h"

using namespace ui;

static int songBars() {
  const irdsp::DrumSong* s = currentDrumSong();
  return s ? s->barCount : 1;
}

int getParam(int id) {
  if (id >= 0 && id < kParamCount) return g_settings.v[id];
  switch (id) {
    case kDrumsPlay: return convolver.drums().playing();
    case kDrumLoopFrom: return drumLoopFirst();
    case kDrumLoopTo: return drumLoopFirst() ? drumLoopLast() : songBars();
    default: return 0;
  }
}

void paramLimits(int id, int* lo, int* hi) {
  const ParamDef& d = paramDef(id);
  *lo = d.min;
  *hi = d.max;
  switch (id) {
    case kDelayTime: *hi = DELAY_MAX_MS; break;
    case kDrumStyle: *hi = irdsp::drumStyleCount() - 1; break;
    case kDrumKit: *hi = irdsp::drumKitCount() - 1; break;
    case kDrumSong: *hi = drumSongTotal(); break;
    case kDrumLoopFrom:
    case kDrumLoopTo: *hi = songBars(); break;
    default: break;
  }
}

void paramText(int id, char* buf, int n) {
  const int v = getParam(id);
  switch (id) {
    case kDrumStyle: snprintf(buf, n, "%s", irdsp::drumStyle(v).title); return;
    case kDrumKit: snprintf(buf, n, "%s", irdsp::drumKit(v).name); return;
    case kDrumSong: snprintf(buf, n, "%s", v ? drumSongTitle(v) : "off"); return;
    case kDrumLoopFrom:
      if (!v) {
        snprintf(buf, n, "off");
        return;
      }
      break;
    default: break;
  }
  formatValue(paramDef(id), v, false, buf, n);
}

// What each setting does. Saved values are already in g_settings.
static void applyParam(int id, int v) {
  irdsp::StudioDelay& d = convolver.delay();
  irdsp::Reverb80& r = convolver.reverb();
  irdsp::DrumMachine& dm = convolver.drums();
  const int16_t* s = g_settings.v;
  switch (id) {
    case kDelayOn: convolver.setDelayOn(v); break;
    case kDelayMix: convolver.setDelayMix(v / 100.0f); break;
    case kDelayTime: d.setDelaySamples(v * (AUDIO_SAMPLE_RATE_EXACT / 1000.0f)); break;
    case kDelayRepeats: d.setFeedback(v / 100.0f); break;
    case kDelayTone: d.setTone(v / 100.0f); break;
    case kDelayMod: d.setModDepth(v / 100.0f); break;
    case kDelayDuck: d.setDuck(v / 100.0f); break;
    case kDelayDrive: d.setDrive(v / 100.0f); break;
    case kDelayEra: d.setEra(v / 100.0f); break;
    case kReverbOn: convolver.setReverbOn(v); break;
    case kReverbMix: convolver.setReverbMix(v / 100.0f); break;
    case kReverbSize: r.setSize(v / 100.0f); break;
    case kReverbTone: r.setTone(v / 100.0f); break;
    case kReverbMode: r.setMode(v); break;
    case kReverbPreDelay: r.setPreDelayMs(v); break;
    case kReverbMod: r.setMod(v / 100.0f); break;
    case kReverbEra: r.setEra(v / 100.0f); break;
    case kReverbGate: r.setGateMs(v); break;
    case kOutGain: convolver.setOutputGainDb(v * 0.5f); break;
    case kLineIn:
      codec.lineInLevel(v);
      ampApplyGains();  // the calibration follows the input's full scale
      break;
    case kLineOut: codec.lineOutLevel(v); break;
    case kHeadphones: codec.volume(v / 100.0f); break;
    case kBypass: convolver.setBypass(v); break;
    case kDrumStyle: dm.setStyle(v); break;
    case kDrumKit: dm.setKit(&irdsp::drumKit(v)); break;
    case kDrumCrash: dm.setCrashEvery(v ? 1 << v : 0); break;  // 2, 4, 8, 16 bars
    case kDrumTempo: dm.setTempo(v); break;
    case kDrumVolume: dm.setVolume(v / 100.0f); break;
    case kDrumComp: dm.setBusComp(v / 100.0f); break;
    case kDrumCountIn: dm.setCountIn(v); break;
    case kTrainerOn:
    case kTrainerStep:
    case kTrainerEvery:
    case kTrainerTarget:
      dm.setTrainer(s[kTrainerOn], s[kTrainerStep], s[kTrainerEvery], s[kTrainerTarget]);
      break;
    case kBassOn: dm.setBassOn(v); break;
    case kBassVolume: dm.setBassVolume(v / 100.0f); break;
    case kBassKey: dm.setBassKey(v); break;
    case kBassTone: dm.setBassTone(v / 100.0f); break;
    case kAmpOn: convolver.amp().setOn(v); break;
    case kAmpIn:
    case kAmpOut:
    case kAmpCal:
    case kAmpGate:
    case kAmpBass:
    case kAmpMid:
    case kAmpTreble:
    case kAmpPresence:
    case kAmpGateRel: ampApplyGains(); break;
    case kDblOn: convolver.doubler().setOn(v); break;
    case kDblTime:
    case kDblWidth: convolver.doubler().set(s[kDblTime], s[kDblWidth] / 100.0f); break;
    case kDrumsPlay:
      if (v) dm.start();
      else dm.stop();
      break;
    case kDrumLoopFrom:
      if (!v) setDrumLoop(0, 0);
      else setDrumLoop(v, max(v, drumLoopFirst() ? drumLoopLast() : songBars()));
      break;
    case kDrumLoopTo: setDrumLoop(drumLoopFirst() ? drumLoopFirst() : 1, v); break;
    default: break;  // kDrumSong: selected in setParam
  }
}

// "DRUMS START: rock, 100 BPM, 2-bar count-in" / "DRUMS STOP".
static void printDrums(bool on) {
  if (!on) {
    Serial.println("DRUMS STOP");
    return;
  }
  const irdsp::DrumSong* song = currentDrumSong();
  Serial.printf("DRUMS START: %s, %d BPM", song ? song->title : irdsp::drumStyle(getParam(kDrumStyle)).title,
                getParam(kDrumTempo));
  if (getParam(kDrumCountIn)) Serial.printf(", %d-bar count-in", getParam(kDrumCountIn));
  Serial.println();
}

void setParam(int id, int v) {
  int lo, hi;
  paramLimits(id, &lo, &hi);
  v = constrain(v, lo, hi);
  if (id == kDrumSong && !selectDrumSong(v)) v = 0;  // prints its own message
  if (id < kParamCount) {
    g_settings.v[id] = v;
    scheduleSave();
  }
  applyParam(id, v);
  switch (id) {
    case kDrumSong:
    case kDrumLoopFrom:
    case kDrumLoopTo: break;  // their own messages
    case kDrumsPlay: printDrums(v); break;
    default: {
      char text[32];
      paramText(id, text, sizeof(text));
      Serial.printf("%s %s\n", paramDef(id).name, text);
    }
  }
}

void applyAllParams() {
  for (int id = 0; id < kParamCount; id++)
    if (id != kDrumSong) applyParam(id, g_settings.v[id]);
}
