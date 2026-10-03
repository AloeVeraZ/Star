#include <Arduino.h>
#include <math.h>
#include "esp_sleep.h"
#include "esp_heap_caps.h"
#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include <Preferences.h>
#include "DEV_Config.h"
#include "LCD_1in28.h"
#include "QMI8658.h"
#include "CST816S.h"
#include "FaceConfig.h"
#include "EyeRenderer.h"
#include "CreatureAnimator.h"

// Star Face for Waveshare ESP32-S3-Touch-LCD-1.28 (240x240 round).
// Hardware, input and power live here; all eye animation is in CreatureAnimator,
// all drawing in EyeRenderer. Eye size, layout, colours, animation feel, shake
// sensitivity and sleep timing are tuned in FaceConfig.h.
uint16_t *BlackImage = nullptr; // Required by Waveshare's LCD_1in28.cpp.
CST816S touch(6, 7, 13, 5);
EyeRenderer renderer;
CreatureAnimator creature;

static constexpr int W = 240, H = 240;
static constexpr uint32_t FRAME_US = 16667;      // frame cap (~60 FPS); render time sets the real rate
static constexpr uint32_t NAP_FRAME_US = 33333;  // 30 FPS is plenty while the eyes are shut
static constexpr float MAX_FRAME_DT = .05f;      // a stall never turns into an animation jump
static constexpr bool LOG_FPS = false;           // print frames per second over serial
// Prints shake strength while it is being shaken (silent when still). Open the
// serial monitor at 115200 to see whether the motion sensor responds and how
// hard your shakes are; set false once tuned.
static constexpr bool LOG_SHAKE = true;

// Power (sleep timeouts and backlight levels are in FaceConfig.h).
static constexpr int LOW_BATTERY_PERCENT = 3;    // a nap at or below this becomes deep sleep
static constexpr uint32_t BATTERY_CHECK_MS = 60000;

static constexpr uint32_t TOUCH_TWO_PRESS_MS = 3000;
static constexpr uint32_t TWIST_WINDOW_MS = 3000;
static constexpr uint32_t TWIST_MAX_PAUSE_MS = 600;
static constexpr float TWIST_RATE_RAD_S = 1.2f; // about 69 degrees/s on X or Y
static constexpr float TWIST_MIN_HALF_TURN_RAD = .25f; // about 14 degrees each way
static constexpr uint8_t TWIST_REVERSALS_TO_WAKE = 3;
static constexpr uint8_t WOM_THRESHOLD_MG = 90; // coarse low-power alert; gyro verifies the twist
static constexpr float TILT_ACTIVITY_DELTA = .55f; // held orientation change, not a walking jostle
static constexpr uint32_t TILT_ACTIVITY_HOLD_MS = 300;
static constexpr int IMU_WAKE_PIN = 3;    // QMI8658 INT2, active low in WoM
static constexpr int TOUCH_WAKE_PIN = 5;  // CST816S IRQ, active-low touch pulse

uint32_t lastActivity = 0, lastFrameUs = 0;
bool sleepPreparing = false, deepSleepPlanned = false;
bool imuReady = false, wokeByShake = false;
float tiltX = 0, tiltY = 0;
float linX = 0, linY = 0; // smoothed gravity-free acceleration
int touchStartX = -1, touchStartY = -1;
uint32_t touchStartAt = 0;
uint32_t lastTouchActionAt = 0, lastSwipeAt = 0;
uint32_t sleepRetryAt = 0, lastBatteryCheck = 0;
uint8_t eyeLook = 0, palette = 0, personality = 0, tapStreak = 0;
uint32_t lastTapAt = 0, lastLongAt = 0;
float dieTempC = 25.0f, batteryVolts = 0;
int batteryPercent = -1;
uint32_t lastTempRead = 0, coldSince = 0, lastColdReaction = 0;
Preferences prefs;

