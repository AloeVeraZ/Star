// Stress test: drives CreatureAnimator + Eyes + EyeRenderer with hours of
// random input (touches, swipes, shakes, sleep/wake, direct expression and
// gaze calls, tilt) and checks for invalid geometry, out-of-screen drawing,
// moods that never end, and any pixel that is not a blend of the palette.
// Build with sanitizers:  see README.md
#include <math.h>
#include <stdio.h>
#include "EyeRenderer.h"
#include "palette.h"
#include "Eyes.h"
#include "CreatureAnimator.h"

static uint16_t fb[SCREEN_WIDTH * SCREEN_HEIGHT];
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { if (fails++ < 20) { printf(__VA_ARGS__); printf("\n"); } } } while (0)

int main() {
  EyeRenderer r;
  Eyes eyes;
  CreatureAnimator c;
  r.begin(fb, nullptr);
  eyes.begin(&r, 0xC0FFEEu, 0);
  c.begin(&eyes, 0xC0FFEEu, 0);
  uint32_t now = 0, moodSince = 0;
  Mood lastMood = Mood::IDLE;
  const Mood moods[] = {Mood::HAPPY, Mood::SAD, Mood::ANGRY, Mood::DIZZY, Mood::SURPRISED, Mood::ANXIOUS, Mood::BOOP, Mood::PETTED,
                        Mood::SWIPING, Mood::FOLLOWING, Mood::CONFUSED, Mood::SHY, Mood::SHIVER, Mood::LOVED, Mood::UPSIDE_DOWN,
                        Mood::CURIOUS, Mood::YAWN, Mood::LOOK_AROUND};
  long frames = 0;
  bool held = false, upside = false;
  for (; now < 3u * 3600u * 1000u; now += 16 + (esp_random() % 30)) { // ~3 h, 20-50 FPS
    float dt = 0;
    static uint32_t prev = 0;
    dt = (now - prev) / 1000.0f;
    prev = now;
    uint32_t r100 = esp_random() % 1000;
    if (r100 < 4) c.react(moods[esp_random() % 18], now, 400 + esp_random() % 2500);
    else if (r100 < 6) c.reactPassive(moods[esp_random() % 18], now, 600);
    else if (r100 < 7) { c.impact((esp_random() % 41) - 20.0f, (esp_random() % 41) - 20.0f); c.react(Mood::DIZZY, now, DIZZY_ANIM_MS); }
    else if (r100 < 9) c.shake((esp_random() % 130) / 100.0f);
    else if (r100 < 10) c.startSleep(now, (esp_random() & 1) ? 0.0f : .3f);
    else if (r100 < 11) c.startWake(now, esp_random() & 1, esp_random() & 1);
    else if (r100 < 12) c.showBattery(int(esp_random() % 120) - 10, now);
    else if (r100 < 13) eyes.setExpression(Expression(esp_random() % EXPRESSION_COUNT), (esp_random() % 130) / 100.0f);
    else if (r100 < 14) eyes.lookAt((esp_random() % 200) / 100.0f - 1, (esp_random() % 200) / 100.0f - 1);
    else if (r100 < 15) eyes.blink(Blinker::Type(esp_random() % 7));
    else if (r100 < 20) { c.setPointer((esp_random() % 200) / 100.0f - 1, (esp_random() % 200) / 100.0f - 1, now);
                          c.setTouchPoint(esp_random() % 240, esp_random() % 240); }
    if (esp_random() % 120 == 0) held = !held;          // holds of a few seconds
    c.setPointerHeld(held, now);
    if (esp_random() % 150 == 0) {                       // held upside down for a while
      upside = !upside;
      c.setUpsideDown(upside, now);
      if (upside) c.react(Mood::UPSIDE_DOWN, now, 600);
      else if (c.annoyance() > .35f) c.huff(c.annoyance(), now);
    }
    if (esp_random() % 200 == 0) c.sustain(Mood::PETTED, now, 1400); // rocking
    {                                                    // carried, held up, lying still
      bool walk = (now / 9000) % 3 == 1;
      c.setCarried(walk, walk && (now / 4500) % 2, walk && esp_random() % 20 == 0, 3);
      c.setHeldUp((now / 13000) % 2 == 1, now);
      c.setStillFor((now / 7000) % 4 == 0 ? 30000 : 0);
      c.setFaceRoll(.3f * sinf(now * .0003f));
      c.setSwing(.2f * sinf(now * .002f), 0);
      if (esp_random() % 3000 == 0) c.notice(.7f, now);
    }
    if (held && esp_random() % 400 == 0) c.huff(c.annoyance(), now);
    c.setTilt(sinf(now * .0007f), cosf(now * .0011f));
    c.applyInertia((esp_random() % 21) - 10.0f, (esp_random() % 21) - 10.0f);
    c.setDrowsiness((now / 7000) % 2 ? .8f : 0);
    c.update(dt, now);
    const EyeFrame *g = eyes.frames();
    for (int i = 0; i < 2; ++i) {
      const float v[] = {g[i].cx, g[i].cy, g[i].rx, g[i].ry, g[i].tilt, g[i].bend,
                         g[i].topA, g[i].topB, g[i].topC, g[i].botA, g[i].botB, g[i].botC,
                         g[i].lidRound, g[i].pupilX, g[i].pupilY, g[i].pupilR, g[i].pupilSX, g[i].pupilSY, g[i].glintX, g[i].glintY, g[i].glintR,
                         g[i].heart, g[i].spiral, g[i].spiralPhase};
      for (float f : v) CHECK(isfinite(f), "t=%u eye %d non-finite value", now, i);
      CHECK(g[i].rx > 5 && g[i].rx < EYE_HALF_WIDTH * 1.25f, "t=%u eye %d rx=%.1f", now, i, g[i].rx);
      CHECK(g[i].ry > 5 && g[i].ry < EYE_HALF_HEIGHT * 1.25f, "t=%u eye %d ry=%.1f", now, i, g[i].ry);
      // The eye's far corner stays on the round screen.
      float dx = fabsf(g[i].cx - SCREEN_CX) + g[i].rx * .7f, dy = fabsf(g[i].cy - SCREEN_CY) + g[i].ry * .7f;
      CHECK(sqrtf(dx * dx + dy * dy) < SCREEN_RADIUS, "t=%u eye %d reaches off-screen", now, i);
    }
    CHECK(c.backlight() >= 0 && c.backlight() <= 1.0001f, "t=%u backlight %.2f", now, c.backlight());
    // Every mood but sleep must end (the longest scripted one is a few seconds);
    // following a finger lasts while the finger is held, then must end too.
    if (c.mood() != lastMood || (c.mood() == Mood::FOLLOWING && held) ||
        (c.mood() == Mood::UPSIDE_DOWN && upside)) { lastMood = c.mood(); moodSince = now; }
    CHECK(c.mood() == Mood::IDLE || c.mood() == Mood::SLEEPY || now - moodSince < 12000,
          "t=%u mood %d stuck for %u ms", now, int(c.mood()), now - moodSince);
    if (frames % 7 == 0) { // render a sample of frames (sanitizers check every pixel write)
      if (esp_random() % 40 == 0) r.invalidate();
      r.compose(g);
      for (int p = 0; p < SCREEN_WIDTH * SCREEN_HEIGHT; ++p)
        if (!inPalette(fb[p])) {
          CHECK(false, "t=%u pixel %d is not a blend of black and the two eye colours", now, p);
          break;
        }
    }
    ++frames;
  }
  printf("%ld frames over %.1f simulated hours: %s (%d problem%s)\n", frames, now / 3.6e6,
         fails ? "FAILED" : "OK", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
