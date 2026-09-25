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
let nextVid = 1;

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
    const model = buildVehicleModel(d, this.color);
    this.model = model;
    this.group = model.group;
    this.group.rotation.order = 'YXZ';
    this.group.userData.vehicle = this;
    game.scene.add(this.group);
    this.pos = this.group.position;
    this.yaw = opts.yaw || 0;
    this.pos.set(opts.x || 0, 0, opts.z || 0);
    this.pos.y = game.map.groundHeight(this.pos.x, this.pos.z);
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
    this.health = 1000;
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
    this._updateVisual(0);
  }

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
    this.occupants[seat] = char;
    char.vehicle = this;
    char.seat = seat;
    char.vel.set(0, 0, 0);
    char.ragdolling = false;
    const s = this.model.seats[seat];
    this.model.bodyGroup.add(char.root);
    char.root.position.set(s.x, s.y - 0.52 * (char.appearance.height || 1) + 0.52, s.z);
    char.root.rotation.set(0, 0, 0);
    char.yaw = this.yaw;
    if (char.weaponMesh && char.weaponDef.type !== 'gun') char.weaponMesh.visible = false;
    this.parked = false;
  }
  takeOut(char, pos) {
    const seat = this.occupants.indexOf(char);
    if (seat >= 0) this.occupants[seat] = null;
    char.vehicle = null;
    char.seat = -1;
    this.game.scene.add(char.root);
    const p = pos || this.doorWorld();
    char.pos.set(p.x, this.game.map.groundHeight(p.x, p.z), p.z);
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

    const steps = dt > 1 / 45 ? 3 : 2;
    const h = dt / steps;
    for (let i = 0; i < steps; i++) this._step(h);
    this._afterPhysics(dt);
    this._updateVisual(dt);
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
    const mu = d.grip * (1 - wet * 0.18) * surf;

    if (this.airborne) {
      this.vel.x -= vx * 0.02 * h; this.vel.z -= vz * 0.02 * h;
      this.r *= 1 - 0.3 * h;
      this.yaw += this.r * h;
      this.pos.x += this.vel.x * h; this.pos.z += this.vel.z * h;
      this.vy -= G * h;
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
    if (thr > 0 && vLong > -0.8) { const k = clamp(vLong / d.top, 0, 1); driveF = thr * d.force * (1 - 0.92 * k * k); }
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
    const gh = (lx, lz) => map.groundHeight(this.pos.x + lx * c + lz * s, this.pos.z - lx * s + lz * c);
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

    // water
    if (!this.sunk && map.waterDepth(this.pos.x, this.pos.z) > 1.0 && this.pos.y < WATER_Y - 0.3) {
      this.sunk = true;
      this.onSunk?.();
      this.game.events?.emit('vehicleSunk', this);
      this.game.effects?.splash(this.pos, 3);
    }
    if (this.sunk) {
      this.vel.multiplyScalar(1 - dt * 1.5);
      this.pos.y = Math.max(map.groundHeight(this.pos.x, this.pos.z), this.pos.y - dt * 0.6);
      this.airborne = false;
    }

    // fire & explosion
    if (!this.exploded && !this.sunk) {
      if (this.health <= 0) {
        this.onFire = true;
        this.burnTime += dt;
        if (this.burnTime > 4.5) this.explode();
      }
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
    this.health -= amount;
    if (source) this.lastDamager = source;
    this.game.events?.emit('vehicleDamaged', this, amount, source);
  }

  dent(wx, wy, wz, strength) {
    if (strength < 5) return;
    // deform body vertices near the impact point (copy-on-write geometry)
    const body = this.model.body;
    const inv = new THREE.Matrix4().copy(body.matrixWorld).invert();
    body.updateMatrixWorld(true);
    const lp = new THREE.Vector3(wx, wy, wz).applyMatrix4(inv);
    const pos = body.geometry.attributes.position;
    const rad = 0.9;
    const amt = Math.min(0.16, strength * 0.006);
    const cx = 0, cy = this.def.H * 0.45, cz = 0;
    let changed = false;
    for (let i = 0; i < pos.count; i++) {
      const x = pos.getX(i), y = pos.getY(i), z = pos.getZ(i);
      const dd = Math.hypot(x - lp.x, y - lp.y, z - lp.z);
      if (dd < rad) {
        const k = (1 - dd / rad) * amt;
        const dx = cx - x, dy = cy - y, dz = cz - z;
        const l = Math.hypot(dx, dy, dz) || 1;
        pos.setXYZ(i, x + dx / l * k, y + dy / l * k * 0.4, z + dz / l * k);
        changed = true;
      }
    }
    if (changed) { pos.needsUpdate = true; this._normalsDirty = true; }
  }

  explode() {
    if (this.exploded) return;
    this.exploded = true;
    this.onFire = false;
    this.health = 0;
    const M = vehicleMaterials();
    this.model.body.material = M.burnt;
    this.model.door.mesh.material = M.burnt;
    this.model.glass.visible = false;
    this.model.head.material = M.headOff;
    this.model.tail.material = M.tailOff;
    this.vy = 6 + Math.random() * 3;
    this.airborne = true;
    this.r += (Math.random() - 0.5) * 3;
    this.game.combat?.explosion(this.pos.clone().add(new THREE.Vector3(0, 0.8, 0)), 9, 180, this.lastDamager, this);
    for (const o of this.occupants) if (o) { o.takeDamage(1000, { type: 'explosion', source: this.lastDamager }); }
    this.game.events?.emit('vehicleExploded', this);
    this.wreckTime = 0;
  }

  // ------------------------------------------------------------------ visuals
  _updateVisual(dt) {
    const g = this.group;
    g.rotation.y = this.yaw;
    g.rotation.x = -this.groundPitch;
    g.rotation.z = this.groundRoll;
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
    const bg = this.model.bodyGroup;
    bg.rotation.x = this.bodyPitch;
    bg.rotation.z = this.bodyRoll;
    bg.position.y = clamp(this.bodyY, -0.15, 0.15) + (this.def.hydraulics ? clamp(this.hydraulic, -0.1, 0.5) : 0);
    // wheels
    for (const w of this.model.wheels) {
      w.spin.rotation.x = this.wheelRot;
      if (w.front) w.pivot.rotation.y = this.steerAngle;
    }
    // door
    const door = this.model.door;
    door.pivot.rotation.y = door.open * 1.1;
    if (this._normalsDirty && dt > 0) { this.model.body.geometry.computeVertexNormals(); this._normalsDirty = false; }
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
    for (const o of this.occupants) if (o && !o.isPlayer) { if (this.game.peds) this.game.peds.remove(o); else o.remove(); }
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
