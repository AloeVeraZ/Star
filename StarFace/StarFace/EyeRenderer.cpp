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
static constexpr float LID_SHADOW = 8.0f;        // soft shade under the upper lid
static constexpr float LID_ROUNDING = 4.0f;      // px of rounding where a lid meets the eye outline
static constexpr float CLOSED_WIDTH = 0.84f;     // a shut eye is this much narrower (lid corners meet)
static constexpr float IRIS_DROP = 0.10f;        // irises sit a touch low, which reads as cute
static constexpr float SPIRAL_TURNS = 2.6f;      // arms of the dizzy spiral across the iris radius
static constexpr float SPIRAL_HALF_WIDTH = 0.21f; // spiral line half width, in turns
// Reciprocals for the shading ramps (no divisions in the per-pixel loop).
static constexpr float INV_SHADOW = 1.0f / (LID_SHADOW + .5f);
static constexpr float INV_EDGE_BAND = 1.0f / (SCREEN_RADIUS - SAFE_RADIUS);

// Eye shapes per style (NOVA, HALO, BLIP, CAT), as multiples of the base size.
static const float LOOK_W[EYE_LOOK_COUNT] = {1.00f, 1.00f, 1.06f, 1.02f};
static const float LOOK_H[EYE_LOOK_COUNT] = {1.00f, 0.96f, 0.84f, 0.98f};
static const float LOOK_IRIS[EYE_LOOK_COUNT] = {1.00f, 0.92f, 1.00f, 1.05f};

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
// atan2 to ~0.005 rad, using rsqrt for the one reciprocal (no division).
static inline float fastAtan2(float y, float x) {
  float ax = fabsf(x), ay = fabsf(y);
  float mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
  if (mx < 1e-6f) return 0.0f;
  float a = mn * rsqrt2(mx * mx), s = a * a;
  float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
  if (ay > ax) r = 1.57079637f - r;
  if (x < 0) r = 3.14159274f - r;
  return y < 0 ? -r : r;
}
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

// Per-style shape and shading parameters (see EyeStyle in EyeRenderer.h).
struct StyleSpec {
  float round;      // corner rounding of the outline, px (big = capsule)
  float band;       // edge band shaded by distance (fast path is deeper than this)
  float regionX, regionY; // pupil effect extent, in iris radii
  float shadow;     // strength of the soft shade under the upper lid
};
static const StyleSpec STYLES[EYE_LOOK_COUNT] = {
  {3.0f, 7.0f, 1.45f, 1.45f, .16f},  // NOVA: glowing orb, deep pupil in a ring of light
  {3.0f, 13.0f, 2.3f, 2.3f, 0.0f},   // HALO: hollow neon ring, bright dot pupil
  {15.0f, 7.0f, 1.25f, 1.25f, .10f}, // BLIP: glowing capsule, scanlines, light spot
  {3.0f, 7.0f, 1.15f, 2.0f, .16f},   // CAT: glowing orb, slit pupil
};

