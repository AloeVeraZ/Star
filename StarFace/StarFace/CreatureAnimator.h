#pragma once
#include <Arduino.h>
#include "AnimMath.h"
#include "EyeRenderer.h"

// Layout, motion limits and feel are tuned in FaceConfig.h.
static constexpr uint32_t DIZZY_ANIM_MS = 2850;
static constexpr uint32_t ANGRY_ANIM_MS = 1900;
static constexpr uint32_t BATTERY_SHOW_MS = 2600;
static constexpr uint32_t SLEEP_SEQUENCE_MS = 5400;

enum Mood : uint8_t {
  IDLE, HAPPY, SAD, ANGRY, DIZZY, SLEEPY, SURPRISED, ANXIOUS,
  WAKE_UP, BATTERY, BOOP, PETTED, SWIPING, FOLLOWING, CONFUSED, SHY,
  SHIVER, LOOK_CHANGE
};

// Layered, spring-driven eye animation:
//   expression pose (springs) + gaze (fast pupil springs, slower eye-travel
//   springs) + saccades + blink + micro motion (noise) + body physics
//   (springs, inertia) + effects (dizzy, shiver) + glow/colour mood
// Moods only change targets; the springs blend every state into every other.
// The final pose is squashed/stretched by velocity, given a slight perspective
// turn, and softly fitted inside the round screen's safe radius.
class CreatureAnimator {
 public:
  typedef anim::Spring Spring;

  // look: the eye style, which also sets the personality.
  void begin(EyeRenderer *renderer, uint8_t look, uint32_t seed, uint32_t now);

  // ---- reactions ----
  // direct = caused by the user; it may interrupt anything except a dizzy spell.
  bool react(Mood m, uint32_t now, uint32_t durationMs, bool direct = true);
  // Self-started behaviour; never overrides an active reaction.
  bool reactPassive(Mood m, uint32_t now, uint32_t durationMs);
  void queue(Mood m, uint32_t at, uint32_t durationMs, int param = 0);
  void showBattery(int percent, uint32_t now);
  // Swaps eye shape and colors; applied while the eyes are shut during LOOK_CHANGE.
  void changeLook(uint8_t look, bool instant);
  // fromShake plays a startled wake into dizziness; touched favors a startled wake.
  void startWake(uint32_t now, bool fromShake, bool touched = false);
  // napLevel > 0: fall asleep but keep the screen glowing at that backlight level.
  void startSleep(uint32_t now, float napLevel);
  bool sleepFinished(uint32_t now) const;
  bool asleep() const { return current == SLEEPY; }

