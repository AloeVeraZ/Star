# Star Face

A wearable eye pet for the **Waveshare ESP32-S3-Touch-LCD-1.28**, adapted from [CREATURE's Starboy](https://hesjustalittleguy.com/) behaviors to the sensors in this board. The round 240 × 240 screen shows two big, friendly cartoon eyes on pure black in **purple and black by default**: brightly glowing purple ovals (with a purple halo) and large black irises with two purple catch-lights, drawn as smooth anti-aliased vector shapes (no stepped pixel edges). They react to the world through the motion sensor: they look toward the ground when tilted, stay level when the star is turned, keep looking at you when it is swung, get dizzy when it is shaken hard or spun, and startle when it is tossed. The eyes themselves carry the expressions: lids slide, slant and curve, the ovals stretch and tilt, irises dilate and tighten, turn into hearts or spin into spirals. (Optional rosy cheeks, a third colour, can be switched on with `CHEEK_BLUSH`.) There is no mouth. The face is drawn procedurally on the device and needs no image files, Wi-Fi, or cloud service. [Front-view preview](face-preview.svg) ([PNG](face-preview.png)).

![Expressions rendered by the device code](face-expressions.png)

**Design.** The look follows what makes cute characters inviting: [Groklet](https://x.com/grok/status/1914624830344978540) (Grok's big-eyed mascot), Meta Muse's [Jolly](https://fortune.com/2026/10/02/meta-openai-ai-agent-mascots-jolly-dots-trust/) (soft, round, rosy cheeks) and Japan's [LOVOT](https://www.engadget.com/2020-01-05-lovot-cuddle-robot-ces-2020.html) (huge layered anime eyes with involuntary eye movement): the baby-schema proportions of big eyes set a little low, huge dilated irises, bold catch-lights, round shapes, a gently happy default and frequent eye contact. The motion follows what reviewers describe of [Starboy](https://hesjustalittleguy.com/) (hand-animated eyes with natural blink rates and subtle gaze tremor that look toward the ground and sulk upside down): quick saccades that settle, a faint fixation tremor, lids that slide (not squash) and lower as the eyes look down, irises that foreshorten toward the side of the eyeball, catch-lights that stay with the light while the iris moves, breathing, and blinks that close fast and open slower. Expressions are sets of shape parameters the eyes blend between, an idea from [esp32-eyes](https://github.com/playfultechnology/esp32-eyes) and [RoboEyes](https://github.com/FluxGarage/RoboEyes).

## What it does

**Current build: directional wake.** Normal serial messages and power-saving
sleep are restored (`RAW_IMU_SERIAL_ONLY = false`). Motion wake requires the
recorded left/right pattern for about 1.5 seconds: AY acceleration dominates,
GZ rotation dominates GY, and both reverse repeatedly. The carried forward/back
pattern is rejected during dark-screen confirmation and does not trigger awake
motion reactions or reset activity once recognized. The existing visual gaze
follower and touch interactions remain available. Each wake grants a 30-second window. Every interaction renews at least 15
seconds from that interaction, indefinitely; touch can cancel closing.

The supplied recordings have no timestamps and several clipped gyro values.
Replay checks at assumed 40, 55, and 80 ms intervals pass; physical testing on
pants/bag is still needed. The gyro range is now ±2048 degrees/s to reduce
clipping. Thresholds are in `FaceConfig.h`; the detector is `LeftRightWake.h`.
Serial Monitor at **115200 baud** reports wake progress, AY/GZ energy shares,
GZ-to-GY ratio, and whether the motion was rejected as carrying.

**Optional six-axis diagnostics.** Set `RAW_IMU_SERIAL_ONLY = true` to print only
native signed register counts labeled **AX,AY,AZ,GX,GY,GZ**, at most every 40 ms.
Acceleration includes gravity (4096 counts/g); gyro measures rotation speed
(16 counts per degree/s at the new range). These are not absolute rotation
angles. The original hardware filter stays enabled; no software smoothing,
screen rotation, unit conversion, or gravity subtraction is applied. This
measurement mode starts awake and bypasses ordinary sleep, while preserving
critical battery shutdown. Startup clears retained WoM after ESP32 reset/upload,
and the checked diagnostic read skips failed I2C transfers and frozen sensor
timestamps. A reset can emit the ROM boot banner before the sketch starts.

- **Starts asleep with the display off.** A confirmed wake uses the existing eye-opening choreography. Idle glances cover the cardinal and diagonal directions, with tiny drift, irregular blinks, and occasional double blinks.
- **It follows the world** through the QMI8658 motion sensor, with or without touch:
  - **Tilt it any way** (left, right, forward, back, flat or upright) and the eyes look toward the side that went down, within a fraction of a second. A tilt held still for 20 s becomes its new normal.
  - The eyes stay square to the board (their line parallel to the edge with the USB-C port). Set `FACE_STAYS_LEVEL = true` to have the face roll back instead when the star is turned, staying level with the ground, or `FACE_ANGLE_DEG` if the enclosure holds the board turned.
  - **Rotate an upright screen left/right or tilt around a circle:** in-plane roll, pitch, and gyro pan combine into a full two-dimensional gaze; diagonals and circles use the same gaze springs as finger tracking. The eyes use the same gaze springs as finger tracking, with slow recentering to limit heading drift.
  - **Swing it around** and the eyes counter-move to keep looking at you, then ease back.
  - **Shake it for about 1 second of recognized motion** for the dizzy spiral-eyed tumble (the eyes rattle and startle the whole time, so you see it building); a brief bump or very gentle shake only rattles and startles it, and turning or spinning it never makes it dizzy (`TWIST_MAKES_DIZZY`, `SPIN_MAKES_DIZZY` turn those back on).
  - **Toss it** (or drop it): weightless for a moment, it gets startled. **Bump or nudge it** while it is calm: it notices with a quick widen and blink. **Pick it up after it has rested** a while: "oh!", then a happy hello.
  - **Walk with it:** it feels your footsteps. The eyes bob with each step, smile a little, look around more and keep watching the world go by; running makes it wide-eyed. Carrying does not reset sleep (`WALKING_KEEPS_AWAKE = false`).
  - **Hold it up in front of you:** it pays attention, looks at you more, its pupils widen and it does happy blinks, curious looks and the odd shy glance, This visual attention does not reset sleep; small sensor noise on a table cannot keep it awake.
  - **Leave it alone:** right up until it falls asleep its eyes keep moving (a new look every half second to few seconds, little micro-expressions in between), and every 3-6 seconds it does something by itself (looks around the room, a curious sideways look, a yawn, a happy squint, a puzzled look); the longer it is left alone, the more it gets bored and yawns, then it gets drowsy and falls asleep (`IDLE_SLEEP_MS`, 15 s after the last interaction).
  - **Warm** (pocket, hand, sun; the IMU chip above `HOT_C`): lazy yawns now and then. **Cold:** it shivers.
- The pupils lead and the whole eyes follow a beat later; the second eye trails a fraction behind the first, fast moves squash and stretch the eyes slightly, and the eye on the side being looked toward grows a little (curious), so gaze reads as a small head turn. Left alone it looks around (often back at you), blinks at random (with doubles, half blinks and slow blinks) and shows brief micro-expressions: a squint, a curious lift, a sceptical look.
- **Touch:** while a finger is on the screen the eyes follow it wherever it moves. A tap makes the nearer eye flinch first, then both rebound and settle; a double tap surprises them; **three or four quick taps** play the existing angry animation for 1.9 seconds. **Five consecutive screen taps** (no more than 650 ms apart) start a protected 3.5-second angry pause: touches are drained and ignored, the pointer does not move, and reactions cannot restart its anger. It then resumes normal interaction and its usual brief sulk. Battery shutdown takes precedence over the pause. Touch still renews the sleep timer during the pause. A short gentle press is a pet (happy crescent eyes). **Holding on annoys it**: after about a second the lids lower, then it glares at your finger and trembles; let go and it huffs, then cools off over a few seconds.
- **Everything works without the touch screen** (for example behind a protective Lexan cover), using the motion sensor:

  | Touch | Motion instead |
  | --- | --- |
  | Tap 1 / 2 / 3 / 4 times (boop, surprised, angry, angry) | **Knock on the case** 1 / 2 / 3 / 4 times; the eye nearer the knock flinches |
  | Swipe up / down / left / right (battery, sad, shy, happy) | **Tip it** up / down / left / right **and straight back** |
  | Gentle press (petted) | **Rock it gently** side to side, like cradling it; it stays content while you rock |
  | Hold a finger on it (worried, then angrier and angrier) | **Hold it upside down** (relative to how you usually hold it, which it learns); turn it back over and it huffs, then cools off |
  | Wake from sleep | **Tap the screen five times or shake left/right for about 1.5 s** |
  | — | **Lay it face down** for 4 s to put it to sleep |

  Knocks and sustained shakes are recognized by a small task on the ESP32's second core that reads the motion sensor about 330 times a second (a knock is over in milliseconds); recognition is independent of display frame time. It also switches on the sensor's ~54 Hz low-pass filter, which Waveshare's driver leaves off by mistake. A knock only counts when the star was still just before and settles right after, so walking, running, shaking and swinging it never count. Quick twists only cause dizziness when `TWIST_MAKES_DIZZY` is enabled; slower swings are rocking. Touch works while awake and wakes the creature from sleep, as in the testing branch. A held finger keeps renewing the sleep timer (`TOUCH_STUCK_MS = 0`); a nonzero `TOUCH_STUCK_MS` restores the optional stuck-touch cutoff.
- A swipe pulls and stretches the eyes in its direction, then releases them into an expression. The direction is taken from **the last flick of your finger** before it lifts, so you can press anywhere, wander around, and flick whichever way you like. **Up** calls `showBatteryLevel(percent)`: the eyes close, reopen to a height reflecting the approximate charge for 2.6 seconds, then return to their previous mood. An unknown reading produces a half-open questioning look. **Down** makes them sad, **left** shy, and **right** delighted. There are no touch rings, particles, icons, or battery bar: only the eyes react.
- **Expressions** (all kept; they morph smoothly into each other): neutral, happy (crescent "^" eyes), sad, angry, focused (determined), surprised (wide and round with small pupils), sleepy, squint, curious (one eye wide, one narrowed), confused, suspicious, worried, annoyed, love (heart pupils) and dizzy (spinning spiral pupils).
- **Carried around:** walking does not reset sleep. Routine movement can wake the CPU briefly, with the screen dark, for gesture verification. The recorded forward/back motion is rejected in replay; confirm with the physical carrying test.
- **Shaking** gets an instant reaction: from the first stroke the eyes widen in alarm, their pupils shrink and jiggle, and each stroke throws the eyes the other way. A very gentle shake only rattles and startles it; moderate shaking can now trigger the full reaction, and turning, twisting or spinning it never makes it dizzy. Only **shaking it for about 1 second of recognized motion** (moderate back-and-forth motion, with four alternating strokes; brief uneven pauses up to roughly half a second are tolerated, while longer pauses restart recognition) sends the eyes into a wobble and a slowing tumble with **spinning spiral pupils**, followed by an angry glare that cools off over a few seconds — "shake him and he gets dizzy and mad at you". Keep shaking and the tumble starts again. The same startled, dizzy wake sequence plays after a sustained shake wakes it from sleep. A face-down pause makes one eye narrow. If the IMU's own chip temperature remains below 18 °C for eight seconds, the eyes shiver; this reads chip temperature and may lag or differ from the surrounding air.
- **Power-saving sleep:** A wake grants **30 seconds**. Interaction grants **at least 15 seconds from the latest input** without shortening time already remaining. At the inactivity deadline, the eyes have drooped, completed their closing blink, and faded to black; then the LCD enters sleep-in, the backlight turns off, and the ESP32 enters **deep sleep**. Every touch, knock, tilt flick, rocking, confirmed shake, and purposeful tilt held still resets the timer; idle animation, sensor noise, standing in one upside-down orientation, and walking do not. Any touch IRQ during the closing animation cancels sleep immediately, even if the touch controller reports incomplete coordinates. There is no hard session limit: continued touch can keep it awake indefinitely. Each input renews the 15-second window, rather than banking 15 seconds for each rapid tap. Closing begins 5.4 seconds before the current deadline and touch can interrupt it. With no input, the initial window ends at 30 seconds (`WAKE_AWAKE_MS`); after renewal, it ends 15 seconds after the last interaction (`IDLE_SLEEP_MS`).
- From deep sleep, **tap the screen five times** (separate presses and releases, no more than 650 ms between presses, within a 3-second check) to wake, or perform the recorded
  **left/right shake for about 1.5 seconds** (25% shorter than before). The CPU checks motion with the
  screen dark: gravity-free AY energy must be at least 50% of acceleration
  energy, GZ at least 42% of rotation energy and 1.35 times GY energy, with
  sustained strength and repeated AY/GZ reversals. The supplied forward/back
  trace fails these combined checks. A single bump, short shake, pure rotation,
  or translation without matching rotation cannot confirm this gesture.

The face layout is symmetric at rest. Small differences between eyes during expressions are intentional. The physical shell preview is a visual mockup based on the screenshots; no CAD geometry was altered. Starboy's camera, microphone, haptic motor, and proprietary device-to-device protocol are absent from this Waveshare board, so its hand-gesture, sound, vibration, and Starboy-to-Starboy features cannot run here without new hardware or protocol information. The swipe and tap mappings provide hands-on substitutes for some of those interactions.

## Battery expressions and shutdown

The normal eyes and every existing expression retain their shape, glow, anti-aliasing, and spring animation. Charge adds a smoothly blended layer:

| Estimated charge | Behavior |
| --- | --- |
| Above 50% / unknown | Existing purple eyes and normal personality |
| 50% or lower | A little tired, soft violet color, slower blinks |
| 25% or lower | Sadder and heavier lids, muted periwinkle, more yawns |
| 10% or lower | Very drowsy and sad, muted blue, less idle movement |
| 5% or lower | Exhausted, very sad half-closed eyes, dim blue-gray color |
| 3% or lower | Finish closing, LCD sleep-in, backlight off, ESP32 deep sleep; all gesture wake disabled |

Critical sleep briefly checks charge once a minute with the screen dark. It remains asleep until the estimate reaches 8% or USB raises the sensed supply above the cell range, then resumes normal sleep awaiting interaction. Battery is sampled every 10 seconds while awake; smoothing and recovery hysteresis reduce flickering between moods. `BATTERY_CHECK_MS`, `BATTERY_RECHECK_S`, and the thresholds are in `FaceConfig.h`.

The [board schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28/ESP32-S3-Touch-LCD-1.28-Sch.pdf) connects GPIO1 through a 200k/100k divider to **VSYS**, not directly to VBAT. Percentages are approximate voltage estimates and need calibration with the intended cell while on battery power. This is software power saving, not a battery disconnect or a substitute for the cell's protection circuit.

## Existing hardware kept intact

The project targets the **Waveshare ESP32-S3-Touch-LCD-1.28** with a **240×240 GC9A01A LCD**. It keeps Waveshare's `LCD_1in28` and `DEV_Config` SPI driver, `CST816S` touch driver, and `QMI8658` IMU driver. LCD pins remain DC 8, CS 9, clock 10, MOSI 11, MISO 12, reset 14, and backlight 2. I²C remains SDA 6 and SCL 7; touch reset/IRQ are 13/5, QMI8658 INT2 is GPIO3, and battery ADC is GPIO1. The full-screen RGB565 buffer is allocated once at startup and reused; each frame is composed off-screen and only the changed window is pushed, so there is no flicker. The frame cap is 60 FPS. Interior pixels take a cheap fast path and only edge pixels get the full anti-aliasing math, but the real frame rate has not been measured on the board yet: set `LOG_FPS` in `StarFace.ino` to print it.

## Upload

1. Open `StarFace/StarFace.ino` in Arduino IDE. All board-specific driver files are beside the sketch, so no extra display or IMU library is needed.
2. Install **esp32 by Espressif Systems** in Boards Manager (checked with core 3.3). Select **ESP32S3 Dev Module**. Set **Flash Size: 16 MB**, **PSRAM: QSPI PSRAM**, and **USB CDC On Boot: Disabled**. The Waveshare board's USB-C port uses a USB-to-serial chip.
3. Connect the board over USB-C, choose its serial port, and click **Upload**. If it does not enter the bootloader automatically, hold **BOOT**, press **RESET**, release **BOOT**, then upload.

The sketch uses the pin mapping and GC9A01A, CST816S, and QMI8658 driver code from the [Waveshare example package](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.28/Resources-And-Documents). The included driver files retain their original notices.

## Using the eyes in code

The eyes are a self-contained animation system (`Eyes`), driven by a behaviour layer (`CreatureAnimator`) that turns touch, motion and time into moods. You can also drive them yourself from anywhere in the sketch; every call is animated, so the eyes always ease from wherever they are:

```cpp
eyes.setExpression(HAPPY);          // NEUTRAL, HAPPY, SAD, ANGRY, FOCUSED, SURPRISED, SLEEPY, SQUINT,
                                    // CURIOUS, CONFUSED, SUSPICIOUS, WORRIED, ANNOYED, LOVE, DIZZY
eyes.setExpression(ANGRY, 0.5f);    // optional intensity (0 neutral .. 1 full .. ~1.3) and speed
eyes.setExpressionMix(FOCUSED, ANGRY, 0.7f); // a blend of two
eyes.lookAt(0.6f, -0.2f);           // -1..1: x right, y down (optional speed)
eyes.lookAtPoint(200, 120);         // a point on the screen, px
eyes.lookAround();                  // hand the gaze back to the idle behaviour
eyes.blink();                       // or blink(Blinker::SLOW), wink(0)
eyes.setIdle(false);                // stop the automatic look-arounds and micro-expressions
eyes.setAutoBlink(false);
```

The behaviour layer sets an expression when something happens (a tap, a shake, falling asleep) and otherwise leaves the eyes alone, so a call of yours stays in effect until the creature reacts to something. While idle behaviour is on, a single `lookAt()` holds for `Eyes::IDLE_RESUME_MS` (2.5 s) before the eyes wander again; call it every frame to track something.

| File | Role |
| --- | --- |
| `Eyes.h/.cpp` | The public API; layers expression, openness, blinks, gaze, physics and effects into each frame and keeps the eyes on the round screen |
| `EyeShape.h` | The shape of one eye (size, roundness, tilt, bend, upper and lower lids, pupil, heart, spiral) and `ShapeSpring`, which springs every field so shapes morph |
| `Expressions.h/.cpp` | Each expression as a pair of `EyeShape`s plus how fast and springy its arrival is |
| `Blinker.h` | Blinks: fast accelerating close, slower decelerating open, random timing and blink types |
| `GazeController.h` | Gaze: pupils lead, the eyes follow, micro-saccades |
| `IdleBehavior.h` | Idle look-arounds, glances and micro-expressions |
| `EyeRenderer.h/.cpp` | Draws the eyes as anti-aliased vector shapes |
| `MotionGestures.h` | Knocks, tilt flicks, rocking, and `WorldFollower` (tilt gaze, level roll, swing, spin, toss) |
| `AnimMath.h` | Springs, easing curves, smooth noise |
| `CreatureAnimator.h/.cpp` | Moods, wake and sleep scripts, dizzy tumble, shake rattle, touch reactions, backlight |

## How the eyes are drawn

`EyeRenderer` builds each eye from smooth shapes, not sprites. Each eye is an oval (optionally tilted and bent into an arc) with an upper and a lower lid sliding over it, each a curved line that arches around the eyeball, with rounded corners where a lid meets the outline. Every pixel gets a signed distance to that shape, computed in floating point, so outlines move and morph continuously in sub-pixel steps. The iris is a circle that narrows as it turns toward the side of the eyeball, with two catch-lights (an optional dark centre, `PUPIL_CORE`); it blends into a heart or a spinning spiral by mixing distance fields. The glow is the same distance seen from outside: a halo of the eye colour that fades to black over `GLOW_SIZE`.

The distance also gives each edge pixel its exact coverage, so a pixel the outline passes through gets the matching blend of the colours on either side (eye, iris and black). That is what makes the curves look continuous instead of stepped on a 240-pixel screen. The palette stays strict: every pixel is a blend of black, `EYE_COLOR` and `PUPIL_COLOR` (edges and the glow), and the desktop tests check every rendered pixel to prove it. Blinks close the lids over the eye until they meet in a soft curved line; the iris tucks away first, so the shut eye is one clean stroke.

The per-pixel loop avoids float division and `sqrtf`, which are slow on the ESP32. Each frame is composed off-screen and only the changed rectangle is pushed. Sizes are fractions of the screen and positions are relative to its centre, so changing `SCREEN_WIDTH`/`SCREEN_HEIGHT` (and `SCREEN_IS_ROUND`) re-lays out the face; on a round panel every eye is fitted to a soft circular safe area each frame, so gaze, surprise and impacts ease against the edge instead of clipping.

[`tools/preview`](tools/preview) builds the same code on a PC and writes frames. Use it to check shapes and timing without flashing.

## Tuning

Everything visual and behavioral is grouped in `StarFace/FaceConfig.h`:

| To change | Constant |
| --- | --- |
| Colours, glow and smoothing | `EYE_COLOR` (eyes, glow, catch-lights), `PUPIL_COLOR` (irises, hearts, spirals), `GLOW`, `GLOW_STRENGTH`, `GLOW_SIZE`, `ANTI_ALIAS`, `CHEEK_BLUSH`, `CHEEK_COLOR` |
| Eye size | `EYE_WIDTH`, `EYE_HEIGHT` (fractions of the screen) |
| Eye spacing and vertical position | `EYE_SPACING`, `EYE_OFFSET_Y` |
| Iris, its dark centre, catch-lights, pupil life | `PUPIL_SIZE`, `PUPIL_CORE`, `GLINT_SIZE`, `PUPIL_LIFE` |
| Following the world | `IMU_ROTATION` (if tilting looks wrong), `TILT_GAZE`, `TILT_SETTLE_S`, `FACE_STAYS_LEVEL`, `FACE_ROLL_MAX`, `FACE_ANGLE_DEG`, `SWING_GAZE`, `SPIN_TURNS_FOR_DIZZY`, `FREEFALL_MS`, `STEP_MS2`, `WALKING_KEEPS_AWAKE`, `HOT_C` |
| Maximum eye movement | `MAX_PUPIL_MOVE`, `MAX_EYE_MOVE_X`, `MAX_EYE_MOVE_Y`, `MAX_EXPRESSION_EXPANSION` |
| Animation speed | `ANIMATION_SPEED` (above 1 is snappier), `EXPRESSION_BLEND_SPEED`, `GAZE_SPEED` |
| Expression strength | `EXPRESSION_INTENSITY` (the shapes themselves are in `Expressions.cpp`) |
| Blink speed and frequency | `BLINK_CLOSE_MS`, `BLINK_OPEN_MS`, `BLINK_MIN_MS`, `BLINK_MAX_MS`, `DOUBLE_BLINK_CHANCE` |
| Idle restlessness | `IDLE_LIVELINESS`, `SQUASH_STRETCH` |
| Screen | `SCREEN_WIDTH`, `SCREEN_HEIGHT`, `SCREEN_IS_ROUND`, `SAFE_MARGIN` |
| Knocks, tilt flicks, rocking, upside down, face-down sleep | `KNOCK_MS2` (lower = more sensitive), `KNOCK_QUIET_MS2`, `KNOCK_QUIET_BEFORE_MS`, `KNOCK_GAP_MS`, `FLICK_START_RAD_S`, `FLICK_MIN_TILT_MS2`, `FLICK_MAX_MS`, `ROCK_MIN_HALF_MS`, `ROCK_MAX_HALF_MS`, `ROCK_SWINGS`, `UPSIDE_DOWN_MS`, `FACE_DOWN_SLEEP_MS` |
| Touch on or off, stuck touches | `TOUCH_ENABLED`, `TOUCH_STUCK_MS` |
| Touch, swipes and hold anger | `TAP_MAX_MS`, `TAP_MAX_MOVE_PX`, `SWIPE_MIN_PX`, `SWIPE_MIN_SPEED`, `SWIPE_MAX_SHORT_MS`, `HOLD_ANGER_START_MS`, `HOLD_ANGER_FULL_MS`, `TOUCH_RELEASE_TIMEOUT_MS` |
| Shake sensitivity | `SHAKE_DIZZY_HOLD_MS` (1 s of recognized shaking), `SHAKE_DIZZY_STRENGTH`, `SHAKE_DIZZY_DROPOUT_MS`, `SHAKE_DIZZY_SWINGS`, `SHAKE_STROKE_MS2`, `TWIST_MAKES_DIZZY`, `SPIN_MAKES_DIZZY`, `SHAKE_GAP_MS`, `SHAKE_NOISE_MS2`, `SHAKE_FULL_MS2`, `BUMP_MS2`, `PICKUP_MS2` |
| Sleep and wake | `AUTO_DEEP_SLEEP`, `IDLE_SLEEP_MS`, `WAKE_AWAKE_MS`, `IDLE_NAP_MS`, `TOUCH_WAKE_TAPS`, `TOUCH_WAKE_WINDOW_MS`, `SHAKE_WAKE_HOLD_MS`, `SHAKE_WAKE_STRENGTH`, `SHAKE_WAKE_STROKE_MS2`, `SHAKE_WAKE_DROPOUT_MS`, `SHAKE_WAKE_SWINGS`, `BACKLIGHT_PERCENT`, `NAP_BACKLIGHT_PERCENT` |
| Anger cool-down | `ANGER_COOLDOWN_S` |

Hardware-specific settings stay in `StarFace.ino`:

| To change | Constant |
| --- | --- |
| Motion alarm that wakes the CPU from deep sleep | `WOM_THRESHOLD_MG` |
| Twist sensitivity (in `ShakeDetector.h`) | `TWIST_RATE_RAD_S`, `TWIST_MIN_HALF_TURN_RAD`, `TWIST_REVERSALS_TO_WAKE`, `TWIST_WINDOW_MS` |


While awake, each counted twist reversal needs roughly 14° of turning at 69°/s or faster, on the same X or Y gyro axis. `WOM_THRESHOLD_MG = 200` sets the low-power accelerometer's coarse motion alert; routine worn motion can wake the CPU briefly, but a sustained shake is required before motion lights the display (the log prints its progress). `LOG_SHAKE` in `StarFace.ino` (on by default) prints the shake strength over serial at 115200 baud while it is being shaken, and each knock (with its strength), flick, rocking spell and upside-down/face-down gesture, and the boot log says whether the motion sensor was found. If shaking feels too hard or too easy, compare the printed strength with `SHAKE_DIZZY_STRENGTH` (the log also shows how long the current shake has been kept up) and adjust it or `SHAKE_DIZZY_HOLD_MS`; waking from sleep uses the `SHAKE_WAKE_*` settings. If knocks don't register, lower `KNOCK_MS2` (2.5 catches even a fingernail flick); if setting it down counts as a knock, raise it. If tilting makes the eyes look uphill (or sideways) instead of toward the low side, set `IMU_ROTATION` in `FaceConfig.h` to 1, 2 or 3 (how the sensor is turned relative to the screen, in quarter turns); that one setting fixes tilt gaze, the level roll, swings, flicks and knock directions together. The detection logic lives in `ShakeDetector.h` and `MotionGestures.h` and is tested on a PC by `tools/preview/run_tests.sh`. `BACKLIGHT_PERCENT` sets active backlight brightness.

This sketch restores the testing branch’s GPIO3 (QMI8658 INT2) and GPIO5 (CST816S touch IRQ) active-low deep-sleep wake inputs. The touch chip remains in automatic standby; normal sleep does not send its full sleep command. The unsuccessful swivel-only path and one-second timer polling are removed. When an IMU motion interrupt wakes the CPU, the display stays dark until sustained shaking is confirmed. Critical battery sleep still disables both gesture wake sources and checks charge once a minute. The restoration was compiled and checked in host simulations, then uploaded to the connected ESP32-S3 on COM10; physical wake acceptance and walking rejection must also be checked on the assembled keychain.

The [research and animation notes](RESEARCH.md) explain what the reference product appears to do, what was observed versus advertised, and why this face has its own reactions.
