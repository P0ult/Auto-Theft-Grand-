// Traffic: lane-following AI on the road network (city grid, freeway, ramps, highways, town roads,
// roundabouts). Obeys the grid's traffic lights, yields at junctions / roundabouts / merges, slows
// for curves, follows cars, brakes for pedestrians and honks. The LaneDriver is also the base for
// police pursuit and mission drivers.
import * as THREE from 'three';
import { XS, ZS, HALF_ROAD, LANES, CITY } from '../world/citymap.js';
import { REMOVED_SEGMENTS } from '../world/roadlayout.js';
import { signalState } from '../world/shaders.js';
import { TRAFFIC_POOL } from '../entities/vehicledefs.js';
import { U } from '../render/materials.js';
import { RNG, rand, randInt, pick, clamp, dist2, wrapAngle } from '../core/utils.js';

const NI = XS.length, NJ = ZS.length;

export function nearestIdx(arr, v) { let b = 0, bd = Infinity; for (let i = 0; i < arr.length; i++) { const d = Math.abs(arr[i] - v); if (d < bd) { bd = d; b = i; } } return b; }

// ---- legacy grid helpers (used to place mission cars on city lanes)
export function segmentValid(i, j, di, dj) {
  const i2 = i + di, j2 = j + dj;
  if (!(i >= 0 && j >= 0 && i < NI && j < NJ && i2 >= 0 && j2 >= 0 && i2 < NI && j2 < NJ)) return false;
  const key = di !== 0 ? `h:${Math.min(i, i2)},${j}` : `v:${i},${Math.min(j, j2)}`;
  return !REMOVED_SEGMENTS.has(key);
}
export function laneGeom(i, j, di, dj, lane) {
  const ax = XS[i], az = ZS[j], bx = XS[i + di], bz = ZS[j + dj];
  const rx = -dj, rz = di;
  const off = LANES[lane];
  return {
    ax: ax + di * (HALF_ROAD + 0.5) + rx * off, az: az + dj * (HALF_ROAD + 0.5) + rz * off,
    bx: bx - di * (HALF_ROAD + 8) + rx * off, bz: bz - dj * (HALF_ROAD + 8) + rz * off,
    dx: di, dz: dj, len: Math.abs(bx - ax) + Math.abs(bz - az) - 2 * HALF_ROAD - 8.5,
  };
}

// ---- paths
function mkPath(pts, extra = {}) {
  const cum = [0];
  for (let i = 1; i < pts.length; i++) cum.push(cum[i - 1] + Math.hypot(pts[i][0] - pts[i - 1][0], pts[i][2] - pts[i - 1][2]));
  const len = cum[cum.length - 1];
  // curve speed limits per point from the turning angle over ~10 m
  const vmax = new Float32Array(pts.length).fill(99);
  for (let i = 1; i < pts.length - 1; i++) {
    const a = pts[i - 1], b = pts[i], c = pts[i + 1];
    const h1 = Math.atan2(b[0] - a[0], b[2] - a[2]), h2 = Math.atan2(c[0] - b[0], c[2] - b[2]);
    const dAng = Math.abs(wrapAngle(h2 - h1));
    const ds = (cum[i + 1] - cum[i - 1]) / 2;
    if (dAng > 1e-3 && ds > 0.01) { const R = ds / dAng; vmax[i] = Math.sqrt(5.2 * R); }
  }
  return { pts, cum, len, vmax, ...extra };
}
function sampleOn(path, s, out) {
  const { pts, cum } = path;
  if (s <= 0) { out[0] = pts[0][0]; out[1] = pts[0][1]; out[2] = pts[0][2]; return out; }
  if (s >= path.len) { const p = pts[pts.length - 1]; out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; return out; }
  let lo = 0, hi = cum.length - 1;
  while (hi - lo > 1) { const m = (lo + hi) >> 1; if (cum[m] <= s) lo = m; else hi = m; }
  const t = (s - cum[lo]) / (cum[hi] - cum[lo] || 1);
  const a = pts[lo], b = pts[hi];
  out[0] = a[0] + (b[0] - a[0]) * t; out[1] = a[1] + (b[1] - a[1]) * t; out[2] = a[2] + (b[2] - a[2]) * t;
  return out;
}

