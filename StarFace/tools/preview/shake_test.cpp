// Feeds simulated accelerometer/gyro data through the sketch's real shake
// detector (StarFace/ShakeDetector.h) on the fast sensor task, with events
// delivered at jittery render rates; checks shakes trigger and walking does not.
//   g++ -std=gnu++17 -I../../StarFace shake_test.cpp -o shake_test && ./shake_test
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <initializer_list>
#include "ShakeDetector.h"

static uint32_t rng = 12345;
static float frand() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; }

struct Motion {            // acceleration (m/s^2, incl. gravity) and spin (rad/s) at time t
  virtual void at(float t, float a[3], float g[3]) = 0;
};
struct Shake : Motion {    // back-and-forth along a direction, from t = 0.3 s (optionally with a pause)
  float amp, freq, spinAmp, stopAt, pauseAt, pauseLen;
  Shake(float A, float f, float s = 0, float stop = 1e9f, float pAt = 1e9f, float pLen = 0)
      : amp(A), freq(f), spinAmp(s), stopAt(stop), pauseAt(pAt), pauseLen(pLen) {}
  void at(float t, float a[3], float g[3]) override {
    bool paused = t > pauseAt && t < pauseAt + pauseLen;
    float on = t > .3f && t < stopAt && !paused ? 1.0f : 0.0f, w = 6.2831853f * freq * t;
    a[0] = 1.5f + on * amp * sinf(w) * .9f;
    a[1] = 2.0f + on * amp * sinf(w) * .4f;
    a[2] = 9.4f + on * amp * .15f * sinf(2 * w) + (frand() - .5f) * .6f;
    g[0] = on * spinAmp * cosf(w); g[1] = g[2] = 0;
  }
};
struct Walk : Motion {     // steps: vertical bounce plus sway, with noise
  float bounce, rate;
  Walk(float b, float r) : bounce(b), rate(r) {}
  void at(float t, float a[3], float g[3]) override {
    float w = 6.2831853f * rate * t;
    a[0] = 1.0f + 1.6f * sinf(w * .5f) + (frand() - .5f);
    a[1] = .5f + .8f * sinf(w + 1) + (frand() - .5f);
    a[2] = 9.81f + bounce * fmaxf(0.0f, sinf(w)) * 1.6f - bounce * .5f + (frand() - .5f);
    g[0] = .6f * sinf(w * .5f); g[1] = .4f * sinf(w); g[2] = 0;
  }
};
struct Knock : Motion {    // one sharp hit at t = 1 s with a short ring-down
  void at(float t, float a[3], float g[3]) override {
    float d = t - 1.0f, k = d > 0 && d < .25f ? 22.0f * expf(-d / .03f) * cosf(6.2831853f * 25 * d) : 0;
    a[0] = 1.0f + k; a[1] = .5f; a[2] = 9.7f + (frand() - .5f) * .4f;
    g[0] = g[1] = g[2] = 0;
  }
};

// Mirrors motionSamplerTask(): recognize at 3 ms, report on the next render frame.
// Returns ms until DIZZY (or -1) and counts strokes.
struct Reactions { int strokes = 0, startles = 0, bumps = 0; float maxRattle = 0; };
static Reactions seen;
static int runAwake(Motion &m, int periodMs, float seconds, int *strokesSeen) {
  seen = Reactions();
  ShakeDetector det;
  TwistDetector twist; // the sketch runs both; either one makes it dizzy
  float grav[3] = {1.5f, 2.0f, 9.4f};
  *strokesSeen = 0;
  float lastMs = 0, nextFrameAt = 0;
  bool pendingDizzy = false;
  for (float tms = 0; tms < seconds * 1000; tms += 3) {
    float a[3], g[3];
    m.at(tms / 1000, a, g);
    float lin[3];
    float dt = tms > 0 ? (tms - lastMs) / 1000.0f : .025f;
    lastMs = tms;
    float k = 1.0f - expf(-dt / .25f);
    for (int i = 0; i < 3; ++i) { grav[i] += (a[i] - grav[i]) * k; lin[i] = a[i] - grav[i]; }
    float spin = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    ShakeDetector::Event ev = det.feed(lin, spin, uint32_t(tms) + 1);
    if (ev == ShakeDetector::STROKE || ev == ShakeDetector::STARTLE) ++*strokesSeen;
    if (ev == ShakeDetector::STARTLE) ++seen.startles;
    if (ev == ShakeDetector::BUMP) ++seen.bumps;
    seen.strokes = *strokesSeen;
    if (det.rattle() > seen.maxRattle) seen.maxRattle = det.rattle();
    bool twisted = twist.feed(g, uint32_t(tms) + 1) && TWIST_MAKES_DIZZY;
    pendingDizzy |= ev == ShakeDetector::DIZZY || twisted;
    if (tms >= nextFrameAt) {
      if (pendingDizzy) return int(tms);
      nextFrameAt = tms + periodMs * (.7f + .6f * frand());
    }
  }
  return -1;
}

