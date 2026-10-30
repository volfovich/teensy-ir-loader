// Tests for PluckBass: tuning, stability, muting, velocity, note changes.
#include <cmath>
#include <cstdio>
#include <vector>

#include "PluckBass.h"

static const float kFs = 48000.0f;
static const size_t kS = 48000;  // samples per second
static const int kB = 32;
static int failures = 0;

static void check(bool ok, const char* what, double value) {
  std::printf("  %-58s %10.3f  %s\n", what, value, ok ? "OK" : "FAIL");
  if (!ok) failures++;
}

// Renders seconds of the bass (silence where it is idle).
static std::vector<float> render(irdsp::PluckBass& b, float seconds) {
  std::vector<float> out((size_t)(seconds * kFs / kB) * kB, 0.0f);
  for (size_t i = 0; i < out.size(); i += kB) b.process(&out[i], kB);
  return out;
}

static float peak(const std::vector<float>& x, size_t a, size_t b) {
  float p = 0.0f;
  for (size_t i = a; i < b && i < x.size(); i++) p = std::fmax(p, std::fabs(x[i]));
  return p;
}

static float rms(const std::vector<float>& x, size_t a, size_t b) {
  double s = 0.0;
  for (size_t i = a; i < b; i++) s += (double)x[i] * x[i];
  return (float)std::sqrt(s / (double)(b - a));
}

// The fundamental near hz: the peak of the normalized autocorrelation (so
// the note's decay over one lag doesn't favour shorter periods) around the
// expected period, refined by a parabola.
static double pitchHz(const std::vector<float>& x, size_t from, size_t len, double hz) {
  const int p0 = (int)(kFs / hz);
  const int lo = (int)(p0 * 0.9), hi = (int)(p0 * 1.1) + 2;
  std::vector<double> ac(hi + 2, 0.0);
  for (int lag = lo - 1; lag <= hi + 1; lag++) {
    double s = 0.0, e0 = 0.0, e1 = 0.0;
    for (size_t i = from; i < from + len; i++) {
      s += (double)x[i] * x[i + lag];
      e0 += (double)x[i] * x[i];
      e1 += (double)x[i + lag] * x[i + lag];
    }
    ac[lag] = s / std::sqrt(e0 * e1 + 1e-30);
  }
  int best = lo;
  for (int lag = lo; lag <= hi; lag++)
    if (ac[lag] > ac[best]) best = lag;
  const double a = ac[best - 1], b = ac[best], c = ac[best + 1];
  const double shift = 0.5 * (a - c) / (a - 2 * b + c);
  return kFs / (best + shift);
}

