#include "Eyes.h"

using namespace anim;

namespace {

constexpr float MIN_HALF_HEIGHT = 2.2f;     // a shut eye is a thin line this thick (px, at 240 px)
constexpr float CLOSE_LINE_DROP = 0.22f;    // the shut line sits below centre: the upper lid travels further
constexpr float CLOSED_BEND = 0.10f;        // a closing eye curves into a soft "^" line (half-heights)
constexpr float CURIOUS_GROW = 0.07f;       // the eye on the side being looked toward grows this much
constexpr float INERTIA_LIMIT_X = 0.075f;   // physical sway limits, of screen size
constexpr float INERTIA_LIMIT_Y = 0.065f;
constexpr float PX = SCREEN_MIN_SIDE / 240.0f; // layout constants are authored at 240 px

} // namespace

void Eyes::begin(EyeRenderer *r, uint32_t unitSeed, uint32_t t) {
  renderer = r;
  seed = unitSeed;
  now = t;
  for (int i = 0; i < 2; ++i) {
    shape[i].snap(expressionShape(NEUTRAL, i == 1));
    openS[i].snap(1);
  }
  blinker.begin(now);
  idle.begin(now);
  compose();
}

void Eyes::setExpression(Expression e, float intensity, float speed) {
  setExpressionMix(e, e, 0, intensity, speed);
}

void Eyes::setExpressionMix(Expression a, Expression b, float t, float intensity, float speed) {
  Expression before = exprA;
  exprA = a;
  exprB = b;
  exprT = clamp01(t);
  exprIntensity = intensity;
  exprSpeed = speed;
  if (a == before) return;
  // Arrival accents, so a new expression lands with a little life.
  float k = clampf(intensity, 0.0f, 1.3f) * EXPRESSION_INTENSITY;
  for (int i = 0; i < 2; ++i) {
    switch (a) {
      case SURPRISED: shape[i].kick(ES_HEIGHT, 1.6f * k); shape[i].kick(ES_WIDTH, .6f * k); break;
      case ANGRY:     shape[i].kick(ES_TOP, 2.2f * k); shape[i].kick(ES_X, .5f * k); break;
      case HAPPY:
      case LOVE:      shape[i].kick(ES_Y, -.9f * k); shape[i].kick(ES_HEIGHT, .5f * k); break;
      case SQUINT:
      case FOCUSED:   shape[i].kick(ES_WIDTH, .5f * k); break;
      default: break;
    }
  }
}

void Eyes::lookAt(float x, float y, float speed) {
  lookOwned = true;
  lookAtTime = now;
  gaze.lookAt(x, y, speed);
}

void Eyes::lookAtPoint(float sx, float sy, float speed) {
  lookAt((sx - EYE_CENTER_X) / (SCREEN_MIN_SIDE * .33f), (sy - EYE_CENTER_Y) / (SCREEN_MIN_SIDE * .33f), speed);
}

EyeShape Eyes::targetShape(int eye) const {
  const bool right = eye == 1;
  EyeShape s = eyeshape::mix(expressionShape(exprA, right), expressionShape(exprB, right), exprT);
  // Idle micro-expressions only flavour the calm face.
  if (idleOn && exprA == NEUTRAL && exprB == NEUTRAL && idle.quirk != NEUTRAL)
    s = eyeshape::mix(s, expressionShape(idle.quirk, right), idle.quirkAmount);
  // Getting drowsy: heavy lids creep in.
  if (drowsy > 0) s = eyeshape::mix(s, expressionShape(SLEEPY, right), .7f * drowsy);
  return eyeshape::amplify(s, exprIntensity * EXPRESSION_INTENSITY);
}

