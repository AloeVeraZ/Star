#include "CreatureAnimator.h"
#include <string.h>

using namespace anim;

namespace {

// The calm rest pose: dx, dy, width, height, open, lid angle, lid drop, lower lid,
// pupil, bend. Pixel channels are authored for a 48 px half-height eye and are
// scaled by EYE_PX_SCALE in compose(), so expressions grow with the eyes.
const float IDLE_POSE[] = {0, 0, 1, 1, 1.0f, .045f, 6, 0, 1, 0};
// Some channels react a little faster than others (blinky lids, quick pupils).
const float CHANNEL_RATE[] = {1, 1, 1.1f, 1.1f, 1.25f, .9f, .9f, 1, 1.2f, .9f};

// ---- Wake-up choreography. Each key sets new targets; springs do the motion. ----
struct WakeKey {
  uint16_t t;                  // ms from wake start (scaled per wake)
  int8_t leftOpen, rightOpen;  // percent
  int8_t gazeX, gazeY;         // percent
  uint8_t flags;
};
enum : uint8_t { WK_SLOW = 1, WK_FAST = 2, WK_BLINK = 4, WK_SQUINT = 8 };

const WakeKey WAKE_CLASSIC[] = {
  {0, 2, 2, 0, 0, 0},                  // screen lights with the eyes shut
  {420, 30, 3, 0, 0, WK_SLOW},         // one eyelid cracks open
  {820, 32, 4, 0, 0, 0},               // ...pause
  {1050, 96, 92, 0, 6, WK_SLOW},       // both eyes slowly open
  {1800, 100, 98, -45, -6, 0},         // look a little left
  {2200, 100, 100, 52, -6, WK_FAST},   // quick look right
  {2550, 100, 100, 0, 0, 0},           // back to the viewer
  {2850, 100, 100, 0, 0, WK_BLINK},    // small blink
  {3200, 100, 100, 0, 0, 0},
};
const WakeKey WAKE_ONE_EYE[] = {
  {0, 2, 2, 0, 0, 0},
  {350, 45, 2, -15, 0, WK_SLOW},       // one eye first
  {820, 60, 4, -25, 5, 0},
  {1150, 100, 52, 10, -8, WK_SQUINT},  // confused squint
  {1800, 100, 70, 35, -10, 0},
  {2200, 100, 100, 0, 0, WK_SLOW},
  {2450, 100, 100, 0, 0, WK_BLINK},
  {2800, 100, 100, 0, 0, 0},
};
const WakeKey WAKE_STARTLED[] = {
  {0, 2, 2, 0, 0, 0},
  {200, 116, 116, 0, 0, WK_FAST},      // pops wide open
  {470, 108, 108, -55, 0, WK_FAST},
  {690, 106, 106, 55, -5, WK_FAST},
  {920, 100, 100, 0, 0, 0},
  {1180, 100, 100, 0, 0, WK_BLINK},
  {1500, 100, 100, 0, 0, 0},
};
const WakeKey WAKE_SLEEPY[] = {
  {0, 2, 2, 0, 10, 0},
  {450, 42, 36, 0, 15, WK_SLOW},
  {1150, 22, 17, 0, 15, WK_SLOW},      // nods off again for a moment
  {1700, 70, 64, -20, 10, WK_SLOW},
  {2400, 100, 98, -35, 0, WK_SLOW},
  {2900, 100, 100, 25, -4, 0},
  {3300, 100, 100, 0, 0, WK_BLINK},
  {3650, 100, 100, 0, 0, 0},
};
struct WakeScript { const WakeKey *keys; uint8_t count; };
#define WAKE_ENTRY(a) {a, uint8_t(sizeof(a) / sizeof(a[0]))}
const WakeScript WAKE_SCRIPTS[] = {
  WAKE_ENTRY(WAKE_CLASSIC), WAKE_ENTRY(WAKE_ONE_EYE),
  WAKE_ENTRY(WAKE_STARTLED), WAKE_ENTRY(WAKE_SLEEPY)
};
constexpr uint8_t WAKE_STARTLED_INDEX = 2;

// Sleep sequence landmarks, ms.
constexpr int32_t SLEEP_BLINK_AT = 1400;
constexpr int32_t SLEEP_DROOP_AT = 2900;
constexpr int32_t SLEEP_CLOSE_AT = 3500;
constexpr int32_t SLEEP_FADE_AT = 4700;
constexpr int32_t SLEEP_FADE_MS = SLEEP_SEQUENCE_MS - SLEEP_FADE_AT;

constexpr float INERTIA_GAIN = 220.0f; // px/s^2 of eye push per m/s^2 of device acceleration

uint8_t priorityOf(Mood m) {
  switch (m) {
    case DIZZY: return 6;
    case SURPRISED: return 5;
    case ANGRY: case BOOP: case ANXIOUS: case SWIPING: case FOLLOWING:
    case PETTED: case BATTERY: case LOOK_CHANGE: return 4;
    case HAPPY: case SAD: case SHY: case CONFUSED: return 3;
    case SHIVER: case WAKE_UP: return 2;
    case SLEEPY: return 1;
    default: return 0;
  }
}

// The dizzy orbit starts fast and winds down.
float dizzyPhase(float t) {
  float s = fmaxf(0.0f, t - .35f);
  return TAU_F * (2.1f * s - .35f * s * s);
}

void setPose(float *p, float dx, float dy, float w, float h, float open,
             float angle, float drop, float lower, float pupil, float bend = 0) {
  p[0] = dx; p[1] = dy; p[2] = w; p[3] = h; p[4] = open;
  p[5] = angle; p[6] = drop; p[7] = lower; p[8] = pupil; p[9] = bend;
}

} // namespace

// ======================= setup & public API =======================

void CreatureAnimator::begin(EyeRenderer *r, uint8_t look, uint8_t palette,
                             uint8_t pers, uint32_t unitSeed, uint32_t now) {
  renderer = r;
  personality = pers;
  seed = unitSeed;
  // Stable per-unit asymmetry: one lid sits a little lower, one eye opens a hair less.
  int lowLid = seed & 1;
  asymDrop[lowLid] = .8f + 1.2f * ((seed >> 3) & 15) / 15.0f;
  asymDrop[1 - lowLid] = 0;
  int narrow = (seed >> 8) & 1;
  asymOpen[narrow] = -.012f - .018f * ((seed >> 9) & 7) / 7.0f;
  asymOpen[1 - narrow] = 0;
  for (int i = 0; i < 2; ++i) {
    for (int c = 0; c < P_COUNT; ++c) expr[i][c].snap(IDLE_POSE[c]);
    expr[i][P_OPEN].snap(.02f);
  }
  applyLook(look, palette, true);
  glowS.snap(1);
  blLevel = 0;
  current = previous = IDLE;
  moodStart = moodUntil = now;
  nextBlinkAt = now + 3000;
  nextGazeAt = now + 1500;
  nextSaccadeAt = now + 600;
  nextIdleAct = now + randMs(12000, 19000);
  nextQuirkAt = now + randMs(7000, 14000);
  compose();
}

bool CreatureAnimator::activeAt(uint32_t now) const {
  return current != IDLE && (current == SLEEPY || int32_t(now - moodUntil) < 0);
}

