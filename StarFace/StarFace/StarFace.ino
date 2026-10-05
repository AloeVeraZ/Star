#include <Arduino.h>
#include <math.h>
#include "esp_sleep.h"
#include "esp_heap_caps.h"
#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "DEV_Config.h"
#include "LCD_1in28.h"
#include "QMI8658.h"
#include "CST816S.h"
#include "FaceConfig.h"
#include "FaceLog.h"
#include "EyeRenderer.h"
#include "Eyes.h"
#include "CreatureAnimator.h"
#include "ShakeDetector.h"
#include "LeftRightWake.h"
#include "MotionGestures.h"
#include "TouchTracker.h"
#include "InteractionPolicy.h"

// Star Face for Waveshare ESP32-S3-Touch-LCD-1.28 (240x240 round).
// Hardware, input and power live here. The eyes are an animation system of
// their own (Eyes: expressions, gaze, blinks, idle behaviour; EyeRenderer:
// anti-aliased vector drawing); CreatureAnimator turns touch, motion and time into
// moods that drive them. Eye size, colours, animation feel, shake sensitivity
// and sleep timing are tuned in FaceConfig.h.
//
// The eyes can also be driven directly from anywhere in the sketch:
//   eyes.setExpression(HAPPY);   eyes.lookAt(0.5f, -0.3f);   eyes.blink();
uint16_t *BlackImage = nullptr; // Required by Waveshare's LCD_1in28.cpp.
CST816S touch(6, 7, 13, 5);
EyeRenderer renderer;
Eyes eyes;
CreatureAnimator creature;

static constexpr int W = SCREEN_WIDTH, H = SCREEN_HEIGHT;
static_assert(W == LCD_1IN28_WIDTH && H == LCD_1IN28_HEIGHT, "FaceConfig.h screen size must match the panel");
static constexpr uint32_t FRAME_US = 16667;      // frame cap (~60 FPS); render time sets the real rate
static constexpr uint32_t NAP_FRAME_US = 33333;  // 30 FPS is plenty while the eyes are shut
static constexpr float MAX_FRAME_DT = .05f;      // a stall never turns into an animation jump
static constexpr bool LOG_FPS = false;           // print frames per second over serial
// Prints shake strength while it is being shaken (silent when still), and
// every knock, flick, rock and upside-down/face-down gesture. Open the serial
// monitor at 115200 to see whether the motion sensor responds and how hard
// your shakes and knocks are; set false once tuned.
static constexpr bool LOG_SHAKE = true;

// Power (sleep timeouts, battery thresholds and backlight are in FaceConfig.h).
RTC_DATA_ATTR bool criticalBatterySleep = false;
static constexpr uint32_t TOUCH_TWO_PRESS_MS = 3000;

// Coarse low-power motion alarm (max 255). Higher means walking wakes the CPU
// less often for a dark-screen rotation check.
static constexpr uint8_t WOM_THRESHOLD_MG = 200;
static constexpr float TILT_ACTIVITY_DELTA = .55f; // held orientation change, not a walking jostle
static constexpr uint32_t TILT_ACTIVITY_HOLD_MS = 300;
static constexpr int IMU_WAKE_PIN = 3;    // QMI8658 INT2, active low in WoM
static constexpr int TOUCH_WAKE_PIN = 5;  // CST816S IRQ, active-low touch pulse

uint32_t lastActivity = 0, lastFrameUs = 0;
bool sleepPreparing = false, deepSleepPlanned = false;
bool imuReady = false, wokeByShake = false;
AwakeLimit awakeLimit;
bool forcedClosing = false;
LeftRightWakeCheck sleepingShake;
float tiltX = 0, tiltY = 0;
float linX = 0, linY = 0; // smoothed gravity-free acceleration
TouchTracker finger;
uint32_t lastTouchActionAt = 0, lastSwipeAt = 0;
uint32_t sleepRetryAt = 0, lastBatteryCheck = 0;
uint8_t tapStreak = 0;
uint32_t lastTapAt = 0;
ScreenTapRun screenTaps;
float dieTempC = 25.0f, batteryVolts = 0;
int batteryPercent = -1;
uint32_t lastTempRead = 0, coldSince = 0, lastColdReaction = 0;

TwistDetector activeTwist;
ShakeDetector shaker;
TiltFlickDetector flicker;
RockDetector rocker;
StepDetector steps;
bool touchStuck = false;   // a touch that never lifts is being ignored

// ---- Fast motion sampling on the second core ----
// A knock on the case is over in a few milliseconds, far quicker than the
// render loop samples the sensor, so a small task on core 0 reads the IMU
// every FAST_SAMPLE_MS and looks for knocks. The render loop takes the latest
// sample from it for everything else (tilt, shakes, flicks, rocking).
static constexpr uint32_t FAST_SAMPLE_MS = 3;   // ~333 samples per second
struct KnockReport { uint8_t count; float x, y; };
portMUX_TYPE motionLock = portMUX_INITIALIZER_UNLOCKED;
volatile bool samplerOn = false, samplerBusy = false;
TaskHandle_t samplerTask = nullptr;
float latestA[3] = {0, 0, 9.81f}, latestG[3] = {0, 0, 0};
bool latestValid = false;
bool latestCarriedMotion = false;
float latestShakeStrength = 0;
uint32_t latestShakeFor = 0;
ShakeDetector::Event pendingShake = ShakeDetector::NONE;
KnockReport knockQueue[8];
uint8_t knockHead = 0, knockTail = 0;
// Wire locks the bus per transfer but hands the received bytes over after
// unlocking, so two cores reading at once could swap each other's data. Every
// sensor read that can overlap the sampler (touch, IMU temperature) holds this.
SemaphoreHandle_t i2cMutex = nullptr;
struct I2CGuard {
  I2CGuard() { if (i2cMutex) xSemaphoreTake(i2cMutex, portMAX_DELAY); }
  ~I2CGuard() { if (i2cMutex) xSemaphoreGive(i2cMutex); }
};

void applyBacklight(float level) {
  static int lastDuty = -1;
  level = constrain(level, 0.0f, 1.0f);
  // Squared so fades look even to the eye instead of rushing at the dark end.
  int duty = int(BACKLIGHT_PERCENT * 2.55f * level * level + .5f);
  if (duty != lastDuty) {
    analogWrite(LCD_BL_PIN, duty);
    lastDuty = duty;
  }
}

bool batteryLow() {
  return batteryPercent >= 0 && batteryPercent <= CRITICAL_BATTERY_PERCENT;
}

uint32_t idleTimeout() {
  return AUTO_DEEP_SLEEP ? IDLE_SLEEP_MS : IDLE_NAP_MS;
}

// A user interaction: always counts as activity and ends any sleep sequence.
bool react(Mood m, uint32_t now, uint32_t duration = 900) {
  lastActivity = now;
  sleepPreparing = false;
  return creature.react(m, now, duration);
}

void startWakeAnimation(uint32_t now, bool fromShake, bool touched = false) {
  awakeLimit.start(now);
  forcedClosing = false;
  sleepingShake = LeftRightWakeCheck();
  sleepPreparing = false;
  lastActivity = now;
  creature.startWake(now, fromShake, touched);
}

void startSleepAnimation(uint32_t now) {
  if (RAW_IMU_SERIAL_ONLY && !batteryLow()) return;
  sleepingShake = LeftRightWakeCheck();
  sleepPreparing = true;
  deepSleepPlanned = AUTO_DEEP_SLEEP || batteryLow();
  float napLevel = deepSleepPlanned ? 0.0f :
                   sqrtf(NAP_BACKLIGHT_PERCENT / float(BACKLIGHT_PERCENT));
  creature.startSleep(now, napLevel);
  FACE_LOG(printf, "%s after %lu ms idle\n", deepSleepPlanned ? "Falling asleep" : "Dozing off",
                (unsigned long)(now - lastActivity));
}

