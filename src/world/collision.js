// Static collision world: spatial hash of axis-aligned boxes (city buildings, fences, containers),
// oriented boxes (barriers, pillars, rotated buildings), circles (props, trees) and road decks
// (bridges / viaducts: drivable sloped surfaces that also act as solid walls from the side).
import { clamp } from '../core/utils.js';
import { DECK_H } from './roadnet.js';

const CELL = 24;

export class CollisionWorld {
  constructor(map) {
    this.map = map;
    this.cells = new Map();
    this.boxes = [];
    this.circles = [];
    this.decks = [];
    this.stamp = 0;
    for (const c of map.colliders) {
      if (c.yaw) this.addOBox({ ...c }); else this.addBox(c);
    }
  }

  _key(ix, iz) { return ix * 100003 + iz; }
  _insert(obj, minX, minZ, maxX, maxZ) {
    const x0 = Math.floor(minX / CELL), x1 = Math.floor(maxX / CELL);
    const z0 = Math.floor(minZ / CELL), z1 = Math.floor(maxZ / CELL);
    obj._cells = [];
    for (let ix = x0; ix <= x1; ix++) for (let iz = z0; iz <= z1; iz++) {
      const k = this._key(ix, iz);
      let arr = this.cells.get(k);
      if (!arr) { arr = []; this.cells.set(k, arr); }
      arr.push(obj);
      obj._cells.push(k);
    }
  }
  remove(obj) {
    if (!obj._cells) return;
    for (const k of obj._cells) {
      const arr = this.cells.get(k);
      if (arr) { const i = arr.indexOf(obj); if (i >= 0) arr.splice(i, 1); }
    }
    obj._cells = null;
    obj.removed = true;
  }
  addBox(b) { b.kind = 'box'; this.boxes.push(b); this._insert(b, b.minX, b.minZ, b.maxX, b.maxZ); return b; }
  addCircle(c) { c.kind = 'circle'; this.circles.push(c); this._insert(c, c.x - c.r, c.z - c.r, c.x + c.r, c.z + c.r); return c; }
  // oriented box: {cx, cz, hx, hz, yaw, minY, maxY}; local +z = (sin yaw, cos yaw)
  addOBox(o) {
    o.kind = 'obox';
    o.s = Math.sin(o.yaw); o.c = Math.cos(o.yaw);
    const ex = Math.abs(o.c) * o.hx + Math.abs(o.s) * o.hz, ez = Math.abs(o.s) * o.hx + Math.abs(o.c) * o.hz;
    o.minX = o.cx - ex; o.maxX = o.cx + ex; o.minZ = o.cz - ez; o.maxZ = o.cz + ez;
    this.boxes.push(o);
    this._insert(o, o.minX, o.minZ, o.maxX, o.maxZ);
    return o;
  }
  // road deck segment: centre line a->b with heights, half widths left/right of travel
  addDeck(d) {
    d.kind = 'deck';
    const dx = d.bx - d.ax, dz = d.bz - d.az;
    d.len = Math.hypot(dx, dz) || 1;
    d.dx = dx / d.len; d.dz = dz / d.len;
    const pad = Math.max(d.hl, d.hr) + 0.5;
    d.minX = Math.min(d.ax, d.bx) - pad; d.maxX = Math.max(d.ax, d.bx) + pad;
    d.minZ = Math.min(d.az, d.bz) - pad; d.maxZ = Math.max(d.az, d.bz) + pad;
    d.minY = Math.min(d.ay, d.by) - DECK_H; d.maxY = Math.max(d.ay, d.by);
    this.decks.push(d);
    this._insert(d, d.minX, d.minZ, d.maxX, d.maxZ);
    return d;
  }
  // height of a deck at (x,z) or -Infinity
  _deckAt(d, x, z) {
    const px = x - d.ax, pz = z - d.az;
    const t = px * d.dx + pz * d.dz;
    if (t < -0.3 || t > d.len + 0.3) return -Infinity;
    const lat = px * -d.dz + pz * d.dx; // right-positive
    if (lat < -d.hl || lat > d.hr) return -Infinity;
    const k = clamp(t / d.len, 0, 1);
    return d.ay + (d.by - d.ay) * k;
  }

  // Collect colliders near an AABB (deduplicated)
  query(minX, minZ, maxX, maxZ, out = []) {
    this.stamp++;
    const x0 = Math.floor(minX / CELL), x1 = Math.floor(maxX / CELL);
    const z0 = Math.floor(minZ / CELL), z1 = Math.floor(maxZ / CELL);
    for (let ix = x0; ix <= x1; ix++) for (let iz = z0; iz <= z1; iz++) {
      const arr = this.cells.get(this._key(ix, iz));
      if (!arr) continue;
      for (const o of arr) { if (o._stamp !== this.stamp) { o._stamp = this.stamp; out.push(o); } }
    }
    return out;
  }

