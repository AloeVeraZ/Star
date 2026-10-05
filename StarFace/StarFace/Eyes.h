#pragma once
#include "AnimMath.h"
#include "Blinker.h"
#include "EyeRenderer.h"
#include "EyeShape.h"
#include "Expressions.h"
#include "FaceConfig.h"
#include "GazeController.h"
#include "IdleBehavior.h"

// The eye animation system. Call update() and draw() every frame; everything
// else is optional and can be called at any time. Every change is animated:
// shapes, lids, gaze and openness move on springs, so any state blends
// smoothly into any other.
//
//   eyes.setExpression(HAPPY);      // or SAD, ANGRY, SURPRISED, SLEEPY, SQUINT, CURIOUS ...
//   eyes.lookAt(0.6f, -0.2f);       // -1..1: x right, y down
//   eyes.blink();
//
// Left alone, the eyes look around, blink at random and show brief
// micro-expressions by themselves (see IdleBehavior). An explicit lookAt()
// takes over the gaze; with idle behaviour on, the eyes wander again
// IDLE_RESUME_MS after the last call (call it every frame to keep a target,
// e.g. a finger); with setIdle(false) they stay where they were told.
class Eyes {
 public:
  static constexpr uint32_t IDLE_RESUME_MS = 2500;

  void begin(EyeRenderer *renderer, uint32_t seed, uint32_t now);

  // ---- Expressions ----
  // intensity scales how strong the expression is (0 neutral .. 1 full,
  // up to ~1.3 exaggerated); speed scales how quickly it morphs in.
  void setExpression(Expression e, float intensity = 1.0f, float speed = 1.0f);
  // A blend of two expressions, t = 0 (a) .. 1 (b), e.g. FOCUSED turning ANGRY.
  void setExpressionMix(Expression a, Expression b, float t, float intensity = 1.0f, float speed = 1.0f);
  Expression expression() const { return exprA; }

  // ---- Gaze ----
  void lookAt(float x, float y, float speed = 1.0f);  // -1..1 (x right, y down)
  void lookAtPoint(float sx, float sy, float speed = 1.0f); // a point on the screen, px
  void lookCenter() { lookAt(0, 0); }
  void lookAround() { lookOwned = false; }             // hand the gaze back to idle behaviour

  // ---- Blinks ----
  void blink(Blinker::Type type = Blinker::NORMAL) { blinker.blink(now, type); }
  void wink(int eye) { blinker.wink(now, eye); }      // 0 left, 1 right (as seen)
  void setAutoBlink(bool on) { blinker.setAuto(on); }

  // ---- Idle behaviour (look-arounds, micro-expressions, saccades) ----
  void setIdle(bool on) { idleOn = on; }
  // How restless the idle looking-around is (default IDLE_LIVELINESS), and how
  // often it looks back at you (0..1).
  void setLiveliness(float live, float eyeContact = 0) { idle.liveliness = live; idle.eyeContact = eyeContact; }

  // ---- Lower-level controls (used by CreatureAnimator) ----
  // Lid opening on top of the expression: 0 shut, 1 the expression's own, >1 wider.
  void setOpenness(float left, float right, float speed = 1.0f) {
    openS[0].target = left; openS[1].target = right; openSpeed = speed;
  }
  void setOpenness(float both, float speed = 1.0f) { setOpenness(both, both, speed); }
  void snapOpenness(float both) { openS[0].snap(both); openS[1].snap(both); }
  float openness(int eye) const { return openS[eye].pos; }
  void setDrowsiness(float d) { drowsy = anim::clamp01(d); }
  void setEyeColor(uint32_t rgb) { if (renderer) renderer->setEyeColor(rgb); }
  void setGazeBias(float x, float y) { biasX = x; biasY = y; } // nudges idle looks (tilt)
  void setSaccades(bool on, bool tense = false) { gaze.setSaccades(on, tense); }
  void setBlinkAllowed(bool allowed) { blinker.setAllowed(allowed); }
  void setBlinkPace(float gapScale) { blinkPace = gapScale; }
  void push(float vx, float vy) { bodyX.kick(vx); bodyY.kick(vy); }  // a jolt, px/s
  void setForce(float ax, float ay) { forceX = ax; forceY = ay; }    // a steady push, px/s^2
  void setBodyTarget(float x, float y, float freq = 3.0f, float zeta = .36f) {
    bodyTx = x; bodyTy = y; bodyF = freq; bodyZ = zeta;
  }
  // Procedural offsets for effects (wobble, tremble), eye px and pupil -1..1.
  void setJitter(int eye, float ex, float ey, float px, float py) {
    jit[eye][0] = ex; jit[eye][1] = ey; jit[eye][2] = px; jit[eye][3] = py;
  }
  void kickShape(int eye, EyeShapeField field, float velocity) { shape[eye].kick(field, velocity); }
  void setSpiralSpeed(float radPerSec) { spiralSpeed = radPerSec; }
  // Rolls the whole face (radians, + clockwise), e.g. to stay level with the
  // ground while the star is turned; it follows on a soft, wobbly spring.
  void setFaceRoll(float radians) { rollS.target = radians; }
  // Added straight onto the gaze (-1..1), e.g. the eyes counter-moving while
  // the star swings, so they keep looking at you.
  void setLookOffset(float x, float y) { offX = x; offY = y; }
  // Pupil size on top of the expression: >1 wide and interested, <1 tight.
  void setPupilDilation(float d) { dilateTarget = d; }
  bool blinking() const { return blinker.active(); }

  // ---- Frame ----
  void update(float dt, uint32_t now);
  void draw();
  const EyeFrame *frames() const { return frame; }

 private:
  EyeRenderer *renderer = nullptr;
  EyeFrame frame[2];
  uint32_t seed = 0, now = 0;
  float clock = 0;

  // Expression layer
  Expression exprA = NEUTRAL, exprB = NEUTRAL;
  float exprT = 0, exprIntensity = 1, exprSpeed = 1;
  ShapeSpring shape[2];
  anim::Spring openS[2];
  float openSpeed = 1;

  // Gaze, blinks, idle
  GazeController gaze;
  Blinker blinker;
  IdleBehavior idle;
  bool idleOn = true, lookOwned = false;
  uint32_t lookAtTime = 0;
  float biasX = 0, biasY = 0, drowsy = 0, blinkPace = 1;

  // Physics and effects
  anim::Spring bodyX, bodyY, jitS[2][4];
  float bodyTx = 0, bodyTy = 0, bodyF = 3.0f, bodyZ = .36f;
  float forceX = 0, forceY = 0;
  float jit[2][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}};
  float spiralSpeed = 7.0f, spiralPhase = 0;
  anim::Spring rollS, dilateS;
  float offX = 0, offY = 0, dilateTarget = 1;

  EyeShape targetShape(int eye) const;
  void compose();
  void fitToScreen(EyeFrame &f) const;
};
