#pragma once
#include <Arduino.h>
#include <math.h>
#include "esp_random.h"

// Small, allocation-free animation helpers used by the eye animator.
namespace anim {

constexpr float PI_F = 3.14159265f;
constexpr float TAU_F = 6.28318531f;

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
// Named mix() because C++20 adds std::lerp, which made unqualified calls ambiguous.
inline float mix(float a, float b, float t) { return a + (b - a) * t; }

// ---- Easing curves: t in [0, 1] -> [0, 1] ----
inline float easeIn(float t) { t = clamp01(t); return t * t * t; }
inline float easeOut(float t) { t = 1.0f - clamp01(t); return 1.0f - t * t * t; }
inline float easeInOut(float t) {
  t = clamp01(t);
  if (t < .5f) return 4.0f * t * t * t;
  float u = 2.0f - 2.0f * t;
  return 1.0f - u * u * u * .5f;
}
inline float smoothstep(float t) { t = clamp01(t); return t * t * (3.0f - 2.0f * t); }
inline float smoothstep(float e0, float e1, float x) { return smoothstep((x - e0) / (e1 - e0)); }
// Runs past 1 and swings back (ease-out-back). amount 1.7 overshoots about 10%.
inline float overshoot(float t, float amount = 1.70158f) {
  t = clamp01(t) - 1.0f;
  return 1.0f + t * t * ((amount + 1.0f) * t + amount);
}
// Closed-form damped spring from 0 to 1: a few shrinking wobbles around 1.
inline float springEase(float t, float wobbles = 1.5f, float decay = 6.0f) {
  t = clamp01(t);
  return 1.0f - expf(-decay * t) * cosf(wobbles * TAU_F * t);
}
// Frame-rate independent exponential approach (rate in 1/s).
inline float approach(float current, float target, float rate, float dt) {
  return target + (current - target) * expf(-rate * dt);
}

// Damped mass-spring. freq is the natural frequency in Hz; zeta is the damping
// ratio (1 = settles without overshoot, .3-.6 = springy overshoot and settle).
// Integrated in fixed sub-steps so the motion is the same at any frame rate.
struct Spring {
  float pos = 0, vel = 0, target = 0;

  void snap(float v) { pos = target = v; vel = 0; }
  void kick(float velocity) { vel += velocity; }

  void update(float dt, float freq, float zeta, float force = 0) {
    const float w = TAU_F * freq;
    const float k = w * w, c = 2.0f * zeta * w;
    int steps = int(dt * 240.0f) + 1;
    float h = dt / steps;
    for (int i = 0; i < steps; ++i) {
      vel += (k * (target - pos) - c * vel + force) * h;
      pos += vel * h;
    }
  }
};

// ---- Smooth noise and constrained randomness ----
inline float hashNoise(int32_t i, uint32_t seed) {
  uint32_t n = uint32_t(i) * 374761393u + seed * 668265263u;
  n = (n ^ (n >> 13)) * 1274126177u;
  n ^= n >> 16;
  return (n & 0xFFFF) * (2.0f / 65535.0f) - 1.0f;
}
// 1D value noise in [-1, 1]; continuous and smooth, never repeats exactly.
inline float noise1(float x, uint32_t seed = 0) {
  float fl = floorf(x);
  int32_t i = int32_t(fl);
  float f = x - fl;
  float u = f * f * (3.0f - 2.0f * f);
  return mix(hashNoise(i, seed), hashNoise(i + 1, seed), u);
}
inline float frand() { return (esp_random() >> 8) * (1.0f / 16777216.0f); }
inline float frand(float lo, float hi) { return lo + (hi - lo) * frand(); }
inline uint32_t randMs(uint32_t lo, uint32_t hi) { return lo + uint32_t(frand() * float(hi - lo)); }
inline bool chance(float p) { return frand() < p; }

} // namespace anim
