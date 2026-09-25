// Traffic: lane-following AI drivers on the road grid with traffic lights, turns, car following,
// pedestrian braking and honking. Also provides a reusable driving controller for police / missions.
import * as THREE from 'three';
import { XS, ZS, HALF_ROAD, LANES, CITY } from '../world/citymap.js';
import { signalState, intersectionPhase } from '../world/shaders.js';
import { TRAFFIC_POOL } from '../entities/vehicledefs.js';
import { U } from '../render/materials.js';
import { RNG, rand, randInt, pick, clamp, dist2, wrapAngle } from '../core/utils.js';

const DIRS = [[1, 0], [-1, 0], [0, 1], [0, -1]];
const NI = XS.length, NJ = ZS.length;

export function nearestIdx(arr, v) { let b = 0, bd = Infinity; for (let i = 0; i < arr.length; i++) { const d = Math.abs(arr[i] - v); if (d < bd) { bd = d; b = i; } } return b; }

export function segmentValid(i, j, di, dj) {
  const i2 = i + di, j2 = j + dj;
  return i >= 0 && j >= 0 && i < NI && j < NJ && i2 >= 0 && j2 >= 0 && i2 < NI && j2 < NJ;
}

function laneGeom(i, j, di, dj, lane) {
  const ax = XS[i], az = ZS[j], bx = XS[i + di], bz = ZS[j + dj];
  const rx = -dj, rz = di; // right of travel direction
  const off = LANES[lane];
  return {
    ax: ax + di * (HALF_ROAD + 0.5) + rx * off, az: az + dj * (HALF_ROAD + 0.5) + rz * off,
    bx: bx - di * (HALF_ROAD + 8) + rx * off, bz: bz - dj * (HALF_ROAD + 8) + rz * off,
    dx: di, dz: dj, len: Math.abs(bx - ax) + Math.abs(bz - az) - 2 * HALF_ROAD - 8.5,
  };
}

// Generic "follow a lane route" driver used by traffic and police patrols
export class LaneDriver {
  constructor(game, veh, seg) {
    this.game = game;
    this.veh = veh;
    this.seg = seg; // {i,j,di,dj,lane}
    this.geom = laneGeom(seg.i, seg.j, seg.di, seg.dj, seg.lane);
    this.mode = 'lane';
    this.turn = null;
    this.cruise = rand(11, 16.5);
    this.blockedTime = 0;
    this.honkTimer = 0;
    this.panic = 0;
    this.ignoreLights = false;
    this.stuck = 0;
    this.targetSpeed = 0;
  }

  _chooseNext() {
    const { i, j, di, dj, lane } = this.seg;
    const bi = i + di, bj = j + dj;
    const straight = [di, dj], right = [-dj, di], left = [dj, -di];
    let prefs = lane === 1 ? [straight, straight, right] : [straight, straight, left];
    if (Math.random() < 0.15) prefs = [left, right, straight];
    let opts = prefs.filter(([a, b]) => segmentValid(bi, bj, a, b));
    if (!opts.length) opts = [straight, right, left].filter(([a, b]) => segmentValid(bi, bj, a, b));
    if (!opts.length) opts = [[-di, -dj]];
    const [ndi, ndj] = pick(opts);
    let nlane = lane;
    if (ndi === right[0] && ndj === right[1]) nlane = 1;
    else if (ndi === left[0] && ndj === left[1]) nlane = 0;
    return { i: bi, j: bj, di: ndi, dj: ndj, lane: nlane };
  }

  _startTurn() {
    const next = this._chooseNext();
    const g0 = this.geom, g1 = laneGeom(next.i, next.j, next.di, next.dj, next.lane);
    const p0 = new THREE.Vector2(g0.bx, g0.bz), p2 = new THREE.Vector2(g1.ax, g1.az);
    let p1;
    if (g0.dx === g1.dx && g0.dz === g1.dz) p1 = p0.clone().add(p2).multiplyScalar(0.5);
    else if (g0.dx === -g1.dx && g0.dz === -g1.dz) { p1 = p0.clone().add(new THREE.Vector2(g0.dx * 8, g0.dz * 8)); }
    else { const t = (p2.x - p0.x) * g0.dx + (p2.y - p0.y) * g0.dz; p1 = new THREE.Vector2(p0.x + g0.dx * t, p0.y + g0.dz * t); }
    this.turn = { p0, p1, p2, next, g1, t: 0, straight: g0.dx === g1.dx && g0.dz === g1.dz };
    this.mode = 'turn';
  }

  _bez(t, out) {
    const { p0, p1, p2 } = this.turn;
    const u = 1 - t;
    return out.set(u * u * p0.x + 2 * u * t * p1.x + t * t * p2.x, u * u * p0.y + 2 * u * t * p1.y + t * t * p2.y);
  }

