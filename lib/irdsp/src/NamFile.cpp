// NamFile.cpp - see NamFile.h. Background code only: on the Teensy it runs
// from flash (tools/imxrt1062_cold.ld).
#include "NamFile.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "NamA2.h"
#include "irdsp_port.h"

namespace irdsp {

namespace {

// A pull reader for JSON over a ByteSource: a small buffer, one character of
// lookahead, and the file offset of every character, so a value can be read
// again later with seek(). A read error or bad syntax makes it fail for
// good (ok() false); every call then returns false.
class Json {
 public:
  explicit Json(ByteSource& src) : src_(src) {}

  bool ok() const { return ok_; }
  bool fail() { return ok_ = false; }
  // The offset of the next value (after any whitespace).
  uint32_t offset() {
    ws();
    return base_ + pos_;
  }
  bool seek(uint32_t off) {
    if (!ok_ || !src_.seek(off)) return fail();
    base_ = off;
    pos_ = len_ = 0;
    return true;
  }
  // The next character after whitespace, not consumed (-1 at the end).
  int next() {
    ws();
    return peek();
  }
  bool expect(char ch) {
    ws();
    return get() == ch ? true : fail();
  }

  // A string, truncated to cap - 1 characters (out may be null). Escapes
  // keep the escaped character; \u.... becomes '?'.
  bool string(char* out, int cap) {
    if (!expect('"')) return false;
    int n = 0;
    for (;;) {
      int c = get();
      if (c < 0) return fail();
      if (c == '"') break;
      if (c == '\\') {
        c = get();
        if (c == 'u') {
          for (int i = 0; i < 4; i++) get();
          c = '?';
        }
        if (c < 0) return fail();
      }
      if (out && n < cap - 1) out[n++] = (char)c;
    }
    if (out) out[n] = 0;
    return true;
  }

  // A number (also NaN and Infinity, which Python's json module writes).
  bool number(double* v) {
    ws();
    char tok[32];
    int n = 0;
    for (int c = peek(); c >= 0 && (isalnum(c) || c == '-' || c == '+' || c == '.'); c = peek()) {
      if (n == (int)sizeof(tok) - 1) return fail();
      tok[n++] = (char)c;
      pos_++;
    }
    tok[n] = 0;
    char* end;
    *v = strtod(tok, &end);
    return n && end == tok + n ? true : fail();
  }

  // true (1), false (0) or null (-1).
  bool literal(int* v) {
    ws();
    char tok[8];
    int n = 0;
    for (int c = peek(); c >= 0 && isalpha(c) && n < 7; c = peek()) {
      tok[n++] = (char)c;
      pos_++;
    }
    tok[n] = 0;
    if (!strcmp(tok, "true")) *v = 1;
    else if (!strcmp(tok, "false")) *v = 0;
    else if (!strcmp(tok, "null")) *v = -1;
    else return fail();
    return true;
  }

  // Skips any value. Objects and arrays are scanned to the matching bracket
  // character by character (their numbers are never converted).
  bool skip() {
    int c = next();
    if (c == '"') return string(nullptr, 0);
    if (c == '{' || c == '[') {
      int depth = 0;
      for (;;) {
        c = peek();
        if (c < 0) return fail();
        if (c == '"') {
          if (!string(nullptr, 0)) return false;
          continue;
        }
        pos_++;
        if (c == '{' || c == '[') depth++;
        else if ((c == '}' || c == ']') && --depth == 0) return true;
      }
    }
    int n = 0;
    for (c = peek(); c >= 0 && (isalnum(c) || c == '-' || c == '+' || c == '.'); c = peek()) {
      pos_++;
      n++;
    }
    return n ? true : fail();
  }

  // Objects: after '{', each call reads the next key and its ':' (true), or
  // the closing '}' (false; check ok() to tell the end from an error).
  bool key(char* out, int cap, bool* first) {
    if (!more('}', first)) return false;
    return string(out, cap) && expect(':');
  }
  // Arrays: after '[', true while there is another item to read.
  bool item(bool* first) { return more(']', first); }

