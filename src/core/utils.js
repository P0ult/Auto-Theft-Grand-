// Shared math helpers, seeded RNG and noise.
import * as THREE from 'three';

export const TAU = Math.PI * 2;
export const DEG = Math.PI / 180;

export const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
export const lerp = (a, b, t) => a + (b - a) * t;
export const invLerp = (a, b, v) => clamp((v - a) / (b - a), 0, 1);
export const smoothstep = (a, b, v) => { const t = invLerp(a, b, v); return t * t * (3 - 2 * t); };
export const damp = (a, b, lambda, dt) => lerp(a, b, 1 - Math.exp(-lambda * dt));
export const sign = (v) => (v < 0 ? -1 : 1);
export const wrapAngle = (a) => { while (a > Math.PI) a -= TAU; while (a < -Math.PI) a += TAU; return a; };
export const angleLerp = (a, b, t) => a + wrapAngle(b - a) * t;
export const dampAngle = (a, b, lambda, dt) => a + wrapAngle(b - a) * (1 - Math.exp(-lambda * dt));
export const dist2 = (ax, az, bx, bz) => { const dx = ax - bx, dz = az - bz; return dx * dx + dz * dz; };
export const dist = (ax, az, bx, bz) => Math.sqrt(dist2(ax, az, bx, bz));

// Mulberry32 seeded random.
export class RNG {
  constructor(seed = 1) { this.s = seed >>> 0; }
  next() {
    let t = (this.s += 0x6d2b79f5);
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  }
  range(a, b) { return a + (b - a) * this.next(); }
  int(a, b) { return Math.floor(this.range(a, b + 1)); }
  pick(arr) { return arr[Math.floor(this.next() * arr.length)]; }
  chance(p) { return this.next() < p; }
  weighted(entries) { // [[item, weight], ...]
    let total = 0; for (const e of entries) total += e[1];
    let r = this.next() * total;
    for (const e of entries) { r -= e[1]; if (r <= 0) return e[0]; }
    return entries[entries.length - 1][0];
  }
}

export const rng = new RNG(Date.now() & 0xffffffff);
export const rand = (a = 0, b = 1) => a + (b - a) * Math.random();
export const randInt = (a, b) => Math.floor(rand(a, b + 1));
export const pick = (arr) => arr[Math.floor(Math.random() * arr.length)];

export function hash2(x, y) {
  let h = Math.imul(x | 0, 374761393) + Math.imul(y | 0, 668265263);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}

// Value noise + fbm (deterministic)
function vnoise(x, y) {
  const xi = Math.floor(x), yi = Math.floor(y);
  const xf = x - xi, yf = y - yi;
  const u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
  const a = hash2(xi, yi), b = hash2(xi + 1, yi), c = hash2(xi, yi + 1), d = hash2(xi + 1, yi + 1);
  return lerp(lerp(a, b, u), lerp(c, d, u), v);
}
export function fbm(x, y, oct = 4) {
  let s = 0, a = 0.5, f = 1, n = 0;
  for (let i = 0; i < oct; i++) { s += a * vnoise(x * f, y * f); n += a; a *= 0.5; f *= 2.03; }
  return s / n;
}
export const noise2 = vnoise;

// Segment/ray helpers
export function raySphere(ox, oy, oz, dx, dy, dz, cx, cy, cz, r) {
  const lx = ox - cx, ly = oy - cy, lz = oz - cz;
  const b = lx * dx + ly * dy + lz * dz;
  const c = lx * lx + ly * ly + lz * lz - r * r;
  const h = b * b - c;
  if (h < 0) return -1;
  const t = -b - Math.sqrt(h);
  return t >= 0 ? t : (c < 0 ? 0 : -1);
}

// Ray vs axis-aligned box. Returns entry t or -1; writes normal into outN if provided.
export function rayAABB(ox, oy, oz, dx, dy, dz, minX, minY, minZ, maxX, maxY, maxZ, outN) {
  let tmin = -Infinity, tmax = Infinity, axis = -1;
  const o = [ox, oy, oz], d = [dx, dy, dz], mn = [minX, minY, minZ], mx = [maxX, maxY, maxZ];
  for (let i = 0; i < 3; i++) {
    if (Math.abs(d[i]) < 1e-9) {
      if (o[i] < mn[i] || o[i] > mx[i]) return -1;
    } else {
      let t1 = (mn[i] - o[i]) / d[i], t2 = (mx[i] - o[i]) / d[i];
      if (t1 > t2) { const tt = t1; t1 = t2; t2 = tt; }
      if (t1 > tmin) { tmin = t1; axis = i; }
      if (t2 < tmax) tmax = t2;
      if (tmin > tmax) return -1;
    }
  }
  if (tmax < 0) return -1;
  if (outN) { outN.set(0, 0, 0); if (axis >= 0) outN.setComponent(axis, d[axis] > 0 ? -1 : 1); }
  return tmin >= 0 ? tmin : 0;
}

// Ray vs oriented box (center c, half extents h, rotation yaw about Y + optional pitch/roll ignored)
const _tmpV = new THREE.Vector3();
export function rayOBBYaw(ox, oy, oz, dx, dy, dz, cx, cy, cz, yaw, hx, hy, hz, outN) {
  const cs = Math.cos(-yaw), sn = Math.sin(-yaw);
  const lx = ox - cx, lz = oz - cz;
  const rox = lx * cs + lz * sn, roz = -lx * sn + lz * cs;
  const rdx = dx * cs + dz * sn, rdz = -dx * sn + dz * cs;
  const t = rayAABB(rox, oy - cy, roz, rdx, dy, rdz, -hx, -hy, -hz, hx, hy, hz, outN ? _tmpV : null);
  if (t >= 0 && outN) {
    // rotate normal back to world
    const c2 = Math.cos(yaw), s2 = Math.sin(yaw);
    outN.set(_tmpV.x * c2 + _tmpV.z * s2, _tmpV.y, -_tmpV.x * s2 + _tmpV.z * c2);
  }
  return t;
}

export function formatMoney(n) {
  const s = Math.max(0, Math.floor(Math.abs(n))).toString().padStart(8, '0');
  return (n < 0 ? '-$' : '$') + s;
}

export function once(fn) { let done = false; return (...a) => { if (!done) { done = true; fn(...a); } }; }
