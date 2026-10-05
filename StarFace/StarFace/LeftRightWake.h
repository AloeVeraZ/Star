#pragma once
#include <math.h>
#include <stdint.h>
#include "FaceConfig.h"

// Calibrated from the two native-axis recordings: intended wake motion has
// strong AY changes and GZ reversals; carried forward/back motion favors GY.
// Input uses readMotion's m/s^2 and rad/s screen axes. Sign flips do not affect
// energy ratios or reversals. This is a temporal gesture, not a single spike.
class LeftRightWakeCheck {
 public:
  float accelShare = 0, gyroShare = 0, zOverY = 0;
  uint8_t swings = 0;

  uint32_t shakingFor(uint32_t now) const { return active ? now - since : 0; }
  bool stopped(uint32_t now) const { return !active && seenMotion && now - lastGoodAt > 1000; }
  bool carryingMotion() const { return carry; }

  bool feed(const float a[3], const float g[3], uint32_t now) {
    if (seeded && now - lastAt < 20) return false;
    if (!seeded || now - lastAt > 250) {
      *this = LeftRightWakeCheck();
      seeded = true;
      startedAt = lastAt = now;
      for (int i = 0; i < 3; ++i) gravity[i] = a[i];
      return false;
    }
    const float dt = (now - lastAt) * .001f;
    lastAt = now;
    const float gravityK = 1.0f - expf(-dt / .25f);
    const float energyK = 1.0f - expf(-dt / .40f);
    float lin[3], accelTotal = .001f, gyroTotal = .001f;
    for (int i = 0; i < 3; ++i) {
      gravity[i] += (a[i] - gravity[i]) * gravityK;
      lin[i] = a[i] - gravity[i];
      accelEnergy[i] += (lin[i] * lin[i] - accelEnergy[i]) * energyK;
      gyroEnergy[i] += (g[i] * g[i] - gyroEnergy[i]) * energyK;
      accelTotal += accelEnergy[i];
      gyroTotal += gyroEnergy[i];
    }
    accelShare = accelEnergy[1] / accelTotal;
    gyroShare = gyroEnergy[2] / gyroTotal;
    zOverY = gyroEnergy[2] / fmaxf(.001f, gyroEnergy[1]);
    const bool ready = now - startedAt >= 200;
    carry = ready && gyroTotal > 1.0f && accelShare < .40f &&
            gyroEnergy[1] > gyroEnergy[2] * 1.25f;
    const bool good = ready && accelShare >= WAKE_AY_ENERGY_SHARE &&
                      gyroShare >= WAKE_GZ_ENERGY_SHARE && zOverY >= WAKE_GZ_OVER_GY &&
                      accelEnergy[1] >= WAKE_AY_RMS_MS2 * WAKE_AY_RMS_MS2 &&
                      gyroEnergy[2] >= WAKE_GZ_RMS_RAD_S * WAKE_GZ_RMS_RAD_S;
    if (good) {
      if (!active) { active = true; since = now; swings = gyroSwings = 0; accelSign = gyroSign = 0; }
      seenMotion = true;
      lastGoodAt = now;
    }
    if (active && (carry || now - lastGoodAt > SHAKE_WAKE_DROPOUT_MS)) {
      active = false;
      swings = gyroSwings = 0;
      accelSign = gyroSign = 0;
    }
    if (!active) return false;

    // Count each signed sweep once, including realistic pauses at its ends.
    if (fabsf(lin[1]) >= WAKE_AY_STROKE_MS2 &&
        (!swings || now - lastAccelSwingAt >= 60)) {
      int sign = lin[1] > 0 ? 1 : -1;
      if (sign != accelSign) {
        accelSign = sign;
        if (swings < 255) ++swings;
        lastAccelSwingAt = now;
      }
    }
    if (fabsf(g[2]) >= WAKE_GZ_REVERSAL_RAD_S &&
        (!gyroSwings || now - lastGyroSwingAt >= 60)) {
      int sign = g[2] > 0 ? 1 : -1;
      if (sign != gyroSign) {
        gyroSign = sign;
        if (gyroSwings < 255) ++gyroSwings;
        lastGyroSwingAt = now;
      }
    }
    // Energy decays slowly; fresh reversals must continue through confirmation.
    if ((swings && now - lastAccelSwingAt > 500) ||
        (gyroSwings && now - lastGyroSwingAt > 500)) {
      active = false;
      swings = gyroSwings = 0;
      return false;
    }
    return good && now - since >= SHAKE_WAKE_HOLD_MS && swings >= SHAKE_WAKE_SWINGS &&
           gyroSwings >= 4 && now - lastAccelSwingAt <= SHAKE_WAKE_DROPOUT_MS &&
           now - lastGyroSwingAt <= SHAKE_WAKE_DROPOUT_MS;
  }

 private:
  float gravity[3] = {}, accelEnergy[3] = {}, gyroEnergy[3] = {};
  bool seeded = false, active = false, seenMotion = false, carry = false;
  uint32_t startedAt = 0, lastAt = 0, since = 0, lastGoodAt = 0;
  uint32_t lastAccelSwingAt = 0, lastGyroSwingAt = 0;
  int accelSign = 0, gyroSign = 0;
  uint8_t gyroSwings = 0;
};