 private:
  int peek() {
    if (pos_ == len_ && !fill()) return -1;
    return buf_[pos_];
  }
  int get() {
    const int c = peek();
    if (c >= 0) pos_++;
    return c;
  }
  void ws() {
    for (int c = peek(); c == ' ' || c == '\n' || c == '\r' || c == '\t'; c = peek()) pos_++;
  }
  bool fill() {
    if (!ok_) return false;
    base_ += len_;
    pos_ = 0;
    const int n = src_.read(buf_, sizeof(buf_));
    len_ = n > 0 ? n : 0;
    return len_ > 0;
  }
  // The separator before the next member, or the closing bracket.
  bool more(char close, bool* first) {
    if (!ok_) return false;
    const int c = next();
    if (c == close) {
      pos_++;
      return false;
    }
    if (!*first) {
      if (c != ',') return fail();
      pos_++;
    }
    *first = false;
    return true;
  }

  ByteSource& src_;
  uint8_t buf_[256];
  int pos_ = 0, len_ = 0;
  uint32_t base_ = 0;  // file offset of buf_[0]
  bool ok_ = true;
};

// Where a model's parts are, from a first pass over its object.
struct ModelAt {
  char arch[24] = "";
  uint32_t config = 0, weights = 0;
  bool hasConfig = false, hasWeights = false;
  float sampleRate = NAN, loudness = NAN;
};

bool readFloat(Json& j, float* v) {
  double d;
  if (!j.number(&d)) return false;
  *v = (float)d;
  return true;
}

// A metadata object: all of it into info (if given), or just the loudness.
bool readMetadata(Json& j, NamInfo* info, float* loudness) {
  if (!j.expect('{')) return false;
  char k[24];
  bool first = true;
  while (j.key(k, sizeof(k), &first)) {
    const int c = j.next();
    bool ok;
    if (!strcmp(k, "loudness") && c != 'n') ok = readFloat(j, loudness);
    else if (!info || c == 'n' || c == '{' || c == '[') ok = j.skip();
    else if (!strcmp(k, "name")) ok = j.string(info->name, sizeof(info->name));
    else if (!strcmp(k, "gear_make")) ok = j.string(info->gearMake, sizeof(info->gearMake));
    else if (!strcmp(k, "gear_model")) ok = j.string(info->gearModel, sizeof(info->gearModel));
    else if (!strcmp(k, "gear_type")) ok = j.string(info->gearType, sizeof(info->gearType));
    else if (!strcmp(k, "tone_type")) ok = j.string(info->toneType, sizeof(info->toneType));
    else if (!strcmp(k, "input_level_dbu")) ok = readFloat(j, &info->inputLevelDbu);
    else if (!strcmp(k, "output_level_dbu")) ok = readFloat(j, &info->outputLevelDbu);
    else ok = j.skip();
    if (!ok) return false;
  }
  return j.ok();
}

// First pass over a model object: notes where its config and weights are.
bool readModel(Json& j, ModelAt* m, NamInfo* info) {
  if (!j.expect('{')) return false;
  char k[24];
  bool first = true;
  while (j.key(k, sizeof(k), &first)) {
    bool ok;
    if (!strcmp(k, "architecture")) {
      ok = j.string(m->arch, sizeof(m->arch));
    } else if (!strcmp(k, "config")) {
      m->config = j.offset();
      m->hasConfig = true;
      ok = j.skip();
    } else if (!strcmp(k, "weights")) {
      m->weights = j.offset();
      m->hasWeights = true;
      ok = j.skip();
    } else if (!strcmp(k, "sample_rate") && j.next() != 'n') {
      ok = readFloat(j, &m->sampleRate);
    } else if (!strcmp(k, "metadata") && j.next() == '{') {
      ok = readMetadata(j, info, &m->loudness);
    } else {
      ok = j.skip();
    }
    if (!ok) return false;
  }
  return j.ok();
}

// Results of the shape checks: the A2 shape, another network, or a read error.
enum Shape : int { kNotA2 = 0, kA2 = 1, kError = -1 };

Shape fromOk(bool ok, bool a2) { return !ok ? kError : (a2 ? kA2 : kNotA2); }

// An array of integers equal to want[0 .. n-1].
Shape intArray(Json& j, const int* want, int n) {
  if (j.next() != '[') return fromOk(j.skip(), false);
  j.expect('[');
  bool first = true, same = true;
  int i = 0;
  while (j.item(&first)) {
    double v;
    if (!j.number(&v)) return kError;
    same = same && i < n && v == (double)want[i];
    i++;
  }
  return fromOk(j.ok(), same && i == n);
}

// An activation: {"type": "LeakyReLU", "negative_slope": 0.01}.
Shape leakyRelu(Json& j) {
  if (j.next() != '{') return fromOk(j.skip(), false);
  j.expect('{');
  char k[24], type[24] = "";
  double slope = -1.0;
  bool first = true, ok = true;
  while (ok && j.key(k, sizeof(k), &first)) {
    if (!strcmp(k, "type")) ok = j.string(type, sizeof(type));
    else if (!strcmp(k, "negative_slope")) ok = j.number(&slope);
    else ok = j.skip();
  }
  return fromOk(ok && j.ok(), !strcmp(type, "LeakyReLU") && fabs(slope - nama2::kLeakySlope) < 1e-6);
}

// One value, or an array of n of them, each passing check.
template <typename Check>
Shape eachOrOne(Json& j, int n, Check check) {
  if (j.next() != '[') return check(j);
  j.expect('[');
  bool first = true;
  Shape all = kA2;
  int i = 0;
  while (j.item(&first)) {
    const Shape s = check(j);
    if (s == kError) return kError;
    if (s == kNotA2) all = kNotA2;
    i++;
  }
  return !j.ok() ? kError : (i == n ? all : kNotA2);
}

// An object {"active": ..., "groups": ...}: active must be wantActive, groups 1.
Shape activeObject(Json& j, bool wantActive) {
  if (j.next() != '{') return fromOk(j.skip(), false);
  j.expect('{');
  char k[24];
  bool first = true, a2 = true, sawActive = false;
  while (j.key(k, sizeof(k), &first)) {
    if (!strcmp(k, "active")) {
      int v = 0;
      if (!j.literal(&v)) return kError;
      a2 = a2 && (v == 1) == wantActive;
      sawActive = true;
    } else if (!strcmp(k, "groups")) {
      double g;
      if (!j.number(&g)) return kError;
      a2 = a2 && g == 1;
    } else if (!j.skip()) {
      return kError;
    }
  }
  return fromOk(j.ok(), a2 && sawActive);
}

// The layer array's head: 1 output, 16 taps, with bias.
Shape layerHead(Json& j) {
  if (j.next() != '{') return fromOk(j.skip(), false);
  j.expect('{');
  char k[24];
  bool first = true, a2 = true, bias = false;
  double outCh = 0, taps = 0, dilation = 1;
  while (j.key(k, sizeof(k), &first)) {
    bool ok;
    if (!strcmp(k, "out_channels")) ok = j.number(&outCh);
    else if (!strcmp(k, "kernel_size")) ok = j.number(&taps);
    else if (!strcmp(k, "dilation") || !strcmp(k, "head_dilation")) ok = j.number(&dilation);
    else if (!strcmp(k, "bias")) {
      int v = 0;
      ok = j.literal(&v);
      bias = v == 1;
    } else ok = j.skip();
    if (!ok) return kError;
  }
  a2 = outCh == 1 && taps == nama2::kHeadTaps && dilation == 1 && bias;
  return fromOk(j.ok(), a2);
}

// Integer key equal to want.
Shape intIs(Json& j, int want) {
  if (j.next() == 'n' || j.next() == '{' || j.next() == '[' || j.next() == '"') return fromOk(j.skip(), false);
  double v;
  return fromOk(j.number(&v), v == want);
}

// The one layer array of an A2 WaveNet: sets *channels.
Shape a2Layer(Json& j, int* channels) {
  if (j.next() != '{') return fromOk(j.skip(), false);
  j.expect('{');
  char k[32];
  bool first = true;
  double ch = 0, bottleneck = -1;
  bool kernels = false, dilations = false, activation = false, layer1x1 = false, head = false;
  while (j.key(k, sizeof(k), &first)) {
    Shape s = kA2;
    const size_t len = strlen(k);
    if (!strcmp(k, "channels")) s = fromOk(j.number(&ch), true);
    else if (!strcmp(k, "bottleneck")) s = fromOk(j.number(&bottleneck), true);
    else if (!strcmp(k, "input_size") || !strcmp(k, "condition_size") || !strcmp(k, "groups_input") ||
             !strcmp(k, "groups_input_mixin"))
      s = intIs(j, 1);
    else if (!strcmp(k, "kernel_sizes")) {
      s = intArray(j, nama2::kKernel, nama2::kLayers);
      kernels = s == kA2;
    } else if (!strcmp(k, "dilations")) {
      s = intArray(j, nama2::kDilation, nama2::kLayers);
      dilations = s == kA2;
    } else if (!strcmp(k, "kernel_size")) {
      s = fromOk(j.skip(), false);  // A1: one kernel size for all layers
    } else if (!strcmp(k, "activation")) {
      s = eachOrOne(j, nama2::kLayers, leakyRelu);
      activation = s == kA2;
    } else if (!strcmp(k, "gated")) {
      int v = 0;
      s = fromOk(j.literal(&v), v != 1);
    } else if (!strcmp(k, "gating_mode")) {
      s = eachOrOne(j, nama2::kLayers, [](Json& jj) {
        char mode[16];
        return fromOk(jj.string(mode, sizeof(mode)), !strcmp(mode, "none"));
      });
    } else if (!strcmp(k, "secondary_activation") || !strcmp(k, "slimmable")) {
      s = eachOrOne(j, nama2::kLayers, [](Json& jj) {
        int v = 0;
        return jj.next() == 'n' ? fromOk(jj.literal(&v), true) : fromOk(jj.skip(), false);
      });
    } else if (!strcmp(k, "layer1x1")) {
      s = activeObject(j, true);
      layer1x1 = s == kA2;
    } else if (!strcmp(k, "head1x1") || (len > 5 && !strcmp(k + len - 5, "_film"))) {
      s = activeObject(j, false);
    } else if (!strcmp(k, "head")) {
      s = layerHead(j);
      head = s == kA2;
    } else {
      s = fromOk(j.skip(), true);
    }
    if (s != kA2) return s;
  }
  if (!j.ok()) return kError;
  if (bottleneck < 0) bottleneck = ch;
  *channels = (int)ch;
  return fromOk(true, (ch == 3 || ch == 8) && bottleneck == ch && kernels && dilations && activation && layer1x1 &&
                          head);
}

// A WaveNet config: the A2 shape, with *channels 3 (Lite) or 8 (Full)?
Shape a2Config(Json& j, int* channels) {
  if (!j.expect('{')) return kError;
  char k[24];
  bool first = true, headScale = false;
  int layers = 0;
  while (j.key(k, sizeof(k), &first)) {
    Shape s = kA2;
    if (!strcmp(k, "layers")) {
      if (j.next() != '[') return fromOk(j.skip(), false);
      j.expect('[');
      bool f = true;
      while (j.item(&f)) {
        s = layers++ ? fromOk(j.skip(), false) : a2Layer(j, channels);
        if (s != kA2) return s;
      }
      if (!j.ok()) return kError;
    } else if (!strcmp(k, "head") || !strcmp(k, "condition_dsp")) {
      // No post-stack head and no conditioning network.
      int v = 0;
      s = j.next() == 'n' ? fromOk(j.literal(&v), true) : fromOk(j.skip(), false);
    } else if (!strcmp(k, "head_scale")) {
      double v;
      s = fromOk(j.number(&v), true);
      headScale = true;
    } else {
      s = fromOk(j.skip(), true);
    }
    if (s != kA2) return s;
  }
  return fromOk(j.ok(), layers == 1 && headScale);
}

constexpr int kMaxSubmodels = 8;

const char* const kErrRead = "can't read the file";
const char* const kErrJson = "damaged file (not valid JSON)";
const char* const kErrA1 = "not an A2 model: download the A2 version";
const char* const kErrNoLite = "no A2-Lite network in this file";
const char* const kErrWeights = "damaged file (wrong number of weights)";

}  // namespace

IRDSP_COLD bool readNamA2Lite(ByteSource& src, float* weights, NamInfo* info, const char** err) {
  NamInfo dummy;
  if (!info) info = &dummy;
  *info = NamInfo();
  Json j(src);
  if (!j.seek(0)) return *err = kErrRead, false;

  // Pass 1: the top level, and where each submodel is.
  ModelAt top;
  if (!readModel(j, &top, info) || !top.hasConfig) return *err = kErrJson, false;

  ModelAt net;  // the network to read
  int channels = 0;
  if (!strcmp(top.arch, "SlimmableContainer")) {
    info->container = true;
    uint32_t sub[kMaxSubmodels];
    int subs = 0;
    if (!j.seek(top.config) || !j.expect('{')) return *err = kErrJson, false;
    char k[24];
    bool first = true;
    while (j.key(k, sizeof(k), &first)) {
      if (strcmp(k, "submodels") || j.next() != '[') {
        if (!j.skip()) break;
        continue;
      }
      j.expect('[');
      bool f = true;
      while (j.item(&f)) {
        // {"max_value": ..., "model": {...}}
        if (!j.expect('{')) break;
        char kk[24];
        bool ff = true;
        while (j.key(kk, sizeof(kk), &ff)) {
          if (!strcmp(kk, "model") && subs < kMaxSubmodels) sub[subs++] = j.offset();
          if (!j.skip()) break;
        }
      }
    }
    if (!j.ok()) return *err = kErrJson, false;
    // The first submodel in the Lite shape.
    for (int i = 0; i < subs && channels != 3; i++) {
      ModelAt m;
      if (!j.seek(sub[i]) || !readModel(j, &m, nullptr)) return *err = kErrJson, false;
      if (strcmp(m.arch, "WaveNet") || !m.hasConfig || !j.seek(m.config)) continue;
      int ch = 0;
      const Shape s = a2Config(j, &ch);
      if (s == kError) return *err = kErrJson, false;
      if (s == kA2 && ch == 3) {
        net = m;
        channels = 3;
      }
    }
    if (channels != 3) return *err = kErrNoLite, false;
  } else if (!strcmp(top.arch, "WaveNet")) {
    if (!j.seek(top.config)) return *err = kErrRead, false;
    const Shape s = a2Config(j, &channels);
    if (s == kError) return *err = kErrJson, false;
    if (s != kA2) return *err = kErrA1, false;
    if (channels != 3) return *err = kErrNoLite, false;
    net = top;
  } else {
    return *err = kErrA1, false;
  }

  // Pass 2: the network's weights.
  if (!net.hasWeights || !j.seek(net.weights) || !j.expect('[')) return *err = kErrJson, false;
  bool first = true;
  int n = 0;
  while (j.item(&first)) {
    double v;
    if (!j.number(&v)) break;
    if (n < nama2::kWeightCount) weights[n] = (float)v;
    n++;
  }
  if (!j.ok()) return *err = kErrJson, false;
  if (n != nama2::kWeightCount) return *err = kErrWeights, false;

  // The network's own rate and loudness, else the file's.
  if (!isnan(net.sampleRate)) info->sampleRate = net.sampleRate;
  else if (!isnan(top.sampleRate)) info->sampleRate = top.sampleRate;
  info->loudness = !isnan(net.loudness) ? net.loudness : top.loudness;
  *err = "";
  return true;
}

}  // namespace irdsp
