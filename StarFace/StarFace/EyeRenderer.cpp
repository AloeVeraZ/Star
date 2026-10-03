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
static constexpr float LID_ROUNDING = 4.0f;      // px of rounding where a lid meets the eye outline
static constexpr float CLOSED_WIDTH = 0.84f;     // a shut eye is this much narrower (lid corners meet)
static constexpr float SPIRAL_TURNS = 2.6f;      // arms of the dizzy spiral across the iris radius
static constexpr float SPIRAL_HALF_WIDTH = 0.21f; // spiral line half width, in turns
// Reciprocals for the shading ramps (no divisions in the per-pixel loop).
static constexpr float INV_EDGE_BAND = 1.0f / (SCREEN_RADIUS - SAFE_RADIUS);

// Eye shapes per style (BEAN, DOT, BLIP, CAT), as multiples of the base size.
static const float LOOK_W[EYE_LOOK_COUNT] = {1.00f, 0.96f, 1.05f, 1.00f};
static const float LOOK_H[EYE_LOOK_COUNT] = {1.02f, 0.95f, 0.92f, 1.00f};
static const float LOOK_IRIS[EYE_LOOK_COUNT] = {1.00f, 1.00f, 1.00f, 1.00f};

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

// Per-style eye and pupil shapes (see EyeStyle in EyeRenderer.h). Pupil sizes
// are fractions of the eye's half width (pw) and half height (ph).
enum PupilShape : uint8_t { P_EGG, P_STAR };
struct StyleSpec {
  float round;     // corner rounding of the eye outline, px
  PupilShape shape;
  float pw, ph;    // pupil half width / half height (STAR: ph = star radius)
  float egg;       // EGG: how much wider the pupil is at the bottom
  float drop;      // pupil sits this far below the eye centre (x half height)
  float lean;      // pupil tilt toward the nose, radians
  float eyeLean;   // whole eye tilt, top outward, radians
  float glintR;    // star glint radius (x pupil half width), 0 = none
  float glintU, glintV; // glint position in the pupil (x pw, x ph)
};
static const StyleSpec STYLES[EYE_LOOK_COUNT] = {
  // round shape  pw    ph    egg   drop  lean  eyeLean glintR glintU glintV
  {3.0f, P_EGG,  .40f, .88f, .32f, .27f, .17f, .06f,  .50f, -.30f, -.50f},  // BEAN
  {3.0f, P_STAR, .24f, .24f, 0,    .02f, 0,    0,     0,     0,     0},      // DOT
  {11.f, P_EGG,  .30f, .33f, 0,    .02f, 0,    0,     .58f, -.32f, -.34f},  // BLIP
  {3.0f, P_EGG,  .085f, .64f, .05f, .08f, 0,   .04f,  0,     0,     0},      // CAT
};

// Exact signed distance to a five-pointed star of radius r and inner ratio rf
// (Inigo Quilez). y points up.
static inline float sdStar5(float x, float y, float r, float rf) {
  const float k1x = .809016994f, k1y = -.587785252f, k2x = -k1x, k2y = k1y;
  x = fabsf(x);
  float d1 = fmax2(k1x * x + k1y * y, 0.0f);
  x -= 2.0f * d1 * k1x; y -= 2.0f * d1 * k1y;
  float d2 = fmax2(k2x * x + k2y * y, 0.0f);
  x -= 2.0f * d2 * k2x; y -= 2.0f * d2 * k2y;
  x = fabsf(x);
  y -= r;
  const float bax = rf * -k1y, bay = rf * k1x - 1.0f;
  const float invBB = 1.0f / (bax * bax + bay * bay);
  float hh = clampf((x * bax + y * bay) * invBB, 0.0f, r);
  float ex = x - bax * hh, ey = y - bay * hh;
  float len = fsqrt(ex * ex + ey * ey);
  return (y * bax - x * bay) < 0 ? -len : len;
}

// Exact signed distance to a heart with its tip at the origin and lobes up to
// y ~ 1.1, half width ~ 0.6 (Inigo Quilez). y points up.
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

