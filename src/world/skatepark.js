// Skateparks: concrete pads with a funbox, kickers and landings, a pyramid, banks, ledges and a rail.
// Boards lie about for anyone to pick up, and a few locals skate laps and throw tricks.
import * as THREE from 'three';
import { patch } from '../render/materials.js';
import { Part, roundBox } from '../entities/loft.js';
import { clamp, rand, pick } from '../core/utils.js';

export const SKATEPARKS = [
  { key: 'santaluz', name: 'Santa Luz Skatepark', x: -440, z: 663, yaw: 0, hx: 30, hz: 13, y: 0.05 },
];

// terrain pads (CityMap.build flattens them) and the concrete they're painted with
export function skateparkPads(list = SKATEPARKS) {
  return list.map((p) => ({ key: 'skate_' + p.key, minX: p.x - p.hx - 2, maxX: p.x + p.hx + 2, minZ: p.z - p.hz - 2, maxZ: p.z + p.hz + 2, y: p.y, blend: 18 }));
}
export function planSkateparks(map, list = SKATEPARKS) {
  for (const p of list) {
    map.padSurfaces.push({ cx: p.x, cz: p.z, hx: p.hx, hz: p.hz, yaw: p.yaw, y: p.y + 0.02, type: 'asphalt', skate: true });
    map.landmarks['skate_' + p.key] = { x: p.x, z: p.z, name: p.name };
  }
}

// ramp layout in park space (x along the park, z across): wedges rise from (x0) to (x1) over the local x
// axis (or z when `alongZ`), flats are table tops
function layout(p) {
  const R = [];
  const wedge = (ax, az, bx, bz, h0, h1, hw) => R.push({ kind: 'deck', ax, az, bx, bz, h0, h1, hw });
  // funbox: a 7 x 4 table at 0.7 with ramps both ends
  wedge(-6.1, 0, -3.5, 0, 0, 0.7, 2); wedge(-3.5, 0, 3.5, 0, 0.7, 0.7, 2); wedge(6.1, 0, 3.5, 0, 0, 0.7, 2);
  // jump line: kicker, gap, landing
  wedge(10, -6.5, 12.6, -6.5, 0, 0.9, 1.5); wedge(17.2, -6.5, 20.8, -6.5, 0.9, 0, 1.5);
  R.push({ kind: 'table', x0: 12.6, x1: 13.2, z0: -8, z1: -5, h: 0.9 }, { kind: 'table', x0: 16.6, x1: 17.2, z0: -8, z1: -5, h: 0.9 });
  // pyramid: a 3 x 3 top at 0.6 with four ramps
  wedge(-22.2, 0, -20, 0, 0, 0.6, 1.5); wedge(-15.8, 0, -18, 0, 0, 0.6, 1.5);
  wedge(-19, -3.7, -19, -1.5, 0, 0.6, 1.5); wedge(-19, 3.7, -19, 1.5, 0, 0.6, 1.5);
  wedge(-20, 0, -18, 0, 0.6, 0.6, 1.5);
  // banks along the seaward side
  wedge(-10, 12.8, -10, 10.4, 1.1, 0, 5); wedge(8, 12.8, 8, 10.4, 1.1, 0, 5);
  // ledges and a rail (low: walk over them, bump a board)
  R.push({ kind: 'ledge', x0: -9, x1: -1, z0: -10.6, z1: -10.1, h: 0.45 }, { kind: 'ledge', x0: 3, x1: 9, z0: -10.6, z1: -10.1, h: 0.45 });
  R.push({ kind: 'rail', x0: -6, x1: 4, z: 7.5, h: 0.4 });
  return R;
}