// closest lane to a position (optionally matching a heading)
export function nearestLane(net, x, z, yaw = null, filter = null, y = null) {
  const c = net.closest(x, z, (e) => !e.removed && (e.lanesF > 0 || e.lanesB > 0) && (!filter || filter(e)), 300, y);
  if (!c) return null;
  const e = c.e;
  const t = net.at(e, c.s);
  const eyaw = Math.atan2(t[3], t[4]);
  let dir;
  if (e.lanesB === 0) dir = 0; else if (e.lanesF === 0) dir = 1;
  else if (yaw != null) dir = Math.abs(wrapAngle(yaw - eyaw)) < Math.PI / 2 ? 0 : 1;
  else dir = c.lat >= 0 ? 0 : 1;
  const n = dir === 0 ? e.lanesF : e.lanesB;
  // lateral offset (right of travel) -> lane index
  const latTravel = dir === 0 ? c.lat : -c.lat;
  let lane = 0, bd = Infinity;
  for (let k = 0; k < n; k++) { const d = Math.abs(net.laneOffset(e, dir, k) - latTravel); if (d < bd) { bd = d; lane = k; } }
  return { e, dir, lane, s: dir === 0 ? c.s : e.len - c.s };
}

// Generic lane-following driver
export class LaneDriver {
  constructor(game, veh, start = null) {
    this.game = game;
    this.veh = veh;
    this.net = game.map.roads;
    this.cruiseFactor = rand(0.82, 1.05);
    this.cruise = 14;
    this.fixedCruise = false;
    this.paths = [];
    this.s = 0;
    this.blockedTime = 0;
    this.honkTimer = 0;
    this.panic = 0;
    this.ignoreLights = false;
    this.stuck = 0;
    this.impatient = 0;
    this.wait = 0;
    if (start) this._start(start); else if (start !== false) this.resnap();
  }

  // --- compatibility with the old grid driver (missions read .seg / .geom)
  get seg() { return this.paths[0] || null; }

  _start(st) {
    this.paths = [];
    const p = this._lanePath(st.e, st.dir, st.lane);
    this.paths.push(p);
    this.s = this._project(p, this.veh.pos.x, this.veh.pos.z, 0, p.len);
    this._ensure();
  }

  _lanePath(e, dir, lane) {
    const pts = this.net.lanePath(e, dir, lane);
    const endNode = this.net.nodes[dir === 0 ? e.b : e.a];
    return mkPath(pts, { kind: 'lane', e, dir, lane, node: endNode, speed: e.speed });
  }

  // exits available at a lane's end node
  _options(cur) {
    const n = cur.node;
    const ex = this.net.exits(n).filter((o) => !(o.e === cur.e && o.dir !== cur.dir) && !o.e.removed);
    if (!ex.length) return this.net.exits(n).filter((o) => !o.e.removed); // dead end: U-turn
    const rightmost = cur.lane >= (cur.dir === 0 ? cur.e.lanesF : cur.e.lanesB) - 1;
    // at freeway splits only the right lane may take the exit
    if (n.kind === 'split') {
      const main = ex.filter((o) => o.e.type === cur.e.type);
      if (!rightmost && main.length) return main;
    }
    return ex;
  }
  _heading(e, dir, atStart) {
    const t = this.net.at(e, atStart ? (dir === 0 ? 0.5 : e.len - 0.5) : (dir === 0 ? e.len - 0.5 : 0.5));
    return dir === 0 ? Math.atan2(t[3], t[4]) : Math.atan2(-t[3], -t[4]);
  }
  _chooseNext(cur) {
    const ex = this._options(cur);
    if (!ex.length) return null;
    const hIn = this._heading(cur.e, cur.dir, false);
    // prefer going straight; turning and exits less often
    const w = ex.map((o) => {
      const d = Math.abs(wrapAngle(this._heading(o.e, o.dir, true) - hIn));
      let k = d < 0.5 ? 3 : d < 2.2 ? 1 : 0.15;
      if (o.e.type === 'dirt') k *= 0.3;
      if (cur.e.type === 'freeway' && o.e.type === 'ramp') k = 0.9;
      return k;
    });
    let r = Math.random() * w.reduce((a, b) => a + b, 0);
    for (let i = 0; i < ex.length; i++) { r -= w[i]; if (r <= 0) return ex[i]; }
    return ex[0];
  }
  _laneFor(cur, o) {
    const n = o.dir === 0 ? o.e.lanesF : o.e.lanesB;
    if (cur.node.kind === 'merge' || cur.e.type === 'ramp') return n - 1;
    if (o.e.type === 'ramp') return 0;
    return clamp(cur.lane, 0, n - 1);
  }

