// Skateboards: a tiny, light "bike" on the car tyre model. The rider stands side-on on the deck, pushes
// with the back foot to get going, carves by leaning, ollies with Space and flips the board in the air
// (A / D: kickflip / heelflip, S: shove-it). Land it clean for a cash bonus; land it crooked and you bail.
import * as THREE from 'three';
import { Bike } from './bikes.js';
import { vehicleMaterials, bodyMaterial } from './vehiclemodels.js';
import { GeoBuilder, mat4 } from '../world/geom.js';
import { clamp, damp } from '../core/utils.js';

const TAU = Math.PI * 2;
const DECK_Y = 0.105; // top of the grip tape above the ground

export function buildBoardModel(def, color) {
  const M = vehicleMaterials();
  const group = new THREE.Group(), bodyGroup = new THREE.Group();
  group.add(bodyGroup);
  // the deck (with trucks and wheels) is its own group so it can flip under the rider
  const deck = new THREE.Group();
  deck.position.y = 0.065;
  bodyGroup.add(deck);
  const L = def.L, hw = def.W / 2;
  // deck outline: a long rounded rectangle with nose and tail kicked up
  const paint = new GeoBuilder(), trim = new GeoBuilder();
  const nx = 10, nz = 26, t = 0.012;
  const shapeW = (z) => { const e = Math.abs(z) / (L / 2); return hw * Math.sqrt(Math.max(0, 1 - Math.max(0, (e - 0.78) / 0.22) ** 2)); };
  const kick = (z) => { const e = (Math.abs(z) - L * 0.3) / (L * 0.2); return e > 0 ? 0.055 * e * e : 0; };
  const concave = (x) => 0.006 * (x / hw) ** 2;
  const top = [], bot = [];
  for (let j = 0; j <= nz; j++) {
    const z = -L / 2 + L * j / nz, w = Math.max(0.02, shapeW(z) * 0.999);
    const rt = [], rb = [];
    for (let i = 0; i <= nx; i++) {
      const x = -w + 2 * w * i / nx, y = kick(z) + concave(x);
      rt.push([x, y + t, z]); rb.push([x, y, z]);
    }
    top.push(rt); bot.push(rb);
  }
  const quadGrid = (gb, rows, up) => {
    for (let j = 0; j < rows.length - 1; j++) for (let i = 0; i < rows[0].length - 1; i++) {
      const a = rows[j][i], b = rows[j][i + 1], c = rows[j + 1][i + 1], d = rows[j + 1][i];
      gb.quad(up ? a : b, up ? d : c, up ? c : d, up ? b : a, [0, up ? 1 : -1, 0]);
    }
  };
  // grip tape on top, painted graphic underneath, the ply edge all round
  trim.set('color', 0.07, 0.07, 0.075);
  quadGrid(trim, top, true);
  paint.set('color', 1, 1, 1);
  quadGrid(paint, bot, false);
  trim.set('color', 0.72, 0.6, 0.42);
  for (const side of [0, nx]) for (let j = 0; j < nz; j++) {
    const a = top[j][side], b = top[j + 1][side], c = bot[j + 1][side], d = bot[j][side];
    trim.quad(side === 0 ? a : b, side === 0 ? b : a, side === 0 ? c : d, side === 0 ? d : c, [side === 0 ? -1 : 1, 0, 0]);
  }
  for (const j of [0, nz]) for (let i = 0; i < nx; i++) {
    const a = top[j][i], b = top[j][i + 1], c = bot[j][i + 1], d = bot[j][i];
    trim.quad(j === 0 ? b : a, j === 0 ? a : b, j === 0 ? d : c, j === 0 ? c : d, [0, 0, j === 0 ? -1 : 1]);
  }
  // a painted stripe on the graphic side
  paint.set('color', 0.15, 0.15, 0.15);
  paint.box(-hw * 0.7, -0.002, -0.05, hw * 0.7, -0.001, 0.05, { top: false, bottom: true, sides: false });
  // trucks: baseplate, kingpin, hanger, axle
  const wheels = [];
  const wheelGeo = (() => {
    const gb = new GeoBuilder();
    gb.set('color', 1, 1, 1);
    const g = new THREE.CylinderGeometry(def.wheelR, def.wheelR, 0.032, 16);
    g.rotateZ(Math.PI / 2);
    gb.addGeometry(g, new THREE.Matrix4());
    gb.set('color', 0.55, 0.55, 0.58);
    const h = new THREE.CylinderGeometry(0.011, 0.011, 0.034, 8); h.rotateZ(Math.PI / 2);
    gb.addGeometry(h, new THREE.Matrix4());
    return gb.build();
  })();
  const wheelMat = new THREE.MeshStandardMaterial({ color: [0xf5f0e0, 0xe63946, 0x2a9d8f, 0xffd166][Math.floor(Math.random() * 4)], roughness: 0.45, vertexColors: true });
  for (const zs of [1, -1]) {
    const z = zs * def.wheelbase / 2;
    trim.set('color', 0.62, 0.63, 0.66);
    trim.box(-0.03, -0.012, z - 0.03, 0.03, 0, z + 0.03);            // baseplate
    trim.addGeometry(new THREE.CylinderGeometry(0.012, 0.016, 0.03, 8), mat4(0, -0.027, z));
    trim.addGeometry(new THREE.CylinderGeometry(0.009, 0.009, def.track + 0.02, 8), mat4(0, def.wheelR - 0.065, z, 0, 0, Math.PI / 2)); // axle
    trim.box(-0.075, -0.05, z - 0.015, 0.075, -0.03, z + 0.015);     // hanger
    for (const xs of [1, -1]) {
      const pivot = new THREE.Group();
      pivot.position.set(xs * def.track / 2, def.wheelR - 0.065, z);
      const spin = new THREE.Group();
      const m = new THREE.Mesh(wheelGeo, wheelMat);
      m.castShadow = true;
      spin.add(m); pivot.add(spin); deck.add(pivot);
      wheels.push({ pivot, spin, x: xs * def.track / 2, z, front: zs > 0, baseY: def.wheelR, comp: 0 });
    }
  }
  const bodyMat = bodyMaterial(color);
  const body = new THREE.Mesh(paint.build(), bodyMat); body.castShadow = true;
  const trimMesh = new THREE.Mesh(trim.build(), M.trim); trimMesh.castShadow = true;
  deck.add(body, trimMesh);
  // (boards have no lamps or glass; the vehicle code expects them)
  const head = new THREE.Mesh(new THREE.BoxGeometry(0.01, 0.01, 0.01), M.headOff); head.visible = false;
  const tail = new THREE.Mesh(new THREE.BoxGeometry(0.01, 0.01, 0.01), M.tailOff); tail.visible = false;
  const glass = new THREE.Mesh(new THREE.BoxGeometry(0.01, 0.01, 0.01), M.glass); glass.visible = false;
  bodyGroup.add(head, tail, glass);
  return {
    group, bodyGroup, deck, body, bodyMat, glass, head, tail, wheels, trim: trimMesh,
    door: { pivot: new THREE.Group(), mesh: head, open: 0 },
    seats: [new THREE.Vector3(0, DECK_Y, 0)],
    stand: { yaw: -Math.PI / 2 }, // the rider stands side-on, left foot forward, facing the board's left
    seatHip: 0,
    doorPos: new THREE.Vector3(0.7, 0, 0),
    grips: [0, 0],
  };
}

