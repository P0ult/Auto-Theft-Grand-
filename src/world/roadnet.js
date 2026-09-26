// Road network: a graph of nodes (junctions, roundabouts, merges) and edges (3D polylines with lane
// layouts). Used by the road renderer, traffic / police / mission drivers, GPS routing, the map and
// terrain shaping (roads cut and fill the heightfield; high sections become bridges / viaducts).
import { clamp, lerp, smoothstep } from '../core/utils.js';

// lanes: [forward, backward]; off0: lateral offset of lane 0's left edge (right-positive, relative to
// the direction of travel); wL / wR: paved half widths to the left / right of the centerline.
export const RT = {
  street: { lanes: [2, 2], laneW: 3.5, off0: 0.15, wL: 10, wR: 10, speed: 13.5, mark: 0, cls: 1 },
  freeway: { lanes: [2, 0], laneW: 3.7, off0: -3.7, wL: 4.8, wR: 5.9, speed: 29, mark: 1, cls: 3, barrier: true },
  ramp: { lanes: [1, 0], laneW: 4.0, off0: -2.0, wL: 3.3, wR: 3.6, speed: 17, mark: 2, cls: 2, barrier: true },
  highway: { lanes: [1, 1], laneW: 3.6, off0: 0.12, wL: 5.5, wR: 5.5, speed: 24, mark: 3, cls: 2 },
  road: { lanes: [1, 1], laneW: 3.3, off0: 0.1, wL: 4.5, wR: 4.5, speed: 15, mark: 4, cls: 1 },
  dirt: { lanes: [1, 1], laneW: 2.8, off0: 0.0, wL: 3.4, wR: 3.4, speed: 11, mark: 5, cls: 0 },
  rail: { lanes: [0, 0], laneW: 0, off0: 0, wL: 3.3, wR: 3.3, speed: 0, mark: 7, cls: -1 },
};
export const DECK_H = 1.3;           // deck thickness below the road surface
export const DECK_MIN = 3.2;         // road this far above the ground is a bridge / viaduct