struct TwistAxis {
  uint32_t startedAt = 0, lastMoveAt = 0;
  float halfTurn = 0;
  int8_t direction = 0;
  uint8_t reversals = 0;

  void reset() {
    startedAt = lastMoveAt = 0;
    halfTurn = 0;
    direction = 0;
    reversals = 0;
  }

  bool feed(float rate, uint32_t now, uint32_t dt) {
    if (startedAt && (now - lastMoveAt > TWIST_MAX_PAUSE_MS ||
                      now - startedAt > TWIST_WINDOW_MS)) reset();
    if (fabsf(rate) < TWIST_RATE_RAD_S) return false;
    int8_t nextDirection = rate > 0 ? 1 : -1;
    float step = fabsf(rate) * min(uint32_t(60), dt) / 1000.0f;
    if (!startedAt) {
      startedAt = now;
      direction = nextDirection;
      halfTurn = step;
    } else if (direction == nextDirection) {
      halfTurn += step;
    } else {
      // A reversal only counts after a real angular sweep, not gyro noise.
      if (halfTurn >= TWIST_MIN_HALF_TURN_RAD) ++reversals;
      else reversals = 0;
      direction = nextDirection;
      halfTurn = step;
    }
    lastMoveAt = now;
    if (reversals >= TWIST_REVERSALS_TO_WAKE &&
        halfTurn >= TWIST_MIN_HALF_TURN_RAD) {
      reset();
      return true;
    }
    return false;
  }
};

struct TwistDetector {
  TwistAxis axis[2]; // board X and Y run parallel to the screen
  uint32_t lastSampleAt = 0;

  bool feed(const float gyro[3], uint32_t now) {
    uint32_t dt = lastSampleAt ? now - lastSampleAt : 25;
    lastSampleAt = now;
    bool x = axis[0].feed(gyro[0], now, dt);
    bool y = axis[1].feed(gyro[1], now, dt);
    return x || y;
  }

  bool active() const { return axis[0].startedAt || axis[1].startedAt; }
  bool progressing() const {
    return axis[0].reversals >= 2 || axis[1].reversals >= 2;
  }
};
TwistDetector activeTwist;

void chooseLook(uint8_t look, uint8_t color, bool save, bool instant) {
  eyeLook = look % EYE_LOOK_COUNT;
  palette = color % EYE_PALETTE_COUNT;
  creature.changeLook(eyeLook, palette, instant);
  if (save) {
    prefs.putUChar("look", eyeLook);
    prefs.putUChar("colorV2", palette);
  }
}

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
  return batteryPercent >= 0 && batteryPercent <= LOW_BATTERY_PERCENT;
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
  sleepPreparing = false;
  lastActivity = now;
  creature.startWake(now, fromShake, touched);
}

void startSleepAnimation(uint32_t now) {
  sleepPreparing = true;
  deepSleepPlanned = imuReady && (AUTO_DEEP_SLEEP || batteryLow());
  float napLevel = deepSleepPlanned ? 0.0f :
                   sqrtf(NAP_BACKLIGHT_PERCENT / float(BACKLIGHT_PERCENT));
  creature.startSleep(now, napLevel);
  Serial.printf("%s after %lu ms idle\n", deepSleepPlanned ? "Falling asleep" : "Dozing off",
                (unsigned long)(now - lastActivity));
}

// Returns true when the touch woke the creature from a finished nap, so that
// same touch is not also treated as a tap.
bool cancelSleepForTouch(uint32_t now) {
  if (!sleepPreparing) return false;
  if (creature.sleepFinished(now)) {
    startWakeAnimation(now, false, true);
    Serial.println("Nap ended by touch");
    return true;
  }
  react(SURPRISED, now, 900);
  Serial.println("Sleep animation canceled by touch");
  return false;
}