export class Skateboard extends Bike {
  setup() {
    super.setup();
    this.pushPhase = 0; this.pushK = 0;
    this.flip = 0; this.flipV = 0; this.shove = 0; this.shoveV = 0;
    this.crouch = 0; this.popT = 0; this.trickName = null; this.airT = 0;
  }
  buildModel(def, color) { return buildBoardModel(def, color); }
  nearestDoor(pos) { const p = this.pos.clone(); p.y = pos.y; p.seat = 0; return p; }
  doorFor(seat, char) {
    const side = char ? (this.worldToLocal(char.pos.x, char.pos.z)[0] >= 0 ? 1 : -1) : 1;
    return this.localToWorld(0.45 * side, 0, 0, new THREE.Vector3());
  }

  // feet on the deck (character frame: +x is towards the nose), the back foot pushing off the ground
  feetFor() {
    const deckY = DECK_Y + 0.02, crouch = this.crouch;
    const p = this.pushK, ph = this.pushPhase % 1;
    // push stroke: plant beside the board, sweep back, lift and swing forward again
    const sweep = ph < 0.55 ? ph / 0.55 : 1 - (ph - 0.55) / 0.45;
    const lift = ph < 0.55 ? 0 : Math.sin((ph - 0.55) / 0.45 * Math.PI) * 0.12;
    const bx = -0.22 + p * (0.12 - 0.5 * sweep), by = deckY + p * (-0.1 + lift), bz = 0.02 + p * 0.16;
    return [[0.24, deckY - crouch * 0.1, 0.02], [bx, by, bz]];
  }
  gripsFor() { return null; }

  playerControl(input, dt) {
    super.playerControl(input, dt);
    this.input.handbrake = false;
    this.wantOllie = input.hit('handbrake');
  }

  ollie() {
    if (this.airborne || !this.driver || this.popT > 0) return false;
    this.airborne = true;
    this.vy = 4.3 + Math.min(1.2, this.speedAbs * 0.08);
    this.popT = 0.25;
    this.flip = 0; this.flipV = 0; this.shove = 0; this.shoveV = 0; this.trickName = null; this.airT = 0;
    this.game.audio?.playAt('clink', this.pos, 0.9);
    this.game.audio?.playAt('footstep', this.pos, 1.5);
    return true;
  }

