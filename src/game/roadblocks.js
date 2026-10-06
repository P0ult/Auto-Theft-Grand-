// Police roadblocks (GTA V): from three stars, while you're driving, the police throw a line of cruisers
// across the road a little way ahead of you, lights flashing, with officers taking cover behind them.
// Four stars brings the SWAT Enforcer and a stinger: a spike strip laid across the lanes in front of the
// block that bursts your tyres (you limp on at reduced speed with sparks until Los Soles Customs fixes
// it). Out in the country the Sheriff's SUVs set them up. They're cleared away once you're well past.
import * as THREE from 'three';
import { clamp, dist2, pick } from '../core/utils.js';

const MAX = 2;                  // roadblocks standing at once
const DIST = [90, 165];         // how far ahead (m), by speed
const SPIKE_AHEAD = 15;         // the strip lies this far in front of the cars

let _spikeGeo = null, _spikeMat = null;
function spikeMesh(len) {
  if (!_spikeGeo) {
    // one metre of stinger: a dark hinged base with two rows of silver spikes (stretched along x)
    const parts = [];
    const base = new THREE.BoxGeometry(1, 0.035, 0.42); base.translate(0, 0.018, 0);
    parts.push(base);
    for (let k = 0; k < 6; k++) for (const z of [-0.1, 0.1]) {
      const c = new THREE.ConeGeometry(0.025, 0.09, 5);
      c.translate(-0.42 + k * 0.17 + (z > 0 ? 0.08 : 0), 0.08, z);
      parts.push(c);
    }
    const pos = [], col = [];
    for (const [i, g] of parts.entries()) {
      const ng = g.toNonIndexed();
      const p = ng.attributes.position;
      for (let v = 0; v < p.count; v++) { pos.push(p.getX(v), p.getY(v), p.getZ(v)); if (i === 0) col.push(0.08, 0.08, 0.09); else col.push(0.75, 0.76, 0.78); }
    }
    _spikeGeo = new THREE.BufferGeometry();
    _spikeGeo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
    _spikeGeo.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
    _spikeGeo.computeVertexNormals();
    _spikeMat = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.4, metalness: 0.7 });
  }
  const g = new THREE.Group();
  const n = Math.max(1, Math.round(len));
  for (let i = 0; i < n; i++) { const m = new THREE.Mesh(_spikeGeo, _spikeMat); m.position.x = -len / 2 + (i + 0.5) * (len / n); m.scale.x = len / n; m.receiveShadow = true; g.add(m); }
  return g;
}

export class Roadblocks {
  constructor(game) {
    this.game = game;
    this.blocks = [];
    this.timer = 6;
    this.enabled = true;
    game.events.on('playerDied', () => { this.timer = 10; });
    game.events.on('busted', () => { this.timer = 10; });
  }

  // radar: the cars, flashing red / blue squares
  blipList() {
    const out = [], flash = Math.floor(this.game.time * 4) % 2;
    for (const b of this.blocks) for (const v of b.cars) if (!v.removed && !v.isWrecked) out.push({ x: v.pos.x, z: v.pos.z, color: flash ? 0x3355ff : 0xff3333, icon: 'dot', small: true, noEdge: true, square: true });
    return out;
  }