// --------------------------------------------------------------------------- polyline helpers
export function catmull(ctrl, spacing = 6) {
  const out = [];
  const P = (i) => ctrl[clamp(i, 0, ctrl.length - 1)];
  for (let i = 0; i < ctrl.length - 1; i++) {
    const p0 = P(i - 1), p1 = P(i), p2 = P(i + 1), p3 = P(i + 2);
    const segLen = Math.hypot(p2[0] - p1[0], p2[1] - p1[1]);
    const n = Math.max(1, Math.ceil(segLen / spacing));
    for (let k = 0; k < n; k++) {
      const t = k / n, t2 = t * t, t3 = t2 * t;
      const f = (a, b, c, d) => 0.5 * ((2 * b) + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (-a + 3 * b - 3 * c + d) * t3);
      out.push([f(p0[0], p1[0], p2[0], p3[0]), f(p0[1], p1[1], p2[1], p3[1])]);
    }
  }
  out.push([ctrl[ctrl.length - 1][0], ctrl[ctrl.length - 1][1]]);
  return out;
}
export function resample(pts, spacing = 6) {
  const cum = cumLen(pts);
  const L = cum[cum.length - 1];
  const n = Math.max(1, Math.round(L / spacing));
  const out = [];
  for (let k = 0; k <= n; k++) out.push(pointAt(pts, cum, L * k / n));
  return out;
}
export function cumLen(pts) {
  const c = [0];
  for (let i = 1; i < pts.length; i++) c.push(c[i - 1] + Math.hypot(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]));
  return c;
}
export function pointAt(pts, cum, s) {
  if (s <= 0) return pts[0].slice();
  const L = cum[cum.length - 1];
  if (s >= L) return pts[pts.length - 1].slice();
  let lo = 0, hi = cum.length - 1;
  while (hi - lo > 1) { const m = (lo + hi) >> 1; if (cum[m] <= s) lo = m; else hi = m; }
  const t = (s - cum[lo]) / (cum[hi] - cum[lo] || 1);
  const a = pts[lo], b = pts[hi];
  const r = [];
  for (let k = 0; k < a.length; k++) r.push(a[k] + (b[k] - a[k]) * t);
  return r;
}
// tangent (unit) at point index of a 2D polyline
export function tangentAt(pts, i) {
  const a = pts[Math.max(0, i - 1)], b = pts[Math.min(pts.length - 1, i + 1)];
  const dx = b[0] - a[0], dz = b[1] - a[1], l = Math.hypot(dx, dz) || 1;
  return [dx / l, dz / l];
}
// offset a polyline sideways (right-positive; right of heading (fx,fz) is (-fz, fx))
export function offsetLine(pts, off) {
  return pts.map((p, i) => { const [tx, tz] = tangentAt(pts, i); const o = typeof off === 'function' ? off(i) : off; return [p[0] - tz * o, p[1] + tx * o, ...p.slice(2)]; });
}
// closest station on a polyline
export function project(pts, cum, x, z) {
  let best = Infinity, bs = 0, bi = 0, bl = 0;
  for (let i = 0; i < pts.length - 1; i++) {
    const a = pts[i], b = pts[i + 1];
    const dx = b[0] - a[0], dz = b[1] - a[1], L2 = dx * dx + dz * dz || 1;
    const t = clamp(((x - a[0]) * dx + (z - a[1]) * dz) / L2, 0, 1);
    const px = a[0] + dx * t, pz = a[1] + dz * t;
    const d = (x - px) ** 2 + (z - pz) ** 2;
    if (d < best) { best = d; bs = cum[i] + (cum[i + 1] - cum[i]) * t; bi = i; bl = ((x - a[0]) * -dz + (z - a[1]) * dx) / Math.sqrt(L2); }
  }
  return { s: bs, d: Math.sqrt(best), i: bi, lat: -bl };
}
// intersection of two 2D polylines (first hit): returns {sa, sb, x, z}
export function intersect(A, cumA, B, cumB) {
  for (let i = 0; i < A.length - 1; i++) {
    const p = A[i], r0 = A[i + 1][0] - p[0], r1 = A[i + 1][1] - p[1];
    for (let j = 0; j < B.length - 1; j++) {
      const q = B[j], s0 = B[j + 1][0] - q[0], s1 = B[j + 1][1] - q[1];
      const den = r0 * s1 - r1 * s0;
      if (Math.abs(den) < 1e-9) continue;
      const t = ((q[0] - p[0]) * s1 - (q[1] - p[1]) * s0) / den;
      const u = ((q[0] - p[0]) * r1 - (q[1] - p[1]) * r0) / den;
      if (t >= 0 && t <= 1 && u >= 0 && u <= 1) {
        const segA = cumA[i + 1] - cumA[i], segB = cumB[j + 1] - cumB[j];
        return { sa: cumA[i] + segA * t, sb: cumB[j] + segB * u, x: p[0] + r0 * t, z: p[1] + r1 * t };
      }
    }
  }
  return null;
}

