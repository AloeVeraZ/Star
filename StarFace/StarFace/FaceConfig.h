#pragma once
#include <stdint.h>

// ============================================================================
//  Star Face tuning: everything you are likely to want to change is here.
//  Eye sizes are fractions of the screen; times are ms; motion is m/s^2.
// ============================================================================

// ============================================================================
//  EYES: the parameters you are most likely to want to change.
// ============================================================================

// ---- Colours ----------------------------------------------------------------
// Just purple and black by default: the eye is a glowing EYE_COLOR, the iris
// is PUPIL_COLOR (black, or any second colour you like), and the
// catch-lights are EYE_COLOR again. Edges are anti-aliased and the glow fades out (each such pixel is a
// blend of these two colours and black, nothing else), which is what makes
// the shapes look like smooth glowing vector art instead of pixel steps.
// 24-bit 0xRRGGBB values (shown as the nearest RGB565 colour on the panel).
static constexpr uint32_t EYE_COLOR = 0xA246FF;     // the eyes, their glow and the catch-lights: glowing purple
static constexpr uint32_t PUPIL_COLOR = 0x000000;   // the big irises, hearts and spirals: black (purple and black only)
static constexpr bool GLOW = true;                  // a soft halo of EYE_COLOR around the eyes
static constexpr float GLOW_STRENGTH = 0.85f;       // halo brightness at the eye's edge
static constexpr float GLOW_SIZE = 0.065f;          // halo reach, as a fraction of the screen
static constexpr bool ANTI_ALIAS = true;            // smooth vector edges (false: hard pixel edges)
// Rosy cheeks under the eyes: a small blush at rest that grows when it is
// happy, petted or in love, and fades when it is sad or cross (the one extra
// colour; set CHEEK_BLUSH = false for strictly two colours).
static constexpr bool CHEEK_BLUSH = false;
static constexpr uint32_t CHEEK_COLOR = 0xFF7EB0;

// ---- Eye size and layout, as fractions of the screen -------------------------
// Everything scales with the display: sizes are fractions of the smaller
// screen side, positions are relative to the screen centre.
static constexpr float EYE_WIDTH = 0.43f;         // width of one oval eye at rest
static constexpr float EYE_HEIGHT = 0.49f;        // height of one oval eye at rest (nearly round = cuter)
static constexpr float EYE_SPACING = 0.49f;       // centre-to-centre distance between the eyes
static constexpr float EYE_OFFSET_Y = 0.03f;      // whole face up (-) or down (+): a little low reads younger, cuter
static constexpr float PUPIL_SIZE = 0.72f;        // iris radius, as a fraction of the eye's half width (big = friendly)
static constexpr float PUPIL_CORE = 0.0f;         // black centre of the iris, as a fraction of it (0 = none, two colours only)
static constexpr float GLINT_SIZE = 0.38f;        // main catch-light radius, as a fraction of the iris (0 = none)
static constexpr float PUPIL_LIFE = 1.0f;         // pupil dilation and fixation tremor: 0 off .. 1.5 lively

// ---- Movement limits ---------------------------------------------------------
static constexpr float MAX_PUPIL_MOVE = 0.85f;    // 0..1: how far an iris may roam inside its eye
static constexpr float MAX_EYE_MOVE_X = 0.055f;   // how far the whole eyes follow the gaze, of screen
static constexpr float MAX_EYE_MOVE_Y = 0.045f;
static constexpr float MAX_EXPRESSION_EXPANSION = 1.15f; // largest size any expression may reach

// ---- Animation -----------------------------------------------------------------
static constexpr float ANIMATION_SPEED = 1.0f;    // >1 snappier, <1 lazier (0.6 .. 1.6)
static constexpr float EXPRESSION_INTENSITY = 1.0f; // 0.5 subtle .. 1.3 exaggerated
static constexpr float EXPRESSION_BLEND_SPEED = 1.0f; // how quickly one expression morphs into the next
static constexpr uint16_t BLINK_CLOSE_MS = 75;    // blink speed: a fast close...
static constexpr uint16_t BLINK_OPEN_MS = 160;    // ...and a slower open
static constexpr uint32_t BLINK_MIN_MS = 2200;    // blink frequency: time between blinks
static constexpr uint32_t BLINK_MAX_MS = 6500;
static constexpr float DOUBLE_BLINK_CHANCE = 0.12f;
static constexpr float GAZE_SPEED = 1.0f;         // how quickly the eyes move to a new target
static constexpr float IDLE_LIVELINESS = 1.3f;    // idle look-arounds and fidgets: 0 still .. 2 restless
static constexpr float SQUASH_STRETCH = 1.0f;     // 0 disables velocity squash & stretch
static constexpr float ANGER_COOLDOWN_S = 4.0f;   // lids stay a little grumpy this long after anger

