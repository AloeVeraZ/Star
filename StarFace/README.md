# Star Face

A wearable eye pet for the **Waveshare ESP32-S3-Touch-LCD-1.28**, adapted from [CREATURE's Starboy](https://hesjustalittleguy.com/) behaviors to the sensors in this board. The 240 × 240 screen shows two smooth, glowing cartoon eyes centered in the symmetrical black printed star enclosure. Eye shape, lids, gaze, color, and motion carry the expressions; there is no mouth. The face is drawn on the device with anti-aliased edges and needs no image files, Wi-Fi, or cloud service. [Front-view preview](face-preview.svg) ([PNG](face-preview.png)).

## What it does

- Starts with closed eyes, slowly opens them, looks left and right, then looks forward. Idle glances cover the cardinal and diagonal directions, with tiny drift, irregular blinks, and occasional double blinks.
- Pupils follow the board's tilt using its QMI8658 motion sensor. The second eye follows a fraction later, giving the gaze a softer feel.
- A tap makes the nearer eye flinch first, then both rebound and settle; a double tap surprises them; three quick taps make them dart anxiously. Dragging pulls the gaze and eye pair toward the finger. A long press gives them a slow, contented half-blink; two long presses within 3.5 seconds switch to another persistent eye shape and color.
- A swipe pulls and stretches the eyes in its direction, then releases them into an expression. **Up** calls `showBatteryLevel(percent)`: the eyes close, reopen to a height reflecting the approximate charge for 2.6 seconds, then return to their previous mood. An unknown reading produces a half-open questioning look. **Down** makes them sad, **left** shy, and **right** delighted. There are no touch rings, particles, icons, or battery bar: only the eyes react.
- The board has four eye shapes and seven color palettes. The default is soft lilac; pink, cyan, blue, red, white, and yellow are available in one palette table. A small personality value derived from its own chip ID changes quiet idle habits.
- Three quick back-and-forth twists around **either axis running across the screen** send the eyes into a three-turn, slowing tumble, followed by an annoyed glare. This plays while awake or after a confirmed twist wake from deep sleep. A face-down pause makes one eye narrow. If the IMU's own chip temperature remains below 18 °C for eight seconds, the eyes shiver; this reads chip temperature and may lag or differ from the surrounding air.
- After **30 seconds from the latest interaction**, the eyes droop, make one long closing blink, stay shut briefly, and then the LCD enters sleep-in, the backlight turns off, and the ESP32 enters **deep sleep**. Every touch event, swipe, tap, recognized fast rotation, and purposeful tilt held still resets the timer. The creature's own idle animations and ordinary walking do not. Any touch IRQ during the closing animation cancels sleep immediately, even if the touch controller reports incomplete coordinates.
- From deep sleep, **press the screen twice** to wake the face. The first press wakes the CPU with the display dark; a second distinct press or the CST816S double-click gesture must arrive within three seconds of boot. Alternatively, twist it back and forth quickly around either in-plane screen axis until three direction changes are detected within three seconds. The QMI8658's low-power motion interrupt starts a dark-screen confirmation; if the pattern is absent, it re-enters deep sleep. Keep twisting briefly after the ESP32 begins booting because gyro readings cannot be recorded while it is asleep.

The face layout is symmetric at rest. Small differences between eyes during expressions are intentional. The physical shell preview is a visual mockup based on the screenshots; no CAD geometry was altered. Starboy's camera, microphone, haptic motor, and proprietary device-to-device protocol are absent from this Waveshare board, so its hand-gesture, sound, vibration, and Starboy-to-Starboy features cannot run here without new hardware or protocol information. The swipe and tap mappings provide hands-on substitutes for some of those interactions.

## Existing hardware kept intact

The project targets the **Waveshare ESP32-S3-Touch-LCD-1.28** with a **240×240 GC9A01A LCD**. It keeps Waveshare's `LCD_1in28` and `DEV_Config` SPI driver, `CST816S` touch driver, and `QMI8658` IMU driver. LCD pins remain DC 8, CS 9, clock 10, MOSI 11, MISO 12, reset 14, and backlight 2. I²C remains SDA 6 and SCL 7; touch reset/IRQ are 13/5, QMI8658 INT2 is GPIO3, and battery ADC is GPIO1. The full-screen RGB565 buffer is allocated once at startup and reused at about 30 FPS.

## Upload

1. Open `StarFace/StarFace.ino` in Arduino IDE. All board-specific driver files are beside the sketch, so no extra display or IMU library is needed.
2. Install **esp32 by Espressif Systems** in Boards Manager. Select **ESP32S3 Dev Module**. Set **Flash Size: 16 MB**, **PSRAM: QSPI PSRAM**, and **USB CDC On Boot: Disabled**. The Waveshare board's USB-C port uses a USB-to-serial chip.
3. Connect the board over USB-C, choose its serial port, and click **Upload**. If it does not enter the bootloader automatically, hold **BOOT**, press **RESET**, release **BOOT**, then upload.

The sketch uses the pin mapping and GC9A01A, CST816S, and QMI8658 driver code from the [Waveshare example package](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.28/Resources-And-Documents). The included driver files retain their original notices.

## Tuning

The main controls are near the top of `StarFace.ino`:

| To change | Constant |
| --- | --- |
| Eye size | `EYE_BASE_HALF_WIDTH`, `EYE_BASE_HALF_HEIGHT` |
| Eye spacing | `EYE_SPACING` |
| Eye color | `DEFAULT_EYE_COLOR` and the seven RGB entries in `setPalette()` |
| Blink frequency | `BLINK_MIN_MS`, `BLINK_MAX_MS` |
| Eye movement speed | `EYE_MOTION_RESPONSE` (larger is snappier) |
| Sleep and wake timing | `IDLE_SLEEP_MS`, `SLEEPY_ANIM_MS`, `WAKE_ANIM_MS` |
| Twist wake sensitivity | `TWIST_RATE_RAD_S`, `TWIST_MIN_HALF_TURN_RAD`, `TWIST_REVERSALS_TO_WAKE`, `TWIST_WINDOW_MS`, `WOM_THRESHOLD_MG` |
| Two-press wake window | `TOUCH_TWO_PRESS_MS` |

Each counted reversal needs roughly 14° of turning at 69°/s or faster, on the same X or Y gyro axis. A single jolt will not light the screen. `WOM_THRESHOLD_MG = 90` sets the low-power accelerometer's coarse motion alert; routine worn motion can now wake the CPU briefly, but only the gyro pattern lights the display. `DEV_SET_PWM(62)` sets active backlight brightness. If the eyes look the wrong way when tilted in your enclosure, reverse the sign of `a[0]` or `a[1]` in `handleMotion()`.

This sketch uses GPIO3 (QMI8658 INT2) and GPIO5 (CST816S IRQ) as active-low deep-sleep wake pins. It keeps the touch chip's automatic standby enabled: sending its full sleep command would disable touch wake. Board-level battery life also depends on the regulator, USB-to-serial chip, charger, and LCD electronics. Measure current on your assembled, battery-powered star before estimating runtime. It was compiled and flashed to an ESP32-S3 on COM10, and the board logged deep-sleep entry. Physical touch/swipe response, wake sensitivity, temperature threshold, battery percentage, and current draw still need hands-on calibration.

The [research and animation notes](RESEARCH.md) explain what the reference product appears to do, what was observed versus advertised, and why this face has its own reactions.
