// The per-pixel loop is the hot path of the sketch; -O2 roughly halves its
// cost compared with the Arduino default -Os.
#pragma GCC optimize("O2")
#include "EyeRenderer.h"
#include <math.h>
#include <string.h>

static constexpr int SW = SCREEN_WIDTH, SH = SCREEN_HEIGHT;

static constexpr float SPIRAL_TURNS = 2.4f;       // arms of the dizzy spiral across its radius
static constexpr float SPIRAL_HALF_WIDTH = 0.24f; // arm half width, in turns
static constexpr float TAU = 6.28318531f;

// ---- Fast math for the per-pixel loop. Float division and sqrtf are slow
// library sequences on the ESP32, so the loop uses only these. ----
static inline float rsqrt(float x) {
  union { float f; uint32_t i; } u;
  u.f = x;
  u.i = 0x5f375a86u - (u.i >> 1);
  float y = u.f;
  y = y * (1.5f - .5f * x * y * y);
  return y * (1.5f - .5f * x * y * y);
}
static inline float fsqrt(float x) { return x > 1e-12f ? x * rsqrt(x) : 0.0f; }
static inline float fmax2(float a, float b) { return a > b ? a : b; }
static inline float fmin2(float a, float b) { return a < b ? a : b; }
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
// atan2 to ~0.005 rad.
static inline float fastAtan2(float y, float x) {
  float ax = fabsf(x), ay = fabsf(y);
  float mx = fmax2(ax, ay), mn = fmin2(ax, ay);
  if (mx < 1e-6f) return 0.0f;
  float a = mn * rsqrt(mx * mx), s = a * a;
  float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
  if (ay > ax) r = 1.57079637f - r;
  if (x < 0) r = 3.14159274f - r;
  return y < 0 ? -r : r;
}
// Smooth maximum: max(a, b) with the corner rounded over k px (k > 0).
static inline float smax(float a, float b, float k, float invK) {
  float h = fmax2(k - fabsf(a - b), 0.0f) * invK;
  return fmax2(a, b) + h * h * k * .25f;
}
// Distance to a box of half size (bx, by) with corners rounded by r.
static inline float sdRoundBox(float x, float y, float bx, float by, float r) {
  float qx = fabsf(x) - bx + r, qy = fabsf(y) - by + r;
  if (qx > 0 && qy > 0) return fsqrt(qx * qx + qy * qy) - r;
  return fmax2(qx, qy) - r;
}
// Heart, about one unit tall, point at y = 0 (Inigo Quilez's sdHeart).
static inline float sdHeart(float x, float y) {
  x = fabsf(x);
  if (y + x > 1.0f) {
    float ax = x - .25f, ay = y - .75f;
    return fsqrt(ax * ax + ay * ay) - .353553391f;
  }
  float ax = x, ay = y - 1.0f;
  float m = .5f * fmax2(x + y, 0.0f);
  float bx = x - m, by = y - m;
  float da = ax * ax + ay * ay, db = bx * bx + by * by;
  float d = fsqrt(da < db ? da : db);
  return x - y < 0 ? -d : d;
}

void EyeRenderer::begin(uint16_t *framebuffer, PushWindowFn pushFn) {
  fb = framebuffer;
  push = pushFn;
  fullRedraw = true;
}

EyeRenderer::Box EyeRenderer::bounds(const EyeFrame &e) {
  float c = fabsf(cosf(e.tilt)), s = fabsf(sinf(e.tilt));
  float h = e.hh + fabsf(e.bend);
  float ex = e.hw * c + h * s + 2.0f, ey = e.hw * s + h * c + 2.0f;
  return {int(floorf(e.cx - ex)), int(floorf(e.cy - ey)), int(ceilf(e.cx + ex)), int(ceilf(e.cy + ey))};
}

