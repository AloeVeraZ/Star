#include "Eyes.h"

using namespace anim;

namespace {

constexpr float CLOSED_GAP = 3.2f;          // a shut eye is a soft line this thick (px, at 240 px)
constexpr float CLOSE_LINE_Y = 0.22f;       // the lids meet below centre (the upper lid travels further)...
constexpr float CLOSE_LINE_CURVE = -0.16f;  // ...along a gentle smile-like curve (ends lift), in half-heights
constexpr float LID_FOLLOWS_GAZE = 0.09f;   // the upper lid lowers as the eyes look down, like real lids
constexpr float LID_ARCH = 0.40f;           // lids curve around the eyeball instead of cutting straight
constexpr float CURIOUS_GROW = 0.06f;       // the eye on the side being looked toward grows this much
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
  rollS.snap(0);
  dilateS.snap(1);
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
      // A clear tilt takes over the gaze; idle glances shrink around it.
      float roam = .8f * (1.0f - .6f * fminf(1.0f, sqrtf(biasX * biasX + biasY * biasY)));
      gaze.lookAt(clampf(idle.gazeX * roam + biasX, -1.0f, 1.0f),
                  clampf(idle.gazeY * roam + biasY, -1.0f, 1.0f), idle.fast ? 1.45f : 1.0f);
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
  rollS.target = clampf(rollS.target, -FACE_ROLL_MAX, FACE_ROLL_MAX);
  rollS.update(sdt, 2.3f, .45f);        // a soft pendulum: it swings level and settles
  dilateS.target = dilateTarget;
  dilateS.update(sdt, 2.0f, .9f);
  compose();
}

void Eyes::draw() {
  if (renderer) renderer->draw(frame);
}

// ---- Combine every layer into the final pixel geometry ----

