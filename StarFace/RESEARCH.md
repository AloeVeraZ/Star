# Motion and battery research for the current testing build

Changes build on testing commit `5952a15`; the original eye proportions, expression presets, blinks, spring motion, wake/sleep scripts, and dizzy performance are preserved.

The current upload enables `RAW_ACCEL_SERIAL_ONLY`: native signed accelerometer
register counts are printed as X,Y,Z CSV at 115200 baud, with other sketch and
driver messages suppressed. The original hardware filtering remains in place.
Idle, maximum-awake, startup, and face-down sleep are temporarily bypassed for
continuous measurement; critical battery shutdown remains active. The user
confirmed touch/shake waking works but walking and forward-back rocking still
wake the display. The measurements are intended to tune that physical failure;
the normal behavior below resumes when the diagnostic flag is disabled.

The [Waveshare schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28/ESP32-S3-Touch-LCD-1.28-Sch.pdf) identifies the QMI8658, shared I2C bus, INT2 on GPIO3, touch IRQ on GPIO5, and a 200k/100k divider from VSYS to GPIO1. VSYS includes the USB/charger power path, so GPIO1 is not an independent cell fuel gauge. Charge expressions use calibrated ADC millivolts times three, a trimmed average, smoothing, a voltage lookup, and recovery hysteresis. At 3% or less, the screen and CPU sleep with only a periodic dark charge check. Exact percentages and total board current require cell/load measurements.