  _turnPath(from, to, node) {
    const A = from.pts[from.pts.length - 1], B = to.pts[0];
    const a2 = from.pts[Math.max(0, from.pts.length - 2)], b2 = to.pts[Math.min(1, to.pts.length - 1)];
    let hax = A[0] - a2[0], haz = A[2] - a2[2]; const la = Math.hypot(hax, haz) || 1; hax /= la; haz /= la;
    let hbx = b2[0] - B[0], hbz = b2[2] - B[2]; const lb = Math.hypot(hbx, hbz) || 1; hbx /= lb; hbz /= lb;
    const d = Math.hypot(B[0] - A[0], B[2] - A[2]);
    let pts;
    if (d < 0.6) pts = [A, B];
    else if (node.kind === 'rb') pts = this._rbPath(A, B, node);
    else if (from.e === to.e) {
      // U-turn at a dead end: loop out to the side
      const rx = -haz, rz = hax;
      const w = Math.max(6, d);
      pts = [];
      for (let k = 0; k <= 10; k++) {
        const t = k / 10, ang = t * Math.PI;
        const cx = (A[0] + B[0]) / 2 + hax * 4, cz = (A[2] + B[2]) / 2 + haz * 4;
        pts.push([cx - rx * Math.cos(ang) * w / 2 * -1 + hax * Math.sin(ang) * 4, A[1], cz - rz * Math.cos(ang) * w / 2 * -1 + haz * Math.sin(ang) * 4]);
      }
      pts.unshift(A); pts.push(B);
    } else {
      const k = clamp(d * 0.42, 1, 40);
      const P1 = [A[0] + hax * k, A[2] + haz * k], P2 = [B[0] - hbx * k, B[2] - hbz * k];
      pts = [];
      const n = Math.max(3, Math.min(16, Math.ceil(d / 3)));
      for (let i = 0; i <= n; i++) {
        const t = i / n, u = 1 - t;
        const x = u * u * u * A[0] + 3 * u * u * t * P1[0] + 3 * u * t * t * P2[0] + t * t * t * B[0];
        const z = u * u * u * A[2] + 3 * u * u * t * P1[1] + 3 * u * t * t * P2[1] + t * t * t * B[2];
        pts.push([x, A[1] + (B[1] - A[1]) * t, z]);
      }
    }
    return mkPath(pts, { kind: 'turn', node, from, to });
  }
  _rbPath(A, B, n) {
    const R = n.rbR;
    const ta = Math.atan2(A[2] - n.z, A[0] - n.x), tb = Math.atan2(B[2] - n.z, B[0] - n.x);
    // right-hand traffic circulates with decreasing angle (counter-clockwise on the map)
    const a0 = ta - 0.45;
    let a1 = tb + 0.45;
    while (a1 > a0) a1 -= Math.PI * 2;
    while (a0 - a1 > Math.PI * 2) a1 += Math.PI * 2;
    const pts = [A];
    const steps = Math.max(4, Math.ceil((a0 - a1) / 0.25));
    for (let i = 0; i <= steps; i++) { const a = a0 + (a1 - a0) * i / steps; pts.push([n.x + Math.cos(a) * R, n.y + 0.05, n.z + Math.sin(a) * R]); }
    pts.push(B);
    return pts;
  }