void Eyes::compose() {
  const float breath = (.7f * sinf(clock * TAU_F / 3.8f) + .3f * noise1(clock * .6f, seed + 3)) * PX;
  const float roll = rollS.pos + FACE_ANGLE_DEG * (PI_F / 180.0f), rc = cosf(roll), rs = sinf(roll);
  for (int i = 0; i < 2; ++i) {
    const EyeShape s = shape[i].value();
    const float side = i == 0 ? -1.0f : 1.0f;   // the left eye sits left of centre
    const float inward = -side;                 // screen direction toward the nose
    const float blink = clampf(blinker.closure(i), -.1f, 1.0f);

    // Gaze: the pupils lead, the eyes follow; plus offsets (swing reflex) and
    // a faint fixation tremor that keeps a held look alive.
    const float tremor = .012f * PUPIL_LIFE;
    const float gx = gaze.pupilX(i) + jitS[i][2].pos + offX + tremor * noise1(clock * 23.0f, seed + 41 + i);
    const float gy = gaze.pupilY(i) + jitS[i][3].pos + offY + tremor * noise1(clock * 21.0f, seed + 51 + i);
    const float tx = gaze.travelX(i) + offX * .3f, ty = gaze.travelY(i) + offY * .3f;

    // Size: the expression, a curious lean toward the gaze, squash and stretch.
    const float look = clampf(tx * side, -1.0f, 1.0f);
    const float grow = 1.0f + CURIOUS_GROW * fmaxf(0.0f, look) - .4f * CURIOUS_GROW * fmaxf(0.0f, -look);
    const float vx = gaze.travelVelX(i) * MAX_EYE_MOVE_X * SCREEN_MIN_SIDE + bodyX.vel;
    const float vy = gaze.travelVelY(i) * MAX_EYE_MOVE_Y * SCREEN_MIN_SIDE + bodyY.vel;
    const float sqx = clampf(fabsf(vx) * (SQUASH_STRETCH / (1600.0f * PX)), 0.0f, .07f);
    const float sqy = clampf(fabsf(vy) * (SQUASH_STRETCH / (1600.0f * PX)), 0.0f, .07f);
    const float open = clampf(openS[i].pos, 0.0f, 1.3f);
    const float wide = 1.0f + .5f * fmaxf(0.0f, open - 1.0f) - .25f * fminf(0.0f, blink); // wide-eyed / the lift after a blink
    float rx = EYE_HALF_WIDTH * s.width * grow * (1.0f + sqx - .5f * sqy);
    float ry = EYE_HALF_HEIGHT * s.height * grow * wide * (1.0f + sqy - .5f * sqx);
    rx = fminf(rx, EYE_HALF_WIDTH * MAX_EXPRESSION_EXPANSION);
    ry = fminf(ry, EYE_HALF_HEIGHT * MAX_EXPRESSION_EXPANSION);

    // Closing: lids slide over the eye (blinks, sleep, wake). The lids of the
    // expression blend into a pair meeting along a soft curve below centre.
    const float c = clampf(1.0f - (1.0f - fmaxf(0.0f, blink)) * fminf(open, 1.0f), 0.0f, 1.0f);
    const float ce = c * c * (3.0f - 2.0f * c);

    EyeFrame &f = frame[i];
    f.rightEye = i == 1;
    float ox = side * EYE_HALF_SPACING + s.x * inward * EYE_HALF_WIDTH
               + tx * MAX_EYE_MOVE_X * SCREEN_MIN_SIDE + bodyX.pos + jitS[i][0].pos;
    float oy = EYE_CENTER_Y - SCREEN_CY + s.y * EYE_HALF_HEIGHT + ty * MAX_EYE_MOVE_Y * SCREEN_MIN_SIDE
               + breath + bodyY.pos + jitS[i][1].pos;
    // The whole face rolls about the screen centre.
    f.cx = SCREEN_CX + ox * rc - oy * rs;
    f.cy = SCREEN_CY + ox * rs + oy * rc;
    f.rx = rx;
    f.ry = ry;
    f.tilt = s.tilt + (f.rightEye ? -roll : roll);
    f.bend = s.bend * EYE_HALF_HEIGHT;
    f.lidRound = clampf(.14f * ry, 2.0f * PX, 8.0f * PX);

    // Lids as quadratics in the eye's frame (x toward the nose, xn = x / rx).
    const float T = s.topLid + LID_FOLLOWS_GAZE * fmaxf(0.0f, gy), Ts = s.topSlope;
    const float Tc = s.topCurve - LID_ARCH * fmaxf(0.0f, T);      // an upper lid arches over the eye
    const float B = s.bottomLid, Bc = s.bottomCurve - .5f * LID_ARCH * fmaxf(0.0f, B);
    const float H2 = 2.0f * ry, iw = 1.0f / rx, rest = .5f * f.lidRound;
    const float oTA = -ry - rest + H2 * (T + Tc), oTB = H2 * Ts * iw, oTC = -H2 * Tc * iw * iw;
    const float oBA = ry + rest - H2 * (B + Bc), oBC = H2 * Bc * iw * iw;
    const float gap = .5f * CLOSED_GAP * PX;
    const float cY = CLOSE_LINE_Y * ry, cC = CLOSE_LINE_CURVE * ry * iw * iw;
    // The lid-corner rounding sharpens as the lids meet, or it would swallow the thin shut line.
    f.lidRound = mix(f.lidRound, .5f * PX, ce);
    f.topA = mix(oTA, cY - gap, ce);
    f.topB = mix(oTB, 0.0f, ce);
    f.topC = mix(oTC, cC, ce);
    f.botA = mix(oBA, cY + gap, ce);
    f.botB = 0;
    f.botC = mix(oBC, cC, ce);

    // Pupil: big and round (friendly), gently breathing in size, centred in
    // whatever the lids leave open, and narrower as it turns toward the side
    // of the eyeball. It tucks away as the lids meet, so a shut eye is one
    // clean line.
    const float pulse = 1.0f + .07f * s.heart * fmaxf(0.0f, sinf(clock * TAU_F * 1.5f)); // a beating heart
    const float life = 1.0f + .035f * PUPIL_LIFE * noise1(clock * .25f, seed + 61);
    float pr = PUPIL_SIZE * EYE_HALF_WIDTH * s.width * grow * s.pupil * smoothstep(.2f, .45f, s.pupil)
               * pulse * life * clampf(dilateS.pos, .5f, 1.4f);
    pr *= 1.0f - smoothstep(.45f, .72f, c);
    const float roomX = fmaxf(0.0f, rx - pr * .9f) * MAX_PUPIL_MOVE;
    const float roomY = fmaxf(0.0f, ry - pr * .9f) * MAX_PUPIL_MOVE;
    const float openTop = fmaxf(-ry, f.topA), openBottom = fminf(ry, f.botA);
    float px = clampf(gx, -1.15f, 1.15f) * roomX;
    float py = clampf(gy, -1.15f, 1.15f) * roomY + .45f * (openTop + openBottom);
    f.pupilR = pr;
    f.coreR = PUPIL_CORE * pr * (1.0f - s.heart) * (1.0f - s.spiral);
    f.pupilSX = 1.0f - .20f * fminf(1.0f, gx * gx);
    f.pupilSY = (1.0f - .14f * fminf(1.0f, gy * gy)) * (1.0f - .25f * ce);
    // Catch-lights: fixed toward the light (upper right), so they slide across
    // the pupil as it moves; that is what makes the eyes look wet and alive.
    const float glintHide = (1.0f - s.spiral) * (1.0f - .5f * s.heart);
    float g1x = px * .55f + .34f * pr, g1y = py * .55f - .40f * pr;
    float g2x = px * .55f - .30f * pr, g2y = py * .55f + .36f * pr;
    f.glintR = GLINT_SIZE * pr * glintHide;
    f.glint2R = .48f * GLINT_SIZE * pr * glintHide;
    // Everything inside the eye turns with the face.
    f.pupilX = px * rc - py * rs;  f.pupilY = px * rs + py * rc;
    f.glintX = g1x * rc - g1y * rs; f.glintY = g1x * rs + g1y * rc;
    f.glint2X = g2x * rc - g2y * rs; f.glint2Y = g2x * rs + g2y * rc;
    f.heart = s.heart;
    f.spiral = s.spiral;
    // Rosy cheeks, a little outward and under each eye; they grow in as they appear.
    const float blush = clampf(s.blush, 0.0f, 1.0f);
    // They grow with the feeling (a small blush at rest, a big one when happy)
    // and sit just below the eye, so they never hide behind it.
    f.cheekRX = .50f * rx * (.55f + .45f * blush);
    f.cheekRY = .19f * rx * (.55f + .45f * blush);
    float cxo = -inward * .24f * rx, cyo = ry * 1.02f + f.cheekRY * .55f;
    f.cheekX = cxo * rc - cyo * rs;
    f.cheekY = cxo * rs + cyo * rc;
    f.cheekAlpha = smoothstep(.02f, .25f, blush) * .9f;
    f.spiralPhase = spiralPhase * side + i * 1.3f;   // the two spirals turn opposite ways
    fitToScreen(f);
  }
}

