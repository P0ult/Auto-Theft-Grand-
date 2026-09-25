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

class SkinBuilder {
  constructor() { this.pos = []; this.nor = []; this.col = []; this.si = []; this.sw = []; this.idx = []; this.n = 0; }
  add(geo, matrix, color, bone) {
    const g = geo.index ? geo.toNonIndexed() : geo;
    const p = g.attributes.position, nn = g.attributes.normal;
    const v = new THREE.Vector3(), n = new THREE.Vector3();
    const nm = new THREE.Matrix3().getNormalMatrix(matrix);
    const c = col(color);
    for (let i = 0; i < p.count; i++) {
      v.fromBufferAttribute(p, i).applyMatrix4(matrix);
      n.fromBufferAttribute(nn, i).applyMatrix3(nm).normalize();
      this.pos.push(v.x, v.y, v.z); this.nor.push(n.x, n.y, n.z); this.col.push(c[0], c[1], c[2]);
      this.si.push(bone, 0, 0, 0); this.sw.push(1, 0, 0, 0);
      this.idx.push(this.n++);
    }
  }
  build() {
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(this.pos, 3));
    g.setAttribute('normal', new THREE.Float32BufferAttribute(this.nor, 3));
    g.setAttribute('color', new THREE.Float32BufferAttribute(this.col, 3));
    g.setAttribute('skinIndex', new THREE.Uint16BufferAttribute(this.si, 4));
    g.setAttribute('skinWeight', new THREE.Float32BufferAttribute(this.sw, 4));
    g.setIndex(this.idx);
    g.computeBoundingSphere();
    return g;
  }
}

const M = (x, y, z, rx = 0, ry = 0, rz = 0, sx = 1, sy = 1, sz = 1) => new THREE.Matrix4().compose(new THREE.Vector3(x, y, z), new THREE.Quaternion().setFromEuler(new THREE.Euler(rx, ry, rz)), new THREE.Vector3(sx, sy, sz));
const cap = (r, len, rs = 8) => new THREE.CapsuleGeometry(r, Math.max(0.001, len), 3, rs);
const rbox = (w, h, d) => { const g = new THREE.BoxGeometry(w, h, d, 2, 2, 2); const p = g.attributes.position; const v = new THREE.Vector3(); for (let i = 0; i < p.count; i++) { v.fromBufferAttribute(p, i); const k = 0.82 + 0.18 * (1 - Math.max(Math.abs(v.x) / (w / 2), Math.abs(v.z) / (d / 2)) ** 4); p.setXYZ(i, v.x * (Math.abs(v.y) > h * 0.3 ? k : 1), v.y, v.z * (Math.abs(v.y) > h * 0.3 ? k : 1)); } g.computeVertexNormals(); return g; };

// Geometry cache keyed by appearance
const geoCache = new Map();

