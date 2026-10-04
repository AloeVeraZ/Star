// The per-pixel loop is the hot path of the sketch; -O2 roughly halves its
// cost compared with the Arduino default -Os.
#pragma GCC optimize("O2")
#include "EyeRenderer.h"
#include <math.h>
#include <string.h>

static constexpr int SW = SCREEN_WIDTH, SH = SCREEN_HEIGHT;

static constexpr float SPIRAL_TURNS = 2.4f;       // arms of the dizzy spiral across its radius
static constexpr float SPIRAL_HALF_WIDTH = 0.25f; // arm half width, in turns
static constexpr float TAU = 6.28318531f;
static constexpr float FAR = 1e6f;                // "nowhere near this edge"

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
// Edge coverage of a pixel from the signed distance at its centre.
static inline float cover(float d) {
  if (ANTI_ALIAS) return clamp01(.5f - d);
  return d < 0 ? 1.0f : 0.0f;
}
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

struct RGB { float r, g, b; };
static constexpr RGB rgbOf(uint32_t c) {
  return {float((c >> 16) & 255), float((c >> 8) & 255), float(c & 255)};
}
static inline void unpack565(uint16_t v, float &r, float &g, float &b) {
  uint16_t c = uint16_t((v << 8) | (v >> 8));
  r = ((c >> 11) & 31) * (255.0f / 31.0f);
  g = ((c >> 5) & 63) * (255.0f / 63.0f);
  b = (c & 31) * (255.0f / 31.0f);
}

// The glow around the eyes: how much of the eye colour shows d px outside
// the outline (a bright rim fading smoothly to nothing at GLOW_PX).
static constexpr float GLOW_PX = GLOW_SIZE * SCREEN_MIN_SIDE;
static float glowLut[65];
static void buildGlow() {
  for (int i = 0; i <= 64; ++i) {
    float d = i * (GLOW_PX / 64.0f);
    float fade = 1.0f - clamp01(d / GLOW_PX);
    glowLut[i] = GLOW_STRENGTH * expf(-d / (GLOW_PX * .3f)) * fade * fade;
  }
}
static inline float glowAt(float d) {
  if (!GLOW || d >= GLOW_PX) return 0;
  return glowLut[int(fmax2(0.0f, d) * (64.0f / GLOW_PX))];
}

static inline uint16_t pack565(float r, float g, float b) {
  int ri = int(r * (31.0f / 255.0f) + .5f), gi = int(g * (63.0f / 255.0f) + .5f), bi = int(b * (31.0f / 255.0f) + .5f);
  ri = ri > 31 ? 31 : (ri < 0 ? 0 : ri);
  gi = gi > 63 ? 63 : (gi < 0 ? 0 : gi);
  bi = bi > 31 ? 31 : (bi < 0 ? 0 : bi);
  uint16_t c = uint16_t((ri << 11) | (gi << 5) | bi);
  return uint16_t((c << 8) | (c >> 8));
}

void EyeRenderer::begin(uint16_t *framebuffer, PushWindowFn pushFn) {
  fb = framebuffer;
  push = pushFn;
  fullRedraw = true;
  buildGlow();
}

EyeRenderer::Box EyeRenderer::bounds(const EyeFrame &e) {
  float c = cosf(e.tilt), s = sinf(e.tilt);
  float ry = e.ry + fabsf(e.bend);
  const float pad = 2.0f + (GLOW ? GLOW_PX : 0.0f);
  float ex = sqrtf(e.rx * e.rx * c * c + ry * ry * s * s) + pad;
  float ey = sqrtf(e.rx * e.rx * s * s + ry * ry * c * c) + pad;
  return {int(floorf(e.cx - ex)), int(floorf(e.cy - ey)), int(ceilf(e.cx + ex)), int(ceilf(e.cy + ey))};
}