bool CreatureAnimator::react(Mood m, uint32_t now, uint32_t durationMs, bool direct) {
  if (activeAt(now) && current == DIZZY) {
    if (m != DIZZY) return false; // the shake reaction always plays through
    if (age < 1600) return true;  // still spinning; impact() already added the jolt
  }
  if (!direct && activeAt(now) && priorityOf(m) < priorityOf(current)) return false;
  queued = IDLE;
  enterMood(m, now, durationMs);
  return true;
}

bool CreatureAnimator::reactPassive(Mood m, uint32_t now, uint32_t durationMs) {
  if (activeAt(now)) return false;
  enterMood(m, now, durationMs, frand(.6f, .85f)); // self-started moods are subtler
  return true;
}

void CreatureAnimator::queue(Mood m, uint32_t at, uint32_t durationMs, int param) {
  queued = m;
  queuedAt = at;
  queuedDuration = durationMs;
  queuedParam = param;
}

void CreatureAnimator::showBattery(int percent, uint32_t now) {
  batteryAmount = percent < 0 ? .55f : .17f + .83f * constrain(percent, 0, 100) / 100.0f;
  bool resume = activeAt(now) && current != SWIPING && current != BATTERY &&
                current != SLEEPY && current != WAKE_UP && current != DIZZY;
  batteryReturn = resume ? current : IDLE;
  batteryReturnRemaining = resume ? moodUntil - now : 0;
  queued = IDLE;
  enterMood(BATTERY, now, BATTERY_SHOW_MS);
}

void CreatureAnimator::changeLook(uint8_t look, uint8_t palette, bool instant) {
  if (!instant && current == LOOK_CHANGE && age < 160) {
    pendingLook = look;
    pendingPalette = palette;
    lookPending = true;
  } else {
    applyLook(look, palette, instant);
  }
}

void CreatureAnimator::applyLook(uint8_t look, uint8_t palette, bool instant) {
  if (renderer) renderer->setPalette(palette);
  baseW.target = lookHalfWidth(look);
  baseH.target = lookHalfHeight(look);
  baseIris.target = lookIris(look);
  if (instant) {
    baseW.snap(baseW.target);
    baseH.snap(baseH.target);
    baseIris.snap(baseIris.target);
  }
}

void CreatureAnimator::startWake(uint32_t now, bool fromShake, bool touched) {
  if (fromShake) {
    wakeVariant = WAKE_STARTLED_INDEX;
    wakeScale = .8f;
  } else {
    // A touch tends to startle; otherwise it wakes in one of several ways.
    float r = frand();
    if (touched) wakeVariant = r < .4f ? WAKE_STARTLED_INDEX : r < .7f ? 0 : r < .85f ? 1 : 3;
    else wakeVariant = r < .45f ? 0 : r < .65f ? 1 : r < .80f ? 2 : 3;
    wakeScale = frand(.88f, 1.15f);
  }
  wakeGazeScale = frand(.8f, 1.2f) * (chance(.5f) ? 1.0f : -1.0f);
  wakeSwap = chance(.5f);
  for (int i = 0; i < 2; ++i) {
    // Continue from wherever the lids are; only a fully dark start snaps shut.
    if (blLevel <= .01f) expr[i][P_OPEN].snap(.02f);
    gazeX[i].target = gazeY[i].target = 0;
  }
  secondBlinkAt = 0;
  napLevel = 0;
  const WakeScript &s = WAKE_SCRIPTS[wakeVariant];
  uint32_t duration = uint32_t(s.keys[s.count - 1].t * wakeScale);
  queued = IDLE;
  enterMood(WAKE_UP, now, duration);
  if (fromShake) queue(DIZZY, moodUntil, DIZZY_ANIM_MS);
}

void CreatureAnimator::startSleep(uint32_t now, float nap) {
  queued = IDLE;
  napLevel = clamp01(nap);
  nextPeekAt = now + SLEEP_SEQUENCE_MS + randMs(9000, 20000);
  peekUntil = 0;
  enterMood(SLEEPY, now, SLEEP_SEQUENCE_MS);
}

bool CreatureAnimator::sleepFinished(uint32_t now) const {
  return current == SLEEPY && now - moodStart >= SLEEP_SEQUENCE_MS;
}

void CreatureAnimator::setPointer(float x, float y, uint32_t now) {
  pointerX = clampf(x, -1.0f, 1.0f);
  pointerY = clampf(y, -1.0f, 1.0f);
  pointerAt = now;
}

void CreatureAnimator::applyInertia(float ax, float ay) {
  // Soft dead band: sensor noise and gentle handling leave the eyes alone.
  auto band = [](float v) {
    float a = fabsf(v) - .7f;
    return a <= 0 ? 0.0f : copysignf(fminf(a, 20.0f), v);
  };
  inertiaX = band(ax);
  inertiaY = band(ay);
}

void CreatureAnimator::impact(float ax, float ay) {
  float mag = sqrtf(ax * ax + ay * ay);
  if (mag < .01f) return;
  impactX = ax / mag;
  impactY = ay / mag;
  float strength = clampf(mag / 15.0f, .4f, 1.6f);
  bodyX.kick(-impactX * 220.0f * strength);
  bodyY.kick(-impactY * 220.0f * strength);
}

void CreatureAnimator::enterMood(Mood m, uint32_t now, uint32_t durationMs, float strength) {
  previous = current;
  current = m;
  // Anger never just switches off: the lids stay a little heavy and the colour
  // a little warm, cooling over ANGER_COOLDOWN_S.
  if (previous == ANGRY && m != ANGRY) grumpy = 1;
  moodStart = now;
  moodUntil = now + durationMs;
  age = 0;
  prevAge = -1;
  intensity = frand(.88f, 1.08f) * strength;
  switch (m) {
    case BOOP: {
      // The eye nearer the finger flinches first; the other follows in updatePhysics.
      int nearEye = touchX < 120 ? 0 : 1;
      boopFar = 1 - nearEye;
      expr[nearEye][P_OPEN].kick(-7.0f);
      expr[nearEye][P_W].kick(1.6f);
      float dx = clampf((touchX - 120) / 100.0f, -1.0f, 1.0f);
      float dy = clampf((touchY - 120) / 100.0f, -1.0f, 1.0f);
      bodyX.kick(-dx * 90.0f);
      bodyY.kick(-dy * 70.0f);
      break;
    }
    case DIZZY:
      // Stage 1, impact: squash and a small extra recoil on top of impact().
      for (int i = 0; i < 2; ++i) {
        expr[i][P_H].kick(-2.2f);
        expr[i][P_W].kick(1.4f);
      }
      bodyX.kick(-impactX * 120.0f);
      bodyY.kick(-impactY * 120.0f);
      queue(ANGRY, now + DIZZY_ANIM_MS - 50, ANGRY_ANIM_MS);
      break;
    case ANGRY:
      nextAngrySquint = now + randMs(700, 1300);
      angrySquintUntil = 0;
      break;
    case ANXIOUS:
      nextDartAt = now;
      break;
    case SURPRISED:
      glowS.kick(2.4f); // the glow flares as the eyes pop open
      break;
    default:
      break;
  }
}

// ======================= frame update =======================