  // keep a few paths queued ahead
  _ensure() {
    let guard = 0;
    while (this.paths.length < 4 && guard++ < 6) {
      const last = this.paths[this.paths.length - 1];
      if (last.kind !== 'lane') break;
      const o = this._chooseNext(last);
      if (!o) break;
      const lane = this._laneFor(last, o);
      const nxt = this._lanePath(o.e, o.dir, lane);
      this.paths.push(this._turnPath(last, nxt, last.node), nxt);
    }
  }

  _project(path, x, z, s0, s1) {
    const { pts, cum } = path;
    let best = Infinity, bs = s0;
    let i0 = 0, i1 = pts.length - 1;
    while (i0 < pts.length - 1 && cum[i0 + 1] < s0) i0++;
    while (i1 > 0 && cum[i1 - 1] > s1) i1--;
    for (let i = i0; i < i1; i++) {
      const a = pts[i], b = pts[i + 1];
      const dx = b[0] - a[0], dz = b[2] - a[2], L2 = dx * dx + dz * dz || 1e-6;
      const t = clamp(((x - a[0]) * dx + (z - a[2]) * dz) / L2, 0, 1);
      const px = a[0] + dx * t, pz = a[2] + dz * t;
      const d = (x - px) ** 2 + (z - pz) ** 2;
      if (d < best) { best = d; bs = cum[i] + Math.sqrt(L2) * t; }
    }
    this._lat = Math.sqrt(best);
    return bs;
  }

  // point `ahead` meters beyond the current progress (walks into queued paths)
  _pointAhead(ahead, out) {
    let s = this.s + ahead, k = 0;
    while (k < this.paths.length - 1 && s > this.paths[k].len) { s -= this.paths[k].len; k++; }
    return sampleOn(this.paths[k], s, out);
  }
  // lowest curve speed within the next `dist` meters
  _curveLimit(dist) {
    let lim = 99, s = this.s, k = 0, acc = 0;
    while (k < this.paths.length && acc < dist) {
      const p = this.paths[k];
      for (let i = 0; i < p.pts.length; i++) {
        if (p.cum[i] < s) continue;
        const d = acc + p.cum[i] - s;
        if (d > dist) break;
        // allowed now so we can brake to vmax by then (decel ~4 m/s^2)
        lim = Math.min(lim, Math.sqrt(p.vmax[i] * p.vmax[i] + 2 * 4 * d));
      }
      acc += p.len - s; s = 0; k++;
    }
    return lim;
  }

  // distance to the nearest obstacle ahead within the lane corridor
  _obstacleAhead(maxD) {
    const v = this.veh;
    const fx = Math.sin(v.yaw), fz = Math.cos(v.yaw);
    let best = maxD;
    const check = (x, z, y, rad) => {
      if (Math.abs(y - v.pos.y) > 3.5) return;
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
      check(o.pos.x, o.pos.z, o.pos.y, Math.min(o.hx, o.hz));
    }
    const pl = this.game.player;
    if (!pl.vehicle) check(pl.pos.x, pl.pos.z, pl.pos.y, 0.5);
    if (this.game.peds && !(this.impatient > 0)) for (const p of this.game.peds.list) {
      if (p.vehicle || p.dead || p.ragdolling) continue;
      if (Math.abs(p.pos.x - v.pos.x) > maxD + 4 || Math.abs(p.pos.z - v.pos.z) > maxD + 4) continue;
      check(p.pos.x, p.pos.z, p.pos.y, 0.4);
    }
    return best;
  }