  // Resolve a circle (character) against static geometry at height range [y, y+h]. Returns push vector.
  resolveCircle(x, z, r, y = 0, h = 1.8, out = { x: 0, z: 0, hit: null }) {
    out.x = x; out.z = z; out.hit = null;
    const list = this.query(x - r - 1, z - r - 1, x + r + 1, z + r + 1, _tmpList);
    for (const o of list) {
      if (o.kind === 'box' || o.kind === 'obox' || o.kind === 'deck') {
        if (y + h < o.minY || y > o.maxY - 0.05) continue;
        if (o.maxY - y < 0.45 && o.type !== 'building') continue; // can step over low stuff
        if (o.kind === 'deck') {
          // the deck is solid from the side where it is above the walker
          const dh = this._deckAt(o, out.x, out.z);
          if (dh === -Infinity) {
            // near the side: push out of the widened footprint
            const px = out.x - o.ax, pz = out.z - o.az;
            const t = px * o.dx + pz * o.dz;
            if (t < 0 || t > o.len) continue;
            const lat = px * -o.dz + pz * o.dx;
            const edge = lat < 0 ? -o.hl : o.hr;
            const d = lat - edge;
            if (Math.abs(d) < r && y < this._deckAtClamped(o, t) - 0.45) {
              const push = (r - Math.abs(d)) * Math.sign(d || 1);
              out.x += -o.dz * push; out.z += o.dx * push; out.hit = o;
            }
          }
          continue;
        }
        let lx, lz, hx, hz;
        if (o.kind === 'obox') { const dx = out.x - o.cx, dz = out.z - o.cz; lx = dx * o.c - dz * o.s; lz = dx * o.s + dz * o.c; hx = o.hx; hz = o.hz; }
        else { lx = out.x - (o.minX + o.maxX) / 2; lz = out.z - (o.minZ + o.maxZ) / 2; hx = (o.maxX - o.minX) / 2; hz = (o.maxZ - o.minZ) / 2; }
        const qx = clamp(lx, -hx, hx), qz = clamp(lz, -hz, hz);
        let dx = lx - qx, dz = lz - qz;
        const d2 = dx * dx + dz * dz;
        if (d2 < r * r) {
          let nlx = lx, nlz = lz;
          if (d2 > 1e-8) { const d = Math.sqrt(d2), push = r - d; nlx = lx + dx / d * push; nlz = lz + dz / d * push; }
          else {
            const l = lx + hx, rr = hx - lx, t = lz + hz, bb = hz - lz;
            const m = Math.min(l, rr, t, bb);
            if (m === l) nlx = -hx - r; else if (m === rr) nlx = hx + r; else if (m === t) nlz = -hz - r; else nlz = hz + r;
          }
          if (o.kind === 'obox') { out.x = o.cx + nlx * o.c + nlz * o.s; out.z = o.cz - nlx * o.s + nlz * o.c; }
          else { out.x = (o.minX + o.maxX) / 2 + nlx; out.z = (o.minZ + o.maxZ) / 2 + nlz; }
          out.hit = o;
        }
      } else if (!o.broken) {
        if (y > (o.h || 3)) continue;
        const dx = out.x - o.x, dz = out.z - o.z;
        const rr = r + o.r;
        const d2 = dx * dx + dz * dz;
        if (d2 < rr * rr && d2 > 1e-8) {
          const d = Math.sqrt(d2);
          out.x = o.x + dx / d * rr; out.z = o.z + dz / d * rr;
          out.hit = o;
        }
      }
    }
    _tmpList.length = 0;
    return out;
  }
  _deckAtClamped(o, t) { const k = clamp(t / o.len, 0, 1); return o.ay + (o.by - o.ay) * k; }