void readBattery() {
  // Waveshare connects GPIO1 to the cell via a 200k/100k divider.
  uint32_t sum = 0;
  for (int i = 0; i < 8; ++i) sum += analogReadMilliVolts(BAT_ADC_PIN);
  batteryVolts = (sum / 8.0f) * .003f;
  if (batteryVolts < 2.8f || batteryVolts > 4.4f) {
    batteryPercent = -1; // no battery or invalid sample (e.g. USB only)
    return;
  }
  // A rough lithium-ion state-of-charge display, not a fuel gauge.
  static const float v[] = {3.30f, 3.55f, 3.70f, 3.78f, 3.86f, 3.95f, 4.08f, 4.20f};
  static const int pct[] = {0, 5, 15, 30, 50, 70, 90, 100};
  batteryPercent = 100;
  for (int i = 1; i < 8; ++i) {
    if (batteryVolts <= v[i]) {
      batteryPercent = pct[i - 1] + int((pct[i] - pct[i - 1]) *
                           (batteryVolts - v[i - 1]) / (v[i] - v[i - 1]));
      break;
    }
  }
  batteryPercent = constrain(batteryPercent, 0, 100);
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
  react(SWIPING, now, 740);
  if (sy < 0) {
    readBattery();
    creature.queue(BATTERY, now + 700, BATTERY_SHOW_MS, batteryPercent);
  } else if (sy > 0) creature.queue(SAD, now + 700, 1400);
  else if (sx < 0) creature.queue(SHY, now + 700, 1400);
  else creature.queue(HAPPY, now + 700, 1400);
}

void pointAt(int x, int y, uint32_t now) {
  creature.setTouchPoint(x, y);
  creature.setPointer((x - 120) / 80.0f, (y - 120) / 80.0f, now);
}

void finishTap(int x, int y, uint32_t now) {
  if (now - lastTouchActionAt < 75) return;
  lastTouchActionAt = now;
  pointAt(x, y, now);
  tapStreak = now - lastTapAt < 430 ? min(3, int(tapStreak) + 1) : 1;
  if (tapStreak >= 3) { react(ANXIOUS, now, 1300); tapStreak = 0; }
  else if (tapStreak == 2) react(SURPRISED, now, 900);
  else react(BOOP, now, 680);
  lastTapAt = now;
}

void handleTouch(uint32_t now) {
  if (!touch.available()) return;
  auto d = touch.data;
  lastActivity = now;
  // Even if the controller reports a partial/invalid coordinate, its IRQ is
  // real activity. Never finish closing the eyes while a finger is present.
  if (cancelSleepForTouch(now)) { touchStartX = -1; return; }
  if (d.x < 0 || d.x >= W || d.y < 0 || d.y >= H) return;

  // The CST816S reports completed swipes; use the same choreography for
  // coordinates from contact/up events when its gesture recognizer misses one.
  if (d.gestureID >= 1 && d.gestureID <= 4) {
    int sx = d.gestureID == 3 ? -1 : d.gestureID == 4 ? 1 : 0;
    int sy = d.gestureID == 1 ? -1 : d.gestureID == 2 ? 1 : 0;
    beginSwipe(touchStartX < 0 ? 120 - sx * 65 : touchStartX,
               touchStartY < 0 ? 120 - sy * 65 : touchStartY,
               sx * 110, sy * 110, now);
    touchStartX = -1;
    return;
  }
  if (d.gestureID == 0x0B) {
    pointAt(d.x, d.y, now);
    react(SURPRISED, now, 850);
    touchStartX = -1;
    lastTouchActionAt = now;
    lastTapAt = now;
    tapStreak = 2;
    return;
  }
  if (d.gestureID == 0x0C) {
    creature.setTouchPoint(d.x, d.y);
    if (lastLongAt && now - lastLongAt < 3500) {
      react(LOOK_CHANGE, now, 1400);
      // Shape and color switch while the eyes are shut.
      chooseLook(eyeLook + 1, palette + 1, true, false);
      lastLongAt = 0;
    } else {
      react(PETTED, now, 1600);
      lastLongAt = now;
    }
    touchStartX = -1;
    lastTouchActionAt = now;
    return;
  }
  if (d.gestureID == 0x05) {
    finishTap(d.x, d.y, now);
    touchStartX = -1;
    return;
  }
  if (d.event == 0) {
    touchStartX = d.x;
    touchStartY = d.y;
    touchStartAt = now;
    pointAt(d.x, d.y, now);
    react(FOLLOWING, now, 1500);
    return;
  }
  if (d.event == 2 && touchStartX >= 0) {
    creature.setPointer((d.x - 120) / 80.0f, (d.y - 120) / 80.0f, now);
    return;
  }
  if (d.event == 1 && touchStartX >= 0) {
    int dx = d.x - touchStartX, dy = d.y - touchStartY;
    if (dx * dx + dy * dy > 28 * 28 && now - touchStartAt < 1400) {
      beginSwipe(touchStartX, touchStartY, dx, dy, now);
    } else finishTap(d.x, d.y, now);
    touchStartX = -1;
  }
}