void CreatureAnimator::update(float dt, uint32_t now) {
  dt = clampf(dt, .0005f, .05f); // a lag spike never turns into a jump
  clock += dt;
  // Breathing: barely there while awake, slower and deeper while asleep.
  bool deep = current == SLEEPY && age > SLEEP_CLOSE_AT;
  breathPhase = fmodf(breathPhase + TAU_F * dt / (deep ? 4.8f : 3.7f), TAU_F);
  breathAmp = approach(breathAmp, deep ? 1.5f : .75f, 1.0f, dt);
  // ANIMATION_SPEED scales spring time (same overshoot, faster or slower
  // settle); scripted timelines and blinks keep real time.
  const float sdt = dt * ANIMATION_SPEED;
  updateMoodTimeline(now);
  updateIdle(sdt, now);
  updateBlink(now);
  updateExpression(sdt, now);
  updateGaze(sdt, now);
  updatePhysics(sdt, now);
  updateShakeReaction(sdt);
  updateMoodColour(dt);
  updateBacklight(dt);
  compose();
  prevAge = age;
}

void CreatureAnimator::updateMoodTimeline(uint32_t now) {
  if (queued != IDLE && int32_t(now - queuedAt) >= 0) {
    Mood m = queued;
    queued = IDLE;
    if (m == BATTERY) showBattery(queuedParam, now);
    else enterMood(m, now, queuedDuration);
  }
  if (current != IDLE && current != SLEEPY && int32_t(now - moodUntil) >= 0) {
    if (current == BATTERY && batteryReturnRemaining) {
      uint32_t remaining = batteryReturnRemaining;
      batteryReturnRemaining = 0;
      enterMood(batteryReturn, now, remaining);
    } else {
      enterMood(IDLE, now, 0);
    }
  }
  age = int32_t(now - moodStart);
}

// ---- Idle controller: gaze wandering, saccades, micro-expressions, habits ----

void CreatureAnimator::pickIdleGaze(uint32_t now) {
  if (glancing) { // a quick glance always comes back
    glancing = false;
    fastGaze = true;
    idleGX = savedGX;
    idleGY = savedGY;
    nextGazeAt = now + randMs(900, 2600);
    return;
  }
  // Mostly small shifts, sometimes medium, rarely far or a quick glance.
  float r = frand(), nx, ny;
  fastGaze = false;
  if (r < .10f) {
    nx = ny = 0;
  } else if (r < .52f) {
    float a = frand(0, TAU_F), d = frand(.10f, .30f);
    nx = idleGX * .6f + cosf(a) * d;
    ny = idleGY * .6f + sinf(a) * d;
  } else if (r < .78f) {
    float a = frand(0, TAU_F), d = frand(.35f, .6f);
    nx = cosf(a) * d;
    ny = sinf(a) * d;
  } else {
    // Far: one of the eight directions, with a little variation.
    float a = int(frand() * 8) * (TAU_F / 8) + frand(-.2f, .2f), d = frand(.7f, .95f);
    nx = cosf(a) * d;
    ny = sinf(a) * d;
    if (r >= .90f) {
      savedGX = idleGX;
      savedGY = idleGY;
      glancing = true;
      fastGaze = true;
    }
  }
  nx = clampf(nx, -.95f, .95f);
  ny = clampf(ny * .8f, -.8f, .8f);
  float jump = sqrtf((nx - idleGX) * (nx - idleGX) + (ny - idleGY) * (ny - idleGY));
  idleGX = nx;
  idleGY = ny;
  uint32_t hold;
  if (glancing) {
    hold = randMs(220, 480);
  } else {
    float p = frand();
    hold = p < .6f ? randMs(700, 2500) : p < .9f ? randMs(2500, 4500) : randMs(4500, 7500);
  }
  staring = !glancing && hold > 4500;
  if (current == HAPPY) hold = hold * 6 / 10;
  nextGazeAt = now + uint32_t(hold * (1.0f + drowsy));
  // Big gaze shifts often carry a blink, as they do in people.
  if (jump > .55f && !blink.active && blinkAllowed() && chance(.3f)) startBlink(now, B_NORMAL);
}

void CreatureAnimator::updateIdle(float dt, uint32_t now) {
  if (int32_t(now - nextGazeAt) >= 0) pickIdleGaze(now);

  // Saccades: tiny, fast 1-3 px hops while looking at something.
  bool sacAllowed = current != DIZZY && current != SLEEPY && current != BATTERY &&
                    current != LOOK_CHANGE && !(current == WAKE_UP && age < 900);
  if (int32_t(now - nextSaccadeAt) >= 0) {
    bool tense = current == ANGRY || current == ANXIOUS;
    if (sacAllowed && chance(.75f)) {
      float a = frand(0, TAU_F), d = frand(1.0f, 3.0f) / 12.0f;
      if (tense) d *= .7f;
      sacX.target = cosf(a) * d;
      sacY.target = sinf(a) * d * .7f;
    } else {
      sacX.target = sacY.target = 0;
    }
    uint32_t gap = tense ? randMs(110, 320) : staring ? randMs(600, 1700) : randMs(300, 1200);
    nextSaccadeAt = now + uint32_t(gap * (1.0f + drowsy * 1.2f));
  }
  if (!sacAllowed) sacX.target = sacY.target = 0;
  sacX.update(dt, 16, .85f);
  sacY.update(dt, 16, .85f);

  if (current != IDLE) { quirk = 0; return; }
  // Occasional idle micro-expressions: squint, curious lift, relaxed softening.
  if (quirk && int32_t(now - quirkUntil) >= 0) quirk = 0;
  if (!quirk && int32_t(now - nextQuirkAt) >= 0) {
    quirk = 1 + uint8_t(frand() * 3);
    quirkUntil = now + randMs(500, 1300);
    nextQuirkAt = now + randMs(7000, 16000);
  }
  // A stable, chip-specific personality picks small self-started performances.
  if (idleActsAllowed && int32_t(now - nextIdleAct) >= 0) {
    Mood act = personality == 0 ? SHY : personality == 1 ? HAPPY : CONFUSED;
    if (chance(.125f)) act = SURPRISED;
    reactPassive(act, now, randMs(480, 830));
    nextIdleAct = now + randMs(12000, 21000);
  }
}

// ---- Blink controller ----

bool CreatureAnimator::blinkAllowed() const {
  switch (current) {
    case DIZZY: case BATTERY: case LOOK_CHANGE: case WAKE_UP:
    case SLEEPY: case SWIPING: case BOOP:
      return false;
    case SURPRISED:
      return age > 700;
    default:
      return true;
  }
}

void CreatureAnimator::scheduleBlink(uint32_t now) {
  // Weighted toward the shorter end of BLINK_MIN_MS..BLINK_MAX_MS.
  float interval = BLINK_MIN_MS + float(BLINK_MAX_MS - BLINK_MIN_MS) * powf(frand(), 1.5f);
  if (grumpy > .3f) interval *= 1.2f; // a sulky stare
  switch (current) {
    case SAD: interval *= 1.35f; break;
    case ANGRY: interval *= 1.25f; break;
    case ANXIOUS: interval *= .45f; break;
    case HAPPY: interval *= .9f; break;
    case PETTED: interval *= 1.3f; break;
    default: break;
  }
  nextBlinkAt = now + uint32_t(interval * (1.0f + .5f * drowsy));
}