void EyeRenderer::renderEye(const EyeFrame &e, const Box &clip) {
  Box b = bounds(e);
  int x0 = b.x0 > clip.x0 ? b.x0 : clip.x0, x1 = b.x1 < clip.x1 ? b.x1 : clip.x1;
  int y0 = b.y0 > clip.y0 ? b.y0 : clip.y0, y1 = b.y1 < clip.y1 ? b.y1 : clip.y1;
  if (x1 < x0 || y1 < y0 || e.hw < .5f || e.hh < .3f) return;

  const float hw = e.hw, hh = e.hh;
  const float r = fmin2(e.radius, fmin2(hw, hh));
  const float cs = cosf(e.tilt), sn = sinf(e.tilt);
  const float mirror = e.rightEye ? -1.0f : 1.0f;  // x toward the nose is +
  const float bendK = e.bend / (hw * hw);
  const float k = fmax2(.5f, e.lidRound), invK = 1.0f / k;
  // Pupil (and its heart / spiral morphs).
  const float pr = e.pupilR;
  const bool pupil = pr > .5f;
  const float heart = clamp01(e.heart), spiral = clamp01(e.spiral);
  const float pRound = pr * PUPIL_ROUNDNESS;
  const float invSquash = 1.0f / fmax2(.05f, e.pupilSquash);
  const float heartS = pr * 1.35f, invHeart = 1.0f / heartS;
  const float spiralR = pr * 1.08f, invSpiralR = 1.0f / spiralR;
  const float spiralPx = spiralR / SPIRAL_TURNS;
  const float spiralShift = e.spiralPhase * (1.0f / TAU);
  const float reach = (spiral > 0 ? spiralR : heart > 0 ? heartS : pr) + 2.0f;
  const float pcx = e.cx + e.pupilX, pcy = e.cy + e.pupilY;

  for (int y = y0; y <= y1; ++y) {
    uint16_t *line = fb + y * SW;
    const float dy = y - e.cy;
    const float uy = (y - pcy) * invSquash;
    const bool pupilRow = pupil && fabsf(uy) < reach;
    for (int x = x0; x <= x1; ++x) {
      const float dxm = (x - e.cx) * mirror;
      // Into the eye's frame: rotate, then undo the bend.
      const float xi = dxm * cs + dy * sn;
      float ly = dy * cs - dxm * sn;
      const float xx = xi * xi;
      ly -= bendK * xx;
      float d = sdRoundBox(xi, ly, hw, hh, r);
      if (d >= 0.0f) continue; // outside (the lids only ever cut more away): stays black
      // Lids: curved lines, distance measured along their normals.
      float sT = e.topB + 2.0f * e.topC * xi;
      float dT = (e.topA + e.topB * xi + e.topC * xx - ly) * rsqrt(1.0f + sT * sT);
      float sB = e.botB + 2.0f * e.botC * xi;
      float dB = (ly - (e.botA + e.botB * xi + e.botC * xx)) * rsqrt(1.0f + sB * sB);
      d = smax(d, smax(dT, dB, k, invK), k, invK);
      if (d >= 0.0f) continue;

      uint16_t px = PIXEL_EYE;
      if (pupilRow) {
        const float ux = x - pcx;
        if (fabsf(ux) < reach) {
          float dP = sdRoundBox(ux, uy, pr, pr, pRound);
          if (heart > 0) {
            float dH = sdHeart(ux * invHeart, -uy * invHeart + .55f) * heartS;
            dP += (dH - dP) * heart;
          }
          if (spiral > 0) {
            float len = fsqrt(ux * ux + uy * uy);
            float vv = len * invSpiralR * SPIRAL_TURNS - fastAtan2(uy, ux) * (1.0f / TAU) - spiralShift;
            float fr = vv - float(int(vv));
            if (fr < 0) fr += 1.0f;
            float dS = fmax2((fabsf(fr - .5f) - SPIRAL_HALF_WIDTH) * spiralPx, len - spiralR);
            dP += (dS - dP) * spiral;
          }
          if (dP < 0.0f) px = PIXEL_ACCENT;
        }
      }
      line[x] = px;
    }
  }
}

void EyeRenderer::compose(const EyeFrame eyes[2]) {
  if (!fb) return;
  Box a = bounds(eyes[0]), b = bounds(eyes[1]);
  Box cur = {a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0,
             a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1};
  cur.x0 = cur.x0 < 0 ? 0 : cur.x0;
  cur.y0 = cur.y0 < 0 ? 0 : cur.y0;
  cur.x1 = cur.x1 > SW - 1 ? SW - 1 : cur.x1;
  cur.y1 = cur.y1 > SH - 1 ? SH - 1 : cur.y1;
  Box d = cur;
  if (fullRedraw) d = {0, 0, SW - 1, SH - 1};
  else if (prev.x1 >= prev.x0) {
    d.x0 = d.x0 < prev.x0 ? d.x0 : prev.x0; d.y0 = d.y0 < prev.y0 ? d.y0 : prev.y0;
    d.x1 = d.x1 > prev.x1 ? d.x1 : prev.x1; d.y1 = d.y1 > prev.y1 ? d.y1 : prev.y1;
  }
  dirty = d;
  if (d.x1 < d.x0 || d.y1 < d.y0) return;
  // Off-screen: clear the window to pure black, draw both eyes, push once.
  for (int y = d.y0; y <= d.y1; ++y)
    memset(fb + y * SW + d.x0, 0, (d.x1 - d.x0 + 1) * sizeof(uint16_t));
  renderEye(eyes[0], d);
  renderEye(eyes[1], d);
  prev = cur;
  fullRedraw = false;
}

void EyeRenderer::draw(const EyeFrame eyes[2]) {
  compose(eyes);
  if (push && dirty.x1 >= dirty.x0 && dirty.y1 >= dirty.y0)
    push(dirty.x0, dirty.y0, dirty.x1 + 1, dirty.y1 + 1, fb);
}