export class Skateparks {
  constructor(game) {
    this.game = game;
    this.parks = SKATEPARKS.map((p) => ({ ...p, boards: [], skaters: [], active: false }));
    this.mat = patch(new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.85, metalness: 0.05 }), { key: 'skatepark' });
    this.metal = patch(new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.3, metalness: 0.85 }), { key: 'skaterail' });
    for (const p of this.parks) this._build(p);
    this.t = 0;
  }

  _toWorld(p, lx, lz) { const s = Math.sin(p.yaw), c = Math.cos(p.yaw); return [p.x + lx * c + lz * s, p.z - lx * s + lz * c]; }

  _build(p) {
    const col = this.game.collision;
    const conc = new Part(), metal = new Part();
    const y0 = p.y + 0.02;
    const W = (lx, lz) => this._toWorld(p, lx, lz);
    const grey = (k) => conc.color(0.66 * k, 0.64 * k, 0.6 * k);
    for (const r of layout(p)) {
      if (r.kind === 'deck') {
        const [ax, az] = W(r.ax, r.az), [bx, bz] = W(r.bx, r.bz);
        col.addDeck({ ax, az, ay: y0 + r.h0, bx, bz, by: y0 + r.h1, hl: r.hw, hr: r.hw, skate: true });
        // mesh: sloped (or flat) top and the sides down to the pad
        const dx = bx - ax, dz = bz - az, L = Math.hypot(dx, dz), ux = dx / L, uz = dz / L, nx = -uz, nz = ux;
        const P = (t, s, h) => [ax + ux * t + nx * s, h, az + uz * t + nz * s];
        const ya = y0 + r.h0, yb = y0 + r.h1;
        grey(1.05);
        conc.poly([P(0, -r.hw, ya), P(L, -r.hw, yb), P(L, r.hw, yb), P(0, r.hw, ya)], [0, 1, 0]);
        grey(0.85);
        for (const sgn of [-1, 1]) conc.poly([P(0, sgn * r.hw, y0), P(L, sgn * r.hw, y0), P(L, sgn * r.hw, yb), P(0, sgn * r.hw, ya)], [nx * sgn, 0, nz * sgn]);
        for (const [t, h] of [[0, ya], [L, yb]]) if (h > y0 + 0.02) conc.poly([P(t, -r.hw, y0), P(t, r.hw, y0), P(t, r.hw, h), P(t, -r.hw, h)], [ux * (t ? 1 : -1), 0, uz * (t ? 1 : -1)]);
        // coping along the lip of a kicker (steel edge)
        if (r.h1 > r.h0 + 0.3) { metal.color(0.75, 0.75, 0.78); const [cx, cz] = [bx, bz]; metal.geo(new THREE.CylinderGeometry(0.035, 0.035, r.hw * 2, 8), new THREE.Matrix4().makeRotationFromEuler(new THREE.Euler(0, Math.atan2(nx, nz), Math.PI / 2, 'YXZ')).setPosition(cx, yb, cz)); }
      } else if (r.kind === 'table' || r.kind === 'ledge') {
        const [x0, z0] = W(r.x0, r.z0), [x1, z1] = W(r.x1, r.z1);
        const minX = Math.min(x0, x1), maxX = Math.max(x0, x1), minZ = Math.min(z0, z1), maxZ = Math.max(z0, z1);
        grey(r.kind === 'ledge' ? 0.75 : 0.9);
        conc.box(minX, y0, minZ, maxX, y0 + r.h, maxZ);
        if (r.kind === 'ledge') { metal.color(0.7, 0.7, 0.72); metal.box(minX, y0 + r.h - 0.02, minZ - 0.01, maxX, y0 + r.h + 0.01, minZ + 0.06); }
        if (r.kind === 'table') col.addDeck({ ax: minX, az: (minZ + maxZ) / 2, ay: y0 + r.h, bx: maxX, bz: (minZ + maxZ) / 2, by: y0 + r.h, hl: (maxZ - minZ) / 2, hr: (maxZ - minZ) / 2, skate: true });
        else col.addBox({ minX, maxX, minZ, maxZ, minY: y0 - 0.2, maxY: y0 + r.h, type: 'ledge' });
      } else if (r.kind === 'rail') {
        const [x0, z0] = W(r.x0, r.z), [x1, z1] = W(r.x1, r.z);
        metal.color(0.8, 0.8, 0.82);
        const len = Math.hypot(x1 - x0, z1 - z0), yaw = Math.atan2(x1 - x0, z1 - z0);
        metal.geo(new THREE.CylinderGeometry(0.03, 0.03, len, 8), new THREE.Matrix4().makeRotationFromEuler(new THREE.Euler(Math.PI / 2, yaw, 0, 'YXZ')).setPosition((x0 + x1) / 2, y0 + r.h, (z0 + z1) / 2));
        for (let k = 0; k <= 3; k++) { const t = k / 3; metal.box(x0 + (x1 - x0) * t - 0.025, y0, z0 + (z1 - z0) * t - 0.025, x0 + (x1 - x0) * t + 0.025, y0 + r.h, z0 + (z1 - z0) * t + 0.025); }
      }
    }
    // a low wall round the landward side with painted panels, benches and floodlights
    const wall = (ax, az, bx, bz) => {
      const [x0, z0] = W(ax, az), [x1, z1] = W(bx, bz);
      grey(0.8);
      conc.box(Math.min(x0, x1) - 0.15, y0, Math.min(z0, z1) - 0.15, Math.max(x0, x1) + 0.15, y0 + 0.9, Math.max(z0, z1) + 0.15);
      this.game.collision.addBox({ minX: Math.min(x0, x1) - 0.15, maxX: Math.max(x0, x1) + 0.15, minZ: Math.min(z0, z1) - 0.15, maxZ: Math.max(z0, z1) + 0.15, minY: y0 - 0.2, maxY: y0 + 0.9, type: 'wall' });
    };
    wall(-p.hx, -p.hz, -6, -p.hz); wall(6, -p.hz, p.hx, -p.hz); // (a gap in the middle to walk in)
    const tags = [[0.9, 0.2, 0.3], [0.2, 0.6, 0.9], [0.95, 0.75, 0.1], [0.3, 0.8, 0.4], [0.7, 0.3, 0.9]];
    for (let k = 0; k < 9; k++) {
      const lx = -p.hx + 2 + k * 3.2 + (k > 3 ? 12.6 : 0);
      if (lx > p.hx - 2) break;
      const [x, z] = W(lx, -p.hz + 0.16);
      conc.color(...pick(tags));
      conc.box(x - rand(0.5, 1.2), y0 + 0.15, z, x + rand(0.5, 1.2), y0 + rand(0.5, 0.8), z + 0.012);
    }
    for (const lx of [-26, 26]) {
      const [x, z] = W(lx, -p.hz + 1.2);
      metal.color(0.3, 0.3, 0.32);
      metal.geo(new THREE.CylinderGeometry(0.08, 0.1, 7, 8), new THREE.Matrix4().setPosition(x, y0 + 3.5, z));
      metal.color(1, 1, 0.9); metal.box(x - 0.4, y0 + 6.9, z - 0.15, x + 0.4, y0 + 7.2, z + 0.15);
    }
    for (const lx of [-12, 14]) { const [x, z] = W(lx, -p.hz + 1.1); conc.color(0.45, 0.33, 0.22); conc.box(x - 1, y0 + 0.4, z - 0.25, x + 1, y0 + 0.47, z + 0.25); grey(0.7); conc.box(x - 0.9, y0, z - 0.15, x - 0.7, y0 + 0.4, z + 0.15); conc.box(x + 0.7, y0, z - 0.15, x + 0.9, y0 + 0.4, z + 0.15); }
    const g = new THREE.Group();
    const m1 = new THREE.Mesh(conc.build(), this.mat); m1.castShadow = true; m1.receiveShadow = true;
    const m2 = new THREE.Mesh(metal.build(), this.metal); m2.castShadow = true;
    g.add(m1, m2);
    this.game.scene.add(g);
    p.group = g;
    // where boards lie and the lap the locals skate
    p.boardSpots = [[-12, -p.hz + 2.2, 0.4], [14, -p.hz + 2.4, 2.8], [-26, 6, 1.2], [24, 6, -1.4]].map(([lx, lz, yaw]) => { const [x, z] = W(lx, lz); return { x, z, yaw: yaw + p.yaw }; });
    p.loop = [[-26, 0], [-19, 0], [-12, 0], [-6, 0], [6, 0], [12, -6.5], [22, -6.5], [26, 4], [8, 7], [-10, 7], [-26, 6]].map(([lx, lz]) => { const [x, z] = W(lx, lz); return { x, z }; });
  }

  update(dt) {
    this.t -= dt;
    if (this.t > 0) return;
    this.t = 1;
    const g = this.game, pl = g.player.vehicle ? g.player.vehicle.pos : g.player.pos;
    for (const p of this.parks) {
      const d = Math.hypot(pl.x - p.x, pl.z - p.z);
      if (d < 140 && !p.active) this._activate(p);
      else if (d > 220 && p.active) this._deactivate(p);
      if (p.active) {
        // keep the locals going (and replace any who've wandered off or been knocked flying)
        p.skaters = p.skaters.filter((s) => !s.v.removed && s.v.driver === s.ped && !s.ped.dead);
        if (p.skaters.length < 2 && d > 25) this._spawnSkater(p);
      }
    }
  }

  _activate(p) {
    p.active = true;
    const vm = this.game.vehicles;
    for (const s of p.boardSpots) {
      if (vm.list.some((v) => Math.hypot(v.pos.x - s.x, v.pos.z - s.z) < 1.5)) continue;
      const b = vm.spawn('skateboard', s.x, s.z, s.yaw, { parked: true });
      b.persistent = true;
      p.boards.push(b);
    }
    for (let i = 0; i < 2; i++) this._spawnSkater(p);
  }

  _spawnSkater(p) {
    const g = this.game;
    if (!g.peds?.spawnPed) return;
    const k = Math.floor(Math.random() * p.loop.length), a = p.loop[k], b = p.loop[(k + 1) % p.loop.length];
    const yaw = Math.atan2(b.x - a.x, b.z - a.z);
    const board = g.vehicles.spawn('skateboard', a.x, a.z, yaw);
    board.persistent = true;
    const ped = g.peds.spawnPed(a.x, a.z, { persistent: true, young: true });
    if (!ped) { g.vehicles.remove(board); return; }
    board.putIn(ped, 0);
    board.ai = new SkaterAI(g, board, p.loop, (k + 1) % p.loop.length);
    p.skaters.push({ v: board, ped });
  }

  _deactivate(p) {
    p.active = false;
    const vm = this.game.vehicles;
    for (const b of p.boards) if (!b.removed && !b.driver) vm.remove(b);
    for (const s of p.skaters) { if (!s.v.removed && s.v.driver === s.ped) { vm.remove(s.v); } }
    p.boards = []; p.skaters = [];
  }
}