void CreatureAnimator::startBlink(uint32_t now, BlinkType type) {
  Blink &b = blink;
  b.active = true;
  b.start = now;
  float depth = 1.0f;
  switch (type) {
    case B_FAST:    b.closeMs = randMs(50, 65);   b.holdMs = randMs(8, 20);    b.openMs = randMs(70, 95);   break;
    case B_LONG:    b.closeMs = randMs(80, 110);  b.holdMs = randMs(160, 320); b.openMs = randMs(150, 210); break;
    case B_HALF:    b.closeMs = randMs(60, 90);   b.holdMs = randMs(0, 30);    b.openMs = randMs(90, 130);
                    depth = frand(.45f, .65f); break;
    case B_SLEEPY:  b.closeMs = randMs(260, 380); b.holdMs = randMs(250, 500); b.openMs = randMs(420, 650); break;
    case B_SQUEEZE: b.closeMs = randMs(90, 120);  b.holdMs = randMs(60, 110);  b.openMs = randMs(140, 190); break;
    default:        b.closeMs = randMs(70, 110);  b.holdMs = randMs(20, 50);   b.openMs = randMs(100, 160); break;
  }
  if (current == SAD || drowsy > .5f) {
    b.closeMs = b.closeMs * 4 / 3; b.holdMs = b.holdMs * 4 / 3; b.openMs = b.openMs * 4 / 3;
  }
  // One eye always leads by a few ms and closes a hair less deep.
  int lead = chance(.5f) ? 0 : 1;
  b.lag[lead] = 0;
  b.lag[1 - lead] = type == B_UNEVEN ? randMs(18, 40) : randMs(0, 14);
  b.depth[lead] = depth * frand(.97f, 1.0f);
  b.depth[1 - lead] = depth * (type == B_UNEVEN ? frand(.82f, .93f) : frand(.95f, 1.0f));
}

// Lid closure over time: quick start that accelerates shut, a brief hold,
// a slower decelerating reopen, then a tiny lift past rest that settles.
float CreatureAnimator::blinkCurve(int32_t ms) const {
  if (ms <= 0) return 0;
  if (ms < blink.closeMs) {
    float u = ms / float(blink.closeMs);
    return .3f * u + .7f * u * u;
  }
  ms -= blink.closeMs;
  if (ms < blink.holdMs) return 1;
  ms -= blink.holdMs;
  if (ms < blink.openMs) return 1.0f - easeOut(ms / float(blink.openMs));
  ms -= blink.openMs;
  if (ms < 110) return -.05f * sinf(PI_F * ms / 110.0f);
  return 0;
}

void CreatureAnimator::updateBlink(uint32_t now) {
  if (blink.active) {
    int32_t t = int32_t(now - blink.start);
    int32_t total = blink.closeMs + blink.holdMs + blink.openMs + 110 + max(blink.lag[0], blink.lag[1]);
    for (int i = 0; i < 2; ++i) closure[i] = blinkCurve(t - blink.lag[i]) * blink.depth[i];
    if (t >= total) {
      blink.active = false;
      closure[0] = closure[1] = 0;
    }
    return;
  }
  if (secondBlinkAt && int32_t(now - secondBlinkAt) >= 0) {
    secondBlinkAt = 0;
    if (blinkAllowed()) startBlink(now, B_FAST); // the second of a pair is quicker
    return;
  }
  if (int32_t(now - nextBlinkAt) < 0) return;
  scheduleBlink(now);
  if (!blinkAllowed()) return;
  float r = frand();
  BlinkType type = B_NORMAL;
  bool pair = false;
  if (current == HAPPY || current == PETTED) type = r < .45f ? B_SQUEEZE : B_NORMAL;
  else if (current == ANXIOUS) type = r < .7f ? B_FAST : B_NORMAL;
  else if (current == SAD || drowsy > .6f) type = r < .45f ? B_SLEEPY : r < .6f ? B_HALF : B_NORMAL;
  else if (chance(DOUBLE_BLINK_CHANCE)) pair = true;
  else if (r < .64f) type = B_NORMAL;
  else if (r < .76f) type = B_FAST;
  else if (r < .84f) type = B_LONG;
  else if (r < .94f) type = B_HALF;
  else type = B_UNEVEN;
  startBlink(now, type);
  if (pair) {
    secondBlinkAt = now + blink.closeMs + blink.holdMs + blink.openMs + 110 + randMs(40, 110);
  }
}

// ---- Expression layer: each mood sets per-eye targets over its timeline ----

void CreatureAnimator::wakeTargets(float *L, float *R, float &freq, float &zeta, uint32_t now) {
  const WakeScript &s = WAKE_SCRIPTS[wakeVariant];
  int k = 0;
  for (int i = 0; i < s.count; ++i) {
    int32_t at = int32_t(s.keys[i].t * wakeScale);
    if (age >= at) k = i;
    if ((s.keys[i].flags & WK_BLINK) && crossed(at)) startBlink(now, B_FAST);
  }
  const WakeKey &key = s.keys[k];
  float lo = key.leftOpen / 100.0f, ro = key.rightOpen / 100.0f;
  if (wakeSwap) { float tmp = lo; lo = ro; ro = tmp; }
  L[P_OPEN] = lo;
  R[P_OPEN] = ro;
  if (key.flags & WK_SQUINT) {
    float *narrow = lo < ro ? L : R;
    narrow[P_ANGLE] = .14f;
    narrow[P_LOWER] = 3;
    narrow[P_DROP] = 9;
  }
  scriptGX = key.gazeX / 100.0f * wakeGazeScale;
  scriptGY = key.gazeY / 100.0f;
  if (key.flags & WK_SLOW) { freq = 1.3f; zeta = 1.0f; scriptGazeFreq = 2.5f; }
  else if (key.flags & WK_FAST) { freq = 8.0f; zeta = .55f; scriptGazeFreq = 9.0f; }
  else { freq = 3.0f; zeta = .8f; scriptGazeFreq = 4.5f; }
}

void CreatureAnimator::sleepTargets(float *L, float *R, float &freq, float &zeta, uint32_t now) {
  float droop = smoothstep(0.0f, float(SLEEP_BLINK_AT), float(age));
  float open = age < SLEEP_BLINK_AT ? mix(1.0f, .70f, droop)   // lids droop
             : age < SLEEP_DROOP_AT ? .62f                       // reopen only partway
             : age < SLEEP_CLOSE_AT ? .40f                       // heavier
             : .02f;                                             // a last long blink that stays shut
  L[P_OPEN] = R[P_OPEN] = open;
  L[P_DROP] = R[P_DROP] = 6 + 6 * droop;
  L[P_ANGLE] = R[P_ANGLE] = .03f;
  L[P_DY] = R[P_DY] = 2.0f * droop;
  // Shut eyes relax into soft U-shaped arcs.
  L[P_BEND] = R[P_BEND] = age < SLEEP_CLOSE_AT ? -1.2f * droop : -4.5f;
  if (crossed(SLEEP_BLINK_AT)) startBlink(now, B_SLEEPY);
  freq = age < SLEEP_CLOSE_AT ? 1.1f : .85f;
  zeta = 1.0f;
  scriptGX = idleGX * .2f;
  scriptGY = .22f;
  scriptGazeFreq = 1.0f;
  if (!napping()) return;

  // Napping with the screen still on: now and then a sleepy peek or a dream flutter.
  if (!peekUntil && int32_t(now - nextPeekAt) >= 0) {
    float r = frand();
    peekKind = r < .3f ? 1 : r < .6f ? 2 : r < .82f ? 3 : 4; // left, right, flutter, both
    peekUntil = now + (peekKind == 3 ? randMs(110, 180) : randMs(700, 1500));
    nextPeekAt = now + randMs(12000, 35000);
    scriptGX = frand(-.4f, .4f);
  }
  if (peekUntil && int32_t(now - peekUntil) < 0) {
    if (peekKind == 1) L[P_OPEN] = .30f;
    else if (peekKind == 2) R[P_OPEN] = .30f;
    else if (peekKind == 3) L[P_OPEN] = R[P_OPEN] = .09f;
    else L[P_OPEN] = R[P_OPEN] = .17f;
    freq = peekKind == 3 ? 6.0f : 1.6f;
    scriptGY = .05f;
  } else {
    peekUntil = 0;
  }
}

