#include "Expressions.h"

namespace {

// Field order: x, y, width, height, tilt, bend,
//              topLid, topSlope, topCurve, bottomLid, bottomCurve, pupil, heart, spiral, blush
// x is toward the nose; presets are mirrored for the left eye. Lids are in
// fractions of the eye's height; the eye itself is an oval.
struct Preset {
  EyeShape left, right;
  float freq, zeta;
  const char *name;
};

#define SAME(...) {__VA_ARGS__}, {__VA_ARGS__}

const Preset PRESETS[EXPRESSION_COUNT] = {
  // NEUTRAL: big soft round eyes, wide open and attentive.
  {SAME(0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0), 3.2f, .80f, "neutral"},
  // HAPPY: closed "^" arches, rosy cheeks; the eyes lift a little.
  {SAME(0, -.04f, 1.04f, .98f, 0, 0, 0, 0, 0, .30f, .44f, 0, 0, 0, 1), 5.0f, .50f, "happy"},
  // SAD: lids slant down to the outside, the eyes sink, pupils a touch bigger.
  {SAME(-.03f, .08f, .98f, .95f, -.04f, 0, .14f, -.24f, .02f, .04f, 0, 1.06f, 0, 0, 0), 1.6f, 1.0f, "sad"},
  // ANGRY: lids slam down toward the nose, pupils tighten.
  {SAME(.04f, .04f, 1.03f, .95f, .03f, 0, .16f, .30f, -.03f, .08f, 0, .86f, 0, 0, 0), 6.0f, .55f, "angry"},
  // FOCUSED (determined): narrowed and level, a slight inward set.
  {SAME(.02f, .02f, 1.02f, .95f, 0, 0, .22f, .08f, 0, .16f, -.02f, .9f, 0, 0, 0), 4.0f, .75f, "focused"},
  // SURPRISED: wide and round with small pupils.
  {SAME(0, -.05f, 1.07f, 1.10f, 0, 0, 0, 0, 0, 0, 0, .66f, 0, 0, 0), 8.0f, .38f, "surprised"},
  // SLEEPY: heavy, drooping upper lids.
  {SAME(0, .06f, 1.02f, .96f, -.02f, 0, .46f, -.04f, .05f, .05f, 0, 1.0f, 0, 0, 0), 1.4f, 1.0f, "sleepy"},
  // SQUINT: narrowed from above and below.
  {SAME(0, 0, 1.05f, .97f, 0, 0, .28f, .05f, 0, .26f, -.04f, .92f, 0, 0, 0), 5.0f, .70f, "squint"},
  // CURIOUS: one eye opens wide, the other narrows.
  {{0, .03f, .97f, .94f, 0, 0, .24f, .06f, 0, .07f, 0, .95f, 0, 0, 0},
   {0, -.05f, 1.05f, 1.08f, 0, 0, 0, 0, 0, 0, 0, 1.04f, 0, 0, 0}, 4.0f, .60f, "curious"},
  // CONFUSED: lopsided, one lid down, the eyes tilted against each other.
  {{0, .05f, .98f, .93f, .07f, 0, .30f, .10f, 0, .05f, 0, .92f, 0, 0, 0},
   {0, -.05f, 1.03f, 1.06f, -.05f, 0, 0, -.06f, 0, 0, 0, 1.0f, 0, 0, 0}, 3.5f, .55f, "confused"},
  // SUSPICIOUS: flat heavy lids, a hair uneven.
  {{0, .02f, 1.03f, .95f, 0, 0, .38f, .05f, -.03f, .14f, 0, .9f, 0, 0, 0},
   {0, .02f, 1.03f, .95f, 0, 0, .30f, .02f, -.03f, .20f, 0, .9f, 0, 0, 0}, 3.0f, .80f, "suspicious"},
  // WORRIED: lids lifted toward the nose, small pupils.
  {SAME(0, -.02f, .98f, 1.02f, -.04f, 0, .10f, -.20f, 0, 0, 0, .84f, 0, 0, 0), 3.0f, .70f, "worried"},
  // ANNOYED: unimpressed half-lids.
  {SAME(0, .03f, 1.03f, .96f, 0, 0, .42f, 0, -.04f, .04f, 0, 1, 0, 0, 0), 3.0f, .85f, "annoyed"},
  // LOVE: happy, with heart pupils and rosy cheeks.
  {SAME(0, -.04f, 1.04f, 1.02f, 0, 0, 0, 0, 0, .08f, .20f, 1.05f, 1, 0, 1), 4.0f, .45f, "love"},
  // DIZZY: spiral pupils, a little lopsided.
  {{0, 0, .98f, 1.0f, .04f, 0, .06f, 0, 0, 0, 0, 1.08f, 0, 1, 0},
   {0, 0, 1.02f, .97f, -.04f, 0, .12f, 0, 0, 0, 0, 1.08f, 0, 1, 0}, 4.0f, .50f, "dizzy"},
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
