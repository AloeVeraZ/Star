#pragma once
#include <stdint.h>

// ============================================================================
//  Star Face tuning: everything you are likely to want to change is here.
//  Eye sizes are fractions of the screen; times are ms; motion is m/s^2.
// ============================================================================

// ============================================================================
//  EYES: the parameters you are most likely to want to change.
// ============================================================================

// ---- The two eye colours ---------------------------------------------------
// The background is always pure black. The eyes use exactly these two colours
// and nothing else: no gradients, glows, shading or anti-aliased edge pixels.
// 24-bit 0xRRGGBB values (shown as the nearest RGB565 colour on the panel).
static constexpr uint32_t EYE_COLOR = 0x8A2EFF;     // primary: the eye shapes
static constexpr uint32_t ACCENT_COLOR = 0xF2EAFF;  // secondary: pupils and accents (hearts, spirals)

// ---- Eye size and layout, as fractions of the screen -------------------------
// Everything scales with the display: sizes are fractions of the smaller
// screen side, positions are relative to the screen centre.
static constexpr float EYE_WIDTH = 0.35f;         // width of one eye at rest
static constexpr float EYE_HEIGHT = 0.48f;        // height of one eye at rest
static constexpr float EYE_SPACING = 0.44f;       // centre-to-centre distance between the eyes
static constexpr float EYE_OFFSET_Y = -0.01f;     // whole face up (-) or down (+) from the centre
static constexpr float EYE_ROUNDNESS = 0.46f;     // corners: 0 sharp box .. 1 fully rounded capsule
static constexpr float PUPIL_SIZE = 0.43f;        // pupil radius, as a fraction of the eye's half width
static constexpr float PUPIL_ROUNDNESS = 0.70f;   // 1 = circle, lower = squarer (robotic)

// ---- Movement limits ---------------------------------------------------------
static constexpr float MAX_PUPIL_MOVE = 0.62f;    // 0..1: how far a pupil may roam inside its eye
static constexpr float MAX_EYE_MOVE_X = 0.040f;   // how far the whole eyes follow the gaze, of screen
static constexpr float MAX_EYE_MOVE_Y = 0.030f;
static constexpr float MAX_EXPRESSION_EXPANSION = 1.15f; // largest size any expression may reach

// ---- Animation -----------------------------------------------------------------
static constexpr float ANIMATION_SPEED = 1.0f;    // >1 snappier, <1 lazier (0.6 .. 1.6)
static constexpr float EXPRESSION_INTENSITY = 1.0f; // 0.5 subtle .. 1.3 exaggerated
static constexpr float EXPRESSION_BLEND_SPEED = 1.0f; // how quickly one expression morphs into the next
static constexpr uint16_t BLINK_CLOSE_MS = 70;    // blink speed: a fast close...
static constexpr uint16_t BLINK_OPEN_MS = 150;    // ...and a slightly slower open
static constexpr uint32_t BLINK_MIN_MS = 2200;    // blink frequency: time between blinks
static constexpr uint32_t BLINK_MAX_MS = 6500;
static constexpr float DOUBLE_BLINK_CHANCE = 0.12f;
static constexpr float GAZE_SPEED = 1.0f;         // how quickly the eyes move to a new target
static constexpr float IDLE_LIVELINESS = 1.0f;    // idle look-arounds and fidgets: 0 still .. 1.5 restless
static constexpr float SQUASH_STRETCH = 1.0f;     // 0 disables velocity squash & stretch
static constexpr float ANGER_COOLDOWN_S = 4.0f;   // lids stay a little grumpy this long after anger

// ---- Display ------------------------------------------------------------------
static constexpr int SCREEN_WIDTH = 240;
static constexpr int SCREEN_HEIGHT = 240;
// A round panel hides its corners: the eyes are then kept inside a circle.
static constexpr bool SCREEN_IS_ROUND = true;
static constexpr float SAFE_MARGIN = 0.065f;      // keep the eyes this far (of screen) from the edge