export function buildHumanoidGeometry(a) {
  const key = JSON.stringify(a);
  if (geoCache.has(key)) return geoCache.get(key);
  const off = restOffsets(a);
  // world-space rest positions of each bone
  const wp = [];
  for (let i = 0; i < off.length; i++) {
    const p = PARENT[i] >= 0 ? wp[PARENT[i]] : [0, 0, 0];
    wp.push([p[0] + off[i][0], p[1] + off[i][1], p[2] + off[i][2]]);
  }
  const sb = new SkinBuilder();
  const skin = a.skin, shirt = a.uniform ? a.uniform.shirt : a.shirt, pants = a.uniform ? a.uniform.pants : a.pants;
  const sleeveLong = a.shirtType === 'long' || a.shirtType === 'jacket' || !!a.uniform;
  const tank = a.shirtType === 'tank';
  const jacket = a.shirtType === 'jacket' ? a.jacketColor : null;
  const torsoCol = jacket || shirt;
  const bw = a.build;
  const fem = a.female;

  // Pelvis / hips
  const hy = wp[B.hips][1];
  sb.add(rbox(fem ? 0.34 : 0.32 * bw, 0.2, 0.2), M(0, hy - 0.02, 0), pants, B.hips);
  // belt
  sb.add(new THREE.BoxGeometry(fem ? 0.35 : 0.33 * bw, 0.04, 0.21), M(0, hy + 0.07, 0), 0x1a1a1a, B.hips);
  // abdomen
  const sy = wp[B.spine][1];
  sb.add(rbox(fem ? 0.28 : 0.3 * bw, 0.2, 0.18), M(0, sy + 0.06, 0), tank ? shirt : torsoCol, B.spine);
  // chest
  const cy = wp[B.chest][1];
  const chestW = fem ? 0.32 : 0.38 * bw;
  sb.add(rbox(chestW, 0.3, fem ? 0.2 : 0.22), M(0, cy + 0.1, 0), tank ? shirt : torsoCol, B.chest);
  if (fem) { sb.add(new THREE.SphereGeometry(0.065, 8, 6), M(0.065, cy + 0.12, 0.085, 0, 0, 0, 1, 0.9, 0.8), tank ? shirt : torsoCol, B.chest); sb.add(new THREE.SphereGeometry(0.065, 8, 6), M(-0.065, cy + 0.12, 0.085, 0, 0, 0, 1, 0.9, 0.8), tank ? shirt : torsoCol, B.chest); }
  // shoulders (deltoids)
  for (const s of [1, -1]) sb.add(new THREE.SphereGeometry(0.07 * (fem ? 0.9 : bw), 8, 6), M(s * (chestW / 2 - 0.02), cy + 0.2, 0), tank ? skin : torsoCol, B.chest);
  if (jacket) { sb.add(new THREE.BoxGeometry(0.1, 0.28, 0.01), M(0, cy + 0.08, 0.112), shirt, B.chest); }
  if (a.uniform) {
    sb.add(new THREE.BoxGeometry(0.05, 0.06, 0.01), M(0.1, cy + 0.16, 0.113), 0xd4af37, B.chest); // badge
    sb.add(new THREE.BoxGeometry(fem ? 0.36 : 0.34 * bw, 0.05, 0.22), M(0, hy + 0.07, 0), 0x111111, B.hips);
  }
  // neck
  const ny = wp[B.neck][1];
  sb.add(new THREE.CylinderGeometry(0.05, 0.058, 0.12, 8), M(0, ny + 0.04, 0), skin, B.neck);
  // head
  const hdy = wp[B.head][1] + 0.1;
  sb.add(new THREE.SphereGeometry(0.105, 14, 10), M(0, hdy, 0.005, 0, 0, 0, 0.92, 1.12, 1.02), skin, B.head);
  sb.add(new THREE.SphereGeometry(0.06, 8, 6), M(0, hdy - 0.07, 0.035, 0, 0, 0, 1.1, 0.8, 1.0), skin, B.head); // jaw
  sb.add(new THREE.BoxGeometry(0.03, 0.04, 0.04), M(0, hdy - 0.005, 0.11), skin, B.head); // nose
  for (const s of [1, -1]) {
    sb.add(new THREE.SphereGeometry(0.014, 6, 4), M(s * 0.037, hdy + 0.025, 0.093), 0x1a1410, B.head); // eyes
    sb.add(new THREE.BoxGeometry(0.035, 0.008, 0.01), M(s * 0.037, hdy + 0.052, 0.095), a.hair, B.head); // brows
    sb.add(new THREE.SphereGeometry(0.022, 6, 4), M(s * 0.098, hdy + 0.0, 0.0, 0, 0, 0, 0.5, 1, 0.8), skin, B.head); // ears
  }
  sb.add(new THREE.BoxGeometry(0.045, 0.008, 0.01), M(0, hdy - 0.055, 0.1), 0x7a3b30, B.head); // mouth
  if (a.beard) sb.add(new THREE.SphereGeometry(0.07, 8, 6), M(0, hdy - 0.07, 0.05, 0, 0, 0, 1.05, 0.7, 0.7), a.hair, B.head);
  if (a.glasses) {
    sb.add(new THREE.BoxGeometry(0.15, 0.035, 0.01), M(0, hdy + 0.028, 0.107), 0x050505, B.head);
  }
  // hair
  switch (a.hairStyle) {
    case 'short': sb.add(new THREE.SphereGeometry(0.112, 12, 8, 0, Math.PI * 2, 0, Math.PI * 0.5), M(0, hdy + 0.01, -0.005, -0.25, 0, 0, 0.96, 1.08, 1.06), a.hair, B.head); break;
    case 'buzz': sb.add(new THREE.SphereGeometry(0.109, 12, 8, 0, Math.PI * 2, 0, Math.PI * 0.45), M(0, hdy + 0.012, 0, -0.2, 0, 0, 0.95, 1.1, 1.04), a.hair, B.head); break;
    case 'afro': sb.add(new THREE.SphereGeometry(0.15, 12, 8), M(0, hdy + 0.06, -0.02), a.hair, B.head); break;
    case 'long':
      sb.add(new THREE.SphereGeometry(0.115, 12, 8, 0, Math.PI * 2, 0, Math.PI * 0.55), M(0, hdy + 0.01, -0.01, -0.2, 0, 0, 0.98, 1.1, 1.08), a.hair, B.head);
      sb.add(new THREE.BoxGeometry(0.22, 0.26, 0.08), M(0, hdy - 0.1, -0.07), a.hair, B.head);
      break;
    case 'ponytail':
      sb.add(new THREE.SphereGeometry(0.113, 12, 8, 0, Math.PI * 2, 0, Math.PI * 0.55), M(0, hdy + 0.01, -0.01, -0.2, 0, 0, 0.97, 1.1, 1.06), a.hair, B.head);
      sb.add(cap(0.035, 0.16, 6), M(0, hdy - 0.07, -0.13, 0.3, 0, 0), a.hair, B.head);
      break;
    case 'bun':
      sb.add(new THREE.SphereGeometry(0.113, 12, 8, 0, Math.PI * 2, 0, Math.PI * 0.55), M(0, hdy + 0.01, -0.01, -0.2, 0, 0, 0.97, 1.1, 1.06), a.hair, B.head);
      sb.add(new THREE.SphereGeometry(0.055, 8, 6), M(0, hdy + 0.1, -0.08), a.hair, B.head);
      break;
    case 'cap':
      sb.add(new THREE.SphereGeometry(0.117, 12, 8, 0, Math.PI * 2, 0, Math.PI * 0.5), M(0, hdy + 0.015, 0), a.hat || 0x222222, B.head);
      sb.add(new THREE.BoxGeometry(0.16, 0.012, 0.11), M(0, hdy + 0.03, 0.14, 0.08, 0, 0), a.hat || 0x222222, B.head);
      break;
    default: break;
  }
  if (a.bandana) sb.add(new THREE.CylinderGeometry(0.113, 0.113, 0.045, 12), M(0, hdy + 0.05, 0), a.bandana, B.head);
  if (a.uniform && a.uniform.hat) {
    sb.add(new THREE.CylinderGeometry(0.11, 0.115, 0.07, 12), M(0, hdy + 0.07, 0), a.uniform.hat, B.head);
    sb.add(new THREE.CylinderGeometry(0.125, 0.125, 0.015, 12), M(0, hdy + 0.11, 0), a.uniform.hat, B.head);
    sb.add(new THREE.BoxGeometry(0.16, 0.01, 0.08), M(0, hdy + 0.045, 0.12, 0.1, 0, 0), 0x111111, B.head);
  }

  // arms
  const armR = fem ? 0.043 : 0.052 * bw;
  for (const [ua, fa, hd, s] of [[B.lUpperArm, B.lForearm, B.lHand, 1], [B.rUpperArm, B.rForearm, B.rHand, -1]]) {
    const u = wp[ua], f = wp[fa], h = wp[hd];
    const upperCol = tank ? skin : torsoCol;
    sb.add(cap(armR, 0.2), M(u[0], u[1] - 0.14, u[2]), upperCol, ua);
    const foreCol = sleeveLong ? (jacket || shirt) : skin;
    sb.add(cap(armR * 0.85, 0.18), M(f[0], f[1] - 0.12, f[2]), foreCol, fa);
    if (sleeveLong) sb.add(new THREE.CylinderGeometry(armR * 0.95, armR * 0.95, 0.03, 8), M(f[0], f[1] - 0.24, f[2]), jacket || shirt, fa);
    // hand
    sb.add(new THREE.BoxGeometry(0.05, 0.09, 0.08), M(h[0], h[1] - 0.05, h[2] + 0.005), skin, hd);
    sb.add(new THREE.BoxGeometry(0.02, 0.05, 0.02), M(h[0] + s * -0.0, h[1] - 0.03, h[2] + 0.05, 0.4, 0, 0), skin, hd); // thumb
  }
  // legs
  const legR = fem ? 0.068 : 0.074 * bw;
  for (const [th, sh, ft] of [[B.lThigh, B.lShin, B.lFoot], [B.rThigh, B.rShin, B.rFoot]]) {
    const t = wp[th], s = wp[sh], f = wp[ft];
    sb.add(cap(legR, 0.3), M(t[0], t[1] - 0.21, t[2]), pants, th);
    const shinCol = a.shorts ? skin : pants;
    sb.add(cap(legR * 0.78, 0.3), M(s[0], s[1] - 0.2, s[2] - 0.005), shinCol, sh);
    if (a.shorts) sb.add(new THREE.CylinderGeometry(legR * 1.05, legR * 1.05, 0.12, 8), M(t[0], t[1] - 0.38, t[2]), pants, th);
    // shoe
    sb.add(rbox(0.1, 0.075, 0.25), M(f[0], f[1] - 0.025, f[2] + 0.05), a.shoes, ft);
    sb.add(new THREE.BoxGeometry(0.105, 0.02, 0.255), M(f[0], f[1] - 0.055, f[2] + 0.05), 0xdddddd, ft);
  }
  const geo = sb.build();
  const res = { geo, rest: off, world: wp };
  geoCache.set(key, res);
  return res;
}

let sharedMat = null;
export function humanoidMaterial() {
  if (!sharedMat) sharedMat = std({ vertexColors: true, roughness: 0.75, metalness: 0.0 }, { key: 'humanoid' });
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
