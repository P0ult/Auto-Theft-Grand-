// Flyable planes, jets and helicopters, plus the tank. They share the Vehicle interface (occupants, damage,
// enter / exit, collisions with cars and people) but run their own physics, controls and weapons.
//
// Planes use an arcade flight model: thrust along the nose, lift up to 1 g that fades below stall speed, air "grip"
// that swings the velocity onto the nose, and weathervaning that swings the nose onto the velocity (coordinated turns,
// nose drop in a stall). Helicopters tilt their rotor disc to move and hold altitude on their own.
import * as THREE from 'three';
import { Vehicle } from './vehicle.js';
import { buildAircraftModel } from './aircraftmodels.js';
import { vehicleMaterials } from './vehiclemodels.js';
import { WATER_Y } from '../world/citymap.js';
import { clamp, damp, wrapAngle, sign } from '../core/utils.js';

const G = 9.81;
const X = new THREE.Vector3(1, 0, 0), Y = new THREE.Vector3(0, 1, 0), Z = new THREE.Vector3(0, 0, 1);
const _f = new THREE.Vector3(), _u = new THREE.Vector3(), _l = new THREE.Vector3(), _v = new THREE.Vector3(), _w = new THREE.Vector3(), _a = new THREE.Vector3();
const _q = new THREE.Quaternion(), _qi = new THREE.Quaternion(), _e = new THREE.Euler(0, 0, 0, 'YXZ');

// Where the crosshair (screen centre) points in the world
export function aimPoint(game, maxDist = 900, exclude = null) {
  const cam = game.camera;
  const dir = game.rig.lookDir(new THREE.Vector3());
  const hit = game.combat.raycast(cam.position.x, cam.position.y, cam.position.z, dir.x, dir.y, dir.z, maxDist, exclude);
  return hit && hit.t > 6 ? hit.point : cam.position.clone().addScaledVector(dir, maxDist);
}

const CANNON = { id: 'cannon', damage: 34, range: 900, spread: 0.012, sound: 'rifle' };
const MINIGUN = { id: 'minigun', damage: 26, range: 500, spread: 0.02, sound: 'smg' };

class AirVehicle extends Vehicle {
  buildModel(d, color) { return buildAircraftModel(d, color, this.type); }

  setup() {
    const d = this.def;
    // simulation reference = ground point under the centre of gravity; the mesh is placed from it every frame
    this.pos = this.group.position.clone();
    this.cgY = this.model.cgY;
    this.hx = (d.colW ?? d.W) / 2;
    this.I = this.mass * (d.L * d.L + (2 * this.hx) ** 2) / 12;
    this.quat = new THREE.Quaternion().setFromAxisAngle(Y, this.yaw);
    this.grounded = true;
    this.gearDown = true; this.gearK = 1;
    this.power = 0;   // throttle lever 0..1
    this.spool = 0;   // engine / rotor speed 0..1
    this.angVel = new THREE.Vector3();
    this.ctl = { pitch: 0, roll: 0, yaw: 0, coll: 0, brake: 0 };
    this.mouseP = 0; this.mouseR = 0;
    this.gunT = 0; this.missileT = 0; this.side = 1;
    this.propAngle = Math.random() * 6;
    this.burnT = 0;
  }

  get armed() { return !!this.def.weapons; }
  get showCrosshair() { return this.armed; }
  get forwardSpeed() { return this.vel.dot(_f.copy(Z).applyQuaternion(this.quat)); }
  get throttle() { return this.def.kind === 'heli' ? this.spool : this.power; }
  get altitude() { return this.pos.y - Math.max(this.game.map.groundHeight(this.pos.x, this.pos.z), WATER_Y); }

  cg(out = new THREE.Vector3()) { return out.set(this.pos.x, this.pos.y + this.cgY, this.pos.z); }
  // model-space point (as built: y = 0 is the ground contact) to world space, following the full attitude
  localPoint(x, y, z, out = new THREE.Vector3()) {
    return out.set(x, y - this.cgY, z).applyQuaternion(this.quat).add(this.cg(_a));
  }

  putIn(char, seat = 0) {
    super.putIn(char, seat);
    char.hiddenInVehicle = seat >= (this.model.visibleSeats ?? 4);
    if (char.hiddenInVehicle) char.root.visible = false;
  }
  takeOut(char, pos) {
    super.takeOut(char, pos);
    char.hiddenInVehicle = false;
    char.root.visible = true;
  }

  dent() {}

  // surface under the aircraft (terrain, bridge decks, roofs for helicopters) and whether it is water
  _floor() {
    const x = this.pos.x, z = this.pos.z;
    const col = this.game.collision;
    const ground = this.def.kind === 'heli' ? col.floorHeight(x, z, this.pos.y + 0.6) : col.surfaceHeight(x, z, this.pos.y + 1.2);
    const wet = this.game.map.waterDepth(x, z) > 1.2 && ground < WATER_Y;
    return wet ? { y: WATER_Y - 0.2, water: true } : { y: ground, water: false };
  }

  crash(speed, where = null) {
    if (this.exploded) return;
    const c = where || this.cg();
    this.game.effects?.sparks?.(c, 24);
    this.game.audio?.playAt('crash', c, 1);
    if (this.driver?.isPlayer) this.game.rig?.addShake(1);
    this.damage(speed * 55, this.lastDamager);
    if (this.health <= 0 || speed > 22) this.explode();
  }