void CreatureAnimator::updateExpression(float dt, uint32_t now) {
  float L[P_COUNT], R[P_COUNT];
  memcpy(L, IDLE_POSE, sizeof(L));
  memcpy(R, IDLE_POSE, sizeof(R));
  float f = 3.2f, z = .78f;
  float t = age / 1000.0f;
  bool scripted = false; // scripted sequences are not intensity-scaled
  auto both = [&](float dx, float dy, float w, float h, float open, float angle,
                  float drop, float lower, float pupil, float bend = 0) {
    setPose(L, dx, dy, w, h, open, angle, drop, lower, pupil, bend);
    setPose(R, dx, dy, w, h, open, angle, drop, lower, pupil, bend);
  };

  switch (current) {
    case IDLE: {
      // Transition out: how gently we return depends on where we came from.
      float from = previous == ANGRY ? 1.2f : previous == SAD ? 1.5f :
                   previous == DIZZY ? 2.0f : previous == SURPRISED ? 2.2f : 3.2f;
      f = mix(from, 3.2f, smoothstep(0.0f, 1800.0f, float(age)));
      z = .8f;
      float *a = (seed & 2) ? L : R, *b = (seed & 2) ? R : L;
      if (quirk == 1) {        // squint
        L[P_OPEN] = R[P_OPEN] = .84f; a[P_LOWER] = 3; b[P_LOWER] = 4.5f;
      } else if (quirk == 2) { // curious: one eye lifts, the other narrows a touch
        a[P_OPEN] = 1.06f; a[P_DROP] = 3.5f; b[P_OPEN] = .9f; b[P_ANGLE] = .1f;
      } else if (quirk == 3) { // soft and relaxed
        L[P_OPEN] = R[P_OPEN] = .9f; L[P_DROP] = R[P_DROP] = 8;
      }
      // Near the sleep timeout the lids start to get heavy.
      for (float *p : {L, R}) { p[P_OPEN] -= .14f * drowsy; p[P_DROP] += 3 * drowsy; }
      // Still grumpy after an angry spell: lids low and angled, calming down.
      if (grumpy > .01f) {
        float gr = grumpy * EXPRESSION_INTENSITY;
        for (float *p : {L, R}) {
          p[P_ANGLE] += .20f * gr; p[P_DROP] += 8 * gr; p[P_OPEN] -= .12f * gr; p[P_DX] += 1.5f * gr;
        }
        f = mix(f, 2.0f, grumpy);
      }
      break;
    }
    case HAPPY:
      if (age < 80) { // a tiny widen first
        both(0, 1, 1.07f, 1.07f, 1.06f, .02f, 4, 0, 1);
        f = 7; z = .7f;
      } else {
        float fade = 1.0f - .5f * smoothstep(0.0f, float(moodUntil - moodStart), float(age));
        float bounce = -1.6f * fmaxf(0.0f, sinf(TAU_F * 1.9f * t)) * fade;
        both(0, -6 + bounce, 1.04f, .98f, .80f, -.06f, 7, 15, 1, 3.5f); // ^ ^ arcs
        R[P_LOWER] += 1.5f;
        f = 5.5f; z = .42f;
      }
      if (crossed(80)) {
        bodyY.kick(-70);
        for (auto &e : expr) e[P_H].kick(.8f);
      }
      break;
    case SAD:
      both(2, 8 + 2.5f * smoothstep(0.0f, 3000.0f, float(age)), .98f, .96f, .60f, -.30f, 13, 0, 1.04f, -1.0f);
      R[P_OPEN] = .57f;
      f = 1.5f; z = 1.0f;
      break;
    case ANGRY:
      if (age < 110) { // anticipation: a small pull back
        both(-3, 1, 1.03f, 1.04f, 1.04f, -.02f, 4, 0, 1.02f);
        f = 6; z = .85f;
      } else {
        both(2.2f, -1, 1.04f, .93f, .68f, .34f, 17, 3, .9f);
        L[P_DROP] += 1.2f;
        R[P_ANGLE] += .03f;
        if (int32_t(now - nextAngrySquint) >= 0) {
          angrySquintUntil = now + randMs(180, 280);
          nextAngrySquint = now + randMs(900, 1700);
        }
        if (int32_t(now - angrySquintUntil) < 0) {
          L[P_OPEN] *= .8f; R[P_OPEN] *= .78f; L[P_DROP] += 2; R[P_DROP] += 2;
        }
        f = age < 650 ? 7.5f : 4.0f;
        z = age < 650 ? .42f : .6f;
      }
      if (crossed(110)) { // impact: snap inward, lids slam down, the glow flares
        for (auto &e : expr) { e[P_DX].kick(45); e[P_OPEN].kick(-2.5f); e[P_DROP].kick(30); }
        bodyY.kick(30);
        glowS.kick(1.8f);
      }
      break;
    case SURPRISED:
      if (age < 70) {           // anticipation: a tiny squash
        both(0, 2.5f, 1.0f, .92f, .84f, .04f, 7, 0, 1.05f);
        f = 9; z = .9f;
      } else if (age < 520) {   // pop wide with overshoot
        both(-1, -3, 1.09f, 1.15f, 1.12f, 0, 1.5f, 0, .66f);
        f = 8.5f; z = .34f;
      } else {                  // slowly settle
        both(-1, -2, 1.06f, 1.09f, 1.06f, 0, 2, 0, .74f);
        f = 2.4f; z = .8f;
      }
      if (crossed(70)) {
        for (auto &e : expr) { e[P_H].kick(2.2f); e[P_W].kick(1.0f); }
        bodyY.kick(-45);
      }
      break;
    case DIZZY: {
      float phase = dizzyPhase(t);
      if (t < 2.0f) {
        both(0, 0, .96f, 1.02f, .84f, -.02f, 4, 0, .9f);
        L[P_OPEN] = .80f + .07f * sinf(phase * .5f);
        R[P_OPEN] = .93f + .06f * sinf(phase * .5f + 2);
        L[P_W] = .95f + .04f * cosf(phase);
        R[P_H] = 1.0f + .04f * sinf(phase + 1);
      } else { // recovering, and already a little annoyed
        both(0, 0, 1, 1, .95f, .07f, 7, 0, 1);
      }
      f = 4; z = .5f;
      break;
    }
    case ANXIOUS:
      both(-1, -2, .97f, 1.03f, .84f + .07f * sinf(TAU_F * 5 * t), .13f, 11, 0, .68f);
      f = 6; z = .6f;
      break;
    case BOOP:
      both(0, 0, 1, 1, .97f, .03f, 5, 0, 1);
      f = 4.5f; z = .38f;
      break;
    case PETTED:
      both(0, -2, 1.05f, .97f, .56f, -.05f, 9, 8, 1.05f, 2.0f);
      R[P_OPEN] = .60f;
      f = 2.2f; z = .9f;
      break;
    case SWIPING: {
      float pull = age < 260 ? 1.0f : 0.0f;
      both(0, 0, 1 + .13f * abs(swipeX) * pull, 1 + .11f * abs(swipeY) * pull, 1, .045f, 6, 0, 1);
      // The trailing eye compresses slightly as the pair is pulled.
      if (pull > 0) {
        if (swipeX > 0) L[P_OPEN] = .8f;
        else if (swipeX < 0) R[P_OPEN] = .8f;
        else L[P_OPEN] = R[P_OPEN] = .9f;
      }
      f = 5; z = .45f;
      break;
    }
    case FOLLOWING:
      both(0, 0, 1, 1, .93f, .04f, 6, 0, 1);
      f = 4; z = .7f;
      break;
    case CONFUSED:
      setPose(L, 0, 2, .98f, .98f, .56f, .14f + .03f * sinf(TAU_F * .7f * t), 10, 0, .95f);
      setPose(R, 0, -3, 1.03f, 1.05f, 1.04f, -.08f, 3, 0, 1);
      f = 3.5f; z = .55f;
      break;
    case SHY:
      both(-2, 3, .98f, .97f, .62f, -.16f, 11, 5, .95f);
      R[P_OPEN] = .48f;
      f = 3; z = .7f;
      break;
    case SHIVER:
      both(1, 1, .97f, .96f, .62f + .03f * noise1(t * 6, seed), .10f, 10, 4, .9f);
      f = 6; z = .5f;
      break;
    case LOOK_CHANGE:
      scripted = true;
      if (age < 170) { L[P_OPEN] = R[P_OPEN] = .03f; f = 9; z = 1; }
      else { f = 6; z = .35f; } // pops back open with a little overshoot
      if (lookPending && age >= 160) {
        lookPending = false;
        applyLook(pendingLook, pendingPalette, false);
      }
      break;
    case BATTERY: {
      scripted = true;
      float open = age < 230 ? .07f : age < int32_t(BATTERY_SHOW_MS) - 320 ? batteryAmount : 1.0f;
      L[P_OPEN] = R[P_OPEN] = open;
      if (batteryAmount == .55f) R[P_ANGLE] = .1f; // unknown reading: questioning look
      f = age < 230 ? 8.0f : 2.6f;
      z = .9f;
      break;
    }
    case WAKE_UP:
      scripted = true;
      wakeTargets(L, R, f, z, now);
      break;
    case SLEEPY:
      scripted = true;
      sleepTargets(L, R, f, z, now);
      break;
  }

  // Being shaken: eyes widen in alarm, pupils shrink.
  if (shakeLevel > .01f && !scripted) {
    float a = fminf(shakeLevel, 1.0f);
    for (float *p : {L, R}) { p[P_OPEN] += .14f * a; p[P_PUPIL] -= .15f * a; p[P_DROP] -= 3 * a; }
    f = fmaxf(f, 6.0f);
  }
  // Every reaction lands with slightly different strength.
  if (!scripted && current != IDLE) {
    const float amount = intensity * EXPRESSION_INTENSITY;
    for (int c = 0; c < P_COUNT; ++c) {
      L[c] = IDLE_POSE[c] + (L[c] - IDLE_POSE[c]) * amount;
      R[c] = IDLE_POSE[c] + (R[c] - IDLE_POSE[c]) * amount;
    }
  }
  for (int i = 0; i < 2; ++i) {
    const float *target = i ? R : L;
    for (int c = 0; c < P_COUNT; ++c) {
      expr[i][c].target = target[c];
      expr[i][c].update(dt, f * CHANNEL_RATE[c], z);
    }
  }
  baseW.update(dt, 5, .6f);
  baseH.update(dt, 5, .6f);
  baseIris.update(dt, 5, .6f);
}

