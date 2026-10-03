#pragma once
#include "AnimMath.h"
#include "FaceConfig.h"

// Where the eyes look. Two layers move toward every target with springs (so
// they accelerate, decelerate and settle, never jump):
//   * the pupils lead, quickly and with a little overshoot;
//   * the whole eyes travel after them a beat later, without overshoot, like
//     a head turning after the eyes.
// The second eye follows a fraction later than the first, and tiny saccades
// (1-3 px hops) keep a held gaze from looking frozen.
class GazeController {
 public:
  // x, y in -1..1: -1 left / up, +1 right / down. speed scales the motion.
  void lookAt(float x, float y, float speed = 1.0f) {
    tx = anim::clampf(x, -1.0f, 1.0f);
    ty = anim::clampf(y, -1.0f, 1.0f);
    this->speed = speed;
  }
  // tense: quicker, smaller saccades (angry, anxious); off: none (scripted).
  void setSaccades(bool on, bool tense = false) { sacOn = on; this->tense = tense; }
  void setDamping(float zeta) { this->zeta = zeta; }

  void update(float dt, uint32_t now, float drowsy) {
    using anim::frand;
    if (int32_t(now - nextSaccadeAt) >= 0) {
      if (sacOn && anim::chance(.75f)) {
        float a = frand(0, anim::TAU_F), d = frand(.08f, .25f) * (tense ? .7f : 1.0f);
        sacX.target = cosf(a) * d;
        sacY.target = sinf(a) * d * .7f;
      } else {
        sacX.target = sacY.target = 0;
      }
      uint32_t gap = tense ? anim::randMs(110, 320) : anim::randMs(350, 1400);
      nextSaccadeAt = now + uint32_t(gap * (1.0f + drowsy));
    }
    if (!sacOn) sacX.target = sacY.target = 0;
    sacX.update(dt, 16, .85f);
    sacY.update(dt, 16, .85f);

    const float f = 4.6f * GAZE_SPEED * speed * (1.0f - .45f * drowsy);
    for (int i = 0; i < 2; ++i) {
      float fi = i ? f * .88f : f;
      pupX[i].target = tx; pupY[i].target = ty;
      pupX[i].update(dt, fi * 1.15f, zeta);
      pupY[i].update(dt, fi * 1.15f, zeta);
      travX[i].target = tx; travY[i].target = ty;
      travX[i].update(dt, fi * .5f, .92f);
      travY[i].update(dt, fi * .5f, .92f);
    }
  }

  // Pupil position in its eye, -1..1 (overshoot can briefly exceed it).
  float pupilX(int eye) const { return pupX[eye].pos + sacX.pos; }
  float pupilY(int eye) const { return pupY[eye].pos + sacY.pos; }
  // Whole-eye travel, -1..1, and its velocity (for squash and stretch).
  float travelX(int eye) const { return travX[eye].pos + sacX.pos * .25f; }
  float travelY(int eye) const { return travY[eye].pos + sacY.pos * .25f; }
  float travelVelX(int eye) const { return travX[eye].vel; }
  float travelVelY(int eye) const { return travY[eye].vel; }
  float targetX() const { return tx; }
  float targetY() const { return ty; }

 private:
  anim::Spring pupX[2], pupY[2], travX[2], travY[2], sacX, sacY;
  float tx = 0, ty = 0, speed = 1, zeta = .68f;
  bool sacOn = true, tense = false;
  uint32_t nextSaccadeAt = 0;
};