// Touch wake, using the testing branch behavior.
bool cancelSleepForTouch(uint32_t now) {
  if (batteryLow() || forcedClosing) return false;
  if (!sleepPreparing) return false;
  if (creature.sleepFinished(now)) {
    startWakeAnimation(now, false, true);
    FACE_LOG(println, "Nap ended by touch");
    return true;
  }
  react(Mood::SURPRISED, now, 900);
  FACE_LOG(println, "Sleep animation canceled by touch");
  return false;
}

void readBattery() {
  // Waveshare's schematic senses VSYS via a 200k/100k divider, not VBAT.
  // Calibrated ADC millivolts already include ESP32 calibration; multiply by 3.
  uint32_t sum = 0;
  uint32_t lo = UINT32_MAX, hi = 0;
  for (int i = 0; i < 16; ++i) {
    uint32_t v = analogReadMilliVolts(BAT_ADC_PIN);
    sum += v; lo = min(lo, v); hi = max(hi, v);
  }
  float volts = (sum - lo - hi) / 14.0f * .003f;
  // Reject single ADC outliers, then smooth load fluctuations over ~20 s.
  batteryVolts = batteryVolts == 0 || volts > 4.4f || batteryVolts > 4.4f
                 ? volts : batteryVolts + (volts - batteryVolts) * .4f;
  batteryPercent = BatteryState::estimate(batteryVolts);
  creature.setBatteryLevel(batteryPercent);
}

void showBatteryLevel(int percent) {
  uint32_t now = millis();
  lastActivity = now;
  sleepPreparing = false;
  creature.showBattery(percent, now);
}

void beginSwipe(int x0, int y0, int dx, int dy, uint32_t now) {
  if (now - lastSwipeAt < 140) return; // CST816S can report gesture and release together.
  lastSwipeAt = now;
  bool horizontal = abs(dx) >= abs(dy);
  int sx = horizontal ? (dx >= 0 ? 1 : -1) : 0;
  int sy = horizontal ? 0 : (dy >= 0 ? 1 : -1);
  creature.setSwipe(sx, sy);
  creature.setPointer(sx, sy, now);
  creature.setTouchPoint(constrain(x0 + dx, 0, W - 1), constrain(y0 + dy, 0, H - 1));
  react(Mood::SWIPING, now, 740);
  if (sy < 0) {
    readBattery();
    creature.queue(Mood::BATTERY, now + 700, BATTERY_SHOW_MS, batteryPercent);
  } else if (sy > 0) creature.queue(Mood::SAD, now + 700, 1400);
  else if (sx < 0) creature.queue(Mood::SHY, now + 700, 1400);
  else creature.queue(Mood::HAPPY, now + 700, 1400);
}

void pointAt(int x, int y, uint32_t now) {
  creature.setTouchPoint(x, y);
  const float reach = SCREEN_MIN_SIDE / 3.0f;
  creature.setPointer((x - SCREEN_CX) / reach, (y - SCREEN_CY) / reach, now);
}

// One tap (or knock) in a run: 1 boop, 2 surprise, 3/4 a brief angry huff.
void tapReaction(int streak, int x, int y, uint32_t now) {
  pointAt(x, y, now);
  tapStreak = streak;
  react(tapMood(tapStreak), now, tapStreak >= 3 ? ANGRY_ANIM_MS : tapStreak == 2 ? 900 : 680);
}

void finishTap(int x, int y, uint32_t now) {
  if (now - lastTouchActionAt < 75) return;
  lastTouchActionAt = now;
  int streak = screenTaps.add(now);
  lastTapAt = now;
  if (streak == 5) {
    lastActivity = now;
    sleepPreparing = false;
    finger.ignoreUntilLift(now);
    creature.startTouchAngerPause(now);
    FACE_LOG(printf, "touch: five taps, angry pause for %lu ms\n", (unsigned long)TOUCH_ANGER_PAUSE_MS);
    return;
  }
  tapReaction(streak, x, y, now);
}

// A finger lifted after being held (no tap, no swipe).
void finishHold(const TouchTracker::Result &r, uint32_t now) {
  float anger = creature.annoyance();
  if (anger > .35f) {
    // Held on too long: a huff, then it cools down.
    creature.huff(anger, now);
    FACE_LOG(printf, "Let go after %lu ms: huff %.2f\n", (unsigned long)r.heldMs, anger);
  } else if (r.heldMs < HOLD_ANGER_START_MS) {
    react(Mood::PETTED, now, 1100); // a short, gentle press feels nice
  }
}

void handleTouch(uint32_t now) {
  if (!TOUCH_ENABLED) return;
  TouchTracker::Result r;
  // A finger that never lifts is not a finger (a cover pressing on the glass,
  // moisture): ignore it until it lifts, so it can't keep it angry or awake.
  if (finger.tracking() && finger.heldFor(now) > TOUCH_STUCK_MS) {
    finger.ignoreUntilLift(now);
    touchStuck = true;
    creature.setPointerHeld(false, now);
    FACE_LOG(println, "Touch held for too long: ignoring it until it lifts");
  }
  bool touched;
  {
    I2CGuard bus;
    touched = touch.available();
  }
  if (creature.touchAngerPauseActive(now)) {
    // Drain reports without restarting anger, following a finger or extending sleep.
    if (touched) {
      auto d = touch.data;
      finger.ignoreUntilLift(now);
      finger.feed(d.x, d.y, d.event, d.gestureID, true, now);
    } else finger.poll(now);
    creature.setPointerHeld(false, now);
    return;
  }
  if (touched) {
    auto d = touch.data;
    bool wasStuck = touchStuck;
    if (wasStuck && d.event == 1) touchStuck = false; // lifted at last
    if (wasStuck) {
      finger.feed(d.x, d.y, d.event, d.gestureID, true, now);
      return;
    }
    lastActivity = now;
    // Even if the controller reports a partial/invalid coordinate, its IRQ is
    // real activity. Never finish closing the eyes while a finger is present.
    if (cancelSleepForTouch(now)) {
      finger.ignoreUntilLift(now); // this touch woke it; don't also follow it
      return;
    }
    bool valid = d.x >= 0 && d.x < W && d.y >= 0 && d.y < H;
    r = finger.feed(d.x, d.y, d.event, d.gestureID, valid, now);
  } else {
    r = finger.poll(now); // a lift report can go missing
    if (touchStuck && !finger.down) touchStuck = false;
  }
  switch (r.kind) {
    case TouchTracker::PRESS:
      pointAt(r.x, r.y, now);
      react(Mood::FOLLOWING, now, 600); // follows the finger for as long as it stays down
      break;
    case TouchTracker::MOVE:
      pointAt(r.x, r.y, now);
      break;
    case TouchTracker::TAP:
      finishTap(r.x, r.y, now);
      break;
    case TouchTracker::SWIPE:
      if (creature.annoyance() > .35f) finishHold(r, now); // too cross to play along
      else beginSwipe(r.x - int(r.dx), r.y - int(r.dy), int(r.dx), int(r.dy), now);
      break;
    case TouchTracker::RELEASE:
      finishHold(r, now);
      break;
    default:
      break;
  }
}

// Reads the IMU in m/s^2 and rad/s, turned into the screen's frame: x to the
// right, y down, z into the screen (the sensor's own y points up the board
// and its z out of the screen; IMU_ROTATION corrects other mountings).
void readMotion(float a[3], float g[3]) {
  float ra[3], rg[3];
  QMI8658_read_xyz(ra, rg, nullptr);
  // Waveshare's driver reports mg and degrees/s.
  float ax = ra[0] * 0.00980665f, ay = -ra[1] * 0.00980665f, gx = rg[0] * 0.017453293f, gy = -rg[1] * 0.017453293f;
  for (uint8_t r = 0; r < (IMU_ROTATION & 3); ++r) {
    float t = ax; ax = -ay; ay = t;
    t = gx; gx = -gy; gy = t;
  }
  a[0] = ax; a[1] = ay; a[2] = -ra[2] * 0.00980665f;
  g[0] = gx; g[1] = gy; g[2] = -rg[2] * 0.017453293f;
}

