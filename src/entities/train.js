// The Sol Line train: a diesel locomotive and three passenger carriages running on the railway polyline.
// It runs station to station on its own (accelerate, brake to the platform, dwell, reverse at the
// termini); climb into the cab to drive it yourself (W / S), or board a carriage at a platform and ride.
// The locomotive is a Vehicle (enter / exit / HUD / camera); the carriages are followers whose
// collisions with cars and people are handled here.
import * as THREE from 'three';
import { Vehicle } from './vehicle.js';
import { vehicleMaterials, bodyMaterial } from './vehiclemodels.js';
import { GeoBuilder, mat4 } from '../world/geom.js';
import { railAt } from '../world/railway.js';
import { clamp, damp } from '../core/utils.js';

const LOCO_L = 17, CAR_L = 20.5, GAP = 1.2, NCARS = 3;
const _t = [0, 0, 0, 0, 0, 0];
const _v = new THREE.Vector3();

// ------------------------------------------------------------------ models (y = 0 at the rail head, +z forward)
function bogie(gb, z, color = [0.12, 0.12, 0.13]) {
  gb.set('color', ...color);
  gb.box(-1.25, 0.35, z - 1.5, 1.25, 0.95, z + 1.5);
  gb.set('color', 0.08, 0.08, 0.08);
  for (const dz of [-1.05, 1.05]) for (const x of [-0.72, 0.72]) gb.addGeometry(new THREE.CylinderGeometry(0.46, 0.46, 0.14, 16), mat4(x, 0.46, z + dz, 0, 0, Math.PI / 2));
  gb.set('color', 0.5, 0.5, 0.52);
  for (const dz of [-1.05, 1.05]) gb.addGeometry(new THREE.CylinderGeometry(0.07, 0.07, 1.7, 8), mat4(0, 0.46, z + dz, 0, 0, Math.PI / 2));
}