  explode() {
    if (this.exploded) return;
    this.exploded = true;
    this.onFire = false;
    this.health = 0;
    this.power = 0;
    const M = vehicleMaterials();
    for (const m of this.model.meshes) {
      if (m.material.transparent || m.material === M.glass) m.visible = false;
      else m.material = M.burnt;
    }
    const c = this.cg();
    const big = this.def.L > 20;
    this.game.combat?.explosion(c.clone(), big ? 16 : this.def.kind === 'heli' ? 11 : 13, 260, this.lastDamager, this);
    if (big) for (let i = 0; i < 3; i++) setTimeout(() => this.game.effects?.explosion(this.localPoint((Math.random() - 0.5) * 24, this.cgY + 1, (Math.random() - 0.5) * 18), 7), 150 + i * 220);
    for (const o of this.occupants) if (o) o.takeDamage(1000, { type: 'explosion', source: this.lastDamager });
    this.game.events?.emit('vehicleExploded', this);
    this.wreckTime = 0;
    if (!this.grounded) { this.angVel.set((Math.random() - 0.5) * 2, (Math.random() - 0.5) * 3, (Math.random() - 0.5) * 3); }
  }

  // tumble to the ground as a wreck
  _wreckStep(h) {
    const fl = this._floor();
    if (!this.grounded) {
      this.vel.y -= G * h;
      this.vel.multiplyScalar(1 - 0.15 * h);
      if (this.angVel.lengthSq() > 1e-6) { _q.setFromAxisAngle(_v.copy(this.angVel).normalize(), this.angVel.length() * h); this.quat.multiply(_q); }
    }
    this.pos.addScaledVector(this.vel, h);
    if (this.pos.y <= fl.y) {
      if (!this.grounded && this.vel.y < -8) this.game.effects?.explosion(this.cg(), 5);
      this.pos.y = fl.y;
      this.vel.y = 0;
      this.vel.x *= Math.exp(-4 * h); this.vel.z *= Math.exp(-4 * h);
      this.angVel.multiplyScalar(Math.exp(-6 * h));
      if (!this.grounded) {
        // settle roughly level with a bit of a lean
        _f.copy(Z).applyQuaternion(this.quat);
        const yaw = Math.atan2(_f.x, _f.z);
        this.quat.setFromEuler(_e.set(-0.05 + Math.random() * 0.1, yaw, (Math.random() - 0.5) * 0.35, 'YXZ'));
      }
      this.grounded = true;
      if (fl.water && !this.sunk) { this.sunk = true; this.game.effects?.splash(this.cg(), 3); }
    }
    if (this.sunk) this.pos.y = Math.max(this.game.map.groundHeight(this.pos.x, this.pos.z), this.pos.y - h * 0.8);
  }

  // cars / buildings: push out when slow, crash when fast
  _statics() {
    const list = this.game.collision.obbContacts(this.pos.x, this.pos.z, this.yaw, this.hx, this.hz * 0.92, this.pos.y + this.cgY - 0.7, this.contacts);
    for (const ct of list) {
      if (ct.obj.kind === 'circle' && ct.obj.breakable && !ct.obj.broken) { this.game.city?.breakProp(ct.obj); continue; }
      const vn = this.vel.x * ct.nx + this.vel.z * ct.nz;
      if (-vn > 12 || (this.vel.length() > 16 && !this.grounded)) { this.crash(Math.max(-vn, this.vel.length() * 0.7), new THREE.Vector3(ct.px, this.pos.y + this.cgY, ct.pz)); if (this.exploded) return; }
      this.pos.x += ct.nx * ct.depth; this.pos.z += ct.nz * ct.depth;
      if (vn < 0) { this.vel.x -= ct.nx * vn * 1.3; this.vel.z -= ct.nz * vn * 1.3; if (-vn > 3) this.damage((-vn - 3) * 20); }
    }
  }

  _water(fl) {
    if (!fl.water || this.sunk) return false;
    this.game.effects?.splash(this.cg(), 3);
    if (this.vel.length() > 22) { this.explode(); return true; }
    this.sunk = true;
    this.onSunk?.();
    this.game.events?.emit('vehicleSunk', this);
    return true;
  }

  // parked, empty and still: skip the physics (the door / canopy can still animate)
  _asleep(dt) {
    if (this.driver || !this.grounded || this.exploded || this.sunk || this.health <= 0 || this.spool > 0.005 || this.vel.lengthSq() > 1e-4) return false;
    this.airborne = false;
    this._commonVisual(dt);
    return true;
  }

  _placeGroup() {
    const g = this.group;
    g.quaternion.copy(this.quat);
    // rotate about the centre of gravity rather than the ground point
    _v.set(0, this.cgY, 0).applyQuaternion(this.quat);
    g.position.set(this.pos.x - _v.x, this.pos.y + this.cgY - _v.y, this.pos.z - _v.z);
  }

  _commonVisual(dt) {
    const m = this.model;
    // door / canopy / hatch
    const door = m.door;
    if (door.axis) door.pivot.rotation[door.axis] = door.open * door.max;
    // strobe (shared material: all strobes flash together)
    if (m.strobe) {
      const on = (this.game.time % 1.3) < 0.07 && !this.exploded;
      m.strobe.material.emissive.setRGB(on ? 12 : 0.05, on ? 12 : 0.05, on ? 12 : 0.05);
    }
  }
}

