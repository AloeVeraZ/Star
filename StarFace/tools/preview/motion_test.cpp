// Feeds simulated motion through the sketch's touch-free gestures
// (StarFace/MotionGestures.h) and its twist detector, and checks that each
// gesture is recognised and that carrying, walking, running and shaking it
// never trigger one by mistake.
//   g++ -std=gnu++17 -I../../StarFace motion_test.cpp -o motion_test && ./motion_test
#include <math.h>
#include <stdio.h>
#include <vector>
#include "MotionGestures.h"
#include "ShakeDetector.h"

static uint32_t rng = 777;
static float frand() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; }
static const float PI2 = 6.2831853f;

// ---- Motion models: acceleration incl. gravity (m/s^2) and spin (rad/s) ----
struct Motion { virtual void at(float t, float a[3], float g[3]) = 0; virtual ~Motion() {} };

// Lying still (flat, or held upright), plus sensor/hand noise.
static void rest(bool upright, float a[3], float g[3]) {
  a[0] = (frand() - .5f) * .15f;
  a[1] = upright ? 9.81f : (frand() - .5f) * .15f;
  a[2] = upright ? (frand() - .5f) * .15f : 9.81f;
  if (upright) a[1] += (frand() - .5f) * .15f; else a[2] += (frand() - .5f) * .15f;
  g[0] = (frand() - .5f) * .05f; g[1] = (frand() - .5f) * .05f; g[2] = 0;
}

struct Knocks : Motion {   // knocks at the given times on a still star
  // A knuckle knock: a short contact pulse (peak `amp` m/s^2 for `contactS`)
  // shoves the star, the holding hand catches it again over ~40 ms, and the
  // case rings briefly.
  std::vector<float> at_s;
  float amp, contactS;
  bool upright;
  Knocks(std::vector<float> t, float A, float contact = .002f, bool up = false)
      : at_s(t), amp(A), contactS(contact), upright(up) {}
  void at(float t, float a[3], float g[3]) override {
    rest(upright, a, g);
    for (float k : at_s) {
      float d = t - k;
      if (d < 0 || d > .25f) continue;
      float push = d < contactS ? amp * sinf(3.14159265f * d / contactS) : 0;
      float dv = amp * contactS * .6366f;                     // velocity the push gave it
      float c = d - contactS;
      float katch = c > 0 ? -dv / .02f * (c / .02f) * expf(1 - c / .02f) * .37f : 0; // the hand catches it
      float ring = c > 0 ? .15f * amp * expf(-c / .005f) * sinf(PI2 * 180 * c) : 0;
      a[0] += push + katch + ring;
      g[2] += .3f * expf(-d / .02f);
    }
  }
};
struct Walk : Motion {     // steps: vertical bounce plus sway, with noise
  float bounce, rate;
  Walk(float b, float r) : bounce(b), rate(r) {}
  void at(float t, float a[3], float g[3]) override {
    float w = PI2 * rate * t;
    a[0] = 1.0f + 1.6f * sinf(w * .5f) + (frand() - .5f);
    a[1] = .5f + .8f * sinf(w + 1) + (frand() - .5f);
    // Heel strikes: a sharp-ish jolt at each step.
    float ph = fmodf(rate * t, 1.0f);
    a[2] = 9.81f + bounce * fmaxf(0.0f, sinf(w)) * 1.6f - bounce * .5f + (frand() - .5f)
           + (ph < .03f ? bounce * 1.5f * (1 - ph / .03f) : 0);
    g[0] = .6f * sinf(w * .5f); g[1] = .4f * sinf(w); g[2] = .2f * sinf(w * .7f);
  }
};
struct Shake : Motion {    // back and forth, from t = .5 s
  float amp, freq;
  Shake(float A, float f) : amp(A), freq(f) {}
  void at(float t, float a[3], float g[3]) override {
    rest(false, a, g);
    if (t < .5f) return;
    float w = PI2 * freq * (t - .5f);
    a[0] += amp * sinf(w);
    a[1] += amp * .3f * sinf(w);
  }
};
// Turning about board axis `ax` (0 = x: tips the top toward/away, a[1] changes;
// 1 = y: tips left/right, a[0] changes) by angle(t), from flat.
struct Turn : Motion {
  int ax;
  float (*angle)(float);
  Turn(int axis, float (*f)(float)) : ax(axis), angle(f) {}
  void at(float t, float a[3], float g[3]) override {
    rest(false, a, g);
    float th = angle(t), dt = .001f, rate = (angle(t + dt) - angle(t - dt)) / (2 * dt);
    if (ax == 1) { a[0] = 9.81f * sinf(th); a[2] = 9.81f * cosf(th); g[1] = rate; }
    else { a[1] = 9.81f * sinf(th); a[2] = 9.81f * cosf(th); g[0] = rate; }
    for (int i = 0; i < 3; ++i) a[i] += (frand() - .5f) * .15f;
  }
};
// Angle profiles (radians).
static float bump(float t, float start, float len, float amp) {
  if (t < start || t > start + len) return 0;
  return amp * .5f * (1 - cosf(PI2 * (t - start) / len));
}
static float flickPos(float t) { return bump(t, 1.0f, .40f, .60f); }   // ~35 deg out and back
static float flickNeg(float t) { return -bump(t, 1.0f, .40f, .60f); }
static float holdTilt(float t) { return t < 1 ? 0 : fminf(.6f, (t - 1) * 3.0f); } // tip and keep it there
static float slowTilt(float t) { return bump(t, 1.0f, 3.0f, .6f); }            // slow, gentle
static float rock(float t) { return t < .5f ? 0 : .26f * sinf(PI2 * .7f * (t - .5f)); } // 15 deg, 0.7 Hz
static float twist(float t) { return t < .5f ? 0 : .45f * sinf(PI2 * 2.6f * (t - .5f)); } // quick twists

