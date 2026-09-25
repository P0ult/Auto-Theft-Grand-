// Procedural character animation: IK-driven gait (walk/run/sprint/strafe/backpedal), crouch, jump/fall,
// swimming, sitting/driving, aiming (pistol/rifle/heavy), melee actions (punch combo, kick, stab, bat),
// reload, flinch, cower, hands up, talking gestures, get-up. Writes Euler angles into skeleton bones.
import { B } from './humanoid.js';
import { clamp, damp, smoothstep, lerp } from '../core/utils.js';

const NB = 17;
const TAU = Math.PI * 2;

// Gait parameter sets: [strideLenAtSpeed, duty, lift, hipDrop, bob, armA, elbow, elbowSwing, lean, chestYaw, hipYaw, footWidth]
const GAITS = {
  idle: { duty: 1, lift: 0, drop: 0.015, bob: 0, armA: 0, elbow: 0.12, elbowSw: 0, lean: 0, chestYaw: 0, hipYaw: 0, width: 0.11 },
  walk: { duty: 0.62, lift: 0.1, drop: 0.035, bob: 0.028, armA: 0.32, elbow: 0.25, elbowSw: 0.25, lean: 0.05, chestYaw: 0.12, hipYaw: 0.1, width: 0.1 },
  run: { duty: 0.38, lift: 0.28, drop: 0.08, bob: 0.045, armA: 0.62, elbow: 1.3, elbowSw: 0.25, lean: 0.14, chestYaw: 0.2, hipYaw: 0.12, width: 0.075 },
  sprint: { duty: 0.33, lift: 0.36, drop: 0.1, bob: 0.05, armA: 0.9, elbow: 1.35, elbowSw: 0.3, lean: 0.24, chestYaw: 0.24, hipYaw: 0.14, width: 0.07 },
};