// a local skating laps of the park: steer for the next point, keep a pace, ollie off the kickers
class SkaterAI {
  constructor(game, v, loop, idx) { this.game = game; this.v = v; this.loop = loop; this.idx = idx; this.pace = rand(4.5, 6.5); this.olT = rand(2, 6); }
  update(dt) {
    const v = this.v;
    const tp = this.loop[this.idx];
    const d = Math.hypot(tp.x - v.pos.x, tp.z - v.pos.z);
    if (d < 2.2) this.idx = (this.idx + 1) % this.loop.length;
    const [lx, lz] = v.worldToLocal(tp.x, tp.z);
    const ang = Math.atan2(lx, Math.max(0.3, lz));
    v.input.steer = clamp(ang / (v.def.steer * 0.8), -1, 1);
    const want = Math.abs(ang) > 0.8 ? this.pace * 0.6 : this.pace;
    v.input.throttle = v.speed < want ? 1 : 0;
    v.input.brake = v.speed > want + 1.5 ? 0.5 : 0;
    // tricks: ollie now and then, flick the board in the air
    this.olT -= dt;
    if (this.olT <= 0 && v.speed > 3 && !v.airborne) { this.olT = rand(3, 8); v.ollie?.(); this.trick = Math.random() < 0.6 ? pick([-1, 1]) : 0; this.trickT = 0.1; }
    if (v.airborne && this.trickT > 0) { this.trickT -= dt; if (this.trickT <= 0 && this.trick) v.input.steer = this.trick; }
    // someone in the way: slow up
    for (const c of this.game.allCharacters()) {
      if (c === v.driver || c.vehicle) continue;
      const [ox, oz] = v.worldToLocal(c.pos.x, c.pos.z);
      if (oz > 0 && oz < 4 && Math.abs(ox) < 1) { v.input.throttle = 0; v.input.brake = 1; break; }
    }
  }
}