  update(dt) {
    const g = this.game, pol = g.police, p = g.player;
    const v = p.vehicle;
    // a new block ahead of a driving suspect
    this.timer -= dt;
    if (this.enabled && pol?.enabled && pol.level >= 3 && v && !v.def.kind && !p.dead && !g.missions?.noRoadblocks && this.blocks.length < MAX && this.timer <= 0) {
      this.timer = 3; // (retry soon if there was nowhere to put it)
      if (v.speedAbs > 11 && this._place(v)) this.timer = [0, 0, 0, 24, 17, 14][pol.level];
    }
    // spike strips, burst tyres
    for (const b of this.blocks) if (b.spike) this._stinger(b);
    for (const o of g.vehicles.list) {
      if (!o.flat || o.removed || o.isWrecked || o.speedAbs < 7) continue;
      if (Math.random() < dt * 6 && dist2(o.pos.x, o.pos.z, g.camera.position.x, g.camera.position.z) < 80 * 80) {
        const s = Math.random() < 0.5 ? 1 : -1, fx = Math.sin(o.yaw), fz = Math.cos(o.yaw);
        const z = (Math.random() < 0.5 ? 1 : -1) * o.def.wheelbase / 2, x = s * o.def.track / 2;
        g.effects.sparks(new THREE.Vector3(o.pos.x + fz * x + fx * z, o.pos.y + 0.05, o.pos.z - fx * x + fz * z), 3);
      }
    }
    // tidy up the ones left behind
    const pp = v ? v.pos : p.pos;
    for (let i = this.blocks.length - 1; i >= 0; i--) {
      const b = this.blocks[i];
      b.t += dt;
      b.cars = b.cars.filter((c) => !c.removed && !c.driver?.isPlayer);
      const d = Math.sqrt(dist2(b.x, b.z, pp.x, pp.z));
      const ahead = (pp.x - b.x) * b.tx + (pp.z - b.z) * b.tz; // > 0: you're past it
      const seen = g.peds._inView(b.x, b.z, 4);
      if (d > 300 || (b.t > 4 && ((ahead > 60 && d > 110) || (pol.level === 0 && d > 70)) && !seen) || b.t > 240 && !seen) this._remove(b, i);
    }
  }

  // find a stretch of road ahead of the car and close it
  _place(v) {
    const g = this.game, net = g.map.roads, pol = g.police;
    if (!net) return false;
    const sp = v.speedAbs;
    let hx = v.vel.x, hz = v.vel.z;
    const hl = Math.hypot(hx, hz) || 1; hx /= hl; hz /= hl;
    for (const dist of [clamp(sp * 5, DIST[0], DIST[1]), DIST[0] + 10, DIST[1] - 10]) {
      const px = v.pos.x + hx * dist, pz = v.pos.z + hz * dist;
      const c = net.closest(px, pz, (e) => !e.removed && e.type !== 'rail' && e.type !== 'ramp', 18, v.pos.y);
      if (!c || c.s < 16 || c.s > c.e.len - 16) continue;
      if (Math.abs(c.y - v.pos.y) > 10) continue;
      const at = net.at(c.e, c.s);
      let tx = at[3], tz = at[4];
      const dot = tx * hx + tz * hz;
      if (Math.abs(dot) < 0.8) continue; // a side road
      const e = c.e;
      // carriageway width each side of the centre line (u = (-tz, tx) is the forward-lane side)
      const T = e.T;
      const wU = Math.min(e.wL ?? 4, (T?.off0 ?? 0) + (T?.laneW ?? 3.4) * Math.max(1, e.lanesF) + 0.4);
      const wD = Math.min(e.wR ?? 4, (T?.off0 ?? 0) + (T?.laneW ?? 3.4) * Math.max(1, e.lanesB) + 0.4);
      const ux = -tz, uz = tx;
      if (dot < 0) { tx = -tx; tz = -tz; } // facing the way the suspect is coming
      const cx = at[0], cz = at[2], y = c.y;
      if (this.blocks.some((b) => dist2(b.x, b.z, cx, cz) < 140 * 140)) return false;
      if (g.peds._inView(cx, cz, 2) && Math.hypot(cx - v.pos.x, cz - v.pos.z) < 95) continue;
      const W = wU + wD;
      const n = clamp(Math.round(W / 4.9), 2, 5);
      const step = W / n;
      // clear the traffic out of the way
      for (const o of [...g.vehicles.list]) {
        if (o.persistent || o.driver?.isPlayer || o.removed) continue;
        if (dist2(o.pos.x, o.pos.z, cx, cz) < 16 * 16 && o.traffic && g.traffic) g.traffic._despawn(o);
      }
      const rural = ['country', 'desert', 'forest'].includes(g.map.districtAt(cx, cz));
      const b = { x: cx, z: cz, y, tx, tz, ux, uz, wU, wD, cars: [], cops: [], t: 0, spike: null };
      for (let i = 0; i < n; i++) {
        const lat = -wD + (i + 0.5) * step;
        const x = cx + ux * lat, z = cz + uz * lat;
        const type = pol.level >= 4 && i === Math.floor(n / 2) ? 'enforcer' : rural ? 'sheriff' : 'police';
        const yawRoad = Math.atan2(tx, tz);
        const yaw = yawRoad + (i % 2 ? 1 : -1) * 1.2;
        const car = g.vehicles.spawn(type, x, z, yaw, { persistent: true, parked: true, y });
        car.sirenOn = true; car.sirenMute = true; car.roadblock = true;
        b.cars.push(car);
        // officers in cover behind it
        const nc = n <= 2 ? 2 : i % 2 ? 1 : 2;
        for (let k = 0; k < nc; k++) {
          const hx2 = x + tx * 3.4 + ux * (k ? 1.1 : -1.1), hz2 = z + tz * 3.4 + uz * (k ? 1.1 : -1.1);
          const cop = pol.spawnCop(hx2, hz2, { y: g.collision.floorHeight(hx2, hz2, y + 1.5) });
          if (type === 'enforcer' && cop.weapon !== 'rifle') { cop.giveWeapon('rifle', 999); cop.equip('rifle'); }
          cop.holdPos = { x: hx2, z: hz2 };
          cop.setYaw?.(yawRoad + Math.PI);
          b.cops.push(cop);
        }
      }
      // the stinger, always at four stars, sometimes at three
      if (pol.level >= 4 || Math.random() < 0.45) {
        const sx = cx - tx * SPIKE_AHEAD, sz = cz - tz * SPIKE_AHEAD;
        const m = spikeMesh(W);
        const mid = (wU - wD) / 2;
        m.position.set(sx + ux * mid, g.collision.surfaceHeight?.(sx, sz, y + 0.6) ?? y, sz + uz * mid);
        m.rotation.y = Math.atan2(ux, uz) - Math.PI / 2;
        g.scene.add(m);
        b.spike = { x: sx + ux * mid, z: sz + uz * mid, half: W / 2, mesh: m };
      }
      this.blocks.push(b);
      g.hud?.dispatch?.(pick(['All units, roadblock in position. Suspect inbound.', 'Roadblock set. Spike strip deployed.', 'Units in position, we have the road closed.']));
      return true;
    }
    return false;
  }

