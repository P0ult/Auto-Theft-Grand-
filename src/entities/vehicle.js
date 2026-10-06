// Vehicle simulation: bicycle-model tire physics with slip angles, weight transfer, traction circle,
// handbrake drifting, ground following + jumps, collision response, damage/deformation, fire & explosion.
import * as THREE from 'three';
import { VEHICLES } from './vehicledefs.js';
import { buildVehicleModel, vehicleMaterials, isSharedMaterial } from './vehiclemodels.js';
import { WATER_Y } from '../world/citymap.js';
import { clamp, damp, pick, wrapAngle, sign } from '../core/utils.js';
import { U } from '../render/materials.js';

const G = 9.81;
const _v = new THREE.Vector3(), _w = new THREE.Vector3();
const _q = new THREE.Quaternion(), _e = new THREE.Euler(0, 0, 0, 'YXZ');
const _p = new THREE.Vector3(), _r = new THREE.Vector3(), _n = new THREE.Vector3(), _j = new THREE.Vector3(), _t = new THREE.Vector3(), _a = new THREE.Vector3();
const UPV = new THREE.Vector3(0, 1, 0);
const _c = new THREE.Vector3(), _ax = new THREE.Vector3(), _fw = new THREE.Vector3(), _J = new THREE.Vector3();
let nextVid = 1;
const drvIsPlayer = (v) => !!v.driver?.isPlayer;

function sat(x) {
  const ax = Math.abs(x);
  if (ax < 1) return x * (1.5 - 0.5 * x * x);
  return sign(x) * (1 - 0.12 * Math.min(1, (ax - 1) / 2.5));
}

export class Vehicle {
  constructor(game, id, opts = {}) {
    this.vid = nextVid++;
    this.game = game;
    this.type = id;
    this.def = VEHICLES[id];
    const d = this.def;
    this.color = opts.color ?? pick(d.colors);
    const model = this.buildModel(d, this.color);
    this.model = model;
    this.group = model.group;
    this.group.rotation.order = 'YXZ';
    this.group.userData.vehicle = this;
    game.scene.add(this.group);
    this.pos = this.group.position;
    this.yaw = opts.yaw || 0;
    this.pos.set(opts.x || 0, 0, opts.z || 0);
    this.pos.y = opts.y ?? game.map.groundHeight(this.pos.x, this.pos.z);
    this.vel = new THREE.Vector3();
    this.r = 0;
    this.hx = d.W / 2; this.hz = d.L / 2;
    this.mass = d.mass;
    this.I = d.mass * (d.L * d.L + d.W * d.W) / 12;
    this.a = d.wheelbase * 0.5; this.b = d.wheelbase * 0.5;
    this.steerAngle = 0;
    this.input = { throttle: 0, brake: 0, steer: 0, handbrake: false };
    this.axLong = 0; this.ayLat = 0;
    this.rearGrip = 1;
    this.health = this.maxHealth = d.health ?? 1000;
    this.burnTime = 0;
    this.onFire = false;
    this.exploded = false;
    this.sunk = false;
    this.occupants = [null, null, null, null];
    this.sirenOn = false;
    this.lightsOn = false;
    this.horn = false;
    this.bodyPitch = 0; this.bodyRoll = 0; this.bodyPitchV = 0; this.bodyRollV = 0; this.bodyY = 0; this.bodyYV = 0;
    this.airborne = false; this.vy = 0; this.lastGroundY = this.pos.y; this.groundVy = 0;
    this.groundPitch = 0; this.groundRoll = 0;
    this.wheelRot = 0;
    this.slipRear = 0; this.slipFront = 0; this.wheelspin = 0;
    this.skid = 0;
    this.locked = opts.locked || false;
    this.parked = !!opts.parked;
    this.ai = null;
    this.lastHit = -10;
    this.hydraulic = 0; this.hydraulicV = 0;
    this.sirenPhase = Math.random() * 10;
    this.brakeLight = false;
    this.deformed = false;
    this.contacts = [];
    this.stuckTime = 0;
    this.removed = false;
    this.persistent = !!opts.persistent;
    this.missionTag = opts.missionTag || null;
    this.group.rotation.y = this.yaw;
    this.setup?.(opts);
    this._updateVisual(0);
  }

  // subclasses (aircraft, tanks) provide their own meshes
  buildModel(def, color) { return buildVehicleModel(def, color); }

  get driver() { return this.occupants[0]; }
  get speed() { return this.vel.x * Math.sin(this.yaw) + this.vel.z * Math.cos(this.yaw); }
  get speedAbs() { return Math.hypot(this.vel.x, this.vel.z); }
  get fwd() { return _v.set(Math.sin(this.yaw), 0, Math.cos(this.yaw)); }
  get isWrecked() { return this.exploded || this.sunk; }
  get empty() { return this.occupants.every((o) => !o); }

  localToWorld(x, y, z, out = new THREE.Vector3()) {
    const s = Math.sin(this.yaw), c = Math.cos(this.yaw);
    return out.set(this.pos.x + x * c + z * s, this.pos.y + y, this.pos.z - x * s + z * c);
  }
  worldToLocal(x, z) {
    const s = Math.sin(this.yaw), c = Math.cos(this.yaw);
    const dx = x - this.pos.x, dz = z - this.pos.z;
    return [dx * c - dz * s, dx * s + dz * c];
  }
  doorWorld(out = new THREE.Vector3()) { const d = this.model.doorPos; return this.localToWorld(d.x, 0, d.z, out); }
  seatWorld(i, out = new THREE.Vector3()) { const s = this.model.seats[i]; return this.localToWorld(s.x, s.y, s.z, out); }

