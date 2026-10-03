# Desktop preview

Builds the real `EyeRenderer` and `CreatureAnimator` sources from the sketch on a
desktop (with a tiny Arduino shim in `stubs/`) and writes frames, so eye shapes
and animation timing can be checked without flashing the board.

```sh
./build.sh                      # needs g++
./preview sheet frames          # one settled frame per expression -> frames/*.ppm
./preview strip frames shake    # timed sequence: blink, wake, sleep, shake, rattle, surprised, angry, gaze
pip install pillow
python3 sheet.py out.png frames/*.ppm --cols 4   # contact sheet, cropped to the round panel
python3 make_previews.py frames                  # refresh the images used in the main README
```

Frames are 240 x 240 and show exactly what the panel receives (RGB565, dithered).
Timing on a PC says nothing about ESP32 speed; set `LOG_FPS` in `StarFace.ino`
to measure frame rate on the board.