void readMotion(float a[3], float g[3]) {
  QMI8658_read_xyz(a, g, nullptr);
  // Waveshare's driver reports mg and degrees/s.
  for (int i = 0; i < 3; ++i) {
    a[i] *= 0.00980665f;
    g[i] *= 0.017453293f;
  }
}

// Reacts to how the star is moved: inertia, bumps, shakes, pick-ups and naps.
void handleSurroundings(const float a[3], const float g[3], bool heldStill, uint32_t now) {
  // A slow gravity estimate leaves the fast, gravity-free part of the motion.
  static bool gravityReady = false;
  static float grav[3];
  if (!gravityReady) {
    for (int i = 0; i < 3; ++i) grav[i] = a[i];
    gravityReady = true;
  }
  float lin[3];
  for (int i = 0; i < 3; ++i) {
    grav[i] += (a[i] - grav[i]) * .08f;
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

  // Shaken while napping: the screen brightens with the eyes still shut, they
  // pop open, tumble dizzily, glare, then calm down. A gentler pick-up just
  // wakes it.
  if (sleepPreparing && creature.sleepFinished(now) && jolt > PICKUP_MS2) {
    bool shaken = jolt > SHAKE_STROKE_MS2;
    if (shaken) creature.impact(lin[0], lin[1]);
    startWakeAnimation(now, shaken);
    Serial.println(shaken ? "Nap ended by a shake" : "Nap ended by motion");
    return;
  }

  // ---- Shaking while awake ----
  // How hard it is being shaken (a short running average) drives a live
  // rattle in the eyes, so they react from the very first stroke. Strong
  // strokes that reverse direction count toward the dizzy spell; each one also
  // throws the eyes the other way, so they slosh with the motion.
  static uint32_t lastSampleAt = 0, lastStrokeAt = 0, lastBumpAt = 0, strongSince = 0, lastLogAt = 0;
  static uint8_t strokes = 0;
  static float strokeDir[3] = {0, 0, 0};
  static float shakeStrength = 0;
  float dt = lastSampleAt ? min(uint32_t(100), now - lastSampleAt) / 1000.0f : .025f;
  lastSampleAt = now;
  // A wrist shake is partly rotation, so a fast spin counts as shaking too.
  float spin = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
  float excess = fmaxf(fmaxf(0.0f, jolt - SHAKE_NOISE_MS2), (spin - SHAKE_GYRO_RAD_S) * 2.5f);
  shakeStrength += (excess - shakeStrength) * (1.0f - expf(-dt / .35f));
  if (shakeStrength > SHAKE_NOISE_MS2 * .5f)
    creature.shake(shakeStrength / SHAKE_FULL_MS2);
  if (LOG_SHAKE && shakeStrength > 1.0f && now - lastLogAt > 200) {
    lastLogAt = now;
    Serial.printf("shake: strength %.1f (dizzy at %.1f)  jolt %.1f m/s^2  spin %.1f rad/s  strokes %u\n",
                  shakeStrength, SHAKE_DIZZY_STRENGTH, jolt, spin, strokes);
  }
  // Sustained hard shaking makes it dizzy even if the strokes are irregular.
  strongSince = shakeStrength > SHAKE_DIZZY_STRENGTH ? (strongSince ? strongSince : now) : 0;
  if (strongSince && now - strongSince >= SHAKE_DIZZY_HOLD_MS) {
    strongSince = 0;
    strokes = 0;
    lastActivity = now;
    sleepPreparing = false;
    creature.impact(lin[0], lin[1]);
    if (react(DIZZY, now, DIZZY_ANIM_MS) && LOG_SHAKE) Serial.println("shake: DIZZY (sustained)");
    return;
  }

  if (strokes && now - lastStrokeAt > SHAKE_GAP_MS) strokes = 0; // the shake paused
  bool stroke = false;
  if (jolt > SHAKE_STROKE_MS2 && now - lastStrokeAt > 60) {
    float dot = lin[0] * strokeDir[0] + lin[1] * strokeDir[1] + lin[2] * strokeDir[2];
    if (strokes == 0 || dot < 0) {
      ++strokes;
      for (int i = 0; i < 3; ++i) strokeDir[i] = lin[i];
      lastStrokeAt = now;
      stroke = true;
      if (LOG_SHAKE) Serial.printf("shake stroke %u: %.1f m/s^2\n", strokes, jolt);
    }
  }
  if (stroke) {
    lastActivity = now; // being shaken is an interaction
    sleepPreparing = false;
    creature.impact(lin[0] * .6f, lin[1] * .6f);
    if (strokes >= SHAKE_STROKES_FOR_DIZZY) {
      // The full reaction: wobble, dizzy spiral eyes, glare, calm down. Keep
      // shaking and it starts spinning again.
      strokes = 0;
      strongSince = 0;
      creature.impact(lin[0], lin[1]);
      if (react(DIZZY, now, DIZZY_ANIM_MS) && LOG_SHAKE) Serial.println("shake: DIZZY (strokes)");
      return;
    }
    if (strokes == 1 && creature.mood() != DIZZY && creature.mood() != ANGRY)
      react(SURPRISED, now, 650); // whoa! -- the first stroke startles it
    return;
  }
  if (sleepPreparing) return;
  if (jolt > BUMP_MS2 && now - lastBumpAt > 1500) {
    // A knock: the eyes recoil and the creature looks startled.
    lastBumpAt = now;
    creature.impact(lin[0] * .6f, lin[1] * .6f);
    creature.reactPassive(SURPRISED, now, 520);
  } else if (jolt > PICKUP_MS2 && stillFor > 15000) {
    // Picked up after resting for a while: it perks up and looks around.
    creature.reactPassive(SURPRISED, now, 600);
  }
}

void handleMotion(uint32_t now) {
  if (!imuReady) return;
  static uint32_t lastSample = 0;
  if (now - lastSample < 25) return;
  lastSample = now;
  float a[3], g[3];
  readMotion(a, g);
  if (activeTwist.feed(g, now)) {
    creature.impact(linX, linY);
    react(DIZZY, now, DIZZY_ANIM_MS); // tumbles, then glares
  } else if (activeTwist.progressing()) {
    // A partial deliberate twist counts; one incidental swing does not.
    lastActivity = now;
  }
  // Accelerometer is the stable source of tilt. Low pass filtering keeps eyes calm.
  // If the eyes look the wrong way when tilted, reverse the sign of a[0] or a[1].
  tiltX += (constrain(a[0] / 7.0f, -1.0f, 1.0f) - tiltX) * .12f;
  tiltY += (constrain(a[1] / 7.0f, -1.0f, 1.0f) - tiltY) * .12f;
  creature.setTilt(tiltX, tiltY);
  // A purposeful tilt held still is an interaction. A moving keychain should
  // not continually restart the idle timer while the wearer walks.
  static bool haveTiltAnchor = false;
  static float anchorX = 0, anchorY = 0, candidateX = 0, candidateY = 0;
  static uint32_t candidateSince = 0;
  if (!haveTiltAnchor) {
    anchorX = tiltX; anchorY = tiltY;
    haveTiltAnchor = true;
  }
  float accelLength = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  bool heldStill = fabsf(accelLength - 9.81f) < .85f &&
                   fabsf(g[0]) + fabsf(g[1]) + fabsf(g[2]) < .45f;
  handleSurroundings(a, g, heldStill, now);
  bool changedTilt = fabsf(tiltX - anchorX) + fabsf(tiltY - anchorY)
                     > TILT_ACTIVITY_DELTA;
  if (heldStill && changedTilt) {
    if (!candidateSince || fabsf(tiltX - candidateX) + fabsf(tiltY - candidateY) > .12f) {
      candidateX = tiltX; candidateY = tiltY;
      candidateSince = now;
    } else if (now - candidateSince >= TILT_ACTIVITY_HOLD_MS) {
      lastActivity = now;
      anchorX = tiltX; anchorY = tiltY;
      candidateSince = 0;
      if (sleepPreparing) {
        if (creature.sleepFinished(now)) startWakeAnimation(now, false);
        else react(CONFUSED, now, 700);
      }
    }
  } else candidateSince = 0;
  // A face-down pause produces a puzzled, asymmetric expression.
  static uint32_t faceDownSince = 0;
  static bool faceDownReacted = false;
  if (a[2] < -7.5f && fabsf(g[0]) + fabsf(g[1]) < 1.2f) {
    if (!faceDownSince) faceDownSince = now;
    if (!faceDownReacted && now - faceDownSince > 500) {
      react(CONFUSED, now, 1000);
      faceDownReacted = true;
    }
  } else {
    faceDownSince = 0;
    faceDownReacted = false;
  }
  if (now - lastTempRead >= 1000) {
    lastTempRead = now;
    dieTempC = QMI8658_readTemp();
    // This is the IMU's die temperature, not a true ambient thermometer.
    if (dieTempC > -30 && dieTempC < 18) {
      if (!coldSince) coldSince = now;
      if (now - coldSince > 8000 && now - lastColdReaction > 6000) {
        creature.reactPassive(SHIVER, now, 1700);
        lastColdReaction = now;
      }
    } else coldSince = 0;
  }
  // Idle eye motion and ordinary walking do not restart the activity timer.
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
  // The IMU keeps WoM state through ESP32 deep sleep. Rev 0.9 requires the
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
    Serial.printf("WoM regs: statusInt=%02X ctrl2=%02X ctrl7=%02X ctrl8=%02X ctrl9=%02X cal=%02X,%02X\n",
                  lastStatus, ctrl2, ctrl7, ctrl8, ctrl9, calL, calH);
    return false;
  }
  QMI8658_enableSensors(QMI8658_CTRL7_ACC_ENABLE);
  uint8_t status = 0;
  QMI8658_read_reg(QMI8658Register_Status1, &status, 1); // clear stale WoM event
  return true;
}