// ---- Gaze layer ----

void CreatureAnimator::updateGaze(float dt, uint32_t now) {
  // Idle wander mixed with the board's tilt (the eyes follow gravity).
  float tx = clampf(idleGX * .8f + tiltX * .55f, -1.0f, 1.0f);
  float ty = clampf(idleGY * .8f + tiltY * .55f, -1.0f, 1.0f);
  float f = fastGaze ? 6.5f : 4.2f, z = .68f;
  bool usePointer = pointerHeld ||
      (now - pointerAt < 1300 && (current == HAPPY || current == BOOP || current == PETTED ||
                                  current == SWIPING || current == FOLLOWING));
  if (usePointer) { tx = pointerX; ty = pointerY; f = 6; z = .7f; }
  switch (current) {
    case SAD: tx = idleGX * .15f; ty = .72f; f = 1.4f; z = .95f; break;
    case SHY: {
      tx = -.85f; ty = .2f; f = 3.5f;
      int32_t cycle = age % 1700;
      if (age > 600 && cycle > 1100 && cycle < 1450) tx = -.25f; // a quick peek back
      break;
    }
    case ANGRY: tx = .05f * noise1(clock * .8f, seed + 5); ty = -.12f; f = 6; z = .7f; break;
    case ANXIOUS:
      if (int32_t(now - nextDartAt) >= 0) {
        dartX = frand(-.75f, .75f);
        dartY = frand(-.55f, -.15f);
        nextDartAt = now + randMs(110, 240);
      }
      tx = dartX; ty = dartY; f = 9; z = .6f;
      break;
    case SURPRISED: tx = ty = 0; f = 10; z = .75f; break;
    case DIZZY: {
      tx = ty = 0; f = 6; z = .7f;
      float t = age / 1000.0f;
      if (t > 2.0f) { // stage 4: struggles to center, overshooting once or twice
        if (t < 2.15f) { tx = -.38f; ty = .14f; }
        else if (t < 2.38f) { tx = .28f; ty = -.06f; }
        else if (t < 2.58f) { tx = -.10f; ty = .03f; }
        f = 3.6f; z = .3f;
      }
      break;
    }
    case WAKE_UP: case SLEEPY: tx = scriptGX; ty = scriptGY; f = scriptGazeFreq; z = .75f; break;
    case BATTERY: case LOOK_CHANGE: tx = ty = 0; f = 4; break;
    case CONFUSED: tx = .35f; ty = -.4f; f = 3.5f; break;
    case HAPPY: if (!usePointer) ty -= .15f; break;
    case SWIPING: tx = swipeX; ty = swipeY; f = 7; z = .6f; break;
    case SHIVER: tx *= .5f; ty *= .5f; break;
    default: break;
  }
  f *= 1.0f - .45f * drowsy;
  for (int i = 0; i < 2; ++i) {
    float fi = i ? f * .88f : f; // the second eye follows a fraction later
    gazeX[i].target = tx;
    gazeY[i].target = ty;
    gazeX[i].update(dt, fi * 1.15f, z);
    gazeY[i].update(dt, fi * 1.15f, z);
    // The whole eye follows the pupil a beat later and without overshoot,
    // like a head turning after the eyes: the motion reads as two masses.
    travelX[i].target = tx;
    travelY[i].target = ty;
    travelX[i].update(dt, fi * .5f, .92f);
    travelY[i].update(dt, fi * .5f, .92f);
  }
}