// Action keyframes: bone -> [x,y,z] at normalized times
const G = { // guard
  rUpperArm: [-0.85, 0.2, -0.35], rForearm: [-2.1, 0, 0], lUpperArm: [-0.85, -0.2, 0.35], lForearm: [-2.1, 0, 0],
};
const ACTIONS = {
  jab: { dur: 0.3, hit: 0.11, keys: [[0, G], [0.35, { lUpperArm: [-1.55, -0.3, 0.05], lForearm: [-0.12, 0, 0], chest: [0.05, -0.35, 0], rUpperArm: G.rUpperArm, rForearm: G.rForearm }], [1, G]] },
  cross: { dur: 0.38, hit: 0.15, keys: [[0, G], [0.4, { rUpperArm: [-1.55, 0.35, -0.05], rForearm: [-0.1, 0, 0], chest: [0.06, 0.55, 0], spine: [0, 0.2, 0], lUpperArm: G.lUpperArm, lForearm: G.lForearm }], [1, G]] },
  kick: { dur: 0.55, hit: 0.27, legs: true, keys: [[0, { ...G }], [0.45, { ...G, rThigh: [-1.45, 0, -0.05], rShin: [0.15, 0, 0], rFoot: [0.3, 0, 0], chest: [-0.25, 0.2, 0] }], [1, { ...G }]] },
  stab: { dur: 0.45, hit: 0.22, keys: [[0, { rUpperArm: [-1.0, 0.2, -0.2], rForearm: [-1.6, 0, 0] }], [0.3, { rUpperArm: [-2.7, 0.25, -0.25], rForearm: [-0.9, 0, 0], chest: [-0.1, -0.2, 0] }], [0.55, { rUpperArm: [-1.0, 0.45, -0.05], rForearm: [-0.25, 0, 0], chest: [0.3, 0.35, 0], spine: [0.1, 0.1, 0] }], [1, { rUpperArm: [-1.0, 0.2, -0.2], rForearm: [-1.6, 0, 0] }]] },
  swing: { dur: 0.62, hit: 0.34, keys: [
    [0, { rUpperArm: [-0.9, -0.4, -0.3], rForearm: [-1.9, 0, 0], lUpperArm: [-1.1, -0.9, 0.2], lForearm: [-1.7, 0, 0], chest: [0, -0.4, 0] }],
    [0.35, { rUpperArm: [-1.5, -0.9, -0.6], rForearm: [-1.9, 0, 0], lUpperArm: [-1.4, -1.3, 0.1], lForearm: [-1.8, 0, 0], chest: [0, -0.95, 0], spine: [0, -0.3, 0] }],
    [0.6, { rUpperArm: [-1.45, 0.5, -0.1], rForearm: [-0.2, 0, 0], lUpperArm: [-1.45, -0.1, 0.1], lForearm: [-0.35, 0, 0], chest: [0.1, 0.95, 0], spine: [0.05, 0.35, 0] }],
    [1, { rUpperArm: [-0.9, -0.4, -0.3], rForearm: [-1.9, 0, 0], lUpperArm: [-1.1, -0.9, 0.2], lForearm: [-1.7, 0, 0], chest: [0, -0.4, 0] }]] },
  reload: { dur: 1.1, keys: [[0, {}], [0.2, { lUpperArm: [-0.9, -0.6, 0.1], lForearm: [-1.9, 0, 0], rUpperArm: [-0.8, 0.2, -0.2], rForearm: [-1.2, 0, 0] }], [0.5, { lUpperArm: [-0.3, -0.2, 0.2], lForearm: [-1.2, 0, 0], rUpperArm: [-0.8, 0.2, -0.2], rForearm: [-1.2, 0, 0] }], [0.8, { lUpperArm: [-0.9, -0.6, 0.1], lForearm: [-1.9, 0, 0], rUpperArm: [-0.8, 0.2, -0.2], rForearm: [-1.2, 0, 0] }], [1, {}]] },
  throw: { dur: 0.75, hit: 0.45, keys: [[0, {}], [0.4, { rUpperArm: [-2.6, 0.1, -0.5], rForearm: [-1.2, 0, 0], chest: [-0.15, -0.5, 0] }], [0.6, { rUpperArm: [-1.3, 0.3, -0.1], rForearm: [-0.2, 0, 0], chest: [0.2, 0.5, 0] }], [1, {}]] },
  pull: { dur: 0.8, keys: [[0, {}], [0.3, { lUpperArm: [-1.4, -0.3, 0.1], lForearm: [-0.3, 0, 0], rUpperArm: [-1.4, 0.3, -0.1], rForearm: [-0.3, 0, 0] }], [0.7, { lUpperArm: [-0.6, -0.1, 0.2], lForearm: [-1.8, 0, 0], rUpperArm: [-0.6, 0.1, -0.2], rForearm: [-1.8, 0, 0], chest: [-0.2, 0.3, 0] }], [1, {}]] },
  wave: { dur: 1.4, keys: [[0, {}], [0.2, { rUpperArm: [-0.4, 0, -2.4], rForearm: [-0.9, 0, 0] }], [0.4, { rUpperArm: [-0.4, 0, -2.6], rForearm: [-0.4, 0, 0] }], [0.6, { rUpperArm: [-0.4, 0, -2.4], rForearm: [-0.9, 0, 0] }], [0.8, { rUpperArm: [-0.4, 0, -2.6], rForearm: [-0.4, 0, 0] }], [1, {}]] },
  flinch: { dur: 0.35, additive: true, keys: [[0, {}], [0.25, { chest: [-0.35, 0.3, 0.1], head: [-0.3, -0.2, 0], spine: [-0.1, 0, 0] }], [1, {}]] },
  getup: { dur: 1.3, full: true, keys: [
    [0, { hipsOff: [0, -0.82, 0], hipsRot: [-1.45, 0, 0], lThigh: [-0.2, 0, 0.1], rThigh: [-0.2, 0, -0.1], lShin: [0.1, 0, 0], rShin: [0.1, 0, 0], lUpperArm: [-0.2, 0, 0.5], rUpperArm: [-0.2, 0, -0.5] }],
    [0.35, { hipsOff: [0, -0.7, -0.1], hipsRot: [-0.4, 0, 0], lThigh: [-1.9, 0, 0.15], rThigh: [-1.9, 0, -0.15], lShin: [2.2, 0, 0], rShin: [2.2, 0, 0], lUpperArm: [0.4, 0, 0.3], rUpperArm: [0.4, 0, -0.3], lForearm: [-0.3, 0, 0], rForearm: [-0.3, 0, 0], spine: [0.5, 0, 0] }],
    [0.7, { hipsOff: [0, -0.45, 0], hipsRot: [0.3, 0, 0], lThigh: [-1.6, 0, 0.1], rThigh: [-1.4, 0, -0.1], lShin: [1.9, 0, 0], rShin: [1.7, 0, 0], spine: [0.5, 0, 0], lUpperArm: [-0.3, 0, 0.3], rUpperArm: [-0.3, 0, -0.3] }],
    [1, { hipsOff: [0, 0, 0] }]] },
};