bool enterDeepSleep(bool lcdReady) {
  if (!imuReady) return false;
  configureTouchWake();
  if (!armMotionWake()) {
    Serial.println("WoM setup failed; keeping face awake so shake wake is not lost");
    QMI8658_init();
    sleepRetryAt = millis() + 5000;
    return false;
  }
  pinMode(IMU_WAKE_PIN, INPUT_PULLUP);
  pinMode(TOUCH_WAKE_PIN, INPUT_PULLUP);
  // A currently asserted wake line causes an instant reboot. Let it clear.
  for (int i = 0; i < 25 && digitalRead(IMU_WAKE_PIN) == LOW; ++i) {
    uint8_t status = 0;
    QMI8658_read_reg(QMI8658Register_Status1, &status, 1);
    delay(10);
  }
  if (digitalRead(IMU_WAKE_PIN) == LOW) {
    QMI8658_init();
    sleepRetryAt = millis() + 5000;
    return false;
  }
  if (lcdReady) LCD_1IN28_Sleep();
  analogWrite(LCD_BL_PIN, 0);
  pinMode(LCD_BL_PIN, OUTPUT);
  digitalWrite(LCD_BL_PIN, LOW);
  gpio_hold_en(GPIO_NUM_2);
  gpio_deep_sleep_hold_en();
  uint64_t pins = 1ULL << IMU_WAKE_PIN;
  if (digitalRead(TOUCH_WAKE_PIN) == HIGH) pins |= 1ULL << TOUCH_WAKE_PIN;
  esp_sleep_enable_ext1_wakeup(pins, ESP_EXT1_WAKEUP_ANY_LOW);
  Serial.println("Entering deep sleep");
  Serial.flush();
  esp_deep_sleep_start();
  return true;
}

