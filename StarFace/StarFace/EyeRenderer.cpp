// The per-pixel shader is the hot loop of the sketch; -O2 roughly halves its
// cost compared with the Arduino default -Os.
#pragma GCC optimize("O2")
#include "EyeRenderer.h"
#include <math.h>
#include <string.h>
#include "AnimMath.h"

using anim::clamp01;
using anim::clampf;
using anim::smoothstep;

static constexpr int SW = 240, SH = 240;

// ---- Shape constants (px) ----
static constexpr float MIN_HALF_HEIGHT = 1.9f;   // a shut eye stays a soft glowing arc
static constexpr float CLOSE_LINE_DROP = 0.20f;  // shut line sits below centre: the upper lid travels further
static constexpr float RIM_WIDTH = 7.0f;         // saturated glowing band just inside the edge
static constexpr float LID_SHADOW = 8.0f;        // soft shade under the upper lid
static constexpr float LID_ROUNDING = 4.0f;      // px of rounding where a lid meets the eye outline
static constexpr float CLOSED_WIDTH = 0.84f;     // a shut eye is this much narrower (lid corners meet)
static constexpr float IRIS_DROP = 0.10f;        // irises sit a touch low, which reads as cute
// Reciprocals for the shading ramps (no divisions in the per-pixel loop).
static constexpr float INV_RIM = 1.0f / (RIM_WIDTH + .5f);
static constexpr float INV_SHADOW = 1.0f / (LID_SHADOW + .5f);
static constexpr float INV_SHADOW_LOW = 1.0f / (LID_SHADOW * .6f + .5f);
static constexpr float INV_EDGE_BAND = 1.0f / (SCREEN_RADIUS - SAFE_RADIUS);

// Eye looks, as multiples of the configured base size.
static const float LOOK_W[EYE_LOOK_COUNT] = {1.00f, 1.07f, 0.88f, 1.03f};
static const float LOOK_H[EYE_LOOK_COUNT] = {1.00f, 0.90f, 1.04f, 0.82f};
static const float LOOK_IRIS[EYE_LOOK_COUNT] = {1.00f, 1.00f, 0.84f, 1.00f};

float lookHalfWidth(uint8_t look) { return EYE_HALF_WIDTH * LOOK_W[look % EYE_LOOK_COUNT]; }
float lookHalfHeight(uint8_t look) { return EYE_HALF_HEIGHT * LOOK_H[look % EYE_LOOK_COUNT]; }
float lookIris(uint8_t look) { return EYE_IRIS_RADIUS * LOOK_IRIS[look % EYE_LOOK_COUNT]; }

void eyeDrawnExtent(const EyeGeom &e, float &cy, float &h) {
  // (The drawn width also narrows as the eye shuts; see CLOSED_WIDTH.)
  float ry = fmaxf(4.0f, e.ry);
  h = fmaxf(MIN_HALF_HEIGHT, ry * clampf(e.open, 0.0f, 1.35f));
  cy = e.y + (ry - h) * CLOSE_LINE_DROP;
}

// ---- Colour helpers. The buffer is byte-swapped RGB565 because Waveshare's
// SPI routine sends raw bytes. Shading happens in float 0..255 per channel. ----

// 4x4 Bayer thresholds in (0, 1): quantising with these instead of 0.5 turns
// RGB565 banding in the glows and gradients into invisible fine noise.
static const float BAYER[16] = {
  0.5f / 16, 8.5f / 16, 2.5f / 16, 10.5f / 16,
  12.5f / 16, 4.5f / 16, 14.5f / 16, 6.5f / 16,
  3.5f / 16, 11.5f / 16, 1.5f / 16, 9.5f / 16,
  15.5f / 16, 7.5f / 16, 13.5f / 16, 5.5f / 16
};

// Fast reciprocal square root (bit trick + Newton steps). sqrtf and division
// are slow multi-step sequences on the ESP32's FPU; one step is ~0.2 % accurate
// (enough for distances far from an edge), two steps are exact for practical
// purposes (used where anti-aliasing needs sub-pixel precision).
static inline float rsqrt1(float x) {
  union { float f; uint32_t i; } u;
  u.f = x;
  u.i = 0x5f375a86u - (u.i >> 1);
  float y = u.f;
  return y * (1.5f - .5f * x * y * y);
}
static inline float rsqrt2(float x) {
  float y = rsqrt1(x);
  return y * (1.5f - .5f * x * y * y);
}
static inline float fsqrt(float x) { return x > 1e-12f ? x * rsqrt2(x) : 0.0f; }
// Inline helpers for the hot loop. Float division is a slow library call on
// the ESP32 and fmaxf is not inlined, so the loop uses only these.
static inline float fmax2(float a, float b) { return a > b ? a : b; }
// Smoothstep of an already-normalised input (no division).
static inline float ss01(float t) { t = clamp01(t); return t * t * (3.0f - 2.0f * t); }