  // ------------------------------------------------------------------ occupants
  putIn(char, seat = 0) {
    const prev = this.occupants[seat];
    if (prev && prev !== char) this.takeOut(prev); // (never leave someone thinking they're still in the seat)
    this.occupants[seat] = char;
    char.vehicle = this;
    char.seat = seat;
    char.vel.set(0, 0, 0);
    char.ragdolling = false;
    const S = this.model.seats;
    const s = S[seat] || S[S.length - 1];
    this.model.bodyGroup.add(char.root);
    // hips a set height above the seat point, whatever the character's size (models without a seatHip
    // were laid out for hips 0.52 above the seat)
    const h = char.appearance.height || 1;
    if (this.model.stand) { // standing (a skateboard): feet on the seat point, turned side-on
      char.root.position.set(s.x, s.y, s.z);
      char.root.rotation.set(0, this.model.stand.yaw, 0);
    } else {
      char.root.position.set(s.x, s.y + (this.model.seatHip ?? 0.52) - (char.anim.hipH - 0.46) * h, s.z);
      char.root.rotation.set(0, 0, 0);
    }
    char._seatPos = char.root.position.clone(); char.leanK = 0; char.leaning = false;
    char.yaw = this.yaw;
    if (char.weaponMesh && char.weaponDef.type !== 'gun') char.weaponMesh.visible = false;
    this.parked = false;
  }
  takeOut(char, pos) {
    const seat = this.occupants.indexOf(char);
    if (seat >= 0) this.occupants[seat] = null;
    char.vehicle = null;
    char._seatPos = null; char.leanK = 0; char.leaning = false; char.aimYaw = null;
    char.seat = -1;
    this.game.scene.add(char.root);
    const p = pos || this.doorWorld();
    // stand on whatever the vehicle is on (bridge decks, rooftops), not the terrain underneath
    char.pos.set(p.x, this.game.collision.floorHeight(p.x, p.z, this.pos.y + 0.4), p.z);
    char.root.rotation.set(0, this.yaw, 0);
    char.yaw = this.yaw;
    char.vel.set(this.vel.x * 0.5, 0, this.vel.z * 0.5);
    if (char.weaponMesh) char.weaponMesh.visible = true;
    if (seat === 0) { this.input.throttle = 0; this.input.brake = 0; this.input.steer = 0; this.input.handbrake = false; }
  }
  ejectOccupant(char, dead = false) {
    const p = this.localToWorld(this.hx + 0.8, 0, 0);
    this.takeOut(char, p);
    if (dead) char.pos.y = this.pos.y + 0.5;
  }

  // ------------------------------------------------------------------ player control
  playerControl(input, dt) {
    this.input.throttle = input.throttle();
    this.input.brake = input.brake();
    this.input.steer = input.steer();
    this.input.handbrake = input.down('handbrake');
    this.horn = input.down('horn');
    if (this.def.hydraulics && input.hit('hydraulics')) this.hydraulicV += 4.5;
    if (this.def.police && input.hit('horn') && input.key('ShiftLeft')) this.sirenOn = !this.sirenOn;
  }

  // ------------------------------------------------------------------ simulation
  update(dt) {
    if (this.removed) return;
    const d = this.def;
    const inp = this.input;
    const wrecked = this.isWrecked;
    if (wrecked || !this.driver) { inp.throttle = 0; inp.steer *= 0.9; if (!this.driver && !wrecked) { inp.brake = 0; inp.handbrake = this.speedAbs < 3 || !this.driver; } }
    // steering
    const spd = Math.abs(this.speed);
    const maxSteer = d.steer / (1 + spd / 20);
    let target = inp.steer * maxSteer;
    // mild self-aligning / countersteer assist when sliding and no input
    if (Math.abs(inp.steer) < 0.05 && spd > 5 && this.driver?.isPlayer) {
      const velYaw = Math.atan2(this.vel.x, this.vel.z);
      const slip = wrapAngle(velYaw - this.yaw);
      if (this.speed > 0) target = clamp(slip * 0.6, -maxSteer, maxSteer);
    }
    this.steerAngle = damp(this.steerAngle, target, inp.steer === 0 ? 10 : 7, dt);
    // handbrake grip dynamics
    if (inp.handbrake) this.rearGrip = Math.max(0.36, this.rearGrip - dt * 4);
    else this.rearGrip = Math.min(1, this.rearGrip + dt * 1.1);

    if (this.tb) { this._tumble(dt); this._updateVisual(dt); return; }
    const steps = dt > 1 / 45 ? 3 : 2;
    const h = dt / steps;
    for (let i = 0; i < steps; i++) this._step(h);
    this._afterPhysics(dt);
    this._rolloverCheck(dt);
    this._updateVisual(dt);
  }

  // ------------------------------------------------------------------ crash physics: tumbling
  // A crash that's hard enough, a blast or a rollover hands the vehicle to a full 3D rigid body: it flips,
  // rolls and slides on its roof or sides until it settles. Landing back on its wheels, it drives on; on
  // its roof or side it stays put (rock it back over with A / D; a car left upside down catches fire).
  get canTumble() { return !this.def.kind && !this.remote && !this.removed; }

  _hullPoints() {
    if (this._hull) return this._hull;
    const d = this.def, H = this.model.hull, pts = [];
    const L = d.L, W = d.W, c = d.clearance;
    const add = (x, y, z, wheel = false) => pts.push({ p: new THREE.Vector3(x, y, z), wheel });
    if (d.bike) {
      for (const z of [d.wheelbase / 2, -d.wheelbase / 2]) add(0, 0, z, true);
      for (const sx of [1, -1]) { add(sx * 0.38, d.H * 0.8, d.wheelbase * 0.35); add(sx * 0.25, d.H * 0.45, -d.wheelbase * 0.3); add(sx * 0.2, c + 0.1, 0); }
      add(0, d.H * 0.75, -0.2);
    } else {
      for (const sx of [1, -1]) {
        for (const sz of [1, -1]) {
          add(sx * d.track / 2, 0, sz * d.wheelbase / 2, true);
          add(sx * W * 0.46, c + 0.06, sz * L * 0.47);
          add(sx * W * 0.5, H ? H.beltY : d.H * 0.6, sz * L * 0.45);
        }
        add(sx * W * 0.5, H ? H.beltY : d.H * 0.6, 0);
        const rx = H ? H.roofX : W * 0.4, ry = H ? H.roofY : d.H, z0 = H ? H.roofZ0 : -L * 0.25, z1 = H ? H.roofZ1 : L * 0.1;
        add(sx * rx, ry, z0); add(sx * rx, ry, z1); add(sx * rx, ry, (z0 + z1) / 2);
      }
    }
    const cgH = this.model.hull?.cgH ?? Math.max(0.35, d.H * 0.4);
    for (const q of pts) q.p.y -= cgH;
    // inertia of a box (body frame: x across, y up, z along)
    const m = this.mass, H2 = d.H * d.H, L2 = L * L, W2 = W * W;
    this._hull = { pts, cgH, inv: new THREE.Vector3(12 / (m * (H2 + L2)), 12 / (m * (W2 + L2)), 12 / (m * (W2 + H2))) };
    return this._hull;
  }
  // world-space inverse inertia applied to v (in place)
  _iinv(v) {
    const inv = this._hull.inv, q = this.tb.q;
    _q.copy(q).invert();
    v.applyQuaternion(_q);
    v.set(v.x * inv.x, v.y * inv.y, v.z * inv.z);
    return v.applyQuaternion(q);
  }
  _impulse(J, r) { // J at r (from the centre of gravity), world space
    this.vel.addScaledVector(J, 1 / this.mass);
    this.tb.w.add(this._iinv(_a.crossVectors(r, J)));
  }

