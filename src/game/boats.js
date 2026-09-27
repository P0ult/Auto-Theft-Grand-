// Boats in the world: marinas with boats tied up (the Santa Luz pontoon, Port Morena, Port Hale and Lake
// Mirador), pleasure boats and jet skis cruising the coast, a harbour patrol, and police boats that come
// after you when you're wanted out on the water (they ram, and the bow gunner opens up).
import * as THREE from 'three';
import { WATER_Y, CITY } from '../world/citymap.js';
import { LAKE } from '../world/worldgen.js';
import { Part } from '../entities/loft.js';
import { patch } from '../render/materials.js';
import { clamp, rand, pick, dist2 } from '../core/utils.js';

// a floating pontoon: planks on floats, walkable, boats moor alongside
function pontoon(game, x0, z0, x1, z1, y) {
  const g = new Part();
  g.color(0.52, 0.4, 0.28);
  const minX = Math.min(x0, x1), maxX = Math.max(x0, x1), minZ = Math.min(z0, z1), maxZ = Math.max(z0, z1);
  const alongZ = maxZ - minZ > maxX - minX;
  const len = alongZ ? maxZ - minZ : maxX - minX;
  for (let k = 0; k < len / 0.5; k++) {
    const a = (alongZ ? minZ : minX) + k * 0.5;
    if (alongZ) g.box(minX, y - 0.12, a + 0.03, maxX, y, a + 0.47); else g.box(a + 0.03, y - 0.12, minZ, a + 0.47, y, maxZ);
  }
  g.color(0.85, 0.85, 0.82);
  for (let k = 0; k < len / 3; k++) {
    const a = (alongZ ? minZ : minX) + k * 3 + 1.5;
    if (alongZ) g.box(minX + 0.2, y - 0.5, a - 0.6, maxX - 0.2, y - 0.12, a + 0.6); else g.box(a - 0.6, y - 0.5, minZ + 0.2, a + 0.6, y - 0.12, maxZ - 0.2);
    // cleats
    g.color(0.3, 0.3, 0.32);
    for (const s of [0, 1]) { const e = s ? (alongZ ? maxX - 0.15 : maxZ - 0.15) : (alongZ ? minX + 0.15 : minZ + 0.15); if (alongZ) g.box(e - 0.05, y, a - 0.12, e + 0.05, y + 0.08, a + 0.12); else g.box(a - 0.12, y, e - 0.05, a + 0.12, y + 0.08, e + 0.05); }
    g.color(0.85, 0.85, 0.82);
  }
  const m = new THREE.Mesh(g.build(), patch(new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.85 }), { key: 'pontoon' }));
  m.castShadow = true; m.receiveShadow = true;
  game.scene.add(m);
  // walkable top (a floor for people), too low to stop a boat sliding alongside... but boats can't cross it
  game.collision.addBox({ minX, maxX, minZ, maxZ, minY: y - 3, maxY: y, type: 'pontoon' });
}

export class BoatSystem {
  constructor(game) {
    this.game = game;
    this.marinas = [];
    this.ambient = [];
    this.police = [];
    this.t = 0;
    this.policeT = 0;
    this._plan();
  }