// ---- Display ------------------------------------------------------------------
static constexpr int SCREEN_WIDTH = 240;
static constexpr int SCREEN_HEIGHT = 240;
// A round panel hides its corners: the eyes are then kept inside a circle.
static constexpr bool SCREEN_IS_ROUND = true;
static constexpr float SAFE_MARGIN = 0.04f;       // keep the eyes this far (of screen) from the edge
// The eyes sit level with the board: their line is parallel to the edge with
// the USB-C port. If the enclosure holds the board turned, turn the face to
// match here (degrees, clockwise).
static constexpr float FACE_ANGLE_DEG = 0.0f;

// ---- Shake & motion sensitivity (gravity-free acceleration, m/s^2; 9.8 = 1 g) --
// Lower SHAKE_STROKE_MS2 / SHAKE_STROKES_FOR_DIZZY if shaking feels too hard,
// raise them if walking or bumps set it off. Set LOG_SHAKE in StarFace.ino to
// print each stroke's strength over serial while you tune.
static constexpr float SHAKE_STROKE_MS2 = 8.5f;   // one strong stroke of a shake (~0.85 g beyond gravity)
static constexpr uint8_t SHAKE_STROKES_FOR_DIZZY = 4; // strong back-and-forth strokes for the dizzy spell
static constexpr uint32_t SHAKE_GAP_MS = 500;     // longest pause between strokes of one shake
static constexpr float SHAKE_NOISE_MS2 = 3.0f;    // motion below this never rattles the eyes
static constexpr float SHAKE_FULL_MS2 = 7.0f;     // average shake strength for a full-strength rattle
// Dizzy also triggers on sustained hard shaking, however the strokes line up:
static constexpr float SHAKE_DIZZY_STRENGTH = 5.5f; // average shake strength (m/s^2 above noise)...
static constexpr uint32_t SHAKE_DIZZY_HOLD_MS = 450; // ...kept up for this long
// Asleep it is much harder to wake: it takes a steady shake, kept up for
// SHAKE_WAKE_HOLD_MS (short dips under SHAKE_WAKE_DROPOUT_MS are forgiven).
static constexpr float SHAKE_WAKE_STRENGTH = 3.5f;   // average shake strength to count as shaking
static constexpr float SHAKE_WAKE_STROKE_MS2 = 6.5f; // one stroke while waking it
static constexpr uint32_t SHAKE_WAKE_HOLD_MS = 4000; // ...for this long
static constexpr uint32_t SHAKE_WAKE_DROPOUT_MS = 350;
static constexpr float SHAKE_GYRO_RAD_S = 3.0f;   // wrist-flick rotation faster than this counts as shaking
static constexpr float BUMP_MS2 = 9.0f;           // a single knock: recoil and a startled look
static constexpr float PICKUP_MS2 = 2.2f;         // picked up / moved: ends a nap

// ---- Following the world (motion sensor) --------------------------------------
// The eyes look toward the low side when it is tilted (any direction), the
// face rolls to stay level when the star is turned, the eyes counter-move to
// keep looking at you when it is swung around, and a tilt held still for
// TILT_SETTLE_S becomes its new normal. If tilting makes the eyes look the
// wrong way, try IMU_ROTATION = 1, 2 or 3 (how the sensor sits relative to
// the screen, in quarter turns); that fixes every motion reaction at once.
static constexpr uint8_t IMU_ROTATION = 0;
static constexpr float TILT_GAZE = 2.2f;          // gaze per radian of tilt (~25 degrees = a full look)
static constexpr float TILT_SETTLE_S = 20.0f;     // a tilt held still this long becomes the new normal
static constexpr bool FACE_STAYS_LEVEL = false;   // true: roll the face to stay level with the ground
                                                  // (false: the eyes stay square to the board and its USB port)