function locoModel(color) {
  const M = vehicleMaterials();
  const group = new THREE.Group();
  const bodyGroup = new THREE.Group();
  group.add(bodyGroup);
  const paint = new THREE.Color(color);
  const body = new GeoBuilder(), trim = new GeoBuilder();
  const W = 1.5, zf = LOCO_L / 2, zr = -LOCO_L / 2;
  // frame & fuel tank
  trim.set('color', 0.14, 0.14, 0.15);
  trim.box(-1.45, 0.95, zr, 1.45, 1.35, zf);
  trim.box(-1.1, 0.55, -3.5, 1.1, 1.0, 3.5);
  bogie(trim, zr + 3.6); bogie(trim, zf - 3.6);
  // long hood (narrow) + cab near the front + short nose
  body.set('color', paint.r, paint.g, paint.b);
  body.box(-1.05, 1.35, zr + 0.3, 1.05, 3.9, zf - 5.2, { top: true });
  body.box(-W, 1.35, zf - 5.2, W, 4.35, zf - 1.9, { top: true });
  body.box(-1.2, 1.35, zf - 1.9, 1.2, 2.9, zf - 0.2, { top: true });
  // yellow stripe & grey roof on the cab
  body.set('color', 0.95, 0.75, 0.12);
  body.box(-1.07, 1.9, zr + 0.3, 1.07, 2.15, zf - 5.2);
  body.box(-W - 0.01, 1.9, zf - 5.2, W + 0.01, 2.15, zf - 1.9);
  body.box(-1.21, 1.9, zf - 1.9, 1.21, 2.15, zf - 0.2);
  body.set('color', 0.35, 0.36, 0.37);
  body.box(-W - 0.02, 4.35, zf - 5.3, W + 0.02, 4.5, zf - 1.8, { top: true });
  // radiator fans & horn on the hood roof
  trim.set('color', 0.2, 0.2, 0.2);
  for (const z of [zr + 2, zr + 4.2]) trim.addGeometry(new THREE.CylinderGeometry(0.6, 0.6, 0.12, 16), mat4(0, 3.96, z));
  trim.addGeometry(new THREE.CylinderGeometry(0.08, 0.12, 0.5, 8), mat4(0.4, 4.7, zf - 3, Math.PI / 2, 0, 0));
  // handrails along the walkways
  trim.set('color', 0.85, 0.8, 0.2);
  for (const x of [-1.38, 1.38]) trim.box(x - 0.03, 2.3, zr + 0.4, x + 0.03, 2.36, zf - 5.4);
  // cab windows
  const glass = new GeoBuilder({ position: 3, normal: 3, uv: 2 });
  glass.box(-1.2, 3.1, zf - 1.88, 1.2, 4.05, zf - 1.86);
  glass.box(-W - 0.02, 3.1, zf - 4.8, -W, 4.05, zf - 2.4);
  glass.box(W, 3.1, zf - 4.8, W + 0.02, 4.05, zf - 2.4);
  // headlights + ditch lights
  const lights = new GeoBuilder({ position: 3, normal: 3, uv: 2 });
  lights.box(-0.25, 2.55, zf - 0.21, 0.25, 2.8, zf - 0.17);
  for (const x of [-0.8, 0.8]) lights.box(x - 0.14, 1.5, zf - 0.21, x + 0.14, 1.72, zf - 0.17);
  const tail = new GeoBuilder({ position: 3, normal: 3, uv: 2 });
  for (const x of [-0.8, 0.8]) tail.box(x - 0.14, 3.3, zr + 0.26, x + 0.14, 3.5, zr + 0.3);
  // plough
  trim.set('color', 0.95, 0.75, 0.12);
  trim.box(-1.4, 0.3, zf - 0.2, 1.4, 0.9, zf + 0.25);
  const bodyMesh = new THREE.Mesh(body.build(), bodyMaterial(0xffffff));
  const trimMesh = new THREE.Mesh(trim.build(), M.trim);
  const glassMesh = new THREE.Mesh(glass.build(), M.glass);
  const head = new THREE.Mesh(lights.build(), M.headOff);
  const tailMesh = new THREE.Mesh(tail.build(), M.tailOff);
  for (const m of [bodyMesh, trimMesh]) { m.castShadow = true; m.receiveShadow = true; }
  bodyGroup.add(bodyMesh, trimMesh, glassMesh, head, tailMesh);
  const seat = new THREE.Vector3(0.6, 2.6, zf - 3.4);
  return {
    group, bodyGroup, body: bodyMesh, glass: glassMesh, head, tail: tailMesh, lightbar: null, beam: null, wheels: [],
    door: { pivot: new THREE.Group(), mesh: null, open: 0 },
    seats: [seat, seat.clone(), seat.clone(), seat.clone()],
    doorPos: new THREE.Vector3(W + 0.6, 0, zf - 3.4),
    meshes: [bodyMesh, trimMesh, glassMesh],
  };
}

function carriageModel(color) {
  const M = vehicleMaterials();
  const g = new THREE.Group();
  const paint = new THREE.Color(color);
  const body = new GeoBuilder(), trim = new GeoBuilder(), glass = new GeoBuilder({ position: 3, normal: 3, uv: 2 });
  const W = 1.48, z0 = -CAR_L / 2, z1 = CAR_L / 2;
  trim.set('color', 0.14, 0.14, 0.15);
  trim.box(-1.4, 0.95, z0 + 0.4, 1.4, 1.3, z1 - 0.4);
  bogie(trim, z0 + 3.2); bogie(trim, z1 - 3.2);
  // body shell: lower (paint), window band (light), roof (grey, rounded by a tapered box)
  body.set('color', paint.r, paint.g, paint.b);
  body.box(-W, 1.3, z0, W, 2.55, z1, { top: false });
  body.set('color', 0.9, 0.9, 0.88);
  body.box(-W, 2.55, z0, W, 3.55, z1, { top: false });
  body.set('color', paint.r, paint.g, paint.b);
  body.box(-W, 3.55, z0, W, 3.8, z1, { top: false });
  body.set('color', 0.42, 0.43, 0.45);
  body.addGeometry(new THREE.CylinderGeometry(W, W, CAR_L, 20, 1, false, -Math.PI / 2, Math.PI), new THREE.Matrix4().compose(new THREE.Vector3(0, 3.8, 0), new THREE.Quaternion().setFromEuler(new THREE.Euler(Math.PI / 2, 0, 0)), new THREE.Vector3(1, 1, 0.28)));
  // doors (two per side) in a darker paint with windows
  body.set('color', paint.r * 0.7, paint.g * 0.7, paint.b * 0.7);
  for (const z of [z0 + 2.6, z1 - 2.6]) for (const x of [-W - 0.01, W + 0.01]) body.box(x - 0.005, 1.35, z - 0.65, x + 0.005, 3.45, z + 0.65);
  // windows
  for (let k = 0; k < 6; k++) {
    const z = z0 + 5 + k * 2.1;
    for (const x of [-W - 0.012, W + 0.012]) glass.box(x - 0.004, 2.7, z - 0.8, x + 0.004, 3.4, z + 0.8);
  }
  for (const z of [z0 + 2.6, z1 - 2.6]) for (const x of [-W - 0.016, W + 0.016]) glass.box(x - 0.004, 2.75, z - 0.35, x + 0.004, 3.3, z + 0.35);
  // gangway bellows
  trim.set('color', 0.08, 0.08, 0.08);
  trim.box(-0.8, 1.4, z1 - 0.05, 0.8, 3.5, z1 + 0.55);
  const b = new THREE.Mesh(body.build(), bodyMaterial(0xffffff));
  const t = new THREE.Mesh(trim.build(), M.trim);
  const gl = new THREE.Mesh(glass.build(), M.glass);
  b.castShadow = true; b.receiveShadow = true; t.castShadow = true;
  g.add(b, t, gl);
  g.rotation.order = 'YXZ';
  return { group: g, meshes: [b, t, gl] };
}

