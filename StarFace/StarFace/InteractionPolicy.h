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

// Independent of lastActivity; unsigned subtraction also handles millis wrap.
struct AwakeLimit {
  uint32_t startedAt = 0;
  bool active = false;
  void start(uint32_t now) { startedAt = now; active = true; }
  void finish() { active = false; }
  bool closingDue(uint32_t now) const {
    return active && now - startedAt >= sleepAnimationDelay(MAX_AWAKE_MS);
  }
  uint32_t closingAt() const { return startedAt + sleepAnimationDelay(MAX_AWAKE_MS); }
};
