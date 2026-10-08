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
  CHECK(TOUCH_ENABLED && TOUCH_WAKE_TAPS == 5 && TOUCH_STUCK_MS == 0);
  CHECK(sleepAnimationDelay(IDLE_SLEEP_MS) + SLEEP_SEQUENCE_MS == 15000);
  CHECK(sleepAnimationDelay(WAKE_AWAKE_MS) + SLEEP_SEQUENCE_MS == 30000);
  for (uint32_t began : {100u, UINT32_MAX - 1000u}) {
    AwakeLimit limit; limit.start(began);
    for (uint32_t elapsed = 0; elapsed < 30000; ++elapsed) {
      // No interaction: finish closing at the initial 30-second deadline.
      CHECK(limit.closingDue(began + elapsed) == (elapsed >= 24600));
    }
    CHECK(limit.closingAt() + SLEEP_SEQUENCE_MS == began + 30000);
    limit.interaction(began + 29000); // touch during closing cancels shutdown
    CHECK(!limit.closingDue(began + 30000));
    CHECK(limit.closingAt() + SLEEP_SEQUENCE_MS == began + 44000);
    limit.interaction(began + 42000);
    CHECK(limit.closingAt() + SLEEP_SEQUENCE_MS == began + 57000);
    CHECK(!limit.closingDue(began + 50000));
    CHECK(limit.closingDue(began + 57000));
    limit.start(began);
    limit.interaction(began + 100); // early taps cannot shorten the wake window
    CHECK(limit.closingAt() + SLEEP_SEQUENCE_MS == began + 30000);
    for (uint32_t elapsed = 1000; elapsed <= 180000; elapsed += 1000) {
      limit.interaction(began + elapsed);
      CHECK(!limit.closingDue(began + elapsed));
    }
    CHECK(limit.closingAt() + SLEEP_SEQUENCE_MS == began + 195000);
    CHECK(!limit.closingDue(began + 189599));
    CHECK(limit.closingDue(began + 189600));
    limit.finish(); CHECK(!limit.closingDue(began + 40000));
    limit.start(began + 50000); CHECK(!limit.closingDue(began + 50000));
  }
  for (uint32_t began : {100u, UINT32_MAX - 1000u}) {
    TouchWakeCheck taps;
    taps.begin(began, 1);
    for (uint32_t t = 5; t < 1000; t += 5) CHECK(!taps.feed(1, began + t));
    CHECK(taps.count == 1); // a continuous hold never counts as several taps
    CHECK(!taps.feed(0xFF, began + 1000));
    CHECK(!taps.feed(0, began + 1005));
    CHECK(taps.count == 0); // a long press is not part of a tap run
    taps.begin(began, 0); // first tap has lifted before CPU boot completes
    for (uint32_t tap = 2; tap <= 5; ++tap) {
      uint32_t press = began + (tap - 1) * 200;
      CHECK(taps.feed(1, press) == (tap == 5));
      CHECK(!taps.feed(1, press + 5)); // duplicate contact reports
      CHECK(!taps.feed(0, press + 50));
    }
    CHECK(taps.count == 5);
    taps.begin(began, 0);
    CHECK(!taps.feed(1, began + 20)); // release debounce
    CHECK(!taps.feed(1, began + 200));
    CHECK(!taps.feed(0xFF, began + 250)); // I2C error cannot act as release
    CHECK(!taps.feed(1, began + 400));
    CHECK(taps.count == 2);
    CHECK(!taps.feed(0, began + 450));
    CHECK(!taps.feed(1, began + 1000)); // slow taps restart the run
    CHECK(taps.count == 1);
  }
  CHECK(tapMood(1) == Mood::BOOP && tapMood(2) == Mood::SURPRISED);
  CHECK(tapMood(3) == Mood::ANGRY && tapMood(4) == Mood::ANGRY);

  for (uint32_t began : {100u, UINT32_MAX - 1000u}) {
    ScreenTapRun run;
    for (unsigned i = 1; i <= 5; ++i) CHECK(run.add(began + i * 200) == i);
    CHECK(run.add(began + 3000) == 1); // a new run after a pause
    EyeRenderer renderer; Eyes eyes; CreatureAnimator creature;
    renderer.begin(fb, nullptr); eyes.begin(&renderer, 1234, began);
    creature.begin(&eyes, 1234, began);
    creature.startWake(began, false);
    creature.startTouchAngerPause(began + 1000);
    for (uint32_t elapsed = 16; elapsed < TOUCH_ANGER_PAUSE_MS; elapsed += 16) {
      uint32_t now = began + 1000 + elapsed;
      creature.setPointerHeld(true, now);
      creature.setPointer(1, 1, now);
      CHECK(!creature.react(Mood::FOLLOWING, now, 600));
      CHECK(!creature.react(Mood::DIZZY, now, DIZZY_ANIM_MS));
      creature.huff(.2f, now);
      creature.update(.016f, now);
      CHECK(creature.mood() == Mood::ANGRY);
    }
    uint32_t resumed = began + 1000 + TOUCH_ANGER_PAUSE_MS;
    CHECK(!creature.touchAngerPauseActive(resumed));
    CHECK(creature.react(Mood::FOLLOWING, resumed, 600));
    // Battery shutdown and inactivity sleep can interrupt the angry pause.
    creature.startTouchAngerPause(resumed + 1);
    creature.startSleep(resumed + 2, 0);
    CHECK(!creature.touchAngerPauseActive(resumed + 3));
    CHECK(creature.asleep());
  }

  { // Interaction after 30 seconds keeps the face awake; release allows sleep.
    EyeRenderer renderer; Eyes eyes; CreatureAnimator creature;
    renderer.begin(fb, nullptr); eyes.begin(&renderer, 1234, 100);
    creature.begin(&eyes, 1234, 100); creature.startWake(100, false);
    AwakeLimit limit; limit.start(100); bool closing = false;
    for (uint32_t t = 116; t <= 75108; t += 16) {
      if (t <= 60100) {
        limit.interaction(t);
        creature.setPointerHeld(true, t);
        creature.react(Mood::FOLLOWING, t, 600);
      }
      if (limit.closingDue(t)) {
        if (!closing) {
          creature.setPointerHeld(false, t);
          creature.startSleep(limit.closingAt(), 0);
          closing = true;
        }
      }
      creature.update(.016f, t);
      if (t <= 60100) CHECK(!closing && !creature.asleep());
    }
    CHECK(closing && creature.sleepFinished(75108));
    CHECK(creature.backlight() < .001f);
  }
  { // A tap late in closing restores the face and restarts the deadline.
    EyeRenderer renderer; Eyes eyes; CreatureAnimator creature;
    renderer.begin(fb, nullptr); eyes.begin(&renderer, 1234, 100);
    creature.begin(&eyes, 1234, 100); creature.startWake(100, false);
    AwakeLimit limit; limit.start(100);
    creature.startSleep(limit.closingAt(), 0);
    creature.update(.016f, 29000);
    limit.interaction(29000);
    CHECK(creature.react(Mood::SURPRISED, 29000, 900));
    creature.update(.016f, 30100);
    CHECK(!creature.asleep() && !limit.closingDue(30100));
    CHECK(limit.closingAt() + SLEEP_SEQUENCE_MS == 44000);
  }

  { // An upright screen rolled/pitched around a cone should trace all quadrants.
    WorldFollower world;
    float a[3] = {0,-9.81f,0}, g[3] = {};
    world.feed(a, g, 0, 1);
    float minX=1, maxX=-1, minY=1, maxY=-1;
    for (uint32_t t=26; t<8026; t+=25) {
      float phase=(t-26)*.001f*6.2831853f/4.0f;
      float s=sinf(.4363f), c=cosf(.4363f);
      a[0]=-9.81f*s*cosf(phase); a[1]=-9.81f*c; a[2]=9.81f*s*sinf(phase);
      g[0]=.4f; // an ongoing turn must not be learned as the new resting pose
      world.feed(a, g, 0, t);
      if (t>4026) {
        minX=fminf(minX,world.tiltX); maxX=fmaxf(maxX,world.tiltX);
        minY=fminf(minY,world.tiltY); maxY=fmaxf(maxY,world.tiltY);
        float radius=sqrtf(world.tiltX*world.tiltX+world.tiltY*world.tiltY);
        CHECK(radius>.75f && radius<1.15f);
      }
    }
    CHECK(minX<-.7f && maxX>.7f && minY<-.7f && maxY>.7f);
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
