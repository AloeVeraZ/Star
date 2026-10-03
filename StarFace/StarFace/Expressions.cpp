#include "Expressions.h"

namespace {

// Field order: x, y, width, height, round, tilt, bend,
//              topLid, topSlope, topCurve, bottomLid, bottomCurve, pupil, heart, spiral
// x is toward the nose; presets are mirrored for the left eye.
struct Preset {
  EyeShape left, right;
  float freq, zeta;
  const char *name;
};

#define SAME(...) {__VA_ARGS__}, {__VA_ARGS__}

const Preset PRESETS[EXPRESSION_COUNT] = {
  // NEUTRAL: the calm rest shape.
  {SAME(0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0), 3.2f, .80f, "neutral"},
  // HAPPY: the lower lids arch up into "^" crescents, the pupils tuck away.
  {SAME(0, -.08f, 1.06f, .90f, 1.15f, 0, 0, 0, 0, 0, .16f, .40f, 0, 0, 0), 5.0f, .50f, "happy"},
  // SAD: lids slant down to the outside, the eyes sink and tilt.
  {SAME(-.03f, .10f, .98f, .92f, 1, -.05f, 0, .15f, -.19f, .02f, .04f, 0, 1.05f, 0, 0), 1.6f, 1.0f, "sad"},
  // ANGRY: lids slam down toward the nose.
  {SAME(.04f, .04f, 1.04f, .94f, .85f, .04f, 0, .15f, .25f, -.03f, .08f, 0, .82f, 0, 0), 6.0f, .55f, "angry"},
  // FOCUSED (determined): narrowed, level, a slight inward set.
  {SAME(.02f, .02f, 1.03f, .92f, .85f, 0, 0, .22f, .07f, 0, .16f, 0, .88f, 0, 0), 4.0f, .75f, "focused"},
  // SURPRISED: wide, rounder, small pupils.
  {SAME(0, -.06f, 1.08f, 1.12f, 1.3f, 0, 0, 0, 0, 0, 0, 0, .68f, 0, 0), 8.0f, .38f, "surprised"},
  // SLEEPY: heavy, drooping lids; shut, a soft "U" curve.
  {SAME(0, .08f, 1.02f, .94f, 1, -.02f, -.07f, .44f, 0, .05f, .06f, 0, 1.05f, 0, 0), 1.4f, 1.0f, "sleepy"},
  // SQUINT: narrowed from above and below.
  {SAME(0, 0, 1.06f, .95f, 1, 0, 0, .30f, .06f, 0, .27f, -.04f, .9f, 0, 0), 5.0f, .70f, "squint"},
  // CURIOUS: one eye opens wide, the other narrows.
  {{0, .03f, .97f, .92f, 1, 0, 0, .22f, .06f, 0, .08f, 0, .95f, 0, 0},
   {0, -.05f, 1.06f, 1.10f, 1.12f, 0, 0, 0, 0, 0, 0, 0, .95f, 0, 0}, 4.0f, .60f, "curious"},
  // CONFUSED: lopsided, one lid down, the eyes tilted against each other.
  {{0, .05f, .98f, .90f, 1, .07f, 0, .30f, .10f, 0, .05f, 0, .9f, 0, 0},
   {0, -.05f, 1.04f, 1.08f, 1.1f, -.05f, 0, 0, -.06f, 0, 0, 0, 1, 0, 0}, 3.5f, .55f, "confused"},
  // SUSPICIOUS: flat heavy lids, a hair uneven.
  {{0, .02f, 1.04f, .92f, .9f, 0, 0, .40f, .05f, 0, .16f, 0, .9f, 0, 0},
   {0, .02f, 1.04f, .92f, .9f, 0, 0, .31f, .02f, 0, .22f, 0, .9f, 0, 0}, 3.0f, .80f, "suspicious"},
  // WORRIED: lids lifted toward the nose.
  {SAME(0, -.02f, .98f, 1.02f, 1, -.04f, 0, .10f, -.18f, 0, 0, 0, .85f, 0, 0), 3.0f, .70f, "worried"},
  // ANNOYED: unimpressed half-lids.
  {SAME(0, .03f, 1.04f, .94f, .9f, 0, 0, .44f, 0, -.04f, .04f, 0, 1, 0, 0), 3.0f, .85f, "annoyed"},
  // LOVE: happy, with heart pupils.
  {SAME(0, -.04f, 1.04f, 1.02f, 1.1f, 0, 0, 0, 0, 0, .08f, .22f, 1.3f, 1, 0), 4.0f, .45f, "love"},
  // DIZZY: spiral pupils, a little lopsided.
  {{0, 0, .98f, 1.0f, 1.1f, .04f, 0, .06f, 0, 0, 0, 0, 1.25f, 0, 1},
   {0, 0, 1.02f, .96f, 1.1f, -.04f, 0, .12f, 0, 0, 0, 0, 1.25f, 0, 1}, 4.0f, .50f, "dizzy"},
};

#undef SAME

inline const Preset &presetOf(Expression e) {
  return PRESETS[e < EXPRESSION_COUNT ? e : NEUTRAL];
}

} // namespace

const EyeShape &expressionShape(Expression e, bool rightEye) {
  const Preset &p = presetOf(e);
  return rightEye ? p.right : p.left;
}

void expressionMotion(Expression e, float &freq, float &zeta) {
  freq = presetOf(e).freq;
  zeta = presetOf(e).zeta;
}

const char *expressionName(Expression e) { return presetOf(e).name; }
