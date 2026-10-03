#include "EyeRenderer.h"
#include <math.h>
#include <string.h>
#include "AnimMath.h"
#include "LCD_1in28.h"

using anim::clamp01;
using anim::smoothstep;

static constexpr int SW = 240, SH = 240;

// The display buffer is byte-swapped because Waveshare's SPI routine sends raw bytes.
static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  return (c << 8) | (c >> 8);
}

static uint16_t blend565(uint16_t dst, uint16_t src, uint8_t alpha) {
  uint16_t a = (dst << 8) | (dst >> 8);
  uint16_t b = (src << 8) | (src >> 8);
  int r = (((a >> 11) & 31) * (255 - alpha) + ((b >> 11) & 31) * alpha) / 255;
  int g = (((a >> 5) & 63) * (255 - alpha) + ((b >> 5) & 63) * alpha) / 255;
  int bl = ((a & 31) * (255 - alpha) + (b & 31) * alpha) / 255;
  uint16_t c = (r << 11) | (g << 5) | bl;
  return (c << 8) | (c >> 8);
}

static const uint16_t BLACK = 0;
static const uint16_t SPARK = rgb(246, 255, 255);

static const float LOOK_W[EYE_LOOK_COUNT] = {
  EYE_BASE_HALF_WIDTH, EYE_BASE_HALF_WIDTH + 3, EYE_BASE_HALF_WIDTH - 5, EYE_BASE_HALF_WIDTH + 1
};
static const float LOOK_H[EYE_LOOK_COUNT] = {
  EYE_BASE_HALF_HEIGHT, EYE_BASE_HALF_HEIGHT - 5, EYE_BASE_HALF_HEIGHT + 2, EYE_BASE_HALF_HEIGHT - 9
};
static const float LOOK_IRIS[EYE_LOOK_COUNT] = {17, 17, 14, 17};

float lookHalfWidth(uint8_t look) { return LOOK_W[look % EYE_LOOK_COUNT]; }
float lookHalfHeight(uint8_t look) { return LOOK_H[look % EYE_LOOK_COUNT]; }
float lookIris(uint8_t look) { return LOOK_IRIS[look % EYE_LOOK_COUNT]; }

void EyeRenderer::setPalette(uint8_t p) {
  static const uint8_t colors[EYE_PALETTE_COUNT][3] = {
    {195, 150, 255}, // soft lilac, default
    {255, 135, 196}, // pink
    {80, 221, 232},  // cyan
    {105, 158, 255}, // blue
    {255, 94, 109},  // red
    {225, 235, 255}, // white
    {255, 204, 96}   // yellow
  };
  p %= EYE_PALETTE_COUNT;
  uint8_t r = colors[p][0], g = colors[p][1], b = colors[p][2];
  white = rgb(min(255, int(r) + 130), min(255, int(g) + 42), min(255, int(b) + 40));
  soft = rgb(r, g, b);
  rim = rgb(r / 3, g / 2, b / 2);
  pupil = rgb(max(5, r / 8), max(12, g / 3), max(12, b / 3));
  glow = rgb(r / 7, g / 5, b / 5);
  halo = rgb(r / 12, g / 8, b / 8);
  lowGlow = blend565(white, soft, 80);
}

void EyeRenderer::blendPixel(int x, int y, uint16_t c, float coverage) {
  if ((unsigned)x >= SW || (unsigned)y >= SH || coverage <= 0.0f) return;
  uint16_t &dst = fb[y * SW + x];
  if (coverage >= .998f) dst = c;
  else dst = blend565(dst, c, uint8_t(coverage * 255.0f + .5f));
}

// Horizontal extent of an ellipse on row y. [lo, hi] holds every pixel within
// about a pixel of the shape; [ilo, ihi] holds pixels whose 3x3 neighbourhood
// is fully inside (drawn solid, no math). Returns false if the row misses.
static bool ellipseRow(float cx, float cy, float rx, float ry, int y,
                       float &lo, float &hi, float &ilo, float &ihi) {
  float dy = fabsf(y - cy);
  float nearRow = dy - 1.0f;
  if (nearRow >= ry) return false;
  float n = nearRow <= 0 ? 0 : nearRow / ry;
  float outer = rx * sqrtf(1.0f - n * n) + 1.0f;
  lo = cx - outer;
  hi = cx + outer;
  float farRow = (dy + 1.0f) / ry;
  if (farRow < 1.0f) {
    float inner = rx * sqrtf(1.0f - farRow * farRow) - 1.0f;
    ilo = cx - inner;
    ihi = cx + inner;
  } else {
    ilo = cx;
    ihi = cx - 1.0f;
  }
  return true;
}

