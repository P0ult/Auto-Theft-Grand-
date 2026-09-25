// San Andreas (the state): the big picture around Los Soles. World bounds, regions (countryside,
// forest mountains, desert), towns, the military base, the river, and the terrain height function.
// The terrain is baked into a Heightfield (4 m grid) that physics and rendering both sample, so the
// ground you see is exactly the ground you drive on.
import { clamp, smoothstep, lerp, hash2 } from '../core/utils.js';
import { catmull, cumLen, solveProfile } from './roadnet.js';

export const WORLD = { minX: -5600, maxX: 1600, minZ: -5400, maxZ: 1400 };
export const HF_STEP = 4;

// City rectangle (kept in sync with citymap.js: XS/ZS extremes +- half road)
export const CITY_RECT = { minX: -840, maxX: 880, minZ: -780, maxZ: 635 };

export const TOWNS = {
  fern: { name: 'Fern Creek', x: -2520, z: -240, r: 330 },
  pine: { name: 'Pine Hollow', x: -300, z: -2330, r: 240 },
  dry: { name: 'Dry Wells', x: -3860, z: -2480, r: 300 },
};
export const BASE = { name: 'Fort Carver', minX: -5320, maxX: -3820, minZ: -4760, maxZ: -3860, gateZ: -4060 };
export const AIRFIELD = { name: 'Fern Creek Airfield', x: -2960, z: 330, len: 560, yaw: Math.PI / 2 };
export const RIVER = [[-1640, -1720], [-1760, -1400], [-1830, -1100], [-1960, -800], [-2010, -560], [-2140, -330], [-2130, -80], [-2050, 150], [-2110, 400], [-2190, 640], [-2230, 900], [-2260, 1400]];
export const MESAS = [
  [-4400, -1900, 150, 70], [-4900, -2600, 210, 95], [-4300, -3150, 120, 60], [-3350, -3500, 170, 80], [-5100, -1500, 180, 85],
  [-3000, -2900, 110, 55], [-4700, -3400, 90, 70], [-3450, -1750, 90, 45], [-5200, -3500, 140, 90], [-2800, -4300, 200, 100],
  [-3500, -4600, 150, 70], [-2600, -3700, 120, 60],
];
export const LAKE = { x: -1500, z: -1900, r: 210, y: 64 };

// ------------------------------------------------------------------ noise (fast, deterministic)
// gradient (Perlin) noise with a hashed gradient table, in roughly [-0.7, 0.7]
const GX = new Float32Array(256), GY = new Float32Array(256);
for (let i = 0; i < 256; i++) { const a = hash2(i, 911) * Math.PI * 2; GX[i] = Math.cos(a); GY[i] = Math.sin(a); }
function gh(ix, iy) { let h = Math.imul(ix, 374761393) + Math.imul(iy, 668265263); h = Math.imul(h ^ (h >>> 13), 1274126177); return (h ^ (h >>> 16)) & 255; }
export function pnoise(x, y) {
  const xi = Math.floor(x), yi = Math.floor(y), xf = x - xi, yf = y - yi;
  const u = xf * xf * xf * (xf * (xf * 6 - 15) + 10), v = yf * yf * yf * (yf * (yf * 6 - 15) + 10);
  let g = gh(xi, yi); const a = GX[g] * xf + GY[g] * yf;
  g = gh(xi + 1, yi); const b = GX[g] * (xf - 1) + GY[g] * yf;
  g = gh(xi, yi + 1); const c = GX[g] * xf + GY[g] * (yf - 1);
  g = gh(xi + 1, yi + 1); const d = GX[g] * (xf - 1) + GY[g] * (yf - 1);
  const ab = a + (b - a) * u, cd = c + (d - c) * u;
  return ab + (cd - ab) * v;
}
// fbm in [0,1] (rotated octaves hide the grid)
export function fbmN(x, y, oct) {
  let s = 0, a = 0.5, n = 0;
  for (let i = 0; i < oct; i++) {
    s += a * pnoise(x, y); n += a; a *= 0.5;
    const nx = (x * 1.6 - y * 1.2) * 1.02 + 5.2, ny = (x * 1.2 + y * 1.6) * 1.02 - 1.7; x = nx; y = ny;
  }
  return clamp(s / n * 0.9 + 0.5, 0, 1);
}
export function ridged(x, y, oct) {
  let s = 0, a = 0.5, n = 0, w = 1;
  for (let i = 0; i < oct; i++) {
    let r = 1 - Math.abs(pnoise(x, y) * 1.5);
    r = r * r * w; w = clamp(r * 1.8, 0, 1);
    s += a * r; n += a; a *= 0.5;
    const nx = (x * 1.6 - y * 1.2) * 1.05 + 3.1, ny = (x * 1.2 + y * 1.6) * 1.05 + 7.3; x = nx; y = ny;
  }
  return s / n;
}

