// Sol Line trains: a diesel locomotive and three passenger carriages (or a string of freight wagons)
// running on the railway polyline, on the main line or the passing loop at Fern Creek.
// It runs station to station on its own (accelerate, brake to the platform, dwell, reverse at the
// termini); climb into the cab to drive it yourself (W / S), or board a carriage at a platform and ride.
// The locomotive is a Vehicle (enter / exit / HUD / camera); the carriages are followers whose
// collisions with cars and people are handled here.
import * as THREE from 'three';
import { Vehicle } from './vehicle.js';
import { vehicleMaterials, bodyMaterial } from './vehiclemodels.js';
import { GeoBuilder, mat4 } from '../world/geom.js';
import { railAtTrack } from '../world/railway.js';
import { clamp, damp } from '../core/utils.js';

const LOCO_L = 17, CAR_L = 20.5, WAG_L = 14.5, GAP = 1.2, NCARS = 3;
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

// freight wagons: box car, tank car, hopper, container flat
const CONTAINER = [[0.72, 0.16, 0.12], [0.12, 0.3, 0.55], [0.15, 0.45, 0.22], [0.85, 0.5, 0.1], [0.55, 0.56, 0.58], [0.6, 0.1, 0.35]];
function wagonModel(type, seed) {
  const M = vehicleMaterials();
  const g = new THREE.Group();
  const body = new GeoBuilder(), trim = new GeoBuilder();
  const z0 = -WAG_L / 2, z1 = WAG_L / 2, W = 1.45;
  trim.set('color', 0.13, 0.13, 0.14);
  trim.box(-1.35, 0.95, z0 + 0.3, 1.35, 1.25, z1 - 0.3);
  bogie(trim, z0 + 2.4); bogie(trim, z1 - 2.4);
  // couplers
  trim.box(-0.15, 0.95, z0 - 0.6, 0.15, 1.15, z0 + 0.3); trim.box(-0.15, 0.95, z1 - 0.3, 0.15, 1.15, z1 + 0.6);
  const rnd = (k) => { const x = Math.sin(seed * 12.9898 + k * 78.233) * 43758.5453; return x - Math.floor(x); };
  if (type === 'box') {
    const c = [[0.45, 0.2, 0.12], [0.3, 0.32, 0.35], [0.55, 0.42, 0.2]][Math.floor(rnd(1) * 3)];
    body.set('color', ...c);
    body.box(-W, 1.25, z0 + 0.2, W, 4.0, z1 - 0.2, { top: true });
    body.set('color', c[0] * 0.75, c[1] * 0.75, c[2] * 0.75);
    for (let z = z0 + 1.2; z < z1 - 0.8; z += 1.1) for (const x of [-W - 0.02, W + 0.02]) body.box(x - 0.02, 1.3, z - 0.05, x + 0.02, 3.95, z + 0.05);
    body.set('color', c[0] * 0.6, c[1] * 0.6, c[2] * 0.6);
    for (const x of [-W - 0.03, W + 0.03]) body.box(x - 0.02, 1.35, -1.2, x + 0.02, 3.7, 1.2);
    body.set('color', 0.28, 0.28, 0.3);
    body.box(-W - 0.05, 4.0, z0 + 0.15, W + 0.05, 4.12, z1 - 0.15, { top: true });
  } else if (type === 'tank') {
    const c = rnd(2) < 0.5 ? [0.1, 0.1, 0.11] : [0.72, 0.73, 0.75];
    body.set('color', ...c);
    body.addGeometry(new THREE.CylinderGeometry(1.35, 1.35, WAG_L - 1.6, 20), mat4(0, 2.65, 0, Math.PI / 2, 0, 0));
    for (const z of [z0 + 0.8, z1 - 0.8]) body.addGeometry(new THREE.SphereGeometry(1.35, 16, 8, 0, Math.PI * 2, 0, Math.PI / 2), mat4(0, 2.65, z, z > 0 ? Math.PI / 2 : -Math.PI / 2, 0, 0, 1, 0.35, 1));
    body.set('color', c[0] * 0.8 + 0.05, c[1] * 0.8 + 0.05, c[2] * 0.8 + 0.05);
    body.addGeometry(new THREE.CylinderGeometry(0.45, 0.45, 0.35, 12), mat4(0, 4.05, 0));
    trim.set('color', 0.85, 0.7, 0.15);
    for (const x of [-1.2, 1.2]) trim.box(x - 0.03, 1.5, z0 + 1, x + 0.03, 1.56, z1 - 1);
  } else if (type === 'hopper') {
    const c = [0.42, 0.44, 0.45];
    body.set('color', ...c);
    body.box(-W, 1.9, z0 + 0.3, W, 3.8, z1 - 0.3, { top: false });
    for (const z of [z0 + 3.5, 0, z1 - 3.5]) body.addGeometry(new THREE.ConeGeometry(1.1, 0.8, 4, 1), mat4(0, 1.55, z, Math.PI, Math.PI / 4, 0));
    body.set('color', 0.08, 0.07, 0.07);
    body.box(-W + 0.1, 3.5, z0 + 0.4, W - 0.1, 3.72, z1 - 0.4, { top: true });
    body.set('color', c[0] * 0.8, c[1] * 0.8, c[2] * 0.8);
    for (let z = z0 + 1; z < z1 - 0.6; z += 1.6) for (const x of [-W - 0.02, W + 0.02]) body.box(x - 0.03, 1.95, z - 0.06, x + 0.03, 3.75, z + 0.06);
  } else {
    // flat car with a 40 ft container
    trim.set('color', 0.2, 0.2, 0.21);
    trim.box(-1.4, 1.25, z0 + 0.2, 1.4, 1.4, z1 - 0.2, { top: true });
    const c = CONTAINER[Math.floor(rnd(3) * CONTAINER.length)];
    body.set('color', ...c);
    body.box(-1.22, 1.4, -6.1, 1.22, 4.0, 6.1, { top: true });
    body.set('color', c[0] * 0.78, c[1] * 0.78, c[2] * 0.78);
    for (let z = -5.7; z < 5.8; z += 0.55) for (const x of [-1.24, 1.24]) body.box(x - 0.02, 1.5, z - 0.05, x + 0.02, 3.9, z + 0.05);
    body.set('color', 0.9, 0.9, 0.9);
    for (const x of [-1.26, 1.26]) body.box(x - 0.01, 3.3, -4, x + 0.01, 3.6, -1.5);
  }
  const b = new THREE.Mesh(body.build(), bodyMaterial(0xffffff));
  const t = new THREE.Mesh(trim.build(), M.trim);
  b.castShadow = true; b.receiveShadow = true; t.castShadow = true;
  g.add(b, t);
  g.rotation.order = 'YXZ';
  return { group: g, meshes: [b, t] };
}