// After the IMU's coarse motion alarm woke the CPU (screen still dark): turn
// the face on for a shake -- a couple of strong jolts or fast flicks within a
// few seconds -- or a back-and-forth twist. Gravity's direction is unknown
// mid-shake, so a jolt is how far the total acceleration strays from 1 g.
// A walking step is too gentle and goes back to sleep.
bool confirmMotionWake() {
  const uint32_t window = 2500;
  uint32_t began = millis(), lastJoltAt = 0;
  TwistDetector detector;
  uint8_t jolts = 0;
  bool armed = true; // a jolt counts on its rising edge only
  while (millis() - began < window) {
    float a[3], g[3];
    readMotion(a, g);
    uint32_t now = millis();
    if (detector.feed(g, now)) return true;
    float mag = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    float spin = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    bool strong = fabsf(mag - 9.81f) > SHAKE_WAKE_MS2 || spin > SHAKE_GYRO_RAD_S * 1.5f;
    if (strong && armed && now - lastJoltAt > 80) {
      armed = false;
      lastJoltAt = now;
      ++jolts;
      if (LOG_SHAKE) Serial.printf("wake jolt %u: %.1f m/s^2 off 1 g, spin %.1f rad/s\n",
                                   jolts, fabsf(mag - 9.81f), spin);
      if (jolts >= SHAKE_STROKES_TO_WAKE) return true;
    } else if (!strong) {
      armed = true;
    }
    delay(10);
  }
  if (LOG_SHAKE) Serial.printf("wake check: %u strong jolt(s), not enough\n", jolts);
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
  renderer.draw(creature.eyes());
  applyBacklight(creature.backlight());
  if (LOG_FPS) {
    static uint32_t frames = 0, windowStart = 0;
    ++frames;
    if (millis() - windowStart >= 5000) {
      Serial.printf("%.1f FPS\n", frames * 1000.0f / (millis() - windowStart));
      frames = 0;
      windowStart = millis();
    }
  }
}