// ====================================================================================== planes & jets
export class Plane extends AirVehicle {
  playerControl(input, dt) {
    const c = this.ctl, gp = input.gp;
    if (input.key('KeyW') || gp.rt > 0.3) this.power = Math.min(1, this.power + dt * (0.5 + gp.rt * 0.3));
    if (input.key('KeyS') || gp.lt > 0.3) this.power = Math.max(0, this.power - dt * 0.7);
    c.brake = input.key('Space') ? 1 : 0;
    c.reverse = this.grounded && this.power === 0 && input.key('KeyS');
    // the mouse behaves like a spring-centred stick
    const [mdx, mdy] = input.lookDelta();
    this.mouseP = clamp(this.mouseP * Math.exp(-dt * 6) - mdy * 7, -1, 1);
    this.mouseR = clamp(this.mouseR * Math.exp(-dt * 6) + mdx * 7, -1, 1);
    const keyP = (input.key('ArrowDown') ? 1 : 0) - (input.key('ArrowUp') ? 1 : 0);
    const keyR = (input.key('KeyD') || input.key('ArrowRight') ? 1 : 0) - (input.key('KeyA') || input.key('ArrowLeft') ? 1 : 0);
    c.pitch = clamp(keyP + this.mouseP + (gp.ly || 0), -1, 1);
    c.roll = clamp(keyR + this.mouseR + (gp.lx || 0), -1, 1);
    c.yaw = clamp((input.key('KeyE') ? 1 : 0) - (input.key('KeyQ') ? 1 : 0), -1, 1);
    this.horn = false;
    if (this.armed && !this.exploded && this.health > 0) {
      if (input.mouse.left && this.gunT <= 0) this.fireCannon();
      if (input.mouse.rightPressed && this.missileT <= 0) this.fireMissile();
    }
  }

  fireCannon() {
    this.gunT = 0.07;
    const f = _f.copy(Z).applyQuaternion(this.quat).clone();
    const mz = this.model.muzzles[0];
    const muzzle = this.localPoint(mz.x, mz.y, mz.z);
    // converge on the crosshair (a point far along the nose)
    this.game.combat.vehicleGun(this.driver, muzzle, f, CANNON);
    if (this.driver?.isPlayer) this.game.rig.addShake(0.08);
  }

  fireMissile() {
    this.missileT = 0.6;
    const pyl = this.model.pylons;
    const p = pyl[(this.side = (this.side + 1) % pyl.length)];
    const pos = this.localPoint(p.x, p.y, p.z);
    const f = _f.copy(Z).applyQuaternion(this.quat).clone();
    const target = this.game.combat.lockTarget(this.cg(), f, this);
    this.game.combat.fireProjectile(this.driver, 'missile', pos, f, { speed: 70, maxSpeed: 230, accel: 140, inherit: this.vel, target, turn: 2.6, radius: 10, damage: 300, life: 7 });
    if (target && this.driver?.isPlayer) this.game.hud?.help('<b>Missile locked</b>', 1.2);
  }

  update(dt) {
    if (this.removed || this._asleep(dt)) return;
    const d = this.def, c = this.ctl;
    this.gunT -= dt; this.missileT -= dt;
    const ctrl = !!this.driver && this.health > 0 && !this.exploded;
    if (!ctrl) {
      c.pitch = c.roll = c.yaw = 0;
      c.reverse = false;
      c.brake = this.grounded ? 1 : 0;
      if (!this.driver && this.grounded) this.power = Math.max(0, this.power - dt * 0.5);
      if (this.health <= 0 && !this.exploded) { this.power = 0; c.roll = 0.6; c.pitch = -0.4; }
    }
    if (this.health <= 0 && !this.exploded) {
      this.onFire = true;
      this.burnT += dt;
      if (this.burnT > (this.grounded ? 4.5 : 14)) this.explode();
    }
    const steps = Math.max(1, Math.ceil(dt * 90));
    const h = dt / steps;
    for (let i = 0; i < steps && !this.removed; i++) {
      if (this.exploded || this.sunk) this._wreckStep(h);
      else this._fly(h);
    }
    _f.copy(Z).applyQuaternion(this.quat);
    this.yaw = Math.atan2(_f.x, _f.z);
    this.airborne = !this.grounded;
    if (!this.exploded && !this.sunk) this._statics();
    // landing gear: tucks away once climbing out, comes down low and slow
    const alt = this.altitude;
    if (this.grounded) this.gearDown = true;
    else if (alt > 35 && this.forwardSpeed > d.vRotate) this.gearDown = false;
    else if (alt < 28) this.gearDown = true;
    this.gearK = clamp(this.gearK + (this.gearDown ? dt : -dt) * 0.8, 0, 1);
    this._updateVisual(dt);
  }

