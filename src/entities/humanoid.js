// Procedural rigid-skinned humanoid: one SkinnedMesh (single draw call) per character.
import * as THREE from 'three';
import { std } from '../render/materials.js';
import { RNG } from '../core/utils.js';

export const BONE_NAMES = ['hips', 'spine', 'chest', 'neck', 'head', 'lUpperArm', 'lForearm', 'lHand', 'rUpperArm', 'rForearm', 'rHand', 'lThigh', 'lShin', 'lFoot', 'rThigh', 'rShin', 'rFoot'];
export const B = Object.fromEntries(BONE_NAMES.map((n, i) => [n, i]));
const PARENT = [-1, 0, 1, 2, 3, 2, 5, 6, 2, 8, 9, 0, 11, 12, 0, 14, 15];

// rest offsets (relative to parent) for a 1.8m person
function restOffsets(a) {
  const sw = a.female ? 0.17 : 0.2 * a.build;
  const hw = a.female ? 0.105 : 0.095;
  return [
    [0, 0.98, 0], [0, 0.12, 0], [0, 0.2, 0], [0, 0.24, 0], [0, 0.09, 0],
    [sw, 0.19, 0], [0, -0.29, 0], [0, -0.26, 0],
    [-sw, 0.19, 0], [0, -0.29, 0], [0, -0.26, 0],
    [hw, -0.06, 0], [0, -0.44, 0], [0, -0.42, 0],
    [-hw, -0.06, 0], [0, -0.44, 0], [0, -0.42, 0],
  ];
}

const SKIN = [0xf1c7a5, 0xe0ac87, 0xc68863, 0xa86b4a, 0x8a5536, 0x5f3a24, 0x3f2618];
const HAIR = [0x111111, 0x2b1a10, 0x4a2c16, 0x7a5230, 0xb08a52, 0x999999, 0x1a1a1a];
const SHIRT = [0xffffff, 0x222222, 0x8b1e1e, 0x1e3f8b, 0x2e7d32, 0xd4a017, 0x7b1fa2, 0x607d8b, 0xe57373, 0x4db6ac, 0xff8f00, 0x455a64, 0xc2b280, 0x3949ab];
const PANTS = [0x1f2a44, 0x2b2b2b, 0x4e4a45, 0x6d5c43, 0x1d3b5c, 0x8d8d8d, 0x3b2f2a, 0xc9b99a];

export function randomAppearance(rng = new RNG((Math.random() * 1e9) | 0), opts = {}) {
  const female = opts.female ?? rng.chance(0.42);
  const a = {
    female,
    skin: opts.skin ?? rng.pick(SKIN),
    hair: rng.pick(HAIR),
    hairStyle: female ? rng.pick(['long', 'ponytail', 'bun', 'long', 'short']) : rng.pick(['short', 'short', 'bald', 'afro', 'cap', 'buzz']),
    shirt: rng.pick(SHIRT),
    shirtType: rng.pick(['tee', 'tee', 'long', 'tank', 'jacket']),
    pants: rng.pick(PANTS),
    shorts: rng.chance(0.2),
    shoes: rng.pick([0x111111, 0xeeeeee, 0x5a3a22, 0x333333, 0x8b0000]),
    hat: null,
    build: female ? rng.range(0.9, 1.0) : rng.range(0.95, 1.18),
    height: female ? rng.range(0.92, 1.0) : rng.range(0.96, 1.07),
    glasses: rng.chance(0.15),
    beard: !female && rng.chance(0.2),
    jacketColor: rng.pick(SHIRT),
    bandana: null,
    uniform: null,
  };
  if (a.hairStyle === 'cap') a.hat = rng.pick(SHIRT);
  Object.assign(a, opts);
  return a;
}

function col(hex) { const c = new THREE.Color(hex); return [c.r, c.g, c.b]; }

// Material ids (per vertex): drive roughness and fabric detail in the character shader
const MAT = { cloth: 0, skin: 1, hair: 2, leather: 3, eye: 4, denim: 5, metal: 6 };