// distance from a point to the city rectangle (0 inside)
export function cityDist(x, z) {
  const dx = Math.max(CITY_RECT.minX - x, 0, x - CITY_RECT.maxX);
  const dz = Math.max(CITY_RECT.minZ - z, 0, z - CITY_RECT.maxZ);
  return Math.hypot(dx, dz);
}
function segDist(px, pz, ax, az, bx, bz) {
  const dx = bx - ax, dz = bz - az;
  const t = clamp(((px - ax) * dx + (pz - az) * dz) / (dx * dx + dz * dz || 1), 0, 1);
  return Math.hypot(px - ax - dx * t, pz - az - dz * t);
}
export function riverDist(x, z) {
  let d = Infinity;
  for (let i = 0; i < RIVER.length - 1; i++) d = Math.min(d, segDist(x, z, RIVER[i][0], RIVER[i][1], RIVER[i + 1][0], RIVER[i + 1][1]));
  return d;
}

// south / east coastlines outside the city's own coast
export function coastZ(x) { return 700 + 120 * Math.sin(x * 0.0011 + 0.6) + 80 * (fbmN(x * 0.0021, 3.3, 3) - 0.5) * 2; }
export function coastX(z) { return 1180 + 110 * Math.sin(z * 0.0013) + 60 * (fbmN(7.1, z * 0.002, 3) - 0.5) * 2; }

// ------------------------------------------------------------------ region weights
export function regionWeights(x, z) {
  // domain-warped borders so the regions don't meet along straight lines
  const wx = x + (fbmN(x * 0.0009, z * 0.0009, 2) - 0.5) * 700, wz = z + (fbmN(x * 0.0009 + 9, z * 0.0009 - 4, 2) - 0.5) * 700;
  const d = clamp(smoothstep(-2750, -3350, wx) + smoothstep(-3200, -3800, wz) * smoothstep(-1900, -2500, wx), 0, 1);
  const m = clamp(smoothstep(-1650, -2300, wz) * (1 - smoothstep(-2300, -2900, wx)), 0, 1) * (1 - d);
  return { desert: d, mountain: m, country: Math.max(0, 1 - d - m) };
}
export function biomeAt(x, z) {
  const w = regionWeights(x, z);
  if (w.desert > 0.5) return 'desert';
  if (w.mountain > 0.5) return 'forest';
  if (cityDist(x, z) < 1) return 'city';
  return 'country';
}

// ------------------------------------------------------------------ the old (hand-tuned) coast by the city
function cityCoast(x, z, h) {
  const eW = CITY_RECT.minX - 6, eE = CITY_RECT.maxX + 6, eS = CITY_RECT.maxZ + 6;
  if (z > eS && x > -1100 && x < 1600) {
    const d = z - eS;
    if (x < 470) {
      const beach = -d * 0.012 - Math.max(0, d - 70) * 0.05;
      const k = smoothstep(-1100, -850, x); // blend into the western coast
      h = z > eS + 2 ? lerp(h, Math.min(h, beach), k) : h;
    } else {
      h = d < 34 ? 0 : -9;
    }
  }
  if (x > eE && z > -250) { const d = x - eE; if (d > 36) h = Math.min(h, -9); else if (z > -250) h = Math.min(h, 0.0); }
  return h;
}