static inline uint16_t pack565(float r, float g, float b, int x, int y) {
  float t = DITHER ? BAYER[((y & 3) << 2) | (x & 3)] : 0.5f;
  int ri = int(r * (31.0f / 255.0f) + t);
  int gi = int(g * (63.0f / 255.0f) + t);
  int bi = int(b * (31.0f / 255.0f) + t);
  ri = ri < 0 ? 0 : (ri > 31 ? 31 : ri);
  gi = gi < 0 ? 0 : (gi > 63 ? 63 : gi);
  bi = bi < 0 ? 0 : (bi > 31 ? 31 : bi);
  uint16_t c = uint16_t((ri << 11) | (gi << 5) | bi);
  return uint16_t((c << 8) | (c >> 8));
}

static inline void unpack565(uint16_t v, float &r, float &g, float &b) {
  if (!v) { r = g = b = 0; return; }
  uint16_t c = uint16_t((v << 8) | (v >> 8));
  r = ((c >> 11) & 31) * (255.0f / 31.0f);
  g = ((c >> 5) & 63) * (255.0f / 63.0f);
  b = (c & 31) * (255.0f / 31.0f);
}

void EyeRenderer::begin(uint16_t *framebuffer, PushWindowFn pushFn) {
  fb = framebuffer;
  push = pushFn;
  fullRedraw = true;
  // Halo profile: a tight bright bloom plus a broad soft one, faded to zero
  // well before GLOW_EXTENT so it never ends in a visible ring.
  const int n = int(GLOW_EXTENT * 4) + 2;
  for (int i = 0; i < n; ++i) {
    float d = i * .25f;
    float v = .62f * expf(-d / (GLOW_FALLOFF * .38f)) + .38f * expf(-d / GLOW_FALLOFF);
    v *= 1.0f - smoothstep(GLOW_EXTENT * .45f, GLOW_EXTENT, d);
    glowLut[i] = uint8_t(clamp01(v) * 255.0f + .5f);
  }
}

void EyeRenderer::setPalette(uint8_t p) {
  p %= EYE_PALETTE_COUNT;
  base = {float(EYE_PALETTE[p][0]), float(EYE_PALETTE[p][1]), float(EYE_PALETTE[p][2])};
}

EyeRenderer::Box EyeRenderer::bounds(const EyeGeom &e) const {
  float cy, h;
  eyeDrawnExtent(e, cy, h);
  float rx = fmaxf(4.0f, e.rx) + GLOW_EXTENT + 1.0f;
  float ry = h + GLOW_EXTENT + fabsf(e.bend) * 1.44f + 2.0f;
  return {int(floorf(e.x - rx)), int(floorf(cy - ry)), int(ceilf(e.x + rx)), int(ceilf(cy + ry))};
}