// --------------------------------------------------------------------------- profile solver
// Road heights along a dense polyline: follow the smoothed terrain (limited cuts), honour pins
// (junction heights, bridge clearances) with local blends, fixed sections, and a maximum grade.
export function solveProfile(pts, terrain, opts = {}) {
  const n = pts.length;
  const cum = cumLen(pts);
  const raw = new Float32Array(n);
  // over water the road stays high enough to bridge it
  for (let i = 0; i < n; i++) { const t = terrain(pts[i][0], pts[i][1]); raw[i] = t < 0.3 ? Math.max(t, opts.waterY ?? 5.5) : Math.max(t, opts.minY ?? 1.2); }
  // moving average over a window (in meters)
  const win = opts.window ?? 120;
  const y = new Float32Array(n);
  let lo = 0, hi = 0, sum = 0;
  for (let i = 0; i < n; i++) {
    while (hi < n && cum[hi] - cum[i] <= win / 2) { sum += raw[hi]; hi++; }
    while (cum[i] - cum[lo] > win / 2) { sum -= raw[lo]; lo++; }
    y[i] = sum / (hi - lo);
  }
  // don't dig deeper than maxCut into hills
  const maxCut = opts.maxCut ?? 14;
  for (let i = 0; i < n; i++) y[i] = Math.max(y[i], raw[i] - maxCut);
  // pins with local blends
  const pins = (opts.pins || []).slice();
  if (opts.y0 != null) pins.push({ s: 0, y: opts.y0 });
  if (opts.y1 != null) pins.push({ s: cum[n - 1], y: opts.y1 });
  const g = opts.maxGrade ?? 0.07;
  const add = new Float32Array(n);
  for (const p of pins) {
    const i0 = nearestIndex(cum, p.s);
    const c = p.y - y[i0];
    const R = Math.max(p.r ?? 0, 30, Math.abs(c) / g * 1.35);
    for (let i = 0; i < n; i++) {
      const d = Math.abs(cum[i] - p.s);
      if (d < R) { const t = 1 - d / R; add[i] += c * t * t * (3 - 2 * t); }
    }
  }
  for (let i = 0; i < n; i++) y[i] += add[i];
  const fixed = new Uint8Array(n);
  if (opts.fixed) for (let i = 0; i < n; i++) { const f = opts.fixed(pts[i][0], pts[i][1], cum[i]); if (f != null) { y[i] = f; fixed[i] = 1; } }
  for (const p of pins) { const i0 = nearestIndex(cum, p.s); y[i0] = p.y; fixed[i0] = 1; }
  // grade limit (a few relaxation passes that keep fixed points)
  for (let it = 0; it < 4; it++) {
    for (let i = 1; i < n; i++) if (!fixed[i]) { const ds = cum[i] - cum[i - 1]; y[i] = clamp(y[i], y[i - 1] - g * ds, y[i - 1] + g * ds); }
    for (let i = n - 2; i >= 0; i--) if (!fixed[i]) { const ds = cum[i + 1] - cum[i]; y[i] = clamp(y[i], y[i + 1] - g * ds, y[i + 1] + g * ds); }
  }
  return { y, cum };
}
function nearestIndex(cum, s) {
  let lo = 0, hi = cum.length - 1;
  while (hi - lo > 1) { const m = (lo + hi) >> 1; if (cum[m] <= s) lo = m; else hi = m; }
  return s - cum[lo] < cum[hi] - s ? lo : hi;
}

// --------------------------------------------------------------------------- graph
let _eid = 0, _nid = 0;
export class RoadNet {
  constructor() {
    this.nodes = [];
    this.edges = [];
    this.cell = 64;
    this.grid = new Map();
  }

  addNode(x, z, y, opts = {}) {
    const n = { id: this.nodes.length, x, z, y, e: [], kind: opts.kind || 'x', r: opts.r ?? 0, sig: opts.sig ?? null, grid: opts.grid || null, rbR: opts.rbR || 0, name: opts.name || '', noStop: !!opts.noStop };
    this.nodes.push(n);
    return n;
  }

