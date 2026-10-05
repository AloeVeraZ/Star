# Desktop preview

Builds the real `Eyes`, `EyeRenderer` and `CreatureAnimator` sources from the sketch on a
desktop (with a tiny Arduino shim in `stubs/`) and writes frames, so eye shapes
and animation timing can be checked without flashing the board.

```sh
./build.sh                      # needs g++
./preview sheet frames          # one settled frame per expression -> frames/*.ppm
./preview strip frames shake    # timed sequence: blink, morph, wake, sleep, shake, rattle, hold, surprised, angry, gaze
pip install pillow
python3 sheet.py out.png frames/*.ppm --cols 4   # contact sheet, cropped to the round panel
python3 make_previews.py frames                  # refresh the images used in the main README
```

`./run_tests.sh` runs four checks on the same sources: `motion_test` plays knocks (simulated at the sensor's real 1 kHz rate and filter), tilt flicks, rocking and twists through the touch-free gestures, against walking, running and shaking; `touch_test` plays
touch-controller report streams (taps, flicks, long holds that wander, lost
lift reports) through the sketch's finger tracker; `shake_test` feeds
simulated shakes, wrist flicks, walking, running and knocks through the
sketch's shake and twist detectors (awake, and the wake-from-sleep check) at
the slow sample rates the render loop allows; `soak_test` drives the animator,
eye system and renderer with hours of random input under address and undefined-behaviour
sanitizers.

Frames show exactly what the panel receives. Every frame is checked against the palette: each pixel must be a blend of black, `EYE_COLOR` and `PUPIL_COLOR` (anti-aliased edges and the glow), or the optional blush on black; the preview and the soak test fail if any other colour appears. `motion_test` also checks the world-following directions (tilt gaze, level roll, swing, spin, toss) and footstep detection (walking and running, but not shaking, rocking, twisting or knocks).
Timing on a PC says nothing about ESP32 speed; set `LOG_FPS` in `StarFace.ino`
to measure frame rate on the board.

Battery and power behavior is covered by `battery_test.cpp`: charge stages and recovery hysteresis, critical sleep recovery, the 15-second idle and unconditional 30-second display-off deadlines (including repeated interaction and millis wrap), triple/four-tap anger ending, and gyro-only upright pan. The restored testing-branch wake check accepts sustained shaking on any axis and rejects walking/running traces (including 20-second continuous checks), short/paused shakes, single knocks and gyro-only swinging. Single-touch wake is enabled as in the testing branch. The shake tests recognize at the same fast sampling interval as the sketch and deliver events at varied render rates.
# Directional motion wake checks

`left_right_wake_test.cpp` exercises the firmware's current `LeftRightWakeCheck`.
It preserves the user's deliberate left/right and carried forward/back records,
replays them at several assumed intervals/starting points, and checks sustained
wake, short/paused attempts, sample gaps and wrong-axis movement. The supplied
records have no timestamps; repeated replay is a classification regression,
not evidence of physical gesture duration or universal walking rejection.
The generic wake checks in `shake_test.cpp` retain the older testing baseline.