  // Oriented box (vehicle) vs statics. Returns list of contacts {nx,nz,depth,px,pz,obj}.
  obbContacts(cx, cz, yaw, hx, hz, y, out) {
    out.length = 0;
    const c = Math.cos(yaw), s = Math.sin(yaw);
    const fx = s, fz = c, rx = c, rz = -s;
    const ex = Math.abs(rx) * hx + Math.abs(fx) * hz, ez = Math.abs(rz) * hx + Math.abs(fz) * hz;
    const list = this.query(cx - ex - 1, cz - ez - 1, cx + ex + 1, cz + ez + 1, _tmpList);
    for (const o of list) {
      if (o.kind === 'box' || o.kind === 'obox' || o.kind === 'deck') {
        if (o.rayOnly) continue;
        if (o.kind === 'deck') {
          // vehicles hit the side of a deck that is above them (low city ramps, bridge abutments)
          if (y + 1.0 < o.minY) continue;
          const top = Math.max(this._deckAt(o, cx, cz), -1e9);
          if (top > -1e9) continue; // centre over the deck: we're on it (or under a high one)
          if (y + 0.4 > o.maxY) continue;
          const bx = (o.ax + o.bx) / 2 + -o.dz * (o.hr - o.hl) / 2, bz = (o.az + o.bz) / 2 + o.dx * (o.hr - o.hl) / 2;
          const res = satOBB(cx, cz, fx, fz, rx, rz, hx, hz, bx, bz, -o.dz, o.dx, 0, (o.hl + o.hr) / 2, o.len / 2, o.dx, o.dz);
          if (!res) continue;
          // only walls where the deck is actually higher than the car (not ramp ends)
          const dh = this._deckAtClamped(o, ((res.px - o.ax) * o.dx + (res.pz - o.az) * o.dz));
          if (dh - y < 0.45) continue;
          out.push({ ...res, obj: o });
          continue;
        }
        if (y + 1.2 < o.minY || y + 0.25 > o.maxY) continue;
        let bx, bz, ax1x, ax1z, bhx, bhz;
        if (o.kind === 'obox') { bx = o.cx; bz = o.cz; bhx = o.hx; bhz = o.hz; ax1x = o.c; ax1z = -o.s; }
        else { bx = (o.minX + o.maxX) / 2; bz = (o.minZ + o.maxZ) / 2; bhx = (o.maxX - o.minX) / 2; bhz = (o.maxZ - o.minZ) / 2; ax1x = 1; ax1z = 0; }
        // box axes: local x = (ax1x, ax1z), local z = (-ax1z, ax1x) ... for obox local z = (s, c)
        const ax2x = o.kind === 'obox' ? o.s : 0, ax2z = o.kind === 'obox' ? o.c : 1;
        const res = satOBB(cx, cz, fx, fz, rx, rz, hx, hz, bx, bz, ax1x, ax1z, 0, bhx, bhz, ax2x, ax2z);
        if (!res) continue;
        out.push({ ...res, obj: o });
      } else if (!o.broken) {
        if (y > (o.h || 3)) continue;
        const lx = o.x - cx, lz = o.z - cz;
        const lr = lx * rx + lz * rz, lf = lx * fx + lz * fz;
        const qr = clamp(lr, -hx, hx), qf = clamp(lf, -hz, hz);
        const wx = cx + rx * qr + fx * qf, wz = cz + rz * qr + fz * qf;
        let dx = wx - o.x, dz = wz - o.z;
        const d2 = dx * dx + dz * dz;
        if (d2 < o.r * o.r) {
          const d = Math.sqrt(d2) || 1e-4;
          if (d2 < 1e-8) { dx = cx - o.x; dz = cz - o.z; }
          const dl = Math.hypot(dx, dz) || 1;
          out.push({ nx: dx / dl, nz: dz / dl, depth: o.r - d, px: wx, pz: wz, obj: o });
        }
      }
    }
    _tmpList.length = 0;
    return out;
  }

