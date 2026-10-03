#pragma once
#include <Arduino.h>
#include "FaceConfig.h"

static constexpr uint8_t EYE_LOOK_COUNT = 4;

// Final geometry of one eye for one frame. Everything is float so slow motion
// moves in sub-pixel steps instead of jumping a whole pixel at a time.
struct EyeGeom {
  float x, y;           // eye centre, px
  float rx, ry;         // half width / half height when fully open, px
  float open;           // vertical opening: 0 shut, 1 normal, >1 wide
  float pupilX, pupilY; // -1..1 inside the eye
  float iris;           // iris radius, px
  float lidAngle;       // upper-lid slope toward the nose (+ angry, - sad)
  float lidDrop;        // how far the black upper lid cuts in, px
  float lowerLid;       // happy-squint cut from below, px
  float bend;           // arcs the whole eye: + ends drop (happy ^), - ends lift (sleepy U), px
  float glow;           // halo strength multiplier, ~1
  float heat;           // 0..1 warms the colours toward red (anger)
  float blink;          // 0..1 blink closure: the iris squashes with the eye
  float spiral;         // 0..1 dizzy spiral replaces the iris and pupil
  float spiralPhase;    // spiral rotation, radians
};

float lookHalfWidth(uint8_t look);
float lookHalfHeight(uint8_t look);
float lookIris(uint8_t look);

// Vertical centre and half height actually drawn for an eye (after lids open/close).
// Shared with the animator so circle fitting matches what is rendered.
void eyeDrawnExtent(const EyeGeom &e, float &cy, float &h);

// Sends the rectangle [x0, x1) x [y0, y1) of the framebuffer to the panel.
typedef void (*PushWindowFn)(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t *fb);

// Procedural, anti-aliased eye renderer. Each eye is a signed-distance shape
// (ellipse intersected with curved lids), shaded per pixel with a gradient
// body, iris, pupil, highlights, lid shadow and an exponential glow. Coverage
// comes from the analytic distance, so edges look like vector art without
// supersampling. Frames are composed off-screen and only the changed window is
// pushed, so there is no flicker.
class EyeRenderer {
 public:
  void begin(uint16_t *framebuffer, PushWindowFn pushFn);
  void setPalette(uint8_t palette);
  void invalidate() { fullRedraw = true; }
  void draw(const EyeGeom eyes[2]);
  // Draws into the framebuffer without pushing (host previews, screenshots).
  void compose(const EyeGeom eyes[2]);

 private:
  struct Box { int x0, y0, x1, y1; };
  struct RGB { float r, g, b; };

  uint16_t *fb = nullptr;
  PushWindowFn push = nullptr;
  bool fullRedraw = true;
  Box prev = {0, 0, -1, -1}, dirty = {0, 0, -1, -1};
  RGB base = {195, 150, 255};
  uint8_t glowLut[int(GLOW_EXTENT * 4) + 2];

  Box bounds(const EyeGeom &e) const;
  void renderEye(const EyeGeom &e, bool leftEye, const Box &clip);
};
