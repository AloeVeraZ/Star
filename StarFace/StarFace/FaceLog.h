#pragma once
#include "FaceConfig.h"

// Keep startup, driver and gesture messages out of the raw sensor stream.
#define FACE_LOG(method, ...) do { \
  if (!RAW_ACCEL_SERIAL_ONLY) Serial.method(__VA_ARGS__); \
} while (0)