function keyInterp(keys, t, out) {
  // returns merged pose object at t by interpolating between surrounding keys (smoothstep easing)
  let a = keys[0], b = keys[keys.length - 1];
  for (let i = 0; i < keys.length - 1; i++) if (t >= keys[i][0] && t <= keys[i + 1][0]) { a = keys[i]; b = keys[i + 1]; break; }
  const span = b[0] - a[0];
  let k = span > 0 ? (t - a[0]) / span : 0;
  k = k * k * (3 - 2 * k);
  for (const name in out) delete out[name];
  const names = new Set([...Object.keys(a[1]), ...Object.keys(b[1])]);
  for (const n of names) {
    const va = a[1][n], vb = b[1][n];
    if (va && vb) out[n] = [lerp(va[0], vb[0], k), lerp(va[1], vb[1], k), lerp(va[2], vb[2], k)];
    else if (va) out[n] = va.map((v) => v * (1 - k)); // fade out to "no override"
    else out[n] = vb.map((v) => v * k);
    out[n].w = va && vb ? 1 : va ? 1 - k : k;
  }
  return out;
}

export class Animator {
  constructor(bones, rest) {
    this.bones = bones;
    this.rest = rest;
    this.P = new Float32Array(NB * 3);
    this.hipsOff = [0, 0, 0];
    this.hipsRot = [0, 0, 0];
    this.phase = 0;
    this.time = Math.random() * 10;
    this.w = { idle: 1, walk: 0, run: 0, sprint: 0, aim: 0, crouch: 0, air: 0, sit: 0, swim: 0, cower: 0, hands: 0, rifle: 0, talk: 0 };
    this.action = null;
    this.additive = [];
    this.L1 = 0.44; this.L2 = 0.42;
    this.thighOff = [rest[B.lThigh], rest[B.rThigh]];
    this.hipH = rest[B.hips][1];
    this.enabled = true;
    this._tmpPose = {};
    this.recoil = 0;
    this.blendFrom = null;
    this.blendT = 0;
  }

  play(name, speed = 1) {
    const def = ACTIONS[name];
    if (!def) return null;
    const act = { name, def, t: 0, dur: def.dur / speed, hitTime: def.hit != null ? def.hit / speed : -1, hitDone: false, done: false };
    if (def.additive) { this.additive.push(act); return act; }
    this.action = act;
    return act;
  }
  get busy() { return !!this.action && !this.action.done; }

  // Store current quaternions to blend from (used after ragdoll)
  beginBlend(dur = 0.4) {
    this.blendFrom = this.bones.map((b) => b.quaternion.clone());
    this.blendFromPos = this.bones[0].position.clone();
    this.blendT = 0; this.blendDur = dur;
  }