  _fly(h) {
    const d = this.def, c = this.ctl, q = this.quat;
    const f = _f.copy(Z).applyQuaternion(q);
    const u = _u.copy(Y).applyQuaternion(q);
    // engine spools toward the lever
    const lever = this.health > 0 ? this.power : 0;
    this.spool += (lever - this.spool) * Math.min(1, h * (d.kind === 'jet' ? 0.9 : 1.6));
    const V = this.vel.length();
    const vf = this.vel.dot(f);
    const auth = clamp((vf - d.vStall * 0.3) / (d.vStall * 0.7), 0, 1);
    const fl = this._floor();

    if (this.grounded) {
      // ----------------------------------------------------------- on the wheels
      let pitch = Math.asin(clamp(f.y, -1, 1));
      let yaw = Math.atan2(f.x, f.z);
      const steer = clamp(c.roll + c.yaw, -1, 1);
      yaw -= steer * h * clamp(1.1 / (1 + Math.abs(vf) / 9), 0.12, 1.1) * (Math.abs(vf) > 0.3 || this.spool > 0.2 ? 1 : 0);
      const canRotate = vf > d.vRotate * 0.82;
      const tP = canRotate && c.pitch > 0 ? 0.22 * c.pitch : 0;
      pitch += clamp(tP - pitch, -h * 0.5, h * 0.5);
      q.setFromEuler(_e.set(-pitch, yaw, 0, 'YXZ'));
      let v = vf;
      if (c.reverse) v += (-2.5 - v) * Math.min(1, h * 1.5); // push back slowly
      else {
        v += d.thrust * this.spool * h;
        v -= sign(v) * Math.min(Math.abs(v), (0.25 + c.brake * 5.5) * h + v * v * d.drag * h);
      }
      this.vel.set(Math.sin(yaw) * v, 0, Math.cos(yaw) * v);
      this.pos.addScaledVector(this.vel, h);
      this.pos.y = fl.y;
      if (this._water(fl)) return;
      // rotate & lift off
      if (vf > d.vRotate && pitch > 0.05) {
        this.grounded = false;
        f.copy(Z).applyQuaternion(q);
        this.vel.copy(f).multiplyScalar(vf);
        this.pos.y += 0.05;
      } else if (this.pos.y > fl.y + 0.6) this.grounded = false; // rolled off an edge
      return;
    }

    // ------------------------------------------------------------- flying
    // control rates (body frame): x pitch (+ nose down), y yaw (+ nose left), z roll (+ right wing down)
    const tx = -c.pitch * d.pitchRate * (0.25 + 0.75 * auth);
    const ty = -c.yaw * d.yawRate * (0.3 + 0.7 * auth);
    const tz = c.roll * d.rollRate * (0.2 + 0.8 * auth);
    const k = 1 - Math.exp(-h * 5), kr = 1 - Math.exp(-h * 8);
    this.angVel.x += (tx - this.angVel.x) * k;
    this.angVel.y += (ty - this.angVel.y) * k;
    this.angVel.z += (tz - this.angVel.z) * kr;
    const w = this.angVel.length();
    if (w > 1e-6) { _q.setFromAxisAngle(_v.copy(this.angVel).divideScalar(w), w * h); q.multiply(_q); }
    // weathervane: the tail pulls the nose onto the flight path (strongly sideways, weakly in pitch unless stalled)
    if (V > 3) {
      _qi.copy(q).invert();
      const vb = _v.copy(this.vel).divideScalar(V).applyQuaternion(_qi);
      const beta = Math.atan2(vb.x, Math.max(0.05, vb.z));
      const alpha = Math.atan2(-vb.y, Math.max(0.05, vb.z));
      const kY = 1.6 + 1.4 * auth, kP = 0.35 * auth + 1.2 * (1 - auth);
      if (vb.z > 0) {
        _q.setFromEuler(_e.set(clamp(alpha * kP * h, -0.05, 0.05), clamp(beta * kY * h, -0.05, 0.05), 0, 'YXZ'));
        q.multiply(_q);
      }
    }
    // stalled / hanging on the prop: gravity swings the nose over toward the ground (hammerhead)
    if (auth < 0.65) {
      const ks = (0.65 - auth) / 0.65;
      const fw = _f.copy(Z).applyQuaternion(q);
      const tdir = _v.set(0, -1, 0);
      if (V > 5 && this.vel.dot(fw) > 0.5 * V) tdir.lerp(_l.copy(this.vel).divideScalar(V), 1 - ks).normalize();
      _w.crossVectors(fw, tdir);
      let sn = _w.length();
      const cs = fw.dot(tdir);
      if (sn < 0.02) { if (cs > 0) sn = 0; else { _w.copy(X).applyQuaternion(q); sn = 1; } }
      if (sn > 0) {
        const ang = Math.atan2(_w.length(), cs);
        _q.setFromAxisAngle(_w.divideScalar(_w.length()), Math.min(ang, ks * 1.9 * h));
        q.premultiply(_q);
      }
    }
    q.normalize();
    f.copy(Z).applyQuaternion(q);
    u.copy(Y).applyQuaternion(q);
    // bank-to-turn: a banked wing swings the heading (and the flight path with it) toward the low wing
    const l = _l.copy(X).applyQuaternion(q);
    const horiz = Math.sqrt(Math.max(0, 1 - f.y * f.y));
    const turn = -G * l.y / Math.max(V, 22) * auth * horiz * 1.15;
    if (Math.abs(turn) > 1e-5) { _q.setFromAxisAngle(Y, turn * h); q.premultiply(_q); this.vel.applyQuaternion(_q); f.applyQuaternion(_q); u.applyQuaternion(_q); }
    // forces: wings cancel the part of gravity across the flight path (hands-off = fly straight in any attitude);
    // the part along the path remains, so climbs cost speed and dives build it. Below stall speed lift fades.
    const liftK = clamp(vf / d.vStall, 0, 1);
    _a.copy(f).multiplyScalar(d.thrust * this.spool);
    _a.y -= G;
    const lift = G * liftK * liftK;
    _a.x -= f.x * f.y * lift; _a.y += (1 - f.y * f.y) * lift; _a.z -= f.z * f.y * lift;
    _a.addScaledVector(this.vel, -V * d.drag - (1 - auth) * 0.05);
    if (c.brake) _a.addScaledVector(this.vel, -0.25);
    this.vel.addScaledVector(_a, h);
    // air grip: the velocity swings onto the nose, keeping most of its energy (arcade handling)
    const kv = 1 - Math.exp(-h * (2.6 * auth));
    const along = this.vel.dot(f);
    const v0 = this.vel.length();
    _w.copy(this.vel).addScaledVector(f, -along);
    this.vel.addScaledVector(_w, -kv);
    const v1 = this.vel.length();
    if (v1 > 1e-3) this.vel.multiplyScalar((v1 + (v0 - v1) * 0.8) / v1); // never adds energy
    this.pos.addScaledVector(this.vel, h);
    // ------------------------------------------------------------- contact
    if (this.pos.y <= fl.y) {
      if (this._water(fl)) { this.pos.y = fl.y; this.grounded = true; this.vel.multiplyScalar(0.3); return; }
      const l = _l.copy(X).applyQuaternion(q);
      const pitch = Math.asin(clamp(f.y, -1, 1)), roll = Math.atan2(l.y, u.y);
      const sink = -this.vel.y;
      this.pos.y = fl.y;
      if (!this.gearDown || this.gearK < 0.6) {
        // belly landing
        this.crash(Math.max(sink * 2, V * 0.35));
        if (this.exploded) return;
      } else if (sink > 7.5 || Math.abs(roll) > 0.5 || pitch < -0.2) {
        this.crash(Math.max(sink, V * 0.5));
        if (this.exploded) return;
      } else if (sink > 3.5) this.damage((sink - 3.5) * 40);
      this.grounded = true;
      this.angVel.set(0, 0, 0);
      this.vel.y = 0;
      const yaw = Math.atan2(f.x, f.z);
      q.setFromEuler(_e.set(-clamp(pitch, 0, 0.22), yaw, 0, 'YXZ'));
      this.game.effects?.tireSmoke?.(this.localPoint(this.hx * 0.7, 0.2, -1), 1);
      this.game.audio?.playAt('bodyhit', this.cg(), 0.4);
      return;
    }
    // nose / wingtips / tail touching terrain in flight
    const L = d.L, W = d.W;
    for (const [x, y, z] of [[0, this.cgY, L * 0.48], [W * 0.48, this.cgY, -L * 0.1], [-W * 0.48, this.cgY, -L * 0.1], [0, this.cgY + 0.5, -L * 0.48]]) {
      const p = this.localPoint(x, y, z, _w);
      if (p.y < this.game.map.groundHeight(p.x, p.z) - 0.1) { this.crash(Math.max(V * 0.8, 23), p.clone()); return; }
    }
  }