// ------------------------------------------------------------------ the train
export class Train extends Vehicle {
  buildModel(d, color) { return locoModel(color); }

  setup(opts) {
    this.rail = this.game.map.roadInfo.rail;
    const L = this.rail.length;
    this.len = LOCO_L + NCARS * (CAR_L + GAP) + GAP;
    this.s = clamp(opts.s ?? L * 0.5, this.len, L - 1);   // arc length of the locomotive's nose
    this.v = 0;                  // speed along +s
    this.dirS = opts.dirS ?? -1; // which way the autopilot is heading
    this.dwell = opts.dwell ?? 12;
    this.hx = 1.5; this.hz = LOCO_L / 2;
    this.I = this.mass * (LOCO_L * LOCO_L) / 12;
    this.maxHealth = this.health = 1e9;
    this.cars = [];
    for (let k = 0; k < NCARS; k++) {
      const c = carriageModel(this.color);
      this.game.scene.add(c.group);
      this.cars.push({ ...c, pos: new THREE.Vector3(), yaw: 0 });
    }
    // lightweight stand-ins so the vehicle manager's contact code can treat each carriage like a car
    this.proxies = this.cars.map((c) => ({
      pos: c.pos, vel: new THREE.Vector3(), r: 0, yaw: 0, hx: 1.5, hz: CAR_L / 2, mass: 2e6, I: 1e9, driver: null, def: { name: 'Carriage', police: false },
      lastHit: 0, bodyYV: 0, damage() {}, dent() {},
      worldToLocal(x, z) { const s = Math.sin(this.yaw), co = Math.cos(this.yaw), dx = x - this.pos.x, dz = z - this.pos.z; return [dx * co - dz * s, dx * s + dz * co]; },
    }));
    this.hornT = 0;
    this._place();
  }

  damage() {}
  dent() {}
  explode() {}
  get isWrecked() { return false; }
  get armed() { return false; }
  get forwardSpeed() { return Math.abs(this.v); }

  // stations in order along the line
  get stations() { return this.game.map.landmarks.stations || []; }
  centreS() { return this.s - this.len / 2; }

  playerControl(input, dt) {
    this.input.throttle = input.throttle();
    this.input.brake = input.brake();
    this.horn = input.down('horn');
  }

