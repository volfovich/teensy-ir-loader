// test_namfile.cpp - reading .nam files (NamFile.h): the real A2 file,
// files made here with their keys in other orders, the files it must refuse
// (A1, LSTM, Full only, the wrong number of weights), and files cut short or
// damaged at random, which must fail cleanly.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "NamA2.h"
#include "NamFile.h"

using namespace irdsp;

static int g_fail = 0;
static void check(bool ok, const char* what, const std::string& detail = "") {
  printf("  %-62s %s %s\n", what, ok ? "OK" : "FAIL", detail.c_str());
  if (!ok) g_fail++;
}

class MemSource : public ByteSource {
 public:
  explicit MemSource(const std::string& s) : s_(s) {}
  int read(void* dst, int n) override {
    const int left = (int)s_.size() - (int)pos_;
    if (n > left) n = left;
    if (n > 0) memcpy(dst, s_.data() + pos_, n), pos_ += n;
    return n > 0 ? n : 0;
  }
  bool seek(uint32_t pos) override {
    if (pos > s_.size()) return false;
    pos_ = pos;
    return true;
  }

 private:
  const std::string& s_;
  uint32_t pos_ = 0;
};

struct Result {
  bool ok;
  std::string err;
  std::vector<float> w;
  NamInfo info;
};

static Result read(const std::string& text) {
  MemSource src(text);
  Result r;
  r.w.assign(nama2::kWeightCount, NAN);
  const char* err = "";
  r.ok = readNamA2Lite(src, r.w.data(), &r.info, &err);
  r.err = err;
  return r;
}

// ---- Model files made here ----------------------------------------------------------

static std::string ints(const int* v, int n) {
  std::string s = "[";
  for (int i = 0; i < n; i++) s += (i ? ", " : "") + std::to_string(v[i]);
  return s + "]";
}

// A WaveNet config in the A2 shape with `channels` channels; `variant`
// changes one thing to make it another network.
static std::string config(int channels, const char* variant = "") {
  std::string act = "[", gate = "[", sec = "[";
  for (int i = 0; i < nama2::kLayers; i++) {
    act += std::string(i ? ", " : "") + "{\"type\": \"LeakyReLU\", \"negative_slope\": 0.01}";
    gate += std::string(i ? ", " : "") + "\"none\"";
    sec += std::string(i ? ", " : "") + "null";
  }
  act += "]", gate += "]", sec += "]";
  if (!strcmp(variant, "tanh")) act = "\"Tanh\"";
  if (!strcmp(variant, "gated")) gate = "\"gated\"";
  std::string kernels = ints(nama2::kKernel, nama2::kLayers);
  if (!strcmp(variant, "kernel")) kernels = "[3, 3, 3]";
  const std::string ch = std::to_string(channels);
  std::string layer = "{\"input_size\": 1, \"condition_size\": 1, \"head\": {\"out_channels\": 1, \"kernel_size\": 16, "
                      "\"bias\": true}, \"channels\": " + ch + ", \"kernel_sizes\": " + kernels +
                      ", \"dilations\": " + ints(nama2::kDilation, nama2::kLayers) + ", \"activation\": " + act +
                      ", \"bottleneck\": " + ch +
                      ", \"head1x1\": {\"active\": false, \"out_channels\": 1, \"groups\": 1}, "
                      "\"layer1x1\": {\"active\": true, \"groups\": 1}, \"groups_input\": 1, \"groups_input_mixin\": 1, "
                      "\"conv_pre_film\": {\"active\": " + std::string(!strcmp(variant, "film") ? "true" : "false") +
                      ", \"shift\": true, \"groups\": 1}, \"gating_mode\": " + gate +
                      ", \"secondary_activation\": " + sec + ", \"slimmable\": null}";
  return "{\"layers\": [" + layer + "], \"head\": null, \"head_scale\": 0.01}";
}

static std::string weights(int n, float base) {
  std::string s = "[";
  char buf[32];
  for (int i = 0; i < n; i++) {
    snprintf(buf, sizeof(buf), "%s%.9g", i ? ", " : "", base + 0.001f * (float)i);
    s += buf;
  }
  return s + "]";
}