void printRawImu(uint32_t now) {
  static uint32_t lastPrintAt = 0;
  static unsigned int lastSample = 0;
  static bool haveSample = false;
  if (!RAW_IMU_SERIAL_ONLY || !imuReady || now - lastPrintAt < RAW_IMU_PRINT_MS) return;
  // Skip a row rather than blocking animation when the serial buffer is full.
  if (Serial.availableForWrite() < 64) return;
  lastPrintAt = now;
  short rawA[3], rawG[3];
  unsigned int sample = 0;
  {
    I2CGuard bus;
    if (!QMI8658_read_xyz_raw(rawA, rawG, &sample)) return;
  }
  // Sensor timestamps distinguish a fresh stationary reading from registers
  // frozen in WoM mode. Never present the latter as another live sample.
  if (haveSample && sample == lastSample) return;
  haveSample = true;
  lastSample = sample;
  // Native sensor axes: no screen rotation, scaling, gravity subtraction or
  // software smoothing. Accel is 4096 counts/g; gyro is 16 counts/(degree/s).
  Serial.printf("AX:%d,AY:%d,AZ:%d,GX:%d,GY:%d,GZ:%d\n",
                int(rawA[0]), int(rawA[1]), int(rawA[2]),
                int(rawG[0]), int(rawG[1]), int(rawG[2]));
}

// Waveshare's QMI8658_config_acc() builds the accelerometer's low-pass
// setting and then writes CTRL5 = 0, leaving it off: a knock is then a spike
// of a millisecond or two that sampling easily misses. Turn the filter on
// (~54 Hz: mode 2 at 1 kHz, well above any shake or step) next to the
// gyro's, without changing the driver.
void tuneImuFilters() {
  uint8_t ctrl5 = 0;
  QMI8658_read_reg(QMI8658Register_Ctrl5, &ctrl5, 1);
  QMI8658_write_reg(QMI8658Register_Ctrl5, uint8_t((ctrl5 & 0xF0) | A_LSP_MODE_2 | 0x01));
}

void motionSamplerTask(void *) {
  KnockDetector knocks;
  ShakeDetector fastShake;
  LeftRightWakeCheck motionDirection;
  float gravity[3] = {0, 0, 0};
  uint32_t previousAt = 0;
  TickType_t wakeAt = xTaskGetTickCount();
  for (;;) {
    if (!samplerOn) {
      samplerBusy = false;
      vTaskDelay(pdMS_TO_TICKS(5));
      wakeAt = xTaskGetTickCount();
      continue;
    }
    samplerBusy = true;
    float a[3], g[3];
    {
      I2CGuard bus;
      readMotion(a, g);
    }
    KnockDetector::Event k = knocks.feed(a, g, millis());
    uint32_t now = millis();
    motionDirection.feed(a, g, now);
    bool carriedMotion = motionDirection.carryingMotion();
    if (!previousAt) for (int i = 0; i < 3; ++i) gravity[i] = a[i];
    float dt = previousAt ? min(uint32_t(100), now - previousAt) / 1000.0f : .003f;
    previousAt = now;
    float lin[3], alpha = 1.0f - expf(-dt / .25f);
    for (int i = 0; i < 3; ++i) {
      gravity[i] += (a[i] - gravity[i]) * alpha;
      lin[i] = a[i] - gravity[i];
    }
    const float quiet[3] = {0, 0, 0};
    ShakeDetector::Event shake = fastShake.feed(carriedMotion ? quiet : lin,
                                              carriedMotion ? 0 : motion::len3(g), now);
    portENTER_CRITICAL(&motionLock);
    for (int i = 0; i < 3; ++i) { latestA[i] = a[i]; latestG[i] = g[i]; }
    latestValid = true;
    latestCarriedMotion = carriedMotion;
    if (carriedMotion) pendingShake = ShakeDetector::NONE;
    latestShakeStrength = fastShake.strength;
    latestShakeFor = fastShake.shakingFor(now);
    // Keep the most meaningful event until the rendering core consumes it.
    if (shake == ShakeDetector::DIZZY ||
        (pendingShake != ShakeDetector::DIZZY && shake == ShakeDetector::STARTLE) ||
        (pendingShake == ShakeDetector::NONE && shake != ShakeDetector::NONE)) pendingShake = shake;
    if (k.count && uint8_t(knockHead + 1) % 8 != knockTail) {
      knockQueue[knockHead] = {k.count, k.x, k.y};
      knockHead = (knockHead + 1) % 8;
    }
    portEXIT_CRITICAL(&motionLock);
    const TickType_t period = pdMS_TO_TICKS(FAST_SAMPLE_MS);
    vTaskDelayUntil(&wakeAt, period > 0 ? period : 1);
  }
}

void startMotionSampler() {
  if (!imuReady) return;
  if (!i2cMutex) i2cMutex = xSemaphoreCreateMutex();
  if (!samplerTask)
    xTaskCreatePinnedToCore(motionSamplerTask, "motion", 4096, nullptr, 2, &samplerTask, 0);
  samplerOn = true;
}

// Stops the fast sampler before the IMU is reconfigured (deep sleep).
void stopMotionSampler() {
  samplerOn = false;
  for (int i = 0; i < 40 && samplerBusy; ++i) delay(2);
  latestValid = false;
  portENTER_CRITICAL(&motionLock);
  pendingShake = ShakeDetector::NONE;
  portEXIT_CRITICAL(&motionLock);
}

// The latest motion sample: from the fast sampler, or read directly.
void currentMotion(float a[3], float g[3]) {
  if (samplerOn && latestValid) {
    portENTER_CRITICAL(&motionLock);
    for (int i = 0; i < 3; ++i) { a[i] = latestA[i]; g[i] = latestG[i]; }
    portEXIT_CRITICAL(&motionLock);
  } else {
    I2CGuard bus;
    readMotion(a, g);
  }
}

bool nextKnock(KnockReport &k) {
  bool any = false;
  portENTER_CRITICAL(&motionLock);
  if (knockTail != knockHead) {
    k = knockQueue[knockTail];
    knockTail = (knockTail + 1) % 8;
    any = true;
  }
  portEXIT_CRITICAL(&motionLock);
  return any;
}