  // --- doors: the cab (driver) and two doors per side on every carriage (passengers)
  doorList() {
    const out = [];
    const s = Math.sin(this.yaw), c = Math.cos(this.yaw);
    for (const side of [-1, 1]) out.push({ x: this.pos.x + side * 2.2 * c + (LOCO_L / 2 - 3.4) * s, z: this.pos.z - side * 2.2 * s + (LOCO_L / 2 - 3.4) * c, seat: 0 });
    this.cars.forEach((car, k) => {
      const sn = Math.sin(car.yaw), cs = Math.cos(car.yaw);
      for (const side of [-1, 1]) for (const lz of [-CAR_L / 2 + 2.6, CAR_L / 2 - 2.6]) out.push({ x: car.pos.x + side * 2.3 * cs + lz * sn, z: car.pos.z - side * 2.3 * sn + lz * cs, seat: 1 + Math.min(2, k) });
    });
    return out;
  }
  nearestDoor(pos) {
    let best = null, bd = Infinity;
    for (const d of this.doorList()) { const dd = Math.hypot(d.x - pos.x, d.z - pos.z); if (dd < bd) { bd = dd; best = d; } }
    this._door = best;
    return best;
  }
  doorFor(seat, char) {
    const d = char ? this.nearestDoor(char.pos) : this._door;
    // on the way out, prefer the platform side when stopped at a station
    if (!char && this._atStation) {
      const lm = this._atStation;
      let best = null, bd = Infinity;
      for (const q of this.doorList()) if ((q.seat === 0) === (seat === 0)) { const dd = Math.hypot(q.x - lm.x, q.z - lm.z) + (q.seat === seat ? 0 : 30); if (dd < bd) { bd = dd; best = q; } }
      if (best) return new THREE.Vector3(best.x, 0, best.z);
    }
    const q = d || this.doorList()[0];
    return new THREE.Vector3(q.x, 0, q.z);
  }
  putIn(char, seat = 0) { super.putIn(char, seat); char.hiddenInVehicle = true; char.root.visible = false; }
  takeOut(char, pos) {
    const p = pos || this.doorFor(char.seat ?? 1);
    super.takeOut(char, p);
    char.hiddenInVehicle = false; char.root.visible = true;
  }

  update(dt) {
    if (this.removed) return;
    const L = this.rail.length;
    const drv = this.driver;
    let accel = 0;
    if (drv?.isPlayer) {
      const inp = this.input;
      if (inp.throttle > 0) accel = 0.9 * inp.throttle * (this.v < -0.2 ? 2.2 : 1);
      if (inp.brake > 0) accel = this.v > 0.2 ? -2.2 * inp.brake : -0.6 * inp.brake;
      if (!inp.throttle && !inp.brake) accel = -Math.sign(this.v) * Math.min(Math.abs(this.v) / dt, 0.05);
      this._atStation = null;
      this.dwell = 0;
    } else accel = this._autopilot(dt);
    // the cab faces +s: the player can only back up slowly, the autopilot runs push-pull at full speed
    this.v = clamp(this.v + accel * dt, drv?.isPlayer ? -12 : -32, 32);
    // buffer stops at both ends
    const sMax = L - 2, sMin = this.len + 2;
    this.s += this.v * dt;
    if (this.s > sMax) { this.s = sMax; if (this.v > 0) { if (this.v > 6) this.game.rig?.addShake(0.6); this.v = 0; } }
    if (this.s < sMin) { this.s = sMin; if (this.v < 0) { if (this.v < -6) this.game.rig?.addShake(0.6); this.v = 0; } }
    this._place();
    this._contacts(dt);
    this.wheelRot += this.v * dt / 0.46;
    this.hornT -= dt;
    if (this.horn && this.hornT <= 0) { this.hornT = 1.2; this.game.audio?.playAt('trainhorn', this.pos, 1); }
  }

  // Station-to-station timetable: brake to stop with the train centred on the platform, dwell, carry on.
  _autopilot(dt) {
    const sts = this.stations;
    if (!sts.length) return 0;
    const c = this.centreS();
    if (this.dwell > 0) {
      this.dwell -= dt;
      if (this.dwell <= 0) {
        this._lastStation = this._atStation;
        this._atStation = null;
        const ahead = sts.filter((st) => st !== this._lastStation && (st.s - c) * this.dirS > 5);
        if (!ahead.length) this.dirS = -this.dirS;
        this.game.audio?.playAt('trainhorn', this.pos, 0.7);
      }
      return -Math.sign(this.v) * Math.min(Math.abs(this.v) / Math.max(dt, 1e-3), 3);
    }
    // next stop: the nearest station ahead that we didn't just leave (a few metres of overshoot still counts)
    const ahead = sts.filter((st) => st !== this._lastStation && (st.s - c) * this.dirS > -4).sort((a, b) => Math.abs(a.s - c) - Math.abs(b.s - c));
    if (!ahead.length) { this.dirS = -this.dirS; this._lastStation = null; return 0; }
    const target = ahead[0];
    const signed = (target.s - c) * this.dirS; // distance still to go (negative = overshot)
    const vDir = this.v * this.dirS;
    if (Math.abs(signed) < 2 && Math.abs(this.v) < 1.6) {
      this.v = 0; this.dwell = 16; this._atStation = target; this._lastStation = target;
      this.game.events?.emit('trainArrived', this, target);
      return 0;
    }
    // speed along the braking curve (0.8 m/s2), proportional control toward it
    const vTarget = signed > 0 ? Math.min(this.cruise ?? 24, Math.sqrt(2 * 0.8 * Math.max(0, signed - 0.3))) : -Math.min(2, Math.sqrt(-signed));
    const a = clamp((vTarget - vDir) * 1.4, -1.6, 0.7);
    return a * this.dirS;
  }

