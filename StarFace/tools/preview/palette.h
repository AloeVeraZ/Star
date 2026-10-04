// Shared by the preview and the soak test: every pixel on screen must be a
// blend of black, EYE_COLOR and PUPIL_COLOR (anti-aliased edges), or the
// blush on black, never any other hue.
#pragma once
#include <math.h>
#include <stdint.h>
#include "FaceConfig.h"

// px = s * C (black blended with one colour)?
inline bool onRay(const float px[3], uint32_t col) {
  const float C[3] = {float((col >> 16) & 255), float((col >> 8) & 255), float(col & 255)};
  float cc = C[0] * C[0] + C[1] * C[1] + C[2] * C[2], pc = px[0] * C[0] + px[1] * C[1] + px[2] * C[2];
  float s = pc / cc;
  s = s < 0 ? 0 : (s > 1 ? 1 : s);
  float worst = 0;
  for (int i = 0; i < 3; ++i) worst = fmaxf(worst, fabsf(s * C[i] - px[i]));
  return worst <= 14.0f;
}

// px on the line between EYE_COLOR and PUPIL_COLOR (an iris edge)?
inline bool onEdge(const float px[3]) {
  const float E[3] = {float((EYE_COLOR >> 16) & 255), float((EYE_COLOR >> 8) & 255), float(EYE_COLOR & 255)};
  const float P[3] = {float((PUPIL_COLOR >> 16) & 255), float((PUPIL_COLOR >> 8) & 255), float(PUPIL_COLOR & 255)};
  float dd = 0, pd = 0;
  for (int i = 0; i < 3; ++i) { float d = E[i] - P[i]; dd += d * d; pd += (px[i] - P[i]) * d; }
  float t = pd / dd;
  t = t < 0 ? 0 : (t > 1 ? 1 : t);
  float worst = 0;
  for (int i = 0; i < 3; ++i) worst = fmaxf(worst, fabsf(P[i] + t * (E[i] - P[i]) - px[i]));
  return worst <= 14.0f;
}

inline bool inPalette(uint16_t v) {
  uint16_t c = uint16_t((v << 8) | (v >> 8));
  const float px[3] = {((c >> 11) & 31) * 255.0f / 31, ((c >> 5) & 63) * 255.0f / 63, (c & 31) * 255.0f / 31};
  if (CHEEK_BLUSH && onRay(px, CHEEK_COLOR)) return true;   // the blush on black
  const float E[3] = {float((EYE_COLOR >> 16) & 255), float((EYE_COLOR >> 8) & 255), float(EYE_COLOR & 255)};
  const float P[3] = {float((PUPIL_COLOR >> 16) & 255), float((PUPIL_COLOR >> 8) & 255), float(PUPIL_COLOR & 255)};
  // Least squares for px = a*E + b*P, then keep a, b in the blend triangle.
  float ee = 0, pp = 0, ep = 0, ce = 0, cp = 0;
  for (int i = 0; i < 3; ++i) {
    ee += E[i] * E[i]; pp += P[i] * P[i]; ep += E[i] * P[i]; ce += px[i] * E[i]; cp += px[i] * P[i];
  }
  float det = ee * pp - ep * ep;
  float a = (ce * pp - cp * ep) / det, b = (cp * ee - ce * ep) / det;
  // Outside the blend triangle: the closest point is on one of its edges.
  if (a < 0 || b < 0 || a + b > 1) return onRay(px, EYE_COLOR) || onRay(px, PUPIL_COLOR) || onEdge(px);
  float worst = 0;
  for (int i = 0; i < 3; ++i) worst = fmaxf(worst, fabsf(a * E[i] + b * P[i] - px[i]));
  return worst <= 14.0f; // RGB565 rounding
}
