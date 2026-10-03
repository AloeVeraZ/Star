#pragma once
#include <Arduino.h>

static constexpr int EYE_BASE_HALF_WIDTH = 37;
static constexpr int EYE_BASE_HALF_HEIGHT = 48;
static constexpr uint8_t EYE_LOOK_COUNT = 4;
static constexpr uint8_t EYE_PALETTE_COUNT = 7;

// Final geometry of one eye for one frame. Everything is float so slow motion
// moves in sub-pixel steps instead of jumping a whole pixel at a time.
struct EyeGeom {
  float x, y;           // eye center, px
  float rx, ry;         // half width / half height when fully open, px
  float open;           // vertical opening: 0 shut, 1 normal, >1 wide
  float pupilX, pupilY; // -1..1 inside the eye
  float iris;           // iris half width, px
  float lidAngle;       // upper-lid slope toward the nose (+ angry, - sad)
  float lidDrop;        // how far the black upper lid cuts in, px
  float lowerLid;       // happy-squint cut from below, px
};

float lookHalfWidth(uint8_t look);
float lookHalfHeight(uint8_t look);
float lookIris(uint8_t look);

class EyeRenderer {
 public:
  void begin(uint16_t *framebuffer) { fb = framebuffer; fullRedraw = true; }
  void setPalette(uint8_t palette);
  void invalidate() { fullRedraw = true; }
  // Clears only the region the eyes covered last frame or cover now, draws
  // both eyes and pushes just that window to the LCD.
  void draw(const EyeGeom eyes[2]);

 private:
  struct Box { int x0, y0, x1, y1; };
  struct Clip { float cx, cy, rx, ry; };

  uint16_t *fb = nullptr;
  bool fullRedraw = true;
  Box prev = {0, 0, -1, -1};
  uint16_t halo = 0, glow = 0, soft = 0, white = 0, rim = 0, pupil = 0, lowGlow = 0;

  Box bounds(const EyeGeom &e) const;
  void drawGlow(const EyeGeom &e);
  void drawCore(const EyeGeom &e);
  void drawLids(const EyeGeom &e, bool leftEye);
  void fillEllipse(float cx, float cy, float rx, float ry, uint16_t c,
                   const Clip *clip = nullptr);
  void blendPixel(int x, int y, uint16_t c, float coverage);
};