void Eyes::update(float dt, uint32_t t) {
  now = t;
  dt = clampf(dt, .0005f, .05f);      // a lag spike never becomes a jump
  const float sdt = dt * ANIMATION_SPEED;
  clock += dt;

  // Idle: wander the gaze unless someone looked somewhere on purpose lately.
  if (idleOn && lookOwned && int32_t(now - lookAtTime) > int32_t(IDLE_RESUME_MS)) lookOwned = false;
  bool bigJump = false;
  if (idleOn) {
    idle.update(now, drowsy, bigJump);
    if (!lookOwned) {
      gaze.lookAt(clampf(idle.gazeX * .8f + biasX, -1.0f, 1.0f),
                  clampf(idle.gazeY * .8f + biasY, -1.0f, 1.0f), idle.fast ? 1.45f : 1.0f);
      // Big gaze shifts often carry a blink, as they do in people.
      if (bigJump && !blinker.active() && chance(.3f)) blinker.blink(now);
    }
  } else if (!lookOwned) {
    gaze.lookAt(biasX, biasY);
  }

  blinker.setHeaviness(1.0f + .7f * drowsy);
  blinker.setPace(blinkPace * (1.0f + .5f * drowsy));
  blinker.update(now);

  float freq, zeta;
  expressionMotion(exprA, freq, zeta);
  freq *= EXPRESSION_BLEND_SPEED * exprSpeed;
  for (int i = 0; i < 2; ++i) {
    shape[i].setTarget(targetShape(i));
    shape[i].update(sdt, freq, zeta);
    openS[i].update(sdt, 5.0f * openSpeed, openSpeed > 1.5f ? .6f : 1.0f);
  }
  gaze.update(sdt, now, drowsy);

  bodyX.target = bodyTx;
  bodyY.target = bodyTy;
  bodyX.update(sdt, bodyF, bodyZ, forceX);
  bodyY.update(sdt, bodyF, bodyZ, forceY);
  bodyX.pos = clampf(bodyX.pos, -INERTIA_LIMIT_X * SCREEN_MIN_SIDE, INERTIA_LIMIT_X * SCREEN_MIN_SIDE);
  bodyY.pos = clampf(bodyY.pos, -INERTIA_LIMIT_Y * SCREEN_MIN_SIDE, INERTIA_LIMIT_Y * SCREEN_MIN_SIDE);
  for (int i = 0; i < 2; ++i)
    for (int c = 0; c < 4; ++c) {
      jitS[i][c].target = jit[i][c];
      jitS[i][c].update(sdt, 16, .8f);
    }
  spiralPhase = fmodf(spiralPhase + spiralSpeed * dt, TAU_F * 64);
  compose();
}

void Eyes::draw() {
  if (renderer) renderer->draw(frame);
}

// ---- Combine every layer into the final pixel geometry ----