  setBones() {
    const P = this.P, bones = this.bones;
    for (let i = 0; i < NB; i++) bones[i].rotation.set(P[i * 3], P[i * 3 + 1], P[i * 3 + 2]);
    bones[0].position.set(this.hipsOff[0], this.hipH + this.hipsOff[1], this.hipsOff[2]);
    bones[0].rotation.set(P[0] + this.hipsRot[0], P[1] + this.hipsRot[1], P[2] + this.hipsRot[2]);
    if (this.blendFrom) {
      this.blendT += this._dt;
      const k = Math.min(1, this.blendT / this.blendDur);
      const e = k * k * (3 - 2 * k);
      for (let i = 0; i < NB; i++) bones[i].quaternion.slerpQuaternions(this.blendFrom[i], bones[i].quaternion.clone(), e);
      bones[0].position.lerpVectors(this.blendFromPos, bones[0].position.clone(), e);
      if (k >= 1) this.blendFrom = null;
    }
  }

  set(bone, x, y, z) { const P = this.P, i = bone * 3; P[i] = x; P[i + 1] = y; P[i + 2] = z; }
  add(bone, x, y, z) { const P = this.P, i = bone * 3; P[i] += x; P[i + 1] += y; P[i + 2] += z; }
  mix(bone, x, y, z, w) { const P = this.P, i = bone * 3; P[i] += (x - P[i]) * w; P[i + 1] += (y - P[i + 1]) * w; P[i + 2] += (z - P[i + 2]) * w; }

  // 2-bone leg IK. target relative to the thigh joint in character space (x lateral, y up, z forward)
  legIK(side, tx, ty, tz) {
    const th = side === 0 ? B.lThigh : B.rThigh, sh = side === 0 ? B.lShin : B.rShin, ft = side === 0 ? B.lFoot : B.rFoot;
    const L1 = this.L1, L2 = this.L2;
    const lat = Math.atan2(tx, -ty);
    const vy = Math.hypot(tx, ty);
    let d = Math.hypot(vy, tz);
    d = clamp(d, 0.1, L1 + L2 - 0.0005);
    const cosK = clamp((L1 * L1 + L2 * L2 - d * d) / (2 * L1 * L2), -1, 1);
    const knee = Math.PI - Math.acos(cosK);
    const cosA = clamp((L1 * L1 + d * d - L2 * L2) / (2 * L1 * d), -1, 1);
    const alpha = Math.acos(cosA);
    const theta = Math.atan2(tz, vy);
    const thighX = -(theta + alpha);
    this.set(th, thighX, 0, lat);
    this.set(sh, knee, 0, 0);
    // keep foot flat to ground
    this.set(ft, -(thighX + knee), 0, -lat * 0.5);
  }