// ------------------------------------------------------------------ land height (before roads)
export function landHeight(x, z) {
  const dC = cityDist(x, z);
  if (dC <= 6) return cityCoast(x, z, 0);
  const W = regionWeights(x, z);
  let h = 0;
  if (W.country > 0.001) {
    // rolling farmland and oak hills
    const c = 18 + (fbmN(x * 0.0009 + 3, z * 0.0009 - 7, 4) - 0.5) * 60 + (fbmN(x * 0.004, z * 0.004, 2) - 0.5) * 8;
    h += c * W.country;
  }
  if (W.mountain > 0.001) {
    // forested mountains with Mount Cedro
    const r = ridged(x * 0.0012, z * 0.0012, 5);
    let m = 55 + r * 300 * (0.5 + 0.7 * fbmN(x * 0.0005, z * 0.0005, 2)) + (fbmN(x * 0.003, z * 0.003, 3) - 0.5) * 30;
    const pk = Math.hypot(x + 900, z + 3250);
    m += 320 * Math.exp(-(pk * pk) / (800 * 800));
    h += m * W.mountain;
  }
  if (W.desert > 0.001) {
    let d = 44 + (fbmN(x * 0.0008 - 11, z * 0.0008 + 4, 3) - 0.5) * 30;
    // dune ripples in the deep desert
    d += Math.sin(x * 0.018 + Math.sin(z * 0.011) * 2.4 + pnoise(x * 0.004, z * 0.004) * 3) * 2.2 * smoothstep(-3800, -4800, x);
    for (const [mx, mz, mr, mh] of MESAS) {
      const dd0 = Math.hypot(x - mx, z - mz);
      if (dd0 > mr * 1.5 + 80) continue;
      // irregular outline
      const wob = mr * (1 + 0.35 * pnoise(x * 0.008 + mx * 0.01, z * 0.008));
      const t = smoothstep(wob + 45, wob - 8, dd0);
      const terr = Math.floor(t * 3 + 0.2) / 3;
      d += mh * lerp(t * t, terr, 0.6) * (0.92 + 0.08 * fbmN(x * 0.03, z * 0.03, 2));
    }
    h += d * W.desert;
  }

  // hills ringing the city: Vistawood (north), Red Canyon (west), coastal hills (north-east)
  if (dC < 1250) h = lerp(cityRing(x, z, dC), h, smoothstep(420, 1150, dC));

  // lake up in the hills: a basin around LAKE.y (water plane added by the renderer)
  const dl = Math.hypot(x - LAKE.x, z - LAKE.z);
  if (dl < LAKE.r + 300) {
    const shore = lerp(LAKE.y + 2.5, h, smoothstep(LAKE.r + 20, LAKE.r + 300, dl));
    h = dl < LAKE.r ? LAKE.y - 1.5 - 9 * smoothstep(LAKE.r, LAKE.r * 0.3, dl) : shore;
  }

  // river canyon down to the sea
  const dr = riverDist(x, z);
  if (dr < 150) {
    const bed = -2.2 - 1.6 * smoothstep(16, 0, dr);
    const k = smoothstep(12, 150, dr);
    h = lerp(bed, h, k * k * (3 - 2 * k));
  }

  // coasts (outside the hand-made city coast)
  if (x < -1000) {
    const cz = coastZ(x);
    if (z > cz - 220) {
      const t = smoothstep(cz - 220, cz, z);
      const cliff = fbmN(x * 0.004, 5.5, 2) > 0.58;
      h = lerp(h, cliff ? Math.max(h * 0.8, 8) : 1.2, t);
      if (z > cz) h = Math.min(h, 1.0 - (z - cz) * (cliff ? 0.4 : 0.045));
    }
  }
  if (z < -250) {
    const cx = coastX(z);
    if (x > cx - 220) {
      const t = smoothstep(cx - 220, cx, x);
      h = lerp(h, 1.0, t);
      if (x > cx) h = Math.min(h, 1.0 - (x - cx) * 0.06);
    }
  }
  h = cityCoast(x, z, h);
  // world edges rise into impassable ridges on the land sides
  const eWd = x - WORLD.minX, eNd = z - WORLD.minZ;
  if (eWd < 450) h += (1 - eWd / 450) ** 2 * 360;
  if (eNd < 450) h += (1 - eNd / 450) ** 2 * 360;
  return Math.max(h, -40);
}

function cityRing(x, z, dC) {
  let h = 5 * smoothstep(0, 50, dC);
  const n = fbmN(x * 0.004, z * 0.004, 5);
  if (z < CITY_RECT.minZ - 6) { // Vistawood Hills / Mount Vista
    const d = CITY_RECT.minZ - 6 - z;
    h = Math.max(h, 5 * smoothstep(0, 50, d) + Math.min(170, Math.max(0, d - 30) * 0.22) + n * 75 * smoothstep(40, 260, d));
  }
  if (x < CITY_RECT.minX - 6) { // Red Canyon, with a pass for the freeway around z = 0
    const d = CITY_RECT.minX - 6 - x;
    const fade = 1 - smoothstep(420, 620, z);
    const pass = 1 - 0.78 * Math.exp(-((z + 40) ** 2) / (170 * 170));
    h = Math.max(h, (5 * smoothstep(0, 50, d) + Math.min(130, Math.max(0, d - 30) * 0.2) + n * 65 * smoothstep(40, 260, d)) * fade * pass);
  }
  if (x > CITY_RECT.maxX + 6 && z < -250) {
    const d = x - CITY_RECT.maxX - 6;
    const fade = smoothstep(-250, -420, z);
    h = Math.max(h, (5 * smoothstep(0, 50, d) + Math.min(140, Math.max(0, d - 30) * 0.22) + n * 70 * smoothstep(40, 260, d)) * fade);
  }
  return h;
}