  // w: extra angular velocity (world, rad/s); vy: extra upward speed
  startTumble(w = null, vy = 0) {
    if (!this.canTumble) return;
    const hull = this._hullPoints();
    if (!this.tb) {
      _e.set(-this.groundPitch, this.yaw, this.groundRoll, 'YXZ');
      const q = new THREE.Quaternion().setFromEuler(_e);
      this.tb = { q, w: new THREE.Vector3(0, this.r, 0), rest: 0, flipT: 0, rockT: 0, bailT: 0, scrapeT: 0, landed: 0, rPrev: this.r };
      this.vel.y = this.airborne ? this.vy : Math.max(0, this.groundVy);
      this.airborne = true;
      this.bodyPitch = this.bodyRoll = this.bodyY = 0;
      this.bodyPitchV = this.bodyRollV = this.bodyYV = 0;
      this.tb.cg = this.pos.clone().add(_v.set(0, hull.cgH, 0).applyQuaternion(q));
    }
    if (w) this.tb.w.add(w);
    this.vel.y += vy;
    this.tb.rest = 0; this.tb.settled = false;
    if (this.def.bike && this.driver) this.throwRiders?.(9);
  }

  _endTumble() {
    const tb = this.tb;
    _v.set(0, 0, 1).applyQuaternion(tb.q);
    this.yaw = Math.atan2(_v.x, _v.z);
    this.r = tb.w.y;
    this.vy = 0; this.vel.y = 0;
    this.airborne = false;
    this.groundPitch = 0; this.groundRoll = 0;
    this.pos.y = this.game.collision.surfaceHeight(this.pos.x, this.pos.z, this.pos.y + 1.2);
    this.lastGroundY = this.pos.y;
    this.tb = null;
    this.flipped = false;
    this.group.quaternion.setFromEuler(_e.set(0, this.yaw, 0, 'YXZ'));
  }

  _tumble(dt) {
    const tb = this.tb, hull = this._hullPoints(), col = this.game.collision;
    const g = G * (this.game.gravity ?? 1);
    // other systems (car-car contacts, walls) push pos / r: fold those into the rigid body
    tb.cg.copy(this.pos).add(_v.set(0, hull.cgH, 0).applyQuaternion(tb.q));
    tb.w.y += this.r - tb.rPrev;
    const steps = 4, h = dt / steps;
    let touching = 0, wheels = 0, maxImpact = 0, scrape = 0, hitAt = null;
    for (let s = 0; s < steps; s++) {
      this.vel.y -= g * h;
      tb.cg.addScaledVector(this.vel, h);
      // integrate orientation
      const w = tb.w, wl = w.length();
      if (wl > 1e-6) { _q.setFromAxisAngle(_v.copy(w).multiplyScalar(1 / wl), wl * h); tb.q.premultiply(_q).normalize(); }
      w.multiplyScalar(1 - 0.08 * h);
      touching = 0; wheels = 0;
      let push = 0; const pn = _t.set(0, 0, 0);
      for (const hp of hull.pts) {
        _r.copy(hp.p).applyQuaternion(tb.q);
        _p.copy(tb.cg).add(_r);
        const gy = col.surfaceHeight(_p.x, _p.z, _p.y + 0.6);
        const pen = gy - _p.y;
        if (pen <= 0) continue;
        touching++;
        if (hp.wheel) wheels++;
        // ground normal from the surface around the point
        const e = 0.6;
        const hx = col.surfaceHeight(_p.x + e, _p.z, _p.y + 1) - col.surfaceHeight(_p.x - e, _p.z, _p.y + 1);
        const hz = col.surfaceHeight(_p.x, _p.z + e, _p.y + 1) - col.surfaceHeight(_p.x, _p.z - e, _p.y + 1);
        _n.set(-hx / (2 * e), 1, -hz / (2 * e));
        if (Math.abs(_n.x) > 1.5 || Math.abs(_n.z) > 1.5) _n.set(0, 1, 0); // (a kerb / wall edge, not a slope)
        _n.normalize();
        if (pen > push) { push = pen; pn.copy(_n); }
        // contact velocity
        const vp = _j.copy(w).cross(_r).add(this.vel);
        const vn = vp.dot(_n);
        if (vn >= 0) continue;
        const denom = (rv) => 1 / this.mass + this._iinv(_c.crossVectors(_r, rv)).cross(_r).dot(rv);
        const kn = denom(_n);
        const eRest = hp.wheel ? 0.05 : -vn > 2 ? 0.22 : 0;
        const jn = -(1 + eRest) * vn / kn;
        this._impulse(_J.copy(_n).multiplyScalar(jn), _r);
        if (-vn > maxImpact) { maxImpact = -vn; hitAt = _p.clone(); }
        // friction: tyres grip sideways and roll along; bodywork slides
        const vp2 = _j.copy(w).cross(_r).add(this.vel);
        const vt = vp2.addScaledVector(_n, -vp2.dot(_n));
        const vtl = vt.length();
        if (vtl < 1e-4) continue;
        if (hp.wheel) {
          _ax.set(1, 0, 0).applyQuaternion(tb.q); // axle
          _ax.addScaledVector(_n, -_ax.dot(_n)).normalize();
          _fw.set(0, 0, 1).applyQuaternion(tb.q);
          _fw.addScaledVector(_n, -_fw.dot(_n)).normalize();
          const vs = vt.dot(_ax), vf = vt.dot(_fw);
          const js = clamp(-vs / denom(_ax), -0.9 * jn, 0.9 * jn);
          this._impulse(_J.copy(_ax).multiplyScalar(js), _r);
          const jf = clamp(-vf / denom(_fw), -0.03 * jn, 0.03 * jn);
          this._impulse(_J.copy(_fw).multiplyScalar(jf), _r);
        } else {
          const tdir = _fw.copy(vt).multiplyScalar(-1 / vtl);
          const jt = Math.min(vtl / denom(tdir), 0.5 * jn);
          this._impulse(_J.copy(tdir).multiplyScalar(jt), _r);
          if (vtl > 3) scrape = Math.max(scrape, vtl);
        }
      }
      if (push > 0) tb.cg.addScaledVector(pn, push * 0.7);
    }
    // back to the vehicle's own frame (pos: the ground point under the car, as built)
    this.pos.copy(tb.cg).sub(_v.set(0, hull.cgH, 0).applyQuaternion(tb.q));
    _v.set(0, 0, 1).applyQuaternion(tb.q);
    if (Math.hypot(_v.x, _v.z) > 0.15) this.yaw = Math.atan2(_v.x, _v.z);
    this.r = tb.w.y; tb.rPrev = this.r;
    this.vy = this.vel.y;
    // crunches, sparks and damage as it lands and rolls
    if (maxImpact > 3.5 && hitAt) {
      const dmg = (maxImpact - 3.5) * 16;
      this.damage(dmg);
      this.dent(hitAt.x, hitAt.y, hitAt.z, maxImpact * 2.2);
      if (this.game.time - this.lastHit > 0.2) {
        this.lastHit = this.game.time;
        this.game.audio?.playAt('crash', hitAt, clamp(maxImpact / 12, 0.25, 1));
        this.game.effects?.sparks?.(hitAt, maxImpact);
        this.game.effects?.dust?.(hitAt, 1.2);
        if (this.driver?.isPlayer) this.game.rig?.addShake(Math.min(0.9, maxImpact / 14));
      }
      for (const o of this.occupants) if (o && !o.isPlayer && maxImpact > 7) o.takeDamage((maxImpact - 7) * 3, { type: 'vehicle' });
      if (this.driver?.isPlayer && maxImpact > 9) this.driver.takeDamage((maxImpact - 9) * 1.5, { type: 'fall' });
      this._crashParts(hitAt, maxImpact);
    }
    tb.scrapeT -= dt;
    if (scrape > 3 && tb.scrapeT <= 0) {
      tb.scrapeT = 0.12;
      const p = this.localToWorld(0, 0.1, 0);
      this.game.effects?.sparks?.(p, Math.min(10, scrape));
      if (Math.random() < 0.3) this.game.audio?.playAt('metalhit', p, 0.35);
    }
    // the static world (buildings, props): the flat contacts still apply
    const px = this.pos.x, pz = this.pos.z;
    const list = this.game.collision.obbContacts(this.pos.x, this.pos.z, this.yaw, this.hx, this.hz, this.pos.y + 0.3, this.contacts);
    for (const ct of list) this._resolveStatic(ct);
    tb.cg.x += this.pos.x - px; tb.cg.z += this.pos.z - pz;
    tb.w.y += this.r - tb.rPrev; tb.rPrev = this.r;
    // settle
    const up = _v.set(0, 1, 0).applyQuaternion(tb.q);
    const spd = this.vel.length(), spin = tb.w.length();
    this.flipped = up.y < 0.5;
    if (up.y > 0.8 && wheels >= 3 && spin < 2.4 && Math.abs(this.vel.y) < 3) {
      if (++tb.landed > 2) { this._endTumble(); this._common(dt); return; }
    } else tb.landed = 0;
    if (touching && spd < 0.6 && spin < 0.6) tb.rest += dt; else tb.rest = 0;
    if (tb.rest > 0.5 && !tb.settled) { tb.settled = true; }
    if (tb.settled) {
      this.vel.multiplyScalar(0.5); tb.w.multiplyScalar(0.5);
      if (drvIsPlayer(this) && !tb.hinted) { tb.hinted = true; this.game.hud?.help(up.y > -0.6 ? 'On its side: <b>A / D</b> to rock it back onto its wheels, or <b>F</b> to climb out.' : 'Upside down! Get out (<b>F</b>) before it catches fire.', 5); }
    }
    // on its side: rock it back over (A / D, or the stick)
    tb.rockT -= dt;
    const drv = this.driver;
    if (tb.settled && drv?.isPlayer && Math.abs(this.input.steer) > 0.5 && tb.rockT <= 0 && up.y > -0.6) {
      tb.rockT = 0.9;
      const fwd = _j.set(0, 0, 1).applyQuaternion(tb.q);
      tb.w.addScaledVector(fwd, -Math.sign(this.input.steer) * (this.def.bike ? 1.5 : 3.2));
      this.vel.y += 1.6;
      tb.settled = false; tb.rest = 0;
    }
    // upside down: out you get, and it catches fire after a while
    if (tb.settled && this.flipped && !this.def.bike && !this.isWrecked) {
      tb.flipT += dt;
      if (up.y < -0.2 && tb.flipT > 6 && this.health > 0) this.health = 0;
      tb.bailT += dt;
      if (tb.bailT > 1.5) for (const o of this.occupants) if (o && !o.isPlayer && !o.dead && !o.remote && !this.game.vehicles.isBusy(o)) { this.game.vehicles.exit(o); o._bailFlee = this; }
    } else tb.flipT = Math.max(0, tb.flipT - dt);
    this._common(dt);
  }