// ------------------------------------------------------------------ the train
export class Train extends Vehicle {
  buildModel(d, color) { return locoModel(color); }

  setup(opts) {
    this.rail = this.game.map.roadInfo.rail;
    const L = this.rail.length;
    this.freight = !!this.def.freight;
    this.track = opts.track ?? (this.freight ? 1 : 0);   // 1: takes the passing loop at Fern Creek
    this.cruise = this.freight ? 17 : 24;
    this.stopKeys = this.freight ? ['dry', 'union'] : null; // freight only stops at the ends of the line
    // consist: passenger carriages, or a string of freight wagons
    const kinds = [];
    if (this.freight) {
      const n = opts.wagons ?? 6;
      const types = ['box', 'tank', 'flat', 'hopper', 'flat', 'box', 'tank', 'flat'];
      for (let k = 0; k < n; k++) kinds.push({ type: types[(k + (opts.seed || 0)) % types.length], len: WAG_L });
    } else for (let k = 0; k < NCARS; k++) kinds.push({ type: 'coach', len: CAR_L });
    this.len = LOCO_L + kinds.reduce((a, k) => a + k.len + GAP, 0) + GAP;
    this.s = clamp(opts.s ?? L * 0.5, this.len, L - 1);   // arc length of the locomotive's nose
    this.v = 0;                  // speed along +s
    this.dirS = opts.dirS ?? -1; // which way the autopilot is heading
    this.dwell = opts.dwell ?? 12;
    this.limitLo = -Infinity; this.limitHi = Infinity; // signals: how far the nose / tail may go (set by the rail system)
    this.hold = false;
    this.hx = 1.5; this.hz = LOCO_L / 2;
    this.I = this.mass * (LOCO_L * LOCO_L) / 12;
    this.maxHealth = this.health = 1e9;
    this.cars = [];
    kinds.forEach((k, i) => {
      const c = k.type === 'coach' ? carriageModel(this.color) : wagonModel(k.type, i * 3.7 + (opts.seed || 0));
      this.game.scene.add(c.group);
      this.cars.push({ ...c, pos: new THREE.Vector3(), yaw: 0, len: k.len, coach: k.type === 'coach' });
    });
    // lightweight stand-ins so the vehicle manager's contact code can treat each carriage like a car
    this.proxies = this.cars.map((c) => ({
      pos: c.pos, vel: new THREE.Vector3(), r: 0, yaw: 0, hx: 1.5, hz: c.len / 2, mass: 2e6, I: 1e9, driver: null, def: { name: c.coach ? 'Carriage' : 'Wagon', police: false },
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
      if (!car.coach) return;
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
    const sts = this.stopKeys ? this.stations.filter((st) => this.stopKeys.includes(st.key)) : this.stations;
    if (!sts.length) return 0;
    const c = this.centreS();
    if (this.dwell > 0) {
      this.dwell -= dt;
      // the line ahead isn't clear yet: wait at the platform
      if (this.dwell <= 0 && this.hold && !this._reversing(sts, c)) this.dwell = 0.5;
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
    let signed = (target.s - c) * this.dirS; // distance still to go (negative = overshot)
    const vDir = this.v * this.dirS;
    // a signal short of the station: stop at it and wait for the line to clear
    const lead = this.dirS > 0 ? this.s : this.s - this.len;
    const toSig = this.dirS > 0 ? this.limitHi - lead : lead - this.limitLo;
    if (toSig < signed) {
      if (toSig < 1.5 && Math.abs(this.v) < 1.2) { this.v = 0; return 0; }
      signed = Math.max(0, toSig);
    } else if (Math.abs(signed) < 2 && Math.abs(this.v) < 1.6) {
      this.v = 0; this.dwell = 16; this._atStation = target; this._lastStation = target;
      this.game.events?.emit('trainArrived', this, target);
      return 0;
    }
    // speed along the braking curve (0.8 m/s2), proportional control toward it (a freight train brakes later)
    const vTarget = signed > 0 ? Math.min(this.cruise ?? 24, Math.sqrt(2 * 0.8 * Math.max(0, signed - 0.3))) : -Math.min(2, Math.sqrt(-signed));
    const a = clamp((vTarget - vDir) * 1.4, -1.6, 0.7);
    return a * this.dirS;
  }

  // at a terminus the next move is back the way we came (no station further on)
  _reversing(sts, c) { return !sts.some((st) => st !== this._atStation && (st.s - c) * this.dirS > 5); }

  _place() {
    const r = this.rail, tr = this.track;
    // locomotive: its centre is half a loco length behind the nose (+s is the nose direction)
    railAtTrack(r, this.s - LOCO_L / 2, tr, _t);
    const yF = railAtTrack(r, this.s - 2, tr, [0, 0, 0, 0, 0, 0])[1], yB = railAtTrack(r, this.s - LOCO_L + 2, tr, [0, 0, 0, 0, 0, 0])[1];
    this.pos.set(_t[0], _t[1] + 0.36, _t[2]);
    this.yaw = Math.atan2(_t[3], _t[4]);
    this.pitch = Math.atan2(yF - yB, LOCO_L - 4);
    this.vel.set(_t[3] * this.v, 0, _t[4] * this.v);
    this.r = 0;
    let s = this.s - LOCO_L - GAP;
    this.cars.forEach((car, k) => {
      const CL = car.len;
      const sc = s - CL / 2;
      railAtTrack(r, sc, tr, _t);
      const y0 = railAtTrack(r, sc + CL / 2 - 3, tr, [0, 0, 0, 0, 0, 0])[1], y1 = railAtTrack(r, sc - CL / 2 + 3, tr, [0, 0, 0, 0, 0, 0])[1];
      car.pos.set(_t[0], _t[1] + 0.36, _t[2]);
      car.yaw = Math.atan2(_t[3], _t[4]);
      car.group.position.copy(car.pos);
      car.group.rotation.set(-Math.atan2(y0 - y1, CL - 6), car.yaw, 0);
      const px = this.proxies[k];
      px.yaw = car.yaw; px.vel.set(_t[3] * this.v, 0, _t[4] * this.v);
      s -= CL + GAP;
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