  _updateVisual(dt) {
    if (!this.quat) return;
    this._placeGroup();
    this._commonVisual(dt);
    const m = this.model;
    // propellers
    this.propAngle += this.spool * dt * 55;
    for (const p of m.props) {
      p.obj.rotation.z = this.propAngle * p.rate;
      p.blade.visible = this.spool < 0.55 || this.exploded;
      p.disc.visible = this.spool > 0.25 && !this.exploded;
    }
    // afterburners
    if (m.flames) {
      const ab = this.exploded ? 0 : clamp((this.spool - 0.55) / 0.45, 0, 1);
      for (let i = 0; i < m.flames.length; i++) {
        const fm = m.flames[i];
        fm.visible = ab > 0.02;
        const flick = 0.85 + Math.random() * 0.3;
        fm.scale.set(0.8 + 0.2 * ab, 0.8 + 0.2 * ab, (0.15 + ab * (i % 2 ? 0.9 : 1)) * flick);
      }
    }
    // gear retracts up into the fuselage
    if (m.gear) { m.gear.visible = this.gearK > 0.25; m.gear.position.y = (1 - this.gearK) * 0.9; }
  }
}

// ====================================================================================== helicopters
export class Heli extends AirVehicle {
  setup(opts) {
    super.setup(opts);
    this.tiltP = 0; this.tiltR = 0; this.yawRate = 0;
    this.rotorAngle = Math.random() * 6; this.tailAngle = 0;
    this.gunYaw = 0; this.gunPitch = 0;
    this.dustT = 0;
  }
  get forwardSpeed() { return Math.hypot(this.vel.x, this.vel.z); }

  playerControl(input, dt) {
    const c = this.ctl, gp = input.gp;
    c.coll = clamp((input.key('Space') ? 1 : 0) - (input.key('ShiftLeft') || input.key('ShiftRight') || input.key('KeyC') ? 1 : 0) + gp.rt - gp.lt, -1, 1);
    c.pitch = clamp((input.key('KeyW') || input.key('ArrowUp') ? 1 : 0) - (input.key('KeyS') || input.key('ArrowDown') ? 1 : 0) - (gp.ly || 0), -1, 1);
    c.yaw = clamp((input.key('KeyD') || input.key('ArrowRight') ? 1 : 0) - (input.key('KeyA') || input.key('ArrowLeft') ? 1 : 0) + (gp.lx || 0), -1, 1);
    c.roll = clamp((input.key('KeyE') ? 1 : 0) - (input.key('KeyQ') ? 1 : 0), -1, 1);
    this.horn = false;
    if (this.armed && !this.exploded && this.health > 0 && this.spool > 0.6) {
      this.aimT = (this.aimT || 0) - dt;
      if (this.aimT <= 0 || input.mouse.left || input.mouse.rightPressed) { this.aimT = 0.12; this.aimAt = aimPoint(this.game, 700, this.driver); }
      if (input.mouse.left && this.gunT <= 0) this.fireMinigun();
      if (input.mouse.rightPressed && this.missileT <= 0) this.fireRocket();
    }
  }

  fireMinigun() {
    this.gunT = 0.06;
    const mz = this.model.muzzles[0];
    const muzzle = this.localPoint(mz.x, mz.y, mz.z);
    const dir = (this.aimAt ? this.aimAt.clone().sub(muzzle) : _f.copy(Z).applyQuaternion(this.quat).clone()).normalize();
    this.game.combat.vehicleGun(this.driver, muzzle, dir, MINIGUN);
    if (this.driver?.isPlayer) this.game.rig.addShake(0.05);
  }