// ---- Shake & motion sensitivity (gravity-free acceleration, m/s^2; 9.8 = 1 g) --
// Lower SHAKE_STROKE_MS2 / SHAKE_STROKES_FOR_DIZZY if shaking feels too hard,
// raise them if walking or bumps set it off. Set LOG_SHAKE in StarFace.ino to
// print each stroke's strength over serial while you tune.
static constexpr float SHAKE_STROKE_MS2 = 6.5f;   // one stroke of a shake (~0.65 g beyond gravity)
static constexpr uint8_t SHAKE_STROKES_FOR_DIZZY = 3; // back-and-forth strokes for the dizzy spell
static constexpr uint32_t SHAKE_GAP_MS = 500;     // longest pause between strokes of one shake
static constexpr float SHAKE_NOISE_MS2 = 3.0f;    // motion below this never rattles the eyes
static constexpr float SHAKE_FULL_MS2 = 7.0f;     // average shake strength for a full-strength rattle
// Dizzy also triggers on sustained hard shaking, however the strokes line up:
static constexpr float SHAKE_DIZZY_STRENGTH = 3.5f; // average shake strength (m/s^2 above noise)...
static constexpr uint32_t SHAKE_DIZZY_HOLD_MS = 400; // ...kept up for this long
// Asleep it is much harder to wake: it takes a steady shake, kept up for
// SHAKE_WAKE_HOLD_MS (short dips under SHAKE_WAKE_DROPOUT_MS are forgiven).
static constexpr float SHAKE_WAKE_STRENGTH = 3.5f;   // average shake strength to count as shaking
static constexpr uint32_t SHAKE_WAKE_HOLD_MS = 4000; // ...for this long
static constexpr uint32_t SHAKE_WAKE_DROPOUT_MS = 350;
static constexpr float SHAKE_GYRO_RAD_S = 3.0f;   // wrist-flick rotation faster than this counts as shaking
static constexpr float BUMP_MS2 = 9.0f;           // a single knock: recoil and a startled look
static constexpr float PICKUP_MS2 = 2.2f;         // picked up / moved: ends a nap

// ---- Touch -------------------------------------------------------------------------
// While a finger is down the eyes follow it. Releasing after a quick flick is
// a swipe in the flick's direction; holding on makes it more and more angry.
static constexpr uint32_t TAP_MAX_MS = 350;       // a tap is shorter than this...
static constexpr float TAP_MAX_MOVE_PX = 22;      // ...and moves less than this
static constexpr float SWIPE_MIN_PX = 35;         // a swipe flicks at least this far...
static constexpr float SWIPE_MIN_SPEED = 250;     // ...at least this fast (px/s) at the end
static constexpr uint32_t SWIPE_MAX_SHORT_MS = 700; // or: a press this short that moved far
static constexpr uint32_t HOLD_ANGER_START_MS = 1200; // holding longer than this annoys it...
static constexpr uint32_t HOLD_ANGER_FULL_MS = 4500;  // ...and by this long it is furious
static constexpr uint32_t TOUCH_RELEASE_TIMEOUT_MS = 250; // no report this long = finger lifted

// ---- Sleep & power ---------------------------------------------------------------
// After IDLE_SLEEP_MS without interaction the eyes droop, close and fade to
// black, then the screen and ESP32 power down (deep sleep). A touch or a shake
// wakes it again (a steady shake of about 4 s; see the shake settings).
// Set AUTO_DEEP_SLEEP = false to keep the screen on instead: it then naps
// with dimmed, closed eyes after IDLE_NAP_MS.
static constexpr bool AUTO_DEEP_SLEEP = true;
static constexpr uint32_t IDLE_SLEEP_MS = 30000;  // power-save timeout when AUTO_DEEP_SLEEP
static constexpr uint32_t IDLE_NAP_MS = 45000;    // dim-nap timeout when !AUTO_DEEP_SLEEP
static constexpr bool TOUCH_WAKE_DOUBLE_PRESS = false; // true: needs two presses (pocket-proof)
static constexpr uint8_t BACKLIGHT_PERCENT = 62;
static constexpr uint8_t NAP_BACKLIGHT_PERCENT = 14;

// ---- Derived (do not edit) -------------------------------------------------------
static constexpr float SCREEN_MIN_SIDE = float(SCREEN_WIDTH < SCREEN_HEIGHT ? SCREEN_WIDTH : SCREEN_HEIGHT);
static constexpr float SCREEN_CX = (SCREEN_WIDTH - 1) * 0.5f;    // pixel centres are whole numbers
static constexpr float SCREEN_CY = (SCREEN_HEIGHT - 1) * 0.5f;
static constexpr float SCREEN_RADIUS = SCREEN_MIN_SIDE * 0.5f;
static constexpr float SAFE_RADIUS = SCREEN_RADIUS - SAFE_MARGIN * SCREEN_MIN_SIDE;
static constexpr float EYE_HALF_WIDTH = EYE_WIDTH * SCREEN_MIN_SIDE * 0.5f;   // px
static constexpr float EYE_HALF_HEIGHT = EYE_HEIGHT * SCREEN_MIN_SIDE * 0.5f; // px
static constexpr float EYE_HALF_SPACING = EYE_SPACING * SCREEN_MIN_SIDE * 0.5f;
static constexpr float EYE_CENTER_X = SCREEN_CX;
static constexpr float EYE_CENTER_Y = SCREEN_CY + EYE_OFFSET_Y * SCREEN_MIN_SIDE;