  // may we enter the junction at the end of the current lane? returns a speed cap (0 = stop)
  _junctionControl(cur, remain, speed) {
    const n = cur.node;
    const game = this.game;
    if (this.ignoreLights || this.panic > 0) return 99;
    if (n.sig != null) {
      // axis of approach: grid streets know it, others use their heading
      let axis;
      if (cur.e.grid) axis = cur.e.grid.di !== 0 ? 1 : 0;
      else { const h = this._heading(cur.e, cur.dir, false); axis = Math.abs(Math.sin(h)) > Math.abs(Math.cos(h)) ? 1 : 0; }
      const state = signalState(U.uTime.value + n.sig, axis);
      if (state !== 'green' && remain > -0.5) {
        const stopDist = remain - 0.5;
        if (state === 'red' || stopDist > speed * 0.9) return Math.max(0, stopDist * 0.6);
      }
      return 99;
    }
    if (remain > 30) return 99;
    const vlist = game.vehicles.list;
    if (n.kind === 'rb') {
      // give way to traffic already circulating and coming towards our entry
      const ta = Math.atan2(cur.pts[cur.pts.length - 1][2] - n.z, cur.pts[cur.pts.length - 1][0] - n.x);
      for (const o of vlist) {
        if (o === this.veh || o.removed || o.speedAbs < 0.5) continue;
        const d = Math.hypot(o.pos.x - n.x, o.pos.z - n.z);
        if (d < n.rbR - 4 || d > n.rbR + 4) continue;
        let da = Math.atan2(o.pos.z - n.z, o.pos.x - n.x) - ta;
        while (da < 0) da += Math.PI * 2;
        while (da > Math.PI * 2) da -= Math.PI * 2;
        if (da < 1.5) return Math.max(0, (remain - 0.5) * 0.6);
      }
      return 11;
    }
    if (n.kind === 'merge' || cur.e.type === 'ramp' && n.kind !== 'x') {
      // merging: slow if something is right alongside in the target lane
      for (const o of vlist) {
        if (o === this.veh || o.removed) continue;
        const d = Math.hypot(o.pos.x - n.x, o.pos.z - n.z);
        if (d < 30 && o.speedAbs > 3 && Math.hypot(o.pos.x - this.veh.pos.x, o.pos.z - this.veh.pos.z) < 22) return Math.max(6, speed * 0.7);
      }
      return 99;
    }
    if (n.kind === 'x' && !n.grid) {
      const minor = cur.e.T.cls < n.maxCls;
      // someone else holds the junction?
      const busy = n.busy && n.busy !== this.veh && !n.busy.removed && Math.hypot(n.busy.pos.x - n.x, n.busy.pos.z - n.z) < n.r + 8;
      if (busy) return Math.max(0, (remain - 0.5) * 0.6);
      if (minor) {
        // stop, then go when nothing is crossing
        let clear = true;
        for (const o of vlist) {
          if (o === this.veh || o.removed || o.speedAbs < 1) continue;
          if (Math.hypot(o.pos.x - n.x, o.pos.z - n.z) < n.r + 26) { clear = false; break; }
        }
        if (!clear || (remain > 1.2 && this.wait < 0.6)) return Math.max(0, (remain - 0.4) * 0.5);
      }
      return 9;
    }
    return 99;
  }