  _plan() {
    const g = this.game, map = g.map, L = map.landmarks;
    const m = (name, x, z, spots, extra = {}) => this.marinas.push({ name, x, z, spots, boats: [], active: false, ...extra });
    // Santa Luz: a pontoon off the beach, west of the pier
    {
      const x = 62, zA = 692, zB = 752;
      pontoon(g, x - 1.6, zA, x + 1.6, zB, WATER_Y + 0.6);
      m('Santa Luz Marina', x, 720, [
        { x: x - 4.2, z: 716, yaw: 0, types: ['speedboat', 'dinghy'] }, { x: x - 4.2, z: 734, yaw: 0, types: ['speedboat'] },
        { x: x + 3.8, z: 712, yaw: 0, types: ['jetski'] }, { x: x + 3.8, z: 722, yaw: 0, types: ['jetski', 'dinghy'] }, { x: x + 4.6, z: 740, yaw: 0, types: ['cruiser', 'speedboat'] },
      ]);
      L.marina = { x: x - 0.5, z: 698, name: 'Santa Luz Marina' };
    }
    // Port Morena: along the quay north of the cargo ship (the harbour patrol berths here)
    {
      const qx = CITY.maxX + 44;
      m('Port Morena', qx, 110, [
        { x: qx + 4, z: 70, yaw: Math.PI, types: ['policeboat'], police: true }, { x: qx + 4, z: 95, yaw: Math.PI, types: ['speedboat', 'cruiser'] },
        { x: qx + 4, z: 130, yaw: Math.PI, types: ['dinghy', 'speedboat'] },
      ]);
    }
    // the model boats that were tied up at Port Hale and on the lake become real ones
    const propBoats = (g.city?.propColliders || []).filter((c) => c.prop?.type === 'boat');
    const take = (pred) => {
      const out = [];
      for (const c of propBoats) if (pred(c.prop)) { g.city.breakProp(c); c.gone = true; out.push(c.prop); }
      return out;
    };
    if (L.halePier) {
      const was = take((p) => p.y > -2 && p.y < 0);
      m('Port Hale', L.halePier.x, L.halePier.z, was.map((p, i) => ({ x: p.x, z: p.z, yaw: p.rot, types: i === 1 ? ['cruiser'] : ['speedboat', 'dinghy'] })));
    }
    // Lake Mirador: dinghies and jet skis by the floating docks
    {
      const was = take((p) => p.y > LAKE.y - 2);
      m('Lake Mirador', LAKE.x + 200, LAKE.z + 10, [
        ...was.map((p) => ({ x: p.x, z: p.z, yaw: p.rot, types: ['dinghy'] })),
        { x: -1318, z: -1860, yaw: Math.PI / 2, types: ['jetski'] }, { x: -1316, z: -1910, yaw: Math.PI / 2, types: ['jetski'] },
      ]);
    }
    // cruising routes (loops out at sea and on the lake)
    this.routes = [
      { name: 'santaluz', lake: false, types: ['speedboat', 'cruiser', 'dinghy'], n: 2, pts: [[-600, 880], [-200, 940], [300, 900], [700, 1020], [300, 1150], [-300, 1100], [-800, 1000]] },
      { name: 'beach', lake: false, types: ['jetski'], n: 2, pts: [[-320, 765], [-120, 790], [40, 775], [-60, 835], [-260, 820]] },
      { name: 'coast', lake: false, types: ['speedboat', 'cruiser'], n: 1, pts: [[1050, 500], [1150, 0], [1300, -500], [1350, -1000], [1250, -1400], [1400, -900], [1300, 200]] },
      { name: 'lake', lake: true, types: ['dinghy', 'jetski'], n: 1, pts: [[-1400, -1800], [-1520, -1760], [-1620, -1880], [-1520, -2020], [-1380, -1960]] },
    ].map((r) => ({ ...r, pts: r.pts.filter(([x, z]) => this._deep(x, z, 2)), boats: [] })).filter((r) => r.pts.length >= 3);
  }

  _deep(x, z, d = 1.5) { const map = this.game.map; return map.waterLevel(x, z) - map.terrainHeight(x, z) > d; }

  update(dt) {
    this.t -= dt;
    if (this.t <= 0) { this.t = 1; this._stream(); }
    this._policeUpdate(dt);
  }

  get _pp() { const p = this.game.player; return p.vehicle ? p.vehicle.pos : p.pos; }

  _stream() {
    const g = this.game, vm = g.vehicles, pp = this._pp;
    // moored boats
    for (const mr of this.marinas) {
      mr.boats = mr.boats.filter((b) => !b.removed);
      const d = Math.hypot(pp.x - mr.x, pp.z - mr.z);
      if (d < 260 && !mr.active) {
        mr.active = true;
        for (const s of mr.spots) {
          if (vm.list.some((v) => dist2(v.pos.x, v.pos.z, s.x, s.z) < 9)) continue;
          const b = vm.spawn(pick(s.types), s.x, s.z, s.yaw, { parked: true });
          b.moored = true;
          if (s.police) b.locked = false;
          mr.boats.push(b);
        }
      } else if (d > 380 && mr.active) {
        mr.active = false;
        for (const b of mr.boats) if (!b.driver && b.moored && !b.persistent) vm.remove(b);
        mr.boats = [];
      }
      // moored boats stay put (lines out) until someone takes them
      for (const b of mr.boats) if (b.moored) { if (b.driver) b.moored = false; else { b.vel.multiplyScalar(0.8); b.r *= 0.8; } }
    }
    // cruising traffic
    for (const r of this.routes) {
      r.boats = r.boats.filter((b) => !b.removed && b.driver && !b.driver.dead && !b.isWrecked);
      let dmin = Infinity;
      for (const [x, z] of r.pts) dmin = Math.min(dmin, Math.hypot(pp.x - x, pp.z - z));
      if (dmin < 500 && r.boats.length < r.n) this._spawnCruiser(r);
      if (dmin > 750) { for (const b of r.boats) if (!b.driver?.isPlayer) this._despawn(b); r.boats = []; }
    }
  }