static int fullWeights() {  // A2-Full's weight count
  int n = 8;
  for (int l = 0; l < nama2::kLayers; l++) n += nama2::kKernel[l] * 64 + 3 * 8 + 64;
  return n + 16 * 8 + 2;
}

// A plain WaveNet model; configFirst puts the config before the weights.
static std::string wavenet(int channels, int n, float base, bool configFirst, float loudness,
                           const char* variant = "") {
  const std::string meta = "\"metadata\": {\"loudness\": " + std::to_string(loudness) + ", \"gain\": 0.5}";
  const std::string cfg = "\"config\": " + config(channels, variant), w = "\"weights\": " + weights(n, base);
  return "{\"version\": \"0.7.0\", " + meta + ", \"architecture\": \"WaveNet\", " +
         (configFirst ? cfg + ", " + w : w + ", " + cfg) + ", \"sample_rate\": 48000}";
}

static std::string container(const std::vector<std::pair<double, std::string>>& subs, bool weightsFirst) {
  std::string list = "[";
  for (size_t i = 0; i < subs.size(); i++) {
    list += (i ? ", " : "");
    list += "{\"max_value\": " + std::to_string(subs[i].first) + ", \"model\": " + subs[i].second + "}";
  }
  list += "]";
  const std::string meta = "\"metadata\": {\"name\": \"Test \\\"amp\\\"\", \"gear_make\": \"Marshall\", "
                           "\"gear_model\": \"JCM800 2203\", \"gear_type\": \"amp\", \"tone_type\": \"hi_gain\", "
                           "\"loudness\": -26.5, \"input_level_dbu\": 14.76, \"output_level_dbu\": NaN, "
                           "\"date\": {\"year\": 2026}}";
  const std::string w = "\"weights\": []", cfg = "\"config\": {\"submodels\": " + list + "}";
  return "{\"version\": \"0.7.0\", " + (weightsFirst ? w + ", " + meta + ", " + cfg : cfg + ", " + meta + ", " + w) +
         ", \"architecture\": \"SlimmableContainer\", \"sample_rate\": 48000.0}";
}

static bool weightsAre(const Result& r, float base) {
  for (int i = 0; i < nama2::kWeightCount; i++)
    if (fabsf(r.w[i] - (base + 0.001f * (float)i)) > 1e-6f) return false;
  return true;
}

// ---- Tests ------------------------------------------------------------------------

static std::string readFile(const char* path) {
  std::string s;
  FILE* f = fopen(path, "rb");
  if (!f) return s;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
  fclose(f);
  return s;
}

static void testRealFile(const std::string& a2) {
  printf("The A2 example model (nam/A2.nam)\n");
  const Result r = read(a2);
  check(r.ok, "reads its Lite network", r.err);
  bool finite = true;
  for (float v : r.w) finite = finite && std::isfinite(v);
  check(finite, "  ... all 1871 weights set");
  check(r.info.container && fabsf(r.info.loudness - -24.038164f) < 1e-4f, "  ... the Lite network's own loudness",
        std::to_string(r.info.loudness));
  check(fabsf(r.info.inputLevelDbu - 14.76f) < 1e-4f && isnan(r.info.outputLevelDbu) && r.info.sampleRate == 48000,
        "  ... input level 14.76 dBu, no output level, 48 kHz");
}