class SkinBuilder {
  constructor() { this.pos = []; this.nor = []; this.col = []; this.mat = []; this.si = []; this.sw = []; this.idx = []; this.n = 0; }
  // rigid part on one bone
  add(geo, matrix, color, bone, mat = MAT.cloth) {
    const g = geo.index ? geo.toNonIndexed() : geo;
    const p = g.attributes.position, nn = g.attributes.normal;
    const v = new THREE.Vector3(), n = new THREE.Vector3();
    const nm = new THREE.Matrix3().getNormalMatrix(matrix);
    const c = col(color);
    for (let i = 0; i < p.count; i++) {
      v.fromBufferAttribute(p, i).applyMatrix4(matrix);
      n.fromBufferAttribute(nn, i).applyMatrix3(nm).normalize();
      this.pos.push(v.x, v.y, v.z); this.nor.push(n.x, n.y, n.z); this.col.push(c[0], c[1], c[2]); this.mat.push(mat);
      this.si.push(bone, 0, 0, 0); this.sw.push(1, 0, 0, 0);
      this.idx.push(this.n++);
    }
  }
  // Smooth-skinned tube through horizontal elliptical rings (rest pose, bottom to top or top to bottom).
  // ring: { y, x, z, rx, rz, w: [[bone, weight], ...], c: color, m: material }. A ring with `cut` repeats
  // the previous ring's position with a new colour (crisp sleeve / hem lines).
  loft(rings, seg = 12, caps = [true, true]) {
    const base = this.n;
    const rows = [];
    for (const r of rings) {
      const row = [];
      const c = col(r.c);
      const w = r.w.slice(0, 4);
      const tot = w.reduce((a, b) => a + b[1], 0) || 1;
      for (let j = 0; j < seg; j++) {
        const a = (j / seg) * Math.PI * 2;
        const ca = Math.cos(a), sa = Math.sin(a);
        // slightly squarer cross-section for the torso
        const sq = r.sq || 0;
        const k = 1 + sq * (1 - Math.abs(Math.cos(2 * a))) * 0.12;
        this.pos.push(r.x + ca * r.rx * k, r.y, r.z + sa * r.rz * k + (sa > 0 ? r.front || 0 : 0) * sa * sa);
        this.nor.push(ca / r.rx, 0, sa / r.rz);
        this.col.push(c[0], c[1], c[2]); this.mat.push(r.m ?? MAT.cloth);
        this.si.push(w[0]?.[0] ?? 0, w[1]?.[0] ?? 0, w[2]?.[0] ?? 0, w[3]?.[0] ?? 0);
        this.sw.push((w[0]?.[1] ?? 0) / tot, (w[1]?.[1] ?? 0) / tot, (w[2]?.[1] ?? 0) / tot, (w[3]?.[1] ?? 0) / tot);
        row.push(this.n++);
      }
      rows.push(row);
    }
    for (let i = 0; i < rows.length - 1; i++) {
      if (rings[i + 1].cut) continue;
      for (let j = 0; j < seg; j++) {
        const a = rows[i][j], b = rows[i][(j + 1) % seg], c = rows[i + 1][j], d = rows[i + 1][(j + 1) % seg];
        const up = rings[i + 1].y > rings[i].y;
        if (up) this.idx.push(a, c, b, b, c, d); else this.idx.push(a, b, c, b, d, c);
      }
    }
    // caps: a centre vertex per end
    const cap = (ri, flipUp) => {
      const r = rings[ri], row = rows[ri];
      const c = col(r.c);
      this.pos.push(r.x, r.y + (flipUp ? 1 : -1) * Math.min(r.rx, r.rz) * 0.55, r.z);
      this.nor.push(0, flipUp ? 1 : -1, 0);
      this.col.push(c[0], c[1], c[2]); this.mat.push(r.m ?? MAT.cloth);
      const w = r.w, tot = w.reduce((a, b) => a + b[1], 0) || 1;
      this.si.push(w[0]?.[0] ?? 0, w[1]?.[0] ?? 0, 0, 0); this.sw.push((w[0]?.[1] ?? 0) / tot, (w[1]?.[1] ?? 0) / tot, 0, 0);
      const ci = this.n++;
      for (let j = 0; j < seg; j++) { const a = row[j], b = row[(j + 1) % seg]; if (flipUp) this.idx.push(a, ci, b); else this.idx.push(a, b, ci); }
    };
    const up = rings[rings.length - 1].y > rings[0].y;
    if (caps[0]) cap(0, !up);
    if (caps[1]) cap(rings.length - 1, up);
    this._smooth.push([base, this.n]);
  }
  build() {
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(this.pos, 3));
    g.setAttribute('normal', new THREE.Float32BufferAttribute(this.nor, 3));
    g.setAttribute('color', new THREE.Float32BufferAttribute(this.col, 3));
    g.setAttribute('aMat', new THREE.Float32BufferAttribute(this.mat, 1));
    g.setAttribute('skinIndex', new THREE.Uint16BufferAttribute(this.si, 4));
    g.setAttribute('skinWeight', new THREE.Float32BufferAttribute(this.sw, 4));
    g.setIndex(this.idx);
    // proper normals for the lofted (smooth) parts
    if (this._smooth.length) {
      const P = g.attributes.position, N = g.attributes.normal, I = this.idx;
      const acc = new Float32Array(this.n * 3);
      const a = new THREE.Vector3(), b = new THREE.Vector3(), c = new THREE.Vector3(), e1 = new THREE.Vector3(), e2 = new THREE.Vector3();
      const inLoft = new Uint8Array(this.n);
      for (const [s0, s1] of this._smooth) for (let i = s0; i < s1; i++) inLoft[i] = 1;
      for (let t = 0; t < I.length; t += 3) {
        const i0 = I[t], i1 = I[t + 1], i2 = I[t + 2];
        if (!inLoft[i0]) continue;
        a.fromBufferAttribute(P, i0); b.fromBufferAttribute(P, i1); c.fromBufferAttribute(P, i2);
        e1.subVectors(b, a); e2.subVectors(c, a); e1.cross(e2);
        for (const k of [i0, i1, i2]) { acc[k * 3] += e1.x; acc[k * 3 + 1] += e1.y; acc[k * 3 + 2] += e1.z; }
      }
      for (let i = 0; i < this.n; i++) {
        if (!inLoft[i]) continue;
        a.set(acc[i * 3], acc[i * 3 + 1], acc[i * 3 + 2]);
        if (a.lengthSq() < 1e-12) continue;
        a.normalize();
        N.setXYZ(i, a.x, a.y, a.z);
      }
    }
    g.computeBoundingSphere();
    return g;
  }
}
SkinBuilder.prototype._smooth = null;