int main() {
  std::vector<float> buf(2 * irdsp::PluckBass::kLineLen);
  irdsp::PluckBass b;

  std::printf("Tuning (cents off)\n");
  for (int note : {23, 28, 33, 40, 47, 55}) {
    b.init(kFs, buf.data());
    b.noteOn(note, 7);
    const std::vector<float> x = render(b, 1.5f);
    const double want = irdsp::PluckBass::noteHz(note);
    const double got = pitchHz(x, (size_t)(0.3f * kFs), (size_t)(0.6f * kFs), want);
    const double cents = 1200.0 * std::log2(got / want);
    char what[64];
    std::snprintf(what, sizeof(what), "note %d (%.1f Hz) within 3 cents", note, want);
    check(std::fabs(cents) < 3.0, what, cents);
  }

  std::printf("Stability\n");
  bool finite = true, bounded = true, decays = true;
  for (float tone : {0.0f, 0.5f, 1.0f}) {
    for (int note = irdsp::PluckBass::kMinNote; note <= irdsp::PluckBass::kMaxNote; note++) {
      b.init(kFs, buf.data());
      b.setTone(tone);
      b.noteOn(note, 9);
      const std::vector<float> x = render(b, 8.0f);
      for (float v : x) finite = finite && std::isfinite(v);
      bounded = bounded && peak(x, 0, x.size()) < 0.9f;
      decays = decays && peak(x, x.size() - kS / 10, x.size()) < 0.5f * peak(x, 0, kS / 10);
    }
  }
  check(finite, "every note and tone: finite", 0);
  check(bounded, "  ... peaks below 0.9 at velocity 9", 0);
  check(decays, "  ... and dies away", 0);

  std::printf("Sustain and mute\n");
  b.init(kFs, buf.data());
  b.noteOn(28, 7);
  std::vector<float> x = render(b, 2.2f);
  const double sustain = 20.0 * std::log10(rms(x, 2 * kS, 21 * kS / 10) / rms(x, kS / 10, kS / 5));
  check(sustain > -50.0 && sustain < -25.0, "E1 falls 25..50 dB in 2 s (3 s to -60 dB)", sustain);
  const float before = rms(x, x.size() - kS / 100, x.size());
  b.mute();
  x = render(b, 0.12f);
  const double muted = 20.0 * std::log10(rms(x, kS / 10, 11 * kS / 100) / before + 1e-12);
  check(muted < -60.0, "mute: below -60 dB after 100 ms", muted);
  x = render(b, 1.0f);
  check(!b.active(), "  ... then idle (process costs nothing)", b.active());
  const float pk = peak(x, x.size() / 2, x.size());
  check(pk == 0.0f, "  ... and silent from then on", pk);

  b.init(kFs, buf.data());
  b.noteOn(33, 7, true);
  x = render(b, 0.3f);
  const double dead = 20.0 * std::log10(rms(x, kS / 5, 21 * kS / 100) / rms(x, 0, kS / 100));
  check(dead < -40.0, "dead note: down 40 dB after 200 ms", dead);

  std::printf("Velocity\n");
  float pv[10] = {};
  for (int v : {2, 5, 9}) {
    b.init(kFs, buf.data());
    b.noteOn(33, v);
    pv[v] = peak(render(b, 0.2f), 0, kS / 5);
  }
  check(pv[2] < pv[5] && pv[5] < pv[9], "louder with velocity (2 < 5 < 9)", pv[9] / pv[2]);
  check(pv[9] > 0.2f && pv[9] < 0.8f, "velocity 9 A1 peak", pv[9]);

  std::printf("Note changes\n");
  // The largest sample-to-sample step right after a new note is no bigger
  // than a fresh pluck's own: the old note is muted, not cut.
  b.init(kFs, buf.data());
  b.noteOn(40, 7);
  std::vector<float> fresh = render(b, 0.01f);
  float freshStep = 0.0f;
  for (size_t i = 1; i < fresh.size(); i++) freshStep = std::fmax(freshStep, std::fabs(fresh[i] - fresh[i - 1]));
  b.init(kFs, buf.data());
  b.noteOn(28, 9);
  render(b, 0.5f);
  b.noteOn(40, 7);
  std::vector<float> change = render(b, 0.01f);
  float changeStep = 0.0f;
  for (size_t i = 1; i < change.size(); i++) changeStep = std::fmax(changeStep, std::fabs(change[i] - change[i - 1]));
  check(changeStep < 1.5f * freshStep, "no click: E1 -> E2 step vs a fresh E2 pluck", changeStep / freshStep);
  x = render(b, 0.5f);
  const double got = pitchHz(x, kS / 10, (size_t)(0.3f * kFs), irdsp::PluckBass::noteHz(40));
  check(std::fabs(1200.0 * std::log2(got / irdsp::PluckBass::noteHz(40))) < 3.0, "  ... the new note in tune", got);

  std::printf("Clicks\n");
  // A click is a jump in the slope. Damping through the loop gain (once per
  // period) dropped a low note by ~90 % in one sample; the envelope keeps
  // every second difference after a mute or note change below 1 % (-40 dB)
  // of the note's level.
  auto curve = [](const std::vector<float>& v, size_t a, size_t e) {
    float m = 0.0f;
    for (size_t i = a + 2; i < e && i < v.size(); i++) m = std::fmax(m, std::fabs(v[i] - 2 * v[i - 1] + v[i - 2]));
    return m;
  };
  for (int note : {23, 28, 33}) {
    b.init(kFs, buf.data());
    b.noteOn(note, 7);
    std::vector<float> s1 = render(b, 1.0f);
    const float level = peak(s1, s1.size() - kS / 10, s1.size());
    b.mute();
    std::vector<float> s2 = render(b, 0.3f);
    char what[64];
    std::snprintf(what, sizeof(what), "note %d: mute, largest slope jump / level", note);
    check(curve(s2, 0, s2.size()) < 0.01f * level, what, curve(s2, 0, s2.size()) / level);
    // A new note while one rings: the old one fades. The jump is no bigger
    // than the new pluck's alone.
    b.noteOn(note, 7);
    render(b, 1.0f);
    b.noteOn(note + 5, 5, true);
    std::vector<float> s3 = render(b, 0.05f);
    b.init(kFs, buf.data());
    b.noteOn(note + 5, 5, true);
    std::vector<float> alone = render(b, 0.05f);
    std::snprintf(what, sizeof(what), "note %d: change to a dead note vs its pluck alone", note);
    check(curve(s3, 0, s3.size()) < 1.5f * curve(alone, 0, alone.size()), what,
          curve(s3, 0, s3.size()) / curve(alone, 0, alone.size()));
  }

  if (failures) std::printf("%d FAILURE(S)\n", failures);
  else std::printf("ALL TESTS PASSED\n");
  return failures ? 1 : 0;
}
