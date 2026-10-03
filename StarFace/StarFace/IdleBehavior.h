#pragma once
#include "AnimMath.h"
#include "Expressions.h"
#include "FaceConfig.h"

// What the eyes do when nobody tells them anything: look around (mostly small
// shifts, sometimes far, now and then a quick glance that comes back), hold
// some looks longer than others, and show brief micro-expressions (a squint,
// a curious lift, a sceptical look). Breathing is a slow, tiny drift.
class IdleBehavior {
 public:
  void begin(uint32_t now) {
    nextGazeAt = now + 1200;
    nextQuirkAt = now + anim::randMs(6000, 12000);
  }

  // Returns true when it picked a new place to look (gazeX/gazeY/fast).
  // bigJump is set when the eyes are about to travel far (a good time to blink).
  bool update(uint32_t now, float drowsy, bool &bigJump) {
    bigJump = false;
    if (quirk != NEUTRAL && int32_t(now - quirkUntil) >= 0) quirk = NEUTRAL;
    if (IDLE_LIVELINESS > 0 && quirk == NEUTRAL && int32_t(now - nextQuirkAt) >= 0) {
      static const Expression QUIRKS[] = {SQUINT, CURIOUS, SUSPICIOUS, HAPPY};
      quirk = QUIRKS[int(anim::frand() * 4) & 3];
      quirkAmount = anim::frand(.25f, .45f);
      quirkUntil = now + anim::randMs(600, 1400);
      nextQuirkAt = now + uint32_t(anim::randMs(8000, 17000) / IDLE_LIVELINESS);
    }
    if (int32_t(now - nextGazeAt) < 0) return false;
    pick(now, drowsy, bigJump);
    return true;
  }

  // Forget the current look (an explicit lookAt took over).
  void interrupt(uint32_t now, uint32_t quietMs) {
    nextGazeAt = now + quietMs;
    glancing = false;
    quirk = NEUTRAL;
  }

  float gazeX = 0, gazeY = 0;  // where it wants to look, -1..1
  bool fast = false;           // a quick glance
  Expression quirk = NEUTRAL;  // current micro-expression (NEUTRAL = none)
  float quirkAmount = 0;

 private:
  uint32_t nextGazeAt = 0, nextQuirkAt = 0, quirkUntil = 0;
  float savedX = 0, savedY = 0;
  bool glancing = false;

  void pick(uint32_t now, float drowsy, bool &bigJump) {
    using anim::frand;
    using anim::randMs;
    if (glancing) { // a quick glance always comes back
      glancing = false;
      fast = true;
      gazeX = savedX;
      gazeY = savedY;
      nextGazeAt = now + randMs(900, 2600);
      return;
    }
    float r = frand(), nx, ny;
    fast = false;
    const float live = anim::clampf(IDLE_LIVELINESS, 0.0f, 1.5f);
    if (r < .10f + .3f * (1.0f - live)) {
      nx = ny = 0;
    } else if (r < .52f) {
      float a = frand(0, anim::TAU_F), d = frand(.10f, .30f);
      nx = gazeX * .6f + cosf(a) * d;
      ny = gazeY * .6f + sinf(a) * d;
    } else if (r < .78f) {
      float a = frand(0, anim::TAU_F), d = frand(.35f, .6f);
      nx = cosf(a) * d;
      ny = sinf(a) * d;
    } else {
      // Far: one of eight directions, with a little variation.
      float a = int(frand() * 8) * (anim::TAU_F / 8) + frand(-.2f, .2f), d = frand(.7f, .95f);
      nx = cosf(a) * d;
      ny = sinf(a) * d;
      if (r >= .90f) {
        savedX = gazeX;
        savedY = gazeY;
        glancing = true;
        fast = true;
      }
    }
    nx = anim::clampf(nx * live, -.95f, .95f);
    ny = anim::clampf(ny * .8f * live, -.8f, .8f);
    float jump = sqrtf((nx - gazeX) * (nx - gazeX) + (ny - gazeY) * (ny - gazeY));
    bigJump = jump > .55f;
    gazeX = nx;
    gazeY = ny;
    uint32_t hold;
    if (glancing) {
      hold = randMs(220, 480);
    } else {
      float p = frand();
      hold = p < .6f ? randMs(700, 2500) : p < .9f ? randMs(2500, 4500) : randMs(4500, 7500);
      if (live > 0) hold = uint32_t(hold / fmaxf(.4f, live));
    }
    nextGazeAt = now + uint32_t(hold * (1.0f + drowsy));
  }
};