const M = (x, y, z, rx = 0, ry = 0, rz = 0, sx = 1, sy = 1, sz = 1) => new THREE.Matrix4().compose(new THREE.Vector3(x, y, z), new THREE.Quaternion().setFromEuler(new THREE.Euler(rx, ry, rz)), new THREE.Vector3(sx, sy, sz));
const cap = (r, len, rs = 8) => new THREE.CapsuleGeometry(r, Math.max(0.001, len), 3, rs);
const rbox = (w, h, d) => { const g = new THREE.BoxGeometry(w, h, d, 2, 2, 2); const p = g.attributes.position; const v = new THREE.Vector3(); for (let i = 0; i < p.count; i++) { v.fromBufferAttribute(p, i); const k = 0.82 + 0.18 * (1 - Math.max(Math.abs(v.x) / (w / 2), Math.abs(v.z) / (d / 2)) ** 4); p.setXYZ(i, v.x * (Math.abs(v.y) > h * 0.3 ? k : 1), v.y, v.z * (Math.abs(v.y) > h * 0.3 ? k : 1)); } g.computeVertexNormals(); return g; };
const shade = (hex, k) => { const c = new THREE.Color(hex); c.multiplyScalar(k); return c.getHex(); };
const mixHex = (h1, h2, t) => new THREE.Color(h1).lerp(new THREE.Color(h2), t).getHex();

// Geometry cache keyed by appearance
const geoCache = new Map();

