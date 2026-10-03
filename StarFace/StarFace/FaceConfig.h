#pragma once
#include <stdint.h>

// ============================================================================
//  Star Face tuning: everything you are likely to want to change is here.
//  Sizes are screen pixels on the 240 x 240 round GC9A01A panel. Times are ms.
// ============================================================================

// ---- Round screen ------------------------------------------------------------
// The panel is a 240 px circle; the corners of the framebuffer are never seen.
// Eye geometry (body plus the bright part of its glow) is softly kept inside
// SAFE_RADIUS, so gaze, wide eyes and impacts never clip against the bezel.
static constexpr float SCREEN_CX = 119.5f;        // centre of the visible circle
static constexpr float SCREEN_CY = 119.5f;
static constexpr float SCREEN_RADIUS = 120.0f;    // visible radius
static constexpr float SAFE_RADIUS = 112.0f;      // nothing important crosses this
static constexpr float SAFE_SOFTNESS = 10.0f;     // eyes ease in over this many px before the limit
static constexpr float SAFE_GLOW_MARGIN = 4.0f;   // how much glow counts as "part of the eye"

// ---- Eye layout --------------------------------------------------------------
// Two tall eyes side by side fill a circle best: their outer edges follow the
// bezel curve while the tops and bottoms use the tall middle of the screen.
static constexpr float EYE_WIDTH = 92.0f;         // full width of one eye at rest
static constexpr float EYE_HEIGHT = 124.0f;       // full height of one eye at rest
static constexpr float EYE_SPACING = 106.0f;      // centre-to-centre distance
static constexpr float EYE_CENTER_X = SCREEN_CX;  // midpoint between the eyes
static constexpr float EYE_CENTER_Y = 117.0f;     // eye centres (a hair above screen centre)
static constexpr float EYE_IRIS_RADIUS = 21.0f;   // iris radius at rest
static constexpr float PUPIL_TRAVEL = 0.80f;      // 0..1: how far the iris may roam inside the eye

// ---- Motion limits -------------------------------------------------------------
static constexpr float MAX_GAZE_SHIFT_X = 9.0f;   // whole-eye travel at full gaze, px
static constexpr float MAX_GAZE_SHIFT_Y = 7.0f;
static constexpr float MAX_EXPRESSION_EXPANSION = 1.16f; // biggest size any expression may reach
static constexpr float EYE_PERSPECTIVE = 0.035f;  // far eye shrinks this much at full side gaze
static constexpr float SQUASH_STRETCH = 1.0f;     // 0 disables velocity squash & stretch

// ---- Animation feel ------------------------------------------------------------
static constexpr float ANIMATION_SPEED = 1.0f;    // >1 snappier springs, <1 lazier (0.6 .. 1.6)
static constexpr float EXPRESSION_INTENSITY = 1.0f; // 0.5 subtle .. 1.3 exaggerated
static constexpr uint32_t BLINK_MIN_MS = 2200;    // gap between spontaneous blinks
static constexpr uint32_t BLINK_MAX_MS = 7000;
static constexpr float DOUBLE_BLINK_CHANCE = 0.10f;
static constexpr float ANGER_COOLDOWN_S = 4.0f;   // grumpy lids linger this long after anger

// ---- Look ----------------------------------------------------------------------
static constexpr uint8_t DEFAULT_EYE_COLOR = 0;   // index into EYE_PALETTE
static constexpr uint8_t EYE_PALETTE_COUNT = 7;
static constexpr uint8_t EYE_PALETTE[EYE_PALETTE_COUNT][3] = {
  {195, 150, 255}, // 0 soft lilac (default)
  {255, 135, 196}, // 1 pink
  {80, 221, 232},  // 2 cyan
  {105, 158, 255}, // 3 blue
  {255, 94, 109},  // 4 red
  {225, 235, 255}, // 5 white
  {255, 204, 96}   // 6 yellow
};
static constexpr float GLOW_STRENGTH = 0.72f;     // brightness of the halo at the eye edge
static constexpr float GLOW_FALLOFF = 5.5f;       // px for the halo to fade to ~37%
static constexpr float GLOW_EXTENT = 16.0f;       // halo fades to zero by this distance (cost grows with it)
static constexpr bool DITHER = true;              // ordered dithering hides RGB565 banding

// ---- Shake & motion sensitivity (gravity-free acceleration, m/s^2; 9.8 = 1 g) --
// Lower SHAKE_STROKE_MS2 / SHAKE_STROKES_FOR_DIZZY if shaking feels too hard,
// raise them if walking or bumps set it off. Set LOG_SHAKE in StarFace.ino to
// print each stroke's strength over serial while you tune.
static constexpr float SHAKE_STROKE_MS2 = 11.0f;  // one stroke of a shake (~1.1 g beyond gravity)
static constexpr uint8_t SHAKE_STROKES_FOR_DIZZY = 3; // back-and-forth strokes for the dizzy spell
static constexpr uint32_t SHAKE_GAP_MS = 450;     // longest pause between strokes of one shake
static constexpr float SHAKE_NOISE_MS2 = 3.0f;    // motion below this never rattles the eyes
static constexpr float SHAKE_FULL_MS2 = 8.0f;     // average shake strength for a full-strength rattle
static constexpr float BUMP_MS2 = 9.0f;           // a single knock: recoil and a startled look
static constexpr float PICKUP_MS2 = 2.2f;         // picked up / moved: ends a nap

// ---- Sleep & power ---------------------------------------------------------------
// After IDLE_SLEEP_MS without interaction the eyes droop, close and fade to
// black, then the screen and ESP32 power down (deep sleep). A touch or a shake
// wakes it again. Set AUTO_DEEP_SLEEP = false to keep the screen on instead: it
// then naps with a dim, breathing glow after IDLE_NAP_MS.
static constexpr bool AUTO_DEEP_SLEEP = true;
static constexpr uint32_t IDLE_SLEEP_MS = 30000;  // power-save timeout when AUTO_DEEP_SLEEP
static constexpr uint32_t IDLE_NAP_MS = 45000;    // dim-nap timeout when !AUTO_DEEP_SLEEP
static constexpr bool TOUCH_WAKE_DOUBLE_PRESS = false; // true: needs two presses (pocket-proof)
static constexpr uint8_t SHAKE_STROKES_TO_WAKE = 3;    // back-and-forth strokes that wake it
static constexpr uint8_t BACKLIGHT_PERCENT = 62;
static constexpr uint8_t NAP_BACKLIGHT_PERCENT = 14;

// ---- Derived (do not edit) -------------------------------------------------------
static constexpr float EYE_HALF_WIDTH = EYE_WIDTH * 0.5f;
static constexpr float EYE_HALF_HEIGHT = EYE_HEIGHT * 0.5f;
// Expression offsets are authored for a 48 px half-height eye and scale with size.
static constexpr float EYE_PX_SCALE = EYE_HALF_HEIGHT / 48.0f;