void EyeRenderer::renderEye(const EyeFrame &e, const Box &clip) {
  Box b = bounds(e);
  int x0 = b.x0 > clip.x0 ? b.x0 : clip.x0, x1 = b.x1 < clip.x1 ? b.x1 : clip.x1;
  int y0 = b.y0 > clip.y0 ? b.y0 : clip.y0, y1 = b.y1 < clip.y1 ? b.y1 : clip.y1;
  if (x1 < x0 || y1 < y0 || e.rx < 1.0f || e.ry < 1.0f) return;

  static constexpr RGB EYE = rgbOf(EYE_COLOR), PUP = rgbOf(PUPIL_COLOR);
  const float rx = e.rx, ry = e.ry;
  const float iax = 1.0f / rx, iay = 1.0f / ry, iax2 = iax * iax, iay2 = iay * iay;
  // Near the oval's edge the distance is computed exactly; well inside or
  // outside it is not needed.
  const float glowPx = GLOW ? GLOW_PX : 0.0f;
  const float band = 1.6f / fmin2(rx, ry), bandOut = (1.6f + glowPx) / fmin2(rx, ry);
  const float qIn = (1.0f - band) * (1.0f - band), qOut = (1.0f + bandOut) * (1.0f + bandOut);
  const float cs = cosf(e.tilt), sn = sinf(e.tilt);
  const float mirror = e.rightEye ? -1.0f : 1.0f;  // x toward the nose is +
  const float bendK = e.bend * iax2;
  const float k = fmax2(.5f, e.lidRound), invK = 1.0f / k;
  // A lid farther than this (unnormalised) cannot affect a pixel.
  const float slopeT = fabsf(e.topB) + 2.0f * fabsf(e.topC) * rx, slopeB = fabsf(e.botB) + 2.0f * fabsf(e.botC) * rx;
  const float reachT = (k + 1.5f + glowPx) * fsqrt(1.0f + slopeT * slopeT);
  const float reachB = (k + 1.5f + glowPx) * fsqrt(1.0f + slopeB * slopeB);

  // Pupil (and its heart / spiral morphs) and catch-lights.
  const float pr = e.pupilR;
  const bool pupil = pr > .4f;
  const float heart = clamp01(e.heart), spiral = clamp01(e.spiral);
  const float isx = 1.0f / fmax2(.05f, e.pupilSX), isy = 1.0f / fmax2(.05f, e.pupilSY);
  const float pScale = fmin2(e.pupilSX, e.pupilSY);
  const float heartS = pr * 1.35f, invHeart = 1.0f / fmax2(.01f, heartS);
  const float spiralR = pr * 1.06f, invSpiralR = 1.0f / fmax2(.01f, spiralR);
  const float spiralPx = spiralR / SPIRAL_TURNS;
  const float spiralShift = e.spiralPhase * (1.0f / TAU);
  const float reach = (spiral > 0 ? spiralR : heart > 0 ? heartS : pr) * fmax2(e.pupilSX, e.pupilSY) + 2.0f;
  const float pcx = e.cx + e.pupilX, pcy = e.cy + e.pupilY;
  const float coreR = e.coreR;
  const float g1x = e.cx + e.glintX, g1y = e.cy + e.glintY, g1r = e.glintR;
  const float g2x = e.cx + e.glint2X, g2y = e.cy + e.glint2Y, g2r = e.glint2R;
  const uint16_t eyePacked = PIXEL_EYE;

  for (int y = y0; y <= y1; ++y) {
    uint16_t *line = fb + y * SW;
    const float dy = y - e.cy;
    const float uy = y - pcy;
    const bool pupilRow = pupil && fabsf(uy) < reach;
    for (int x = x0; x <= x1; ++x) {
      const float dxm = (x - e.cx) * mirror;
      // Into the eye's frame: rotate, then undo the bend.
      const float xi = dxm * cs + dy * sn;
      const float xx = xi * xi;
      const float ly = dy * cs - dxm * sn - bendK * xx;
      // The oval.
      const float q = xx * iax2 + ly * ly * iay2;
      if (q >= qOut) continue;                            // outside: stays black
      float d;
      if (q <= qIn) {
        d = -FAR;
      } else {                                            // near the edge: distance to the ellipse
        float sq = fsqrt(q);
        float gg = xx * iax2 * iax2 + ly * ly * iay2 * iay2;
        d = (sq - 1.0f) * sq * rsqrt(gg);
      }
      // Lids: curved lines, distance along their normals (only when close).
      float uT = e.topA + e.topB * xi + e.topC * xx - ly;
      float uB = ly - (e.botA + e.botB * xi + e.botC * xx);
      if (uT > -reachT) {
        float s1 = e.topB + 2.0f * e.topC * xi;
        d = smax(d, uT * rsqrt(1.0f + s1 * s1), k, invK);
      }
      if (uB > -reachB) {
        float s2 = e.botB + 2.0f * e.botC * xi;
        d = smax(d, uB * rsqrt(1.0f + s2 * s2), k, invK);
      }
      if (d >= .5f) {
        // Outside: the glow. (Brighter wins, so the two eyes' glows and an
        // eye under the other's glow never darken each other.)
        float gl = glowAt(d);
        if (gl > .004f) {
          float r0, g0, b0;
          unpack565(line[x], r0, g0, b0);
          line[x] = pack565(fmax2(r0, EYE.r * gl), fmax2(g0, EYE.g * gl), fmax2(b0, EYE.b * gl));
        }
        continue;
      }
      const float aEye = cover(d);
      // An edge pixel shows the glow behind the part the eye doesn't cover.
      const float rim = aEye < 1.0f ? glowAt(0) * (1.0f - aEye) : 0.0f;

      float ux = x - pcx;
      if (!pupilRow || fabsf(ux) >= reach) {
        if (aEye >= 1.0f) { line[x] = eyePacked; continue; }
        line[x] = pack565(EYE.r * (aEye + rim), EYE.g * (aEye + rim), EYE.b * (aEye + rim));
        continue;
      }
      // Pupil: a circle seen on a round eyeball (narrower toward the sides).
      float sx = ux * isx, sy = uy * isy;
      float dP = (fsqrt(sx * sx + sy * sy) - pr) * pScale;
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
      float aP = cover(dP);
      // The iris's black centre (the background colour: depth without a new colour).
      float aC = 0;
      if (coreR > .4f && aP > 0) aC = cover((fsqrt(sx * sx + sy * sy) - coreR) * pScale);
      // Catch-lights, in the eye colour, only show on the pupil.
      if (aP > 0 && g1r > .3f) {
        float gx = x - g1x, gy = y - g1y;
        float aG = cover(fsqrt(gx * gx + gy * gy) - g1r);
        if (g2r > .3f) {
          float hx = x - g2x, hy = y - g2y;
          aG = fmax2(aG, cover(fsqrt(hx * hx + hy * hy) - g2r));
        }
        aP *= 1.0f - aG;
        aC *= 1.0f - aG;
      }
      if (aP <= 0 && aEye >= 1.0f) { line[x] = eyePacked; continue; }
      if (aC >= 1.0f) { line[x] = PIXEL_BACKGROUND; continue; }
      if (aP >= 1.0f && aC <= 0 && aEye >= 1.0f) { line[x] = PIXEL_PUPIL; continue; }
      const float keep = aEye * (1.0f - aC);
      float r = EYE.r + (PUP.r - EYE.r) * aP;
      float g = EYE.g + (PUP.g - EYE.g) * aP;
      float bl = EYE.b + (PUP.b - EYE.b) * aP;
      line[x] = pack565(r * keep + EYE.r * rim, g * keep + EYE.g * rim, bl * keep + EYE.b * rim);
    }
  }
}