// Reacts to how the star is moved: inertia, bumps, shakes, pick-ups and naps.
void handleSurroundings(const float a[3], const float g[3], bool heldStill, uint32_t now) {
  // A slow gravity estimate leaves the fast, gravity-free part of the motion.
  static bool gravityReady = false;
  static uint32_t gravityAt = 0;
  static float grav[3];
  if (!gravityReady) {
    for (int i = 0; i < 3; ++i) grav[i] = a[i];
    gravityReady = true;
  }
  float lin[3];
  float gravityDt = gravityAt ? min(uint32_t(100), now - gravityAt) / 1000.0f : .025f;
  gravityAt = now;
  float gravityK = 1.0f - expf(-gravityDt / .25f);
  for (int i = 0; i < 3; ++i) {
    grav[i] += (a[i] - grav[i]) * gravityK;
    lin[i] = a[i] - grav[i];
  }
  // Light smoothing so sensor noise does not make the eyes jitter.
  linX += (lin[0] - linX) * .55f;
  linY += (lin[1] - linY) * .55f;
  creature.applyInertia(linX, linY); // moving right throws the eyes left, and so on
  float jolt = sqrtf(lin[0] * lin[0] + lin[1] * lin[1] + lin[2] * lin[2]);

  static uint32_t stillSince = 0;
  uint32_t stillFor = stillSince ? now - stillSince : 0;
  stillSince = heldStill ? (stillSince ? stillSince : now) : 0;
  creature.setStillFor(stillFor);   // lying still for long: bored, then sleepy

  // Carried around: footsteps bob the eyes and it watches the world go by.
  steps.feed(a, shaker.strength, now);
  creature.setCarried(steps.walking, steps.running, steps.step, steps.strength);
  static bool wasWalking = false;
  if (steps.walking != wasWalking && LOG_SHAKE)
    FACE_LOG(println, steps.walking ? (steps.running ? "motion: running" : "motion: walking") : "motion: stopped walking");
  wasWalking = steps.walking;
  if (steps.walking && WALKING_KEEPS_AWAKE && !sleepPreparing) lastActivity = now;

  // ---- Shaking while awake ----
  // How hard it is being shaken drives a live rattle in the eyes; a single
  // strong push only sloshes them (so being carried, even running, stays
  // calm); a push back the other way startles it; a real shake (see
  // ShakeDetector) makes it dizzy, then angry, then it calms down.
  static uint32_t lastLogAt = 0;
  float spin = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
  ShakeDetector::Event ev;
  uint32_t shakeFor;
  if (samplerOn && latestValid) {
    // Recognition runs on every fast IMU sample, independent of LCD frame time.
    portENTER_CRITICAL(&motionLock);
    ev = pendingShake;
    pendingShake = ShakeDetector::NONE;
    shaker.strength = latestShakeStrength;
    shakeFor = latestShakeFor;
    portEXIT_CRITICAL(&motionLock);
  } else {
    ev = shaker.feed(lin, spin, now);
    shakeFor = shaker.shakingFor(now);
  }
  if (shaker.rattle() > 0) creature.shake(shaker.rattle());
  // Gestures that stand in for touch (see MotionGestures.h).
  TiltFlickDetector::Dir flick = flicker.feed(a, g, shaker.strength, now);
  bool rocking = rocker.feed(a, g, now);
  if (LOG_SHAKE && shaker.strength > 1.0f && now - lastLogAt > 200) {
    lastLogAt = now;
    FACE_LOG(printf, "shake: strength %.1f (dizzy at %.1f, kept up %.1f of %.1f s)  jolt %.1f m/s^2\n",
                  shaker.strength, SHAKE_DIZZY_STRENGTH, shakeFor / 1000.0f,
                  SHAKE_DIZZY_HOLD_MS / 1000.0f, jolt);
  }
  if (ev == ShakeDetector::DIZZY) {
    // Wobble, spiral eyes, glare, calm down. Keep shaking: it spins again.
    lastActivity = now;
    sleepPreparing = false;
    creature.impact(lin[0], lin[1]);
    bool started = react(Mood::DIZZY, now, DIZZY_ANIM_MS);
    if (LOG_SHAKE) FACE_LOG(println, started ? "shake: DIZZY" : "shake: (already dizzy)");
    return;
  }
  if (ev == ShakeDetector::STARTLE) {
    lastActivity = now; // a deliberate shake has begun: an interaction
    sleepPreparing = false;
    creature.impact(lin[0] * .6f, lin[1] * .6f);
    if (creature.mood() != Mood::DIZZY && creature.mood() != Mood::ANGRY) react(Mood::SURPRISED, now, 650);
    return;
  }
  if (ev == ShakeDetector::STROKE) {
    creature.impact(lin[0] * .45f, lin[1] * .45f); // just slosh; not an interaction
    return;
  }
  if (sleepPreparing) return;
  if (flick != TiltFlickDetector::NONE) {
    // Tipped one way and back: the same as a swipe that way.
    static const int8_t FX[] = {0, -1, 1, 0, 0}, FY[] = {0, 0, 0, -1, 1};
    static const char *NAMES[] = {"", "left", "right", "up", "down"};
    if (LOG_SHAKE) FACE_LOG(printf, "motion: flick %s\n", NAMES[flick]);
    beginSwipe(int(SCREEN_CX), int(SCREEN_CY), FX[flick] * 60, FY[flick] * 60, now);
    return;
  }
  if (rocking) {
    // Rocked gently: it feels petted for as long as the rocking goes on.
    lastActivity = now;
    if (!creature.sustain(Mood::PETTED, now, 1400)) {
      if (LOG_SHAKE) FACE_LOG(println, "motion: rocking");
      react(Mood::PETTED, now, 1400);
    }
    return;
  }
  if (ev == ShakeDetector::BUMP && !samplerOn) {
    // A knock on the resting star (when knocks are not counted on the fast
    // sampler): the eyes recoil and it looks startled.
    creature.impact(lin[0] * .6f, lin[1] * .6f);
    creature.reactPassive(Mood::SURPRISED, now, 520);
  } else if (jolt > PICKUP_MS2 && stillFor > 1200 && creature.mood() == Mood::IDLE) {
    // Bumped, nudged or picked up while it was calm: it notices.
    creature.notice(jolt / 8.0f, now);
    if (stillFor > 20000) {
      // Picked up after a long rest: "oh!" then a happy hello.
      react(Mood::SURPRISED, now, 520);
      creature.queue(Mood::HAPPY, now + 540, 1200);
      if (LOG_SHAKE) FACE_LOG(println, "motion: picked up after a rest -> hello");
    }
  }
}

// Knocks on the case, counted by the fast sampler: the same as taps.
void handleKnocks(uint32_t now) {
  KnockReport k;
  while (nextKnock(k)) {
    if (LOG_SHAKE) FACE_LOG(printf, "motion: knock %u (push %.1f, %.1f m/s^2)\n", k.count, k.x, k.y);
    if (sleepPreparing) {
      if (!creature.sleepFinished(now)) react(Mood::SURPRISED, now, 900);
      continue;
    }
    // A knock from the left pushes the star right: the nearer (left) eye flinches.
    float side = fabsf(k.x) > fabsf(k.y) * .5f ? (k.x > 0 ? -1.0f : 1.0f) : 0.0f;
    creature.impact(k.x * .5f, k.y * .5f);
    tapReaction(min(4, int(k.count)), int(SCREEN_CX + side * SCREEN_MIN_SIDE * .25f), int(SCREEN_CY), now);
  }
}

// The eyes follow the world: they look toward the low side when it is
// tilted, the face rolls to stay level when it is turned, the eyes
// counter-move to keep looking at you when it is swung, and spinning it
// around, tossing it or bumping it get their own reactions. a and g are in the
// screen's frame (see readMotion).
void followWorld(const float a[3], const float g[3], uint32_t now, bool allowReactions = true) {
  static WorldFollower world;
  world.feed(a, g, shaker.strength, now);
  creature.setTilt(world.tiltX, world.tiltY);
  creature.setFaceRoll(FACE_STAYS_LEVEL ? world.roll : 0.0f);
  creature.setSwing(world.swingX, world.swingY);
  tiltX = world.tiltX;
  tiltY = world.tiltY;
  if (allowReactions && world.weightless && !sleepPreparing) {
    creature.impact(0, 12);                 // the eyes float up
    react(Mood::SURPRISED, now, 1300);
    if (LOG_SHAKE) FACE_LOG(println, "motion: weightless (tossed or dropped)");
  }
  if (allowReactions && world.spun && SPIN_MAKES_DIZZY) {
    if (LOG_SHAKE) FACE_LOG(println, "motion: spun around -> dizzy");
    creature.impact(linX + 8, linY);
    react(Mood::DIZZY, now, DIZZY_ANIM_MS);
  }
}

