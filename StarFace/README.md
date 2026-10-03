# Star Face

A wearable eye pet for the **Waveshare ESP32-S3-Touch-LCD-1.28**, adapted from [CREATURE's Starboy](https://hesjustalittleguy.com/) behaviors to the sensors in this board. The round 240 × 240 screen shows two large, glowing cartoon eyes composed for the circular panel inside the symmetrical black printed star enclosure. Eye shape, lids, gaze, color, glow, and motion carry the expressions; there is no mouth. The face is drawn procedurally on the device with vector-smooth, anti-aliased edges and needs no image files, Wi-Fi, or cloud service. [Front-view preview](face-preview.svg) ([PNG](face-preview.png)).

![Expressions rendered by the device code](face-expressions.png)

## What it does

- Starts with closed eyes, slowly opens them, looks left and right, then looks forward. Idle glances cover the cardinal and diagonal directions, with tiny drift, irregular blinks, and occasional double blinks.
- Pupils follow the board's tilt using its QMI8658 motion sensor. The pupils lead and the whole eyes follow a beat later; the second eye trails a fraction behind the first, fast moves squash and stretch the eyes slightly, and the eye on the side being looked toward turns a little away, so gaze reads as a small head turn.
- A tap makes the nearer eye flinch first, then both rebound and settle; a double tap surprises them; three quick taps make them dart anxiously. Dragging pulls the gaze and eye pair toward the finger. A long press gives them a slow, contented half-blink; two long presses within 3.5 seconds switch to another persistent eye shape and color.
- A swipe pulls and stretches the eyes in its direction, then releases them into an expression. **Up** calls `showBatteryLevel(percent)`: the eyes close, reopen to a height reflecting the approximate charge for 2.6 seconds, then return to their previous mood. An unknown reading produces a half-open questioning look. **Down** makes them sad, **left** shy, and **right** delighted. There are no touch rings, particles, icons, or battery bar: only the eyes react.
- The board has four eye shapes and seven color palettes. The default is soft lilac; pink, cyan, blue, red, white, and yellow are available in one palette table. A small personality value derived from its own chip ID changes quiet idle habits.
- **Shaking** gets an instant reaction: from the first stroke the eyes widen in alarm, their pupils shrink and jiggle, and each stroke throws the eyes the other way. Three back-and-forth strokes (or three quick twists around **either axis running across the screen**) send them into a wobble and a three-turn, slowing tumble (pupils spinning against the eyes), followed by an annoyed, red-tinged glare that cools off over a few seconds — "shake him and he gets dizzy and mad at you". Keep shaking and the tumble starts again. The same sequence plays after a shake wakes it from sleep. A face-down pause makes one eye narrow. If the IMU's own chip temperature remains below 18 °C for eight seconds, the eyes shiver; this reads chip temperature and may lag or differ from the surrounding air.
- **Power-saving sleep:** after **30 seconds from the latest interaction**, the eyes droop, make one long closing blink, close, and fade to black; then the LCD enters sleep-in, the backlight turns off, and the ESP32 enters **deep sleep**. Every touch event, swipe, tap, shake, recognized fast rotation, and purposeful tilt held still resets the timer. The creature's own idle animations and ordinary walking do not. Any touch IRQ during the closing animation cancels sleep immediately, even if the touch controller reports incomplete coordinates.
- From deep sleep, **touch the screen** to wake the face (set `TOUCH_WAKE_DOUBLE_PRESS` to require two presses within three seconds, so it doesn't wake in a pocket). Or **shake it**: the QMI8658's low-power motion interrupt wakes the CPU with the display still dark, and the face turns on once it counts three strong back-and-forth strokes (or three quick twists) — it then wakes startled and dizzy. A walking jolt has neither pattern, so it goes straight back to sleep. Keep shaking for a moment after the first jolt, because readings can't be recorded while the CPU is asleep. Set `AUTO_DEEP_SLEEP = false` to keep the screen on with a dim, breathing nap instead.

The face layout is symmetric at rest. Small differences between eyes during expressions are intentional. The physical shell preview is a visual mockup based on the screenshots; no CAD geometry was altered. Starboy's camera, microphone, haptic motor, and proprietary device-to-device protocol are absent from this Waveshare board, so its hand-gesture, sound, vibration, and Starboy-to-Starboy features cannot run here without new hardware or protocol information. The swipe and tap mappings provide hands-on substitutes for some of those interactions.

## Existing hardware kept intact

The project targets the **Waveshare ESP32-S3-Touch-LCD-1.28** with a **240×240 GC9A01A LCD**. It keeps Waveshare's `LCD_1in28` and `DEV_Config` SPI driver, `CST816S` touch driver, and `QMI8658` IMU driver. LCD pins remain DC 8, CS 9, clock 10, MOSI 11, MISO 12, reset 14, and backlight 2. I²C remains SDA 6 and SCL 7; touch reset/IRQ are 13/5, QMI8658 INT2 is GPIO3, and battery ADC is GPIO1. The full-screen RGB565 buffer is allocated once at startup and reused; each frame is composed off-screen and only the changed window is pushed, so there is no flicker. The frame cap is 60 FPS; the estimated rendering cost (about 15 ms per frame plus the SPI push) puts the real rate around 40 FPS. Set `LOG_FPS` in `StarFace.ino` to measure it on your board.

## Upload

1. Open `StarFace/StarFace.ino` in Arduino IDE. All board-specific driver files are beside the sketch, so no extra display or IMU library is needed.
2. Install **esp32 by Espressif Systems** in Boards Manager (checked with core 3.3). Select **ESP32S3 Dev Module**. Set **Flash Size: 16 MB**, **PSRAM: QSPI PSRAM**, and **USB CDC On Boot: Disabled**. The Waveshare board's USB-C port uses a USB-to-serial chip.
3. Connect the board over USB-C, choose its serial port, and click **Upload**. If it does not enter the bootloader automatically, hold **BOOT**, press **RESET**, release **BOOT**, then upload.

The sketch uses the pin mapping and GC9A01A, CST816S, and QMI8658 driver code from the [Waveshare example package](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.28/Resources-And-Documents). The included driver files retain their original notices.

## How the eyes are drawn

`EyeRenderer` builds each eye from smooth shapes, not sprites. Each eye is an ellipse intersected with curved upper and lower lids (a rounded, smooth intersection), optionally bent into an arc (happy ^ or sleepy U). Every pixel gets an approximate signed distance to that shape. The distance gives exact anti-aliased coverage at the edge, which is why curves and angled lids look like vector art on the raster panel, and it also drives an exponential glow outside the edge. Inside, the eye is shaded with a vertical gradient, a saturated rim, a soft shadow under the lids, an iris lit from below with a dark ring and pupil, and two crisp highlights that lag the iris slightly (parallax). Ordered dithering hides RGB565 banding in the gradients and glow. Pixels outside the visible circle are skipped, and the glow fades out before the bezel.

All geometry and motion are float, so slow movements travel in sub-pixel steps. The per-pixel loop avoids float division and `sqrtf`, which are slow on the ESP32, and plain interior pixels take a fast path.

`CreatureAnimator` keeps the existing spring-driven moods and layers on top of them. Every eye is fitted to a soft circular safe radius each frame, so gaze, surprise, and impacts ease against the round edge instead of clipping.

[`tools/preview`](tools/preview) builds the same renderer and animator on a PC and writes frames. Use it to check shapes and timing without flashing.

## Tuning

Everything visual and behavioral is grouped in `StarFace/FaceConfig.h`:

| To change | Constant |
| --- | --- |
| Eye size | `EYE_WIDTH`, `EYE_HEIGHT`, `EYE_IRIS_RADIUS` |
| Eye spacing and vertical position | `EYE_SPACING`, `EYE_CENTER_Y` |
| Round-screen safe area | `SCREEN_CX`, `SCREEN_CY`, `SCREEN_RADIUS`, `SAFE_RADIUS`, `SAFE_SOFTNESS` |
| Gaze travel | `MAX_GAZE_SHIFT_X`, `MAX_GAZE_SHIFT_Y`, `PUPIL_TRAVEL` |
| Largest expression | `MAX_EXPRESSION_EXPANSION` |
| Animation speed | `ANIMATION_SPEED` (above 1 is snappier) |
| Expression strength | `EXPRESSION_INTENSITY` |
| Blink timing | `BLINK_MIN_MS`, `BLINK_MAX_MS`, `DOUBLE_BLINK_CHANCE` |
| Eye color and glow | `DEFAULT_EYE_COLOR`, `EYE_PALETTE`, `GLOW_STRENGTH`, `GLOW_FALLOFF`, `GLOW_EXTENT` |
| Shake sensitivity | `SHAKE_STROKE_MS2`, `SHAKE_STROKES_FOR_DIZZY`, `SHAKE_GAP_MS`, `SHAKE_NOISE_MS2`, `SHAKE_FULL_MS2`, `BUMP_MS2`, `PICKUP_MS2` |
| Sleep and wake | `AUTO_DEEP_SLEEP`, `IDLE_SLEEP_MS`, `IDLE_NAP_MS`, `TOUCH_WAKE_DOUBLE_PRESS`, `SHAKE_STROKES_TO_WAKE`, `BACKLIGHT_PERCENT`, `NAP_BACKLIGHT_PERCENT` |
| Anger cool-down | `ANGER_COOLDOWN_S` |

Hardware-specific settings stay in `StarFace.ino`:

| To change | Constant |
| --- | --- |
| Twist wake sensitivity | `TWIST_RATE_RAD_S`, `TWIST_MIN_HALF_TURN_RAD`, `TWIST_REVERSALS_TO_WAKE`, `TWIST_WINDOW_MS`, `WOM_THRESHOLD_MG` |
| Two-press wake window (with `TOUCH_WAKE_DOUBLE_PRESS`) | `TOUCH_TWO_PRESS_MS` |

Each counted reversal needs roughly 14° of turning at 69°/s or faster, on the same X or Y gyro axis. A single jolt will not light the screen. `WOM_THRESHOLD_MG = 90` sets the low-power accelerometer's coarse motion alert; routine worn motion can wake the CPU briefly, but only a shake or twist pattern lights the display. If shaking feels too hard or too easy, set `LOG_SHAKE = true` in `StarFace.ino`, watch the stroke strengths over serial, and adjust `SHAKE_STROKE_MS2`. `BACKLIGHT_PERCENT` sets active backlight brightness. If the eyes look the wrong way when tilted in your enclosure, reverse the sign of `a[0]` or `a[1]` in `handleMotion()`.

This sketch uses GPIO3 (QMI8658 INT2) and GPIO5 (CST816S IRQ) as active-low deep-sleep wake pins. It keeps the touch chip's automatic standby enabled: sending its full sleep command would disable touch wake. Board-level battery life also depends on the regulator, USB-to-serial chip, charger, and LCD electronics. Measure current on your assembled, battery-powered star before estimating runtime. It was compiled and flashed to an ESP32-S3 on COM10, and the board logged deep-sleep entry. Physical touch/swipe response, wake sensitivity, temperature threshold, battery percentage, and current draw still need hands-on calibration.

The [research and animation notes](RESEARCH.md) explain what the reference product appears to do, what was observed versus advertised, and why this face has its own reactions.