The [QMI8658C revision 0.9 datasheet](https://files.waveshare.com/wiki/common/QMI8658C_datasheet_rev_0.9.pdf), sections 5 and 9, documents accelerometer scale, angular velocity, and wake-on-motion configuration. The existing driver returns mg and degrees/second; the sketch converts to m/s2 and rad/s. Acceleration includes gravity. A time-based 250 ms gravity filter separates fast shake acceleration; a smoothed magnitude must remain high for two seconds with at least six alternating swings. Short interruptions reset the sustained gesture. Recognition now runs in the existing ~333 Hz sampling task, so slower display frames do not alias a rapid shake. Sustained acceleration is used both for awake dizziness and wake confirmation.

The [QMI8658A Rev A datasheet](https://files.waveshare.com/wiki/common/QMI8658A_Datasheet_Rev_A.pdf), sections 5.10, 6, and 12, also explains the existing testing firmware's INT2 output-enable and CTRL9 acknowledgement steps. INT2 starts high-impedance until enabled; WoM requires disabling sensors, choosing low-power acceleration, writing threshold/interrupt selection, issuing CTRL9, and enabling acceleration. Reading STATUS1 clears the event. Exiting WoM requires a zero threshold and the command before normal readings resume. These existing board-specific steps are retained. WoM is a coarse alarm, not a shake classifier: ordinary worn motion can wake the CPU, but the display stays dark until the full gesture is confirmed.

The current wake path is restored from GitHub `origin/testing` commit `5952a15`: the CST816S stays in automatic standby and GPIO5 touch IRQ joins GPIO3 IMU INT2 in the EXT1 wake mask. A single touch wakes directly. Motion uses the testing branch's `ShakeWakeCheck` with its gravity estimate and acceleration/gyro strength. The requested hold is two seconds. To filter worn motion without a specific rotation axis, the wake check additionally requires six strong alternating acceleration strokes above 11 m/s2 and recent continued strokes. Simulated walking/running traces, isolated knocks, short/paused shakes and gyro-only swinging fail confirmation; strong sustained shakes succeed. Physical walking rejection has failed and needs the diagnostic measurements described above. The swivel-only classifier and periodic gyro polling were removed after they prevented practical waking.

An independent awake-session clock starts with the existing wake animation. Its closing script begins at 24.6 seconds and is protected from all input, finishing at the 30-second deadline even under continuous touch or motion. The separate 15-second idle deadline still applies sooner.

An accelerometer can observe tilt through gravity but cannot determine a pan around the gravity axis. The existing tilt follower is extended with bounded, deadbanded gyro integration for that component; it slowly recenters rather than claiming absolute heading. Clear tilt/pan uses the same lookAt springs as a finger. The existing gesture/animation scripts still take precedence while they perform.

The [ESP32-S3 sleep documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html) describes CPU power-down and EXT1 wake. Normal sleep retains IMU motion and touch IRQ wake with the backlight held low. There is no periodic gyro-polling wake. Startup goes straight into sleep. The closing performance starts early enough to be fully dark at the 15-second idle deadline or 30-second awake limit. Walking, gyro tremor, passive posture, and the creature's own animation do not refresh the idle deadline. A purposeful turn followed by settling still counts as activity, without extending the awake limit. Failed motion-interrupt setup stays dark and retries, rather than replaying a wake animation.

Host checks cover strong/light/brief/paused shakes, walking/running/jolts and pure rotation, touch tracking, cardinal tilt and upright pan, battery thresholds and hysteresis, tap anger ending, power timing, and random animation geometry. They are simulations; enclosure-specific sensitivity, real ADC readings, interrupt wake, and battery draw remain physical checks.

---

## Earlier reference and animation notes

# Research and animation direction

The star device in the reference photographs is **CREATURE's Starboy**. Its maker describes a wearable digital pet with animated eyes, an accelerometer, camera, microphone, and temperature sensor. In the maker's examples, shaking makes it dizzy and annoyed, cold makes it shiver, loud sound makes it anxious, and camera-recognized hand gestures trigger responses. Each unit has a distinct personality and eye set; the site advertises over 400 looks and device-to-device encounters. Source: [CREATURE product site](https://hesjustalittleguy.com/).

A reviewer who handled a demo unit for about 30 minutes directly observed the shake/dizzy, rude gesture/angry, freezer/shiver, and thumbs-up/battery reactions. That reviewer also reported that face and hand detection sometimes lagged or failed, and that communication between two Starboys was still a future feature in that demo. These are observations about the demonstrated unit, not guarantees about every production unit. Source: [Gizmodo hands-on](https://gizmodo.com/starboy-ai-keychain-is-pointless-extravagant-and-weirdly-lovable-2000740949).

The useful idea is that each event gets a short *performance*, not merely a new static picture. The official site does not publish its source artwork, timing curves, or firmware, so the pixel shapes and motion curves here are recreated for this device. The linked ESP32 board has a 240×240 GC9A01A LCD, CST816S touch controller, QMI8658 accelerometer/gyroscope with an **internal chip temperature reading**, and GPIO1 battery sensing. It does **not** have Starboy's camera, microphone, dedicated ambient thermometer, haptic motor, or its proprietary peer protocol. The chip temperature may be warmer than ambient. Sources: [Waveshare board documentation](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28), [QMI8658C datasheet](https://files.waveshare.com/wiki/common/QMI8658C_datasheet_rev_0.9.pdf).

## Original behavior for this build

| Input | Face performance | Design reason |
| --- | --- | --- |
| Power-on or wake | Eyes slowly open, glance left, glance right, then meet the viewer | A little moment of curiosity, instead of popping instantly to an expression |
| Quiet idle | Cardinal and diagonal glances with two eyes settling at slightly different speeds; irregular and occasional double blinks | Keeps a simple face feeling present without constant motion |
| Tilt | Eyes ease toward gravity and the eye pair leans a few pixels; a new orientation held still resets the 30-second timer | Makes the device feel aware of how it is held without treating walking as constant play |
| Tap | The nearer eye flinches first; both eyes spring open and settle at different rates | A playful “boop” made entirely with facial motion |
| Swipe | The eyes stretch and travel with the finger, then ease into a direction-specific expression; up briefly opens the eyes in proportion to estimated battery charge, down sadness, left shyness, right delight | Touch substitutes for unavailable camera gestures while preserving an eyes-only screen |
| Long press | Relaxed half-closed eyes and a tiny, slow sway | Feels like being petted |
| Two long presses | Switches persistent eye shape and color through a blink | A small collectible-look system on one board |
| Double / triple tap | Wide surprise / anxious darting | Several reactions from the same sensor |
| Double tap from sleep | The first tap wakes the CPU dark; a second tap or CST816S double-click report confirms the wake | A deliberate wake gesture that can be used while wearing the star |
| Three quick back-and-forth screen-plane twists | Wakes from deep sleep after a dark-screen gyro confirmation; awake or just woken, both eyes tumble in three slowing turns, then glare | One X or Y axis is sufficient; three qualified reversals avoid a one-off bump |
| Face-down pause | One eye narrows while the other stays open | Makes an unusual orientation feel noticed |
| Cold IMU die | Eyes shiver while the chip remains below 18 °C | Closest available temperature cue, with lag and self-heating limits |
| Idle | A stable chip-specific personality occasionally selects a small eye performance | Makes one unit feel consistent over time |
| Inactivity | Eyelids droop, complete a long closing blink, remain shut briefly, then the LCD and ESP32 sleep | A visible goodbye before power saving |

The animation is deliberately restrained on a 1.28-inch screen: only two big eyes and their gaze, shape, and motion are visible. Touch never draws ripples, glints, icons, or a battery bar. The default eye color is soft lilac, with six alternative palettes in the sketch. The eyes have anti-aliased contours for a smoother, vector-like look on the raster LCD. There is no mouth. The black background also blends with a black 3D-printed enclosure. There is no camera-based eye contact claim; looking forward creates that *impression* when the device is held up.

## Tuning on the physical enclosure

Waveshare's bundled QMI8658 driver is configured to report acceleration in **mg** and gyro speed in **degrees/s**. The sketch converts those readings to m/s² and rad/s before using its thresholds. It samples about every 25 ms and filters tilt to reduce jitter. A twist attempt uses either X or Y gyro axis: each sweep must reach roughly 0.25 radians at 1.2 rad/s or faster, with three qualified reversals and a final sweep within three seconds. A single bump should not count. The sleeping board begins measuring only after the ESP32 wakes from its coarse motion interrupt, so the person must continue twisting past the initial movement. Confirm touch axes and screen rotation on the assembled device. A thick cover or recessed bezel may make edge swipes harder for the CST816S to detect.

True sleep uses the [ESP32-S3 EXT1 wake input](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html) with the board's [GPIO3 IMU INT2 and GPIO5 touch IRQ](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28). The [QMI8658C revision 0.9 wake-on-motion procedure](https://files.waveshare.com/wiki/common/QMI8658C_datasheet_rev_0.9.pdf) uses low-power acceleration, a 90 mg threshold, INT2 idle high, and a CTRL9 configuration command. This board's IMU revision 0x7C reports command completion through STATUSINT bit 7 when CTRL8 bit 7 is set; the older bundled driver does not implement this handshake. Its hardware alert is only a first-stage trigger; the CPU keeps the display dark while checking the gyro twist. The [CST816S touch controller](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28/CST816S_Datasheet_EN.pdf) stays in automatic standby so touch can produce a wake pulse. Its [gesture register](https://files.waveshare.com/wiki/common/CST816S_register_declaration.pdf) has double-click recognition that the sketch now enables. A touch wake also remains dark until a second press is detected. Command acknowledgement and deep-sleep entry were checked on the connected board; physical two-press and twist wake sensitivity and battery current still need hands-on testing.