void setup() {
  Serial.begin(115200);
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(GPIO_NUM_2);
  pinMode(LCD_BL_PIN, OUTPUT);
  digitalWrite(LCD_BL_PIN, LOW);

  Wire.begin(6, 7);
  Wire.setClock(400000);
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  uint64_t source = cause == ESP_SLEEP_WAKEUP_EXT1 ? esp_sleep_get_ext1_wakeup_status() : 0;
  Serial.printf("Wake cause=%d, pins=0x%llX\n", int(cause), source);
  bool touchWake = (source & (1ULL << TOUCH_WAKE_PIN)) != 0;
  // A touch wakes it straight away (or needs a second press when
  // TOUCH_WAKE_DOUBLE_PRESS is set, to avoid waking in a pocket).
  bool touchConfirmed = touchWake && (!TOUCH_WAKE_DOUBLE_PRESS || confirmTwoPressWake());
  if (cause == ESP_SLEEP_WAKEUP_EXT1) exitMotionWake();
  imuReady = QMI8658_init() != 0;
  Serial.println(imuReady ? "Motion sensor ready" :
                 "Motion sensor NOT found: shake, tilt and shake-wake are disabled");
  if (touchWake && !touchConfirmed && !(source & (1ULL << IMU_WAKE_PIN))) {
    Serial.println("Single touch wake rejected; returning to sleep");
    if (enterDeepSleep(false)) return;
  }
  if (imuReady && (source & (1ULL << IMU_WAKE_PIN)) && !touchConfirmed) {
    // Motion wakes the CPU but not the LCD. Only a real shake (or twist)
    // turns the face on; it then wakes startled, dizzy and grumpy.
    if (!confirmMotionWake()) {
      Serial.println("Motion wake rejected; returning to sleep");
      if (enterDeepSleep(false)) return;
    } else wokeByShake = true;
  }
  // Internal RAM is much faster than PSRAM for per-pixel blending.
  size_t bytes = W * H * sizeof(uint16_t);
  BlackImage = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!BlackImage) BlackImage = (uint16_t *)ps_malloc(bytes);
  if (!BlackImage) BlackImage = (uint16_t *)malloc(bytes);
  if (!BlackImage) { Serial.println("Display buffer allocation failed"); return; }
  DEV_Module_Init();
  DEV_SET_PWM(0);
  LCD_1IN28_Init(HORIZONTAL);
  touch.begin(FALLING);
  Wire.setClock(400000);
  configureTouchWake();
  randomSeed(esp_random());
  prefs.begin("starface", false);
  uint64_t mac = ESP.getEfuseMac();
  personality = (mac >> 16) % 3;
  eyeLook = prefs.getUChar("look", uint8_t(mac & 3)) % EYE_LOOK_COUNT;
  palette = prefs.getUChar("colorV2", DEFAULT_EYE_COLOR) % EYE_PALETTE_COUNT;
  Serial.printf("Eye look=%u palette=%u personality=%u\n", eyeLook, palette, personality);
  lastActivity = millis();
  renderer.begin(BlackImage, LCD_1IN28_DisplayWindows);
  creature.begin(&renderer, eyeLook, palette, personality,
                 uint32_t(mac ^ (mac >> 32)), lastActivity);
  readBattery();
  lastBatteryCheck = lastActivity;
  startWakeAnimation(lastActivity, wokeByShake);
  if (wokeByShake) {
    Serial.println("Shake confirmed; waking into dizzy eyes");
  } else if (touchConfirmed) {
    Serial.println("Touch wake confirmed; waking face");
  }
  // First frame: the whole screen is cleared while the backlight is still off.
  lastFrameUs = micros() - FRAME_US;
  renderFrame();
}