  // shared end-of-step work: water, fire, wheel spin
  _common(dt) {
    const map = this.game.map;
    if (!this.sunk && map.waterDepth(this.pos.x, this.pos.z) > 1.0 && this.pos.y < WATER_Y - 0.3) {
      this.sunk = true;
      this.onSunk?.();
      this.game.events?.emit('vehicleSunk', this);
      this.game.effects?.splash(this.pos, 3);
      if (this.tb) this._endTumble();
    }
    if (!this.exploded && !this.sunk && this.health <= 0) {
      this.onFire = true;
      this.burnTime += dt;
      if (this.burnTime > 4.5) this.explode();
    }
    if (!this.tb) return;
    this.skid = 0;
    this.wheelRot += this.tb.settled ? (this.flipped && this.driver ? this.input.throttle * 12 * dt : 0) : 0;
  }

  // tall, top-heavy vehicles roll over when cornered too hard, and anything tips over on a steep side slope
  _rolloverCheck(dt) {
    if (!this.canTumble || this.airborne || this.def.bike) return;
    const d = this.def;
    const cgH = this.model.hull?.cgH ?? d.H * 0.4;
    // static stability factor (g's of sideways grip it takes to tip); army trucks are built not to roll
    const ssf = d.track / (2 * cgH) * (this.stable ? 1.6 : 1);
    const lat = Math.abs(this.ayLat) / G;
    this._rollT = lat > ssf * 1.02 && this.speedAbs > 8 ? (this._rollT || 0) + dt : 0;
    const steep = Math.abs(this.groundRoll) > 0.62 && this.speedAbs > 3;
    if (this._rollT > 0.18 || steep) {
      this._rollT = 0;
      // roll to the outside of the turn (or down the slope)
      const rgt = _v.set(-Math.cos(this.yaw), 0, Math.sin(this.yaw));
      const dir = steep ? -Math.sign(this.groundRoll) : -Math.sign(this.ayLat);
      const d2 = rgt.multiplyScalar(dir);
      this.startTumble(_w.crossVectors(UPV, d2).multiplyScalar(2.6 + Math.random()), 1.2);
    }
  }

  // a blast: tossed into the air, spinning (from Combat.explosion)
  blast(pos, k, dir) {
    if (!this.canTumble || k < 0.15) {
      this.vel.addScaledVector(dir, k * 9 * 1500 / this.mass);
      if (k > 0.4) { this.airborne = true; this.vy = Math.max(this.vy, k * 7); }
      return;
    }
    const lift = k * 9 * Math.min(1.6, 1500 / this.mass);
    this.startTumble(null, 0);
    this.vel.addScaledVector(dir, k * 8 * Math.min(1.6, 1500 / this.mass));
    this.vel.y = Math.max(this.vel.y, lift);
    // flip away from the blast, with a random twist
    this.tb.w.add(_w.crossVectors(UPV, dir).multiplyScalar(k * (3 + Math.random() * 3)));
    this.tb.w.add(_v.set(Math.random() - 0.5, Math.random() - 0.5, Math.random() - 0.5).multiplyScalar(k * 3));
  }

