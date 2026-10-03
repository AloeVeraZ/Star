#pragma once
#include <math.h>
#include <stdint.h>
#include "FaceConfig.h"

// Follows one finger on the CST816S and classifies what it did, free of
// Arduino calls so it can be tested on a PC (tools/preview/touch_test.cpp).
//
// The CST816S's own gesture recognizer is ignored while a finger is tracked:
// once it decides "swipe up" it keeps tagging later reports with that code,
// which used to cancel finger tracking and replay the same swipe. Instead:
//   * PRESS / MOVE report the finger position the whole time it is down;
//   * on release, the swipe direction comes from the finger's *last* flick
//     (its recent velocity), falling back to the overall movement of a short
//     press; a short, still press is a TAP; anything else is a RELEASE;
//   * a missing lift report is covered by a timeout, so a hold never sticks.
class TouchTracker {
 public:
  enum Kind : uint8_t { NONE, PRESS, MOVE, TAP, SWIPE, RELEASE };
  struct Result {
    Kind kind = NONE;
    int x = 0, y = 0;        // finger position (release: last position)
    float dx = 0, dy = 0;    // SWIPE: direction of the flick, px
    uint32_t heldMs = 0;     // TAP / SWIPE / RELEASE: how long it was down
  };

  bool down = false;
  int startX = 0, startY = 0;
  uint32_t startAt = 0;

  // One CST816S report. event: 0 down, 1 up, 2 contact. valid: coordinates
  // are on screen (an off-screen report still proves the finger is there).
  Result feed(int x, int y, uint8_t event, uint8_t gesture, bool valid, uint32_t now) {
    Result r;
    if (down) lastSeenAt = now;
    if (!valid) return r;
    if (ignoring) {               // a touch that woke it: wait for the lift
      if (event == 1) ignoring = down = false;
      return r;
    }
    if (!down) {
      if (event == 1) {
        // Missed the press entirely: trust the chip's gesture, else a tap.
        r.x = x; r.y = y;
        int sx = gesture == 3 ? -1 : gesture == 4 ? 1 : 0;
        int sy = gesture == 1 ? -1 : gesture == 2 ? 1 : 0;
        if (sx || sy) { r.kind = SWIPE; r.dx = sx * 100.0f; r.dy = sy * 100.0f; }
        else r.kind = TAP;
        return r;
      }
      down = true;
      startX = lastX = x; startY = lastY = y;
      startAt = lastSeenAt = now;
      n = 0;
      remember(x, y, now);
      r.kind = PRESS; r.x = x; r.y = y;
      return r;
    }
    lastX = x; lastY = y;
    remember(x, y, now);
    if (event == 1) return finish(now);
    r.kind = MOVE; r.x = x; r.y = y;
    return r;
  }

  // Call every loop: ends a hold whose lift report never arrived.
  Result poll(uint32_t now) {
    if (down && now - lastSeenAt > TOUCH_RELEASE_TIMEOUT_MS) {
      if (ignoring) { ignoring = down = false; return Result(); }
      return finish(lastSeenAt);
    }
    return Result();
  }

  // Ignore the current touch until the finger lifts (it woke the creature).
  void ignoreUntilLift(uint32_t now) {
    ignoring = down = true;
    lastSeenAt = now;
  }

  bool tracking() const { return down && !ignoring; }  // a finger is being followed
  uint32_t heldFor(uint32_t now) const { return tracking() ? now - startAt : 0; }

 private:
  struct Sample { int16_t x, y; uint32_t t; };
  static constexpr int HISTORY = 12;
  Sample hist[HISTORY];
  int n = 0;
  int lastX = 0, lastY = 0;
  uint32_t lastSeenAt = 0;
  bool ignoring = false;

  void remember(int x, int y, uint32_t t) {
    if (n == HISTORY) { for (int i = 1; i < HISTORY; ++i) hist[i - 1] = hist[i]; --n; }
    hist[n++] = {int16_t(x), int16_t(y), t};
  }

  Result finish(uint32_t now) {
    Result r;
    down = false;
    r.x = lastX; r.y = lastY;
    r.heldMs = now - startAt;
    float tx = float(lastX - startX), ty = float(lastY - startY);
    float total = sqrtf(tx * tx + ty * ty);
    // The finger's last flick: movement over (up to) the final 180 ms.
    int k = n - 1;
    while (k > 0 && now - hist[k - 1].t <= 180) --k;
    float fx = float(lastX - hist[k].x), fy = float(lastY - hist[k].y);
    float flick = sqrtf(fx * fx + fy * fy);
    float flickMs = float(now - hist[k].t);
    float speed = flickMs > 5 ? flick * 1000.0f / flickMs : 0.0f;
    if (r.heldMs <= TAP_MAX_MS && total < TAP_MAX_MOVE_PX) {
      r.kind = TAP;
    } else if (flick >= SWIPE_MIN_PX && speed >= SWIPE_MIN_SPEED) {
      r.kind = SWIPE; r.dx = fx; r.dy = fy;               // wherever it flicked last
    } else if (r.heldMs <= SWIPE_MAX_SHORT_MS && total >= SWIPE_MIN_PX) {
      r.kind = SWIPE; r.dx = tx; r.dy = ty;               // a short, slower swipe
    } else {
      r.kind = RELEASE;
    }
    return r;
  }
};
