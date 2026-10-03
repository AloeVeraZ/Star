#pragma once
#include <math.h>
#include <stdint.h>
#include "FaceConfig.h"

// Motion gestures, so every reaction works without the touch screen (for
// example behind a protective cover). Like ShakeDetector they are free of
// Arduino calls and tested on a PC (tools/preview/motion_test.cpp).
//
//   Knocks       tap the case 1-4 times      (like tapping the screen)
//   Tilt flicks  tip it one way and back     (like a swipe in that direction)
//   Rocking      rock it gently side to side (like petting it)
//
// Inputs: acceleration including gravity (m/s^2) and rotation (rad/s), in the
// board's axes. Directions are reported the same way the eyes follow tilt
// (a[0] right, a[1] down), so if tilt gaze is set up right, these are too.

namespace motion {
inline float len3(const float v[3]) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
inline float decay(float dt, float tau) { return 1.0f - expf(-dt / tau); }
} // namespace motion

// ---- Knocks: short, sharp jolts on a star that was otherwise still ----------
// Needs fast samples (a few ms apart; the sketch reads the sensor on a
// separate core for this) because a knock is over in a few milliseconds.
// A jolt only counts when the star was quiet just before it, did not swing
// or keep moving, and settled again within KNOCK_RING_MS, so walking,
// running, shaking and swinging it around are not knocks.
class KnockDetector {
 public:
  struct Event {
    uint8_t count = 0;   // 0: nothing; else knocks in this run so far (1, 2, 3 ...)
    float x = 0, y = 0;  // direction the star was pushed by the knock (board axes)
  };

  Event feed(const float a[3], const float g[3], uint32_t now) {
    Event ev;
    if (!seeded) {
      for (int i = 0; i < 3; ++i) grav[i] = a[i];
      seeded = true;
      last = lastBusyAt = now;
      return ev;
    }
    float dt = (now - last) / 1000.0f;
    last = now;
    if (dt > .1f) dt = .1f;
    float lin[3];
    float k = motion::decay(dt, .4f);
    for (int i = 0; i < 3; ++i) {
      lin[i] = a[i] - grav[i];
      grav[i] += lin[i] * k;
    }
    const float mag = motion::len3(lin), spin = motion::len3(g);
    // How suddenly it changed since the last sample: a knock is a jump, while
    // walking, shaking and swinging change smoothly.
    float jerk[3] = {lin[0] - prev[0], lin[1] - prev[1], lin[2] - prev[2]};
    for (int i = 0; i < 3; ++i) prev[i] = lin[i];
    const float sharp = fmaxf(mag, .6f * motion::len3(jerk));

    if (pendingAt) {
      uint32_t age = now - pendingAt;
      if (age <= 15 && mag > peak) { peak = mag; px = lin[0]; py = lin[1]; }
      if (spin > KNOCK_MAX_SPIN_RAD_S) pendingAt = 0;               // it was swung, not knocked
      else if (age >= KNOCK_RING_MS - 40 && mag > fmaxf(3.0f, .25f * peak)) pendingAt = 0; // still moving
      else if (age >= KNOCK_RING_MS) {
        // A clean knock.
        count = (lastKnockAt && now - lastKnockAt < KNOCK_RING_MS + KNOCK_GAP_MS) ? count + 1 : 1;
        lastKnockAt = now;
        pendingAt = 0;
        ev.count = count;
        ev.x = px;
        ev.y = py;
      }
      if (!pendingAt) { lastBusyAt = now; spikeStart = 0; } // quiet is measured from the end of the ring
      return ev;
    }
    // Any movement marks it busy. A new disturbance after a quiet spell
    // remembers how long that spell was: the first samples of a knock are
    // often small, so the quiet is measured up to where the jolt began.
    if (sharp > KNOCK_QUIET_MS2 || spin > KNOCK_MAX_SPIN_RAD_S * .6f) {
      if (now - lastBusyAt > 20) { spikeStart = now; quietSpan = now - lastBusyAt; }
      lastBusyAt = now;
    }
    // Quiet long enough before? (A follow-up knock in a run needs less.)
    bool inRun = lastKnockAt && now - lastKnockAt < KNOCK_GAP_MS + KNOCK_RING_MS;
    uint32_t quietMs = inRun ? 60 : KNOCK_QUIET_BEFORE_MS;
    if (sharp > KNOCK_MS2 && now - spikeStart <= 20 && quietSpan >= quietMs && spin < KNOCK_MAX_SPIN_RAD_S) {
      pendingAt = now;
      peak = sharp; px = lin[0]; py = lin[1];
    }
    return ev;
  }

 private:
  float grav[3] = {0, 0, 0};
  bool seeded = false;
  uint32_t last = 0, lastBusyAt = 0, pendingAt = 0, lastKnockAt = 0, spikeStart = 0, quietSpan = 0;
  float peak = 0, px = 0, py = 0, prev[3] = {0, 0, 0};
  uint8_t count = 0;
};