  // a crash: flips a lighter vehicle hit hard in the side or nose-first (from the car-car / wall contacts).
  // n: unit push direction into this vehicle, impact: closing speed, k: share of the momentum it took
  crashTumble(n, impact, k, wall = false) {
    if (!this.canTumble) return;
    const eff = impact * k;
    const fwd = _v.set(Math.sin(this.yaw), 0, Math.cos(this.yaw));
    const side = Math.abs(n.x * fwd.z - n.z * fwd.x); // 1 = hit square in the side
    const d = _j.copy(n).setY(0).normalize();
    if (this.def.bike) { if (eff > 5) this.startTumble(_w.crossVectors(UPV, d).multiplyScalar(eff * 0.35), eff * 0.12); return; }
    // tall vehicles go over more easily
    const tall = clamp((this.model.hull?.cgH ?? 0.6) / 0.62, 0.8, 1.8);
    if (side > 0.6) {
      // shoved sideways by a car the top goes with the push; sliding sideways into a wall it trips over it
      const p = clamp((eff * tall - 12) / 12, 0, 0.9);
      if (Math.random() < p) this.startTumble(_w.crossVectors(UPV, d).multiplyScalar((wall ? -1 : 1) * clamp((eff * tall - 9) * 0.32, 1.5, 7.5)), clamp(eff * 0.14, 1, 4));
    } else if (eff > 17) {
      const p = clamp((eff - 17) / 14, 0, 0.7);
      if (Math.random() < p) {
        // nose-first: the tail kicks up over the nose (or the nose over the tail)
        this.startTumble(_w.crossVectors(UPV, d).multiplyScalar(-clamp((eff - 14) * 0.18, 1, 5)), clamp(eff * 0.12, 1, 4));
      }
    }
  }

  // bumpers, doors, bonnet and boot: dented, sprung open or torn off by heavy hits near them
  _crashParts(at, impact) {
    const m = this.model, P = m.panels;
    if (!P || this.def.bike) return;
    const [lx, lz] = this.worldToLocal(at.x, at.z);
    const hz = this.hz, front = lz > hz * 0.55, rear = lz < -hz * 0.55, side = Math.abs(lx) > this.hx * 0.7;
    const r = Math.random();
    if (impact > 7 && m.hinges?.hood && front && !this._hoodOpen && r < 0.5) { this._hoodOpen = true; m.hinges.hood.rotation.x = -(0.15 + Math.random() * 0.35); }
    if (impact > 7 && m.hinges?.trunk && rear && !this._trunkOpen && r < 0.4) { this._trunkOpen = true; m.hinges.trunk.rotation.x = 0.15 + Math.random() * 0.3; }
    if (impact > 11 && front && P.bumperF && r < 0.45) this.detachPart('bumperF', at);
    if (impact > 11 && rear && P.bumperR && r < 0.45) this.detachPart('bumperR', at);
    if (impact > 14 && front && P.hood && this._hoodOpen && r < 0.3) this.detachPart('hood', at);
    if (impact > 14 && side && !front && !rear && r < 0.35) this.detachPart(lx > 0 ? 'door' : 'door2', at);
    if (impact > 13 && !this._glassBroken && r < 0.35) this.shatterGlass();
  }

  detachPart(name, at) {
    const m = this.model, mesh = m.panels?.[name];
    if (!mesh || !mesh.parent) return;
    if (name === 'door') { const dr = m.door; if (this.occupants.some((o) => o && this.game.vehicles.isBusy(o))) return; dr.pivot.traverse((o) => { if (o !== dr.pivot) o.visible = false; }); }
    this.group.updateMatrixWorld(true);
    const wm = mesh.matrixWorld.clone();
    const obj = name === 'door' ? mesh.clone() : mesh;
    if (name === 'door') { obj.visible = true; for (const c of m.door.pivot.children) if (c.material === vehicleMaterials().glass) { /* the window goes with it */ const gl = c.clone(); gl.visible = true; obj.add(gl); } }
    mesh.parent.remove(mesh);
    if (name !== 'door') delete m.panels[name];
    wm.decompose(obj.position, obj.quaternion, obj.scale);
    this.game.scene.add(obj);
    const out = _v.set(at.x - this.pos.x, 0, at.z - this.pos.z).normalize();
    const e = this.game.effects;
    if (e) e.debris.push({ obj, vx: this.vel.x * 0.8 + out.x * 3, vy: 2 + Math.random() * 3, vz: this.vel.z * 0.8 + out.z * 3, ax: (Math.random() - 0.5) * 8, az: (Math.random() - 0.5) * 8, t: 0, part: true });
    this.game.audio?.playAt('metalhit', at, 0.6);
  }

  shatterGlass() {
    if (this._glassBroken || !this.model.glass) return;
    this._glassBroken = true;
    this.model.glass.visible = false;
    this.model.door?.pivot?.traverse((o) => { if (o.material === vehicleMaterials().glass) o.visible = false; });
    this.game.effects?.glassBurst?.(this.localToWorld(0, this.def.H * 0.8, 0), this.def.W);
    this.game.audio?.playAt('glass', this.pos, 0.8);
  }