  _spawnCruiser(r) {
    const g = this.game, pp = this._pp;
    // start at a route point out of sight-ish (not right next to the player)
    const cands = r.pts.map((p, i) => ({ p, i, d: Math.hypot(pp.x - p[0], pp.z - p[1]) })).filter((c) => c.d > 90 && !this.game.vehicles.list.some((v) => dist2(v.pos.x, v.pos.z, c.p[0], c.p[1]) < 30 * 30)).sort((a, b) => a.d - b.d);
    const c = cands[0];
    if (!c) return;
    const nx = r.pts[(c.i + 1) % r.pts.length];
    const b = g.vehicles.spawn(pick(r.types), c.p[0], c.p[1], Math.atan2(nx[0] - c.p[0], nx[1] - c.p[1]));
    const ped = g.peds?.spawnPed(c.p[0], c.p[1], { persistent: true });
    if (!ped) { g.vehicles.remove(b); return; }
    b.putIn(ped, 0);
    b.persistent = true;
    b.ai = new BoatDriver(g, b, r.pts.map(([x, z]) => ({ x, z })), (c.i + 1) % r.pts.length, rand(0.45, 0.75));
    r.boats.push(b);
  }

  _despawn(b) {
    const g = this.game;
    for (const o of b.occupants) if (o && !o.isPlayer) g.peds?.remove(o);
    b.occupants.fill(null);
    g.vehicles.remove(b);
  }

  // ---------------------------------------------------------------- police on the water
  get playerAtSea() {
    const p = this.game.player;
    return !!(p.vehicle?.isBoat) || (p.swimming && !p.vehicle);
  }

  _policeUpdate(dt) {
    const g = this.game, pol = g.police;
    if (!pol) return;
    this.police = this.police.filter((b) => !b.removed);
    const pp = this._pp;
    const want = pol.level > 0 && this.playerAtSea && !g.player.dead ? (pol.level >= 3 ? 3 : pol.level >= 2 ? 2 : 1) : 0;
    const active = this.police.filter((b) => !b.isWrecked && b.driver && !b.driver.dead);
    this.policeT -= dt;
    if (active.length < want && this.policeT <= 0) { this.policeT = 6; this._spawnPolice(); }
    for (const b of this.police) {
      if (b.isWrecked || !b.driver || b.driver.dead) continue;
      const chase = pol.level > 0 && !g.player.dead;
      b.ai.target = chase ? pp : null; // (the harbour patrol is talked in by the helicopter and the radio)
      b.sirenOn = chase;
      // the bow gunner (from two stars, or once they've been shot at)
      const gunner = b.occupants[1] || b.occupants[0];
      const d = Math.hypot(pp.x - b.pos.x, pp.z - b.pos.z);
      b.gunT2 = (b.gunT2 ?? 1) - dt;
      if (chase && pol.level >= 2 && gunner && !gunner.dead && d < 90 && b.gunT2 <= 0) {
        b.gunT2 = rand(0.16, 0.3);
        const burst = (b.burst = ((b.burst || 0) + 1) % 14) < 5; // short bursts, then a pause
        if (burst && g.collision.lineOfSight(b.pos.x, b.pos.y + 2, b.pos.z, pp.x, pp.y + 1, pp.z)) {
          const miss = clamp(d / 25, 0.6, 3.5) * (1.4 - pol.level * 0.12);
          const tgt = new THREE.Vector3(pp.x + rand(-miss, miss), pp.y + 0.8 + rand(-0.5, 0.8), pp.z + rand(-miss, miss));
          gunner.isPlayer = false;
          b.fireGun(tgt);
        }
      }
      // off duty and far away: back to base (gone)
      if (!chase && d > 300) this._despawn(b);
    }
  }