  // distance to the nearest obstacle ahead within the lane corridor
  _obstacleAhead(maxD) {
    const v = this.veh;
    const fx = Math.sin(v.yaw), fz = Math.cos(v.yaw);
    let best = maxD;
    const check = (x, z, rad) => {
      const dx = x - v.pos.x, dz = z - v.pos.z;
      const along = dx * fx + dz * fz;
      if (along < 0 || along > maxD + v.hz) return;
      const lat = Math.abs(dx * fz - dz * fx);
      const allowed = v.hx + rad + 0.25;
      if (lat < allowed) best = Math.min(best, along - v.hz - rad);
    };
    for (const o of this.game.vehicles.list) {
      if (o === v || o.removed) continue;
      if (Math.abs(o.pos.x - v.pos.x) > maxD + 8 || Math.abs(o.pos.z - v.pos.z) > maxD + 8) continue;
      check(o.pos.x, o.pos.z, Math.min(o.hx, o.hz));
    }
    const pl = this.game.player;
    if (!pl.vehicle) check(pl.pos.x, pl.pos.z, 0.5);
    if (this.game.peds) for (const p of this.game.peds.list) {
      if (p.vehicle || Math.abs(p.pos.x - v.pos.x) > maxD + 4 || Math.abs(p.pos.z - v.pos.z) > maxD + 4) continue;
      check(p.ragdolling ? p.ragdoll.pos[0] : p.pos.x, p.ragdolling ? p.ragdoll.pos[2] : p.pos.z, 0.4);
    }
    return best;
  }

  update(dt) {
    const v = this.veh;
    if (!v.driver || v.isWrecked) return;
    const inp = v.input;
    const speed = v.speed;
    const target = new THREE.Vector2();
    let desired = this.cruise * (this.panic > 0 ? 1.5 : 1);
    this.panic = Math.max(0, this.panic - dt);
    if (this.mode === 'lane') {
      const g = this.geom;
      // projection on the lane
      const px = v.pos.x - g.ax, pz = v.pos.z - g.az;
      const s = px * g.dx + pz * g.dz;
      const remain = g.len - s;
      const look = 6 + Math.abs(speed) * 0.45;
      const ts = Math.min(g.len, Math.max(0, s) + look);
      target.set(g.ax + g.dx * ts, g.az + g.dz * ts);
      // traffic light at the end
      const { i, j, di, dj } = this.seg;
      const bi = i + di, bj = j + dj;
      const axis = di !== 0 ? 1 : 0;
      const state = signalState(U.uTime.value + intersectionPhase(bi, bj), axis);
      if (!this.ignoreLights && this.panic <= 0 && state !== 'green' && remain > -0.5) {
        const stopDist = remain - 0.5;
        if (state === 'red' || stopDist > speed * 0.9) desired = Math.min(desired, Math.max(0, stopDist * 0.6));
      }
      if (remain < 2 + Math.max(0, speed) * 0.1) this._startTurn();
      // very far off the lane (pushed by collision): steer back
    }
    if (this.mode === 'turn') {
      const T = this.turn;
      // advance t by projecting position roughly
      const cur = this._bez(T.t, new THREE.Vector2());
      const ahead = this._bez(Math.min(1, T.t + 0.05), new THREE.Vector2());
      const tx = ahead.x - cur.x, tz = ahead.y - cur.y;
      const tl = Math.hypot(tx, tz) || 1;
      const prog = ((v.pos.x - cur.x) * tx + (v.pos.z - cur.y) * tz) / tl;
      const approxLen = T.straight ? 2 * HALF_ROAD + 6 : 18;
      T.t = clamp(T.t + Math.max(0, prog) / approxLen, 0, 1);
      this._bez(Math.min(1, T.t + (T.straight ? 0.35 : 0.22) + Math.abs(speed) * 0.01), target);
      if (!T.straight) desired = Math.min(desired, 7.5);
      if (T.t > 0.93) {
        this.seg = T.next; this.geom = T.g1; this.mode = 'lane'; this.turn = null;
      }
    }
    // obstacles
    const obs = this._obstacleAhead(22 + Math.max(0, speed));
    if (obs < 20 + speed) desired = Math.min(desired, Math.max(0, (obs - 2.5) * 0.9));
    if (obs < 2.5) desired = 0;
    // steering toward target
    const [lx, lz] = v.worldToLocal(target.x, target.y);
    const ang = Math.atan2(lx, Math.max(0.5, lz));
    inp.steer = clamp(ang / (v.def.steer * 0.8), -1, 1);
    inp.handbrake = false;
    // speed control
    const err = desired - speed;
    if (err > 0.5) { inp.throttle = clamp(err * 0.25, 0.15, 1); inp.brake = 0; }
    else if (err < -1) { inp.throttle = 0; inp.brake = speed > 0.5 ? clamp(-err * 0.2, 0.2, 1) : 0; }
    else { inp.throttle = desired > 0.5 ? 0.15 : 0; inp.brake = desired < 0.3 && speed > 0.3 ? 0.6 : 0; }
    if (desired < 0.3 && Math.abs(speed) < 0.5) { inp.throttle = 0; inp.brake = 0; inp.handbrake = true; }
    // blocked / honking
    if (obs < 4 && desired < 1) {
      this.blockedTime += dt;
      if (this.blockedTime > 2.5 && this.honkTimer <= 0 && this.game.player && dist2(v.pos.x, v.pos.z, this.game.player.pos.x, this.game.player.pos.z) < 30 * 30) {
        this.honkTimer = rand(1.5, 4);
        this.game.audio?.playAt('horn', v.pos, 0.7);
      }
    } else this.blockedTime = 0;
    // blocked for a while (double-parked car, wreck, player's car): change lanes to get around
    if (this.blockedTime > 5 && this.mode === 'lane') {
      this.blockedTime = 0;
      this.seg = { ...this.seg, lane: 1 - this.seg.lane };
      this.geom = laneGeom(this.seg.i, this.seg.j, this.seg.di, this.seg.dj, this.seg.lane);
    }
    this.honkTimer -= dt;
    // stuck detection (e.g. after collisions)
    if (Math.abs(speed) < 0.5 && desired > 3) this.stuck += dt; else this.stuck = 0;
    if (this.stuck > 3) { inp.brake = 1; inp.throttle = 0; inp.steer = -inp.steer; if (this.stuck > 5) this.stuck = 0; }
    // off-route recovery: if far from lane, re-snap segment
    if (this.mode === 'lane') {
      const g = this.geom;
      const px = v.pos.x - g.ax, pz = v.pos.z - g.az;
      const lat = Math.abs(px * g.dz - pz * g.dx);
      if (lat > 12) this.resnap();
    }
  }

