#include <math.h>
#include <stdio.h>
#include <initializer_list>
#include "LeftRightWake.h"

// User recordings, captured with the OLD +/-512 dps range (64 counts/dps).
// No timestamps were supplied: repeated replays check classification at several
// assumed intervals, not the physical duration or reliability of either clip.
static const int intended[12][6] = {
  {2194,17912,-2803,2186,2198,-32768}, {10473,-28543,11424,2260,945,-29714},
  {3143,-6286,-2165,5029,20522,32767}, {9417,3522,2234,-11100,6596,32767},
  {1857,13001,-5161,18804,-9612,-32768}, {14457,-6445,-119,-12105,-32768,-32768},
  {7663,-10803,1894,28637,32767,32767}, {17374,26997,-5675,32767,-12043,18675},
  {5751,4215,-7002,-24924,-32768,-32768}, {514,-12930,6629,-22608,6796,32767},
  {-2766,8943,354,-32768,14034,31362}, {-4673,-4428,157,-8058,7175,-5245}
};
static const int carried[12][6] = {
  {69,-127,-4021,-10254,-32768,17865}, {3748,-2567,321,-11099,-32768,32767},
  {9946,3047,5953,-25025,-32768,20310}, {6339,638,3912,-7562,13378,9996},
  {5450,1965,6644,3650,24856,-15000}, {4419,2078,4424,-2267,32767,-32768},
  {4555,-2090,-1026,-18104,32767,-9438}, {2348,4436,-7329,-7799,-2820,-144},
  {-2657,1445,-3519,-1450,19265,-6191}, {-2469,1385,-2624,2234,1809,-1564},
  {-2109,452,-3031,478,9165,-2449}, {-2401,461,-3658,2741,-7912,116}
};

static int replay(const int rows[12][6], int period, int cycles, int offset = 0) {
  LeftRightWakeCheck check;
  for (int i = 0; i < 12 * cycles; ++i) {
    const int *r = rows[(i + offset) % 12];
    float a[3] = {r[0] * (9.80665f/4096), -r[1] * (9.80665f/4096), -r[2] * (9.80665f/4096)};
    float g[3] = {r[3] * (.017453293f/64), -r[4] * (.017453293f/64), -r[5] * (.017453293f/64)};
    if (check.feed(a, g, 1 + i * period)) return i * period;
  }
  return -1;
}

static int synthetic(int direction, int duration = 6000, bool pause = false, bool invert = false) {
  LeftRightWakeCheck check;
  for (int t = 0; t < 6500; t += 10) {
    float wave = sinf(t * .001f * 6.2831853f * 3.5f);
    bool on = t >= 200 && t < duration && !(pause && t >= 1300 && t < 2200);
    float a[3] = {0, 0, 9.81f}, g[3] = {};
    if (on) {
      if (direction == 0) { a[1] = 18 * wave; g[2] = 7 * wave; }
      if (direction == 1) { a[0] = 18 * wave; a[2] += 8 * wave; g[1] = 7 * wave; }
      if (direction == 2) { a[2] += 6 * fmaxf(0, wave); g[1] = .5f * wave; }
      if (direction == 3) { g[2] = 7 * wave; } // rotation alone
      if (direction == 4) { a[1] = 18 * wave; } // translation alone
      if (direction == 5) { a[1] = 18 * wave; g[1] = 7 * wave; } // wrong rotation axis
      if (direction == 6) { a[1] = 18; g[2] = 7; } // sustained one-way push
    }
    if (invert) { a[1] = -a[1]; g[2] = -g[2]; }
    if (check.feed(a, g, 1 + t)) return t;
  }
  return -1;
}

int main() {
  int failures = 0;
  for (int period : {40, 55, 80}) {
    for (int offset = 0; offset < 12; ++offset) {
      int yes = replay(intended, period, 20, offset);
      int no = replay(carried, period, 40, offset);
      if (yes < 2000 || no >= 0) {
        ++failures;
        printf("Replay %d ms offset %d: intended=%d carried=%d FAILED\n", period, offset, yes, no);
      }
    }
    if (replay(intended, period, 1) >= 0) { ++failures; puts("Brief recording woke too soon"); }
  }
  int good = synthetic(0), inverted = synthetic(0, 6000, false, true);
  printf("Deliberate left/right: %d ms, reversed signs: %d ms\n", good, inverted);
  if (good < 2200 || good > 3500 || inverted < 2200 || inverted > 3500) ++failures;
  for (int direction = 1; direction <= 6; ++direction) {
    int got = synthetic(direction);
    printf("Rejected motion %d: %s\n", direction, got < 0 ? "asleep" : "FALSE WAKE");
    if (got >= 0) ++failures;
  }
  if (synthetic(0, 1900) >= 0 || synthetic(0, 3800, true) >= 0) ++failures;
  // A gap in sampling must not count as time spent performing the gesture.
  LeftRightWakeCheck gap;
  for (int t = 0; t < 1000; t += 20) {
    float a[3] = {0, 18*sinf(t*.022f), 9.81f}, g[3] = {0,0,7*sinf(t*.022f)};
    gap.feed(a, g, t + 1);
  }
  float still[3] = {0,0,9.81f}, zero[3] = {};
  if (gap.feed(still, zero, 3001) || gap.shakingFor(3001)) ++failures;
  printf("%s: %d failure(s)\n", failures ? "FAILED" : "ALL PASSED", failures);
  return failures ? 1 : 0;
}
