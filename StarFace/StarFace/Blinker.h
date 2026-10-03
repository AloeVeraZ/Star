#pragma once
#include "AnimMath.h"
#include "FaceConfig.h"

// Blinks: a fast, accelerating close, a brief hold, then a slower,
// decelerating open that lifts a hair past rest before settling. Automatic
// blinks come at random intervals (weighted toward the short end of
// BLINK_MIN_MS..BLINK_MAX_MS), with occasional doubles, half blinks, slow
// blinks and slightly uneven ones, and one eye always leads by a few ms.
class Blinker {
 public:
  enum Type : uint8_t { NORMAL, FAST, LONG, HALF, SLOW, SQUEEZE, UNEVEN };

  void begin(uint32_t now) { next = now + anim::randMs(1200, 3000); }

  // Blink now (both eyes).
  void blink(uint32_t now, Type type = NORMAL) { start(now, type, -1); }
  // Close only one eye (0 left, 1 right on screen).
  void wink(uint32_t now, int eye) { start(now, LONG, eye); }

  void setAuto(bool on) { autoBlink = on; }
  // Multiplies the time between automatic blinks (>1 fewer, <1 more often).
  void setPace(float gapScale) { pace = gapScale; }
  // Automatic blinks wait while this is false (scripted moments).
  void setAllowed(bool allowed) { this->allowed = allowed; }
  // Slows every blink down (sleepy / sad), 1 = normal.
  void setHeaviness(float h) { heavy = h; }

  // Returns true on the frame an automatic blink starts.
  bool update(uint32_t now) {
    bool started = false;
    if (secondAt && int32_t(now - secondAt) >= 0) {
      secondAt = 0;
      if (allowed) { start(now, FAST, -1); started = true; } // the second of a pair is quicker
    } else if (autoBlink && !b.active && int32_t(now - next) >= 0) {
      schedule(now);
      if (allowed) { startRandom(now); started = true; }
    }
    if (b.active) {
      int32_t t = int32_t(now - b.start);
      for (int i = 0; i < 2; ++i) amount[i] = curve(t - b.lag[i]) * b.depth[i];
      if (t >= b.total) { b.active = false; amount[0] = amount[1] = 0; }
    }
    return started;
  }

  float closure(int eye) const { return amount[eye]; } // 0 open .. 1 shut (slightly < 0 just after)
  bool active() const { return b.active; }

 private:
  struct State {
    bool active = false;
    uint32_t start = 0;
    uint16_t closeMs = BLINK_CLOSE_MS, holdMs = 30, openMs = BLINK_OPEN_MS;
    int32_t total = 0;
    uint16_t lag[2] = {0, 0};
    float depth[2] = {1, 1};
  } b;
  uint32_t next = 0, secondAt = 0;
  float amount[2] = {0, 0};
  float pace = 1, heavy = 1;
  bool autoBlink = true, allowed = true;

  static constexpr int32_t SETTLE_MS = 110;

  void schedule(uint32_t now) {
    float gap = BLINK_MIN_MS + float(BLINK_MAX_MS - BLINK_MIN_MS) * powf(anim::frand(), 1.5f);
    next = now + uint32_t(gap * pace);
  }

  void startRandom(uint32_t now) {
    float r = anim::frand();
    if (anim::chance(DOUBLE_BLINK_CHANCE)) {
      start(now, NORMAL, -1);
      secondAt = now + b.total + anim::randMs(30, 110);
      return;
    }
    Type t = r < .62f ? NORMAL : r < .74f ? FAST : r < .82f ? LONG : r < .92f ? HALF : UNEVEN;
    if (heavy > 1.2f && anim::chance(.5f)) t = SLOW;
    start(now, t, -1);
  }

  void start(uint32_t now, Type type, int onlyEye) {
    using anim::randMs;
    using anim::frand;
    const float c = BLINK_CLOSE_MS, o = BLINK_OPEN_MS;
    float closeMs = c, holdMs = 30, openMs = o, depth = 1;
    switch (type) {
      case FAST:    closeMs = c * .78f; holdMs = 12;  openMs = o * .62f; break;
      case LONG:    closeMs = c * 1.3f; holdMs = randMs(160, 300); openMs = o * 1.25f; break;
      case HALF:    closeMs = c * 1.0f; holdMs = 10;  openMs = o * .8f; depth = frand(.45f, .65f); break;
      case SLOW:    closeMs = c * 4.0f; holdMs = randMs(250, 450); openMs = o * 3.5f; break;
      case SQUEEZE: closeMs = c * 1.4f; holdMs = randMs(70, 120); openMs = o * 1.2f; break;
      default:      closeMs = c * frand(.9f, 1.25f); holdMs = randMs(15, 45); openMs = o * frand(.85f, 1.15f); break;
    }
    closeMs *= heavy; holdMs *= heavy; openMs *= heavy;
    b.active = true;
    b.start = now;
    b.closeMs = uint16_t(closeMs);
    b.holdMs = uint16_t(holdMs);
    b.openMs = uint16_t(openMs);
    int lead = anim::chance(.5f) ? 0 : 1;
    b.lag[lead] = 0;
    b.lag[1 - lead] = type == UNEVEN ? randMs(18, 40) : randMs(0, 14);
    b.depth[lead] = depth;
    b.depth[1 - lead] = depth * (type == UNEVEN ? frand(.82f, .93f) : frand(.96f, 1.0f));
    if (onlyEye >= 0) {
      b.lag[0] = b.lag[1] = 0;
      b.depth[onlyEye] = 1;
      b.depth[1 - onlyEye] = .12f; // the other eye squints along a little
    }
    b.total = b.closeMs + b.holdMs + b.openMs + SETTLE_MS + (b.lag[0] > b.lag[1] ? b.lag[0] : b.lag[1]);
  }

  // Closure over time for the current blink.
  float curve(int32_t ms) const {
    if (ms <= 0) return 0;
    if (ms < b.closeMs) {
      float u = ms / float(b.closeMs);
      return .25f * u + .75f * u * u;            // accelerating shut
    }
    ms -= b.closeMs;
    if (ms < b.holdMs) return 1;
    ms -= b.holdMs;
    if (ms < b.openMs) return 1.0f - anim::easeOut(ms / float(b.openMs)); // decelerating open
    ms -= b.openMs;
    if (ms < SETTLE_MS) return -.05f * sinf(anim::PI_F * ms / float(SETTLE_MS)); // lifts past rest
    return 0;
  }
};