static void testMadeFiles() {
  printf("Files made here\n");
  const int lite = nama2::kWeightCount, full = fullWeights();
  for (int order = 0; order < 4; order++) {
    const bool liteFirst = order & 1, cfgFirst = order & 2;
    std::vector<std::pair<double, std::string>> subs = {{0.5, wavenet(3, lite, 0.25f, cfgFirst, -20.0f)},
                                                        {1.0, wavenet(8, full, -1.0f, !cfgFirst, -22.0f)}};
    if (!liteFirst) std::swap(subs[0], subs[1]);
    const Result r = read(container(subs, order == 1));
    char what[80];
    snprintf(what, sizeof(what), "container, Lite %s, config %s the weights", liteFirst ? "first" : "second",
             cfgFirst ? "before" : "after");
    check(r.ok && weightsAre(r, 0.25f) && r.info.loudness == -20.0f, what, r.err);
  }
  {
    const Result r = read(container({{0.5, wavenet(3, lite, 0.25f, true, -20.0f)}}, false));
    check(!strcmp(r.info.name, "Test \"amp\"") && !strcmp(r.info.gearMake, "Marshall") &&
              !strcmp(r.info.gearModel, "JCM800 2203") && !strcmp(r.info.gearType, "amp") &&
              !strcmp(r.info.toneType, "hi_gain") && r.info.inputLevelDbu == 14.76f && isnan(r.info.outputLevelDbu),
          "metadata: names (with escapes), levels, NaN", r.info.name);
  }
  {
    // Some trainers write their own name as the tone type: 16 letters, kept whole.
    std::string text = container({{0.5, wavenet(3, lite, 0.25f, true, -20.0f)}}, false);
    text.replace(text.find("hi_gain"), 7, "neuralampmodeler");
    const Result r = read(text);
    check(r.ok && !strcmp(r.info.toneType, "neuralampmodeler"), "metadata: a 16-letter tone type", r.info.toneType);
  }
  {
    const Result r = read(wavenet(3, lite, 0.5f, false, -18.0f));
    check(r.ok && weightsAre(r, 0.5f) && !r.info.container && r.info.loudness == -18.0f, "a plain Lite WaveNet",
          r.err);
  }
  struct Bad {
    std::string text;
    const char* want;
    const char* what;
  };
  const Bad bad[] = {
      {wavenet(16, 13802, 0.1f, true, -18.0f, "tanh"), "not an A2 model", "an A1 WaveNet (tanh)"},
      {wavenet(3, lite, 0.1f, true, -18.0f, "gated"), "not an A2 model", "gated layers"},
      {wavenet(3, lite, 0.1f, true, -18.0f, "kernel"), "not an A2 model", "other kernel sizes"},
      {wavenet(3, lite, 0.1f, true, -18.0f, "film"), "not an A2 model", "FiLM active"},
      {"{\"architecture\": \"LSTM\", \"config\": {\"num_layers\": 1}, \"weights\": [1, 2]}", "not an A2 model",
       "an LSTM"},
      {wavenet(8, full, 0.1f, true, -18.0f), "no A2-Lite", "a plain A2-Full WaveNet"},
      {container({{1.0, wavenet(8, full, 0.1f, true, -18.0f)}}, false), "no A2-Lite", "a container with Full only"},
      {wavenet(3, lite - 1, 0.1f, true, -18.0f), "wrong number of weights", "one weight short"},
      {wavenet(3, lite + 1, 0.1f, true, -18.0f), "wrong number of weights", "one weight too many"},
      {"not json at all", "damaged", "not JSON"},
      {"", "damaged", "an empty file"},
  };
  for (const Bad& b : bad) {
    const Result r = read(b.text);
    check(!r.ok && r.err.find(b.want) != std::string::npos, b.what, "(" + r.err + ")");
  }
}

static void testDamage(const std::string& a2) {
  printf("Cut short and damaged\n");
  // Cut at 400 points across the file: always an error.
  bool allFail = true;
  for (int i = 0; i < 400; i++) {
    const std::string cut = a2.substr(0, a2.size() * i / 400);
    const Result r = read(cut);
    allFail = allFail && !r.ok && !r.err.empty();
  }
  check(allFail, "cut anywhere: refused with a message");
  // 300 copies with 20 random bytes changed: never a crash or an overrun (run
  // under -fsanitize=address to check the latter).
  uint32_t rng = 12345;
  int ok = 0;
  for (int i = 0; i < 300; i++) {
    std::string d = a2;
    for (int k = 0; k < 20; k++) {
      rng = rng * 1664525u + 1013904223u;
      const size_t at = (rng >> 4) % d.size();
      rng = rng * 1664525u + 1013904223u;
      d[at] = (char)(rng >> 24);
    }
    ok += read(d).ok;
  }
  check(true, "300 randomly damaged files: no crash", std::to_string(ok) + " still read");
}

int main() {
  printf("test_namfile\n");
  const std::string a2 = readFile("nam/A2.nam");
  check(!a2.empty(), "nam/A2.nam is there (run from test/host)");
  if (!a2.empty()) testRealFile(a2);
  testMadeFiles();
  if (!a2.empty()) testDamage(a2);
  printf(g_fail ? "test_namfile: %d FAILED\n" : "test_namfile: all passed\n", g_fail);
  return g_fail ? 1 : 0;
}