// ------------------------------------------------------------------ heightfield
export class Heightfield {
  constructor(bounds = WORLD, step = HF_STEP) {
    this.minX = bounds.minX; this.minZ = bounds.minZ;
    this.step = step;
    this.nx = Math.round((bounds.maxX - bounds.minX) / step) + 1;
    this.nz = Math.round((bounds.maxZ - bounds.minZ) / step) + 1;
    this.h = new Float32Array(this.nx * this.nz);
  }

  // Coarse evaluation of the land function, bilinearly upsampled to the full grid (fast + smooth)
  generate(fn = landHeight, coarse = 2, pads = []) {
    const cs = this.step * coarse;
    const cnx = Math.ceil((this.nx - 1) / coarse) + 1, cnz = Math.ceil((this.nz - 1) / coarse) + 1;
    const c = new Float32Array(cnx * cnz);
    for (let j = 0; j < cnz; j++) {
      const z = this.minZ + j * cs;
      for (let i = 0; i < cnx; i++) c[j * cnx + i] = fn(this.minX + i * cs, z);
    }
    const h = this.h;
    for (let j = 0; j < this.nz; j++) {
      const fj = j / coarse, j0 = Math.min(cnz - 2, Math.floor(fj)), tj = fj - j0;
      for (let i = 0; i < this.nx; i++) {
        const fi = i / coarse, i0 = Math.min(cnx - 2, Math.floor(fi)), ti = fi - i0;
        const a = c[j0 * cnx + i0], b = c[j0 * cnx + i0 + 1], cc = c[(j0 + 1) * cnx + i0], d = c[(j0 + 1) * cnx + i0 + 1];
        h[j * this.nx + i] = a + (b - a) * ti + (cc - a) * tj + (a - b - cc + d) * ti * tj;
      }
    }
    // the river channel at full resolution (the coarse pass is too blurry for a 30 m wide river)
    for (let i = 0; i < RIVER.length - 1; i++) {
      const [ax, az] = RIVER[i], [bx, bz] = RIVER[i + 1];
      this.forRect(Math.min(ax, bx) - 60, Math.min(az, bz) - 60, Math.max(ax, bx) + 60, Math.max(az, bz) + 60, (k, x, z) => {
        const d = riverDist(x, z);
        if (d > 55) return;
        const bed = -2.4 - 1.4 * smoothstep(18, 0, d);
        const t = smoothstep(16, 55, d);
        h[k] = Math.min(h[k], lerp(bed, Math.max(h[k], 1.5), t));
      });
    }
    // the city itself must be exactly flat
    this.forRect(CITY_RECT.minX - 2, CITY_RECT.minZ - 2, CITY_RECT.maxX + 2, CITY_RECT.maxZ + 2, (k, x, z) => { if (cityDist(x, z) < 1) h[k] = cityCoast(x, z, 0); });
    for (const p of pads) this.pad(p);
    return this;
  }

  // Carve valleys / passes along planned road routes: the land is lowered toward a smooth, grade-limited
  // profile of the route with wide, natural side slopes (so roads follow the land instead of trenches).
  carveRoutes(routes, endY) {
    const h = this.h;
    for (const key in routes) {
      const R = routes[key];
      if (R.type === 'dirt') continue;
      const pts = catmull(R.ctrl, 24);
      const e0 = R.ends?.[0], e1 = R.ends?.[1];
      const y0 = e0 == null ? null : typeof e0 === 'number' ? e0 : endY(e0);
      const y1 = e1 == null ? null : typeof e1 === 'number' ? e1 : endY(e1);
      const grade = R.grade ?? (R.type === 'road' ? 0.08 : 0.06);
      const prof = solveProfile(pts, (x, z) => this.sample(x, z), { window: 420, maxGrade: grade, maxCut: 400, y0, y1, minY: 1.5 });
      const core = (R.width ?? 18) + 10, fall = 190;
      for (let i = 0; i < pts.length - 1; i++) {
        const ax = pts[i][0], az = pts[i][1], bx = pts[i + 1][0], bz = pts[i + 1][1];
        const ya = prof.y[i], yb = prof.y[i + 1];
        const dx = bx - ax, dz = bz - az, L2 = dx * dx + dz * dz || 1;
        const Rr = core + fall;
        this.forRect(Math.min(ax, bx) - Rr, Math.min(az, bz) - Rr, Math.max(ax, bx) + Rr, Math.max(az, bz) + Rr, (k, x, z) => {
          if (cityDist(x, z) < 2) return;
          const t = clamp(((x - ax) * dx + (z - az) * dz) / L2, 0, 1);
          const d = Math.hypot(x - ax - dx * t, z - az - dz * t);
          if (d > Rr) return;
          const ty = ya + (yb - ya) * t + 0.5;
          if (h[k] <= ty) return;
          const w = d <= core ? 1 : 1 - smoothstep(core, Rr, d);
          h[k] = h[k] + (ty - h[k]) * (w * w * (3 - 2 * w));
        });
      }
    }
  }

