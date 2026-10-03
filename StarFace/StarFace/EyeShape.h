#pragma once
#include "AnimMath.h"

// The shape of one eye, in units relative to the eye's size, so it reads the
// same on any display. Expressions are just EyeShape values (see
// Expressions.cpp); the animator springs every field from one to the next, so
// the outline itself deforms between expressions instead of switching.
//
// "Inner" means toward the nose: presets are written once and mirrored for the
// left eye, so a symmetric face is the default.
struct EyeShape {
  float x, y;           // offset from the eye's home position, in half-widths / half-heights
  float width, height;  // size multipliers (1 = rest)
  float round;          // corner roundness multiplier (1 = EYE_ROUNDNESS)
  float tilt;           // rotation of the whole eye, radians (+ inner end lower)
  float bend;           // arcs the eye: + ends drop (a happy "^"), - ends lift ("U"), in half-heights
  float topLid;         // 0..1: how far the upper lid comes down at the middle
  float topSlope;       // upper lid slant: + lower toward the nose (angry), - higher (sad)
  float topCurve;       // + the upper lid sags in the middle, - it arches
  float bottomLid;      // 0..1: how far the lower lid comes up at the middle
  float bottomCurve;    // + the lower lid arches up in the middle (happy crescent)
  float pupil;          // pupil size multiplier (0 hides it)
  float heart;          // 0..1: the pupil becomes a heart
  float spiral;         // 0..1: the pupil becomes a spinning spiral
};

namespace eyeshape {

constexpr int FIELDS = sizeof(EyeShape) / sizeof(float);

inline float *fields(EyeShape &s) { return reinterpret_cast<float *>(&s); }
inline const float *fields(const EyeShape &s) { return reinterpret_cast<const float *>(&s); }

// The calm, symmetric rest shape.
constexpr EyeShape NEUTRAL_SHAPE = {0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0};

// a + (b - a) * t for every field.
inline EyeShape mix(const EyeShape &a, const EyeShape &b, float t) {
  EyeShape r;
  const float *pa = fields(a), *pb = fields(b);
  float *pr = fields(r);
  for (int i = 0; i < FIELDS; ++i) pr[i] = pa[i] + (pb[i] - pa[i]) * t;
  return r;
}

// Scales how far a shape departs from neutral (expression intensity).
inline EyeShape amplify(const EyeShape &s, float amount) {
  return mix(NEUTRAL_SHAPE, s, amount);
}

} // namespace eyeshape

// Every EyeShape field on its own damped spring: setting a new target morphs
// the eye there smoothly, with acceleration, deceleration and (if damping is
// below 1) a little overshoot. Kicks add velocity for squash-and-stretch hits.
class ShapeSpring {
 public:
  void snap(const EyeShape &s) {
    const float *p = eyeshape::fields(s);
    for (int i = 0; i < eyeshape::FIELDS; ++i) f[i].snap(p[i]);
  }
  void setTarget(const EyeShape &s) {
    const float *p = eyeshape::fields(s);
    for (int i = 0; i < eyeshape::FIELDS; ++i) f[i].target = p[i];
  }
  // freq in Hz (higher is quicker), zeta damping (1 no overshoot, ~.5 springy).
  void update(float dt, float freq, float zeta) {
    for (int i = 0; i < eyeshape::FIELDS; ++i) f[i].update(dt, freq * RATE[i], zeta);
  }
  void kick(int field, float velocity) { f[field].kick(velocity); }
  EyeShape value() const {
    EyeShape s;
    float *p = eyeshape::fields(s);
    for (int i = 0; i < eyeshape::FIELDS; ++i) p[i] = f[i].pos;
    return s;
  }

 private:
  anim::Spring f[eyeshape::FIELDS];
  // Lids move a little quicker than the eye outline, pupils quicker still.
  static constexpr float RATE[eyeshape::FIELDS] = {
    1, 1, 1, 1, .9f, .9f, .9f, 1.15f, 1.1f, 1.1f, 1.15f, 1.1f, 1.2f, .9f, 1.0f
  };
};

// Field indices for ShapeSpring::kick.
enum EyeShapeField : uint8_t {
  ES_X, ES_Y, ES_WIDTH, ES_HEIGHT, ES_ROUND, ES_TILT, ES_BEND, ES_TOP, ES_TOP_SLOPE,
  ES_TOP_CURVE, ES_BOTTOM, ES_BOTTOM_CURVE, ES_PUPIL, ES_HEART, ES_SPIRAL
};