void loop() {
  if (!BlackImage) { delay(1000); return; }
  uint32_t now = millis();
  handleTouch(now);
  if (sleepPreparing && digitalRead(TOUCH_WAKE_PIN) == LOW) cancelSleepForTouch(now);
  handleMotion(now);
  creature.setPointerHeld(touchStartX >= 0);
  if (now - lastBatteryCheck >= BATTERY_CHECK_MS) {
    lastBatteryCheck = now;
    readBattery();
  }
  uint32_t timeout = idleTimeout();
  if (sleepPreparing) {
    // Napping keeps the screen on. Only planned deep sleep or a nearly empty
    // battery actually switches it off.
    if (creature.sleepFinished(now) && (deepSleepPlanned || (imuReady && batteryLow()))) {
      if (enterDeepSleep(true)) return;
      startWakeAnimation(millis(), false);
    }
  } else if (now - lastActivity > timeout && now >= sleepRetryAt) {
    startSleepAnimation(now);
  }
  if (!sleepPreparing) {
    float idle = (now - lastActivity) / float(timeout);
    creature.setDrowsiness(anim::smoothstep(.6f, 1.0f, idle));
    creature.setIdleActsAllowed(now - lastActivity + 2500 < timeout);
  }
  renderFrame();
  delay(1);
}