void Eyes::compose() {
  const float breath = (.7f * sinf(clock * TAU_F / 3.8f) + .3f * noise1(clock * .6f, seed + 3)) * PX;
  for (int i = 0; i < 2; ++i) {
    const EyeShape s = shape[i].value();
    const float side = i == 0 ? -1.0f : 1.0f;   // the left eye sits left of centre
    const float inward = -side;                 // screen direction toward the nose
    const float c = clampf(blinker.closure(i), -.1f, 1.0f);

    // Size: the expression, a curious lean toward the gaze, squash and stretch.
    const float tx = gaze.travelX(i), ty = gaze.travelY(i);
    const float look = clampf(tx * side, -1.0f, 1.0f);
    const float grow = 1.0f + CURIOUS_GROW * fmaxf(0.0f, look) - .4f * CURIOUS_GROW * fmaxf(0.0f, -look);
    const float vx = gaze.travelVelX(i) * MAX_EYE_MOVE_X * SCREEN_MIN_SIDE + bodyX.vel;
    const float vy = gaze.travelVelY(i) * MAX_EYE_MOVE_Y * SCREEN_MIN_SIDE + bodyY.vel;
    const float sx = clampf(fabsf(vx) * (SQUASH_STRETCH / (1600.0f * PX)), 0.0f, .08f);
    const float sy = clampf(fabsf(vy) * (SQUASH_STRETCH / (1600.0f * PX)), 0.0f, .08f);
    float hw = EYE_HALF_WIDTH * s.width * grow * (1.0f + sx - .5f * sy);
    float hhFull = EYE_HALF_HEIGHT * s.height * grow * (1.0f + sy - .5f * sx);
    hw = fminf(hw, EYE_HALF_WIDTH * MAX_EXPRESSION_EXPANSION);
    hhFull = fminf(hhFull, EYE_HALF_HEIGHT * MAX_EXPRESSION_EXPANSION);

    // Opening: the expression's own lids, the openness override and blinks.
    const float open = clampf(openS[i].pos * (1.0f - c), 0.0f, 1.25f);
    const float hh = fmaxf(MIN_HALF_HEIGHT * PX, fminf(hhFull * open, EYE_HALF_HEIGHT * MAX_EXPRESSION_EXPANSION));
    hw *= 1.0f + .05f * fmaxf(0.0f, c);          // lids pressing shut spread the eye a little
    // Lids melt away as the eye shuts, so a closed eye is one clean line.
    const float lidK = smoothstep(.08f, .55f, open);

    EyeFrame &f = frame[i];
    f.rightEye = i == 1;
    f.cx = EYE_CENTER_X + side * EYE_HALF_SPACING + s.x * inward * EYE_HALF_WIDTH
           + tx * MAX_EYE_MOVE_X * SCREEN_MIN_SIDE + bodyX.pos + jitS[i][0].pos;
    f.cy = EYE_CENTER_Y + s.y * EYE_HALF_HEIGHT + ty * MAX_EYE_MOVE_Y * SCREEN_MIN_SIDE + breath
           + bodyY.pos + jitS[i][1].pos + (hhFull - hh) * CLOSE_LINE_DROP;
    f.hw = hw;
    f.hh = hh;
    f.radius = clampf(EYE_ROUNDNESS * s.round, 0.0f, 1.0f) * fminf(hw, hhFull);
    f.tilt = s.tilt;
    f.bend = (s.bend + CLOSED_BEND * fmaxf(0.0f, c)) * EYE_HALF_HEIGHT;

    // Lids as quadratics in the eye's frame (x toward the nose, normalised xn = x / hw).
    const float T = s.topLid * lidK, Ts = s.topSlope * lidK, Tc = s.topCurve * lidK;
    const float B = s.bottomLid * lidK, Bc = s.bottomCurve * lidK;
    // Rounding where a lid meets the outline (kept small on a thin, nearly shut eye).
    f.lidRound = fminf(clampf(.16f * hhFull, 2.0f * PX, 9.0f * PX), .45f * hh);
    // A lid at rest sits half a rounding outside the eye, so it leaves the outline untouched.
    const float H2 = 2.0f * hh, iw = 1.0f / hw, rest = .5f * f.lidRound;
    f.topA = -hh - rest + H2 * (T + Tc);
    f.topB = H2 * Ts * iw;
    f.topC = -H2 * Tc * iw * iw;
    f.botA = hh + rest - H2 * (B + Bc);
    f.botB = 0;
    f.botC = H2 * Bc * iw * iw;

    // Pupil: follows the gaze inside the eye, centred in whatever the lids leave open.
    float pulse = 1.0f + .07f * s.heart * fmaxf(0.0f, sinf(clock * TAU_F * 1.5f)); // a beating heart
    // (A pupil shrinking away vanishes before it becomes a stray dot.)
    float pr = PUPIL_SIZE * EYE_HALF_WIDTH * s.width * grow * s.pupil * smoothstep(.2f, .45f, s.pupil) * pulse;
    pr *= smoothstep(.10f, .40f, open);              // a shut eye is one clean line
    const float squash = clampf(hh / hhFull, .05f, 1.0f);
    const float roomX = fmaxf(0.0f, hw - pr * .85f) * MAX_PUPIL_MOVE;
    const float roomY = fmaxf(0.0f, hhFull - pr * .85f) * MAX_PUPIL_MOVE;
    const float openTop = fmaxf(-hh, f.topA), openBottom = fminf(hh, f.botA);
    f.pupilR = pr;
    f.pupilSquash = squash;
    f.pupilX = (gaze.pupilX(i) + jitS[i][2].pos) * roomX;
    f.pupilY = ((gaze.pupilY(i) + jitS[i][3].pos) * roomY) * squash + .5f * (openTop + openBottom);
    f.heart = s.heart;
    f.spiral = s.spiral;
    f.spiralPhase = spiralPhase * side + i * 1.3f;   // the two spirals turn opposite ways
    fitToScreen(f);
  }
}

