#pragma once
#include <math.h>
#include <stdint.h>
#include "FaceConfig.h"

// Shake recognition, kept free of Arduino calls so it can be tested on a PC
// (tools/preview/shake_test.cpp) with simulated accelerometer data.
//
// Input is the gravity-free acceleration (m/s^2) and rotation rate (rad/s),
// sampled at whatever rate the render loop allows (roughly every 20-70 ms).
// Two independent triggers make it dizzy, so an irregular real-world shake
// still counts:
//   * strokes: strong pushes that reverse direction, each within SHAKE_GAP_MS
//   * strength: a running average of how hard it is shaken, held high
// It is built to ignore being carried: walking never makes a stroke, and the
// odd stroke from running only sloshes the eyes. Only a real back-and-forth
// (STARTLE, then DIZZY) changes the mood, and a knock (BUMP) only counts when
// it was sitting calmly before.
class ShakeDetector {
 public:
  enum Event : uint8_t {
    NONE,
    STROKE,   // one strong push: slosh the eyes
    STARTLE,  // a second push back the other way: a real shake has begun
    DIZZY,    // a full shake
    BUMP      // a sharp knock while it was resting
  };

  float strength = 0;   // m/s^2 above SHAKE_NOISE_MS2, smoothed (~0.35 s)
  uint8_t strokes = 0;  // strokes in the current shake
  // Thresholds (awake defaults; the wake-from-sleep check uses its own).
  float strokeMs2 = SHAKE_STROKE_MS2;
  float dizzyStrength = SHAKE_DIZZY_STRENGTH;
  // Awake, DIZZY needs a steady shake kept up for SHAKE_DIZZY_HOLD_MS. The
  // wake-from-sleep check instead uses the quick rule (strokesForDizzy strokes
  // in a row, or holdMs of strong shaking) only to count its own jolts.
  bool steadyDizzy = true;
  uint8_t strokesForDizzy = 3;
  uint32_t holdMs = SHAKE_DIZZY_HOLD_MS;
  // Rotation faster than this counts as shaking too (0: never; awake, only
  // real back-and-forth motion counts, so turning it can't make it dizzy).
  float spinRadS = 0;

  Event feed(const float lin[3], float spin, uint32_t now) {
    float dt = lastSampleAt ? fminf(.1f, (now - lastSampleAt) / 1000.0f) : .025f;
    lastSampleAt = now;
    float jolt = sqrtf(lin[0] * lin[0] + lin[1] * lin[1] + lin[2] * lin[2]);
    // A wrist shake is partly rotation, so a fast spin counts as shaking too.
    float excess = fmaxf(0.0f, jolt - SHAKE_NOISE_MS2);
    if (spinRadS > 0) excess = fmaxf(excess, (spin - spinRadS) * 2.5f);
    strength += (excess - strength) * (1.0f - expf(-dt / .35f));

    if (strokes && now - lastStrokeAt > SHAKE_GAP_MS) strokes = 0; // the shake paused
    Event ev = NONE;
    // A knock on a resting star (not a step while it is carried).
    bool wasCalm = !lastBusyAt || now - lastBusyAt > 1000;
    if (jolt > BUMP_MS2 && wasCalm && (!lastBumpAt || now - lastBumpAt > 1500)) {
      lastBumpAt = now;
      ev = BUMP;
    }
    if (strength > 1.0f || jolt > SHAKE_NOISE_MS2 * 1.5f) lastBusyAt = now;
    if (jolt > strokeMs2 && (!lastStrokeAt || now - lastStrokeAt > 60)) {
      float dot = lin[0] * dir[0] + lin[1] * dir[1] + lin[2] * dir[2];
      if (strokes == 0 || dot < 0) {
        ++strokes;
        for (int i = 0; i < 3; ++i) dir[i] = lin[i];
        lastStrokeAt = now;
        if (ev != BUMP) ev = strokes == 2 ? STARTLE : STROKE;
      }
    }
    if (steadyDizzy) {
      // A steady shake: strong enough on average, kept up (short dips are
      // forgiven), and really back and forth (swings that reverse).
      if (strength > dizzyStrength) {
        if (!shakingSince) { shakingSince = now; swings = 0; }
        lastStrongAt = now;
      } else if (shakingSince && now - lastStrongAt > SHAKE_DIZZY_DROPOUT_MS) {
        shakingSince = 0;   // it stopped: start over
        swings = 0;
      }
      if (shakingSince && jolt > SHAKE_NOISE_MS2 + dizzyStrength && now - lastSwingAt > 60) {
        float dot = lin[0] * swingDir[0] + lin[1] * swingDir[1] + lin[2] * swingDir[2];
        if (swings == 0 || dot < 0) {
          ++swings;
          for (int i = 0; i < 3; ++i) swingDir[i] = lin[i];
          lastSwingAt = now;
        }
      }
      // Swings must keep coming: a shake that stopped no longer counts, even
      // while the averaged strength is still fading out.
      if (shakingSince && swings && now - lastSwingAt > SHAKE_DIZZY_DROPOUT_MS + 100) {
        shakingSince = 0;
        swings = 0;
      }
      if (shakingSince && now - shakingSince >= holdMs && swings >= SHAKE_DIZZY_SWINGS &&
          now - lastSwingAt <= SHAKE_DIZZY_DROPOUT_MS) {
        shakingSince = now;  // keep shaking: another spell after another hold
        swings = 0;
        strokes = 0;
        return DIZZY;
      }
      return ev;
    }
    strongSince = strength > dizzyStrength ? (strongSince ? strongSince : now) : 0;
    bool sustained = strongSince && now - strongSince >= holdMs;
    if (strokes >= strokesForDizzy || sustained) {
      strokes = 0;
      strongSince = 0;
      return DIZZY;
    }
    return ev;
  }