  // pts: array of [x, z, y] (dense). Endpoints may be offset from the node positions.
  addEdge(a, b, pts, type, opts = {}) {
    const T = RT[type];
    const n = pts.length;
    const p = new Float32Array(n * 3), cum = new Float32Array(n);
    for (let i = 0; i < n; i++) {
      p[i * 3] = pts[i][0]; p[i * 3 + 1] = pts[i][2] ?? 0; p[i * 3 + 2] = pts[i][1];
      if (i) cum[i] = cum[i - 1] + Math.hypot(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]);
    }
    const e = {
      id: this.edges.length, a: a.id, b: b.id, type, T, p, cum, len: cum[n - 1], n,
      lanesF: opts.lanes ? opts.lanes[0] : T.lanes[0], lanesB: opts.lanes ? opts.lanes[1] : T.lanes[1],
      render: opts.render !== false, grid: opts.grid || null, name: opts.name || '', deck: new Uint8Array(n),
      speed: opts.speed ?? T.speed, wL: opts.wL ?? T.wL, wR: opts.wR ?? T.wR, barrierL: opts.barrierL, barrierR: opts.barrierR,
      under: opts.under || 0, city: !!opts.city, lowerNearA: 0, lowerNearB: 0, noBarrierA: opts.noBarrierA || 0, noBarrierB: opts.noBarrierB || 0,
      trimA: opts.trimA ?? null, trimB: opts.trimB ?? null, base: !!opts.base,
    };
    this.edges.push(e);
    a.e.push(e.id); if (b !== a) b.e.push(e.id);
    this._index(e);
    return e;
  }

  _index(e) {
    const c = this.cell;
    const pad = Math.max(e.wL, e.wR) + 2;
    for (let i = 0; i < e.n - 1; i++) {
      const x0 = Math.min(e.p[i * 3], e.p[i * 3 + 3]) - pad, x1 = Math.max(e.p[i * 3], e.p[i * 3 + 3]) + pad;
      const z0 = Math.min(e.p[i * 3 + 2], e.p[i * 3 + 5]) - pad, z1 = Math.max(e.p[i * 3 + 2], e.p[i * 3 + 5]) + pad;
      for (let gx = Math.floor(x0 / c); gx <= Math.floor(x1 / c); gx++) for (let gz = Math.floor(z0 / c); gz <= Math.floor(z1 / c); gz++) {
        const k = gx * 100003 + gz;
        let arr = this.grid.get(k);
        if (!arr) { arr = []; this.grid.set(k, arr); }
        const last = arr[arr.length - 1];
        if (last && last.e === e && last.i1 === i) last.i1 = i + 1;
        else arr.push({ e, i0: i, i1: i + 1 });
      }
    }
  }

  // segments near a point
  near(x, z, out = []) {
    const c = this.cell;
    const arr = this.grid.get(Math.floor(x / c) * 100003 + Math.floor(z / c));
    if (arr) for (const r of arr) out.push(r);
    return out;
  }
  edgesIn(x0, z0, x1, z1) {
    const c = this.cell, set = new Set();
    for (let gx = Math.floor(x0 / c); gx <= Math.floor(x1 / c); gx++) for (let gz = Math.floor(z0 / c); gz <= Math.floor(z1 / c); gz++) {
      const arr = this.grid.get(gx * 100003 + gz);
      if (arr) for (const r of arr) set.add(r.e);
    }
    return [...set];
  }

  // Closest point on any edge (optionally only renderable / non-grid). Returns {e, s, d, lat, y, i}
  // nearest edge point in plan view; with `y`, roads far above / below (viaducts vs streets) are ruled out
  closest(x, z, filter = null, maxD = 1e9, y = null) {
    let best = null;
    const tryRange = (e, i0, i1) => {
      for (let i = i0; i < i1; i++) {
        const ax = e.p[i * 3], az = e.p[i * 3 + 2], bx = e.p[i * 3 + 3], bz = e.p[i * 3 + 5];
        const dx = bx - ax, dz = bz - az, L2 = dx * dx + dz * dz || 1;
        const t = clamp(((x - ax) * dx + (z - az) * dz) / L2, 0, 1);
        const px = ax + dx * t, pz = az + dz * t;
        let d = Math.hypot(x - px, z - pz);
        if (y != null) { const ey = e.p[i * 3 + 1] + (e.p[i * 3 + 4] - e.p[i * 3 + 1]) * t; d += Math.max(0, Math.abs(ey - y) - 3) * 12; }
        if (d < maxD && (!best || d < best.d)) {
          const L = Math.sqrt(L2);
          const lat = ((x - ax) * -dz + (z - az) * dx) / L; // left-positive
          best = { e, s: e.cum[i] + L * t, d, lat: -lat, y: e.p[i * 3 + 1] + (e.p[i * 3 + 4] - e.p[i * 3 + 1]) * t, i, t };
        }
      }
    };
    const c = this.cell;
    const R = Math.min(maxD, 400);
    for (let gx = Math.floor((x - R) / c); gx <= Math.floor((x + R) / c); gx++) for (let gz = Math.floor((z - R) / c); gz <= Math.floor((z + R) / c); gz++) {
      const arr = this.grid.get(gx * 100003 + gz);
      if (!arr) continue;
      for (const r of arr) if (!filter || filter(r.e)) tryRange(r.e, r.i0, r.i1);
    }
    return best;
  }

  // Is (x,z) on a (non-city-grid) paved road surface? Returns the hit or null.
  onRoad(x, z, margin = 0) {
    const arr = this.grid.get(Math.floor(x / this.cell) * 100003 + Math.floor(z / this.cell));
    if (!arr) return null;
    for (const r of arr) {
      const e = r.e;
      if (e.grid) continue;
      for (let i = r.i0; i < r.i1; i++) {
        const ax = e.p[i * 3], az = e.p[i * 3 + 2], bx = e.p[i * 3 + 3], bz = e.p[i * 3 + 5];
        const dx = bx - ax, dz = bz - az, L2 = dx * dx + dz * dz || 1;
        const t = clamp(((x - ax) * dx + (z - az) * dz) / L2, 0, 1);
        const px = ax + dx * t, pz = az + dz * t;
        const lat = ((x - px) * -dz + (z - pz) * dx) / Math.sqrt(L2); // left positive
        const w = lat > 0 ? e.wL : e.wR;
        if (Math.abs(lat) <= w + margin) return { e, i, t, y: e.p[i * 3 + 1] + (e.p[i * 3 + 4] - e.p[i * 3 + 1]) * t, deck: e.deck[i] || e.deck[i + 1] };
      }
    }
    return null;
  }

  other(e, nodeId) { return e.a === nodeId ? this.nodes[e.b] : this.nodes[e.a]; }

  // directions a vehicle may leave node n along: [{e, dir}] (dir 0: a->b, 1: b->a)
  exits(n) {
    const out = [];
    for (const id of n.e) {
      const e = this.edges[id];
      if (e.a === n.id && e.lanesF > 0) out.push({ e, dir: 0 });
      if (e.b === n.id && e.lanesB > 0) out.push({ e, dir: 1 });
    }
    return out;
  }

  // point on the edge centerline at station s: [x, y, z, tx, tz]
  at(e, s, out = [0, 0, 0, 0, 0]) {
    s = clamp(s, 0, e.len);
    const cum = e.cum;
    let lo = 0, hi = e.n - 1;
    while (hi - lo > 1) { const m = (lo + hi) >> 1; if (cum[m] <= s) lo = m; else hi = m; }
    const t = (s - cum[lo]) / (cum[hi] - cum[lo] || 1);
    const p = e.p;
    const ax = p[lo * 3], ay = p[lo * 3 + 1], az = p[lo * 3 + 2], bx = p[hi * 3], by = p[hi * 3 + 1], bz = p[hi * 3 + 2];
    const dx = bx - ax, dz = bz - az, l = Math.hypot(dx, dz) || 1;
    out[0] = ax + dx * t; out[1] = ay + (by - ay) * t; out[2] = az + dz * t; out[3] = dx / l; out[4] = dz / l;
    return out;
  }

  laneOffset(e, dir, lane) {
    const T = e.T;
    const nl = dir === 0 ? e.lanesF : e.lanesB;
    lane = clamp(lane, 0, nl - 1);
    return T.off0 + T.laneW * (lane + 0.5);
  }

  // clearance of a node for an edge end: where lanes stop / start
  clearance(n) {
    if (n.kind === 'via' || n.kind === 'split' || n.kind === 'merge') return 0;
    if (n.kind === 'rb') return n.rbR + 9;
    return n.r;
  }

  // Dense lane polyline for edge e travelled in direction dir, lane index. Array of [x,y,z].
  lanePath(e, dir, lane, trimStart = null, trimEnd = null) {
    const na = this.nodes[dir === 0 ? e.a : e.b], nb = this.nodes[dir === 0 ? e.b : e.a];
    const off = this.laneOffset(e, dir, lane);
    const tA = dir === 0 ? e.trimA : e.trimB, tB = dir === 0 ? e.trimB : e.trimA;
    let s0 = trimStart ?? tA ?? this.clearance(na) + (na.kind === 'x' || na.kind === 'rb' ? 0.5 : 0);
    let s1 = e.len - (trimEnd ?? tB ?? this.clearance(nb) + (nb.kind === 'x' ? 7.5 : nb.kind === 'rb' ? 1 : 0));
    if (s1 - s0 < 2) { const m = (s0 + s1) / 2; s0 = Math.max(0, m - 1); s1 = Math.min(e.len, m + 1); }
    const out = [];
    const step = 5;
    const n = Math.max(1, Math.ceil((s1 - s0) / step));
    const tmp = [0, 0, 0, 0, 0];
    for (let k = 0; k <= n; k++) {
      const s = s0 + (s1 - s0) * k / n;
      const ss = dir === 0 ? s : e.len - s;
      this.at(e, ss, tmp);
      let tx = tmp[3], tz = tmp[4];
      if (dir === 1) { tx = -tx; tz = -tz; }
      // right of travel = (-tz, tx)
      out.push([tmp[0] - tz * off, tmp[1], tmp[2] + tx * off]);
    }
    return out;
  }

  // ------------------------------------------------------------------ routing (A*)
  route(fromX, fromZ, toX, toZ, opts = {}) {
    const drivable = (e) => e.type !== 'rail' && (!opts.filter || opts.filter(e));
    const start = this.closest(fromX, fromZ, drivable);
    const goal = this.closest(toX, toZ, drivable);
    if (!start || !goal) return null;
    const nodes = this.nodes;
    // seed with both ends of the start edge
    const g = new Map(), came = new Map(), open = [];
    const h = (n) => Math.hypot(n.x - goal.e.p[0], n.z - goal.e.p[2]) * 0 + Math.hypot(n.x - toX, n.z - toZ);
    const push = (id, cost, from, via) => {
      if (g.has(id) && g.get(id) <= cost) return;
      g.set(id, cost); came.set(id, { from, via });
      open.push([cost + h(nodes[id]), id]);
    };
    const se = start.e;
    if (se.lanesF > 0 || opts.anyDir) push(se.b, se.len - start.s, -1, se.id);
    if (se.lanesB > 0 || opts.anyDir) push(se.a, start.s, -1, se.id);
    const goalNodes = new Set([goal.e.a, goal.e.b]);
    let found = null, iter = 0;
    while (open.length && iter++ < 20000) {
      let bi = 0;
      for (let i = 1; i < open.length; i++) if (open[i][0] < open[bi][0]) bi = i;
      const [, id] = open[bi];
      open[bi] = open[open.length - 1]; open.pop();
      if (goalNodes.has(id)) { found = id; break; }
      const n = nodes[id];
      const gc = g.get(id);
      for (const eid of n.e) {
        const e = this.edges[eid];
        let to, ok;
        if (e.a === id) { to = e.b; ok = e.lanesF > 0 || opts.anyDir; } else { to = e.a; ok = e.lanesB > 0 || opts.anyDir; }
        if (!ok || to === id) continue;
        const pen = e.type === 'dirt' ? 1.6 : e.type === 'freeway' ? 0.75 : e.type === 'ramp' ? 0.9 : 1;
        push(to, gc + e.len * pen, id, eid);
      }
    }
    if (found == null) return null;
    // rebuild the edge list
    const seq = [];
    let cur = found;
    while (cur != null && came.has(cur)) { const c = came.get(cur); seq.push({ node: cur, edge: c.via }); if (c.from === -1) break; cur = c.from; }
    seq.reverse();
    return { start, goal, seq };
  }

  // polyline of a route for drawing (x,z pairs)
  routePolyline(r, fromX, fromZ, toX, toZ) {
    if (!r) return [[fromX, fromZ], [toX, toZ]];
    const pts = [[fromX, fromZ]];
    const tmp = [0, 0, 0, 0, 0];
    let prevNode = null;
    for (const step of r.seq) {
      const e = this.edges[step.edge];
      const forward = e.b === step.node;
      if (prevNode === null) {
        // partial start edge
        const s0 = r.start.s, s1 = forward ? e.len : 0;
        const n = Math.max(1, Math.ceil(Math.abs(s1 - s0) / 25));
        for (let k = 0; k <= n; k++) { this.at(e, s0 + (s1 - s0) * k / n, tmp); pts.push([tmp[0], tmp[2]]); }
      } else {
        const n = Math.max(1, Math.ceil(e.len / 25));
        for (let k = 0; k <= n; k++) { this.at(e, forward ? e.len * k / n : e.len * (1 - k / n), tmp); pts.push([tmp[0], tmp[2]]); }
      }
      prevNode = step.node;
    }
    // final partial goal edge
    const ge = r.goal.e;
    if (prevNode != null) {
      const fromA = ge.a === prevNode;
      const s0 = fromA ? 0 : ge.len, s1 = r.goal.s;
      const n = Math.max(1, Math.ceil(Math.abs(s1 - s0) / 25));
      for (let k = 0; k <= n; k++) { this.at(ge, s0 + (s1 - s0) * k / n, tmp); pts.push([tmp[0], tmp[2]]); }
    }
    pts.push([toX, toZ]);
    return pts;
  }
}