// ---- Physics layer: face offset with weight, inertia and recoil ----

void CreatureAnimator::updatePhysics(float dt, uint32_t now) {
  (void)now;
  float tx = 0, ty = 0, f = 3.0f, z = .36f;
  float t = age / 1000.0f;
  switch (current) {
    case FOLLOWING:
      // Gaze travel already moves the eyes; the body only leans a little more.
      if (pointerHeld) { tx = pointerX * 2.5f; ty = pointerY * 2.0f; }
      break;
    case SWIPING:
      if (age < 260) { // pulled with the finger, then released into a springy rebound
        float u = easeOut(age / 260.0f);
        tx = swipeX * 15 * u;
        ty = swipeY * 12 * u;
      }
      f = 4.5f; z = .32f;
      break;
    case PETTED:
      tx = 2.2f * sinf(TAU_F * .45f * t);
      ty = .8f * sinf(TAU_F * .9f * t);
      f = 2; z = .9f;
      break;
    case BOOP:
      f = 4; z = .3f;
      if (crossed(55) && boopFar >= 0) {
        expr[boopFar][P_OPEN].kick(-3.5f);
        expr[boopFar][P_W].kick(.8f);
      }
      break;
    default:
      break;
  }
  bodyX.target = tx;
  bodyY.target = ty;
  bodyX.update(dt, f, z, -inertiaX * INERTIA_GAIN);
  bodyY.update(dt, f, z, -inertiaY * INERTIA_GAIN);
  bodyX.pos = clampf(bodyX.pos, -18.0f, 18.0f);
  bodyY.pos = clampf(bodyY.pos, -16.0f, 16.0f);
  leanSm = approach(leanSm, tiltX * 4.0f, 6.0f, dt);
}

// ---- Effects layer: dizzy wobble/orbit, shiver, tremors ----

void CreatureAnimator::updateShakeReaction(float dt) {
  float ex[2] = {0, 0}, ey[2] = {0, 0}, px[2] = {0, 0}, py[2] = {0, 0};
  float t = age / 1000.0f;
  if (current == DIZZY) {
    for (int i = 0; i < 2; ++i) {
      // Stage 2: wobble along the impact axis with decaying amplitude.
      float ti = t - i * .03f;
      float amp = 7.0f * expf(-fmaxf(0.0f, ti - .08f) / .28f) * smoothstep(0.0f, .08f, ti);
      float s1 = sinf(TAU_F * 6.5f * ti), s2 = sinf(TAU_F * 5.1f * ti + 1);
      ex[i] += amp * (impactX * s1 - impactY * .35f * s2);
      ey[i] += amp * (impactY * s1 + impactX * .35f * s2);
    }
    // Stage 3: each eye circles slightly out of phase, the circles shrinking.
    float env = smoothstep(.35f, .75f, t) * (1.0f - smoothstep(1.85f, 2.3f, t));
    float u = clamp01((t - .35f) / 1.95f), phase = dizzyPhase(t);
    float radius = mix(10, 3, u) * env, pupilRadius = mix(.75f, .3f, u) * env;
    ex[0] += cosf(phase) * radius;
    ey[0] += sinf(phase) * radius * .8f;
    ex[1] += cosf(phase + .9f) * radius * .85f;
    ey[1] += sinf(phase + .9f) * radius * .68f;
    // Pupils spin against the eyes and a touch faster: the classic woozy look.
    px[0] += cosf(1.6f - phase * 1.3f) * pupilRadius;
    py[0] += sinf(1.6f - phase * 1.3f) * pupilRadius * .85f;
    px[1] += cosf(2.5f - phase * 1.3f) * pupilRadius;
    py[1] += sinf(2.5f - phase * 1.3f) * pupilRadius * .85f;
  } else if (current == SHIVER) {
    for (int i = 0; i < 2; ++i) {
      ex[i] = 2.4f * sinf(TAU_F * 11 * clock + i * .6f) * (.7f + .3f * noise1(clock * 3, seed + i));
      ey[i] = .6f * sinf(TAU_F * 13 * clock + i);
    }
  } else if (current == ANXIOUS) {
    ex[0] = ex[1] = 2.5f * sinf(TAU_F * 8.7f * clock);
  }
  // Being shaken: both eyes rattle around (a little out of step with each
  // other) and the pupils jiggle inside them. Fades out once shaking stops.
  shakeLevel = approach(shakeLevel, 0.0f, 3.5f, dt);
  if (shakeLevel > .01f && current != SLEEPY) {
    const float a = shakeLevel;
    for (int i = 0; i < 2; ++i) {
      ex[i] += a * 6.0f * noise1(clock * 13.0f + i * 3.1f, seed + 61 + i);
      ey[i] += a * 4.5f * noise1(clock * 11.0f + i * 1.7f, seed + 71 + i);
      px[i] += a * .55f * noise1(clock * 9.0f + i, seed + 81 + i);
      py[i] += a * .40f * noise1(clock * 8.0f + i, seed + 91 + i);
    }
  }
  if (current == ANGRY || current == ANXIOUS) { // small rapid eye movements
    for (int i = 0; i < 2; ++i) {
      px[i] += .05f * noise1(clock * 9, seed + 11 + i);
      py[i] += .04f * noise1(clock * 9, seed + 21 + i);
    }
  }
  for (int i = 0; i < 2; ++i) {
    fxX[i].target = ex[i]; fxY[i].target = ey[i];
    pfxX[i].target = px[i]; pfxY[i].target = py[i];
    fxX[i].update(dt, 16, .8f); fxY[i].update(dt, 16, .8f);
    pfxX[i].update(dt, 14, .8f); pfxY[i].update(dt, 14, .8f);
  }
}

void CreatureAnimator::updateBacklight(float dt) {
  if (current == WAKE_UP) {
    // The screen lights while the eyes are still shut.
    float fade = wakeVariant == WAKE_STARTLED_INDEX ? 150.0f : 380.0f;
    blLevel = fmaxf(blLevel, easeOut(age / fade));
  } else if (current == SLEEPY && age >= SLEEP_FADE_AT) {
    float u = easeInOut((age - SLEEP_FADE_AT) / float(SLEEP_FADE_MS));
    if (napping()) // the glow breathes slowly with the creature instead of going dark
      blLevel = approach(blLevel, napLevel * (.86f + .14f * sinf(breathPhase)), 3.0f, dt);
    else
      blLevel = fminf(blLevel, mix(1.0f, napLevel, u));
  } else {
    blLevel = fminf(1.0f, blLevel + dt * 5.0f);
  }
}

// ---- Glow and colour mood: anger warms the eyes, surprise flares them ----