// Anti-aliasing coverage from an approximate signed pixel distance to the edge.
static float ellipseCoverage(float x, float y, float cx, float cy, float rx, float ry) {
  float nx = (x - cx) / rx, ny = (y - cy) / ry;
  float q = nx * nx + ny * ny;
  float g = 2.0f * sqrtf(nx * nx / (rx * rx) + ny * ny / (ry * ry));
  if (g < 1e-5f) return 1.0f;
  return clamp01((1.0f - q) / g + .5f);
}

void EyeRenderer::fillEllipse(float cx, float cy, float rx, float ry, uint16_t c,
                              const Clip *clip) {
  if (rx < .5f || ry < .5f) return;
  int y0 = max(0, int(ceilf(cy - ry - 1.0f)));
  int y1 = min(SH - 1, int(floorf(cy + ry + 1.0f)));
  for (int y = y0; y <= y1; ++y) {
    float lo, hi, ilo, ihi;
    if (!ellipseRow(cx, cy, rx, ry, y, lo, hi, ilo, ihi)) continue;
    if (clip) {
      float clo, chi, cilo, cihi;
      if (!ellipseRow(clip->cx, clip->cy, clip->rx, clip->ry, y, clo, chi, cilo, cihi)) continue;
      lo = fmaxf(lo, clo); hi = fminf(hi, chi);
      ilo = fmaxf(ilo, cilo); ihi = fminf(ihi, cihi);
    }
    int xa = max(0, int(ceilf(lo))), xb = min(SW - 1, int(floorf(hi)));
    if (xa > xb) continue;
    int ia = max(xa, int(ceilf(ilo))), ib = min(xb, int(floorf(ihi)));
    if (ia > ib) { ia = xb + 1; ib = xb; }
    uint16_t *row = fb + y * SW;
    for (int x = xa; x <= xb; ++x) {
      if (x == ia) {
        for (; x <= ib; ++x) row[x] = c;
        if (x > xb) break;
      }
      float cov = ellipseCoverage(x, y, cx, cy, rx, ry);
      if (clip && cov > 0) cov *= ellipseCoverage(x, y, clip->cx, clip->cy, clip->rx, clip->ry);
      blendPixel(x, y, c, cov);
    }
  }
}

// Eye height in pixels after the lid opening is applied.
static float openHeight(const EyeGeom &e) {
  return fmaxf(1.2f, fmaxf(6.0f, e.ry) * anim::clampf(e.open, 0.0f, 1.3f));
}

EyeRenderer::Box EyeRenderer::bounds(const EyeGeom &e) const {
  float rx = fmaxf(6.0f, e.rx), h = openHeight(e);
  return {int(floorf(e.x - rx - 11)), int(floorf(e.y - h - 12)),
          int(ceilf(e.x + rx + 11)), int(ceilf(e.y + h + 12))};
}

void EyeRenderer::drawGlow(const EyeGeom &e) {
  float rx = fmaxf(6.0f, e.rx), h = openHeight(e);
  // While the eye is nearly shut the glow hugs the line instead of popping off.
  float t = smoothstep(2.0f, 16.0f, h);
  fillEllipse(e.x, e.y, rx + 8, h + 1.5f + 7.5f * t, halo);
  fillEllipse(e.x, e.y, rx + 5, h + 1.0f + 4.0f * t, glow);
  fillEllipse(e.x, e.y, rx + 2, h + .6f + 1.4f * t, soft);
}