  _step(h) {
    const d = this.def, inp = this.input;
    const m = this.mass;
    const s = Math.sin(this.yaw), c = Math.cos(this.yaw);
    const fx = s, fz = c, rx = -c, rz = s;
    const vx = this.vel.x, vz = this.vel.z;
    const vLong = vx * fx + vz * fz;
    const vLat = vx * rx + vz * rz;
    const a = this.a, b = this.b, wb = a + b;
    const r = this.r;
    const wet = U.uWet.value;
    const surf = this._surface || 1;
    const mu = d.grip * (1 - wet * 0.18) * surf * (this.driver?.isPlayer ? this.game.special?.grip ?? 1 : 1) * (this.flat ? 0.6 : 1); // (the special ability grips harder; burst tyres don't)

    if (this.airborne) {
      this.vel.x -= vx * 0.02 * h; this.vel.z -= vz * 0.02 * h;
      this.r *= 1 - 0.3 * h;
      this.yaw += this.r * h;
      this.pos.x += this.vel.x * h; this.pos.z += this.vel.z * h;
      this.vy -= G * h * (this.game.gravity ?? 1);
      this.pos.y += this.vy * h;
      return;
    }

    // loads (longitudinal weight transfer)
    const hcg = 0.55;
    let Nf = m * G * b / wb - m * this.axLong * hcg / wb;
    let Nr = m * G * a / wb + m * this.axLong * hcg / wb;
    Nf = Math.max(Nf, m * G * 0.15); Nr = Math.max(Nr, m * G * 0.15);

    // front tire kinematics
    const dlt = this.steerAngle;
    const cd = Math.cos(dlt), sd = Math.sin(dlt);
    const vFlat = vLat - r * a;
    const fLong = vLong * cd - vFlat * sd;
    const fLat = vFlat * cd + vLong * sd;
    const alphaF = Math.atan2(fLat, Math.max(Math.abs(fLong), 1.6));
    const vRlat = vLat + r * b;
    const alphaR = Math.atan2(vRlat, Math.max(Math.abs(vLong), 1.6));
    const peak = 0.11;

    // longitudinal forces: engine (forward / reverse) and brakes
    const thr = this.exploded ? 0 : inp.throttle;
    const brk = inp.brake;
    let driveF = 0;
    if (thr > 0 && vLong > -0.8) { const k = clamp(vLong / (this.flat ? d.top * 0.45 : d.top), 0, 1); driveF = thr * d.force * (this.flat ? 0.6 : 1) * (1 - 0.92 * k * k); }
    if (brk > 0 && vLong < 0.8 && vLong > -12) driveF = -brk * d.force * 0.55;
    let brakeF = 0;
    if (brk > 0 && vLong > 0.8) brakeF = brk * d.brake;
    if (thr > 0 && vLong < -0.8) brakeF = thr * d.brake * 0.8;
    let rearDrive = 0, frontDrive = 0;
    if (d.drive === 'rwd') rearDrive = driveF;
    else if (d.drive === 'fwd') frontDrive = driveF;
    else { rearDrive = driveF * 0.6; frontDrive = driveF * 0.4; }
    const bSign = Math.abs(vLong) > 0.3 ? sign(vLong) : 0;
    let FxF = frontDrive - bSign * brakeF * 0.6;
    let FxR = rearDrive - bSign * brakeF * 0.4;
    // handbrake locks the rear
    let muR = mu * this.rearGrip;
    if (inp.handbrake) {
      FxR = -bSign * mu * Nr * 0.75;
      if (Math.abs(vLong) < 0.5) this.vel.multiplyScalar(1 - 3 * h);
    }
    // lateral forces from normalized tire curve
    let FyF = -mu * Nf * sat(alphaF / peak);
    let FyR = -muR * Nr * sat(alphaR / (peak * (inp.handbrake ? 1.4 : 1)));
    // traction circles
    const capF = mu * Nf, capR = muR * Nr;
    let mF = Math.hypot(FxF, FyF);
    if (mF > capF) { FxF *= capF / mF; FyF *= capF / mF; }
    let mR = Math.hypot(FxR, FyR);
    this.wheelspin = 0;
    if (mR > capR) {
      const k = capR / mR;
      if (Math.abs(rearDrive) > capR * 0.9) this.wheelspin = clamp((Math.abs(rearDrive) - capR * 0.9) / capR, 0, 1);
      FxR *= k; FyR *= k;
    }
    // world forces
    // wheel forward/right vectors for the front
    const wfx = fx * cd - rx * sd, wfz = fz * cd - rz * sd;
    const wrx = rx * cd + fx * sd, wrz = rz * cd + fz * sd;
    let Fx = FxF * wfx + FyF * wrx + FxR * fx + FyR * rx;
    let Fz = FxF * wfz + FyF * wrz + FxR * fz + FyR * rz;
    // drag & rolling resistance
    const spd = Math.hypot(vx, vz);
    const cdrag = 0.08 * d.force / (d.top * d.top);
    Fx -= vx * spd * cdrag + vx * 18 * (m / 1500);
    Fz -= vz * spd * cdrag + vz * 18 * (m / 1500);
    // low speed lateral stick (parking)
    if (Math.abs(vLong) < 3 && !inp.handbrake) {
      const k = (1 - Math.abs(vLong) / 3) * m * 4;
      Fx -= vLat * rx * k; Fz -= vLat * rz * k;
    }
    // torque
    const cross2 = (ux, uz, wx, wz) => uz * wx - ux * wz;
    let tau = cross2(fx * a, fz * a, FxF * wfx + FyF * wrx, FxF * wfz + FyF * wrz) + cross2(-fx * b, -fz * b, FxR * fx + FyR * rx, FxR * fz + FyR * rz);
    tau -= r * this.I * (spd < 4 ? 3.5 : 0.4);
    // integrate
    const ax = Fx / m, az = Fz / m;
    this.vel.x += ax * h; this.vel.z += az * h;
    this.r += tau / this.I * h;
    this.axLong = damp(this.axLong, ax * fx + az * fz, 12, h);
    this.ayLat = damp(this.ayLat, ax * rx + az * rz, 12, h);
    if (thr === 0 && brk === 0 && Math.hypot(this.vel.x, this.vel.z) < 0.25) { this.vel.x *= 0.8; this.vel.z *= 0.8; this.r *= 0.8; }
    this.yaw += this.r * h;
    this.pos.x += this.vel.x * h;
    this.pos.z += this.vel.z * h;
    this.slipRear = Math.abs(vRlat);
    this.slipFront = Math.abs(fLat);
  }

