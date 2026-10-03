// Stress test: drives CreatureAnimator + EyeRenderer with hours of random
// input (touches, swipes, shakes, sleep/wake, look changes, tilt) and checks
// for invalid geometry, out-of-screen drawing and moods that never end.
// Build with sanitizers:  see README.md
#include <math.h>
#include <stdio.h>
#include "EyeRenderer.h"
#include "CreatureAnimator.h"

static uint16_t fb[240 * 240];
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { if (fails++ < 20) { printf(__VA_ARGS__); printf("\n"); } } } while (0)

int main() {
  EyeRenderer r;
  CreatureAnimator c;
  r.begin(fb, nullptr);
  c.begin(&r, 0, 0, 1, 0xC0FFEEu, 0);
  uint32_t now = 0, moodSince = 0;
  Mood lastMood = IDLE;
  const Mood moods[] = {HAPPY, SAD, ANGRY, DIZZY, SURPRISED, ANXIOUS, BOOP, PETTED,
                        SWIPING, FOLLOWING, CONFUSED, SHY, SHIVER, LOOK_CHANGE};
  long frames = 0;
  bool held = false;
  for (; now < 3u * 3600u * 1000u; now += 16 + (esp_random() % 30)) { // ~3 h, 20-50 FPS
    float dt = 0;
    static uint32_t prev = 0;
    dt = (now - prev) / 1000.0f;
    prev = now;
    uint32_t r100 = esp_random() % 1000;
    if (r100 < 4) c.react(moods[esp_random() % 14], now, 400 + esp_random() % 2500);
    else if (r100 < 6) c.reactPassive(moods[esp_random() % 14], now, 600);
    else if (r100 < 7) { c.impact((esp_random() % 41) - 20.0f, (esp_random() % 41) - 20.0f); c.react(DIZZY, now, DIZZY_ANIM_MS); }
    else if (r100 < 9) c.shake((esp_random() % 130) / 100.0f);
    else if (r100 < 10) c.startSleep(now, (esp_random() & 1) ? 0.0f : .3f);
    else if (r100 < 11) c.startWake(now, esp_random() & 1, esp_random() & 1);
    else if (r100 < 12) c.showBattery(int(esp_random() % 120) - 10, now);
    else if (r100 < 13) c.changeLook(esp_random() % 4, esp_random() % 7, esp_random() & 1);
    else if (r100 < 20) { c.setPointer((esp_random() % 200) / 100.0f - 1, (esp_random() % 200) / 100.0f - 1, now);
                          c.setTouchPoint(esp_random() % 240, esp_random() % 240); }
    if (esp_random() % 120 == 0) held = !held;          // holds of a few seconds
    c.setPointerHeld(held, now);
    if (held && esp_random() % 400 == 0) c.huff(c.annoyance(), now);
    c.setTilt(sinf(now * .0007f), cosf(now * .0011f));
    c.applyInertia((esp_random() % 21) - 10.0f, (esp_random() % 21) - 10.0f);
    c.setDrowsiness((now / 7000) % 2 ? .8f : 0);
    c.update(dt, now);
    const EyeGeom *g = c.eyes();
    for (int i = 0; i < 2; ++i) {
      const float v[] = {g[i].x, g[i].y, g[i].rx, g[i].ry, g[i].open, g[i].pupilX, g[i].pupilY, g[i].iris,
                         g[i].lidAngle, g[i].lidDrop, g[i].lowerLid, g[i].bend, g[i].glow, g[i].heat,
                         g[i].blink, g[i].spiral, g[i].spiralPhase};
      for (float f : v) CHECK(isfinite(f), "t=%u eye %d non-finite value", now, i);
      CHECK(g[i].rx > 5 && g[i].rx < EYE_HALF_WIDTH * 1.25f, "t=%u eye %d rx=%.1f", now, i, g[i].rx);
      CHECK(g[i].ry > 5 && g[i].ry < EYE_HALF_HEIGHT * 1.25f, "t=%u eye %d ry=%.1f", now, i, g[i].ry);
      float dx = g[i].x - SCREEN_CX, dy = g[i].y - SCREEN_CY;
      CHECK(sqrtf(dx * dx + dy * dy) < SAFE_RADIUS, "t=%u eye %d centre off-screen", now, i);
    }
    CHECK(c.backlight() >= 0 && c.backlight() <= 1.0001f, "t=%u backlight %.2f", now, c.backlight());
    // Every mood but sleep must end (the longest scripted one is a few seconds);
    // following a finger lasts while the finger is held, then must end too.
    if (c.mood() != lastMood || (c.mood() == FOLLOWING && held)) { lastMood = c.mood(); moodSince = now; }
    CHECK(c.mood() == IDLE || c.mood() == SLEEPY || now - moodSince < 12000,
          "t=%u mood %d stuck for %u ms", now, c.mood(), now - moodSince);
    if (frames % 7 == 0) { // render a sample of frames (sanitizers check every pixel write)
      if (esp_random() % 40 == 0) r.invalidate();
      r.compose(g);
    }
    ++frames;
  }
  printf("%ld frames over %.1f simulated hours: %s (%d problem%s)\n", frames, now / 3.6e6,
         fails ? "FAILED" : "OK", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