  _place() {
    const r = this.rail;
    // locomotive: its centre is half a loco length behind the nose (+s is the nose direction)
    railAt(r, this.s - LOCO_L / 2, _t);
    const yF = railAt(r, this.s - 2, [0, 0, 0, 0, 0, 0])[1], yB = railAt(r, this.s - LOCO_L + 2, [0, 0, 0, 0, 0, 0])[1];
    this.pos.set(_t[0], _t[1] + 0.36, _t[2]);
    this.yaw = Math.atan2(_t[3], _t[4]);
    this.pitch = Math.atan2(yF - yB, LOCO_L - 4);
    this.vel.set(_t[3] * this.v, 0, _t[4] * this.v);
    this.r = 0;
    let s = this.s - LOCO_L - GAP;
    this.cars.forEach((car, k) => {
      const sc = s - CAR_L / 2;
      railAt(r, sc, _t);
      const y0 = railAt(r, sc + CAR_L / 2 - 3, [0, 0, 0, 0, 0, 0])[1], y1 = railAt(r, sc - CAR_L / 2 + 3, [0, 0, 0, 0, 0, 0])[1];
      car.pos.set(_t[0], _t[1] + 0.36, _t[2]);
      car.yaw = Math.atan2(_t[3], _t[4]);
      car.group.position.copy(car.pos);
      car.group.rotation.set(-Math.atan2(y0 - y1, CAR_L - 6), car.yaw, 0);
      const px = this.proxies[k];
      px.yaw = car.yaw; px.vel.set(_t[3] * this.v, 0, _t[4] * this.v);
      s -= CAR_L + GAP;
    });
  }

  // carriages vs cars and people (the locomotive itself goes through the vehicle manager)
  _contacts(dt) {
    const vm = this.game.vehicles;
    if (!vm) return;
    for (const px of this.proxies) {
      for (const v of vm.list) {
        if (v === this || v.removed) continue;
        const dx = v.pos.x - px.pos.x, dz = v.pos.z - px.pos.z;
        if (dx * dx + dz * dz > 30 * 30 || Math.abs(v.pos.y - px.pos.y) > 3) continue;
        vm._carCar(px, v);
      }
      for (const c of this.game.allCharacters()) {
        if (c.vehicle || c.removed || c.ragdolling) continue;
        const dx = c.pos.x - px.pos.x, dz = c.pos.z - px.pos.z;
        if (dx * dx + dz * dz > 13 * 13 || Math.abs(c.pos.y - px.pos.y) > 2.5) continue;
        vm._carPed(px, c);
      }
    }
  }

  _updateVisual(dt) {
    if (!this.cars) return;
    const g = this.group;
    g.rotation.set(-(this.pitch || 0), this.yaw, 0);
    const M = vehicleMaterials();
    const night = (this.game.env?.night ?? 0) > 0.3;
    const on = night || Math.abs(this.v) > 0.5;
    const hm = on ? M.headOn : M.headOff;
    if (this.model.head.material !== hm) this.model.head.material = hm;
    const tm = night ? M.tailOn : M.tailOff;
    if (this.model.tail.material !== tm) this.model.tail.material = tm;
    this.lightsOn = on && !!this.driver?.isPlayer;
  }

  remove() {
    for (const c of this.cars || []) c.group.parent?.remove(c.group);
    super.remove();
  }
}
