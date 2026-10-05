#include <stdio.h>
#include <stdlib.h>
#include <initializer_list>
#include "BatteryState.h"
#include "InteractionPolicy.h"
#include "MotionGestures.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); ++fails; } } while (0)
static uint16_t fb[SCREEN_WIDTH * SCREEN_HEIGHT];

int main(int argc, char **argv) {
  int fails = 0;
  BatteryState battery;
  const int levels[] = {100, 50, 25, 10, 5, 3};
  float fatigue = -1, sadness = -1;
  for (int i = 0; i < 6; ++i) {
    battery.update(levels[i]);
    CHECK(int(battery.stage) == i);
    CHECK(battery.fatigue() >= fatigue && battery.sadness() >= sadness);
    fatigue = battery.fatigue(); sadness = battery.sadness();
  }
  battery.update(5); CHECK(battery.stage == BatteryState::CRITICAL);
  battery.update(8); CHECK(battery.stage == BatteryState::VERY_LOW);
  battery.update(10); CHECK(battery.stage == BatteryState::VERY_LOW);
  battery.update(11); CHECK(battery.stage == BatteryState::VERY_LOW);
  battery.update(13); CHECK(battery.stage == BatteryState::LOW_CHARGE);
  battery.update(-1); CHECK(battery.stage == BatteryState::NORMAL && battery.fatigue() == 0);
  CHECK(BatteryState::estimate(2.0f) == -1 && BatteryState::estimate(4.8f) == -1);
  CHECK(BatteryState::estimate(2.9f) == 0 && BatteryState::estimate(3.3f) == 0);
  CHECK(BatteryState::estimate(4.2f) == 100);
  CHECK(BatteryState::mustRemainAsleep(3, 3.45f, false));
  CHECK(BatteryState::mustRemainAsleep(7, 3.57f, true));
  CHECK(BatteryState::mustRemainAsleep(-1, 2.5f, true));
  CHECK(!BatteryState::mustRemainAsleep(8, 3.60f, true));
  CHECK(!BatteryState::mustRemainAsleep(-1, 4.8f, true)); // USB recovery, not a fake low cell
  CHECK(!BatteryState::mustRemainAsleep(50, 3.86f, false));
  int previous = 0;
  for (float v = 3.3f; v <= 4.3f; v += .001f) {
    int p = BatteryState::estimate(v);
    CHECK(p >= previous && p <= 100); previous = p;
  }
  CHECK(START_ASLEEP && AUTO_DEEP_SLEEP && !WALKING_KEEPS_AWAKE);
  CHECK(TOUCH_ENABLED && !TOUCH_WAKE_DOUBLE_PRESS); // testing branch single-touch wake
  CHECK(sleepAnimationDelay(IDLE_SLEEP_MS) + SLEEP_SEQUENCE_MS == 15000);
  CHECK(sleepAnimationDelay(MAX_AWAKE_MS) + SLEEP_SEQUENCE_MS == 30000);
  for (uint32_t began : {100u, UINT32_MAX - 1000u}) {
    AwakeLimit limit; limit.start(began);
    for (uint32_t elapsed = 0; elapsed < 30000; ++elapsed) {
      // Every millisecond represents another touch/shake; only wake starts the clock.
      CHECK(limit.closingDue(began + elapsed) == (elapsed >= 24600));
    }
    CHECK(limit.closingAt() + SLEEP_SEQUENCE_MS == began + 30000);
    limit.finish(); CHECK(!limit.closingDue(began + 40000));
    limit.start(began + 50000); CHECK(!limit.closingDue(began + 50000));
  }
  CHECK(tapMood(1) == Mood::BOOP && tapMood(2) == Mood::SURPRISED);
  CHECK(tapMood(3) == Mood::ANGRY && tapMood(4) == Mood::ANGRY);

  { // Same final closing as the sketch: an active dizzy/touch hold cannot extend it.
    EyeRenderer renderer; Eyes eyes; CreatureAnimator creature;
    renderer.begin(fb, nullptr); eyes.begin(&renderer, 1234, 100);
    creature.begin(&eyes, 1234, 100); creature.startWake(100, false);
    AwakeLimit limit; limit.start(100); bool closing = false;
    for (uint32_t t = 116; t <= 30100; t += 16) {
      if (limit.closingDue(t)) {
        if (!closing) {
          creature.setPointerHeld(false, t);
          creature.startSleep(limit.closingAt(), 0);
          closing = true;
        }
      } else {
        creature.setPointerHeld(true, t);
        creature.react(Mood::DIZZY, t, DIZZY_ANIM_MS);
      }
      creature.update(.016f, t);
    }
    CHECK(closing && creature.sleepFinished(30100));
    CHECK(creature.backlight() < .001f);
  }

  // A pan about vertical gravity is invisible to the accelerometer. The gyro
  // must carry this gaze, while sensor bias at rest must leave it centered.
  for (int sign : {-1, 1}) {
    WorldFollower world;
    float a[3] = {0, -9.81f, 0}, g[3] = {0, 0, 0};
    world.feed(a, g, 0, 1);
    g[1] = float(sign);
    for (uint32_t t = 26; t <= 526; t += 25) world.feed(a, g, 0, t);
    CHECK(world.tiltX * sign > .8f);
  }
  {
    WorldFollower world;
    float a[3] = {0, -9.81f, 0}, g[3] = {.01f, .02f, -.01f};
    for (uint32_t t = 1; t < 60000; t += 25) world.feed(a, g, 0, t);
    CHECK(fabsf(world.tiltX) < .01f && fabsf(world.tiltY) < .01f);
  }

  // Exercise actual springs and rendering for all battery stages; optionally
  // write PPMs for visual inspection of the new battery layer.
  for (int level : levels) {
    EyeRenderer renderer;
    Eyes eyes;
    CreatureAnimator creature;
    renderer.begin(fb, nullptr);
    eyes.begin(&renderer, 1234, 0);
    creature.begin(&eyes, 1234, 0);
    creature.setBatteryLevel(level);
    creature.setIdleActsAllowed(false);
    eyes.setAutoBlink(false);
    eyes.setIdle(false); // compare settled battery poses without incidental glances/blinks
    for (uint32_t t = 16; t <= 8000; t += 16) creature.update(.016f, t);
    CHECK(creature.mood() == Mood::IDLE);
    renderer.compose(eyes.frames());
    for (int i = 0; i < 2; ++i) CHECK(isfinite(eyes.frames()[i].topA));
    if (argc > 1) {
      char path[1024]; snprintf(path, sizeof(path), "%s/battery-%d.ppm", argv[1], level);
      FILE *f = fopen(path, "wb"); if (!f) return 2;
      fprintf(f, "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
      for (uint16_t value : fb) {
        uint16_t c = (value << 8) | (value >> 8);
        const unsigned char rgb[] = {static_cast<unsigned char>(((c >> 11) & 31) * 255 / 31),
          static_cast<unsigned char>(((c >> 5) & 63) * 255 / 63), static_cast<unsigned char>((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
      }
      fclose(f);
    }
    creature.react(tapMood(3), 8100, ANGRY_ANIM_MS);
    for (uint32_t t = 8116; t < 10300; t += 16) creature.update(.016f, t);
    CHECK(creature.mood() == Mood::IDLE); // anger ends, including at low charge
  }
  printf("Battery, power timing, tapping and pan: %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
  return fails ? 1 : 0;
}