  _afterPhysics(dt) {
    const map = this.game.map, d = this.def;
    // ground sampling at wheels
    const s = Math.sin(this.yaw), c = Math.cos(this.yaw);
    const hw = d.track / 2, zf = d.wheelbase / 2, zr = -d.wheelbase / 2;
    const col = this.game.collision;
    const yRef = this.pos.y + (this.airborne ? 0.6 : 1.3);
    const gh = (lx, lz) => col.surfaceHeight(this.pos.x + lx * c + lz * s, this.pos.z - lx * s + lz * c, yRef);
    const h0 = gh(hw, zf), h1 = gh(-hw, zf), h2 = gh(hw, zr), h3 = gh(-hw, zr);
    const hF = (h0 + h1) / 2, hR = (h2 + h3) / 2, hL = (h0 + h2) / 2, hRt = (h1 + h3) / 2;
    const target = (hF + hR) / 2;
    this._surface = map.isOnRoad(this.pos.x, this.pos.z) || Math.abs(target - 0.15) < 0.01 ? 1 : 0.85;
    // cliff / steep terrain blocking
    const fwdBlock = (hF - this.pos.y > 0.9 && this.speed > 0) || (hR - this.pos.y > 0.9 && this.speed < 0);
    if (fwdBlock && !this.airborne) {
      const vl = this.speed;
      this.vel.x -= s * vl * 1.3; this.vel.z -= c * vl * 1.3;
      this.pos.x -= s * sign(vl) * 0.1; this.pos.z -= c * sign(vl) * 0.1;
      if (Math.abs(vl) > 8) this.damage(Math.abs(vl) * 8);
    }
    if (this.airborne) {
      if (this.pos.y <= target) {
        this.pos.y = target;
        const impact = -this.vy;
        this.airborne = false;
        this.bodyYV -= impact * 0.35;
        if (impact > 9) { this.damage((impact - 9) * 25); this.game.audio?.playAt('crash', this.pos, Math.min(1, impact / 20)); }
        this.vy = 0;
      }
    } else {
      const dy = target - this.pos.y;
      if (dy < -0.4) {
        // ground dropped away: launch with previous vertical velocity
        this.airborne = true;
        this.vy = Math.max(this.groundVy, -2);
      } else {
        if (Math.abs(dy) < 0.25) this.groundVy = damp(this.groundVy, dy / Math.max(dt, 1e-3), 20, dt);
        else { this.groundVy = 0; this.bodyYV += dy > 0 ? 1.5 : -0.5; }
        this.pos.y = target;
      }
    }
    const tgtPitch = Math.atan2(hF - hR, d.wheelbase);
    const tgtRoll = Math.atan2(hL - hRt, d.track);
    if (!this.airborne) { this.groundPitch = damp(this.groundPitch, tgtPitch, 12, dt); this.groundRoll = damp(this.groundRoll, tgtRoll, 12, dt); }
    else { this.groundPitch = damp(this.groundPitch, clamp(this.vy * 0.03, -0.4, 0.3), 1.5, dt); }

    // static collisions
    const list = this.game.collision.obbContacts(this.pos.x, this.pos.z, this.yaw, this.hx, this.hz, this.pos.y, this.contacts);
    for (const ct of list) this._resolveStatic(ct);

    // water, fire & explosion
    this._common(dt);
    if (this.sunk) {
      this.vel.multiplyScalar(1 - dt * 1.5);
      this.pos.y = Math.max(map.groundHeight(this.pos.x, this.pos.z), this.pos.y - dt * 0.6);
      this.airborne = false;
    }
    this.wheelRot += this.speed * dt / this.def.wheelR;
    this.skid = (this.slipRear > 3.2 || this.wheelspin > 0.2 || (this.input.handbrake && Math.abs(this.speed) > 4) || (this.input.brake > 0.5 && this.speed > 12)) && !this.airborne ? 1 : 0;
  }

  _resolveStatic(ct) {
    const o = ct.obj;
    if (o.kind === 'circle' && o.breakable && !o.broken && this.speedAbs > 2.5) {
      // smash through street furniture
      this.game.city?.breakProp(o);
      this.game.effects?.propDebris?.(o, this.vel);
      this.vel.multiplyScalar(0.9);
      this.damage(this.speedAbs * 1.5);
      this.game.audio?.playAt('metalhit', this.pos, 0.6);
      if (o.prop?.type === 'hydrant') this.game.effects?.hydrantSpray?.(o.x, o.prop.y, o.z);
      return;
    }
    // push out
    this.pos.x += ct.nx * ct.depth;
    this.pos.z += ct.nz * ct.depth;
    // impulse at contact point
    const px = ct.px - this.pos.x, pz = ct.pz - this.pos.z;
    const vpx = this.vel.x + this.r * pz, vpz = this.vel.z - this.r * px;
    const vn = vpx * ct.nx + vpz * ct.nz;
    if (vn >= 0) return;
    const e = 0.18;
    const rn = pz * ct.nx - px * ct.nz; // cross2(p, n)
    const j = -(1 + e) * vn / (1 / this.mass + rn * rn / this.I);
    this.vel.x += j * ct.nx / this.mass;
    this.vel.z += j * ct.nz / this.mass;
    this.r += rn * j / this.I;
    // friction along the wall
    const tx = -ct.nz, tz = ct.nx;
    const vt = vpx * tx + vpz * tz;
    const jt = clamp(-vt * this.mass * 0.25, -j * 0.4, j * 0.4);
    this.vel.x += jt * tx / this.mass; this.vel.z += jt * tz / this.mass;
    const impact = -vn;
    if (impact > 3) {
      this.damage((impact - 3) * 14);
      this.dent(ct.px, this.pos.y + 0.6, ct.pz, impact);
      if (impact > 9) this._crashParts(_p.set(ct.px, this.pos.y + 0.5, ct.pz), impact);
      if (impact > 15 && !this.tb) this.crashTumble(new THREE.Vector3(ct.nx, 0, ct.nz), impact, 1, true);
      if (this.game.time - this.lastHit > 0.25) {
        this.lastHit = this.game.time;
        this.game.audio?.playAt('crash', this.pos, clamp(impact / 18, 0.2, 1));
        this.game.effects?.sparks?.(new THREE.Vector3(ct.px, this.pos.y + 0.5, ct.pz), impact);
        if (this.driver?.isPlayer) this.game.rig?.addShake(Math.min(0.8, impact / 25));
      }
      this.onCrash?.(impact, o);
    }
  }

  damage(amount, source = null) {
    if (this.exploded) return;
    if (this.game.cheatsOn?.vehGod && this.driver?.isPlayer) return;
    if (this.remote) { this.game.net?.sendVehicleHit(this, amount, source); return; } // another player's: their client decides
    this.health -= amount;
    if (source) this.lastDamager = source;
    this.game.events?.emit('vehicleDamaged', this, amount, source);
  }

  // the painted panels that dent (cars list theirs; other models only have a body)
  _panels() { const P = this.model.panels; return P ? Object.values(P).filter((m) => m && m.parent) : [this.model.body]; }

  dent(wx, wy, wz, strength) {
    if (strength < 5) return;
    if (this.game.cheatsOn?.vehGod && this.driver?.isPlayer) return;
    // deform body vertices near the impact point (copy-on-write geometry)
    const rad = 0.9;
    const amt = Math.min(0.16, strength * 0.006);
    const cy = this.def.H * 0.45;
    const wp = new THREE.Vector3(wx, wy, wz), lp = new THREE.Vector3(), cp = new THREE.Vector3();
    const inv = new THREE.Matrix4();
    this.group.updateMatrixWorld(true);
    for (const body of this._panels()) {
      if (!body.geometry?.attributes?.position || body.geometry.userData.shared) continue;
      inv.copy(body.matrixWorld).invert();
      lp.copy(wp).applyMatrix4(inv);
      // the car's centre in this panel's frame (doors hang off hinges)
      cp.set(0, cy, 0).applyMatrix4(this.model.bodyGroup.matrixWorld).applyMatrix4(inv);
      const pos = body.geometry.attributes.position;
      if (!body.userData.undented) body.userData.undented = pos.array.slice();
      let changed = false;
      for (let i = 0; i < pos.count; i++) {
        const x = pos.getX(i), y = pos.getY(i), z = pos.getZ(i);
        const dd = Math.hypot(x - lp.x, y - lp.y, z - lp.z);
        if (dd < rad) {
          const k = (1 - dd / rad) * amt;
          const dx = cp.x - x, dy = cp.y - y, dz = cp.z - z;
          const l = Math.hypot(dx, dy, dz) || 1;
          pos.setXYZ(i, x + dx / l * k, y + dy / l * k * 0.4, z + dz / l * k);
          changed = true;
        }
      }
      if (changed) { pos.needsUpdate = true; body.userData.normalsDirty = true; this._normalsDirty = true; }
    }
  }

