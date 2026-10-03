// Minimal Arduino shim so the eye renderer and animator build on a desktop.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <initializer_list>
template <class T> static inline T min(T a, T b) { return a < b ? a : b; }
template <class T> static inline T max(T a, T b) { return a > b ? a : b; }
template <class T, class L, class H> static inline T constrain(T v, L lo, H hi) {
  return v < lo ? T(lo) : (v > hi ? T(hi) : v);
}