  // How long the current steady shake has been kept up (0: not shaking).
  uint32_t shakingFor(uint32_t now) const { return shakingSince ? now - shakingSince : 0; }

  // 0 (still) .. ~1.3 (hard): how much the eyes should rattle right now.
  float rattle() const {
    return strength > SHAKE_NOISE_MS2 * .5f ? strength / SHAKE_FULL_MS2 : 0.0f;
  }

 private:
  float dir[3] = {0, 0, 0};
  uint32_t lastSampleAt = 0, lastStrokeAt = 0, strongSince = 0, lastBusyAt = 0, lastBumpAt = 0;
  uint32_t shakingSince = 0, lastStrongAt = 0, lastSwingAt = 0;
  float swingDir[3] = {0, 0, 0};
  uint8_t swings = 0;
};

// ---- Twist: quick back-and-forth turns around either axis across the screen ----
static constexpr uint32_t TWIST_WINDOW_MS = 3000;
static constexpr uint32_t TWIST_MAX_PAUSE_MS = 600;
static constexpr float TWIST_RATE_RAD_S = 1.2f;        // about 69 degrees/s on X or Y
static constexpr float TWIST_MIN_HALF_TURN_RAD = .25f; // about 14 degrees each way
static constexpr uint8_t TWIST_REVERSALS_TO_WAKE = 3;
static constexpr uint32_t TWIST_MAX_HALF_MS = 330;     // slower swings are rocking (petting), not twisting

struct TwistAxis {
  uint32_t startedAt = 0, lastMoveAt = 0, halfAt = 0;
  float halfTurn = 0;
  int8_t direction = 0;
  uint8_t reversals = 0;

  void reset() {
    startedAt = lastMoveAt = halfAt = 0;
    halfTurn = 0;
    direction = 0;
    reversals = 0;
  }