// Mirrors confirmMotionWake(): samples every ~12 ms after boot until a steady
// ~1.5 s shake is confirmed, or gives up when no shake starts or it stops.
static int runWake(Motion &m, float bootDelay) {
  ShakeWakeCheck chk;
  for (float tms = 0; tms < SHAKE_WAKE_HOLD_MS + 3000; tms += 12) {
    float a[3], g[3];
    uint32_t now = uint32_t(tms) + 1;
    m.at(bootDelay + tms / 1000, a, g);
    if (chk.feed(a, g, now)) return int(tms);
    if (!chk.shakingFor(now) && (tms > 1200 || chk.stopped(now))) break;
  }
  return -1;
}

int main() {
  int fails = 0;
  printf("Awake: ms until DIZZY for a shake kept up (starting at 0.3 s; amplitude m/s^2 x frequency), by loop sample period\n");
  printf("  amp  freq |  25ms  45ms  70ms\n");
  const float amps[] = {6, 9, 12, 18, 30}, freqs[] = {2.5f, 4, 6};
  for (float A : amps) for (float f : freqs) {
    printf("  %4.0f %4.1f |", A, f);
    for (int p : {25, 45, 70}) {
      int s; int ms = runAwake(*new Shake(A, f), p, 7.0f, &s);
      printf(" %5d", ms);
      // Moderate shaking (>= ~0.9 g) now qualifies, after about 1 s of
      // shaking (it starts 0.3 s in), never sooner; a light one (0.6 g) never.
      if (A >= 9 && (ms < 1300 || ms > 2300)) { ++fails; printf("!"); }
      if (ms >= 0 && ms < 1300) { ++fails; printf("!"); }
      if (A <= 6 && ms >= 0) { ++fails; printf("!"); }
    }
    printf("\n");
  }
  int s, ms;
  for (int p : {25, 45, 70}) {
    Shake uneven(12, 3, 0, 3.5f, .9f, .35f);
    ms = runAwake(uneven, p, 4.0f, &s);
    printf("Moderate shake with a 350 ms pause, %d ms loop: %d ms\n", p, ms);
    if (ms < 0 || ms > 2300) ++fails;
  }
  for (int p : {25, 45, 70}) {
    ms = runAwake(*new Shake(4, 4, 6.0f), p, 3.0f, &s);
    printf("Turning it back and forth (mostly rotation, 6 rad/s), %d ms loop: %s\n", p,
           ms < 0 ? "not dizzy" : "DIZZY  <-- WRONG (rotation alone must not)");
    if (ms >= 0) ++fails;
  }
  printf("Shakes that stop too soon must not make it dizzy:\n");
  {
    struct { const char *name; Motion *m; } brief[] = {
      {"hard shake for 0.4 s", new Shake(18, 4, 0, .7f)},
      {"hard shake for 0.6 s", new Shake(18, 4, 0, .9f)},
      {"0.4 s, 0.8 s pause, 0.4 s", new Shake(18, 4, 0, 1.9f, .7f, .8f)},
    };
    for (auto &b : brief) {
      for (int p : {25, 45, 70}) {
        int st; int got = runAwake(*b.m, p, 6.0f, &st);
        printf("  %-26s %d ms loop: %s\n", b.name, p, got < 0 ? "not dizzy" : "DIZZY  <-- WRONG");
        if (got >= 0) ++fails;
      }
    }
  }
  struct { const char *name; Motion *m; } calm[] = {
    {"walking", new Walk(3.0f, 1.8f)}, {"brisk walking", new Walk(4.5f, 2.2f)},
    {"running", new Walk(8.0f, 2.8f)}, {"single knock", new Knock()},
  };
  printf("Carried around for 20 s (must never startle or go dizzy; a knock at rest should register):\n");
  for (auto &c : calm) {
    ms = runAwake(*c.m, 40, 20.0f, &s);
    bool knock = c.name[0] == 's';
    // (A ~30 ms knock may fall between samples; it must just never startle.)
    bool bad = ms >= 0 || seen.startles > 0 || (knock ? seen.bumps > 1 : seen.bumps > 0);
    printf("  %-13s: dizzy %-5s startled %d  knock-reaction %d  eye sloshes %d  max rattle %.2f%s\n",
           c.name, ms < 0 ? "never" : "YES", seen.startles, seen.bumps, s, seen.maxRattle,
           bad ? "  <-- WRONG" : "");
    if (bad) ++fails;
  }

  {
    // A knock that is sampled, on a star that has been resting: BUMP. The same
    // jolt right after being carried (busy) must not count as a knock.
    ShakeDetector det;
    float still[3] = {0, 0, 0}, hit[3] = {14, 3, 0};
    for (uint32_t t = 1; t < 3000; t += 40) det.feed(still, 0, t);
    bool resting = det.feed(hit, 0, 3001) == ShakeDetector::BUMP;
    ShakeDetector busy;
    float step[3] = {0, 0, 6};
    for (uint32_t t = 1; t < 3000; t += 40) busy.feed(step, 0, t);
    bool carried = busy.feed(hit, 0, 3001) == ShakeDetector::BUMP;
    printf("Knock on a resting star: %s; same knock while carried: %s\n",
           resting ? "reacts" : "MISSED", carried ? "REACTS (wrong)" : "ignored");
    if (!resting || carried) ++fails;
  }
  printf("\nLegacy wake checker needs a steady ~1.5 s shake (ms after boot; boot ends 0.4 s into the shake)\n");
  for (float A : amps) for (float f : {3.0f, 5.0f}) {
    ms = runWake(*new Shake(A, f), .7f);
    printf("  steady shake %4.0f m/s^2 at %.0f Hz: %s", A, f, ms < 0 ? "stays asleep" : "");
    if (ms >= 0) printf("on after %d ms", ms);
    bool bad = (A >= 12 && ms < 0) || (ms >= 0 && ms < int(SHAKE_WAKE_HOLD_MS));
    if (bad) { ++fails; printf("  <-- WRONG"); }
    printf("\n");
  }
  struct { const char *name; Motion *m; } brief[] = {
    {"hard 1 s shake", new Shake(18, 4, 0, 1.3f)},
    {"hard 1.2 s shake", new Shake(18, 4, 0, 1.5f)},
    {"1 s + pause + 1 s", new Shake(18, 4, 0, 3.1f, 1.3f, .8f)},
  };
  for (auto &b : brief) {
    ms = runWake(*b.m, .7f);
    printf("  %-30s: %s%s\n", b.name, ms < 0 ? "stays asleep" : "turns on",
           ms >= 0 ? "  <-- TOO EASY" : "");
    if (ms >= 0) ++fails;
  }
  for (auto &c : calm) {
    ms = runWake(*c.m, c.name[0] == 's' ? .95f : 1.0f);
    bool bad = ms >= 0;
    printf("  %-13s: %s%s\n", c.name, ms < 0 ? "stays asleep" : "turns on",
           bad ? "  <-- FALSE WAKE" : "");
    if (bad) ++fails;
  }
  {
    struct GyroOnly : Motion {
      void at(float t, float a[3], float g[3]) override {
        a[0] = a[1] = 0; a[2] = 9.81f;
        g[0] = g[1] = 0; g[2] = 8 * sinf(6.2831853f * 3 * t);
      }
    } spin;
    int got = runWake(spin, 0);
    printf("Gyro-only swinging: %s\n", got < 0 ? "stays asleep" : "FALSE WAKE");
    if (got >= 0) ++fails;
  }
  // Check the whole carried-motion trace, not just a short boot probe.
  for (auto &c : calm) {
    ShakeWakeCheck check;
    for (uint32_t t = 1; t < 20000; t += 12) {
      float a[3], g[3]; c.m->at(t / 1000.0f, a, g);
      if (check.feed(a, g, t)) { ++fails; printf("FALSE carried wake: %s\n", c.name); break; }
    }
  }
  printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