// The sensor as the sketch sets it up: motion filtered by its ~54 Hz low-pass
// (two one-pole stages here, run at 10 kHz like the chip's fast internal
// sampling), output at 1 kHz, and read by the sampler core every 2-4 ms.
static std::vector<KnockDetector::Event> runKnocks(Motion &m, float seconds) {
  KnockDetector k;
  std::vector<KnockDetector::Event> out;
  float f1[3] = {0, 0, 0}, f2[3] = {0, 0, 0}, g[3] = {0, 0, 0}, reg[3] = {0, 0, 0};
  const float alpha = 1.0f - expf(-PI2 * 85.0f * .0001f); // two stages: ~54 Hz overall
  bool primed = false;
  float nextRead = 0;
  for (int step = 0; step < int(seconds * 10000); ++step) {
    float a[3];
    m.at(step / 10000.0f, a, g);
    for (int i = 0; i < 3; ++i) {
      if (!primed) f1[i] = f2[i] = a[i];
      f1[i] += (a[i] - f1[i]) * alpha;
      f2[i] += (f1[i] - f2[i]) * alpha;
    }
    primed = true;
    if (step % 10) continue;                    // a new 1 kHz output sample
    for (int i = 0; i < 3; ++i) reg[i] = f2[i];
    float ms = step / 10.0f;
    if (ms < nextRead) continue;
    nextRead = ms + 2.0f + 2.0f * frand();
    KnockDetector::Event e = k.feed(reg, g, uint32_t(ms) + 1);
    if (e.count) out.push_back(e);
  }
  return out;
}

struct LoopResult { int flicks = 0; TiltFlickDetector::Dir dir = TiltFlickDetector::NONE; int rocks = 0; bool dizzy = false; };
// The render loop's rate (25-70 ms), mirroring handleSurroundings()/handleMotion().
static LoopResult runLoop(Motion &m, float seconds, int periodMs) {
  LoopResult r;
  TiltFlickDetector flick;
  RockDetector rocker;
  ShakeDetector shaker;
  TwistDetector tw;
  float grav[3] = {0, 0, 9.81f};
  for (float tms = 0; tms < seconds * 1000; tms += periodMs * (.7f + .6f * frand())) {
    float a[3], g[3], lin[3];
    m.at(tms / 1000, a, g);
    uint32_t now = uint32_t(tms) + 1;
    for (int i = 0; i < 3; ++i) { grav[i] += (a[i] - grav[i]) * .08f; lin[i] = a[i] - grav[i]; }
    shaker.feed(lin, sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]), now);
    TiltFlickDetector::Dir d = flick.feed(a, g, shaker.strength, now);
    if (d) { ++r.flicks; r.dir = d; }
    if (rocker.feed(a, g, now)) ++r.rocks;
    if (tw.feed(g, now)) r.dizzy = true;
  }
  return r;
}