  // Raycast against static boxes, circles, decks and the ground. dir must be normalized.
  raycast(ox, oy, oz, dx, dy, dz, maxDist, opts = {}) {
    let bestT = maxDist, bestObj = null, nX = 0, nY = 0, nZ = 0;
    // ground via marching
    const step = maxDist > 600 ? 4 : 2;
    let prevT = 0;
    for (let t = 0; t <= Math.min(maxDist, 1500); t += step) {
      const px = ox + dx * t, py = oy + dy * t, pz = oz + dz * t;
      const gh = this.map.groundHeight(px, pz);
      if (py < gh) {
        let a = prevT, b = t;
        for (let k = 0; k < 8; k++) { const m = (a + b) / 2; const my = oy + dy * m; if (my < this.map.groundHeight(ox + dx * m, oz + dz * m)) b = m; else a = m; }
        if (b < bestT) { bestT = b; bestObj = 'ground'; nX = 0; nY = 1; nZ = 0; }
        break;
      }
      prevT = t;
    }
    const ex = ox + dx * bestT, ez = oz + dz * bestT;
    const list = this.query(Math.min(ox, ex), Math.min(oz, ez), Math.max(ox, ex), Math.max(oz, ez), _tmpList);
    for (const o of list) {
      if (o.kind === 'box') {
        if (opts.ignoreSoft && o.soft) continue;
        const t = rayBox(ox, oy, oz, dx, dy, dz, o.minX, o.minY, o.minZ, o.maxX, o.maxY, o.maxZ);
        if (t >= 0 && t < bestT) { bestT = t; bestObj = o; nX = _n[0]; nY = _n[1]; nZ = _n[2]; }
      } else if (o.kind === 'obox' || o.kind === 'deck') {
        if (opts.ignoreSoft && o.soft) continue;
        let cxo, czo, s, c, hx, hz, y0, y1;
        if (o.kind === 'obox') { cxo = o.cx; czo = o.cz; s = o.s; c = o.c; hx = o.hx; hz = o.hz; y0 = o.minY; y1 = o.maxY; }
        else { cxo = (o.ax + o.bx) / 2 + -o.dz * (o.hr - o.hl) / 2; czo = (o.az + o.bz) / 2 + o.dx * (o.hr - o.hl) / 2; s = o.dx; c = o.dz; hx = (o.hl + o.hr) / 2; hz = o.len / 2; y0 = o.minY; y1 = o.maxY; }
        // ray into local frame (x' = dx*c - dz*s, z' = dx*s + dz*c)
        const lox = (ox - cxo) * c - (oz - czo) * s, loz = (ox - cxo) * s + (oz - czo) * c;
        const ldx = dx * c - dz * s, ldz = dx * s + dz * c;
        const t = rayBox(lox, oy, loz, ldx, dy, ldz, -hx, y0, -hz, hx, y1, hz);
        if (t >= 0 && t < bestT) {
          bestT = t; bestObj = o;
          // normal back to world
          const nlx = _n[0], nlz = _n[2];
          nX = nlx * c + nlz * s; nY = _n[1]; nZ = -nlx * s + nlz * c;
        }
      } else if (!o.broken && !opts.ignoreProps) {
        const lx = ox - o.x, lz = oz - o.z;
        const a = dx * dx + dz * dz; if (a < 1e-8) continue;
        const b = 2 * (lx * dx + lz * dz), c = lx * lx + lz * lz - o.r * o.r;
        const disc = b * b - 4 * a * c; if (disc < 0) continue;
        const t = (-b - Math.sqrt(disc)) / (2 * a);
        if (t >= 0 && t < bestT) {
          const hy = oy + dy * t;
          if (hy < (o.h || 3) && hy > (o.y0 ?? -50)) { bestT = t; bestObj = o; const hx = ox + dx * t - o.x, hz = oz + dz * t - o.z, hl = Math.hypot(hx, hz) || 1; nX = hx / hl; nY = 0; nZ = hz / hl; }
        }
      }
    }
    _tmpList.length = 0;
    if (!bestObj) return null;
    return { t: bestT, x: ox + dx * bestT, y: oy + dy * bestT, z: oz + dz * bestT, nx: nX, ny: nY, nz: nZ, obj: bestObj };
  }

  // Highest walkable surface under (x,z) that is not above y + step (terrain, curbs, roofs, containers, decks)
  floorHeight(x, z, y, step = 0.55) {
    let best = this.map.groundHeight(x, z);
    const arr = this.cells.get(this._key(Math.floor(x / CELL), Math.floor(z / CELL)));
    if (!arr) return best;
    if (y - best < 0.3 && !arr.some((o) => o.kind === 'deck')) return best;
    for (const o of arr) {
      if (o.removed) continue;
      if (o.kind === 'deck') { const h = this._deckAt(o, x, z); if (h <= y + step && h > best) best = h; continue; }
      if (o.kind === 'box') {
        if (x < o.minX || x > o.maxX || z < o.minZ || z > o.maxZ) continue;
        if (o.maxY <= y + step && o.maxY > best) best = o.maxY;
      } else if (o.kind === 'obox') {
        const dx = x - o.cx, dz = z - o.cz;
        if (Math.abs(dx * o.c - dz * o.s) > o.hx || Math.abs(dx * o.s + dz * o.c) > o.hz) continue;
        if (o.maxY <= y + step && o.maxY > best && !o.low) best = o.maxY;
      }
    }
    return best;
  }

  // Road surface for vehicles: terrain / curbs, or a bridge deck the vehicle is on
  surfaceHeight(x, z, y, step = 1.4) {
    let best = this.map.groundHeight(x, z);
    const arr = this.cells.get(this._key(Math.floor(x / CELL), Math.floor(z / CELL)));
    if (!arr) return best;
    for (const o of arr) {
      if (o.kind !== 'deck') continue;
      const h = this._deckAt(o, x, z);
      if (h <= y + step && h > best) best = h;
    }
    return best;
  }