  update(dt) {
    const v = this.veh;
    if (!v.driver || v.isWrecked || !this.paths.length) return;
    const inp = v.input;
    const speed = v.speed;
    this.panic = Math.max(0, this.panic - dt);
    // progress
    let cur = this.paths[0];
    this.s = this._project(cur, v.pos.x, v.pos.z, this.s - 4, this.s + 30);
    while (this.s >= cur.len - 0.05 && this.paths.length > 1) {
      if (cur.kind === 'turn' && cur.node.busy === v) cur.node.busy = null;
      this.s -= cur.len;
      this.paths.shift();
      cur = this.paths[0];
      if (cur.kind === 'turn' && cur.node.kind === 'x' && !cur.node.grid) cur.node.busy = v;
      this._ensure();
      this.s = Math.max(0, this.s);
    }
    const lane = cur.kind === 'lane' ? cur : cur.to;
    const baseSpeed = this.fixedCruise ? this.cruise : (lane?.speed ?? 14) * this.cruiseFactor;
    let desired = baseSpeed * (this.panic > 0 ? 1.4 : 1);
    // curves
    desired = Math.min(desired, this._curveLimit(28 + Math.max(0, speed) * 2.2));
    // junction control at the end of our lane
    if (cur.kind === 'lane') {
      const remain = cur.len - this.s;
      const cap = this._junctionControl(cur, remain, speed);
      desired = Math.min(desired, cap);
      if (cap < 0.5 && Math.abs(speed) < 0.6) this.wait += dt; else if (cap > 5) this.wait = 0;
    }
    // obstacles
    const obs = this._obstacleAhead(22 + Math.max(0, speed) * 1.2);
    if (obs < 20 + speed) desired = Math.min(desired, Math.max(0, (obs - 2.5) * 0.9));
    if (obs < 2.5) desired = 0;
    // steering toward a point ahead on the path
    const look = 5 + Math.abs(speed) * 0.45;
    const T = this._pointAhead(look, _t3);
    const [lx, lz] = v.worldToLocal(T[0], T[2]);
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
    this.impatient = Math.max(0, this.impatient - dt);
    if (this.blockedTime > 9) { this.impatient = 3; this.blockedTime = 0; }
    // blocked for a while on a multi-lane road: switch lanes to get around
    if (this.blockedTime > 5 && cur.kind === 'lane') {
      const n = cur.dir === 0 ? cur.e.lanesF : cur.e.lanesB;
      if (n > 1) { this.blockedTime = 0; this._start({ e: cur.e, dir: cur.dir, lane: (cur.lane + 1) % n }); }
    }
    this.honkTimer -= dt;
    // stuck detection (e.g. after collisions)
    if (Math.abs(speed) < 0.5 && desired > 3) this.stuck += dt; else this.stuck = 0;
    if (this.stuck > 3) { inp.brake = 1; inp.throttle = 0; inp.steer = -inp.steer; if (this.stuck > 5) this.stuck = 0; }
    // pushed far off the route, or knocked off a viaduct / ramp onto the ground below: find the nearest lane again
    if (Math.abs(T[1] - v.pos.y) > 4 && !v.airborne) this.offLevel = (this.offLevel || 0) + dt; else this.offLevel = 0;
    if (this._lat > 14 || this.offLevel > 1.2) { this.offLevel = 0; this.resnap(); }
  }