static constexpr float FACE_ROLL_MAX = 0.75f;     // radians (~43 degrees), so upside down still reads as upside down
static constexpr float SWING_GAZE = 0.12f;        // eye counter-move per rad/s of swing
static constexpr float SPIN_TURNS_FOR_DIZZY = 1.5f; // spinning it around on the spot this many turns: dizzy
static constexpr uint32_t FREEFALL_MS = 90;       // weightless this long (tossed or dropped): startled
static constexpr float STEP_MS2 = 1.2f;           // a footstep bounce at least this strong (walking with it)
static constexpr bool WALKING_KEEPS_AWAKE = true; // being carried around keeps it awake and watching
static constexpr float HOT_C = 38.0f;             // the IMU chip warmer than this (pocket, hand, sun): lazy and yawny

// ---- Motion gestures (everything also works without the touch screen) -------
// Knock on the case 1/2/3/4 times = tap the screen 1/2/3/4 times; tip it one
// way and straight back = swipe that way; rock it gently = pet it; hold it
// upside down = hold a finger on it (it gets angrier the longer it lasts);
// lay it face down = put it to sleep.
static constexpr float KNOCK_MS2 = 3.0f;          // a knock is a jolt at least this sharp (lower = more sensitive)
static constexpr float KNOCK_QUIET_MS2 = 1.5f;    // ...on a star moving less than this just before
static constexpr uint32_t KNOCK_QUIET_BEFORE_MS = 250; // ...for this long (walking never is)
static constexpr float KNOCK_MAX_SPIN_RAD_S = 1.5f; // a swing is not a knock
static constexpr uint32_t KNOCK_RING_MS = 110;    // a knock must settle within this
static constexpr uint32_t KNOCK_GAP_MS = 450;     // longest pause between knocks of one run
static constexpr float FLICK_START_RAD_S = 2.0f;  // a flick starts with a turn at least this quick...
static constexpr float FLICK_MIN_TILT_MS2 = 3.3f; // ...tips it at least ~20 degrees...
static constexpr float FLICK_RETURN_MS2 = 2.0f;   // ...and comes back to within ~12 degrees...
static constexpr uint32_t FLICK_MAX_MS = 800;     // ...within this time
static constexpr uint32_t ROCK_MIN_HALF_MS = 360; // one rocking swing takes this long or more
static constexpr uint32_t ROCK_MAX_HALF_MS = 1400;
static constexpr uint8_t ROCK_SWINGS = 3;         // swings in a row before it feels petted
static constexpr float ROCK_MAX_BOUNCE_MS2 = 1.6f; // jostling above this (walking) is not rocking
static constexpr uint32_t UPSIDE_DOWN_MS = 500;   // held upside down this long annoys it
static constexpr uint32_t FACE_DOWN_SLEEP_MS = 4000; // face down this long: it goes to sleep (0 = never)

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
// Set TOUCH_ENABLED = false if the screen sits behind a cover that blocks
// touch: the touch chip is then ignored and only motion wakes it from sleep.
static constexpr bool TOUCH_ENABLED = true;
// A "finger" that never lifts (a cover pressing on the glass, moisture) is
// ignored after this long, so it cannot keep the creature angry or awake.
static constexpr uint32_t TOUCH_STUCK_MS = 15000;

// ---- Sleep & power ---------------------------------------------------------------
// After IDLE_SLEEP_MS without interaction the eyes droop, close and fade to
// black, then the screen and ESP32 power down (deep sleep). A touch or a shake
// wakes it again (a steady shake of about 4 s; see the shake settings).
// Set AUTO_DEEP_SLEEP = false to keep the screen on instead: it then naps
// with dimmed, closed eyes after IDLE_NAP_MS.
static constexpr bool AUTO_DEEP_SLEEP = true;
static constexpr uint32_t IDLE_SLEEP_MS = 60000;  // power-save timeout when AUTO_DEEP_SLEEP
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