namespace {
struct Col { float r, g, b; };
inline Col mixc(const Col &a, const Col &b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
inline Col scalec(const Col &a, float s) { return {a.r * s, a.g * s, a.b * s}; }
// Polynomial smooth maximum (rounded intersection of two distance fields).
inline float smax(float a, float b, float k, float quarterInvK) {
  float m = a > b ? a : b;
  float h = k - fabsf(a - b);
  return h <= 0 ? m : m + h * h * quarterInvK;
}
// Per-column lid and bend data, reused every frame (no allocation).
float colShift[SW], upY[SW], upN[SW], loY[SW], loN[SW];
} // namespace

// One eye = ellipse ∩ below the upper lid ∩ above the lower lid, all bent by
// `bend`. d is an approximate signed distance in px (negative inside); it gives
// anti-aliased coverage at the edge and the halo outside it.
void EyeRenderer::renderEye(const EyeGeom &e, bool leftEye, const Box &clip) {
  float cy, h;
  eyeDrawnExtent(e, cy, h);
  const float cx = e.x;
  const float ry = fmaxf(4.0f, e.ry);
  const float rx = fmaxf(4.0f, e.rx) * anim::mix(CLOSED_WIDTH, 1.0f, smoothstep(0.0f, .3f, e.open));

  // A slightly smaller ellipse grown by `round`: a shut eye gets soft round tips.
  const float round = fminf(h * .85f, 3.0f);
  const float crx = rx - round, cry = fmaxf(.35f, h - round);
  const float icrx2 = 1.0f / (crx * crx), icry2 = 1.0f / (cry * cry);
  const float minR = fminf(crx, cry);
  const float innerK = fmaxf(0.0f, 1.0f - (RIM_WIDTH + 1.0f) / minR);
  const float qInner = innerK * innerK; // q below this: deeper than the rim band
  const bool thin = cry < crx * .5f;

  // Lids flatten away as the eye shuts so a closed eye is one clean arc.
  const float k = smoothstep(2.0f, .4f * ry, h);
  const float drop = e.lidDrop * k, angle = e.lidAngle * k;
  const float lower = fmaxf(0.0f, e.lowerLid) * k;
  const float lidCurve = 2.0f * EYE_PX_SCALE * k;
  const bool hasLower = lower > .3f;
  const float lidRound = fmaxf(.05f, LID_ROUNDING * k);
  const float lidRoundQ = .25f / lidRound;
  const float nose = leftEye ? 1.0f : -1.0f; // +x points at the nose for the left eye
  const float bend = e.bend;

  // ---- Colours for this frame ----
  const float heat = clamp01(e.heat);
  Col c = mixc({base.r, base.g, base.b}, {255, 66, 58}, heat * .6f);
  const Col white = {255, 255, 255};
  const Col top = mixc(c, white, .80f), bottom = mixc(c, white, .42f);
  const Col rim = mixc(c, white, .10f);
  const Col irisDeep = scalec(c, .20f), irisLight = scalec(c, .64f);
  const Col irisRing = scalec(c, .09f), pupilC = scalec(c, .04f);
  const Col spark = {255, 255, 255};
  const float glowGain = GLOW_STRENGTH * fmaxf(0.0f, e.glow) / 255.0f;
  const Col glowC = scalec(c, glowGain);

  // ---- Iris, pupil and highlights (eye-local coordinates) ----
  // The lids cover the iris as the eye narrows; only the last few px fade it
  // out so the shut eye is a clean glowing line. During a blink the iris
  // squashes with the eye instead (squash & stretch).
  const float irisShow = smoothstep(2.5f, 7.0f, h);
  const float irisR = e.iris;
  const float blinkSquash = 1.0f - .8f * clamp01(e.blink);
  const float px = clampf(e.pupilX, -1.15f, 1.15f), py = clampf(e.pupilY, -1.15f, 1.15f);
  const float travelX = fmaxf(0.0f, rx - irisR * .92f) * PUPIL_TRAVEL;
  const float travelY = fminf(fmaxf(0.0f, ry - irisR) * PUPIL_TRAVEL * .75f, h * .55f);
  const float iox = px * travelX, ioy = py * travelY + irisR * IRIS_DROP;
  // Foreshortening: an iris looking sideways is a narrower oval.
  const float irx = irisR * (1.0f - .14f * fabsf(px));
  const float iry = irisR * 1.06f * (1.0f - .07f * fabsf(py)) * blinkSquash;
  const float iirx = 1.0f / irx, iiry = 1.0f / iry;
  const float prx = irx * .50f, pry = iry * .53f, pupilUp = iry * .04f;
  const float iprx = 1.0f / prx, ipry = 1.0f / pry;
  // Highlights reflect a fixed light, so they lag the iris (parallax).
  const float hlShow = smoothstep(8.0f, 18.0f, h);
  const float hlSquash = 1.0f / fmaxf(.2f, blinkSquash);
  const float hlSquashInv = fmaxf(.2f, blinkSquash);
  const float h1x = iox * .86f - irisR * .32f, h1y = ioy * .86f - irisR * .38f, h1r = irisR * .29f;
  const float h2x = iox * .86f + irisR * .36f, h2y = ioy * .86f + irisR * .36f, h2r = irisR * .12f;

  // ---- Bounds, clipped to the dirty window ----
  int x0 = max(clip.x0, int(floorf(cx - rx - GLOW_EXTENT)));
  int x1 = min(clip.x1, int(ceilf(cx + rx + GLOW_EXTENT)));
  const float yr = h + GLOW_EXTENT + fabsf(bend) * 1.44f + 1.0f;
  int y0 = max(clip.y0, int(floorf(cy - yr)));
  int y1 = min(clip.y1, int(ceilf(cy + yr)));
  if (x0 > x1 || y0 > y1) return;

  // ---- Per-column curves: bend offset, lid edges and their slope normalisers ----
  const float irx0 = 1.0f / rx;
  for (int x = x0; x <= x1; ++x) {
    float u = (x - cx) * irx0;
    bool inside = fabsf(u) < 1.2f;
    float sc = clampf(u, -1.2f, 1.2f);
    float bo = bend * sc * sc;
    float bs = inside ? 2.0f * bend * sc * irx0 : 0.0f;
    colShift[x] = bo;
    upY[x] = cy - h + drop + nose * sc * angle * rx + lidCurve * sc * sc + bo;
    float us = (inside ? nose * angle + 2.0f * lidCurve * sc * irx0 : 0.0f) + bs;
    upN[x] = rsqrt2(1.0f + us * us);
    if (hasLower) {
      loY[x] = cy + h + 2.0f - lower * (1.0f - .9f * sc * sc) + bo;
      float ls = (inside ? 1.8f * lower * sc * irx0 : 0.0f) + bs;
      loN[x] = rsqrt2(1.0f + ls * ls);
    }
  }

  const float bx = rx + GLOW_EXTENT, by = h + GLOW_EXTENT;
  const float ibx2 = 1.0f / (bx * bx), iby2 = 1.0f / (by * by);
  const float safe2 = SAFE_RADIUS * SAFE_RADIUS;
  const float screen2 = (SCREEN_RADIUS + .5f) * (SCREEN_RADIUS + .5f);
  const float glowEnd = GLOW_EXTENT - .3f;

  for (int y = y0; y <= y1; ++y) {
    // Only pixels inside the round panel are shaded.
    float sy = y - SCREEN_CY;
    float span = screen2 - sy * sy;
    if (span <= 0) continue;
    float half = sqrtf(span);
    int xa = max(x0, int(ceilf(SCREEN_CX - half))), xb = min(x1, int(floorf(SCREEN_CX + half)));
    float v = smoothstep(clamp01((y - (cy - h)) / (2.0f * h)));
    const Col row = mixc(top, bottom, v);
    // Plain interior pixels need no shading at all: the row colour, already
    // dithered for each of the four x phases.
    uint16_t rowPacked[4];
    for (int i = 0; i < 4; ++i) rowPacked[i] = pack565(row.r, row.g, row.b, i, y);
    uint16_t *line = fb + y * SW;

    for (int x = xa; x <= xb; ++x) {
      float dx = x - cx, yy = y - cy - colShift[x];
      if (dx * dx * ibx2 + yy * yy * iby2 > 1.0f) continue;

      // Signed distance to the eye: max() intersects ellipse and lids.
      float nx2 = dx * dx * icrx2, ny2 = yy * yy * icry2, q = nx2 + ny2;
      bool deep = q < qInner;
      float dU = (upY[x] - y) * upN[x];
      float dL = hasLower ? (y - loY[x]) * loN[x] : -1e9f;
      float lx = dx - iox, ly = yy - ioy;
      bool inIris = irisShow > 0 && fabsf(lx) < irx + 1.0f && fabsf(ly) < iry + 1.0f;
      if (deep && !inIris && dU <= -LID_SHADOW && dL <= -LID_SHADOW * .6f &&
          !(fabsf(dx - h1x) < h1r + 1.0f && fabsf(yy - h1y) < h1r * hlSquashInv + 1.0f)) {
        line[x] = rowPacked[x & 3]; // fast path: most of the eye
        continue;
      }
      float dE;
      if (deep) {
        dE = -RIM_WIDTH - 1.0f;
      } else if (q < 1e-8f) {
        dE = -minR;
      } else {
        // d ~ (k - 1) k / |grad|, with k = sqrt(q); all via fast rsqrt.
        float kq = q * rsqrt2(q);
        dE = (kq - 1.0f) * kq * rsqrt1(nx2 * icrx2 + ny2 * icry2);
        // The first-order estimate collapses for a thin (nearly shut) ellipse
        // along its long axis. The ellipse sits inside the capsule around that
        // axis, so the capsule distance is a lower bound that fixes the tips.
        if (thin) {
          float sx = fmax2(fabsf(dx) - crx, 0.0f);
          dE = fmax2(dE, fsqrt(sx * sx + yy * yy) - cry);
        }
        dE -= round;
      }
      // Smooth intersection: rounded corners where lid meets outline, and no
      // crease in the halo along the hidden part of the ellipse.
      float d = smax(smax(dE, dU, lidRound, lidRoundQ), dL, lidRound, lidRoundQ);
      if (d >= glowEnd) continue;

      // Glow fades out smoothly toward the bezel instead of being cut by it.
      float r2 = (x - SCREEN_CX) * (x - SCREEN_CX) + sy * sy;
      float edgeFade = r2 > safe2 ? 1.0f - ss01((fsqrt(r2) - SAFE_RADIUS) * INV_EDGE_BAND) : 1.0f;

      if (d > .5f) { // halo only: additive light on whatever is below
        float gi = glowLut[int(d * 4.0f)] * edgeFade;
        if (gi < 1.0f) continue;
        float r, gg, b;
        unpack565(line[x], r, gg, b);
        line[x] = pack565(r + glowC.r * gi, gg + glowC.g * gi, b + glowC.b * gi, x, y);
        continue;
      }

      // ---- Eye body ----
      Col col = row;
      if (!deep) { // brighter, more saturated band just inside the edge
        float rt = ss01((dE + RIM_WIDTH) * INV_RIM);
        col = mixc(col, rim, rt * rt * .75f);
      }
      if (inIris) {
        float ux = lx * iirx, uy = ly * iiry;
        float qI = ux * ux + uy * uy;
        float kI = fsqrt(qI);
        float dI = qI > 1e-8f ? (kI - 1.0f) * kI * rsqrt1(ux * ux * iirx * iirx + uy * uy * iiry * iiry)
                              : -irx;
        float aI = clamp01(.5f - dI);
        if (aI > 0) {
          // Deep at the top, lit from below, with a dark limbal ring.
          Col ic = mixc(irisDeep, irisLight, ss01((uy + .15f) * (1.0f / 1.15f)) * .9f);
          ic = mixc(ic, irisRing, ss01((kI - .70f) * (1.0f / .30f)) * .85f);
          float pxu = lx * iprx, pyu = (ly + pupilUp) * ipry;
          float dP = (fsqrt(pxu * pxu + pyu * pyu) - 1.0f) * prx;
          ic = mixc(ic, pupilC, clamp01(.5f - dP));
          // As the lids meet, the iris dissolves into the glowing rim colour
          // (never into a muddy grey).
          if (irisShow < 1.0f) ic = mixc(rim, ic, irisShow);
          col = mixc(col, ic, aI);
        }
      }
      if (hlShow > 0) {
        // Highlights squash with a blink rather than fading to grey.
        float hx = dx - h1x, hy = (yy - h1y) * hlSquash;
        if (fabsf(hx) < h1r + 1.0f && fabsf(hy) < h1r + 1.0f) {
          float a = clamp01(.5f - (fsqrt(hx * hx + hy * hy) - h1r)) * hlShow;
          col = mixc(col, spark, a);
        }
        hx = dx - h2x; hy = (yy - h2y) * hlSquash;
        if (fabsf(hx) < h2r + 1.0f && fabsf(hy) < h2r + 1.0f) {
          float a = clamp01(.5f - (fsqrt(hx * hx + hy * hy) - h2r)) * hlShow * .9f;
          col = mixc(col, spark, a);
        }
      }
      // Soft shade under the lids gives the eye depth instead of a flat cut.
      if (dU > -LID_SHADOW) {
        float s = ss01((dU + LID_SHADOW) * INV_SHADOW);
        col = scalec(col, 1.0f - .34f * k * s * s);
      }
      if (hasLower && dL > -LID_SHADOW * .6f) {
        float s = ss01((dL + LID_SHADOW * .6f) * INV_SHADOW_LOW);
        col = scalec(col, 1.0f - .22f * s * s);
      }

      float a = clamp01(.5f - d);
      if (a >= .999f) {
        line[x] = pack565(col.r, col.g, col.b, x, y);
      } else { // edge pixel: body over (background + halo)
        float r, gg, b;
        unpack565(line[x], r, gg, b);
        float gi = glowLut[0] * edgeFade;
        r += glowC.r * gi; gg += glowC.g * gi; b += glowC.b * gi;
        line[x] = pack565(r + (col.r - r) * a, gg + (col.g - gg) * a, b + (col.b - b) * a, x, y);
      }
    }
  }
}

void EyeRenderer::compose(const EyeGeom eyes[2]) {
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
  dirty = d;
  if (d.x1 < d.x0 || d.y1 < d.y0) return;
  // Off-screen: clear the window, draw both eyes, then push once.
  for (int y = d.y0; y <= d.y1; ++y)
    memset(fb + y * SW + d.x0, 0, (d.x1 - d.x0 + 1) * sizeof(uint16_t));
  renderEye(eyes[0], true, d);
  renderEye(eyes[1], false, d);
  prev = cur;
  fullRedraw = false;
}

void EyeRenderer::draw(const EyeGeom eyes[2]) {
  compose(eyes);
  if (push && dirty.x1 >= dirty.x0 && dirty.y1 >= dirty.y0)
    push(dirty.x0, dirty.y0, dirty.x1 + 1, dirty.y1 + 1, fb);
}