export function buildHumanoidGeometry(a) {
  const key = a.cacheKey ? a.cacheKey + JSON.stringify(a) : null;
  if (key && geoCache.has(key)) return geoCache.get(key);
  const off = restOffsets(a);
  // world-space rest positions of each bone
  const wp = [];
  for (let i = 0; i < off.length; i++) {
    const p = PARENT[i] >= 0 ? wp[PARENT[i]] : [0, 0, 0];
    wp.push([p[0] + off[i][0], p[1] + off[i][1], p[2] + off[i][2]]);
  }
  const sb = new SkinBuilder();
  sb._smooth = [];
  const skin = a.skin, shirt = a.uniform ? a.uniform.shirt : a.shirt, pants = a.uniform ? a.uniform.pants : a.pants;
  const sleeveLong = a.shirtType === 'long' || a.shirtType === 'jacket' || !!a.uniform;
  const tank = a.shirtType === 'tank';
  const jacket = a.shirtType === 'jacket' ? a.jacketColor : null;
  const torsoCol = jacket || shirt;
  const bw = a.build;
  const fem = a.female;
  const denim = !a.uniform && (pants === 0x1f2a44 || pants === 0x1d3b5c || pants === 0x2b2b2b) ? MAT.denim : MAT.cloth;
  const belt = a.uniform ? 0x111111 : 0x1a1512;
  const lip = mixHex(skin, 0x8a3a3a, 0.35);
  const hy = wp[B.hips][1], sy = wp[B.spine][1], cy = wp[B.chest][1], ny = wp[B.neck][1];
  const H = B.hips, S = B.spine, C = B.chest, N = B.neck, HD = B.head;

  // ---------------- torso: one smooth shell from the crotch to the neck
  const hipW = fem ? 0.19 : 0.172 * bw, waistW = fem ? 0.13 : 0.155 * bw, chestW = fem ? 0.165 : 0.19 * bw, shW = fem ? 0.16 : 0.2 * bw;
  const top = tank ? shirt : torsoCol;
  const R = (y, rx, rz, w, c, m, extra = {}) => ({ y, x: 0, z: extra.z ?? 0, rx, rz, w, c, m, sq: extra.sq ?? 0.6, front: extra.front ?? 0, cut: extra.cut });
  sb.loft([
    R(hy - 0.16, 0.12, 0.09, [[H, 1]], pants, denim),
    R(hy - 0.1, hipW * 0.95, 0.105, [[H, 1]], pants, denim),
    R(hy - 0.02, hipW, 0.115, [[H, 1]], pants, denim),
    R(hy + 0.05, hipW * 0.95, 0.11, [[H, 0.85], [S, 0.15]], pants, denim),
    R(hy + 0.05, hipW * 0.95, 0.11, [[H, 0.85], [S, 0.15]], belt, MAT.leather, { cut: true }),
    R(hy + 0.085, hipW * 0.93, 0.108, [[H, 0.7], [S, 0.3]], belt, MAT.leather),
    R(hy + 0.085, hipW * 0.93, 0.108, [[H, 0.7], [S, 0.3]], top, MAT.cloth, { cut: true }),
    R(sy + 0.02, waistW, 0.1, [[S, 0.8], [H, 0.2]], top, MAT.cloth, { front: 0.01 }),
    R(sy + 0.1, waistW * 1.04, 0.102, [[S, 0.6], [C, 0.4]], top, MAT.cloth, { front: 0.015 }),
    R(cy + 0.0, chestW * 0.96, 0.11, [[C, 0.7], [S, 0.3]], top, MAT.cloth, { front: fem ? 0.03 : 0.02 }),
    R(cy + 0.1, chestW, 0.118, [[C, 1]], top, MAT.cloth, { front: fem ? 0.045 : 0.025 }),
    R(cy + 0.18, shW * 0.92, 0.11, [[C, 1]], top, MAT.cloth, { front: 0.01 }),
    R(cy + 0.21, shW * 0.64, 0.088, [[C, 1]], top, MAT.cloth),
    R(ny + 0.005, 0.078, 0.066, [[C, 0.75], [N, 0.25]], top, MAT.cloth, { sq: 0 }),
  ], 14, [true, false]);
  // neck
  sb.loft([
    { y: ny - 0.02, x: 0, z: 0, rx: 0.064, rz: 0.058, w: [[C, 0.7], [N, 0.3]], c: skin, m: MAT.skin },
    { y: ny + 0.04, x: 0, z: 0.004, rx: 0.056, rz: 0.053, w: [[N, 1]], c: skin, m: MAT.skin },
    { y: ny + 0.1, x: 0, z: 0.008, rx: 0.054, rz: 0.052, w: [[N, 0.4], [HD, 0.6]], c: skin, m: MAT.skin },
    { y: ny + 0.14, x: 0, z: 0.01, rx: 0.05, rz: 0.05, w: [[HD, 1]], c: skin, m: MAT.skin },
  ], 10, [false, false]);
  // collar / neckline trim
  if (!tank) sb.add(new THREE.TorusGeometry(0.07, 0.011, 5, 16), M(0, ny + 0.012, 0.006, Math.PI / 2 - 0.25, 0, 0, 1.05, 1.08, 1), jacket || shirt, C);
  if (jacket) {
    sb.add(new THREE.BoxGeometry(0.1, 0.34, 0.012), M(0, cy + 0.04, chestW * 0.62 + 0.008), shirt, C); // open front
    for (const s of [1, -1]) sb.add(new THREE.BoxGeometry(0.05, 0.16, 0.012), M(s * 0.06, cy + 0.14, chestW * 0.6 + 0.012, 0, 0, s * 0.35), shade(jacket, 0.85), C); // lapels
  }
  // belt buckle & jeans pockets
  sb.add(new THREE.BoxGeometry(0.045, 0.032, 0.012), M(0, hy + 0.068, hipW * 0.62 + 0.004), 0xb8a060, H, MAT.metal);
  if (denim === MAT.denim) for (const s of [1, -1]) sb.add(new THREE.BoxGeometry(0.075, 0.08, 0.006), M(s * 0.07, hy - 0.02, -0.117), shade(pants, 0.82), H, MAT.denim);
  if (a.uniform) sb.add(new THREE.BoxGeometry(0.05, 0.06, 0.01), M(0.09, cy + 0.14, 0.13), 0xd4af37, C, MAT.metal); // badge

  // ---------------- arms: shoulder to wrist in one piece, bending smoothly at the elbow
  const armR = fem ? 0.044 : 0.052 * bw;
  const sleeveEnd = sleeveLong ? 0 : tank ? 9 : 0.16; // metres below the shoulder
  for (const [ua, fa, hd, sgn] of [[B.lUpperArm, B.lForearm, B.lHand, 1], [B.rUpperArm, B.rForearm, B.rHand, -1]]) {
    const u = wp[ua], f = wp[fa], h = wp[hd];
    const x = u[0];
    const sleeve = jacket || shirt;
    const colAt = (y) => (tank ? skin : u[1] - y < sleeveEnd || sleeveLong && y > h[1] + 0.02 ? sleeve : skin);
    const matAt = (y) => (colAt(y) === skin && !(sleeveLong && y > h[1] + 0.02) ? MAT.skin : MAT.cloth);
    const rings = [];
    const pushR = (y, r, w, cut = false) => rings.push({ y, x: x + sgn * 0.004 * (y > u[1] - 0.05 ? 1 : 0), z: 0, rx: r, rz: r * 0.95, w, c: colAt(y), m: matAt(y), cut });
    pushR(u[1] + 0.035, armR * 0.6, [[C, 0.6], [ua, 0.4]]);
    pushR(u[1] - 0.005, armR * 1.2, [[C, 0.45], [ua, 0.55]]);
    pushR(u[1] - 0.06, armR * 1.1, [[ua, 0.9], [C, 0.1]]);
    if (!tank && !sleeveLong) {
      // short sleeve hem: a slightly wider ring, then the bare arm
      const yh = u[1] - sleeveEnd;
      pushR(yh + 0.004, armR * 1.12, [[ua, 1]]);
      rings.push({ y: yh + 0.002, x, z: 0, rx: armR * 0.97, rz: armR * 0.92, w: [[ua, 1]], c: jacket || shirt, m: MAT.cloth }); // underside of the hem
      rings.push({ y: yh + 0.002, x, z: 0, rx: armR * 0.97, rz: armR * 0.92, w: [[ua, 1]], c: skin, m: MAT.skin, cut: true });
    }
    pushR(u[1] - 0.2, armR * 0.95, [[ua, 1]]);
    pushR(f[1] + 0.04, armR * 0.86, [[ua, 0.75], [fa, 0.25]]);
    pushR(f[1], armR * 0.82, [[ua, 0.5], [fa, 0.5]]);
    pushR(f[1] - 0.04, armR * 0.86, [[fa, 0.8], [ua, 0.2]]);
    pushR(f[1] - 0.12, armR * 0.88, [[fa, 1]]);
    if (sleeveLong) {
      pushR(h[1] + 0.05, armR * 0.78, [[fa, 0.95], [hd, 0.05]]);
      pushR(h[1] + 0.024, armR * 0.8, [[fa, 0.8], [hd, 0.2]]); // cuff
      rings.push({ y: h[1] + 0.022, x, z: 0, rx: armR * 0.64, rz: armR * 0.6, w: [[fa, 0.7], [hd, 0.3]], c: jacket || shirt, m: MAT.cloth });
      rings.push({ y: h[1] + 0.022, x, z: 0, rx: armR * 0.64, rz: armR * 0.6, w: [[fa, 0.7], [hd, 0.3]], c: skin, m: MAT.skin, cut: true });
      rings.push({ y: h[1] - 0.005, x, z: 0, rx: armR * 0.62, rz: armR * 0.58, w: [[fa, 0.4], [hd, 0.6]], c: skin, m: MAT.skin });
    } else {
      pushR(h[1] + 0.03, armR * 0.68, [[fa, 0.85], [hd, 0.15]]);
      pushR(h[1] - 0.005, armR * 0.62, [[fa, 0.4], [hd, 0.6]]);
    }
    rings.sort((p, q) => q.y - p.y);
    sb.loft(rings, 10, [true, true]);
    // hand: palm, four fingers and a thumb (palm faces the thigh)
    const hx = h[0], hyy = h[1];
    sb.add(rbox(0.026, 0.085, 0.078), M(hx, hyy - 0.05, 0.004), skin, hd, MAT.skin);
    for (let k = 0; k < 4; k++) {
      const z = 0.03 - k * 0.02;
      const len = [0.068, 0.076, 0.072, 0.058][k];
      sb.add(cap(0.0085, len - 0.017, 5), M(hx + sgn * 0.004, hyy - 0.095 - len / 2 + 0.006, z, 0, 0, sgn * 0.08), skin, hd, MAT.skin);
    }
    sb.add(cap(0.01, 0.035, 5), M(hx + sgn * 0.012, hyy - 0.055, 0.045, 0.55, 0, sgn * 0.3), skin, hd, MAT.skin);
  }

  // ---------------- legs: hip to ankle, bending smoothly at the knee
  const legR = fem ? 0.07 : 0.078 * bw;
  for (const [th, shn, ft, sgn] of [[B.lThigh, B.lShin, B.lFoot, 1], [B.rThigh, B.rShin, B.rFoot, -1]]) {
    const t = wp[th], s = wp[shn], f = wp[ft];
    const x = t[0];
    const shortsHem = s[1] + 0.08;
    const colAt = (y) => (a.shorts && y < shortsHem ? skin : pants);
    const matAt = (y) => (a.shorts && y < shortsHem ? MAT.skin : denim);
    const rings = [];
    const L = (y, rx, rz, w, cut = false, xo = 0) => rings.push({ y, x: x + xo, z: 0, rx, rz, w, c: colAt(y), m: matAt(y), cut });
    L(t[1] + 0.05, legR * 0.95, legR, [[H, 0.6], [th, 0.4]]);
    L(t[1] - 0.03, legR * 1.12, legR * 1.12, [[H, 0.3], [th, 0.7]], false, sgn * 0.008);
    L(t[1] - 0.15, legR * 1.08, legR * 1.06, [[th, 1]], false, sgn * 0.01);
    L(t[1] - 0.3, legR * 0.92, legR * 0.92, [[th, 1]]);
    if (a.shorts) {
      L(shortsHem + 0.004, legR * 0.98, legR * 0.98, [[th, 0.9], [shn, 0.1]]);
      rings.push({ y: shortsHem + 0.002, x, z: 0, rx: legR * 0.8, rz: legR * 0.82, w: [[th, 0.9], [shn, 0.1]], c: pants, m: denim });
      rings.push({ y: shortsHem + 0.002, x, z: 0, rx: legR * 0.8, rz: legR * 0.82, w: [[th, 0.9], [shn, 0.1]], c: skin, m: MAT.skin, cut: true });
    }
    L(s[1] + 0.04, legR * 0.78, legR * 0.82, [[th, 0.75], [shn, 0.25]]);
    L(s[1], legR * 0.75, legR * 0.8, [[th, 0.5], [shn, 0.5]]);
    L(s[1] - 0.05, legR * 0.76, legR * 0.8, [[shn, 0.8], [th, 0.2]]);
    L(s[1] - 0.15, legR * 0.82, legR * 0.88, [[shn, 1]]);
    L(f[1] + 0.12, legR * 0.6, legR * 0.62, [[shn, 1]]);
    L(f[1] + 0.05, legR * (a.shorts ? 0.52 : 0.66), legR * (a.shorts ? 0.54 : 0.66), [[shn, 0.7], [ft, 0.3]]);
    L(f[1] + 0.01, legR * (a.shorts ? 0.5 : 0.64), legR * (a.shorts ? 0.52 : 0.64), [[shn, 0.4], [ft, 0.6]]);
    rings.sort((p, q) => q.y - p.y);
    sb.loft(rings, 10, [false, true]);
    // shoe: upper, toe cap, sole, laces
    sb.add(rbox(0.098, 0.075, 0.25), M(f[0], f[1] - 0.022, f[2] + 0.052), a.shoes, ft, MAT.leather);
    sb.add(new THREE.SphereGeometry(0.05, 8, 5, 0, Math.PI * 2, 0, Math.PI / 2), M(f[0], f[1] - 0.045, f[2] + 0.15, 0, 0, 0, 0.98, 0.9, 0.9), a.shoes, ft, MAT.leather);
    sb.add(rbox(0.104, 0.022, 0.262), M(f[0], f[1] - 0.058, f[2] + 0.053), 0xe8e4dc, ft, MAT.leather);
    sb.add(new THREE.BoxGeometry(0.04, 0.008, 0.07), M(f[0], f[1] + 0.017, f[2] + 0.075, -0.25, 0, 0), shade(a.shoes, 0.6), ft, MAT.cloth);
  }

  // ---------------- head
  const hdy = wp[B.head][1] + 0.1;
  sb.add(new THREE.SphereGeometry(0.105, 18, 14), M(0, hdy, 0.0, 0, 0, 0, 0.9, 1.1, 1.0), skin, HD, MAT.skin);
  sb.add(new THREE.SphereGeometry(0.066, 12, 8), M(0, hdy - 0.066, 0.03, 0.2, 0, 0, 1.05, 0.82, 1.02), skin, HD, MAT.skin); // jaw & chin
  for (const s of [1, -1]) sb.add(new THREE.SphereGeometry(0.024, 8, 6), M(s * 0.048, hdy - 0.02, 0.058, 0, 0, 0, 1, 0.85, 0.8), skin, HD, MAT.skin); // cheekbones
  sb.add(new THREE.BoxGeometry(0.1, 0.022, 0.03), M(0, hdy + 0.045, 0.085, -0.2, 0, 0), skin, HD, MAT.skin); // brow ridge
  // nose: bridge + tip + nostrils
  sb.add(new THREE.BoxGeometry(0.02, 0.05, 0.03), M(0, hdy + 0.01, 0.1, -0.25, 0, 0), skin, HD, MAT.skin);
  sb.add(new THREE.SphereGeometry(0.0135, 8, 6), M(0, hdy - 0.016, 0.112, 0, 0, 0, 1.15, 0.9, 1), skin, HD, MAT.skin);
  for (const s of [1, -1]) sb.add(new THREE.SphereGeometry(0.0085, 6, 4), M(s * 0.012, hdy - 0.021, 0.105), shade(skin, 0.92), HD, MAT.skin);
  // eyes: white, iris, and a lid line
  const iris = [0x3b2a1e, 0x2e4a6e, 0x3c5a3a, 0x1c1410][Math.floor(((a.skin % 97) + (a.hair % 13)) % 4)];
  for (const s of [1, -1]) {
    sb.add(new THREE.SphereGeometry(0.0135, 10, 8), M(s * 0.035, hdy + 0.022, 0.084, 0, 0, 0, 1.15, 0.72, 0.8), 0xe6e0d8, HD, MAT.eye);
    sb.add(new THREE.SphereGeometry(0.0068, 8, 6), M(s * 0.035, hdy + 0.022, 0.0935, 0, 0, 0, 1, 1, 0.5), iris, HD, MAT.eye);
    sb.add(new THREE.SphereGeometry(0.003, 6, 4), M(s * 0.035, hdy + 0.022, 0.0968), 0x050505, HD, MAT.eye);
    sb.add(new THREE.BoxGeometry(0.034, 0.007, 0.016), M(s * 0.035, hdy + 0.0325, 0.09, -0.35, 0, 0), shade(skin, 0.85), HD, MAT.skin); // upper lid
    sb.add(new THREE.BoxGeometry(0.038, 0.009, 0.012), M(s * 0.037, hdy + 0.056, 0.098, -0.15, 0, s * -0.12), a.hair, HD, MAT.hair); // brows
    sb.add(new THREE.SphereGeometry(0.024, 8, 6), M(s * 0.1, hdy + 0.0, -0.005, 0, 0, 0, 0.45, 1, 0.75), skin, HD, MAT.skin); // ears
  }
  // lips
  sb.add(cap(0.0085, 0.034, 6), M(0, hdy - 0.047, 0.1, 0, 0, Math.PI / 2, 1, 1, 0.9), lip, HD, MAT.skin);
  sb.add(cap(0.0095, 0.03, 6), M(0, hdy - 0.061, 0.096, 0, 0, Math.PI / 2, 1, 1, 0.9), lip, HD, MAT.skin);
  if (a.beard) {
    sb.add(new THREE.SphereGeometry(0.07, 10, 7, 0, Math.PI * 2, Math.PI * 0.35, Math.PI * 0.65), M(0, hdy - 0.045, 0.03, 0.15, 0, 0, 1.06, 1.0, 1.08), a.hair, HD, MAT.hair);
    sb.add(new THREE.BoxGeometry(0.05, 0.01, 0.012), M(0, hdy - 0.038, 0.108), a.hair, HD, MAT.hair); // moustache
  }
  if (a.glasses) {
    for (const s of [1, -1]) sb.add(new THREE.BoxGeometry(0.046, 0.03, 0.006), M(s * 0.037, hdy + 0.022, 0.112), 0x080808, HD, MAT.eye);
    sb.add(new THREE.BoxGeometry(0.03, 0.006, 0.006), M(0, hdy + 0.03, 0.112), 0x111111, HD, MAT.metal);
    for (const s of [1, -1]) sb.add(new THREE.BoxGeometry(0.004, 0.006, 0.1), M(s * 0.062, hdy + 0.03, 0.065), 0x111111, HD, MAT.metal);
  }
  // hair
  const hairC = a.hair;
  const scalp = (sz = 1, cut = 0.5) => sb.add(new THREE.SphereGeometry(0.112 * sz, 16, 10, 0, Math.PI * 2, 0, Math.PI * cut), M(0, hdy + 0.008, -0.006, -0.22, 0, 0, 0.94, 1.08, 1.05), hairC, HD, MAT.hair);
  switch (a.hairStyle) {
    case 'short':
      scalp(1.005, 0.54);
      for (const s of [1, -1]) sb.add(new THREE.BoxGeometry(0.01, 0.04, 0.026), M(s * 0.092, hdy + 0.018, 0.035), hairC, HD, MAT.hair); // sideburns
      break;
    case 'buzz': scalp(0.985, 0.46); break;
    case 'afro':
      sb.add(new THREE.IcosahedronGeometry(0.15, 2), M(0, hdy + 0.06, -0.025, 0, 0, 0, 1, 0.92, 1), hairC, HD, MAT.hair);
      break;
    case 'long':
      scalp(1.02, 0.56);
      sb.add(rbox(0.22, 0.28, 0.08), M(0, hdy - 0.09, -0.075, 0.08, 0, 0), hairC, HD, MAT.hair);
      for (const s of [1, -1]) sb.add(rbox(0.04, 0.2, 0.09), M(s * 0.1, hdy - 0.05, -0.01), hairC, HD, MAT.hair);
      break;
    case 'ponytail':
      scalp(1.01, 0.56);
      sb.add(new THREE.SphereGeometry(0.03, 8, 6), M(0, hdy + 0.02, -0.12), hairC, HD, MAT.hair);
      sb.add(cap(0.032, 0.17, 7), M(0, hdy - 0.07, -0.14, 0.35, 0, 0), hairC, HD, MAT.hair);
      break;
    case 'bun':
      scalp(1.01, 0.56);
      sb.add(new THREE.SphereGeometry(0.052, 10, 8), M(0, hdy + 0.105, -0.075), hairC, HD, MAT.hair);
      break;
    case 'cap':
      sb.add(new THREE.SphereGeometry(0.118, 16, 10, 0, Math.PI * 2, 0, Math.PI * 0.5), M(0, hdy + 0.012, -0.004, -0.08, 0, 0, 0.95, 1.05, 1.04), a.hat || 0x222222, HD, MAT.cloth);
      sb.add(new THREE.CylinderGeometry(0.085, 0.09, 0.012, 14, 1, false, -Math.PI / 2, Math.PI), M(0, hdy + 0.03, 0.09, 0.1, 0, 0, 1, 1, 1.25), a.hat || 0x222222, HD, MAT.cloth);
      sb.add(new THREE.SphereGeometry(0.012, 6, 4), M(0, hdy + 0.128, 0), shade(a.hat || 0x222222, 0.7), HD, MAT.cloth);
      break;
    default: break;
  }
  if (a.bandana) sb.add(new THREE.CylinderGeometry(0.108, 0.11, 0.045, 16), M(0, hdy + 0.05, -0.005, -0.1, 0, 0, 0.95, 1, 1.02), a.bandana, HD, MAT.cloth);
  if (a.uniform && a.uniform.hat) {
    sb.add(new THREE.CylinderGeometry(0.108, 0.113, 0.07, 16), M(0, hdy + 0.07, 0), a.uniform.hat, HD, MAT.cloth);
    sb.add(new THREE.CylinderGeometry(0.125, 0.125, 0.015, 16), M(0, hdy + 0.11, 0), a.uniform.hat, HD, MAT.cloth);
    sb.add(new THREE.BoxGeometry(0.16, 0.01, 0.08), M(0, hdy + 0.045, 0.12, 0.1, 0, 0), 0x111111, HD, MAT.leather);
    sb.add(new THREE.BoxGeometry(0.03, 0.03, 0.006), M(0, hdy + 0.08, 0.113), 0xd4af37, HD, MAT.metal);
  }

  const geo = sb.build();
  const res = { geo, rest: off, world: wp, cached: !!key };
  if (key) { geo.userData.shared = true; geoCache.set(key, res); }
  return res;
}