EyeRenderer::Box EyeRenderer::cheekBounds(const EyeFrame &e) {
  if (!CHEEK_BLUSH || e.cheekRX < .5f || e.cheekAlpha <= 0) return {0, 0, -1, -1};
  float x = e.cx + e.cheekX, y = e.cy + e.cheekY;
  return {int(floorf(x - e.cheekRX - 2)), int(floorf(y - e.cheekRY - 2)),
          int(ceilf(x + e.cheekRX + 2)), int(ceilf(y + e.cheekRY + 2))};
}

// Rosy cheeks: soft-edged ovals under the eyes, drawn before the eyes.
void EyeRenderer::renderCheek(const EyeFrame &e, const Box &clip) {
  Box b = cheekBounds(e);
  int x0 = b.x0 > clip.x0 ? b.x0 : clip.x0, x1 = b.x1 < clip.x1 ? b.x1 : clip.x1;
  int y0 = b.y0 > clip.y0 ? b.y0 : clip.y0, y1 = b.y1 < clip.y1 ? b.y1 : clip.y1;
  if (x1 < x0 || y1 < y0) return;
  static constexpr RGB CHEEK = rgbOf(CHEEK_COLOR);
  const float cx = e.cx + e.cheekX, cy = e.cy + e.cheekY;
  const float iax = 1.0f / e.cheekRX, iay = 1.0f / e.cheekRY, iax2 = iax * iax, iay2 = iay * iay;
  const float alpha = clamp01(e.cheekAlpha);
  for (int y = y0; y <= y1; ++y) {
    uint16_t *line = fb + y * SW;
    const float ly = y - cy;
    for (int x = x0; x <= x1; ++x) {
      const float lx = x - cx;
      const float q = lx * lx * iax2 + ly * ly * iay2;
      if (q >= 1.3f) continue;
      float sq = fsqrt(q), gg = lx * lx * iax2 * iax2 + ly * ly * iay2 * iay2;
      float d = gg > 1e-9f ? (sq - 1.0f) * sq * rsqrt(gg) : -FAR;
      float a = cover(d) * alpha;
      if (a <= 0) continue;
      line[x] = a >= 1.0f ? PIXEL_CHEEK : pack565(CHEEK.r * a, CHEEK.g * a, CHEEK.b * a);
    }
  }
}

static inline void grow(int &x0, int &y0, int &x1, int &y1, int bx0, int by0, int bx1, int by1) {
  if (bx1 < bx0) return;
  x0 = bx0 < x0 ? bx0 : x0; y0 = by0 < y0 ? by0 : y0;
  x1 = bx1 > x1 ? bx1 : x1; y1 = by1 > y1 ? by1 : y1;
}

void EyeRenderer::compose(const EyeFrame eyes[2]) {
  if (!fb) return;
  Box a = bounds(eyes[0]), b = bounds(eyes[1]);
  Box cur = {a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0,
             a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1};
  for (int i = 0; i < 2; ++i) {
    Box c = cheekBounds(eyes[i]);
    grow(cur.x0, cur.y0, cur.x1, cur.y1, c.x0, c.y0, c.x1, c.y1);
  }
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
  renderCheek(eyes[0], d);
  renderCheek(eyes[1], d);
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