  fireRocket() {
    this.missileT = 0.3;
    const pods = this.model.pods;
    const p = pods[(this.side = (this.side + 1) % pods.length)];
    const pos = this.localPoint(p.x, p.y, p.z);
    const dir = (this.aimAt ? this.aimAt.clone().sub(pos) : _f.copy(Z).applyQuaternion(this.quat).clone()).normalize();
    this.game.combat.fireProjectile(this.driver, 'rocket', pos, dir, { speed: 95, inherit: this.vel, radius: 8.5, damage: 240, life: 5 });
  }

  update(dt) {
    if (this.removed || this._asleep(dt)) return;
    this.gunT -= dt; this.missileT -= dt;
    const c = this.ctl;
    const ctrl = !!this.driver && this.health > 0 && !this.exploded;
    if (!ctrl) { c.pitch = c.roll = c.yaw = 0; c.coll = this.grounded ? 0 : -0.3; }
    if (this.health <= 0 && !this.exploded) {
      this.onFire = true;
      this.burnT += dt;
      if (this.burnT > (this.grounded ? 4 : 12)) this.explode();
    }
    const steps = Math.max(1, Math.ceil(dt * 90));
    const h = dt / steps;
    for (let i = 0; i < steps && !this.removed; i++) {
      if (this.exploded || this.sunk) this._wreckStep(h);
      else this._fly(h);
    }
    this.airborne = !this.grounded;
    if (!this.exploded && !this.sunk) this._heliStatics();
    // rotor downwash kicks up dust / spray
    this.dustT -= dt;
    if (this.spool > 0.6 && !this.exploded && this.dustT <= 0) {
      const alt = this.pos.y - this._floor().y;
      if (alt < 16) {
        this.dustT = 0.05;
        const a = Math.random() * Math.PI * 2, r = 3 + Math.random() * 6;
        const x = this.pos.x + Math.cos(a) * r, z = this.pos.z + Math.sin(a) * r;
        const wet = this.game.map.waterDepth(x, z) > 1;
        const y = wet ? WATER_Y : this.game.map.groundHeight(x, z);
        if (wet) this.game.effects?.splash?.(new THREE.Vector3(x, y, z), 0.5);
        else this.game.effects?.dust?.(new THREE.Vector3(x, y + 0.2, z), 1.4);
      }
    }
    this._updateVisual(dt);
  }

  _fly(h) {
    const d = this.def, c = this.ctl;
    const alive = this.health > 0;
    const want = alive && this.driver ? 1 : 0;
    this.spool += (want - this.spool) * h * (want ? 0.4 : this.grounded ? 0.12 : 0.35);
    const maxT = 0.42;
    // attitude: tilt the rotor disc; hands off = the auto-hover gently brakes the drift
    const sy = Math.sin(this.yaw), cy = Math.cos(this.yaw);
    const vFwd = this.vel.x * sy + this.vel.z * cy, vLeft = this.vel.x * cy - this.vel.z * sy;
    let tP = c.pitch ? c.pitch * maxT : clamp(-vFwd * 0.03, -0.2, 0.2);
    let tR = c.roll ? c.roll * maxT * 0.8 : clamp(vLeft * 0.04, -0.2, 0.2);
    if (this.grounded) { tP = 0; tR = 0; }
    if (!alive) { tP = 0.15; tR = 0.2; }
    this.tiltP = damp(this.tiltP, tP, 2.4, h);
    this.tiltR = damp(this.tiltR, tR, 2.4, h);
    let yr = this.grounded ? 0 : -c.yaw * 1.45;
    if (!alive) yr = 4.2 * (1 - this.spool * 0.5);
    this.yawRate = damp(this.yawRate, yr, 3, h);
    this.yaw = wrapAngle(this.yaw + this.yawRate * h);
    this.quat.setFromEuler(_e.set(this.tiltP, this.yaw, this.tiltR, 'YXZ'));
    const u = _u.copy(Y).applyQuaternion(this.quat);
    const lift = this.spool * (G + c.coll * 11) / Math.max(0.6, u.y);
    _a.copy(u).multiplyScalar(lift);
    _a.y -= G;
    const kd = G * Math.tan(maxT) / d.vMax;
    _a.x -= this.vel.x * kd; _a.z -= this.vel.z * kd;
    _a.y -= this.vel.y * (1.1 * this.spool + 0.05);
    this.vel.addScaledVector(_a, h);
    const fl = this._floor();
    // ground-effect flare: holding descend near the ground settles you gently onto it
    const hAbove = this.pos.y - fl.y;
    if (alive && this.spool > 0.8 && hAbove < 12) this.vel.y = Math.max(this.vel.y, -(1.6 + hAbove * 0.45));
    this.pos.addScaledVector(this.vel, h);
    if (this.pos.y <= fl.y) {
      if (this._water(fl)) { this.pos.y = fl.y; this.grounded = true; return; }
      const sink = -this.vel.y;
      const tilt = Math.max(Math.abs(this.tiltP), Math.abs(this.tiltR));
      if (!this.grounded) {
        if (!alive || sink > 8 || tilt > 0.6) { this.crash(Math.max(sink, 23 * (alive ? 0 : 1)) + Math.hypot(this.vel.x, this.vel.z) * 0.4); if (this.exploded) return; }
        else if (sink > 4) this.damage((sink - 4) * 45);
      }
      this.pos.y = fl.y;
      this.vel.y = Math.max(0, this.vel.y);
      this.vel.x *= Math.exp(-5 * h); this.vel.z *= Math.exp(-5 * h);
      this.grounded = true;
    } else if (this.pos.y > fl.y + 0.08) this.grounded = false;
  }

