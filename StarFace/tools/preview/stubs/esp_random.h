#pragma once
#include <stdint.h>
// Deterministic xorshift so previews are reproducible.
inline uint32_t &previewRandomState() { static uint32_t s = 0x9E3779B9u; return s; }
inline uint32_t esp_random() {
  uint32_t &s = previewRandomState();
  s ^= s << 13; s ^= s >> 17; s ^= s << 5;
  return s;
}