void EyeRenderer::drawCore(const EyeGeom &e) {
  float rx = fmaxf(6.0f, e.rx), h = openHeight(e);
  float t = smoothstep(2.0f, 16.0f, h);
  float cx = e.x, cy = e.y;
  uint16_t core = t >= 1.0f ? white : blend565(soft, white, uint8_t(90 + 165 * t));
  fillEllipse(cx, cy, rx, h, core);
  if (h <= 4.0f) return;
  // A subtle lower glow gives the eyes volume without a visible pixel border.
  fillEllipse(cx, cy + fmaxf(1.0f, h / 3), rx - 4, fmaxf(2.0f, h / 2), lowGlow);
  fillEllipse(cx, cy - 3.0f * t, rx - 3, fmaxf(h * .5f, h - 7), core);
  // The iris fades in as the lids part and is always kept inside the white.
  float show = smoothstep(5.0f, 20.0f, h);
  float iris = e.iris * show;
  if (iris < 1.5f) return;
  Clip clip = {cx, cy, rx - 1.5f, h - 1.5f};
  float px = cx + anim::clampf(e.pupilX, -1.15f, 1.15f) * 12.0f;
  float py = cy + anim::clampf(e.pupilY, -1.15f, 1.15f) * fminf(11.0f, h / 3);
  fillEllipse(px, py + 4, iris, fminf(20.0f, h - 2), rim, &clip);
  fillEllipse(px, py + 4, fmaxf(5.0f * show, iris - 4), fminf(17.0f, h - 3), pupil, &clip);
  fillEllipse(px - 5, py - 5, 4.0f * show, fminf(6.0f, h / 3), SPARK, &clip);
  fillEllipse(px + 6, py + 11, 2.0f * show, 2.0f * show, white, &clip);
}

// Black curved lids belong to the eyes in every expression. They only paint
// over this eye's own glow, so a neighbouring eye is never cut.
void EyeRenderer::drawLids(const EyeGeom &e, bool leftEye) {
  float rx = fmaxf(6.0f, e.rx), ryFull = fmaxf(6.0f, e.ry), h = openHeight(e);
  float t = smoothstep(2.0f, 16.0f, h);
  float k = smoothstep(2.0f, .4f * ryFull, h); // lids flatten out as the eye shuts
  float drop = e.lidDrop * k, angle = e.lidAngle * k, lower = fmaxf(0.0f, e.lowerLid) * k;
  float haloRx = rx + 9.0f, haloRy = h + 2.5f + 7.5f * t;
  int x0 = max(0, int(ceilf(e.x - haloRx))), x1 = min(SW - 1, int(floorf(e.x + haloRx)));
  for (int x = x0; x <= x1; ++x) {
    float hx = (x - e.x) / haloRx;
    if (hx * hx >= 1.0f) continue;
    float extent = haloRy * sqrtf(1.0f - hx * hx);
    float side = (x - e.x) / rx;
    float sideC = anim::clampf(side, -1.2f, 1.2f);
    float inner = leftEye ? sideC : -sideC;
    float edge = e.y - h - 9.0f * (1.0f - k) + drop + inner * angle * rx
                 + 2.0f * k * sideC * sideC;
    int yEnd = min(SH - 1, int(floorf(edge + .5f)));
    for (int y = max(0, int(floorf(e.y - extent))); y <= yEnd; ++y)
      blendPixel(x, y, BLACK, clamp01(edge - y + .5f));
    if (lower > .3f) {
      float bottom = e.y + h + 2.0f - lower * (1.0f - .9f * sideC * sideC);
      int yStop = min(SH - 1, int(ceilf(e.y + extent)));
      for (int y = max(0, int(floorf(bottom - .5f))); y <= yStop; ++y)
        blendPixel(x, y, BLACK, clamp01(y - bottom + .5f));
    }
  }
}

void EyeRenderer::draw(const EyeGeom eyes[2]) {
  if (!fb) return;
  Box a = bounds(eyes[0]), b = bounds(eyes[1]);
  Box cur = {max(0, min(a.x0, b.x0)), max(0, min(a.y0, b.y0)),
             min(SW - 1, max(a.x1, b.x1)), min(SH - 1, max(a.y1, b.y1))};
  Box d = cur;
  if (fullRedraw) d = {0, 0, SW - 1, SH - 1};
  else if (prev.x1 >= prev.x0) {
    d.x0 = min(d.x0, prev.x0); d.y0 = min(d.y0, prev.y0);
    d.x1 = max(d.x1, prev.x1); d.y1 = max(d.y1, prev.y1);
  }
  if (d.x1 < d.x0 || d.y1 < d.y0) return;
  for (int y = d.y0; y <= d.y1; ++y)
    memset(fb + y * SW + d.x0, 0, (d.x1 - d.x0 + 1) * sizeof(uint16_t));
  // Glows first, then the bright cores, then lids, so overlapping eyes layer cleanly.
  drawGlow(eyes[0]); drawGlow(eyes[1]);
  drawCore(eyes[0]); drawCore(eyes[1]);
  drawLids(eyes[0], true); drawLids(eyes[1], false);
  LCD_1IN28_DisplayWindows(d.x0, d.y0, d.x1 + 1, d.y1 + 1, fb);
  prev = cur;
  fullRedraw = false;
}