  // Line of sight test (true if clear)
  lineOfSight(ax, ay, az, bx, by, bz) {
    const dx = bx - ax, dy = by - ay, dz = bz - az;
    const d = Math.hypot(dx, dy, dz);
    if (d < 0.01) return true;
    const h = this.raycast(ax, ay, az, dx / d, dy / d, dz / d, d, { ignoreProps: true, ignoreSoft: true });
    return !h;
  }
}

// 2D SAT between a vehicle OBB (axes f, r; half extents hx along r, hz along f) and a box with axes
// A1 (half bhx) and A2 (half bhz). Returns {nx, nz, depth, px, pz} (n pushes the vehicle out) or null.
function satOBB(cx, cz, fx, fz, rx, rz, hx, hz, bx, bz, a1x, a1z, _u, bhx, bhz, a2x, a2z) {
  const dx = bx - cx, dz = bz - cz;
  const axes = [[a1x, a1z], [a2x, a2z], [rx, rz], [fx, fz]];
  let minOverlap = Infinity, nx = 0, nz = 0;
  for (const [ax, az] of axes) {
    const ra = Math.abs(rx * ax + rz * az) * hx + Math.abs(fx * ax + fz * az) * hz;
    const rb = Math.abs(a1x * ax + a1z * az) * bhx + Math.abs(a2x * ax + a2z * az) * bhz;
    const dist = dx * ax + dz * az;
    const ov = ra + rb - Math.abs(dist);
    if (ov <= 0) return null;
    if (ov < minOverlap) { minOverlap = ov; const sgn = dist > 0 ? -1 : 1; nx = ax * sgn; nz = az * sgn; }
  }
  // contact point: deepest vehicle corner along -n, clamped into the box
  let best = -Infinity, px = cx, pz = cz;
  for (const [sx, sz] of CORNERS) {
    const qx = cx + rx * hx * sx + fx * hz * sz, qz = cz + rz * hx * sx + fz * hz * sz;
    const d = -(qx * nx + qz * nz);
    if (d > best) { best = d; px = qx; pz = qz; }
  }
  const l1 = clamp((px - bx) * a1x + (pz - bz) * a1z, -bhx, bhx), l2 = clamp((px - bx) * a2x + (pz - bz) * a2z, -bhz, bhz);
  px = bx + a1x * l1 + a2x * l2; pz = bz + a1z * l1 + a2z * l2;
  return { nx, nz, depth: minOverlap, px, pz };
}

const CORNERS = [[1, 1], [1, -1], [-1, 1], [-1, -1]];
const _tmpList = [];
const _n = [0, 0, 0];
function rayBox(ox, oy, oz, dx, dy, dz, minX, minY, minZ, maxX, maxY, maxZ) {
  let tmin = -Infinity, tmax = Infinity, axis = -1;
  if (Math.abs(dx) < 1e-9) { if (ox < minX || ox > maxX) return -1; }
  else { let t1 = (minX - ox) / dx, t2 = (maxX - ox) / dx; if (t1 > t2) { const t = t1; t1 = t2; t2 = t; } if (t1 > tmin) { tmin = t1; axis = 0; } if (t2 < tmax) tmax = t2; if (tmin > tmax) return -1; }
  if (Math.abs(dy) < 1e-9) { if (oy < minY || oy > maxY) return -1; }
  else { let t1 = (minY - oy) / dy, t2 = (maxY - oy) / dy; if (t1 > t2) { const t = t1; t1 = t2; t2 = t; } if (t1 > tmin) { tmin = t1; axis = 1; } if (t2 < tmax) tmax = t2; if (tmin > tmax) return -1; }
  if (Math.abs(dz) < 1e-9) { if (oz < minZ || oz > maxZ) return -1; }
  else { let t1 = (minZ - oz) / dz, t2 = (maxZ - oz) / dz; if (t1 > t2) { const t = t1; t1 = t2; t2 = t; } if (t1 > tmin) { tmin = t1; axis = 2; } if (t2 < tmax) tmax = t2; if (tmin > tmax) return -1; }
  if (tmax < 0) return -1;
  _n[0] = _n[1] = _n[2] = 0;
  if (axis === 0) _n[0] = dx > 0 ? -1 : 1; else if (axis === 1) _n[1] = dy > 0 ? -1 : 1; else if (axis === 2) _n[2] = dz > 0 ? -1 : 1;
  return Math.max(0, tmin);
}
