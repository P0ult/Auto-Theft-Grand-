// Static collision world: spatial hash of boxes (buildings, fences, containers) and circles (props).
import { clamp } from '../core/utils.js';

const CELL = 24;

export class CollisionWorld {
  constructor(map) {
    this.map = map;
    this.cells = new Map();
    this.boxes = [];
    this.circles = [];
    this.stamp = 0;
    for (const c of map.colliders) this.addBox(c);
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
      if (o.kind === 'box') {
        if (y + h < o.minY || y > o.maxY - 0.05) continue;
        if (o.maxY - y < 0.45 && o.type !== 'building') continue; // can step over low stuff
        const cx = clamp(out.x, o.minX, o.maxX), cz = clamp(out.z, o.minZ, o.maxZ);
        let dx = out.x - cx, dz = out.z - cz;
        const d2 = dx * dx + dz * dz;
        if (d2 < r * r) {
          if (d2 > 1e-8) {
            const d = Math.sqrt(d2), push = r - d;
            out.x += dx / d * push; out.z += dz / d * push;
          } else {
            // inside box: push out along smallest axis
            const l = out.x - o.minX, rr = o.maxX - out.x, t = out.z - o.minZ, bb = o.maxZ - out.z;
            const m = Math.min(l, rr, t, bb);
            if (m === l) out.x = o.minX - r; else if (m === rr) out.x = o.maxX + r; else if (m === t) out.z = o.minZ - r; else out.z = o.maxZ + r;
          }
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

  // Oriented box (vehicle) vs statics. Returns list of contacts {nx,nz,depth,px,pz,obj}.
  obbContacts(cx, cz, yaw, hx, hz, y, out) {
    out.length = 0;
    const c = Math.cos(yaw), s = Math.sin(yaw);
    // axes: forward f = (s, c), right r = (c, -s)
    const fx = s, fz = c, rx = c, rz = -s;
    const ex = Math.abs(rx) * hx + Math.abs(fx) * hz, ez = Math.abs(rz) * hx + Math.abs(fz) * hz;
    const list = this.query(cx - ex - 1, cz - ez - 1, cx + ex + 1, cz + ez + 1, _tmpList);
    for (const o of list) {
      if (o.kind === 'box') {
        if (y + 1.2 < o.minY || y + 0.25 > o.maxY) continue;
        // SAT between OBB and AABB in 2D
        const bx = (o.minX + o.maxX) / 2, bz = (o.minZ + o.maxZ) / 2;
        const bhx = (o.maxX - o.minX) / 2, bhz = (o.maxZ - o.minZ) / 2;
        const dx = bx - cx, dz = bz - cz;
        const axes = [[1, 0], [0, 1], [rx, rz], [fx, fz]];
        let minOverlap = Infinity, nx = 0, nz = 0;
        let sep = false;
        for (const [ax, az] of axes) {
          const ra = Math.abs(rx * ax + rz * az) * hx + Math.abs(fx * ax + fz * az) * hz;
          const rb = Math.abs(ax) * bhx + Math.abs(az) * bhz;
          const dist = dx * ax + dz * az;
          const ov = ra + rb - Math.abs(dist);
          if (ov <= 0) { sep = true; break; }
          if (ov < minOverlap) { minOverlap = ov; const sgn = dist > 0 ? -1 : 1; nx = ax * sgn; nz = az * sgn; }
        }
        if (sep) continue;
        // contact point: deepest vehicle corner along -n
        let best = -Infinity, px = cx, pz = cz;
        for (const [sx, sz] of CORNERS) {
          const qx = cx + rx * hx * sx + fx * hz * sz, qz = cz + rz * hx * sx + fz * hz * sz;
          const d = -(qx * nx + qz * nz);
          if (d > best) { best = d; px = qx; pz = qz; }
        }
        // clamp contact to the box
        px = clamp(px, o.minX, o.maxX); pz = clamp(pz, o.minZ, o.maxZ);
        out.push({ nx, nz, depth: minOverlap, px, pz, obj: o });
      } else if (!o.broken) {
        if (y > (o.h || 3)) continue;
        // circle vs OBB
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

  // Raycast against static boxes, circles and the ground. dir must be normalized.
  raycast(ox, oy, oz, dx, dy, dz, maxDist, opts = {}) {
    let bestT = maxDist, bestObj = null, nX = 0, nY = 0, nZ = 0;
    // ground via marching
    const step = 2;
    let prevT = 0;
    for (let t = 0; t <= Math.min(maxDist, 400); t += step) {
      const px = ox + dx * t, py = oy + dy * t, pz = oz + dz * t;
      const gh = this.map.groundHeight(px, pz);
      if (py < gh) {
        // refine
        let a = prevT, b = t;
        for (let k = 0; k < 8; k++) { const m = (a + b) / 2; const my = oy + dy * m; if (my < this.map.groundHeight(ox + dx * m, oz + dz * m)) b = m; else a = m; }
        if (b < bestT) { bestT = b; bestObj = 'ground'; nX = 0; nY = 1; nZ = 0; }
        break;
      }
      prevT = t;
    }
    // DDA through grid cells for boxes/circles
    const ex = ox + dx * bestT, ez = oz + dz * bestT;
    const list = this.query(Math.min(ox, ex), Math.min(oz, ez), Math.max(ox, ex), Math.max(oz, ez), _tmpList);
    // For long rays the AABB query can be big; acceptable for our ranges (<= 300m)
    for (const o of list) {
      if (o.kind === 'box') {
        if (opts.ignoreSoft && o.soft) continue;
        const t = rayBox(ox, oy, oz, dx, dy, dz, o.minX, o.minY, o.minZ, o.maxX, o.maxY, o.maxZ);
        if (t >= 0 && t < bestT) { bestT = t; bestObj = o; const n = _n; nX = n[0]; nY = n[1]; nZ = n[2]; }
      } else if (!o.broken && !opts.ignoreProps) {
        // vertical cylinder
        const lx = ox - o.x, lz = oz - o.z;
        const a = dx * dx + dz * dz; if (a < 1e-8) continue;
        const b = 2 * (lx * dx + lz * dz), c = lx * lx + lz * lz - o.r * o.r;
        const disc = b * b - 4 * a * c; if (disc < 0) continue;
        const t = (-b - Math.sqrt(disc)) / (2 * a);
        if (t >= 0 && t < bestT) {
          const hy = oy + dy * t;
          if (hy < (o.h || 3) && hy > 0) { bestT = t; bestObj = o; const hx = ox + dx * t - o.x, hz = oz + dz * t - o.z, hl = Math.hypot(hx, hz) || 1; nX = hx / hl; nY = 0; nZ = hz / hl; }
        }
      }
    }
    _tmpList.length = 0;
    if (!bestObj) return null;
    return { t: bestT, x: ox + dx * bestT, y: oy + dy * bestT, z: oz + dz * bestT, nx: nX, ny: nY, nz: nZ, obj: bestObj };
  }

  // Highest walkable surface under (x,z) that is not above y + step (terrain, curbs, roofs, containers)
  floorHeight(x, z, y, step = 0.55) {
    let best = this.map.groundHeight(x, z);
    if (y - best < 0.3) return best;
    const arr = this.cells.get(this._key(Math.floor(x / CELL), Math.floor(z / CELL)));
    if (!arr) return best;
    for (const o of arr) {
      if (o.kind !== 'box' || o.removed) continue;
      if (x < o.minX || x > o.maxX || z < o.minZ || z > o.maxZ) continue;
      if (o.maxY <= y + step && o.maxY > best) best = o.maxY;
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

const CORNERS = [[1, 1], [1, -1], [-1, 1], [-1, -1]];
const _tmpList = [];
const _n = [0, 0, 0];
function rayBox(ox, oy, oz, dx, dy, dz, minX, minY, minZ, maxX, maxY, maxZ) {
  let tmin = -Infinity, tmax = Infinity, axis = -1;
  // x
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