  idx(i, j) { return j * this.nx + i; }
  forRect(x0, z0, x1, z1, cb) {
    const i0 = clamp(Math.floor((x0 - this.minX) / this.step), 0, this.nx - 1), i1 = clamp(Math.ceil((x1 - this.minX) / this.step), 0, this.nx - 1);
    const j0 = clamp(Math.floor((z0 - this.minZ) / this.step), 0, this.nz - 1), j1 = clamp(Math.ceil((z1 - this.minZ) / this.step), 0, this.nz - 1);
    for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) cb(j * this.nx + i, this.minX + i * this.step, this.minZ + j * this.step);
  }

  // flatten a round or rectangular pad to height y with a soft skirt
  pad(p) {
    const h = this.h;
    const blend = p.blend ?? 60;
    if (p.r) {
      const y = p.y ?? this.sample(p.x, p.z);
      this.forRect(p.x - p.r - blend, p.z - p.r - blend, p.x + p.r + blend, p.z + p.r + blend, (k, x, z) => {
        const d = Math.hypot(x - p.x, z - p.z);
        const t = smoothstep(p.r + blend, p.r, d);
        h[k] = lerp(h[k], y, t);
      });
      p.y = y;
    } else {
      const y = p.y ?? this.sample((p.minX + p.maxX) / 2, (p.minZ + p.maxZ) / 2);
      this.forRect(p.minX - blend, p.minZ - blend, p.maxX + blend, p.maxZ + blend, (k, x, z) => {
        const dx = Math.max(p.minX - x, 0, x - p.maxX), dz = Math.max(p.minZ - z, 0, z - p.maxZ);
        const t = smoothstep(blend, 0, Math.hypot(dx, dz));
        h[k] = lerp(h[k], y, t);
      });
      p.y = y;
    }
    return p.y;
  }

  sample(x, z) {
    let fx = (x - this.minX) / this.step, fz = (z - this.minZ) / this.step;
    if (fx < 0) fx = 0; else if (fx > this.nx - 1.001) fx = this.nx - 1.001;
    if (fz < 0) fz = 0; else if (fz > this.nz - 1.001) fz = this.nz - 1.001;
    const i = fx | 0, j = fz | 0, tx = fx - i, tz = fz - j;
    const k = j * this.nx + i, h = this.h;
    const a = h[k], b = h[k + 1], c = h[k + this.nx], d = h[k + this.nx + 1];
    return a + (b - a) * tx + (c - a) * tz + (a - b - c + d) * tx * tz;
  }
  normal(x, z, out = [0, 1, 0]) {
    const e = this.step;
    const dx = this.sample(x + e, z) - this.sample(x - e, z), dz = this.sample(x, z + e) - this.sample(x, z - e);
    const l = Math.hypot(dx, 2 * e, dz);
    out[0] = -dx / l; out[1] = 2 * e / l; out[2] = -dz / l;
    return out;
  }
}

// deterministic scatter helper: jittered grid points inside a rect, filtered by a density callback
export function scatter(x0, z0, x1, z1, spacing, seed, density) {
  const out = [];
  for (let gz = Math.floor(z0 / spacing); gz <= Math.floor(z1 / spacing); gz++) {
    for (let gx = Math.floor(x0 / spacing); gx <= Math.floor(x1 / spacing); gx++) {
      const r1 = hash2(gx * 7 + seed, gz * 13 - seed), r2 = hash2(gx * 11 - seed, gz * 5 + seed * 3), r3 = hash2(gx + seed * 7, gz - seed);
      const x = (gx + r1) * spacing, z = (gz + r2) * spacing;
      if (x < x0 || x > x1 || z < z0 || z > z1) continue;
      if (density(x, z) > r3) out.push([x, z, r1, r2, r3]);
    }
  }
  return out;
}
