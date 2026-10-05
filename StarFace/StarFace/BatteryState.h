#pragma once
#include <stdint.h>
#include "FaceConfig.h"

// GPIO1 measures VSYS through a 3:1 divider, not a coulomb-counting fuel gauge.
// USB can drive VSYS above the cell range; unknown readings have no mood effect.
class BatteryState {
 public:
  enum Stage : uint8_t { NORMAL, TIRED, LOW_CHARGE, VERY_LOW, EXHAUSTED, CRITICAL };
  Stage stage = NORMAL;
  int percent = -1;

  static bool mustRemainAsleep(int reading, float volts, bool locked) {
    if (reading >= 0 && reading <= CRITICAL_BATTERY_PERCENT) return true;
    return locked && volts <= 4.4f && (reading < 0 || reading < BATTERY_RECOVER_PERCENT);
  }

  static int estimate(float volts) {
    if (volts < 2.8f || volts > 4.4f) return -1;
    const float v[] = {3.30f, 3.55f, 3.70f, 3.78f, 3.86f, 3.95f, 4.08f, 4.20f};
    const int p[] = {0, 5, 15, 30, 50, 70, 90, 100};
    if (volts <= v[0]) return 0;
    for (int i = 1; i < 8; ++i)
      if (volts <= v[i]) return p[i-1] + int((p[i]-p[i-1]) * (volts-v[i-1]) / (v[i]-v[i-1]));
    return 100;
  }

  void update(int reading) {
    percent = reading;
    if (reading < 0) { stage = NORMAL; return; }
    Stage next = reading <= 3 ? CRITICAL : reading <= 5 ? EXHAUSTED : reading <= 10 ? VERY_LOW
               : reading <= 25 ? LOW_CHARGE : reading <= 50 ? TIRED : NORMAL;
    // Falling charge takes effect immediately; recovery has a 2% deadband.
    const int boundary[] = {100, 50, 25, 10, 5, 3};
    if (next >= stage || reading > boundary[stage] + 2) stage = next;
  }

  float fatigue() const {
    const float f[] = {0, .22f, .45f, .70f, .90f, 1};
    return f[stage];
  }
  float sadness() const {
    const float s[] = {0, .08f, .30f, .60f, .90f, 1};
    return s[stage];
  }
  uint32_t color() const {
    const uint32_t c[] = {EYE_COLOR, 0x9565DD, 0x787CDA, 0x5B83C4, 0x527696, 0x465F80};
    return c[stage];
  }
};