  resnap() {
    const v = this.veh;
    const st = nearestLane(this.net, v.pos.x, v.pos.z, v.yaw, null, v.pos.y);
    if (st) this._start(st);
  }
}
const _t3 = [0, 0, 0];

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
    const mult = { downtown: { taxi: 4, zenith: 2, kestrel: 2 }, hood: { bouncer: 4, brawler: 2, hauler: 2 }, docks: { boxer: 5, parcel: 4, hauler: 3 }, hills: { zenith: 4, kestrel: 4, summit: 3 }, beach: { kestrel: 2, bouncer: 2 }, corona: { bouncer: 3, hauler: 2 }, country: { hauler: 6, summit: 3, boxer: 2, taxi: 0.1 }, desert: { hauler: 5, summit: 3, brawler: 2, taxi: 0.1 }, forest: { summit: 5, hauler: 4, taxi: 0.1 } }[district] || {};
    return new RNG((Math.random() * 1e9) | 0).weighted(pool.map(([id, w]) => [id, w * (mult[id] ?? 1)]));
  }

  // start: {e, dir, lane} + s0 (distance along the lane path)
  spawnCar(start, s0, opts = {}) {
    const game = this.game;
    const net = game.map.roads;
    const pts = net.lanePath(start.e, start.dir, start.lane);
    const p = mkPath(pts);
    const P = sampleOn(p, s0, [0, 0, 0]), Q = sampleOn(p, s0 + 2, [0, 0, 0]);
    const yaw = Math.atan2(Q[0] - P[0], Q[2] - P[2]);
    const type = opts.type || this.pickType(game.map.districtAt(P[0], P[2]));
    const v = game.vehicles.spawn(type, P[0], P[2], yaw, opts);
    const driver = game.peds.spawnPed(P[0], P[2], { persistent: false });
    v.putIn(driver, 0);
    v.ai = new LaneDriver(game, v, start);
    v.traffic = true;
    const sp = Math.min(start.e.speed * 0.8, 20);
    v.vel.set(Math.sin(yaw) * sp, 0, Math.cos(yaw) * sp);
    if (Math.random() < 0.15) { const pa = game.peds.spawnPed(P[0], P[2], {}); v.putIn(pa, 1); }
    this.cars.push(v);
    return v;
  }

  populate(count = this.maxCars) {
    const saved = this._ignoreView;
    this._ignoreView = true;
    for (let k = 0; k < count * 2 && this.cars.filter((c) => !c.removed).length < Math.min(count, this.maxCars); k++) this._trySpawn(35);
    this._ignoreView = saved;
  }

  // pick a random lane position around a point, between rMin and rMax meters away
  static sampleLane(game, cx, cz, rMin, rMax, filter = null) {
    const net = game.map.roads;
    for (let tries = 0; tries < 12; tries++) {
      const a = Math.random() * Math.PI * 2, r = rand(rMin, rMax);
      const x = cx + Math.cos(a) * r, z = cz + Math.sin(a) * r;
      const c = net.closest(x, z, (e) => !e.removed && e.type !== 'dirt' && (!filter || filter(e)), 90);
      if (!c) continue;
      const e = c.e;
      const dirs = [];
      if (e.lanesF > 0) dirs.push(0);
      if (e.lanesB > 0) dirs.push(1);
      if (!dirs.length) continue;
      const dir = pick(dirs);
      const lane = randInt(0, (dir === 0 ? e.lanesF : e.lanesB) - 1);
      const pts = net.lanePath(e, dir, lane);
      if (pts.length < 2) continue;
      const p = mkPath(pts);
      if (p.len < 8) continue;
      const s0 = rand(3, Math.max(4, p.len - 6));
      const P = sampleOn(p, s0, [0, 0, 0]);
      const d = Math.hypot(P[0] - cx, P[2] - cz);
      if (d < rMin * 0.8 || d > rMax * 1.2) continue;
      return { start: { e, dir, lane }, s0, x: P[0], y: P[1], z: P[2] };
    }
    return null;
  }

  _trySpawn(minDist = 70) {
    const game = this.game;
    const pv = game.player.vehicle;
    const p = pv ? pv.pos : game.player.pos;
    // spawn further out (and ahead) when moving fast
    const fast = pv ? clamp(pv.speedAbs / 30, 0, 1) : 0;
    let cx = p.x, cz = p.z;
    if (pv && fast > 0.3) { cx += pv.vel.x * 3; cz += pv.vel.z * 3; }
    const rMax = 190 + fast * 110;
    const smp = Traffic.sampleLane(game, cx, cz, minDist, rMax);
    if (!smp) return;
    const d2 = dist2(smp.x, smp.z, p.x, p.z);
    if (d2 < minDist * minDist || d2 > (rMax + 40) ** 2) return;
    if (!this._ignoreView && game.peds._inView(smp.x, smp.z) && d2 < 140 * 140) return;
    for (const v of game.vehicles.list) if (dist2(v.pos.x, v.pos.z, smp.x, smp.z) < 12 * 12 && Math.abs(v.pos.y - smp.y) < 4) return;
    // quieter out in the country
    const rural = !game.map.isOnCityStreet(smp.x, smp.z, 4) && smp.start.e.type !== 'freeway';
    if (rural && Math.random() < 0.45) return;
    this.spawnCar(smp.start, smp.s0);
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
    const airborne = game.player.vehicle?.def.aircraft && game.player.vehicle.altitude > 40;
    if (this.spawnTimer <= 0 && alive.length < this.maxCars * this.density() && !game.disableAmbient && !airborne) { this.spawnTimer = 0.3; this._trySpawn(); }
    for (const v of alive) {
      if (v.ai && v.driver && !v.driver.isPlayer && !v.driver.dead) v.ai.update(dt);
      else if (v.driver?.isPlayer || !v.driver) { v.ai = null; v.traffic = false; }
      const d2 = dist2(v.pos.x, v.pos.z, p.x, p.z);
      if ((d2 > 320 * 320 || (v.isWrecked && d2 > 100 * 100)) && !v.persistent && game.player.vehicle !== v) {
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

// old name used by police / missions
export function nearestSegment(x, z, yaw, game) { return game ? nearestLane(game.map.roads, x, z, yaw) : null; }