// --------------------------------------------------------------------------- terrain shaping
// Roads cut into hills and sit on embankments; where a road runs high above the ground it becomes a
// bridge / viaduct (deck flag). City roads above street level are always decks (the city isn't
// part of the heightfield). groundAt(x,z) returns the non-heightfield ground (city) or null.
const FILL_MAX_BY_TYPE = { freeway: 7.5, ramp: 6.5, highway: 7.5, road: 6.5, dirt: 16, rail: 9 };
export function shapeTerrain(net, hf, groundAt, opts = {}) {
  const edges = net.edges.filter((e) => !e.removed && !e.grid);
  const avgY = (e) => { let s = 0; for (let i = 0; i < e.n; i++) s += e.p[i * 3 + 1]; return s / e.n; };
  edges.sort((a, b) => avgY(b) - avgY(a));
  const h = hf.h, st = hf.step;
  for (const e of edges) {
    const p = e.p;
    // decide per point: deck (bridge) or ground-hugging (flatten the terrain)
    for (let i = 0; i < e.n; i++) {
      const x = p[i * 3], y = p[i * 3 + 1], z = p[i * 3 + 2];
      const cg = groundAt(x, z);
      if (cg != null) { e.deck[i] = y - cg > 0.14 ? 1 : 0; continue; }
      const g = hf.sample(x, z);
      e.deck[i] = y - g > (FILL_MAX_BY_TYPE[e.type] ?? 6) || g < 0.4 ? 1 : 0;
    }
    // soften: isolated flags off, pad decks by one point so bridges reach solid ground
    const d0 = e.deck.slice();
    for (let i = 0; i < e.n; i++) if (d0[i] && !(d0[i - 1] || d0[i + 1]) && e.n > 2) e.deck[i] = 0;
    const core = Math.max(e.wL, e.wR) + 2.2, blend = e.type === 'freeway' || e.type === 'ramp' ? 22 : 16;
    for (let i = 0; i < e.n - 1; i++) {
      if (e.deck[i] && e.deck[i + 1]) continue;
      const ax = p[i * 3], ay = p[i * 3 + 1], az = p[i * 3 + 2], bx = p[i * 3 + 3], by = p[i * 3 + 4], bz = p[i * 3 + 5];
      if (groundAt(ax, az) != null && groundAt(bx, bz) != null) continue;
      const R = core + blend;
      const x0 = Math.min(ax, bx) - R, x1 = Math.max(ax, bx) + R, z0 = Math.min(az, bz) - R, z1 = Math.max(az, bz) + R;
      const dx = bx - ax, dz = bz - az, L2 = dx * dx + dz * dz || 1;
      hf.forRect(x0, z0, x1, z1, (k, x, z) => {
        if (groundAt(x, z) != null) return;
        const t = clamp(((x - ax) * dx + (z - az) * dz) / L2, 0, 1);
        const d = Math.hypot(x - ax - dx * t, z - az - dz * t);
        if (d > R) return;
        const ty = ay + (by - ay) * t - 0.07;
        const w = d <= core ? 1 : 1 - smoothstep(core, R, d);
        // cuts only lower, fills only raise (so crossing roads don't bury each other)
        const cur = h[k];
        const nv = cur + (ty - cur) * w;
        if (d <= core) h[k] = ty; else h[k] = nv;
      });
    }
  }
  // junction pads
  for (const n of net.nodes) {
    if (n.grid || n.city || n.dead) continue;
    if (groundAt(n.x, n.z) != null) continue;
    const r = n.kind === 'rb' ? n.rbR + 11 : n.kind === 'x' ? n.r + 1 : n.kind === 'end' ? 6 : 0;
    if (!r) continue;
    hf.pad({ x: n.x, z: n.z, r, y: n.y - 0.07, blend: 16 });
  }
  // final deck flags against the shaped terrain
  for (const e of edges) {
    const p = e.p;
    for (let i = 0; i < e.n; i++) {
      const x = p[i * 3], y = p[i * 3 + 1], z = p[i * 3 + 2];
      const cg = groundAt(x, z);
      e.deck[i] = cg != null ? (y - cg > 0.14 ? 1 : 0) : (y - hf.sample(x, z) > 1.6 ? 1 : 0);
    }
  }
}