  // put the bodywork back as built (repair)
  undent() {
    for (const body of this._panels()) {
      const u = body.userData.undented;
      if (!u) continue;
      const pos = body.geometry.attributes.position;
      pos.array.set(u);
      pos.needsUpdate = true;
      body.userData.normalsDirty = true;
    }
    this._normalsDirty = true;
  }

  explode() {
    if (this.exploded) return;
    this.exploded = true;
    this.onFire = false;
    this.health = 0;
    const M = vehicleMaterials();
    for (const m of this._panels()) m.material = M.burnt;
    this.model.door.mesh.material = M.burnt;
    this.model.glass.visible = false;
    this.model.door.pivot.traverse((o) => { if (o.material === M.glass) o.visible = false; });
    this.model.head.material = M.headOff;
    this.model.tail.material = M.tailOff;
    if (this.canTumble && !this.sunk) {
      this.startTumble(new THREE.Vector3((Math.random() - 0.5) * 5, (Math.random() - 0.5) * 3, (Math.random() - 0.5) * 5), 6 + Math.random() * 3);
      this.shatterGlass();
    } else {
      this.vy = 6 + Math.random() * 3;
      this.airborne = true;
      this.r += (Math.random() - 0.5) * 3;
    }
    this.game.combat?.vehicleExplosion(this, this.pos.clone().add(new THREE.Vector3(0, this.def.H * 0.45, 0)));
    for (const o of this.occupants) if (o) { o.takeDamage(1000, { type: 'explosion', source: this.lastDamager }); }
    this.game.events?.emit('vehicleExploded', this);
    this.wreckTime = 0;
  }

  // ------------------------------------------------------------------ visuals
  _updateVisual(dt) {
    const g = this.group;
    if (this.tb) g.quaternion.copy(this.tb.q);
    else if (this.netQ) g.quaternion.copy(this.netQ); // (another player's car, tumbling)
    else {
      g.rotation.y = this.yaw;
      g.rotation.x = -this.groundPitch;
      g.rotation.z = this.groundRoll;
    }
    // body suspension springs
    const tp = clamp(this.axLong * 0.008, -0.07, 0.07);
    const tr = clamp(-this.ayLat * 0.011, -0.09, 0.09);
    const k = 90, cdamp = 11;
    this.bodyPitchV += ((tp - this.bodyPitch) * k - this.bodyPitchV * cdamp) * dt;
    this.bodyRollV += ((tr - this.bodyRoll) * k - this.bodyRollV * cdamp) * dt;
    this.bodyYV += ((-this.bodyY) * 120 - this.bodyYV * 9) * dt;
    this.bodyPitch += this.bodyPitchV * dt;
    this.bodyRoll += this.bodyRollV * dt;
    this.bodyY += this.bodyYV * dt;
    if (this.def.hydraulics) {
      this.hydraulicV += (-this.hydraulic * 30 - this.hydraulicV * 2.5) * dt;
      this.hydraulic += this.hydraulicV * dt;
    }
    // distant cars drop their small parts (interior, trim, chrome)
    if (this.model.detail && (this._lodT = (this._lodT ?? Math.random() * 0.5) - dt) <= 0) {
      this._lodT = 0.5;
      const cam = this.game.camera?.position;
      const near = !cam || (cam.x - this.pos.x) ** 2 + (cam.z - this.pos.z) ** 2 < 75 * 75;
      for (const m of this.model.detail) m.visible = near;
    }
    const bg = this.model.bodyGroup;
    bg.rotation.x = this.bodyPitch;
    bg.rotation.z = this.bodyRoll;
    bg.position.y = clamp(this.bodyY, -0.15, 0.15) + (this.def.hydraulics ? clamp(this.hydraulic, -0.1, 0.5) : 0) - (this.flat ? 0.07 : 0);
    // wheels
    for (const w of this.model.wheels) {
      w.spin.rotation.x = this.wheelRot;
      if (w.front) w.pivot.rotation.y = this.steerAngle;
    }
    // door
    const door = this.model.door;
    door.pivot.rotation.y = door.open * (door.max ?? 1.1);
    if (this._normalsDirty && dt > 0) {
      for (const m of this._panels()) if (m.userData.normalsDirty || m === this.model.body) { m.geometry.computeVertexNormals(); m.userData.normalsDirty = false; }
      this._normalsDirty = false;
    }
    // lights
    const M = vehicleMaterials();
    const night = U.uNight.value > 0.4;
    const on = (night || this.game.env?.rain > 0.3) && !!this.driver && !this.isWrecked;
    if (on !== this.lightsOn) { this.lightsOn = on; this.model.head.material = on ? M.headOn : M.headOff; }
    if (this.model.beam) this.model.beam.visible = on && !this.airborne;
    const braking = !!this.driver && (this.input.brake > 0.1 && this.speed > 0.5 || this.input.handbrake);
    const tailMat = this.isWrecked ? M.tailOff : braking ? M.tailBrake : on ? M.tailOn : M.tailOff;
    if (this.model.tail.material !== tailMat) this.model.tail.material = tailMat;
    if (this.model.lightbar) {
      const t = this.game.time * 7 + this.sirenPhase;
      const onR = this.sirenOn && Math.sin(t) > 0, onB = this.sirenOn && Math.sin(t) <= 0;
      this.model.lightbar.red.material.emissive.setRGB(onR ? 9 : 0.15, onR ? 0.3 : 0, 0);
      this.model.lightbar.blue.material.emissive.setRGB(0, onB ? 0.6 : 0, onB ? 12 : 0.2);
    }
  }

  remove() {
    if (this.removed) return;
    this.removed = true;
    for (const o of this.occupants) {
      if (!o || o.isPlayer) continue;
      if (o.remote && !o.npcProxy) { this.takeOut(o); continue; } // another player's avatar just steps out
      if (this.game.peds) this.game.peds.remove(o); else o.remove();
    }
    this.group.parent?.remove(this.group);
    // free GPU resources owned by this car
    const seen = new Set();
    this.group.traverse((obj) => {
      if (obj.userData.character) return;
      if (!obj.isMesh || obj.isSkinnedMesh) return;
      const g = obj.geometry;
      if (g && !g.userData.shared && !seen.has(g)) { seen.add(g); g.dispose(); }
      const mats = Array.isArray(obj.material) ? obj.material : [obj.material];
      for (const m of mats) if (m && !isSharedMaterial(m) && !seen.has(m)) { seen.add(m); m.dispose(); }
    });
  }
}
