#pragma once
#include <stdint.h>
#include "EyeShape.h"

// Every expression the eyes can make. setExpression() morphs to any of these
// from wherever the eyes are; see Expressions.cpp for the shapes.
enum Expression : uint8_t {
  NEUTRAL,
  HAPPY,       // crescent "smiling" eyes
  SAD,         // lids slant down to the outside, eyes sink
  ANGRY,       // lids slam down toward the nose (angry / determined glare)
  FOCUSED,     // determined: narrowed and level
  SURPRISED,   // wide, round, small pupils
  SLEEPY,      // heavy, drooping lids
  SQUINT,      // narrowed from above and below
  CURIOUS,     // one eye opens wide, the other narrows
  CONFUSED,    // lopsided, one lid down, eyes tilted
  SUSPICIOUS,  // flat, heavy lids
  WORRIED,     // lids lifted in the middle toward the nose
  ANNOYED,     // unimpressed half-lids
  LOVE,        // happy, with heart pupils
  DIZZY,       // spiral pupils, a little lopsided
  EXPRESSION_COUNT
};

// Shape of one eye for an expression. rightEye selects the eye for
// asymmetric expressions (CURIOUS, CONFUSED, DIZZY ...).
const EyeShape &expressionShape(Expression e, bool rightEye);

// How quickly the eyes morph into an expression (Hz) and how springy the
// arrival is (damping ratio: 1 settles smoothly, lower overshoots a little).
void expressionMotion(Expression e, float &freq, float &zeta);

const char *expressionName(Expression e);