// One eye = ellipse ∩ below the upper lid ∩ above the lower lid, all bent by
// `bend`. d is an approximate signed distance in px (negative inside); it gives
// anti-aliased coverage at the edge and the halo outside it.
void EyeRenderer::renderEye(const EyeGeom &e, bool leftEye, const Box &clip) {
  const uint8_t style = e.style % EYE_LOOK_COUNT;
  const StyleSpec &st = STYLES[style];
  float cy, h;
  eyeDrawnExtent(e, cy, h);
  const float cx = e.x;
  const float ry = fmaxf(4.0f, e.ry);
  const float rx = fmaxf(4.0f, e.rx) * anim::mix(CLOSED_WIDTH, 1.0f, smoothstep(0.0f, .3f, e.open));

  // A smaller ellipse grown by `round`: rounder corners, and a shut eye gets
  // soft round tips.
  const float round = fminf(h * .85f, fminf(st.round, rx * .45f));
  const float crx = rx - round, cry = fmaxf(.35f, h - round);
  const float icrx2 = 1.0f / (crx * crx), icry2 = 1.0f / (cry * cry);
  const float minR = fminf(crx, cry);
  const float innerK = fmaxf(0.0f, 1.0f - (st.band + 1.0f) / minR);
  const float qInner = innerK * innerK; // q below this: deeper than the edge band
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

  // ---- Colours for this frame: everything is light, not paint ----
  const float heat = clamp01(e.heat);
  const Col white = {255, 255, 255};
  const Col c = mixc({base.r, base.g, base.b}, {255, 50, 70}, heat * .65f); // the eye's colour
  const Col hot = mixc(c, white, .50f);       // white-hot core
  const Col neon = mixc(c, white, .42f);      // bright outline
  const Col dark = scalec(c, .10f);           // unlit inside (HALO)
  const Col pupilC = scalec(c, .05f);         // near-black, tinted
  const Col pupilDeep = mixc(scalec(c, .16f), {10, 0, 40}, .5f); // NOVA: deep indigo pupil
  const Col spark = {255, 255, 255};
  const float glowGain = GLOW_STRENGTH * fmaxf(0.0f, e.glow) / 255.0f;
  const Col glowC = scalec(c, glowGain);
  // Radial body gradient (NOVA, CAT): hot core fading to the deep colour.
  Col radial[33];
  for (int i = 0; i <= 32; ++i) radial[i] = mixc(hot, c, ss01(i / 32.0f * 1.15f - .1f));

  // ---- Pupil effects (eye-local coordinates) ----
  // They shrink into the lids as the eye shuts and squash with a blink.
  const float irisShow = smoothstep(2.5f, 7.0f, h);
  const float irisR = e.iris;
  const float blinkSquash = 1.0f - .8f * clamp01(e.blink);
  const float px = clampf(e.pupilX, -1.15f, 1.15f), py = clampf(e.pupilY, -1.15f, 1.15f);
  const float travelX = fmaxf(0.0f, rx - irisR * .92f) * PUPIL_TRAVEL;
  const float travelY = fminf(fmaxf(0.0f, ry - irisR) * PUPIL_TRAVEL * .75f, h * .55f);
  const float iox = px * travelX, ioy = py * travelY + irisR * IRIS_DROP;
  // Looking sideways squeezes the pupil a little (it is on a curved surface).
  const float irx = irisR * (1.0f - .14f * fabsf(px));
  const float iry = irisR * 1.06f * (1.0f - .07f * fabsf(py)) * blinkSquash;
  const float iirx = 1.0f / irx, iiry = 1.0f / iry;
  const float regX = irx * st.regionX + 1.0f, regY = iry * st.regionY + 1.0f;
  const float spiral = clamp01(e.spiral);
  const float spiralPx = irisR / SPIRAL_TURNS;     // px per spiral turn
  const float spiralShift = e.spiralPhase * (1.0f / anim::TAU_F);
  // A crisp sparkle (NOVA, CAT) lags the pupil a little: a fixed light source.
  const bool hasSpark = style == STYLE_NOVA || style == STYLE_CAT || style == STYLE_BLIP;
  const float hlShow = hasSpark ? smoothstep(8.0f, 18.0f, h) * (1.0f - .75f * spiral) : 0.0f;
  const float hlSquash = 1.0f / fmaxf(.2f, blinkSquash);
  const float hlSquashInv = fmaxf(.2f, blinkSquash);
  float h1x, h1y, h1r, h2x, h2y, h2r;
  if (style == STYLE_BLIP) { // a glint on the capsule's upper corner
    h1x = -rx * .48f; h1y = -h * .50f; h1r = 3.2f * EYE_PX_SCALE;
    h2x = -rx * .30f; h2y = -h * .62f; h2r = 1.6f * EYE_PX_SCALE;
  } else {
    h1x = iox * .86f - irisR * .30f; h1y = ioy * .86f - irisR * .34f; h1r = irisR * .22f;
    h2x = iox * .86f + irisR * .30f; h2y = ioy * .86f + irisR * .30f; h2r = irisR * .09f;
  }

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
  const bool radialBody = style == STYLE_NOVA || style == STYLE_CAT;
  const float shadowBand = LID_SHADOW;
  const float inv2h = 1.0f / (2.0f * h);

  for (int y = y0; y <= y1; ++y) {
    // Only pixels inside the round panel are shaded.
    float sy = y - SCREEN_CY;
    float span = screen2 - sy * sy;
    if (span <= 0) continue;
    float half = sqrtf(span);
    int xa = max(x0, int(ceilf(SCREEN_CX - half))), xb = min(x1, int(floorf(SCREEN_CX + half)));
    float v = clamp01((y - (cy - h)) * inv2h); // 0 top .. 1 bottom of the eye
    // Light falls a little from top to bottom; BLIP adds retro scanlines.
    float rowShade = 1.06f - .16f * v;
    Col row;
    if (style == STYLE_HALO) row = dark;
    else if (style == STYLE_BLIP) {
      row = scalec(mixc(hot, c, ss01(v * 1.1f)), (y % 3 == 0) ? .74f : 1.0f);
    } else row = c; // radial bodies are shaded per pixel
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
      bool inPupil = irisShow > 0 && fabsf(lx) < regX && fabsf(ly) < regY;
      bool inSpark = hlShow > 0 && fabsf(dx - h1x) < h1r + 1.0f &&
                     fabsf(yy - h1y) < h1r * hlSquashInv + 1.0f;
      if (deep && !inPupil && !inSpark && dU <= -shadowBand && dL <= -shadowBand * .6f) {
        // Fast path: most of the eye.
        if (radialBody) {
          Col b = scalec(radial[int(q * 32.0f)], rowShade);
          line[x] = pack565(b.r, b.g, b.b, x, y);
        } else {
          line[x] = rowPacked[x & 3];
        }
        continue;
      }
      float dE;
      if (deep) {
        dE = -st.band - 1.0f;
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
      Col col;
      float edgeIn = -d; // px inside the visible outline (lids included)
      if (style == STYLE_HALO) {
        // A neon tube along the outline, dim and hollow inside.
        float ring = ss01((edgeIn + .5f) * (1.0f / 2.0f)) * (1.0f - ss01((edgeIn - 3.0f) * (1.0f / 4.0f)));
        float inner = 1.0f - ss01((edgeIn - 3.0f) * (1.0f / 9.0f)); // faint light spilling inward
        col = mixc(mixc(dark, scalec(c, .55f), inner * .6f), neon, ring);
      } else {
        col = radialBody ? scalec(radial[int((q < 1.0f ? q : 1.0f) * 32.0f)], rowShade) : row;
        // A thin bright outline just inside the edge, like a neon sign.
        float line1 = ss01((edgeIn - .3f) * (1.0f / 1.4f)) * (1.0f - ss01((edgeIn - 2.2f) * (1.0f / 2.2f)));
        col = mixc(col, neon, line1 * .7f);
      }
      if (inPupil) {
        float ux = lx * iirx, uy = ly * iiry;
        float kI = fsqrt(ux * ux + uy * uy); // 1 = one iris radius from the pupil centre
        float show = irisShow;
        if (style == STYLE_NOVA) {
          // A deep pupil wrapped in a bright ring of light.
          float ringGlow = 1.0f - ss01((kI - .60f) * (1.0f / .70f));
          col = mixc(col, mixc(hot, white, .45f), ringGlow * ringGlow * .85f * show);
          float inPup = clamp01((.60f - kI) * irx + .5f);
          // The pupil glows faintly from its rim toward the middle.
          Col pc = mixc(pupilDeep, scalec(c, .45f), ss01((kI - .25f) * (1.0f / .35f)) * .6f);
          col = mixc(col, pc, inPup * show);
        } else if (style == STYLE_HALO) {
          // A bright floating dot with its own little glow.
          float dotGlow = 1.0f - ss01((kI - .40f) * (1.0f / 1.6f));
          col = mixc(col, neon, dotGlow * dotGlow * .8f * show);
          col = mixc(col, mixc(c, white, .88f), clamp01((.40f - kI) * irx + .5f) * show);
        } else if (style == STYLE_BLIP) {
          // No pupil: a soft spot of extra light shows where it is looking.
          float spot = 1.0f - ss01(kI * (1.0f / 1.2f));
          col = mixc(col, mixc(hot, white, .5f), spot * .6f * show);
        } else { // STYLE_CAT: a vertical slit with a glowing rim
          float sx = lx * iirx * (1.0f / .27f), syy = ly * iiry * (1.0f / 1.15f);
          float ks = fsqrt(sx * sx + syy * syy);
          float rimGlow = 1.0f - ss01((ks - 1.0f) * (1.0f / .6f)); // fades out inside the region
          col = mixc(col, hot, rimGlow * .5f * show);
          col = mixc(col, pupilC, clamp01((1.0f - ks) * irx * .27f + .5f) * show);
        }
        if (spiral > 0 && kI < 1.08f) {
          // Dizzy: a bright disc with a dark Archimedean spiral that spins.
          float vv = kI * SPIRAL_TURNS - fastAtan2(ly, lx) * (1.0f / anim::TAU_F) - spiralShift;
          float fr = vv - float(int(vv));
          if (fr < 0) fr += 1.0f;
          float arm = clamp01((SPIRAL_HALF_WIDTH - fabsf(fr - .5f)) * spiralPx + .5f);
          Col sc = mixc(hot, pupilC, arm);
          float disc = clamp01((1.0f - kI) * irx + .5f);
          col = mixc(col, sc, disc * spiral * show);
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
      // A faint shade under the upper lid keeps the lid shape readable.
      if (st.shadow > 0 && dU > -shadowBand) {
        float s = ss01((dU + shadowBand) * INV_SHADOW);
        col = scalec(col, 1.0f - st.shadow * k * s * s);
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