  _heliStatics() {
    const list = this.game.collision.obbContacts(this.pos.x, this.pos.z, this.yaw, Math.max(this.hx, this.def.rotorR * 0.6), this.hz * 0.85, this.pos.y + 0.4, this.contacts);
    for (const ct of list) {
      if (ct.obj.kind === 'circle' && ct.obj.breakable && !ct.obj.broken) { this.game.city?.breakProp(ct.obj); continue; }
      const vn = this.vel.x * ct.nx + this.vel.z * ct.nz;
      this.pos.x += ct.nx * ct.depth; this.pos.z += ct.nz * ct.depth;
      if (vn < 0) {
        this.vel.x -= ct.nx * vn * 1.4; this.vel.z -= ct.nz * vn * 1.4;
        if (-vn > 14) this.crash(-vn, new THREE.Vector3(ct.px, this.pos.y + 2, ct.pz));
        else if (-vn > 4) { this.damage((-vn - 4) * 30); this.game.effects?.sparks?.(new THREE.Vector3(ct.px, this.pos.y + 3, ct.pz), 12); this.game.audio?.playAt('metalhit', this.pos, 0.8); }
        if (this.exploded) return;
      }
    }
  }

  _updateVisual(dt) {
    if (!this.quat) return;
    this._placeGroup();
    this._commonVisual(dt);
    const m = this.model;
    this.rotorAngle += this.spool * dt * 26;
    this.tailAngle += this.spool * dt * 90;
    if (m.rotor) {
      m.rotor.obj.rotation.y = this.rotorAngle;
      m.rotor.blade.visible = this.spool < 0.6 || this.exploded;
      m.rotor.disc.visible = this.spool > 0.3 && !this.exploded;
      // blades droop when stopped
      m.rotor.blade.rotation.x = 0;
    }
    if (m.tailRotor) {
      m.tailRotor.obj.rotation.x = this.tailAngle;
      m.tailRotor.blade.visible = this.spool < 0.5 || this.exploded;
      m.tailRotor.disc.visible = this.spool > 0.3 && !this.exploded;
    }
    // chin gun follows the aim point
    if (m.gun && this.aimAt && this.driver?.isPlayer) {
      const g = m.gun;
      const lp = _v.copy(this.aimAt).sub(this.cg(_w)).applyQuaternion(_qi.copy(this.quat).invert());
      const gy = lp.y + this.cgY - g.position.y;
      const ty = Math.atan2(lp.x - g.position.x, lp.z - g.position.z), tp = Math.atan2(gy, Math.hypot(lp.x - g.position.x, lp.z - g.position.z));
      this.gunYaw = damp(this.gunYaw, clamp(ty, -1.6, 1.6), 8, dt);
      this.gunPitch = damp(this.gunPitch, clamp(tp, -1.0, 0.25), 8, dt);
      g.rotation.set(-this.gunPitch, this.gunYaw, 0, 'YXZ');
    }
  }
}

// ====================================================================================== tank
export class Tank extends Vehicle {
  buildModel(d, color) { return buildAircraftModel(d, color, this.type); }
  setup() {
    this.turretYaw = 0; this.gunPitch = 0;
    this.reload = 0; this.recoil = 0;
    this.wheelL = 0; this.wheelR = 0;
    this.aimAt = null; this.aimT = 0;
  }
  get armed() { return true; }
  get showCrosshair() { return true; }

  putIn(char, seat = 0) { super.putIn(char, seat); char.hiddenInVehicle = true; char.root.visible = false; }
  takeOut(char, pos) { super.takeOut(char, pos); char.hiddenInVehicle = false; char.root.visible = true; }
  dent() {}

  playerControl(input, dt) {
    super.playerControl(input, dt);
    this.aimT -= dt;
    if (this.aimT <= 0) { this.aimT = 0.1; this.aimAt = aimPoint(this.game, 900, this.driver); }
    if (input.mouse.left && this.reload <= 0 && !this.exploded) this.fireCannon();
  }

  muzzleWorld(out = new THREE.Vector3()) {
    this.group.updateMatrixWorld(true);
    return out.set(0, 0, 5.75).applyMatrix4(this.model.gun.matrixWorld);
  }

  fireCannon() {
    const g = this.game;
    this.reload = 1.8;
    this.recoil = 1;
    const muzzle = this.muzzleWorld();
    const dir = new THREE.Vector3(0, 0, 1).transformDirection(this.model.gun.matrixWorld);
    g.combat.fireProjectile(this.driver, 'shell', muzzle, dir, { speed: 190, gravity: 2.5, radius: 10, damage: 320, life: 6 });
    g.effects.muzzleFlash(muzzle, dir, true);
    for (let i = 0; i < 4; i++) g.effects.dust?.(muzzle.clone().addScaledVector(dir, 1.5 + i).setY(muzzle.y - 0.5), 1.5);
    g.audio?.playAt('explosion', muzzle, 0.55);
    g.audio?.playAt('rpg', muzzle, 1);
    if (this.driver?.isPlayer) g.rig.addShake(0.7);
    this.bodyPitchV -= 1.2 * Math.cos(this.turretYaw);
    this.bodyRollV += 0.8 * Math.sin(this.turretYaw);
    g.events.emit('gunshot', this.driver, muzzle, { id: 'cannon' });
  }

  update(dt) {
    super.update(dt);
    this.reload -= dt;
    this.recoil = Math.max(0, this.recoil - dt * 1.6);
  }

