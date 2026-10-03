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
// Flat cartoon eyes: a smooth solid eye shape with a big solid pupil and a soft
// purple glow on black. The pupils change shape with the mood: star glints,
// hearts when happy, spirals when dizzy. Four styles, each with its own
// personality (four quick taps cycles them):
//   0 BEAN  leaning egg pupils with a twinkling star glint   -- calm & curious
//   1 DOT   little star-shaped pupils                         -- shy
//   2 BLIP  big round sparkly pupils, rounder eyes            -- playful
//   3 CAT   slit pupils that dilate when startled             -- sassy
static constexpr uint8_t DEFAULT_EYE_STYLE = 0;
struct EyeColors {
  uint8_t body[3];    // the eye shape
  uint8_t pupilL[3];  // left pupil (odd-coloured eyes are part of the charm)
  uint8_t pupilR[3];  // right pupil
  uint8_t glow[3];    // the soft halo around the eye
};
static constexpr EyeColors STYLE_COLORS[4] = {
  {{226, 212, 255}, {88, 22, 205}, {176, 40, 222}, {124, 40, 255}},  // BEAN: lavender, deep purple / magenta
  {{214, 206, 255}, {70, 30, 170}, {70, 30, 170}, {96, 70, 255}},    // DOT: periwinkle, indigo stars
  {{240, 214, 255}, {150, 24, 214}, {150, 24, 214}, {200, 70, 255}}, // BLIP: pink-lilac, orchid
  {{206, 184, 255}, {36, 8, 84}, {36, 8, 84}, {140, 70, 255}},       // CAT: violet, near-black slits
};
static constexpr float GLOW_STRENGTH = 0.85f;     // brightness of the halo at the eye edge
static constexpr float GLOW_FALLOFF = 6.5f;       // px for the halo to fade to ~37%
static constexpr float GLOW_EXTENT = 16.0f;       // halo fades to zero by this distance (cost grows with it)
static constexpr bool DITHER = true;              // ordered dithering hides RGB565 banding

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
// wakes it again (the same shake that makes it dizzy; see the shake settings). Set AUTO_DEEP_SLEEP = false to keep the screen on instead: it
// then naps with a dim, breathing glow after IDLE_NAP_MS.
static constexpr bool AUTO_DEEP_SLEEP = true;
static constexpr uint32_t IDLE_SLEEP_MS = 30000;  // power-save timeout when AUTO_DEEP_SLEEP
static constexpr uint32_t IDLE_NAP_MS = 45000;    // dim-nap timeout when !AUTO_DEEP_SLEEP
static constexpr bool TOUCH_WAKE_DOUBLE_PRESS = false; // true: needs two presses (pocket-proof)
static constexpr uint8_t BACKLIGHT_PERCENT = 62;
static constexpr uint8_t NAP_BACKLIGHT_PERCENT = 14;

// ---- Derived (do not edit) -------------------------------------------------------
static constexpr float EYE_HALF_WIDTH = EYE_WIDTH * 0.5f;
static constexpr float EYE_HALF_HEIGHT = EYE_HEIGHT * 0.5f;
// Expression offsets are authored for a 48 px half-height eye and scale with size.
static constexpr float EYE_PX_SCALE = EYE_HALF_HEIGHT / 48.0f;