void handleMotion(uint32_t now) {
  if (!imuReady) return;
  if (sleepPreparing && creature.sleepFinished(now)) {
    // A dark fallback if WoM could not be armed: use the full wake gesture.
    float a[3], g[3];
    currentMotion(a, g);
    if (sleepingShake.feed(a, g, now)) {
      startWakeAnimation(now, true);
    }
    return;
  }
  static uint32_t lastSample = 0;
  if (now - lastSample < 25) return;
  lastSample = now;
  float a[3], g[3];
  currentMotion(a, g);
  bool carriedMotion;
  portENTER_CRITICAL(&motionLock);
  carriedMotion = latestCarriedMotion;
  portEXIT_CRITICAL(&motionLock);
  bool angryPause = creature.touchAngerPauseActive(now);
  followWorld(a, g, now, !carriedMotion && !angryPause);
  if (carriedMotion || angryPause) {
    // This motion is transport, not play: consume knocks queued during it and
    // leave lastActivity alone. The existing visual gaze follower still runs.
    KnockReport ignored;
    while (nextKnock(ignored)) {}
    shaker.strength = 0;
    return;
  }
  handleKnocks(now);
  if (activeTwist.feed(g, now) && TWIST_MAKES_DIZZY) {
    creature.impact(linX, linY);
    react(Mood::DIZZY, now, DIZZY_ANIM_MS); // tumbles, then glares
  } else if (TWIST_MAKES_DIZZY && activeTwist.progressing()) {
    // A partial deliberate twist counts; one incidental swing does not.
    lastActivity = now;
  }
  // Held up in front of someone: screen upright, in a hand (a little tremor,
  // not perfectly still like on a stand), not being walked around. It pays
  // attention: looks at them, happy blinks, and stays awake.
  {
    static uint32_t uprightSince = 0;
    float n = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    float planar = sqrtf(a[0] * a[0] + a[1] * a[1]);
    float spinNow = fabsf(g[0]) + fabsf(g[1]) + fabsf(g[2]);
    bool upright = planar > 7.5f && fabsf(n - 9.81f) < 1.2f && spinNow < .8f && !steps.walking;
    uprightSince = upright ? (uprightSince ? uprightSince : now) : 0;
    bool held = uprightSince && now - uprightSince > 600;
    creature.setHeldUp(held, now);
    // Gyro noise on a table and passive carrying are visual cues, not activity.
  }
  // A purposeful tilt held still is an interaction. A moving keychain should
  // not continually restart the idle timer while the wearer walks.
  static bool haveTiltAnchor = false;
  static float anchorX = 0, anchorY = 0, candidateX = 0, candidateY = 0;
  static uint32_t candidateSince = 0;
  static uint32_t lastTurnAt = 0;
  if (!haveTiltAnchor) {
    anchorX = tiltX; anchorY = tiltY;
    haveTiltAnchor = true;
  }
  float accelLength = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  bool heldStill = fabsf(accelLength - 9.81f) < .85f &&
                   fabsf(g[0]) + fabsf(g[1]) + fabsf(g[2]) < .45f;
  handleSurroundings(a, g, heldStill, now);
  if (fabsf(g[0]) + fabsf(g[1]) + fabsf(g[2]) > .5f && !steps.walking) lastTurnAt = now;
  bool changedTilt = fabsf(tiltX - anchorX) + fabsf(tiltY - anchorY)
                     > TILT_ACTIVITY_DELTA;
  if (heldStill && changedTilt && lastTurnAt && now - lastTurnAt < 1000 && !steps.walking) {
    if (!candidateSince || fabsf(tiltX - candidateX) + fabsf(tiltY - candidateY) > .12f) {
      candidateX = tiltX; candidateY = tiltY;
      candidateSince = now;
    } else if (now - candidateSince >= TILT_ACTIVITY_HOLD_MS) {
      lastActivity = now;
      anchorX = tiltX; anchorY = tiltY;
      candidateSince = 0;
      if (sleepPreparing) {
        if (!creature.sleepFinished(now)) react(Mood::CONFUSED, now, 700);
      }
    }
  } else candidateSince = 0;
  // A face-down pause produces a puzzled, asymmetric expression; left face
  // down, it goes to sleep (a way to switch it off without the touch screen).
  static uint32_t faceDownSince = 0;
  static bool faceDownReacted = false;
  if (a[2] > 7.5f && fabsf(g[0]) + fabsf(g[1]) < 1.2f) {   // screen facing the floor
    if (!faceDownSince) faceDownSince = now;
    if (!faceDownReacted && now - faceDownSince > 500) {
      react(Mood::CONFUSED, now, 1000);
      faceDownReacted = true;
    }
    if (FACE_DOWN_SLEEP_MS && !sleepPreparing && now - faceDownSince > FACE_DOWN_SLEEP_MS) {
      if (LOG_SHAKE) FACE_LOG(println, "motion: face down, going to sleep");
      startSleepAnimation(now);
    }
  } else {
    faceDownSince = 0;
    faceDownReacted = false;
  }
  // Held upside down: worried, then cross, then furious, like a finger held
  // on the screen. Turned back over, it huffs if it got angry, or just looks
  // puzzled. "Upside down" is relative to how it is usually held (learned
  // after it has been held upright and steady for a second, then slowly
  // adapted), so it works whichever way the sensor sits in the enclosure.
  static bool upside = false, haveUsual = false;
  static uint32_t upsideCandidate = 0, uprightSince = 0;
  static float usualX = 0, usualY = 1;
  float planar = sqrtf(a[0] * a[0] + a[1] * a[1]);  // gravity across the screen
  float dirX = planar > .1f ? a[0] / planar : 0, dirY = planar > .1f ? a[1] / planar : 0;
  bool upright = planar > 7.0f && fabsf(g[0]) + fabsf(g[1]) + fabsf(g[2]) < .6f;
  if (!upside && upright) {
    if (!haveUsual) {
      uprightSince = uprightSince ? uprightSince : now;
      if (now - uprightSince >= 1000) { usualX = dirX; usualY = dirY; haveUsual = true; }
    } else if (dirX * usualX + dirY * usualY > .5f) {
      usualX += (dirX - usualX) * .002f;  // drifts toward how it is held (~15 s)
      usualY += (dirY - usualY) * .002f;
      float n = sqrtf(usualX * usualX + usualY * usualY);
      usualX /= n; usualY /= n;
    }
  } else if (!haveUsual) {
    uprightSince = 0;
  }
  float facing = dirX * usualX + dirY * usualY;    // 1 as usual, -1 upside down
  bool upsideNow = haveUsual && planar > 6.5f && facing < -.75f && shaker.strength < 1.5f;
  if (!upside) {
    upsideCandidate = upsideNow ? (upsideCandidate ? upsideCandidate : now) : 0;
    if (upsideCandidate && now - upsideCandidate >= UPSIDE_DOWN_MS && !sleepPreparing) {
      upside = true;
      creature.setUpsideDown(true, now);
      react(Mood::UPSIDE_DOWN, now, 600);
      if (LOG_SHAKE) FACE_LOG(println, "motion: upside down");
    }
  } else if (planar < 4.0f || facing > -.4f || sleepPreparing) {
    upside = false;
    upsideCandidate = 0;
    float anger = creature.annoyance();
    creature.setUpsideDown(false, now);
    if (anger > .35f && !sleepPreparing) creature.huff(anger, now);
    else if (!sleepPreparing) react(Mood::CONFUSED, now, 700);
    if (LOG_SHAKE) FACE_LOG(printf, "motion: turned back over (anger %.2f)\n", anger);
  } // Remaining in one upside-down orientation is not ongoing interaction.
  if (now - lastTempRead >= 1000) {
    lastTempRead = now;
    {
      I2CGuard bus;
      dieTempC = QMI8658_readTemp();
    }
    // This is the IMU's die temperature, not a true ambient thermometer.
    if (dieTempC > -30 && dieTempC < 18) {
      if (!coldSince) coldSince = now;
      if (now - coldSince > 8000 && now - lastColdReaction > 6000) {
        creature.reactPassive(Mood::SHIVER, now, 1700);
        lastColdReaction = now;
      }
    } else coldSince = 0;
    // Warm (a pocket, a hand, the sun): lazy and yawny now and then.
    static uint32_t hotSince = 0, lastYawn = 0;
    if (dieTempC > HOT_C && dieTempC < 90) {
      if (!hotSince) hotSince = now;
      if (now - hotSince > 10000 && now - lastYawn > 25000) {
        creature.reactPassive(Mood::YAWN, now, 1900);
        lastYawn = now;
      }
    } else hotSince = 0;
  }
  // Idle eye motion and merely being held up do not restart the activity timer.
}

void touchRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(0x15);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

void configureTouchWake() {
  // Keep auto-standby enabled. In standby, touch generates an active-low IRQ.
  // Enable the chip's double-click recognizer as a backup if the second touch
  // IRQ happens while the ESP32 is still booting.
  touchRegister(0xEC, 0x01);
  // 20 ms pulse makes it easier for EXT1 to catch than the 1 ms default.
  touchRegister(0xED, 200);
  Wire.beginTransmission(0x15);
  Wire.write(0xFA);
  uint8_t irqCtl = 0;
  if (Wire.endTransmission(false) == 0 && Wire.requestFrom(0x15, 1) == 1)
    irqCtl = Wire.read();
  touchRegister(0xFA, irqCtl | 0x40);
}

uint8_t rawTouchGesture() {
  Wire.beginTransmission(0x15);
  Wire.write(0x01);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(0x15, 1) != 1)
    return 0;
  return Wire.read();
}

uint8_t rawTouchFingerCount() {
  Wire.beginTransmission(0x15);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(0x15, 1) != 1)
    return 0xFF; // unknown; do not mistake an I2C failure for a release
  return Wire.read();
}

bool confirmTwoPressWake() {
  // The first tap woke the ESP32. Keep the LCD dark until the controller
  // reports a double click or a second distinct press IRQ arrives.
  pinMode(TOUCH_WAKE_PIN, INPUT_PULLUP);
  uint32_t began = millis(), releasedAt = 0;
  bool firstReleased = rawTouchFingerCount() == 0;
  bool irqHigh = digitalRead(TOUCH_WAKE_PIN) == HIGH;
  if (firstReleased) releasedAt = began;
  while (millis() - began < TOUCH_TWO_PRESS_MS) {
    uint32_t now = millis();
    if (rawTouchGesture() == 0x0B) return true;
    uint8_t fingers = rawTouchFingerCount();
    bool high = digitalRead(TOUCH_WAKE_PIN) == HIGH;
    if (!firstReleased && fingers == 0) {
      firstReleased = true;
      releasedAt = now;
    }
    if (!high && irqHigh && firstReleased && fingers > 0 && fingers != 0xFF &&
        now - releasedAt >= 35 && now - began >= 60) {
      return true;
    }
    irqHigh = high;
    delay(5);
  }
  return false;
}

void rawImuWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(0x6B); // address of QMI8658 on this Waveshare board
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t rawImuRead(uint8_t reg) {
  Wire.beginTransmission(0x6B);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(0x6B, 1) != 1) return 0;
  return Wire.read();
}

void exitMotionWake() {
  // The IMU keeps WoM state through ESP32 deep sleep AND upload/reset. Rev 0.9 requires the
  // zero-threshold CTRL9 command before returning to normal accel/gyro mode.
  rawImuRead(QMI8658Register_Status1); // release the latched interrupt
  rawImuWrite(QMI8658Register_Ctrl7, 0);
  rawImuWrite(QMI8658Register_Ctrl8, 0x80);
  rawImuWrite(QMI8658Register_Cal1_L, 0);
  rawImuWrite(QMI8658Register_Ctrl9, QMI8658_Ctrl9_Cmd_WoM_Setting);
  for (int i = 0; i < 60; ++i) {
    if (rawImuRead(QMI8658Register_StatusInt) & 0x80) break;
    delay(2);
  }
  rawImuWrite(QMI8658Register_Ctrl9, 0x00); // acknowledge the command
}

bool armMotionWake() {
  // The bundled Waveshare WoM helper never sends CTRL9 and uses INT1.
  // This board connects INT2 to RTC-capable GPIO3.
  QMI8658_enableSensors(QMI8658_CTRL7_DISABLE_ALL);
  QMI8658_config_acc(QMI8658AccRange_2g, QMI8658AccOdr_LowPower_21Hz,
                     QMI8658Lpf_Disable, QMI8658St_Disable);
  QMI8658_write_reg(QMI8658Register_Ctrl8, 0x80); // route CTRL9 done to STATUSINT bit 7
  QMI8658_write_reg(QMI8658Register_Cal1_L, WOM_THRESHOLD_MG);
  QMI8658_write_reg(QMI8658Register_Cal1_H, 0xC0 | 4); // INT2 idle high, 4-sample blanking
  QMI8658_write_reg(QMI8658Register_Ctrl9, QMI8658_Ctrl9_Cmd_WoM_Setting);
  bool accepted = false;
  uint8_t lastStatus = 0;
  for (int i = 0; i < 60; ++i) {
    uint8_t status = 0;
    // Rev 0.9 silicon signals CTRL9 completion in STATUSINT bit 7.
    // The older bundled header incorrectly names STATUS1 bit 0.
    QMI8658_read_reg(QMI8658Register_StatusInt, &status, 1);
    lastStatus = status;
    if (status & 0x80) { accepted = true; break; }
    delay(2);
  }
  if (!accepted) {
    uint8_t ctrl2 = 0, ctrl7 = 0, ctrl8 = 0, ctrl9 = 0, calL = 0, calH = 0;
    QMI8658_read_reg(QMI8658Register_Ctrl2, &ctrl2, 1);
    QMI8658_read_reg(QMI8658Register_Ctrl7, &ctrl7, 1);
    QMI8658_read_reg(QMI8658Register_Ctrl8, &ctrl8, 1);
    QMI8658_read_reg(QMI8658Register_Ctrl9, &ctrl9, 1);
    QMI8658_read_reg(QMI8658Register_Cal1_L, &calL, 1);
    QMI8658_read_reg(QMI8658Register_Cal1_H, &calH, 1);
    FACE_LOG(printf, "WoM regs: statusInt=%02X ctrl2=%02X ctrl7=%02X ctrl8=%02X ctrl9=%02X cal=%02X,%02X\n",
                  lastStatus, ctrl2, ctrl7, ctrl8, ctrl9, calL, calH);
    return false;
  }
  // Acknowledge the command so the next one is accepted (CTRL_CMD_ACK).
  QMI8658_write_reg(QMI8658Register_Ctrl9, 0x00);
  for (int i = 0; i < 30; ++i) {
    uint8_t st = 0;
    QMI8658_read_reg(QMI8658Register_StatusInt, &st, 1);
    if (!(st & 0x80)) break;
    delay(1);
  }
  QMI8658_enableSensors(QMI8658_CTRL7_ACC_ENABLE);
  // The QMI8658A keeps its INT pins high-impedance until CTRL1 bit 4 (INT2)
  // is set; without this the motion alarm never reaches GPIO3, so shaking
  // could not wake the board. (The Waveshare init writes CTRL1 = 0x60.)
  uint8_t ctrl1 = 0;
  QMI8658_read_reg(QMI8658Register_Ctrl1, &ctrl1, 1);
  QMI8658_write_reg(QMI8658Register_Ctrl1, ctrl1 | 0x10);
  uint8_t status = 0;
  QMI8658_read_reg(QMI8658Register_Status1, &status, 1); // clear stale WoM event
  return true;
}

