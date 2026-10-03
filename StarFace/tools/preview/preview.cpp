// Desktop preview of the Star Face eyes. Builds the real EyeRenderer and
// CreatureAnimator sources and writes frames as binary PPM images.
//
//   ./preview sheet  OUTDIR        one settled frame per expression
//   ./preview strip  OUTDIR NAME   a timed sequence for one reaction
//                                  (blink wake sleep shake rattle hold surprised angry gaze)
//
// See README.md next to this file.
#include <stdio.h>
#include <string>
#include <vector>
#include "EyeRenderer.h"
#include "CreatureAnimator.h"

static uint16_t fb[240 * 240];

static void writePPM(const std::string &path) {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) { perror(path.c_str()); exit(1); }
  fprintf(f, "P6\n240 240\n255\n");
  for (int i = 0; i < 240 * 240; ++i) {
    uint16_t c = uint16_t((fb[i] << 8) | (fb[i] >> 8));
    uint8_t rgb[3] = {uint8_t(((c >> 11) & 31) * 255 / 31), uint8_t(((c >> 5) & 63) * 255 / 63),
                      uint8_t((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

struct Sim {
  EyeRenderer r;
  CreatureAnimator c;
  uint32_t now = 0;
  explicit Sim(uint8_t look = DEFAULT_EYE_STYLE) {
    previewRandomState() = 0x9E3779B9u;
    r.begin(fb, nullptr);
    c.begin(&r, look, 0x5A17C3u, now);
    c.setIdleActsAllowed(false);
  }
  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 16) { now += 16; c.update(.016f, now); }
  }
  void wake() { c.startWake(now, false); run(4200); }
  void shot(const std::string &path) {
    r.invalidate();
    r.compose(c.eyes());
    writePPM(path);
  }
};

struct Shot { const char *name; Mood mood; uint32_t duration, at; };

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: preview sheet|strip OUTDIR [NAME]\n"); return 1; }
  std::string mode = argv[1], out = argv[2];
  if (mode == "sheet") {
    const Shot shots[] = {
      {"idle", IDLE, 0, 0},           {"happy", HAPPY, 2400, 700},
      {"sad", SAD, 3000, 1400},       {"angry", ANGRY, 2400, 900},
      {"surprised", SURPRISED, 1600, 260}, {"dizzy", DIZZY, DIZZY_ANIM_MS, 1050},
      {"shy", SHY, 2000, 900},        {"petted", PETTED, 2000, 900},
      {"confused", CONFUSED, 1600, 800},
    };
    for (const Shot &s : shots) {
      Sim sim;
      sim.wake();
      if (s.mood != IDLE) { sim.c.react(s.mood, sim.now, s.duration); sim.run(s.at); }
      else sim.run(400);
      sim.shot(out + "/" + s.name + ".ppm");
    }
    { Sim sim; sim.wake(); sim.c.setPointer(1, -.2f, sim.now); sim.c.setPointerHeld(true, sim.now);
      sim.c.react(FOLLOWING, sim.now, 3000); sim.run(900); sim.shot(out + "/look_right.ppm"); }
    { Sim sim; sim.wake(); sim.c.setTilt(-.9f, .9f); sim.run(1600); sim.shot(out + "/look_down_left.ppm"); }
    { Sim sim; sim.wake(); sim.c.startSleep(sim.now, .3f); sim.run(SLEEP_SEQUENCE_MS + 1200);
      sim.shot(out + "/sleeping.ppm"); }
    { Sim sim; sim.wake(); sim.c.react(ANGRY, sim.now, ANGRY_ANIM_MS); sim.run(ANGRY_ANIM_MS + 900);
      sim.shot(out + "/grumpy_after.ppm"); }
    for (int look = 0; look < EYE_LOOK_COUNT; ++look) {
      Sim sim(look); sim.wake(); sim.run(300);
      static const char *names[] = {"bean", "dot", "blip", "cat"};
      sim.shot(out + "/style_" + names[look] + ".ppm");
      sim.c.react(SURPRISED, sim.now, 1600); sim.run(300);
      sim.shot(out + "/style_" + names[look] + "_surprised.ppm");
      sim.c.react(HAPPY, sim.now, 2400); sim.run(700);
      sim.shot(out + "/style_" + names[look] + "_happy.ppm");
    }
    return 0;
  }
  if (mode == "strip" && argc >= 4) {
    std::string name = argv[3];
    Sim sim;
    sim.wake();
    uint32_t total = 1200, step = 66;
    if (name == "blink") { sim.c.react(IDLE, sim.now, 0); total = 330; step = 16; }
    else if (name == "wake") { sim.c.startSleep(sim.now, 0); sim.run(SLEEP_SEQUENCE_MS + 500);
                               sim.c.startWake(sim.now, false); total = 3600; step = 200; }
    else if (name == "sleep") { sim.c.startSleep(sim.now, .3f); total = SLEEP_SEQUENCE_MS; step = 300; }
    else if (name == "shake") { sim.c.impact(14, 4); sim.c.react(DIZZY, sim.now, DIZZY_ANIM_MS);
                                total = DIZZY_ANIM_MS + ANGRY_ANIM_MS + 2000; step = 250; }
    else if (name == "rattle") {
      // A hand shake as the sketch reports it: strokes every ~120 ms that
      // reverse direction, a live shake level, and the dizzy spell after the
      // third stroke.
      int frame = 0;
      for (int ms = 0; ms <= 2600; ms += 16) {
        if (ms < 900) {
          sim.c.shake(1.0f);
          if (ms % 128 == 0) {
            float s = (ms / 128) % 2 ? -1.0f : 1.0f;
            sim.c.impact(14 * s, 3 * s);
            int stroke = ms / 128 + 1;
            if (stroke == 1) sim.c.react(SURPRISED, sim.now, 650);
            if (stroke == 3) sim.c.react(DIZZY, sim.now, DIZZY_ANIM_MS);
          }
        }
        if (ms % 112 == 0) {
          char buf[64];
          snprintf(buf, sizeof buf, "/rattle_%02d.ppm", frame++);
          sim.shot(out + buf);
        }
        sim.run(16);
      }
      return 0;
    }
    else if (name == "hold") {
      // A finger held on the screen for 6 s, moving a little, then let go.
      int frame = 0;
      sim.c.setPointer(.3f, .1f, sim.now);
      sim.c.setPointerHeld(true, sim.now);
      sim.c.react(FOLLOWING, sim.now, 600);
      for (int ms = 0; ms <= 9000; ms += 16) {
        if (ms < 6000) sim.c.setPointer(.3f + .4f * sinf(ms * .001f), .1f, sim.now);
        if (ms == 6000) { sim.c.huff(sim.c.annoyance(), sim.now); sim.c.setPointerHeld(false, sim.now); }
        if (ms % 752 == 0) {
          char buf[64];
          snprintf(buf, sizeof buf, "/hold_%02d.ppm", frame++);
          sim.shot(out + buf);
        }
        sim.run(16);
      }
      return 0;
    }
    else if (name == "surprised") { sim.c.react(SURPRISED, sim.now, 1600); total = 900; step = 50; }
    else if (name == "angry") { sim.c.react(ANGRY, sim.now, ANGRY_ANIM_MS); total = 1000; step = 60; }
    else if (name == "gaze") { sim.c.setPointer(1, 0, sim.now); sim.c.setPointerHeld(true, sim.now);
                               sim.c.react(FOLLOWING, sim.now, 3000); total = 700; step = 50; }
    if (name == "blink") {
      // Force a blink right now by advancing to the next scheduled one.
      for (int i = 0; i < 600; ++i) { sim.run(16); if (sim.c.eyes()[0].open < .9f) break; }
    }
    int frame = 0;
    for (uint32_t t = 0; t <= total; t += step) {
      char buf[64];
      snprintf(buf, sizeof buf, "/%s_%02d.ppm", name.c_str(), frame++);
      sim.shot(out + buf);
      sim.run(step);
    }
    return 0;
  }
  fprintf(stderr, "unknown mode\n");
  return 1;
}
