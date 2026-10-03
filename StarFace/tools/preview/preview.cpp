// Desktop preview of the Star Face eyes. Builds the real Eyes, EyeRenderer
// and CreatureAnimator sources and writes frames as binary PPM images.
//
//   ./preview sheet  OUTDIR        one settled frame per expression and mood
//   ./preview strip  OUTDIR NAME   a timed sequence for one reaction
//                                  (blink wake sleep shake rattle hold surprised
//                                   angry gaze morph)
//
// Every frame is checked: it may only contain black and the two eye colours.
// See README.md next to this file.
#include <stdio.h>
#include <string>
#include "EyeRenderer.h"
#include "Eyes.h"
#include "CreatureAnimator.h"

static uint16_t fb[SCREEN_WIDTH * SCREEN_HEIGHT];
static int badFrames = 0;

static void writePPM(const std::string &path) {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) { perror(path.c_str()); exit(1); }
  fprintf(f, "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
  int bad = 0;
  for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
    if (fb[i] != PIXEL_BACKGROUND && fb[i] != PIXEL_EYE && fb[i] != PIXEL_ACCENT) ++bad;
    uint16_t c = uint16_t((fb[i] << 8) | (fb[i] >> 8));
    uint8_t rgb[3] = {uint8_t(((c >> 11) & 31) * 255 / 31), uint8_t(((c >> 5) & 63) * 255 / 63),
                      uint8_t((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  if (bad) { fprintf(stderr, "%s: %d pixels outside the two-colour palette\n", path.c_str(), bad); ++badFrames; }
}

struct Sim {
  EyeRenderer r;
  Eyes eyes;
  CreatureAnimator c;
  uint32_t now = 0;
  Sim() {
    previewRandomState() = 0x9E3779B9u;
    r.begin(fb, nullptr);
    eyes.begin(&r, 0x5A17C3u, now);
    c.begin(&eyes, 0x5A17C3u, now);
    c.setIdleActsAllowed(false);
  }
  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 16) { now += 16; c.update(.016f, now); }
  }
  void wake() { c.startWake(now, false); run(4200); }
  void shot(const std::string &path) {
    r.invalidate();
    r.compose(eyes.frames());
    writePPM(path);
  }
};

// The eye system on its own, driven only through its public API.
struct Bare {
  EyeRenderer r;
  Eyes eyes;
  uint32_t now = 0;
  Bare() {
    previewRandomState() = 0x9E3779B9u;
    r.begin(fb, nullptr);
    eyes.begin(&r, 0x5A17C3u, now);
    eyes.setIdle(false);
    eyes.setAutoBlink(false);
  }
  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 16) { now += 16; eyes.update(.016f, now); }
  }
  void shot(const std::string &path) {
    r.invalidate();
    r.compose(eyes.frames());
    writePPM(path);
  }
};

struct Shot { const char *name; Mood mood; uint32_t duration, at; };