  // anything rolling over the strip loses its tyres (not the police: they know where it is)
  _stinger(b) {
    const g = this.game, s = b.spike;
    for (const o of g.vehicles.list) {
      if (o.flat || o.removed || o.def.kind || o.def.police || o.speedAbs < 2) continue;
      const rx = o.pos.x - s.x, rz = o.pos.z - s.z;
      if (rx * rx + rz * rz > 30 * 30) continue;
      const along = rx * b.tx + rz * b.tz, lat = rx * b.ux + rz * b.uz;
      if (Math.abs(along) < o.def.L / 2 && Math.abs(lat) < s.half + 0.4 && Math.abs(o.pos.y - s.mesh.position.y) < 2) {
        o.flat = true;
        g.audio?.playAt?.('pop', o.pos, 1);
        if (o.driver?.isPlayer) { g.audio?.play('pop', 0.6); g.hud?.help('Spike strip! Your tyres are shredded: get them fixed at <b>Los Soles Customs</b>.', 4); }
      }
    }
  }

  _remove(b, i) {
    const g = this.game;
    for (const c of b.cars) if (!c.removed && !c.driver?.isPlayer) { for (const o of c.occupants) if (o && !o.isPlayer) g.peds.remove(o); c.occupants.fill(null); g.vehicles.remove(c); }
    for (const c of b.cops) if (!c.removed && !c.vehicle) g.peds.remove(c);
    if (b.spike) b.spike.mesh.parent?.remove(b.spike.mesh);
    this.blocks.splice(i, 1);
  }

  reset() {
    for (let i = this.blocks.length - 1; i >= 0; i--) this._remove(this.blocks[i], i);
    this.timer = 6;
  }
}
