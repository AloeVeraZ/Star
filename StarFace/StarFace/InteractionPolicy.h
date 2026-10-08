#pragma once
#include "CreatureAnimator.h"

inline Mood tapMood(unsigned streak) {
  return streak >= 3 ? Mood::ANGRY : streak == 2 ? Mood::SURPRISED : Mood::BOOP;
}

// Screen taps only; case knocks cannot contribute to the five-tap cooldown.
struct ScreenTapRun {
  uint8_t count = 0;
  uint32_t lastAt = 0;
  uint8_t add(uint32_t now) {
    count = count && now - lastAt <= TOUCH_TAP_RUN_GAP_MS ? (count < 5 ? count + 1 : 5) : 1;
    lastAt = now;
    return count;
  }
};

// Keep the existing closing choreography, with the display off at the deadline.
inline uint32_t sleepAnimationDelay(uint32_t timeout) {
  return timeout > SLEEP_SEQUENCE_MS ? timeout - SLEEP_SEQUENCE_MS : 0;
}

// A wake grants 30 seconds. Interaction renews at least 15 seconds, without
// shortening the initial window or accumulating unused time from rapid taps.
// Unsigned subtraction handles millis wrap even across repeated renewals.
struct AwakeLimit {
  uint32_t startedAt = 0;
  uint32_t timeout = WAKE_AWAKE_MS;
  bool active = false;
  void start(uint32_t now) { startedAt = now; timeout = WAKE_AWAKE_MS; active = true; }
  void interaction(uint32_t now) {
    if (active && (now - startedAt >= timeout || timeout - (now - startedAt) < IDLE_SLEEP_MS)) {
      startedAt = now;
      timeout = IDLE_SLEEP_MS;
    }
  }
  void finish() { active = false; }
  bool closingDue(uint32_t now) const {
    return active && now - startedAt >= sleepAnimationDelay(timeout);
  }
  uint32_t closingAt() const { return startedAt + sleepAnimationDelay(timeout); }
};

// The first screen press woke the CPU. Count only distinct subsequent presses
// separated by a release; contact reports, IRQ pulses and double-click gesture
// codes cannot multiply one touch. Unknown I2C readings never count as release.
struct TouchWakeCheck {
  uint8_t count = 1;
  bool down = true;
  uint32_t lastPressAt = 0, releasedAt = 0;
  void begin(uint32_t now, uint8_t fingers) {
    count = 1;
    down = fingers != 0;
    lastPressAt = releasedAt = now;
  }
  bool feed(uint8_t fingers, uint32_t now) {
    if (fingers == 0xFF) return false;
    if (fingers == 0) {
      if (down) {
        down = false;
        releasedAt = now;
        if (now - lastPressAt > TAP_MAX_MS) count = 0;
      }
    } else if (!down && now - releasedAt >= 35 && now - lastPressAt >= 60) {
      count = count && now - lastPressAt <= TOUCH_TAP_RUN_GAP_MS ? count + 1 : 1;
      lastPressAt = now;
      down = true;
      return count >= TOUCH_WAKE_TAPS;
    }
    return false;
  }
};