// ---- Keep each eye on screen. On a round panel a soft knee eases the eye in
// before the safe circle; the excess is absorbed by nudging it toward the
// centre and a small shrink, so it looks like it presses against the glass,
// never cropped. ----

static void scaleFrame(EyeFrame &f, float k) {
  f.rx *= k; f.ry *= k; f.bend *= k;
  f.topA *= k; f.topC /= k; f.botA *= k; f.botC /= k;
  f.lidRound *= k; f.pupilR *= k; f.pupilX *= k; f.pupilY *= k;
  f.glintX *= k; f.glintY *= k; f.glintR *= k; f.glint2X *= k; f.glint2Y *= k; f.glint2R *= k;
  f.coreR *= k; f.cheekX *= k; f.cheekY *= k; f.cheekRX *= k; f.cheekRY *= k;
}

void Eyes::fitToScreen(EyeFrame &f) const {
  // The oval's outline, sampled (tilted and bent as drawn).
  static constexpr int N = 20;
  static float cs[N], sn[N];
  static bool ready = false;
  if (!ready) {
    for (int k = 0; k < N; ++k) { cs[k] = cosf(TAU_F * k / N); sn[k] = sinf(TAU_F * k / N); }
    ready = true;
  }
  const float tc = cosf(f.tilt), ts = sinf(f.tilt), mirror = f.rightEye ? -1.0f : 1.0f;
  const float bendK = f.bend / (f.rx * f.rx);
  float best = -1, fx = 0, fy = 0, reach = 0;
  float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
  for (int k = 0; k < N; ++k) {
    float lx = f.rx * cs[k], ly = f.ry * sn[k] + bendK * lx * lx;
    float dx = (lx * tc - ly * ts) * mirror, dy = lx * ts + ly * tc;
    float px = f.cx + dx - SCREEN_CX, py = f.cy + dy - SCREEN_CY;
    float d = sqrtf(px * px + py * py);
    if (d > best) { best = d; fx = px; fy = py; reach = sqrtf(dx * dx + dy * dy); }
    minX = fminf(minX, f.cx + dx); maxX = fmaxf(maxX, f.cx + dx);
    minY = fminf(minY, f.cy + dy); maxY = fmaxf(maxY, f.cy + dy);
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
