// Plays recorded-style CST816S report streams through the sketch's real
// TouchTracker (StarFace/TouchTracker.h) at a render-loop-like ~35 ms rate.
#include <math.h>
#include <stdio.h>
#include <functional>
#include "TouchTracker.h"

static int fails = 0;
struct Seen { int press = 0, move = 0, tap = 0, swipe = 0, release = 0; float dx = 0, dy = 0; uint32_t held = 0; };

// path(t) -> finger position at t ms; lift at `upAt`. gesture(t): what the
// chip's own recognizer puts in each report. sendLift: whether the lift report arrives.
static Seen play(uint32_t upAt, std::function<void(float, int &, int &)> path,
                 std::function<int(float)> gesture = [](float) { return 0; }, bool sendLift = true) {
  TouchTracker tt;
  Seen s;
  uint32_t now = 1000;
  auto take = [&](const TouchTracker::Result &r) {
    switch (r.kind) {
      case TouchTracker::PRESS: ++s.press; break;
      case TouchTracker::MOVE: ++s.move; break;
      case TouchTracker::TAP: ++s.tap; s.held = r.heldMs; break;
      case TouchTracker::SWIPE: ++s.swipe; s.dx = r.dx; s.dy = r.dy; s.held = r.heldMs; break;
      case TouchTracker::RELEASE: ++s.release; s.held = r.heldMs; break;
      default: break;
    }
  };
  for (float t = 0; t <= upAt + 600; t += 30 + (int(t * 7) % 11)) {
    now = 1000 + uint32_t(t);
    if (t <= upAt) {
      int x, y;
      path(t, x, y);
      uint8_t ev = t == 0 ? 0 : (t + 41 > upAt ? 1 : 2);
      if (ev == 1 && !sendLift) { take(tt.poll(now)); continue; }
      take(tt.feed(x, y, ev, gesture(t), true, now));
      if (ev == 1) upAt = uint32_t(t); // lifted
    } else {
      take(tt.poll(now));
    }
  }
  return s;
}

#define EXPECT(c, name) do { bool ok = (c); printf("  %-58s %s\n", name, ok ? "ok" : "FAIL"); if (!ok) ++fails; } while (0)

int main() {
  printf("Touch tracker\n");
  Seen s = play(120, [](float, int &x, int &y) { x = 120; y = 130; });
  EXPECT(s.tap == 1 && s.swipe == 0, "quick tap -> TAP");

  s = play(220, [](float t, int &x, int &y) { x = 120; y = 200 - int(t * .6f); });
  EXPECT(s.swipe == 1 && s.dy < 0 && fabsf(s.dx) < fabsf(s.dy), "fast flick up -> SWIPE up");

  // The reported problem: press low, drag up, keep holding and wander around
  // while the chip keeps tagging reports "swipe up" (1), then flick left, lift.
  s = play(3000,
           [](float t, int &x, int &y) {
             if (t < 400) { x = 120; y = 200 - int(t * .25f); }                       // drag up
             else if (t < 2700) { float a = (t - 400) * .004f;                          // wander
                                  x = 120 + int(40 * cosf(a)); y = 100 + int(30 * sinf(a * 1.3f)); }
             else { x = 150 - int((t - 2700) * .7f); y = 115; }                          // flick left
           },
           [](float t) { return t > 300 ? 1 : 0; });
  EXPECT(s.move > 60, "hold + wander: finger followed the whole time (MOVE reports)");
  EXPECT(s.swipe == 1 && s.dx < 0 && fabsf(s.dx) > fabsf(s.dy),
         "...then flick left -> one SWIPE, to the LEFT (not 'up')");

  s = play(3000, [](float, int &x, int &y) { x = 118; y = 122; });
  EXPECT(s.release == 1 && s.swipe == 0 && s.held >= 2900, "hold still 3 s -> RELEASE after ~3 s");

  s = play(3000, [](float, int &x, int &y) { x = 118; y = 122; }, [](float) { return 0; }, false);
  EXPECT(s.release == 1 && s.held >= 2900 && s.held < 3300, "lift report lost -> still released (timeout)");

  s = play(1500, [](float t, int &x, int &y) { x = t < 600 ? 60 + int(t * .2f) : 180; y = 120; });
  EXPECT(s.release == 1 && s.swipe == 0, "drag, stop, then lift (no flick) -> RELEASE");

  s = play(450, [](float t, int &x, int &y) { x = 80 + int(t * .15f); y = 120; });
  EXPECT(s.swipe == 1 && s.dx > 0, "short, slower swipe right -> SWIPE right");

  s = play(700, [](float t, int &x, int &y) { x = 120 + int(3 * sinf(t)); y = 120 + int(3 * cosf(t * 1.7f)); });
  EXPECT(s.swipe == 0 && s.tap == 0 && s.release == 1, "jittery 0.7 s press -> RELEASE, not a swipe");

  {
    TouchTracker tt;
    TouchTracker::Result r = tt.feed(150, 120, 1, 4, true, 5000);
    EXPECT(r.kind == TouchTracker::SWIPE && r.dx > 0, "missed press, only a 'swipe right' lift -> SWIPE right");
    tt.ignoreUntilLift(6000);
    r = tt.feed(120, 120, 2, 0, true, 6030);
    TouchTracker::Result r2 = tt.poll(6400);
    EXPECT(r.kind == TouchTracker::NONE && r2.kind == TouchTracker::NONE && !tt.down,
           "touch that woke it is ignored until lifted");
  }
  printf("%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