  resnap() {
    const seg = nearestSegment(this.veh.pos.x, this.veh.pos.z, this.veh.yaw);
    if (seg) { this.seg = seg; this.geom = laneGeom(seg.i, seg.j, seg.di, seg.dj, seg.lane); this.mode = 'lane'; this.turn = null; }
  }
}

export function nearestSegment(x, z, yaw) {
  // find the road and direction best matching position & heading
  let bi = 0, bj = 0, bd = Infinity;
  for (let i = 0; i < NI; i++) { const d = Math.abs(x - XS[i]); if (d < bd) { bd = d; bi = i; } }
  let bdz = Infinity, bjj = 0;
  for (let j = 0; j < NJ; j++) { const d = Math.abs(z - ZS[j]); if (d < bdz) { bdz = d; bjj = j; } }
  const fx = Math.sin(yaw), fz = Math.cos(yaw);
  if (bd < bdz) {
    // on a N-S road (x = XS[bi])
    let j = 0; for (let k = 0; k < NJ - 1; k++) if (z >= ZS[k] && z <= ZS[k + 1]) j = k;
    const dj = fz >= 0 ? 1 : -1;
    const sj = dj > 0 ? j : j + 1;
    if (!segmentValid(bi, sj, 0, dj)) return null;
    const lane = Math.abs(Math.abs(x - XS[bi]) - LANES[0]) < Math.abs(Math.abs(x - XS[bi]) - LANES[1]) ? 0 : 1;
    return { i: bi, j: sj, di: 0, dj, lane };
  } else {
    let i = 0; for (let k = 0; k < NI - 1; k++) if (x >= XS[k] && x <= XS[k + 1]) i = k;
    const di = fx >= 0 ? 1 : -1;
    const si = di > 0 ? i : i + 1;
    if (!segmentValid(si, bjj, di, 0)) return null;
    const lane = Math.abs(Math.abs(z - ZS[bjj]) - LANES[0]) < Math.abs(Math.abs(z - ZS[bjj]) - LANES[1]) ? 0 : 1;
    return { i: si, j: bjj, di, dj: 0, lane };
  }
}

export class Traffic {
  constructor(game) {
    this.game = game;
    this.cars = [];
    this.maxCars = game.quality?.traffic ?? 26;
    this.spawnTimer = 0;
    game.events.on('gunshot', (s, pos) => { for (const c of this.cars) if (c.ai && dist2(c.pos.x, c.pos.z, pos.x, pos.z) < 40 * 40) { c.ai.panic = 8; c.ai.ignoreLights = true; } });
  }