// ---- Keep each eye on screen. On a round panel a soft knee eases the eye in
// before the safe circle; the excess is absorbed by nudging it toward the
// centre and a small shrink, so it looks like it presses against the glass,
// never cropped. ----

static void scaleFrame(EyeFrame &f, float k) {
  f.hw *= k; f.hh *= k; f.radius *= k; f.bend *= k;
  f.topA *= k; f.topC /= k; f.botA *= k; f.botC /= k;
  f.lidRound *= k; f.pupilR *= k; f.pupilX *= k; f.pupilY *= k;
}

void Eyes::fitToScreen(EyeFrame &f) const {
  const float r = fminf(f.radius, fminf(f.hw, f.hh));
  const float cs = cosf(f.tilt), sn = sinf(f.tilt), mirror = f.rightEye ? -1.0f : 1.0f;
  const float bendK = f.bend / (f.hw * f.hw);
  float best = -1, fx = 0, fy = 0, reach = 0;
  float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
  for (int k = 0; k < 4; ++k) {
    // Corner-circle centres of the (bent, tilted) rounded box.
    float lx = (k & 1 ? 1 : -1) * (f.hw - r);
    float ly = (k & 2 ? 1 : -1) * (f.hh - r) + bendK * lx * lx;
    float dx = (lx * cs - ly * sn) * mirror, dy = lx * sn + ly * cs;
    float px = f.cx + dx - SCREEN_CX, py = f.cy + dy - SCREEN_CY;
    float d = sqrtf(px * px + py * py) + r;
    if (d > best) { best = d; fx = px; fy = py; reach = sqrtf(dx * dx + dy * dy) + r; }
    minX = fminf(minX, f.cx + dx - r); maxX = fmaxf(maxX, f.cx + dx + r);
    minY = fminf(minY, f.cy + dy - r); maxY = fmaxf(maxY, f.cy + dy + r);
  }
  if (!SCREEN_IS_ROUND) {
    const float m = SAFE_MARGIN * SCREEN_MIN_SIDE;
    float w = maxX - minX, h = maxY - minY;
    float k = fminf(1.0f, fminf((SCREEN_WIDTH - 2 * m) / fmaxf(1.0f, w), (SCREEN_HEIGHT - 2 * m) / fmaxf(1.0f, h)));
    if (k < 1.0f) scaleFrame(f, k);
    if (minX < m) f.cx += m - minX;
    if (maxX > SCREEN_WIDTH - 1 - m) f.cx -= maxX - (SCREEN_WIDTH - 1 - m);
    if (minY < m) f.cy += m - minY;
    if (maxY > SCREEN_HEIGHT - 1 - m) f.cy -= maxY - (SCREEN_HEIGHT - 1 - m);
    return;
  }
  const float soft = 10.0f * PX;
  const float knee = SAFE_RADIUS - soft;
  if (best <= knee) return;
  const float allowed = knee + soft * tanhf((best - knee) / soft);
  const float excess = best - allowed;
  const float len = sqrtf(fx * fx + fy * fy);
  if (len < 1e-3f) return;
  const float ux = fx / len, uy = fy / len;
  // Overflow at the sides is mostly absorbed by shrinking (moving inward would
  // crowd the pair together); at the top and bottom by moving.
  const float move = excess * mix(.3f, .8f, uy * uy);
  f.cx -= ux * move;
  f.cy -= uy * move;
  if (reach > 1.0f) scaleFrame(f, clampf(1.0f - (excess - move) / reach, .85f, 1.0f));
}