  _spawnPolice() {
    const g = this.game, pol = g.police, pp = this._pp;
    const lake = g.map.waterLevel(pp.x, pp.z) > WATER_Y + 1;
    // open water 110-200 m away, out of the player's view if possible
    for (let k = 0; k < 24; k++) {
      const a = Math.random() * Math.PI * 2, r = rand(lake ? 70 : 110, lake ? 140 : 200);
      const x = pp.x + Math.cos(a) * r, z = pp.z + Math.sin(a) * r;
      if (!this._deep(x, z, 3)) continue;
      if (k < 16 && g.peds?._inView?.(x, z)) continue;
      const b = g.vehicles.spawn('policeboat', x, z, Math.atan2(pp.x - x, pp.z - z), { persistent: true });
      const n = pol.level >= 2 ? 2 : 1;
      for (let q = 0; q < n; q++) { const cop = pol.spawnCop(x, z); b.putIn(cop, q); cop.homeCar = b; }
      b.ai = new BoatDriver(g, b, null, 0, 1);
      b.ai.ram = true;
      b.policeBoat = true;
      b.sirenOn = true;
      this.police.push(b);
      return b;
    }
    return null;
  }
}

// Drives a boat round a loop of points, or straight at a target (police: ramming speed). Keeps off the
// shallows by feeling ahead for the bottom.
export class BoatDriver {
  constructor(game, v, pts, idx, pace = 0.6) { this.game = game; this.v = v; this.pts = pts; this.idx = idx; this.pace = pace; this.target = null; this.ram = false; this.stuckT = 0; }
  update(dt) {
    const v = this.v, g = this.game, map = g.map;
    let tx, tz, want;
    if (this.target) {
      tx = this.target.x; tz = this.target.z;
      const d = Math.hypot(tx - v.pos.x, tz - v.pos.z);
      // lead the target a little, and slow when right on top of it
      want = d < 12 ? v.def.top * 0.5 : v.def.top * 0.95;
    } else if (this.pts) {
      const tp = this.pts[this.idx];
      tx = tp.x; tz = tp.z;
      if (Math.hypot(tx - v.pos.x, tz - v.pos.z) < 25) this.idx = (this.idx + 1) % this.pts.length;
      want = v.def.top * this.pace;
    } else { v.input.throttle = 0; v.input.brake = v.speed > 1 ? 0.5 : 0; v.input.steer = 0; return; }
    const [lx, lz] = v.worldToLocal(tx, tz);
    let ang = Math.atan2(lx, Math.max(0.5, lz));
    // shallows ahead: steer away (feel 25 m out, left and right)
    const feel = (a, dist) => { const yaw = v.yaw + a; const x = v.pos.x + Math.sin(yaw) * dist, z = v.pos.z + Math.cos(yaw) * dist; return map.waterLevel(x, z) - map.terrainHeight(x, z) > v.def.draft + 0.6; };
    if (!feel(0, 22)) { const l = feel(0.6, 20), r = feel(-0.6, 20); ang = l && !r ? 0.9 : r && !l ? -0.9 : (ang >= 0 ? 1 : -1); want *= 0.5; }
    // piers, the quay, the ship: look ahead and go round
    const col = g.collision;
    const probe = (a) => { const h = col.raycast(v.pos.x, v.pos.y + 0.6, v.pos.z, Math.sin(v.yaw + a), 0, Math.cos(v.yaw + a), 26, { ignoreProps: true }); return h ? h.t : 26; };
    const f0 = probe(0);
    if (f0 < 22) { const fl = probe(0.5), fr = probe(-0.5); ang = fl > fr ? 1 : -1; want *= f0 < 10 ? 0.3 : 0.6; }
    v.input.steer = clamp(ang * 1.6, -1, 1);
    const turn = Math.abs(ang);
    if (turn > 1.2) want *= 0.45;
    v.input.throttle = v.speed < want ? 1 : 0.2;
    v.input.brake = v.speed > want + 4 ? 0.4 : 0;
    // aground: back off
    if (!v.floating || (v.speedAbs < 0.8 && v.input.throttle > 0)) this.stuckT += dt; else this.stuckT = Math.max(0, this.stuckT - dt);
    if (this.stuckT > 2) { v.input.throttle = 0; v.input.brake = 1; v.input.steer = -v.input.steer; if (this.stuckT > 4.5) this.stuckT = 0; }
  }
}