  pickType(district) {
    const pool = TRAFFIC_POOL.slice();
    const mult = { downtown: { taxi: 4, zenith: 2, kestrel: 2 }, hood: { bouncer: 4, brawler: 2, hauler: 2 }, docks: { boxer: 5, parcel: 4, hauler: 3 }, hills: { zenith: 4, kestrel: 4, summit: 3 }, beach: { kestrel: 2, bouncer: 2 }, corona: { bouncer: 3, hauler: 2 } }[district] || {};
    return new RNG((Math.random() * 1e9) | 0).weighted(pool.map(([id, w]) => [id, w * (mult[id] || 1)]));
  }

  spawnCar(seg, s0, opts = {}) {
    const game = this.game;
    const g = laneGeom(seg.i, seg.j, seg.di, seg.dj, seg.lane);
    const x = g.ax + g.dx * s0, z = g.az + g.dz * s0;
    const yaw = Math.atan2(g.dx, g.dz);
    const type = opts.type || this.pickType(game.map.districtAt(x, z));
    const v = game.vehicles.spawn(type, x, z, yaw, opts);
    const driver = game.peds.spawnPed(x, z, { persistent: false });
    v.putIn(driver, 0);
    v.ai = new LaneDriver(game, v, seg);
    v.traffic = true;
    v.vel.set(Math.sin(yaw) * 8, 0, Math.cos(yaw) * 8);
    // passengers sometimes
    if (Math.random() < 0.15) { const pa = game.peds.spawnPed(x, z, {}); v.putIn(pa, 1); }
    this.cars.push(v);
    return v;
  }

  // Fill the streets immediately (used at game start / after teleports)
  populate(count = this.maxCars) {
    const saved = this._ignoreView;
    this._ignoreView = true;
    for (let k = 0; k < count && this.cars.filter((c) => !c.removed).length < this.maxCars; k++) this._trySpawn(35);
    this._ignoreView = saved;
  }

  _trySpawn(minDist = 70) {
    const game = this.game;
    const p = game.player.vehicle ? game.player.vehicle.pos : game.player.pos;
    const ci = nearestIdx(XS, p.x), cj = nearestIdx(ZS, p.z);
    for (let tries = 0; tries < 10; tries++) {
      const i = clamp(ci + randInt(-2, 2), 0, NI - 1), j = clamp(cj + randInt(-2, 2), 0, NJ - 1);
      const [di, dj] = pick(DIRS);
      if (!segmentValid(i, j, di, dj)) continue;
      const lane = Math.random() < 0.5 ? 0 : 1;
      const g = laneGeom(i, j, di, dj, lane);
      const s0 = rand(5, Math.max(6, g.len - 10));
      const x = g.ax + g.dx * s0, z = g.az + g.dz * s0;
      const d2 = dist2(x, z, p.x, p.z);
      if (d2 < minDist * minDist || d2 > 190 * 190) continue;
      if (!this._ignoreView && game.peds._inView(x, z) && d2 < 140 * 140) continue;
      let blocked = false;
      for (const v of game.vehicles.list) if (dist2(v.pos.x, v.pos.z, x, z) < 12 * 12) { blocked = true; break; }
      if (blocked) continue;
      this.spawnCar({ i, j, di, dj, lane }, s0);
      return;
    }
  }

  density() {
    const h = this.game.env.hours;
    return h < 5 || h > 23 ? 0.5 : h < 7 ? 0.7 : 1;
  }

  update(dt) {
    const game = this.game;
    const p = game.player.vehicle ? game.player.vehicle.pos : game.player.pos;
    this.spawnTimer -= dt;
    const alive = this.cars.filter((c) => !c.removed);
    this.cars = alive;
    if (this.spawnTimer <= 0 && alive.length < this.maxCars * this.density() && !game.disableAmbient) { this.spawnTimer = 0.3; this._trySpawn(); }
    for (const v of alive) {
      if (v.ai && v.driver && !v.driver.isPlayer && !v.driver.dead) v.ai.update(dt);
      else if (v.driver?.isPlayer || !v.driver) { v.ai = null; v.traffic = false; }
      const d2 = dist2(v.pos.x, v.pos.z, p.x, p.z);
      if ((d2 > 240 * 240 || (v.isWrecked && d2 > 100 * 100)) && !v.persistent && game.player.vehicle !== v) {
        this._despawn(v);
      } else if (v.ai && v.ai.stuck > 0 && v.ai.blockedTime > 25 && !game.peds._inView(v.pos.x, v.pos.z) && d2 > 50 * 50) this._despawn(v);
    }
  }

  _despawn(v) {
    for (const o of v.occupants) if (o && !o.isPlayer) this.game.peds.remove(o);
    v.occupants.fill(null);
    this.game.vehicles.remove(v);
  }
}

export { laneGeom };