  update(dt, s) {
    this._dt = dt;
    this.time += dt;
    const w = this.w;
    const P = this.P;
    P.fill(0);
    this.hipsOff[0] = this.hipsOff[1] = this.hipsOff[2] = 0;
    this.hipsRot[0] = this.hipsRot[1] = this.hipsRot[2] = 0;

    const speed = s.speed || 0;
    // gait weights
    const tw = { idle: 0, walk: 0, run: 0, sprint: 0 };
    if (speed < 0.15) tw.idle = 1;
    else if (speed < 2.2) { const k = smoothstep(0.15, 1.2, speed); tw.idle = 1 - k; tw.walk = k; }
    else if (speed < 5.2) { const k = smoothstep(2.2, 4.2, speed); tw.walk = 1 - k; tw.run = k; }
    else { const k = smoothstep(5.2, 7.0, speed); tw.run = 1 - k; tw.sprint = k; }
    for (const k in tw) w[k] = damp(w[k], tw[k], 10, dt);
    w.aim = damp(w.aim, s.aim ? 1 : 0, 14, dt);
    w.crouch = damp(w.crouch, s.crouch ? 1 : 0, 8, dt);
    w.air = damp(w.air, s.grounded === false && !s.swim && !s.sit ? 1 : 0, 10, dt);
    w.sit = damp(w.sit, s.sit ? 1 : 0, 12, dt);
    w.swim = damp(w.swim, s.swim ? 1 : 0, 5, dt);
    w.cower = damp(w.cower, s.cower ? 1 : 0, 6, dt);
    w.hands = damp(w.hands, s.handsUp ? 1 : 0, 6, dt);
    w.talk = damp(w.talk, s.talking ? 1 : 0, 3, dt);
    const heavy = s.weapon === 'rifle' || s.weapon === 'smg' || s.weapon === 'shotgun' || s.weapon === 'rpg';
    w.rifle = damp(w.rifle, heavy ? 1 : 0, 10, dt);

    // blended gait params
    const gp = { duty: 0, lift: 0, drop: 0, bob: 0, armA: 0, elbow: 0, elbowSw: 0, lean: 0, chestYaw: 0, hipYaw: 0, width: 0 };
    for (const g of ['idle', 'walk', 'run', 'sprint']) { const wg = w[g]; const G2 = GAITS[g]; for (const k in gp) gp[k] += G2[k] * wg; }
    const moving = 1 - w.idle;
    const strideLen = lerp(1.25, 3.9, clamp((speed - 1.2) / 6.3, 0, 1)) * (1 - w.crouch * 0.35);
    const freq = speed > 0.05 ? speed / strideLen : 0;
    this.phase = (this.phase + freq * dt) % 1;
    const ph = this.phase;
    const mA = s.moveAngle || 0;
    const mx = Math.sin(mA), mz = Math.cos(mA);

    // hips
    const bob = gp.bob * Math.pow(Math.sin(TAU * ph * 2 - Math.PI / 2) * 0.5 + 0.5, 1.0) * moving;
    this.hipsOff[1] = -gp.drop - bob - w.crouch * 0.42;
    this.hipsOff[0] = -Math.cos(TAU * ph) * 0.018 * w.walk + Math.sin(this.time * 0.7) * 0.008 * w.idle;
    const hipYaw = -gp.hipYaw * Math.sin(TAU * ph) * moving;
    this.set(B.hips, 0, hipYaw * mz, 0);

    // legs via IK: foot trajectories
    for (let side = 0; side < 2; side++) {
      const p = (ph + side * 0.5) % 1;
      const D = gp.duty * moving + (1 - moving);
      const half = strideLen * D * 0.5 * moving;
      let fz, fy;
      if (p < D || moving < 0.01) { const q = D > 0 ? p / D : 0; fz = lerp(half, -half, q); fy = 0; }
      else {
        const q = (p - D) / (1 - D);
        const e = q * q * (3 - 2 * q);
        fz = lerp(-half, half, e);
        fy = gp.lift * Math.sin(Math.PI * Math.pow(q, 0.75)) * moving;
      }
      const lateral = (side === 0 ? 1 : -1) * gp.width;
      // stride along move direction relative to facing
      const tx = lateral + fz * mx, tz = fz * mz;
      const off = this.thighOff[side];
      const hipY = this.hipH + this.hipsOff[1] + off[1];
      const ankleH = 0.06 + fy;
      this.legIK(side, tx - off[0] - this.hipsOff[0], ankleH - hipY, tz + (w.crouch * 0.12));
      // counter hip yaw on thighs
      this.add(side === 0 ? B.lThigh : B.rThigh, 0, -hipYaw * mz, 0);
    }

    // torso
    const s1 = Math.sin(TAU * ph);
    const lean = gp.lean * (mz >= 0 ? 1 : -0.4) + w.crouch * 0.3;
    this.set(B.spine, lean * 0.5 + 0.015 * Math.sin(this.time * 1.9) * w.idle, gp.chestYaw * s1 * 0.4 * moving * mz, 0);
    this.set(B.chest, lean * 0.5 + 0.02 * Math.sin(this.time * 1.9) * w.idle, gp.chestYaw * s1 * 0.6 * moving * mz, 0);
    this.set(B.neck, -lean * 0.3, 0, 0);
    this.set(B.head, -lean * 0.4 + 0.02 * Math.sin(this.time * 0.9) * w.idle, -hipYaw * 0.5, 0);
    // arms swing opposite legs
    const aA = gp.armA * moving;
    this.set(B.lUpperArm, aA * s1 + 0.04, 0, 0.09 + w.run * 0.05);
    this.set(B.rUpperArm, -aA * s1 + 0.04, 0, -0.09 - w.run * 0.05);
    this.set(B.lForearm, -(gp.elbow + gp.elbowSw * Math.max(0, -s1) * moving), 0, 0);
    this.set(B.rForearm, -(gp.elbow + gp.elbowSw * Math.max(0, s1) * moving), 0, 0);
    this.set(B.lHand, 0, 0, 0.1);
    this.set(B.rHand, 0, 0, -0.1);

    // carrying a rifle (not aiming): hold across the body
    if (w.rifle > 0.01 && !s.sit && !s.swim) {
      const k = w.rifle * (1 - w.aim);
      this.mix(B.rUpperArm, -0.35, 0.1, -0.25, k);
      this.mix(B.rForearm, -1.35, 0, 0, k);
      this.mix(B.lUpperArm, -0.75, -0.55, 0.1, k);
      this.mix(B.lForearm, -1.45, 0, 0, k);
    }

    // air pose
    if (w.air > 0.01) {
      const k = w.air;
      const fall = clamp(-(s.vy || 0) / 12, 0, 1);
      const fl = Math.sin(this.time * 12) * fall;
      this.mix(B.lThigh, -0.7, 0, 0.1, k); this.mix(B.rThigh, -0.25, 0, -0.1, k);
      this.mix(B.lShin, 1.1, 0, 0, k); this.mix(B.rShin, 0.6, 0, 0, k);
      this.mix(B.lFoot, -0.2, 0, 0, k); this.mix(B.rFoot, -0.2, 0, 0, k);
      if (w.rifle < 0.5 && w.aim < 0.5) {
        this.mix(B.lUpperArm, -0.6 + fl * 0.8, 0, 0.7 + fl * 0.3, k); this.mix(B.rUpperArm, -0.6 - fl * 0.8, 0, -0.7 - fl * 0.3, k);
        this.mix(B.lForearm, -0.5, 0, 0, k); this.mix(B.rForearm, -0.5, 0, 0, k);
      }
    }

    // swimming
    if (w.swim > 0.01) {
      const k = w.swim;
      const t = this.time * (speed > 0.3 ? 2.2 : 1.0);
      const st = Math.sin(t * TAU * 0.5);
      this.hipsRot[0] += 1.25 * k * (speed > 0.3 ? 1 : 0.45);
      this.hipsOff[1] = lerp(this.hipsOff[1], -0.55, k);
      this.mix(B.head, -1.0 * (speed > 0.3 ? 1 : 0.4), 0, 0, k);
      this.mix(B.neck, -0.3, 0, 0, k);
      const flutter = Math.sin(this.time * 9) * 0.35;
      this.mix(B.lThigh, flutter, 0, 0.12, k); this.mix(B.rThigh, -flutter, 0, -0.12, k);
      this.mix(B.lShin, 0.35 + flutter * 0.4, 0, 0, k); this.mix(B.rShin, 0.35 - flutter * 0.4, 0, 0, k);
      this.mix(B.lFoot, 0.6, 0, 0, k); this.mix(B.rFoot, 0.6, 0, 0, k);
      const arm = st * 0.5 + 0.5;
      this.mix(B.lUpperArm, lerp(-2.9, -1.2, arm), 0, lerp(0.2, 1.3, arm), k);
      this.mix(B.rUpperArm, lerp(-2.9, -1.2, arm), 0, -lerp(0.2, 1.3, arm), k);
      this.mix(B.lForearm, lerp(-0.2, -1.4, arm), 0, 0, k); this.mix(B.rForearm, lerp(-0.2, -1.4, arm), 0, 0, k);
    }

    // sitting (vehicle)
    if (w.sit > 0.01) {
      const k = w.sit;
      this.hipsOff[1] = lerp(this.hipsOff[1], -0.46, k);
      this.hipsOff[2] = lerp(this.hipsOff[2], 0, k);
      this.hipsOff[0] = lerp(this.hipsOff[0], 0, k);
      this.set(B.hips, P[B.hips * 3] * (1 - k), P[B.hips * 3 + 1] * (1 - k), 0);
      const save = this.P.slice();
      for (let side = 0; side < 2; side++) {
        const off = this.thighOff[side];
        const hipY = this.hipH - 0.46 + off[1];
        this.legIK(side, (side === 0 ? 1 : -1) * 0.13 - off[0], 0.12 - hipY, 0.5);
      }
      // blend legs from saved to sitting
      for (const b of [B.lThigh, B.lShin, B.lFoot, B.rThigh, B.rShin, B.rFoot]) {
        for (let c = 0; c < 3; c++) { const i = b * 3 + c; P[i] = lerp(save[i], P[i], k); }
      }
      this.mix(B.spine, -0.12, 0, 0, k); this.mix(B.chest, -0.06, 0, 0, k); this.mix(B.head, 0.1, (s.lookYaw || 0) * 0.6, 0, k);
      const steer = s.steer || 0;
      if (s.sit === 1) {
        this.mix(B.lUpperArm, -0.95 + steer * 0.35, -0.25, 0.25, k); this.mix(B.lForearm, -0.75 - steer * 0.2, 0, 0, k);
        this.mix(B.rUpperArm, -0.95 - steer * 0.35, 0.25, -0.25, k); this.mix(B.rForearm, -0.75 + steer * 0.2, 0, 0, k);
        this.mix(B.lHand, -0.3, 0, 0.6, k); this.mix(B.rHand, -0.3, 0, -0.6, k);
      } else {
        this.mix(B.lUpperArm, -0.35, 0, 0.1, k); this.mix(B.lForearm, -0.9, 0, 0, k);
        this.mix(B.rUpperArm, -0.35, 0, -0.1, k); this.mix(B.rForearm, -0.9, 0, 0, k);
      }
    }

    // aiming weapons
    if (w.aim > 0.01) {
      const k = w.aim;
      const pitch = s.aimPitch || 0;
      const rec = this.recoil;
      if (s.weapon === 'pistol') {
        this.mix(B.rUpperArm, -Math.PI / 2 - pitch - rec * 0.35, 0.3, 0, k);
        this.mix(B.rForearm, -0.05, 0, 0, k);
        this.mix(B.lUpperArm, -Math.PI / 2 - pitch + 0.12 - rec * 0.3, -0.62, 0, k);
        this.mix(B.lForearm, -0.55, 0, 0, k);
        this.mix(B.rHand, 0, 0, 0, k); this.mix(B.lHand, 0, 0, 0, k);
        this.mix(B.chest, -pitch * 0.3 + P[B.chest * 3] * 0.3, 0.12, 0, k);
        this.mix(B.head, -pitch * 0.35, -0.12, 0, k);
      } else if (s.weapon === 'rpg') {
        this.mix(B.rUpperArm, -1.3 - pitch, 0.15, -0.45, k);
        this.mix(B.rForearm, -1.1, 0, 0, k);
        this.mix(B.lUpperArm, -1.5 - pitch, -0.45, 0.1, k);
        this.mix(B.lForearm, -0.5, 0, 0, k);
        this.mix(B.chest, -pitch * 0.3, 0.15, 0, k);
        this.mix(B.head, -pitch * 0.4, -0.15, 0.15, k);
      } else if (heavy) {
        this.mix(B.rUpperArm, -0.75 - pitch * 0.9 - rec * 0.2, 0.25, -0.55, k);
        this.mix(B.rForearm, -1.45, 0, 0, k);
        this.mix(B.lUpperArm, -1.35 - pitch - rec * 0.2, -0.75, 0.15, k);
        this.mix(B.lForearm, -0.45, 0, 0, k);
        this.mix(B.rHand, 0, 0, 0.2, k);
        this.mix(B.chest, -pitch * 0.35, 0.22, 0, k);
        this.mix(B.head, -pitch * 0.35 + 0.05, -0.2, 0.1, k);
      }
      this.recoil = Math.max(0, this.recoil - dt * 8);
    }

    // melee/one-shot actions
    const act = this.action;
    if (act && !act.done) {
      act.t += dt;
      const nt = Math.min(1, act.t / act.dur);
      const pose = keyInterp(act.def.keys, nt, this._tmpPose);
      const fullOverride = act.def.full;
      for (const name in pose) {
        const v = pose[name];
        if (name === 'hipsOff') { this.hipsOff[0] += v[0]; this.hipsOff[1] += v[1]; this.hipsOff[2] += v[2]; continue; }
        if (name === 'hipsRot') { this.hipsRot[0] += v[0]; this.hipsRot[1] += v[1]; this.hipsRot[2] += v[2]; continue; }
        const bi = B[name];
        if (bi == null) continue;
        const wgt = fullOverride ? 1 : v.w;
        if (name === 'chest' || name === 'spine' || name === 'head') this.add(bi, v[0], v[1], v[2]);
        else this.mix(bi, v[0], v[1], v[2], clamp(wgt, 0, 1));
      }
      if (act.t >= act.dur) act.done = true;
    }
    for (let i = this.additive.length - 1; i >= 0; i--) {
      const a = this.additive[i];
      a.t += dt;
      const pose = keyInterp(a.def.keys, Math.min(1, a.t / a.dur), this._tmpPose);
      for (const name in pose) { const bi = B[name]; if (bi != null) this.add(bi, pose[name][0], pose[name][1], pose[name][2]); }
      if (a.t >= a.dur) this.additive.splice(i, 1);
    }

    // cower / hands up (upper body)
    if (w.cower > 0.01) {
      const k = w.cower;
      this.mix(B.lUpperArm, -2.3, -0.3, 0.7, k); this.mix(B.rUpperArm, -2.3, 0.3, -0.7, k);
      this.mix(B.lForearm, -2.0, 0, 0, k); this.mix(B.rForearm, -2.0, 0, 0, k);
      this.mix(B.head, 0.5, 0, 0, k);
    }
    if (w.hands > 0.01) {
      const k = w.hands;
      this.mix(B.lUpperArm, -0.2, 0, 2.6, k); this.mix(B.rUpperArm, -0.2, 0, -2.6, k);
      this.mix(B.lForearm, -0.3, 0, 0, k); this.mix(B.rForearm, -0.3, 0, 0, k);
    }
    if (w.talk > 0.01 && !this.busy) {
      const k = w.talk;
      const t = this.time;
      this.add(B.rUpperArm, (-0.5 + Math.sin(t * 2.3) * 0.25) * k, 0.2 * k, 0);
      this.add(B.rForearm, (-0.9 + Math.sin(t * 3.1) * 0.3) * k, 0, 0);
      this.add(B.lUpperArm, (-0.2 + Math.sin(t * 1.7 + 1) * 0.15) * k, 0, 0);
      this.add(B.lForearm, (-0.5 + Math.sin(t * 2.7 + 2) * 0.2) * k, 0, 0);
      this.add(B.head, Math.sin(t * 1.3) * 0.08 * k, Math.sin(t * 0.8) * 0.15 * k, 0);
    }
    // look direction (head)
    if (s.lookYaw) this.add(B.head, s.lookPitch || 0, clamp(s.lookYaw, -1.1, 1.1) * 0.7, 0);

    this.setBones();
  }
}

export { ACTIONS };