  // ---- inputs ----
  void setTilt(float x, float y) { tiltX = x; tiltY = y; }
  void setPointer(float x, float y, uint32_t now);
  // A finger is on the screen. The longer it stays, the angrier it gets.
  void setPointerHeld(bool held, uint32_t now) {
    if (held && !pointerHeld) holdSince = now;
    pointerHeld = held;
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
  const EyeGeom *eyes() const { return geom; }
  float backlight() const { return blLevel; } // 0..1, perceptual
  Mood mood() const { return current; }

 private:
  enum Channel : uint8_t {
    P_DX, P_DY, P_W, P_H, P_OPEN, P_ANGLE, P_DROP, P_LOWER, P_PUPIL, P_BEND, P_COUNT
  };
  enum BlinkType : uint8_t {
    B_NORMAL, B_FAST, B_LONG, B_HALF, B_SLEEPY, B_SQUEEZE, B_UNEVEN
  };
  struct Blink {
    bool active = false;
    uint32_t start = 0;
    uint16_t closeMs = 90, holdMs = 30, openMs = 130;
    uint16_t lag[2] = {0, 0};
    float depth[2] = {1, 1};
  };

  EyeRenderer *renderer = nullptr;
  EyeGeom geom[2];
  uint8_t personality = 0;
  uint32_t seed = 0;
  float clock = 0;                 // seconds, drives noise and oscillators
  float breathPhase = 0, breathAmp = .75f;

  // Mood / reaction state
  Mood current = IDLE, previous = IDLE;
  uint32_t moodStart = 0, moodUntil = 0;
  int32_t age = 0, prevAge = -1;   // ms since the mood began (this / last frame)
  float intensity = 1;
  Mood queued = IDLE;
  uint32_t queuedAt = 0, queuedDuration = 0;
  int queuedParam = 0;
  Mood batteryReturn = IDLE;
  uint32_t batteryReturnRemaining = 0;
  float batteryAmount = .55f;
  bool lookPending = false;
  uint8_t pendingLook = 0;
  float impactX = 1, impactY = 0;
  uint32_t angrySquintUntil = 0, nextAngrySquint = 0;
  uint32_t nextDartAt = 0;
  float dartX = 0, dartY = 0;

  // Scripted sequences (wake, sleep, nap)
  uint8_t wakeVariant = 0;
  float wakeScale = 1, wakeGazeScale = 1;
  bool wakeSwap = false;
  float napLevel = 0;              // backlight kept while napping; 0 = full power-down fade
  uint32_t nextPeekAt = 0, peekUntil = 0;
  uint8_t peekKind = 0;
  float scriptGX = 0, scriptGY = 0, scriptGazeFreq = 3;

  // Expression layer
  Spring expr[2][P_COUNT];
  Spring baseW, baseH, baseIris;

  // Gaze layer: pupils lead (gaze), the eye bodies follow a beat later (travel)
  Spring gazeX[2], gazeY[2];
  Spring travelX[2], travelY[2];
  Spring sacX, sacY;
  float idleGX = 0, idleGY = 0, savedGX = 0, savedGY = 0;
  uint32_t nextGazeAt = 0, nextSaccadeAt = 0;
  bool glancing = false, staring = false, fastGaze = false;

  // Physics / effect layers
  Spring bodyX, bodyY;
  Spring fxX[2], fxY[2];           // procedural eye offsets (dizzy, shiver), px
  Spring pfxX[2], pfxY[2];         // procedural pupil offsets
  float inertiaX = 0, inertiaY = 0;
  float shakeLevel = 0;            // live rattle while being shaken, decays when it stops
  float tiltX = 0, tiltY = 0, leanSm = 0;
  int8_t boopFar = -1;             // eye that flinches second after a boop

  // Blink layer
  Blink blink;
  uint32_t nextBlinkAt = 0, secondBlinkAt = 0;
  float closure[2] = {0, 0};

  // Idle behaviour
  uint32_t nextIdleAct = 0, nextQuirkAt = 0, quirkUntil = 0;
  uint8_t quirk = 0;
  bool idleActsAllowed = true;
  float drowsy = 0;
  float asymDrop[2] = {0, 0}, asymOpen[2] = {0, 0};

  // Touch context
  float pointerX = 0, pointerY = 0;
  uint32_t pointerAt = 0, holdSince = 0;
  bool pointerHeld = false;
  float holdAnnoy = 0;
  float angerResidue = 1;          // grumpiness left when an angry spell ends
  int touchX = 120, touchY = 120;
  int swipeX = 0, swipeY = 0;

  // Glow and colour mood
  Spring glowS;
  // Shape-shifting pupils: hearts when happy, a twinkling star glint
  Spring heartS;
  float twinkleBurst = 0, twinkleSpin = 0;
  uint32_t nextTwinkleAt = 0;
  float heat = 0;
  float grumpy = 0;                // lingering annoyance after anger, 1 -> 0

  // Backlight 0..1
  float blLevel = 0;

  bool activeAt(uint32_t now) const;
  bool crossed(int32_t ms) const { return prevAge < ms && age >= ms; }
  bool napping() const { return current == SLEEPY && napLevel > 0 && age >= int32_t(SLEEP_SEQUENCE_MS); }
  void enterMood(Mood m, uint32_t now, uint32_t durationMs, float strength = 1);
  void applyLook(uint8_t look, bool instant);

  void updateMoodTimeline(uint32_t now);
  void updateIdle(float dt, uint32_t now);
  void updateBlink(uint32_t now);
  void updateExpression(float dt, uint32_t now);
  void updateGaze(float dt, uint32_t now);
  void updatePhysics(float dt, uint32_t now);
  void updateShakeReaction(float dt);
  void updateBacklight(float dt);
  void updateMoodColour(float dt);
  void compose();
  void fitToCircle(EyeGeom &g, float baseW, float baseH);

  bool blinkAllowed() const;
  void scheduleBlink(uint32_t now);
  void startBlink(uint32_t now, BlinkType type);
  float blinkCurve(int32_t ms) const;
  void pickIdleGaze(uint32_t now);
  void wakeTargets(float *L, float *R, float &freq, float &zeta, uint32_t now);
  void sleepTargets(float *L, float *R, float &freq, float &zeta, uint32_t now);
};