void enterCriticalSleep(bool lcdReady) {
  criticalBatterySleep = true;
  stopMotionSampler();
  if (imuReady) {
    exitMotionWake();
    QMI8658_enableSensors(QMI8658_CTRL7_DISABLE_ALL);
  } else rawImuWrite(QMI8658Register_Ctrl7, 0); // early boot: disable without normal-mode init
  if (!lcdReady) { DEV_Module_Init(); DEV_SET_PWM(0); }
  LCD_1IN28_Sleep();
  if (TOUCH_ENABLED) touch.sleep(); // critical sleep has no touch wake
  analogWrite(LCD_BL_PIN, 0);
  pinMode(LCD_BL_PIN, OUTPUT);
  digitalWrite(LCD_BL_PIN, LOW);
  gpio_hold_en(GPIO_NUM_2);
  gpio_deep_sleep_hold_en();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_timer_wakeup(uint64_t(BATTERY_RECHECK_S) * 1000000ULL);
  FACE_LOG(println, "Battery <=3%: sleeping; shake/touch disabled until charge recovers");
  Serial.flush();
  esp_deep_sleep_start();
}

bool enterDeepSleep(bool lcdReady) {
  if (batteryLow()) { enterCriticalSleep(lcdReady); return true; }
  stopMotionSampler(); // the IMU is reconfigured below
  if (!lcdReady) {
    // Cold/reset boot and rejected wakes also put the LCD controller to sleep,
    // not just its backlight. Initialize the bus without lighting the panel.
    DEV_Module_Init();
    DEV_SET_PWM(0);
    LCD_1IN28_Sleep();
    if (TOUCH_ENABLED) touch.begin(FALLING); // restart from the prior touch-disabled firmware
  }
  if (TOUCH_ENABLED) configureTouchWake();
  if (imuReady && !armMotionWake()) {
    FACE_LOG(println, "WoM setup failed; staying dark and retrying in 5 s");
    exitMotionWake();
    QMI8658_init();
    tuneImuFilters();
    startMotionSampler();
    sleepRetryAt = millis() + 5000;
    return false;
  }
  pinMode(IMU_WAKE_PIN, INPUT_PULLUP);
  pinMode(TOUCH_WAKE_PIN, INPUT_PULLUP);
  // A currently asserted wake line causes an instant reboot. Let it clear.
  for (int i = 0; imuReady && i < 25 && digitalRead(IMU_WAKE_PIN) == LOW; ++i) {
    uint8_t status = 0;
    QMI8658_read_reg(QMI8658Register_Status1, &status, 1);
    delay(10);
  }
  if (imuReady && digitalRead(IMU_WAKE_PIN) == LOW) {
    exitMotionWake();
    QMI8658_init();
    tuneImuFilters();
    startMotionSampler();
    sleepRetryAt = millis() + 5000;
    return false;
  }
  if (lcdReady) LCD_1IN28_Sleep();
  analogWrite(LCD_BL_PIN, 0);
  pinMode(LCD_BL_PIN, OUTPUT);
  digitalWrite(LCD_BL_PIN, LOW);
  gpio_hold_en(GPIO_NUM_2);
  gpio_deep_sleep_hold_en();
  uint64_t pins = imuReady ? 1ULL << IMU_WAKE_PIN : 0;
  // Restore testing: touch IRQ and IMU INT2 can wake the CPU.
  if (TOUCH_ENABLED && !touchStuck && digitalRead(TOUCH_WAKE_PIN) == HIGH) pins |= 1ULL << TOUCH_WAKE_PIN;
  // Keep both wake lines pulled up while asleep so neither floats low (a
  // phantom wake) or can't be pulled low (no wake). Digital pull-ups set by
  // pinMode() switch off in deep sleep; the RTC ones need RTC_PERIPH powered.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  for (gpio_num_t pin : {gpio_num_t(IMU_WAKE_PIN), gpio_num_t(TOUCH_WAKE_PIN)}) {
    rtc_gpio_pullup_en(pin);
    rtc_gpio_pulldown_dis(pin);
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  if (pins) esp_sleep_enable_ext1_wakeup(pins, ESP_EXT1_WAKEUP_ANY_LOW);
  if (!pins) esp_sleep_enable_timer_wakeup(uint64_t(BATTERY_RECHECK_S) * 1000000ULL);
  FACE_LOG(println, "Entering deep sleep");
  Serial.flush();
  esp_deep_sleep_start();
  return true;
}

// After the IMU's coarse motion alarm woke the CPU (screen still dark): turn
// the face on only after the recorded left/right pattern for about 2 seconds.
// Anything less, like walking or a short shake, goes straight back to sleep.
bool confirmMotionWake() {
  delay(70); // let the gyro settle after leaving low-power mode
  uint32_t began = millis(), lastLogAt = 0;
  LeftRightWakeCheck shake;
  while (millis() - began < SHAKE_WAKE_HOLD_MS + 3000) {
    float a[3], g[3];
    readMotion(a, g);
    uint32_t now = millis();
    if (shake.feed(a, g, now)) return true;
    if (LOG_SHAKE && now - lastLogAt >= 500) {
      lastLogAt = now;
      FACE_LOG(printf, "wake: left/right %lu / %lu ms, AY %.0f%%, GZ %.0f%%, Z/Y %.2f, swings %u%s\n",
                    (unsigned long)shake.shakingFor(now), (unsigned long)SHAKE_WAKE_HOLD_MS,
                    shake.accelShare * 100, shake.gyroShare * 100, shake.zOverY,
                    shake.swings, shake.carryingMotion() ? " (carried motion ignored)" : "");
    }
    // Save the battery: give up quickly if no shake starts, or once it stops.
    if (!shake.shakingFor(now) && (now - began > 1200 || shake.stopped(now))) break;
    delay(10);
  }
  if (LOG_SHAKE) FACE_LOG(println, "wake check: no sustained left/right gesture; back to sleep");
  return false;
}

void renderFrame() {
  uint32_t nowUs = micros();
  bool napping = sleepPreparing && creature.sleepFinished(millis());
  if (nowUs - lastFrameUs < (napping ? NAP_FRAME_US : FRAME_US)) return;
  // Frame-independent animation: everything advances by real elapsed time.
  float dt = (nowUs - lastFrameUs) / 1000000.0f;
  lastFrameUs = nowUs;
  if (dt > MAX_FRAME_DT) dt = MAX_FRAME_DT;
  creature.update(dt, millis());
  eyes.draw();
  applyBacklight(creature.backlight());
  if (LOG_FPS) {
    static uint32_t frames = 0, windowStart = 0;
    ++frames;
    if (millis() - windowStart >= 5000) {
      FACE_LOG(printf, "%.1f FPS\n", frames * 1000.0f / (millis() - windowStart));
      frames = 0;
      windowStart = millis();
    }
  }
}