  bool feed(float rate, uint32_t now, uint32_t dt) {
    if (startedAt && (now - lastMoveAt > TWIST_MAX_PAUSE_MS ||
                      now - startedAt > TWIST_WINDOW_MS)) reset();
    if (fabsf(rate) < TWIST_RATE_RAD_S) return false;
    int8_t nextDirection = rate > 0 ? 1 : -1;
    float step = fabsf(rate) * (dt < 60 ? dt : 60) / 1000.0f;
    if (!startedAt) {
      startedAt = halfAt = now;
      direction = nextDirection;
      halfTurn = step;
    } else if (direction == nextDirection) {
      halfTurn += step;
    } else {
      // A reversal only counts after a real, quick angular sweep (not gyro
      // noise, and not a slow rocking swing).
      if (halfTurn >= TWIST_MIN_HALF_TURN_RAD && now - halfAt <= TWIST_MAX_HALF_MS) ++reversals;
      else reversals = 0;
      direction = nextDirection;
      halfTurn = step;
      halfAt = now;
    }
    lastMoveAt = now;
    if (reversals >= TWIST_REVERSALS_TO_WAKE &&
        halfTurn >= TWIST_MIN_HALF_TURN_RAD) {
      reset();
      return true;
    }
    return false;
  }
};

struct TwistDetector {
  TwistAxis axis[2]; // board X and Y run parallel to the screen
  uint32_t lastSampleAt = 0;

  bool feed(const float gyro[3], uint32_t now) {
    uint32_t dt = lastSampleAt ? now - lastSampleAt : 25;
    lastSampleAt = now;
    bool x = axis[0].feed(gyro[0], now, dt);
    bool y = axis[1].feed(gyro[1], now, dt);
    return x || y;
  }

  bool active() const { return axis[0].startedAt || axis[1].startedAt; }
  bool progressing() const {
    return axis[0].reversals >= 2 || axis[1].reversals >= 2;
  }
};

// Wake check after the IMU's motion alarm woke the CPU with the screen dark.
// Asleep it is deliberately hard to wake: it needs a steady shake, kept up for
// SHAKE_WAKE_HOLD_MS (about 4 s); stop for longer than SHAKE_WAKE_DROPOUT_MS
// and the count starts over. Gravity is estimated from scratch here (the CPU
// was off), starting from the first sample scaled to 1 g.
class ShakeWakeCheck {
 public:
  uint8_t jolts = 0;      // strokes in the current run (for the log)
  float lastJolt = 0;

  ShakeWakeCheck() {      // the sleep-time thresholds (unchanged by awake tuning)
    det.strokeMs2 = SHAKE_WAKE_STROKE_MS2;
    det.steadyDizzy = false;
    det.strokesForDizzy = 3;
    det.dizzyStrength = 3.5f;
    det.holdMs = 400;
    det.spinRadS = SHAKE_GYRO_RAD_S;
  }

  uint32_t shakingFor(uint32_t now) const { return since ? now - since : 0; }
  bool stopped(uint32_t now) const { return !since && lastOnAt && now - lastOnAt > 1000; }

  bool feed(const float a[3], const float g[3], uint32_t now) {
    float mag = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (!seeded) {
      float s = mag > .1f ? 9.81f / mag : 0.0f;
      for (int i = 0; i < 3; ++i) grav[i] = a[i] * s;
      seeded = true;
      lastAt = now;
    }
    float dt = (now - lastAt) / 1000.0f;
    lastAt = now;
    float k = 1.0f - expf(-dt / .25f), lin[3];
    for (int i = 0; i < 3; ++i) {
      grav[i] += (a[i] - grav[i]) * k;
      lin[i] = a[i] - grav[i];
    }
    float spin = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    ShakeDetector::Event ev = det.feed(lin, spin, now);
    if (ev == ShakeDetector::STROKE || ev == ShakeDetector::STARTLE || ev == ShakeDetector::DIZZY) {
      ++jolts;
      lastJolt = sqrtf(lin[0] * lin[0] + lin[1] * lin[1] + lin[2] * lin[2]);
    }
    if (det.strength > SHAKE_WAKE_STRENGTH) {
      if (!since) since = now;
      lastOnAt = now;
    } else if (since && now - lastOnAt > SHAKE_WAKE_DROPOUT_MS) {
      since = 0;   // the shake stopped: start over
      jolts = 0;
    }
    // Steady shaking for long enough, and really back and forth.
    return since && now - since >= SHAKE_WAKE_HOLD_MS && jolts >= 6;
  }

 private:
  ShakeDetector det;
  float grav[3] = {0, 0, 0};
  bool seeded = false;
  uint32_t lastAt = 0, since = 0, lastOnAt = 0;
};
