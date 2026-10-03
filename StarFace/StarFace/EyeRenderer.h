#pragma once
#include <stdint.h>
#include "FaceConfig.h"

// Final geometry of one eye for one frame, in screen pixels. Everything is
// float, so shapes are computed with sub-pixel precision (smooth vector
// outlines that move and morph continuously); only the final per-pixel
// inside/outside test is rounded.
struct EyeFrame {
  float cx, cy;            // eye centre
  float hw, hh;            // half width / half height as drawn (after lids open or close)
  float radius;            // corner radius
  float tilt;              // rotation, radians: + lowers the inner (nose-side) end
  float bend;              // + both ends drop (a "^" arc), - they lift, px at the ends
  // Lids, in the eye's own frame (x toward the nose, y down, origin at the
  // centre): the upper lid hides everything above topA + topB*x + topC*x^2,
  // the lower lid everything below botA + botB*x + botC*x^2.
  float topA, topB, topC;
  float botA, botB, botC;
  float lidRound;          // px of rounding where a lid meets the outline
  float pupilX, pupilY;    // pupil centre relative to the eye centre, screen axes
  float pupilR;            // pupil radius
  float pupilSquash;       // vertical pupil scale (flattens as the eye closes)
  float heart;             // 0..1 pupil -> heart
  float spiral;            // 0..1 pupil -> spiral
  float spiralPhase;       // spiral rotation, radians
  bool rightEye;           // mirrors "toward the nose" for the right eye
};

// Sends the rectangle [x0, x1) x [y0, y1) of the framebuffer to the panel.
typedef void (*PushWindowFn)(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t *fb);

// RGB565 (byte-swapped for the panel's SPI order) of the three colours that
// can ever appear on screen.
constexpr uint16_t panel565(uint32_t rgb) {
  return uint16_t(((((rgb >> 19) & 0x1F) << 11) | (((rgb >> 10) & 0x3F) << 5) | ((rgb >> 3) & 0x1F)) << 8 |
                  ((((rgb >> 19) & 0x1F) << 11) | (((rgb >> 10) & 0x3F) << 5) | ((rgb >> 3) & 0x1F)) >> 8);
}
static constexpr uint16_t PIXEL_BACKGROUND = 0x0000;
static constexpr uint16_t PIXEL_EYE = panel565(EYE_COLOR);
static constexpr uint16_t PIXEL_ACCENT = panel565(ACCENT_COLOR);

// Draws the eyes with exactly two colours on pure black. Each eye is a
// signed-distance shape (a rounded box, bent and tilted, cut by curved lids
// with rounded corners); the pupil is a rounded square that can morph into a
// heart or a spinning spiral. Each pixel is tested at its centre and gets
// exactly PIXEL_BACKGROUND, PIXEL_EYE or PIXEL_ACCENT: no blending, no
// anti-aliased in-between colours. Frames are composed off-screen and only
// the changed rectangle is pushed, so there is no flicker.
class EyeRenderer {
 public:
  void begin(uint16_t *framebuffer, PushWindowFn pushFn);
  void invalidate() { fullRedraw = true; }
  void draw(const EyeFrame eyes[2]);
  // Draws into the framebuffer without pushing (host previews, screenshots).
  void compose(const EyeFrame eyes[2]);

 private:
  struct Box { int x0, y0, x1, y1; };
  uint16_t *fb = nullptr;
  PushWindowFn push = nullptr;
  bool fullRedraw = true;
  Box prev = {0, 0, -1, -1}, dirty = {0, 0, -1, -1};

  static Box bounds(const EyeFrame &e);
  void renderEye(const EyeFrame &e, const Box &clip);
};