  // tracks: no sliding sideways, pivot steering, heavy acceleration
  _step(h) {
    const d = this.def, inp = this.input;
    if (this.airborne) {
      this.pos.x += this.vel.x * h; this.pos.z += this.vel.z * h;
      this.vy -= G * h; this.pos.y += this.vy * h;
      return;
    }
    const s = Math.sin(this.yaw), c = Math.cos(this.yaw);
    const fx = s, fz = c, rx = -c, rz = s;
    let vLong = this.vel.x * fx + this.vel.z * fz, vLat = this.vel.x * rx + this.vel.z * rz;
    const v0 = vLong;
    const thr = this.isWrecked ? 0 : inp.throttle - inp.brake;
    const target = thr >= 0 ? thr * d.top : thr * d.top * 0.5;
    const dv = target - vLong;
    const acc = Math.abs(vLong) > 0.3 && sign(dv) !== sign(vLong) ? 7 : d.accel;
    vLong += clamp(dv, -acc * h, acc * h);
    if (inp.handbrake) vLong *= Math.exp(-4 * h);
    vLat *= Math.exp(-9 * h);
    const dir = vLong < -0.5 ? -1 : 1;
    const rT = this.isWrecked ? 0 : inp.steer * d.turn * dir * (1 - 0.35 * Math.min(1, Math.abs(vLong) / d.top));
    this.r += (rT - this.r) * Math.min(1, h * 6);
    this.vel.x = fx * vLong + rx * vLat; this.vel.z = fz * vLong + rz * vLat;
    this.axLong = damp(this.axLong, (vLong - v0) / h, 8, h);
    this.ayLat = damp(this.ayLat, this.r * vLong, 8, h);
    this.yaw += this.r * h;
    this.pos.x += this.vel.x * h; this.pos.z += this.vel.z * h;
    this.slipRear = 0; this.slipFront = 0; this.wheelspin = 0;
    // track speeds for the road wheels
    this.trackL = vLong + this.r * 1.5; this.trackR = vLong - this.r * 1.5;
  }

  explode() {
    if (this.exploded) return;
    this.exploded = true;
    this.onFire = false;
    this.health = 0;
    const M = vehicleMaterials();
    for (const m of this.model.meshes) m.material = M.burnt;
    this.game.combat?.explosion(this.pos.clone().add(new THREE.Vector3(0, 1.6, 0)), 12, 250, this.lastDamager, this);
    for (const o of this.occupants) if (o) o.takeDamage(1000, { type: 'explosion', source: this.lastDamager });
    // the turret gets blown off its ring
    this.turretVy = 9; this.turretSpin = (Math.random() - 0.5) * 4;
    this.game.events?.emit('vehicleExploded', this);
    this.wreckTime = 0;
  }

  _updateVisual(dt) {
    const g = this.group, m = this.model;
    g.rotation.y = this.yaw;
    g.rotation.x = -this.groundPitch;
    g.rotation.z = this.groundRoll;
    // hull suspension rock
    const tp = clamp(this.axLong * 0.01, -0.05, 0.05), tr = clamp(-this.ayLat * 0.006, -0.04, 0.04);
    this.bodyPitchV += ((tp - this.bodyPitch) * 70 - this.bodyPitchV * 9) * dt;
    this.bodyRollV += ((tr - this.bodyRoll) * 70 - this.bodyRollV * 9) * dt;
    this.bodyPitch += this.bodyPitchV * dt; this.bodyRoll += this.bodyRollV * dt;
    m.bodyGroup.rotation.x = this.bodyPitch;
    m.bodyGroup.rotation.z = this.bodyRoll;
    // road wheels
    this.wheelL += (this.trackL ?? 0) * dt / 0.36; this.wheelR += (this.trackR ?? 0) * dt / 0.36;
    if (m.wheelSets) { for (const w of m.wheelSets[0]) w.rotation.x = this.wheelL; for (const w of m.wheelSets[1]) w.rotation.x = this.wheelR; }
    // turret + gun toward the aim point (world-stable while the hull turns)
    if (this.turretYaw === undefined) return;
    if (this.exploded) {
      if (this.turretVy !== undefined) {
        this.turretVy -= G * dt;
        m.turret.position.y = Math.max(1.78, m.turret.position.y + this.turretVy * dt);
        if (m.turret.position.y <= 1.78 && this.turretVy < 0) { this.turretVy = undefined; m.turret.rotation.z = 0.25; }
        m.turret.rotation.y += this.turretSpin * dt;
      }
      return;
    }
    if (this.aimAt && this.driver) {
      const tx = this.pos.x, tz = this.pos.z;
      const want = wrapAngle(Math.atan2(this.aimAt.x - tx, this.aimAt.z - tz) - this.yaw);
      const dyaw = wrapAngle(want - this.turretYaw);
      this.turretYaw = wrapAngle(this.turretYaw + clamp(dyaw, -1.25 * dt, 1.25 * dt));
      const hd = Math.hypot(this.aimAt.x - tx, this.aimAt.z - tz);
      const wantP = clamp(Math.atan2(this.aimAt.y - (this.pos.y + 2.25), hd - 2.5) - this.groundPitch * Math.cos(this.turretYaw), -0.14, 0.36);
      this.gunPitch += clamp(wantP - this.gunPitch, -0.7 * dt, 0.7 * dt);
    }
    m.turret.rotation.y = this.turretYaw;
    m.gunPivot.rotation.x = -this.gunPitch;
    m.gun.position.z = -this.recoil * 0.55;
    const door = m.door;
    if (door.axis) door.pivot.rotation[door.axis] = door.open * door.max;
  }
}

export function vehicleClass(def) {
  if (def.kind === 'plane' || def.kind === 'jet') return Plane;
  if (def.kind === 'heli') return Heli;
  if (def.kind === 'tank') return Tank;
  return null;
}