void CreatureAnimator::updateMoodColour(float dt) {
  grumpy = approach(grumpy, 0.0f, 3.0f / ANGER_COOLDOWN_S, dt);
  float hot = grumpy * .35f, bright = 1.0f;
  switch (current) {
    case ANGRY: hot = age < 110 ? .25f : .9f; bright = 1.12f; break;
    case DIZZY: hot = .35f * smoothstep(1.7f, 2.7f, age / 1000.0f); bright = .94f; break;
    case SURPRISED: bright = 1.12f; break;
    case HAPPY: case PETTED: bright = 1.07f; break;
    case SAD: bright = .80f; break;
    case SHY: bright = .90f; break;
    case SLEEPY: bright = age > SLEEP_CLOSE_AT ? .80f : .92f; break;
    default: break;
  }
  hot = fmaxf(hot, grumpy * .35f) * fminf(1.0f, EXPRESSION_INTENSITY);
  heat = approach(heat, hot, hot > heat ? 7.0f : 1.1f, dt); // flares fast, cools slowly
  glowS.target = bright;
  glowS.update(dt, 2.2f, .5f);
}

// ---- Round-screen fit: keep each eye (and the bright part of its glow) inside
// the safe circle. A soft knee starts easing before the limit; the excess is
// absorbed by nudging the eye toward the centre and by a small shrink, so it
// looks like the eye presses against the glass, never cropped. ----

void CreatureAnimator::fitToCircle(EyeGeom &g, float baseW, float baseH) {
  // Expressions may grow the eyes, but never past MAX_EXPRESSION_EXPANSION.
  g.rx = fminf(g.rx, baseW * MAX_EXPRESSION_EXPANSION);
  g.ry = fminf(g.ry, baseH * MAX_EXPRESSION_EXPANSION);
  if (g.open > 1.0f) g.open = fminf(g.open, baseH * MAX_EXPRESSION_EXPANSION / g.ry);

  static constexpr int N = 24;
  static float cs[N], sn[N];
  static bool ready = false;
  if (!ready) {
    for (int i = 0; i < N; ++i) { cs[i] = cosf(TAU_F * i / N); sn[i] = sinf(TAU_F * i / N); }
    ready = true;
  }
  float cy, h;
  eyeDrawnExtent(g, cy, h);
  const float ax = g.rx + SAFE_GLOW_MARGIN, ay = h + fabsf(g.bend) + SAFE_GLOW_MARGIN;
  const float ox = g.x - SCREEN_CX, oy = cy - SCREEN_CY;
  float best = 0;
  int far = 0;
  for (int i = 0; i < N; ++i) {
    float px = ox + ax * cs[i], py = oy + ay * sn[i];
    float d2 = px * px + py * py;
    if (d2 > best) { best = d2; far = i; }
  }
  const float knee = SAFE_RADIUS - SAFE_SOFTNESS;
  const float dmax = sqrtf(best);
  if (dmax <= knee) return;
  const float allowed = knee + SAFE_SOFTNESS * tanhf((dmax - knee) / SAFE_SOFTNESS);
  const float excess = dmax - allowed;
  const float ux = (ox + ax * cs[far]) / dmax, uy = (oy + ay * sn[far]) / dmax;
  // Overflow at the sides is mostly absorbed by shrinking (moving inward
  // would crowd the pair together); at the top/bottom by moving.
  const float move = excess * mix(.3f, .8f, uy * uy);
  g.x -= ux * move;
  g.y -= uy * move;
  const float reach = ax * cs[far] * ux + ay * sn[far] * uy; // far point's radial reach from the eye centre
  if (reach > 1.0f) {
    float s = clampf(1.0f - (excess - move) / reach, .88f, 1.0f);
    g.rx *= s; g.ry *= s; g.iris *= s;
  }
}

// ---- Combine every layer into the final eye geometry ----

void CreatureAnimator::compose() {
  const float S = EYE_PX_SCALE;
  const float breath = (breathAmp * sinf(breathPhase) + .25f * noise1(clock * .6f, seed + 3)) * S;
  const float glowBreath = 1.0f + (current == SLEEPY ? .10f : .035f) * sinf(breathPhase);
  for (int i = 0; i < 2; ++i) {
    const float side = i == 0 ? -1.0f : 1.0f; // left eye sits left of center
    const float inward = -side;
    const float c = clampf(closure[i], -.1f, 1.0f);
    // Pupils: the fast layer (gaze + saccades + effects + a whisper of drift).
    float gx = gazeX[i].pos + sacX.pos + pfxX[i].pos + .02f * noise1(clock * .7f, seed + 41 + i);
    float gy = gazeY[i].pos + sacY.pos + pfxY[i].pos + .02f * noise1(clock * .7f, seed + 51 + i);
    // Eye bodies: the slow layer, following the pupils a beat later.
    float tx = travelX[i].pos + sacX.pos * .25f, ty = travelY[i].pos + sacY.pos * .25f;
    float microW = .008f * noise1(clock * .33f, seed + 21 + i);
    float microH = .012f * noise1(clock * .4f, seed + 11 + i);
    float microLid = .45f * noise1(clock * .5f, seed + 31 + i);
    EyeGeom &g = geom[i];
    g.x = EYE_CENTER_X + side * EYE_SPACING * .5f + tx * MAX_GAZE_SHIFT_X
          + (inward * expr[i][P_DX].pos + bodyX.pos + fxX[i].pos) * S;
    // A blink tugs the eye down a hair, as if the lid pulls on it.
    g.y = EYE_CENTER_Y + ty * MAX_GAZE_SHIFT_Y + breath
          + (expr[i][P_DY].pos + bodyY.pos + fxY[i].pos + side * leanSm + 1.4f * fmaxf(0.0f, c)) * S;
    // Depth: the eye on the side being looked toward sits a little farther away.
    float persp = 1.0f - EYE_PERSPECTIVE * clampf(travelX[i].pos, -1.0f, 1.0f) * side;
    // Squash & stretch along fast motion, roughly keeping the eye's area.
    float vx = travelX[i].vel * MAX_GAZE_SHIFT_X + bodyX.vel * S;
    float vy = travelY[i].vel * MAX_GAZE_SHIFT_Y + bodyY.vel * S;
    float sx = clampf(fabsf(vx) * (SQUASH_STRETCH / 1600.0f), 0.0f, .08f);
    float sy = clampf(fabsf(vy) * (SQUASH_STRETCH / 1600.0f), 0.0f, .08f);
    g.rx = baseW.pos * expr[i][P_W].pos * (1.0f + microW) * persp * (1.0f + sx - .5f * sy)
           * (1.0f + .06f * fmaxf(0.0f, c)); // lids pressing shut spread the eye a little
    g.ry = baseH.pos * expr[i][P_H].pos * (1.0f + microH) * persp * (1.0f + sy - .5f * sx);
    float open = expr[i][P_OPEN].pos * (1.0f + asymOpen[i]);
    g.open = clampf(open * (1.0f - .985f * c), .02f, 1.3f);
    g.pupilX = gx;
    g.pupilY = gy;
    g.iris = baseIris.pos * clampf(expr[i][P_PUPIL].pos, .45f, 1.4f) * persp;
    g.lidAngle = expr[i][P_ANGLE].pos;
    g.lidDrop = (expr[i][P_DROP].pos + asymDrop[i] + microLid) * S;
    g.lowerLid = expr[i][P_LOWER].pos * S;
    // A closing lid curves the shut eye into a gentle smile-line arc.
    g.bend = (expr[i][P_BEND].pos - 2.0f * fmaxf(0.0f, c)) * S;
    g.glow = glowS.pos * glowBreath;
    g.heat = heat;
    g.blink = fmaxf(0.0f, c);
    fitToCircle(g, baseW.pos, baseH.pos);
  }
}