  update(dt) {
    if (this.wantOllie) { this.wantOllie = false; this.ollie(); }
    // NPC riders (and the player) push along with the back foot when below cruising speed
    const drv = this.driver;
    const pushing = !!drv && !this.airborne && !this.tb && this.input.throttle > 0.1 && this.speed > -0.5 && this.speedAbs < this.def.top * 0.85;
    this.pushK = damp(this.pushK, pushing ? 1 : 0, 8, dt);
    if (pushing || this.pushK > 0.05) this.pushPhase += dt / 0.85;
    else this.pushPhase = 0;
    // no push, no go: coast between strokes (the tyre model sees the throttle only on the push)
    const thr = this.input.throttle;
    if (pushing) { const ph = this.pushPhase % 1; this.input.throttle = ph < 0.55 ? thr * 1.6 : 0; }
    const wasAir = this.airborne;
    super.update(dt);
    this.input.throttle = thr;
    if (this.removed) return;
    this.popT -= dt;
    // tricks in the air
    if (this.airborne && !this.tb) {
      this.airT += dt;
      const st = this.input.steer;
      if (Math.abs(st) > 0.5 && this.flipV === 0 && this.airT < 0.35) { this.flipV = -Math.sign(st) * 15; this.trickName = st > 0 ? 'KICKFLIP' : 'HEELFLIP'; }
      if (this.input.brake > 0.5 && this.shoveV === 0 && this.airT < 0.35) { this.shoveV = 13; this.trickName = this.trickName ? this.trickName.replace('FLIP', 'FLIP SHOVE-IT') : 'SHOVE-IT'; }
      this.flip += this.flipV * dt; this.shove += this.shoveV * dt;
      // the flip slows to catch it on a whole turn
      if (this.flipV && Math.abs(this.flip) > TAU * 0.8) { this.flipV *= 0.9; if (Math.abs(this.flip) >= TAU - 0.12) { this.flip = 0; this.flipV = 0; this._flipDone = (this._flipDone || 0) + 1; } }
      if (this.shoveV && this.shove > Math.PI * 0.8) { this.shoveV *= 0.9; if (this.shove >= Math.PI - 0.1) { this.shove = 0; this.shoveV = 0; this._shoveDone = true; } }
    }
    if (wasAir && !this.airborne && !this.tb) this._land();
    this.skid = 0; // (no tyre smoke or skid marks from urethane wheels)
    // only smooth ground rolls: on sand, grass or dirt a board digs in and stops
    this._pavedT = (this._pavedT ?? 0) - dt;
    if (this._pavedT <= 0) { this._pavedT = 0.25; this._paved = this._isPaved(); }
    this._surface = 1;
    if (!this._paved && !this.airborne && !this.tb) { const k = Math.max(0, 1 - 3.5 * dt); this.vel.x *= k; this.vel.z *= k; }
    this.crouch = damp(this.crouch, this.airborne ? 0.3 : this.popT > 0 ? 0.2 : this.pushK * 0.08 + 0.05, 14, dt);
  }

  _isPaved() {
    const map = this.game.map, x = this.pos.x, z = this.pos.z;
    if (map.isOnRoad(x, z) || map.inCity?.(x, z)) return true;
    if (this.pos.y > map.groundHeight(x, z) + 0.2) return true; // on a deck, a ramp or a bridge
    for (const p of map.padSurfaces || []) {
      const s = Math.sin(p.yaw), c = Math.cos(p.yaw), dx = x - p.cx, dz = z - p.cz;
      if (Math.abs(dx * c - dz * s) < p.hx && Math.abs(dx * s + dz * c) < p.hz) return true;
    }
    return false;
  }

  _land() {
    const clean = Math.abs(this.flip) < 0.5 && Math.abs(this.shove) < 0.45;
    const did = this.trickName && (this._flipDone || this._shoveDone);
    const pl = this.driver?.isPlayer ? this.driver : null;
    if (!clean && this.driver) { this.throwRiders(6); this.flip = 0; this.shove = 0; }
    else if (did && pl) {
      const cash = 25 + Math.round(this.airT * 40) + (this._flipDone && this._shoveDone ? 40 : 0);
      pl.money += cash;
      this.game.hud?.bigMessage(this.trickName, 'hint', 1.4, `+$${cash}`);
      this.game.audio?.play('cash', 0.4);
      this.game.stats && (this.game.stats.tricks = (this.game.stats.tricks || 0) + 1);
      this.game.events?.emit('skateTrick', this.trickName, cash, this);
    }
    this.game.audio?.playAt('clink', this.pos, 1);
    this.flip = 0; this.shove = 0; this.flipV = 0; this.shoveV = 0; this.trickName = null; this._flipDone = 0; this._shoveDone = false;
    this.popT = 0.15;
  }

  _updateVisual(dt) {
    super._updateVisual(dt);
    const d = this.model.deck;
    if (!d) return;
    // the tail pops and the board flips / spins under the rider
    const pop = this.popT > 0 ? Math.sin(clamp(this.popT / 0.25, 0, 1) * Math.PI) * 0.35 : 0;
    d.rotation.set(this.airborne ? -pop : 0, this.shove, this.flip);
    d.position.y = 0.065 + (this.airborne ? 0.04 : 0);
    // the board tilts with the carve (the trucks) more than a bike leans
    if (!this.tb) this.group.rotation.z = this.groundRoll - (this.lean || 0) * 0.35;
  }

  onCrash(impact) { if (impact > 3.8) this.throwRiders(impact); }
}