static int finish() {
  if (badFrames) fprintf(stderr, "%d frame(s) broke the two-colour rule\n", badFrames);
  return badFrames ? 1 : 0;
}

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: preview sheet|strip OUTDIR [NAME]\n"); return 1; }
  std::string mode = argv[1], out = argv[2];
  if (mode == "sheet") {
    // Every expression, through setExpression().
    for (int e = 0; e < EXPRESSION_COUNT; ++e) {
      Bare b;
      b.eyes.setExpression(Expression(e));
      b.run(1500);
      b.shot(out + "/expr_" + expressionName(Expression(e)) + ".ppm");
    }
    { Bare b; b.eyes.lookAt(1, 0); b.run(1500); b.eyes.lookAt(1, 0); b.shot(out + "/look_right.ppm"); }
    { Bare b; b.eyes.lookAt(-.8f, -.8f); b.run(1500); b.eyes.lookAt(-.8f, -.8f); b.shot(out + "/look_up_left.ppm"); }
    { Bare b; b.eyes.lookAt(0, 1); b.run(1500); b.eyes.lookAt(0, 1); b.shot(out + "/look_down.ppm"); }
    // Moods of the creature (expression plus gaze and effects).
    const Shot shots[] = {
      {"idle", Mood::IDLE, 0, 0},           {"happy", Mood::HAPPY, 2400, 700},
      {"sad", Mood::SAD, 3000, 1400},       {"angry", Mood::ANGRY, 2400, 900},
      {"surprised", Mood::SURPRISED, 1600, 260}, {"dizzy", Mood::DIZZY, DIZZY_ANIM_MS, 1050},
      {"shy", Mood::SHY, 2000, 900},        {"petted", Mood::PETTED, 2000, 900},
      {"confused", Mood::CONFUSED, 1600, 800}, {"loved", Mood::LOVED, 1800, 900},
    };
    for (const Shot &s : shots) {
      Sim sim;
      sim.wake();
      if (s.mood != Mood::IDLE) { sim.c.react(s.mood, sim.now, s.duration); sim.run(s.at); }
      else sim.run(400);
      sim.shot(out + "/" + s.name + ".ppm");
    }
    { Sim sim; sim.wake(); sim.c.startSleep(sim.now, .3f); sim.run(SLEEP_SEQUENCE_MS + 1200);
      sim.shot(out + "/sleeping.ppm"); }
    { Sim sim; sim.wake(); sim.c.react(Mood::ANGRY, sim.now, ANGRY_ANIM_MS); sim.run(ANGRY_ANIM_MS + 900);
      sim.shot(out + "/grumpy_after.ppm"); }
    // Held upside down: worried, then cross, then furious.
    for (int ms : {800, 2600, 5000}) {
      Sim sim; sim.wake(); sim.c.setTilt(0, -.9f); sim.c.setUpsideDown(true, sim.now);
      sim.c.react(Mood::UPSIDE_DOWN, sim.now, 600); sim.run(ms);
      sim.shot(out + "/upside_down_" + std::to_string(ms) + ".ppm");
    }
    return finish();
  }
  if (mode == "strip" && argc >= 4) {
    std::string name = argv[3];
    if (name == "morph") {
      // setExpression() from one expression to the next, every 100 ms.
      Bare b;
      const Expression seq[] = {NEUTRAL, HAPPY, ANGRY, SURPRISED, SLEEPY, CURIOUS};
      int frame = 0;
      for (Expression e : seq) {
        b.eyes.setExpression(e);
        for (int k = 0; k < 6; ++k) {
          char buf[64];
          snprintf(buf, sizeof buf, "/morph_%02d.ppm", frame++);
          b.shot(out + buf);
          b.run(100);
        }
      }
      return finish();
    }
    if (name == "blink") {
      Bare b;
      b.run(500);
      b.eyes.blink();
      for (int frame = 0; frame < 20; ++frame) {
        char buf[64];
        snprintf(buf, sizeof buf, "/blink_%02d.ppm", frame);
        b.shot(out + buf);
        b.run(16);
      }
      return finish();
    }
    Sim sim;
    sim.wake();
    uint32_t total = 1200, step = 66;
    if (name == "wake") { sim.c.startSleep(sim.now, 0); sim.run(SLEEP_SEQUENCE_MS + 500);
                          sim.c.startWake(sim.now, false); total = 3600; step = 200; }
    else if (name == "sleep") { sim.c.startSleep(sim.now, .3f); total = SLEEP_SEQUENCE_MS; step = 300; }
    else if (name == "shake") { sim.c.impact(14, 4); sim.c.react(Mood::DIZZY, sim.now, DIZZY_ANIM_MS);
                                total = DIZZY_ANIM_MS + ANGRY_ANIM_MS + 2000; step = 250; }
    else if (name == "rattle") {
      // A hand shake as the sketch reports it: strokes every ~128 ms that
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
            if (stroke == 1) sim.c.react(Mood::SURPRISED, sim.now, 650);
            if (stroke == 3) sim.c.react(Mood::DIZZY, sim.now, DIZZY_ANIM_MS);
          }
        }
        if (ms % 112 == 0) {
          char buf[64];
          snprintf(buf, sizeof buf, "/rattle_%02d.ppm", frame++);
          sim.shot(out + buf);
        }
        sim.run(16);
      }
      return finish();
    }
    else if (name == "hold") {
      // A finger held on the screen for 6 s, moving a little, then let go.
      int frame = 0;
      sim.c.setPointer(.3f, .1f, sim.now);
      sim.c.setPointerHeld(true, sim.now);
      sim.c.react(Mood::FOLLOWING, sim.now, 600);
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
      return finish();
    }
    else if (name == "surprised") { sim.c.react(Mood::SURPRISED, sim.now, 1600); total = 900; step = 50; }
    else if (name == "angry") { sim.c.react(Mood::ANGRY, sim.now, ANGRY_ANIM_MS); total = 1000; step = 60; }
    else if (name == "gaze") { sim.c.setPointer(1, 0, sim.now); sim.c.setPointerHeld(true, sim.now);
                               sim.c.react(Mood::FOLLOWING, sim.now, 3000); total = 700; step = 50; }
    int frame = 0;
    for (uint32_t t = 0; t <= total; t += step) {
      char buf[64];
      snprintf(buf, sizeof buf, "/%s_%02d.ppm", name.c_str(), frame++);
      sim.shot(out + buf);
      sim.run(step);
    }
    return finish();
  }
  fprintf(stderr, "unknown mode\n");
  return 1;
}
