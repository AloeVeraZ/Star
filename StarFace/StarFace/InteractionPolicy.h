#pragma once
#include "CreatureAnimator.h"

inline Mood tapMood(unsigned streak) {
  return streak >= 3 ? Mood::ANGRY : streak == 2 ? Mood::SURPRISED : Mood::BOOP;
}

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