int main() {
  int fails = 0;
  auto check = [&](bool ok, const char *what, const char *got) {
    printf("  %-58s %s%s\n", what, got, ok ? "" : "   <-- WRONG");
    if (!ok) ++fails;
  };
  char buf[96];

  printf("Knocks (1 kHz sensor, ~54 Hz filter, read every 2-4 ms):\n");
  // Light (~2 g), firm and hard knuckle knocks, with short and longer contact.
  // (A 1 ms, 2 g fingernail flick needs KNOCK_MS2 = 2.5; the default leaves it out.)
  for (float A : {20.0f, 40.0f, 80.0f}) {
    for (float c : {.001f, .002f, .004f}) {
      if (A == 20.0f && c == .001f) continue;
      auto e = runKnocks(*new Knocks({1.0f}, A, c), 2.0f);
      snprintf(buf, sizeof buf, "%zu knock(s)", e.size());
      char what[64];
      snprintf(what, sizeof what, "single knock %.0f m/s^2 for %.0f ms", A, c * 1000);
      check(e.size() == 1 && e[0].count == 1, what, buf);
    }
  }
  {
    auto e = runKnocks(*new Knocks({1.0f}, 30, .002f, true), 2.0f);
    snprintf(buf, sizeof buf, "%zu knock(s)", e.size());
    check(e.size() == 1, "single knock while held upright", buf);
  }
  {
    auto e = runKnocks(*new Knocks({1.0f, 1.3f}, 30), 2.5f);
    snprintf(buf, sizeof buf, "counts %d,%d", e.size() > 0 ? e[0].count : 0, e.size() > 1 ? e[1].count : 0);
    check(e.size() == 2 && e[1].count == 2, "double knock (300 ms apart)", buf);
  }
  {
    auto e = runKnocks(*new Knocks({1.0f, 1.25f, 1.5f, 1.75f}, 30), 3.0f);
    snprintf(buf, sizeof buf, "%zu knocks, last count %d", e.size(), e.empty() ? 0 : e.back().count);
    check(e.size() == 4 && e.back().count == 4, "four quick knocks", buf);
  }
  {
    auto e = runKnocks(*new Knocks({1.0f, 2.5f}, 30), 3.0f);
    snprintf(buf, sizeof buf, "counts %d,%d", e.size() > 0 ? e[0].count : 0, e.size() > 1 ? e[1].count : 0);
    check(e.size() == 2 && e[1].count == 1, "two knocks 1.5 s apart = two single knocks", buf);
  }
  {
    auto e = runKnocks(*new Knocks({1.0f}, 6.0f, .004f), 2.0f);
    snprintf(buf, sizeof buf, "%zu knock(s)", e.size());
    check(e.empty(), "a brush of the hand (0.6 g, 4 ms) is not a knock", buf);
  }
  struct { const char *name; Motion *m; } busy[] = {
    {"walking 20 s", new Walk(3.0f, 1.8f)}, {"brisk walking 20 s", new Walk(4.5f, 2.2f)},
    {"running 20 s", new Walk(8.0f, 2.8f)}, {"shaking 6 m/s^2 at 3 Hz", new Shake(6, 3)},
    {"shaking 18 m/s^2 at 5 Hz", new Shake(18, 5)},
    {"twisting back and forth", new Turn(1, twist)}, {"rocking", new Turn(1, rock)},
    {"tilt flick", new Turn(1, flickPos)},
  };
  for (auto &b : busy) {
    auto e = runKnocks(*b.m, 20.0f);
    snprintf(buf, sizeof buf, "%zu knock(s)", e.size());
    char what[80];
    snprintf(what, sizeof what, "%s: no knocks", b.name);
    check(e.empty(), what, buf);
  }

  static const char *DIR[] = {"none", "left", "right", "up", "down"};
  printf("Tilt flicks, rocking and twists (render-loop sampling 25-70 ms):\n");
  for (int p : {25, 45, 70}) {
    struct { const char *name; Motion *m; TiltFlickDetector::Dir want; } flicks[] = {
      {"tip right and back", new Turn(1, flickPos), TiltFlickDetector::RIGHT},
      {"tip left and back", new Turn(1, flickNeg), TiltFlickDetector::LEFT},
      {"tip top-down and back", new Turn(0, flickPos), TiltFlickDetector::DOWN},
      {"tip top-up and back", new Turn(0, flickNeg), TiltFlickDetector::UP},
    };
    for (auto &f : flicks) {
      LoopResult r = runLoop(*f.m, 3.0f, p);
      snprintf(buf, sizeof buf, "%d flick(s), %s", r.flicks, DIR[r.dir]);
      char what[80];
      snprintf(what, sizeof what, "%s (%d ms loop) -> %s", f.name, p, DIR[f.want]);
      check(r.flicks == 1 && r.dir == f.want && !r.dizzy && !r.rocks, what, buf);
    }
    struct { const char *name; Motion *m; bool rock, dizzy; } others[] = {
      {"tip and hold (a tilt, not a flick)", new Turn(1, holdTilt), false, false},
      {"slow, gentle tip and back", new Turn(1, slowTilt), false, false},
      {"rocking 15 deg at 0.7 Hz -> petted", new Turn(1, rock), true, false},
      {"rocking top-to-bottom -> petted", new Turn(0, rock), true, false},
      {"quick twists -> dizzy (no flick, no rocking)", new Turn(1, twist), false, true},
      {"walking", new Walk(3.0f, 1.8f), false, false},
      {"running", new Walk(8.0f, 2.8f), false, false},
      {"shaking", new Shake(14, 4), false, false},
    };
    for (auto &o : others) {
      LoopResult r = runLoop(*o.m, 8.0f, p);
      snprintf(buf, sizeof buf, "flicks %d, rock swings %d, dizzy %s", r.flicks, r.rocks, r.dizzy ? "yes" : "no");
      char what[80];
      snprintf(what, sizeof what, "%s (%d ms)", o.name, p);
      bool ok = r.flicks == 0 && (o.rock ? r.rocks >= 2 : r.rocks == 0) && r.dizzy == o.dizzy;
      // Shaking may count as a twist on its own; only flicks and rocking matter there.
      if (o.m && o.name[0] == 's' && o.name[1] == 'h') ok = r.flicks == 0 && r.rocks == 0;
      check(ok, what, buf);
    }
  }
  printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