// Characters: per-part roughness (skin, cloth, denim, leather, hair, eyes) and fine fabric detail
let sharedMat = null;
export function humanoidMaterial() {
  if (!sharedMat) {
    sharedMat = std({ vertexColors: true, roughness: 0.75, metalness: 0.0 }, {
      key: 'humanoid2',
      vertexPars: 'attribute float aMat; varying float vMat; varying vec3 vObj;',
      vertexMain: 'vMat = aMat; vObj = position;',
      fragPars: 'varying float vMat; varying vec3 vObj; float atgHRough = 0.8; float atgHMetal = 0.0;',
      fragColor: `
      {
        float m = floor(vMat + 0.5);
        vec3 o = vObj * 60.0;
        float fw = clamp(length(fwidth(o)) * 0.6, 0.0, 1.0); // fade the weave out with distance
        if (m < 0.5) { // knit cloth
          float k = sin(o.x * 3.0) * sin(o.y * 3.0) * 0.5 + 0.5;
          diffuseColor.rgb *= mix(0.94 + 0.08 * k, 1.0, fw);
          atgHRough = 0.9;
        } else if (m < 1.5) { // skin
          atgHRough = 0.52;
          diffuseColor.rgb *= 1.0 + 0.03 * sin(o.x * 2.1 + o.y * 1.7) * (1.0 - fw);
        } else if (m < 2.5) { // hair: fine strands
          float st = sin((vObj.x * 0.6 + vObj.z * 0.8) * 900.0) * 0.5 + 0.5;
          diffuseColor.rgb *= mix(0.85 + 0.25 * st, 1.0, fw);
          atgHRough = 0.55;
        } else if (m < 3.5) { atgHRough = 0.38; } // leather / rubber
        else if (m < 4.5) { atgHRough = 0.06; } // eyes & lenses
        else if (m < 5.5) { // denim twill
          float tw = sin((vObj.y * 1.0 + vObj.x * 0.7 + vObj.z * 0.7) * 1100.0) * 0.5 + 0.5;
          diffuseColor.rgb *= mix(0.86 + 0.2 * tw, 1.0, fw);
          atgHRough = 0.93;
        } else { atgHRough = 0.3; atgHMetal = 0.8; }
      }`,
      fragRoughness: 'roughnessFactor = atgHRough;',
      fragMetal: 'metalnessFactor = atgHMetal;',
    });
  }
  return sharedMat;
}

export function createHumanoid(appearance) {
  const { geo, rest } = buildHumanoidGeometry(appearance);
  const bones = BONE_NAMES.map((n) => { const b = new THREE.Bone(); b.name = n; return b; });
  bones.forEach((b, i) => {
    b.position.set(rest[i][0], rest[i][1], rest[i][2]);
    b.rotation.order = 'YXZ';
    if (PARENT[i] >= 0) bones[PARENT[i]].add(b);
  });
  bones[0].updateMatrixWorld(true);
  const skeleton = new THREE.Skeleton(bones);
  const mesh = new THREE.SkinnedMesh(geo, humanoidMaterial());
  mesh.add(bones[0]);
  mesh.bind(skeleton);
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  mesh.frustumCulled = false;
  const root = new THREE.Group();
  root.add(mesh);
  root.scale.setScalar(appearance.height || 1);
  return { root, mesh, bones, skeleton, rest };
}

export { PARENT as BONE_PARENT };