// ---- Tilt flicks: tip the star one way and straight back --------------------
// It has to be held fairly still first, then tipped quickly by at least
// FLICK_MIN_TILT_MS2 (about 20 degrees) and brought back within FLICK_MAX_MS.
// A tilt that is held stays a tilt (the eyes just look that way), and twisting
// back and forth (dizzy) or shaking cancels it.
class TiltFlickDetector {
 public:
  enum Dir : uint8_t { NONE, LEFT, RIGHT, UP, DOWN };

  Dir feed(const float a[3], const float g[3], float shakeStrength, uint32_t now) {
    const float spin = motion::len3(g);
    const float tx = a[0], ty = a[1];
    if (!seeded) { bx = tx; by = ty; seeded = true; }
    if (!moving) {
      if (spin < .6f) {
        if (!stillSince) stillSince = now;
        lastStillAt = now;
        bx += (tx - bx) * .3f;
        by += (ty - by) * .3f;
      } else if (spin > FLICK_START_RAD_S && stillSince && lastStillAt - stillSince >= 250 &&
                 now - lastStillAt <= 250 && shakeStrength < 1.5f) {
        // Quick turn right after holding still (it may take a sample or two to speed up).
        moving = true;
        start = lastStillAt;
        mx = my = 0;
        lastSign = 0;
        reversals = 0;
      } else if (now - lastStillAt > 250) {
        stillSince = 0;
      }
      return NONE;
    }
    // Tipping: remember the furthest point; count direction changes.
    float dx = tx - bx, dy = ty - by;
    if (dx * dx + dy * dy > mx * mx + my * my) { mx = dx; my = dy; }
    float main = fabsf(g[0]) > fabsf(g[1]) ? g[0] : g[1];
    if (fabsf(main) > 1.2f) {
      int8_t s = main > 0 ? 1 : -1;
      if (lastSign && s != lastSign) ++reversals;
      lastSign = s;
    }
    if (shakeStrength > 2.5f || reversals > 1 || now - start > FLICK_MAX_MS) return cancel();
    bool back = dx * dx + dy * dy < FLICK_RETURN_MS2 * FLICK_RETURN_MS2 && spin < 1.0f;
    if (!back) return NONE;
    float far = sqrtf(mx * mx + my * my);
    moving = false;
    stillSince = 0;   // needs a fresh still moment before the next flick
    if (far < FLICK_MIN_TILT_MS2) return NONE;
    if (fabsf(mx) >= fabsf(my)) return mx > 0 ? RIGHT : LEFT;
    return my > 0 ? DOWN : UP;
  }

 private:
  bool seeded = false, moving = false;
  float bx = 0, by = 0, mx = 0, my = 0;
  uint32_t stillSince = 0, lastStillAt = 0, start = 0;
  int8_t lastSign = 0;
  uint8_t reversals = 0;

  Dir cancel() {
    moving = false;
    stillSince = 0;
    return NONE;
  }
};

// ---- Rocking: slow, gentle turns side to side, like cradling it -------------
// Each swing must take ROCK_MIN_HALF_MS..ROCK_MAX_HALF_MS (quick twists are
// the dizzy gesture instead) and the star must not be bouncing around (so
// walking with it swinging in a hand does not count).
class RockDetector {
 public:
  // Returns true on every swing once it has rocked ROCK_SWINGS times in a row.
  // a includes gravity: turning leaves its length at 1 g, bouncing does not.
  bool feed(const float a[3], const float g[3], uint32_t now) {
    float dt = last ? fminf(.1f, (now - last) / 1000.0f) : .025f;
    last = now;
    bounce += (fabsf(motion::len3(a) - 9.81f) - bounce) * motion::decay(dt, .5f);
    bool any = false;
    for (int i = 0; i < 2; ++i) any |= axis[i].feed(g[i], now, dt, bounce < ROCK_MAX_BOUNCE_MS2);
    return any;
  }

 private:
  struct Axis {
    int8_t dir = 0;
    uint32_t halfStart = 0, lastMoveAt = 0;
    float angle = 0, peak = 0;
    uint8_t swings = 0;

    bool feed(float rate, uint32_t now, float dt, bool calm) {
      if (!calm) { swings = 0; dir = 0; return false; }
      if (dir && now - lastMoveAt > 700) { swings = 0; dir = 0; }   // stopped rocking
      if (fabsf(rate) < .35f) return false;
      lastMoveAt = now;
      int8_t d = rate > 0 ? 1 : -1;
      if (d == dir) {
        angle += fabsf(rate) * dt;
        peak = fmaxf(peak, fabsf(rate));
        return false;
      }
      bool good = false;
      if (dir) {
        uint32_t half = now - halfStart;
        bool ok = half >= ROCK_MIN_HALF_MS && half <= ROCK_MAX_HALF_MS &&
                  angle >= .12f && angle <= 1.2f && peak < 3.0f;
        swings = ok ? swings + 1 : 0;
        good = ok && swings >= ROCK_SWINGS;
      }
      dir = d;
      halfStart = now;
      angle = fabsf(rate) * dt;
      peak = fabsf(rate);
      return good;
    }
  } axis[2];
  float bounce = 0;
  uint32_t last = 0;
};