void setup() {
  Serial.begin(115200);
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(GPIO_NUM_2);
  // Pins that woke the chip stay in RTC mode until released; GPIO5 must be a
  // normal GPIO again for the touch interrupt.
  rtc_gpio_deinit(gpio_num_t(IMU_WAKE_PIN));
  rtc_gpio_deinit(gpio_num_t(TOUCH_WAKE_PIN));
  pinMode(LCD_BL_PIN, OUTPUT);
  digitalWrite(LCD_BL_PIN, LOW);

  Wire.begin(6, 7);
  Wire.setClock(400000);
  analogReadResolution(12);
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
  readBattery(); // before lighting the screen or accepting a wake
  if (BatteryState::mustRemainAsleep(batteryPercent, batteryVolts, criticalBatterySleep)) {
    enterCriticalSleep(false);
    return;
  }
  criticalBatterySleep = false;
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  uint64_t source = cause == ESP_SLEEP_WAKEUP_EXT1 ? esp_sleep_get_ext1_wakeup_status() : 0;
  FACE_LOG(printf, "Wake cause=%d, pins=0x%llX\n", int(cause), source);
  bool touchWake = TOUCH_ENABLED && (source & (1ULL << TOUCH_WAKE_PIN)) != 0;
  // A touch wakes it straight away (or needs a second press when
  // TOUCH_WAKE_DOUBLE_PRESS is set, to avoid waking in a pocket).
  bool touchConfirmed = touchWake && (RAW_IMU_SERIAL_ONLY || !TOUCH_WAKE_DOUBLE_PRESS || confirmTwoPressWake());
  // Upload/reset restarts only the ESP32, not the separately powered IMU.
  // Clear retained WoM even when this boot was not an EXT1 sleep wake.
  exitMotionWake();
  imuReady = QMI8658_init() != 0;
  if (imuReady) tuneImuFilters();
  FACE_LOG(println, imuReady ? "Motion sensor ready" :
                 "Motion sensor NOT found: shake, tilt and shake-wake are disabled");
  if (!RAW_IMU_SERIAL_ONLY && START_ASLEEP && !source) {
    FACE_LOG(println, "Starting asleep; touch or shake left/right for two seconds to wake");
    if (enterDeepSleep(false)) return;
  }
  if (!RAW_IMU_SERIAL_ONLY && touchWake && !touchConfirmed && !(source & (1ULL << IMU_WAKE_PIN))) {
    FACE_LOG(println, "Single touch wake rejected; returning to sleep");
    if (enterDeepSleep(false)) return;
  }
  if (!RAW_IMU_SERIAL_ONLY && imuReady && (source & (1ULL << IMU_WAKE_PIN)) && !touchConfirmed) {
    // Motion wakes the CPU but not the LCD. Only a real shake (or twist)
    // turns the face on; it then wakes startled, dizzy and grumpy.
    if (!confirmMotionWake()) {
      FACE_LOG(println, "Motion wake rejected; returning to sleep");
      if (enterDeepSleep(false)) return;
    } else wokeByShake = true;
  }
  // Internal RAM is much faster than PSRAM for per-pixel blending.
  size_t bytes = W * H * sizeof(uint16_t);
  BlackImage = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!BlackImage) BlackImage = (uint16_t *)ps_malloc(bytes);
  if (!BlackImage) BlackImage = (uint16_t *)malloc(bytes);
  if (!BlackImage) { FACE_LOG(println, "Display buffer allocation failed"); return; }
  DEV_Module_Init();
  DEV_SET_PWM(0);
  LCD_1IN28_Init(HORIZONTAL);
  if (TOUCH_ENABLED) {
    touch.begin(FALLING);
    // Do not treat the waking press as a second interaction.
    if (touchConfirmed) finger.ignoreUntilLift(millis());
  }
  Wire.setClock(400000);
  if (TOUCH_ENABLED) configureTouchWake();
  else FACE_LOG(println, "Touch disabled (TOUCH_ENABLED = false): motion gestures only");
  startMotionSampler(); // knocks and fast motion, on core 0
  randomSeed(esp_random());
  uint64_t mac = ESP.getEfuseMac();
  uint32_t unitSeed = uint32_t(mac ^ (mac >> 32)); // small per-unit quirks in the idle motion
  lastActivity = millis();
  renderer.begin(BlackImage, LCD_1IN28_DisplayWindows);
  eyes.begin(&renderer, unitSeed, lastActivity);
  creature.begin(&eyes, unitSeed, lastActivity);
  readBattery();
  lastBatteryCheck = lastActivity;
  if (!RAW_IMU_SERIAL_ONLY && START_ASLEEP && !touchConfirmed && !wokeByShake) {
    // Sleep setup failed: remain black while retrying; no automatic wake animation.
    sleepPreparing = true;
    deepSleepPlanned = true;
    creature.startSleep(lastActivity - SLEEP_SEQUENCE_MS, 0.0f);
  } else startWakeAnimation(lastActivity, wokeByShake, touchConfirmed);
  if (wokeByShake) FACE_LOG(println, "Shake confirmed; waking into dizzy eyes");
  else if (touchConfirmed) FACE_LOG(println, "Touch wake confirmed; waking face");
  // First frame: the whole screen is cleared while the backlight is still off.
  lastFrameUs = micros() - FRAME_US;
  renderFrame();
}

void loop() {
  printRawImu(millis());
  if (!BlackImage) { delay(1000); return; }
  uint32_t now = millis();
  if (now - lastBatteryCheck >= BATTERY_CHECK_MS) {
    lastBatteryCheck = now;
    readBattery();
  }
  if (batteryLow()) {
    // Critical shutdown is immediate and cannot be canceled by any interaction.
    if (!sleepPreparing || !creature.asleep()) startSleepAnimation(now);
    if (creature.sleepFinished(now)) enterCriticalSleep(true);
    renderFrame();
    delay(1);
    return;
  }
  if (!RAW_IMU_SERIAL_ONLY && (forcedClosing || awakeLimit.closingDue(now))) {
    if (!forcedClosing) {
      forcedClosing = true;
      creature.setPointerHeld(false, now);
      startSleepAnimation(awakeLimit.closingAt());
      deepSleepPlanned = true; // the 30-second limit always powers down
      FACE_LOG(println, "30-second awake limit: finishing sleep regardless of interaction");
    }
    if (!creature.sleepFinished(now)) {
      renderFrame();
      delay(1);
      return; // touch, shakes and other reactions cannot cancel this closing
    }
    awakeLimit.finish();
    forcedClosing = false;
  }
  if (sleepPreparing && creature.sleepFinished(now)) {
    awakeLimit.finish();
    if (deepSleepPlanned) applyBacklight(0); // darkness precedes any sensor handshake/retry
    if (deepSleepPlanned && int32_t(now - sleepRetryAt) >= 0 && enterDeepSleep(true)) return;
    handleTouch(now); // restored touch wake when sleep setup needs a retry
    if (sleepPreparing && TOUCH_ENABLED && !touchStuck && digitalRead(TOUCH_WAKE_PIN) == LOW)
      cancelSleepForTouch(now);
    handleMotion(now); // dark fallback requires the same sustained shake check
    renderFrame();
    delay(1);
    return;
  }
  handleTouch(now);
  if (TOUCH_ENABLED && !touchStuck && sleepPreparing && digitalRead(TOUCH_WAKE_PIN) == LOW)
    cancelSleepForTouch(now);
  handleMotion(now);
  creature.setPointerHeld(finger.tracking() && !touchStuck, now);
  uint32_t timeout = idleTimeout();
  if (sleepPreparing) {
    // Napping keeps the screen on. Only planned deep sleep or a nearly empty
    // battery actually switches it off.
    if (creature.sleepFinished(now) && deepSleepPlanned && int32_t(now - sleepRetryAt) >= 0) {
      if (enterDeepSleep(true)) return;
    }
  } else if (!RAW_IMU_SERIAL_ONLY && now - lastActivity >= sleepAnimationDelay(timeout) && int32_t(now - sleepRetryAt) >= 0) {
    startSleepAnimation(now);
  }
  if (!sleepPreparing) {
    float idle = (now - lastActivity) / float(timeout);
    // Lively right up to the end: heavy lids only in the last couple of seconds.
    creature.setDrowsiness(RAW_IMU_SERIAL_ONLY ? 0.0f : anim::smoothstep(.82f, 1.0f, idle));
    creature.setIdleActsAllowed(RAW_IMU_SERIAL_ONLY || now - lastActivity + 1800 < timeout);
  }
  renderFrame();
  delay(1);
}
