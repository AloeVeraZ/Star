#pragma once
#include <Arduino.h>
#include "AnimMath.h"
#include "Eyes.h"

// Timings of the longer reactions.
static constexpr uint32_t DIZZY_ANIM_MS = 2850;
static constexpr uint32_t ANGRY_ANIM_MS = 1900;
static constexpr uint32_t BATTERY_SHOW_MS = 2600;
static constexpr uint32_t SLEEP_SEQUENCE_MS = 5400;

// What the creature is doing. Moods are reactions to the outside world
// (touch, motion, time); each one drives the Eyes through their public API.
// Scoped (Mood::HAPPY) so it never clashes with the Expression names.
enum class Mood : uint8_t {
  IDLE, HAPPY, SAD, ANGRY, DIZZY, SLEEPY, SURPRISED, ANXIOUS,
  WAKE_UP, BATTERY, BOOP, PETTED, SWIPING, FOLLOWING, CONFUSED, SHY,
  SHIVER, LOVED, UPSIDE_DOWN
};

// The creature's behaviour on top of the eye system: it turns events into
// expressions, gazes, blinks, lid scripts (waking, falling asleep) and effects
// (dizzy wobble, shake rattle, trembling), and sets the backlight. Everything
// visual goes through Eyes (setExpression, lookAt, blink, ...), so the eyes
// can also be driven directly from the sketch.
class CreatureAnimator {
 public:
  void begin(Eyes *eyes, uint32_t seed, uint32_t now);

  // ---- reactions ----
  // direct = caused by the user; it may interrupt anything except a dizzy spell.
  bool react(Mood m, uint32_t now, uint32_t durationMs, bool direct = true);
  // Self-started behaviour; never overrides an active reaction.
  bool reactPassive(Mood m, uint32_t now, uint32_t durationMs);
  // Keeps a mood that is already showing going a little longer (true if it was showing).
  bool sustain(Mood m, uint32_t now, uint32_t durationMs);
  void queue(Mood m, uint32_t at, uint32_t durationMs, int param = 0);
  void showBattery(int percent, uint32_t now);
  // fromShake plays a startled wake into dizziness; touched favors a startled wake.
  void startWake(uint32_t now, bool fromShake, bool touched = false);
  // napLevel > 0: fall asleep but keep the screen on at that backlight level.
  void startSleep(uint32_t now, float napLevel);
  bool sleepFinished(uint32_t now) const;
  bool asleep() const { return current == Mood::SLEEPY; }

  // ---- inputs ----
  void setTilt(float x, float y) { tiltX = x; tiltY = y; }
  void setPointer(float x, float y, uint32_t now);
  // A finger is on the screen. The longer it stays, the angrier it gets.
  void setPointerHeld(bool held, uint32_t now) {
    if (held && !pointerHeld) holdSince = now;
    pointerHeld = held;
  }
  // Held upside down: the same slow build-up of anger as a finger held on it.
  void setUpsideDown(bool upside, uint32_t now) {
    if (upside && !upsideDown) upsideSince = now;
    upsideDown = upside;
  }
  float annoyance() const { return holdAnnoy; }  // 0 calm .. 1 furious, from holding
  // Let go after being held: a huff proportional to how annoyed it got, then
  // it cools down over ANGER_COOLDOWN_S.
  void huff(float amount, uint32_t now);
  void setTouchPoint(int x, int y) { touchX = x; touchY = y; }
  void setSwipe(int sx, int sy) { swipeX = sx; swipeY = sy; }
  void applyInertia(float ax, float ay);  // gravity-free device acceleration, m/s^2
  void impact(float ax, float ay);        // a hard jolt: throw the eyes the other way
  // Being shaken right now, 0 (still) .. 1 (hard). Called every IMU sample
  // while shaking; the eyes rattle, widen and their pupils jiggle, then settle.
  void shake(float level) { shakeLevel = fmaxf(shakeLevel, anim::clampf(level, 0.0f, 1.3f)); }
  void setDrowsiness(float d) { drowsy = anim::clamp01(d); }
  void setIdleActsAllowed(bool allowed) { idleActsAllowed = allowed; }

  // ---- frame ----
  void update(float dt, uint32_t now);
  float backlight() const { return blLevel; } // 0..1, perceptual
  Mood mood() const { return current; }

 private:
  Eyes *eyes = nullptr;
  uint32_t seed = 0;
  float clock = 0;

  // Mood state
  Mood current = Mood::IDLE, previous = Mood::IDLE;
  uint32_t moodStart = 0, moodUntil = 0;
  int32_t age = 0, prevAge = -1;   // ms since the mood began (this / last frame)
  float intensity = 1;
  Mood queued = Mood::IDLE;
  uint32_t queuedAt = 0, queuedDuration = 0;
  int queuedParam = 0;
  Mood batteryReturn = Mood::IDLE;
  uint32_t batteryReturnRemaining = 0;
  float batteryAmount = .55f;
  float impactX = 1, impactY = 0;
  uint32_t angrySquintUntil = 0, nextAngrySquint = 0, nextDartAt = 0;
  float dartX = 0, dartY = 0;

  // Scripted sequences (wake, sleep, nap)
  uint8_t wakeVariant = 0;
  float wakeScale = 1, wakeGazeScale = 1;
  bool wakeSwap = false;
  float napLevel = 0;
  uint32_t nextPeekAt = 0, peekUntil = 0;
  uint8_t peekKind = 0;
  float scriptGX = 0, scriptGY = 0, scriptGazeSpeed = 1;

  // Inputs and effects
  float tiltX = 0, tiltY = 0;
  float pointerX = 0, pointerY = 0;
  uint32_t pointerAt = 0, holdSince = 0;
  bool pointerHeld = false, upsideDown = false;
  uint32_t upsideSince = 0;
  float holdAnnoy = 0;
  float angerResidue = 1, grumpy = 0;
  int touchX = 120, touchY = 120, swipeX = 0, swipeY = 0;
  float shakeLevel = 0;
  float inertiaX = 0, inertiaY = 0;
  int8_t boopFar = -1;
  uint32_t nextIdleAct = 0;
  bool idleActsAllowed = true;
  float drowsy = 0;
  float blLevel = 0;

  bool activeAt(uint32_t now) const;
  bool crossed(int32_t ms) const { return prevAge < ms && age >= ms; }
  bool napping() const { return current == Mood::SLEEPY && napLevel > 0 && age >= int32_t(SLEEP_SEQUENCE_MS); }
  void enterMood(Mood m, uint32_t now, uint32_t durationMs, float strength = 1);
  void updateMoodTimeline(uint32_t now);
  void updateFace(float dt, uint32_t now);
  void updateGaze(uint32_t now);
  void updateBody();
  void updateEffects(float dt);
  void updateBacklight(float dt);
  void wakeScript(uint32_t now, Expression &e, float &k, float *open, float &openSpeed);
  void sleepScript(uint32_t now, Expression &e, float &k, float *open, float &openSpeed);
};