// One eye = ellipse ∩ below the upper lid ∩ above the lower lid, all bent by
// `bend`. d is an approximate signed distance in px (negative inside); it gives
// anti-aliased coverage at the edge and the halo outside it.
void EyeRenderer::renderEye(const EyeGeom &e, bool leftEye, const Box &clip) {
  const uint8_t style = e.style % EYE_LOOK_COUNT;
  const StyleSpec &st = STYLES[style];
  const EyeColors &pal = STYLE_COLORS[style];
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
  const float innerK = fmaxf(0.0f, 1.0f - 3.0f / minR);
  const float qInner = innerK * innerK; // q below this: well inside the outline
  const bool thin = cry < crx * .5f;
  const float nose = leftEye ? 1.0f : -1.0f; // +x points at the nose for the left eye
  // The whole eye leans a touch outward at the top (only while open).
  const float eyeLean = -nose * st.eyeLean * smoothstep(4.0f, 20.0f, h);
  const float ecos = cosf(eyeLean), esin = sinf(eyeLean);

  // Lids flatten away as the eye shuts so a closed eye is one clean arc.
  const float k = smoothstep(2.0f, .4f * ry, h);
  const float drop = e.lidDrop * k, angle = e.lidAngle * k;
  const float lower = fmaxf(0.0f, e.lowerLid) * k;
  const float lidCurve = 2.0f * EYE_PX_SCALE * k;
  const bool hasLower = lower > .3f;
  const float lidRound = fmaxf(.05f, LID_ROUNDING * k);
  const float lidRoundQ = .25f / lidRound;
  const float bend = e.bend;

  // ---- Colours: flat fills; anger flushes the eye pink and the pupil red ----
  const float heat = clamp01(e.heat);
  auto rgbOf = [](const uint8_t c[3]) { return Col{float(c[0]), float(c[1]), float(c[2])}; };
  const Col body = mixc(rgbOf(pal.body), {255, 196, 206}, heat * .55f);
  const Col pupilC = mixc(rgbOf(leftEye ? pal.pupilL : pal.pupilR), {214, 24, 52}, heat * .75f);
  const Col glintC = {255, 255, 255};
  const float glowGain = GLOW_STRENGTH * fmaxf(0.0f, e.glow) / 255.0f;
  const Col glowC = scalec(mixc(rgbOf(pal.glow), {255, 40, 70}, heat * .6f), glowGain);

  // ---- Pupil (eye-local coordinates) ----
  // It is cut by the eye outline and lids, fades into the shut line, and
  // squashes with a blink.
  const float show = smoothstep(2.5f, 7.0f, h);
  const float scale = e.iris / EYE_IRIS_RADIUS;      // expression size (surprise shrinks it)
  const float blinkSquash = 1.0f - .8f * clamp01(e.blink);
  float pw = st.pw * rx * scale, ph = st.ph * ry * scale;
  if (style == STYLE_CAT) {
    // A cat's slit widens into a round pupil when startled (small scale).
    float dilate = clamp01((1.0f - scale) * 3.0f);
    pw = anim::mix(st.pw * rx, .30f * rx, dilate);
    ph = anim::mix(st.ph * ry, .34f * ry, dilate);
  }
  ph *= blinkSquash;
  const float px = clampf(e.pupilX, -1.15f, 1.15f), py = clampf(e.pupilY, -1.15f, 1.15f);
  const float travelX = fmaxf(0.0f, rx - pw - 2.0f) * PUPIL_TRAVEL;
  const float travelY = fminf((ry - ph) * .7f + ry * .12f, h * .6f);
  const float pcx = px * travelX, pcy = py * travelY + st.drop * ry;
  const float lean = nose * st.lean, lcos = cosf(lean), lsin = sinf(lean);
  const float heart = clamp01(e.heart);
  const float heartS = .62f * fminf(rx, ry) * fminf(scale, 1.2f) * blinkSquash; // heart size, px
  const float spiral = clamp01(e.spiral);
  const float spiralR = .56f * fminf(rx, ry);
  const float spiralPx = spiralR / SPIRAL_TURNS;
  const float spiralShift = e.spiralPhase * (1.0f / anim::TAU_F);
  const float twCos = cosf(e.twinkleAngle), twSin = sinf(e.twinkleAngle);
  // Star glint: in the pupil's own frame, so it leans with it.
  const float glintR = st.glintR * pw * fmaxf(.2f, e.twinkle) * (1.0f - heart) * blinkSquash;
  const float glintU = st.glintU * pw, glintV = st.glintV * ph;
  const float dotR = style == STYLE_BLIP ? pw * .16f * blinkSquash * (1.0f - heart) : 0.0f;
  // How far pupil effects can reach from the pupil centre (bounding box).
  float pupilReach = st.shape == P_STAR ? ph * 1.1f : fmaxf(pw * (1.0f + st.egg), ph) + 2.0f;
  pupilReach = fmaxf(pupilReach, heart > 0 ? heartS * 1.25f : 0.0f);
  pupilReach = fmaxf(pupilReach, spiral > 0 ? spiralR + 1.5f : 0.0f) + 1.5f;
  const float reachX = st.shape == P_STAR || heart > 0 || spiral > 0
                           ? pupilReach
                           : pw * (1.0f + st.egg) + ph * fabsf(lsin) + 3.0f;

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
  const float iph = 1.0f / fmaxf(.5f, ph), iHeart = 1.0f / fmaxf(.5f, heartS);
  const float pupilR2 = pupilReach * pupilReach;

  for (int y = y0; y <= y1; ++y) {
    // Only pixels inside the round panel are shaded.
    float sy = y - SCREEN_CY;
    float span = screen2 - sy * sy;
    if (span <= 0) continue;
    float half = sqrtf(span);
    int xa = max(x0, int(ceilf(SCREEN_CX - half))), xb = min(x1, int(floorf(SCREEN_CX + half)));
    uint16_t bodyPacked[4];
    for (int i = 0; i < 4; ++i) bodyPacked[i] = pack565(body.r, body.g, body.b, i, y);
    uint16_t *line = fb + y * SW;

    for (int x = xa; x <= xb; ++x) {
      float dx = x - cx, yy = y - cy - colShift[x];
      if (dx * dx * ibx2 + yy * yy * iby2 > 1.0f) continue;

      // Signed distance to the eye: max() intersects ellipse and lids.
      float ex = dx * ecos + yy * esin, ey = -dx * esin + yy * ecos; // leaned eye frame
      float nx2 = ex * ex * icrx2, ny2 = ey * ey * icry2, q = nx2 + ny2;
      bool deep = q < qInner;
      float dU = (upY[x] - y) * upN[x];
      float dL = hasLower ? (y - loY[x]) * loN[x] : -1e9f;
      float lx = dx - pcx, ly = yy - pcy;
      bool inPupil = show > 0 && fabsf(lx) < reachX && fabsf(ly) < pupilReach &&
                     lx * lx + ly * ly < pupilR2 * 2.0f;
      if (deep && !inPupil && dU <= -1.0f && dL <= -1.0f) {
        line[x] = bodyPacked[x & 3]; // fast path: the flat body
        continue;
      }
      float dE;
      if (deep) {
        dE = -4.0f;
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
          float sx = fmax2(fabsf(ex) - crx, 0.0f);
          dE = fmax2(dE, fsqrt(sx * sx + ey * ey) - cry);
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

      // ---- Eye body: flat ----
      Col col = body;
      if (inPupil) {
        // Pupil frame: leaned toward the nose, y down.
        float pu = lx * lcos + ly * lsin, pv = -lx * lsin + ly * lcos;
        float dP;
        if (st.shape == P_STAR) {
          // Star pupils, slowly turning.
          float su = pu * twCos - pv * twSin, sv = pu * twSin + pv * twCos;
          dP = sdStar5(su, -sv, ph - 1.5f, .52f) - 1.5f; // rounded points
        } else {
          // Egg: wider toward the bottom (egg > 0).
          float w = pw * (1.0f + st.egg * clampf(pv * iph, -1.0f, 1.0f));
          w = fmax2(.5f, w);
          float iw = rsqrt2(w * w); // 1 / w without a slow division
          float nx = pu * iw, ny = pv * iph, qq = nx * nx + ny * ny;
          float kk = fsqrt(qq);
          dP = qq > 1e-8f ? (kk - 1.0f) * kk * rsqrt1(nx * nx * iw * iw + ny * ny * iph * iph)
                          : -(w < ph ? w : ph);
        }
        if (heart > 0) {
          // Happy: the pupil melts into a heart (upright, not leaned).
          float hd = sdHeart(lx * iHeart, -ly * iHeart + .55f) * heartS;
          dP = anim::mix(dP, hd, heart);
        }
        // (Dizzy: the spiral below replaces the pupil.)
        float aP = clamp01(.5f - dP) * show * (1.0f - spiral);
        col = mixc(col, pupilC, aP);
        if (glintR > .6f && aP > 0) {
          // A twinkling star glint on the pupil.
          float gu = pu - glintU, gv = pv - glintV;
          float tu = gu * twCos - gv * twSin, tv = gu * twSin + gv * twCos;
          float dG = sdStar5(tu, -tv, glintR, .45f);
          col = mixc(col, glintC, clamp01(.5f - dG) * aP);
        }
        if (dotR > .5f && aP > 0) {
          float du = pu + .30f * pw, dv = pv - .38f * ph;
          float dG = fsqrt(du * du + dv * dv) - dotR;
          col = mixc(col, glintC, clamp01(.5f - dG) * aP);
        }
        if (spiral > 0) {
          // Dizzy: a spinning spiral replaces the pupil.
          float kS = fsqrt(lx * lx + ly * ly) * (1.0f / spiralR);
          if (kS < 1.06f) {
            float vv = kS * SPIRAL_TURNS - fastAtan2(ly, lx) * (1.0f / anim::TAU_F) - spiralShift;
            float fr = vv - float(int(vv));
            if (fr < 0) fr += 1.0f;
            float arm = clamp01((SPIRAL_HALF_WIDTH - fabsf(fr - .5f)) * spiralPx + .5f);
            Col sc = mixc(body, pupilC, arm);
            float disc = clamp01((1.0f - kS) * spiralR + .5f);
            col = mixc(col, sc, disc * spiral * show);
          }
        }
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
