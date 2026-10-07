// Prints each animal breed model: key, numbers, and per part the vertex and index counts with the
// sums of positions, normals and colours (diff against animalstest.cpp).
//   node --import ./three-hook.mjs dumpanimals.mjs > jsanimals.txt
import * as THREE from 'three';
import { GeoBuilder, mat4 } from '../../../src/world/geom.js';

const SPECIES = {
  dog: { name: 'Dog', quad: true, len: 0.72, h: 0.55, w: 0.26, bh: 0.28, head: 0.2, snout: 0.13, neck: 0.16, ears: 'flop', tail: 'up', walk: 1.5, run: 7.8, fear: 0, hp: 60 },
  cat: { name: 'Cat', quad: true, len: 0.46, h: 0.27, w: 0.15, bh: 0.16, head: 0.12, snout: 0.03, neck: 0.06, ears: 'up', tail: 'long', walk: 0.9, run: 5.6, fear: 6, hp: 25 },
  deer: { name: 'Deer', quad: true, len: 1.25, h: 1.0, w: 0.38, bh: 0.42, head: 0.2, snout: 0.18, neck: 0.46, ears: 'up', tail: 'stub', walk: 1.3, run: 11, fear: 24, hp: 70, antlers: true },
  rabbit: { name: 'Rabbit', quad: true, hop: true, len: 0.32, h: 0.19, w: 0.16, bh: 0.17, head: 0.1, snout: 0.04, neck: 0.04, ears: 'long', tail: 'puff', walk: 0.8, run: 6.5, fear: 10, hp: 12 },
  coyote: { name: 'Coyote', quad: true, len: 0.82, h: 0.6, w: 0.24, bh: 0.28, head: 0.19, snout: 0.16, neck: 0.18, ears: 'up', tail: 'down', walk: 1.4, run: 9.5, fear: 18, hp: 50 },
  cow: { name: 'Cow', quad: true, len: 1.85, h: 1.35, w: 0.66, bh: 0.72, head: 0.3, snout: 0.16, neck: 0.3, ears: 'side', tail: 'thin', walk: 0.9, run: 3.8, fear: 4, hp: 220, horns: true },
  pigeon: { name: 'Pigeon', bird: true, len: 0.3, h: 0.2, walk: 0.5, fly: 8, fear: 5, hp: 5 },
  seagull: { name: 'Seagull', bird: true, len: 0.42, h: 0.25, walk: 0.6, fly: 9, fear: 6, hp: 6 },
  crow: { name: 'Crow', bird: true, len: 0.36, h: 0.22, walk: 0.55, fly: 9, fear: 7, hp: 5 },
};

const BREEDS = {
  lab: { species: 'dog', name: 'Labrador', coat: [0xd4a24c, 0xe2b868, 0xc49240, 0xb88a3c], scale: 1.0, price: 1200 },
  shepherd: { species: 'dog', name: 'German Shepherd', coat: [0x2a211a, 0xa66a2c, 0x2c2218, 0x1d1712], ears: 'up', scale: 1.08, price: 1800, saddle: true },
  husky: { species: 'dog', name: 'Husky', coat: [0x7f858c, 0xeeeeee, 0xf2f2f2, 0x5e646a], ears: 'up', tail: 'curl', scale: 1.0, price: 2000 },
  rottweiler: { species: 'dog', name: 'Rottweiler', coat: [0x141212, 0x8a4b1f, 0x1a1616, 0x121010], scale: 1.12, price: 1600 },
  pug: { species: 'dog', name: 'Pug', coat: [0xcfa874, 0xd9b888, 0x2a2420, 0x2a2420], scale: 0.52, legK: 0.75, snoutK: 0.35, tail: 'curl', price: 900 },
  poodle: { species: 'dog', name: 'Poodle', coat: [0xf2efe8, 0xf2efe8, 0xece8e0, 0xf2efe8], scale: 0.78, price: 1400, fluffy: true },
  tabby: { species: 'cat', name: 'Tabby Cat', coat: [0xd98a3c, 0xf0c890, 0xd98a3c, 0xc0783a], scale: 1, price: 500, stripes: 0xa8602a },
  blackcat: { species: 'cat', name: 'Black Cat', coat: [0x1b1a1c, 0x222124, 0x1b1a1c, 0x19181a], scale: 1, price: 500 },
  siamese: { species: 'cat', name: 'Siamese', coat: [0xeadcc6, 0xf1e6d4, 0x5a4638, 0x4a3a2e], scale: 1, price: 700 },
  deer: { species: 'deer', coat: [0x8a5a33, 0xd8c3a0, 0x6e4526, 0x7a4d2c], scale: 1 },
  rabbit: { species: 'rabbit', coat: [0x8c7a66, 0xd8ccbc, 0x7a6a58, 0x9a8672], scale: 1 },
  coyote: { species: 'coyote', coat: [0x9c8260, 0xd8c8aa, 0x8a7050, 0x6e5a40], scale: 1 },
  cow: { species: 'cow', coat: [0xf0ece4, 0xf0ece4, 0xf0ece4, 0x1b1a18], scale: 1, patches: 0x1b1a18 },
  brown_cow: { species: 'cow', coat: [0x7a4a2a, 0xe8e0d0, 0xe8e0d0, 0x5a3a20], scale: 1 },
  pigeon: { species: 'pigeon', coat: [0x8e929c, 0x6a6f7a, 0x4d6a64, 0xd0d2d8], scale: 1 },
  seagull: { species: 'seagull', coat: [0xf4f4f2, 0xa8adb4, 0xf4f4f2, 0xe8b020], scale: 1 },
  crow: { species: 'crow', coat: [0x18181c, 0x101014, 0x18181c, 0x303036], scale: 1 },
};

const CACHE = new Map();
const rgb = (hex) => { const c = new THREE.Color(hex); return [c.r, c.g, c.b]; };
function ellipsoid(gb, x, y, z, rx, ry, rz, col, seg = 10) {
  gb.set('color', ...col);
  gb.addGeometry(new THREE.SphereGeometry(1, seg, Math.max(6, seg - 2)), mat4(x, y, z, 0, 0, 0, rx, ry, rz));
}
function cyl(gb, a, b, r0, r1, col, seg = 7) {
  const A = new THREE.Vector3(...a), Bv = new THREE.Vector3(...b), d = Bv.clone().sub(A), len = d.length();
  gb.set('color', ...col);
  const q = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 1, 0), d.normalize());
  gb.addGeometry(new THREE.CylinderGeometry(r1, r0, len, seg), new THREE.Matrix4().compose(A.clone().lerp(Bv, 0.5), q, new THREE.Vector3(1, 1, 1)));
}

function buildParts(breedKey) {
  if (CACHE.has(breedKey)) return CACHE.get(breedKey);
  const br = BREEDS[breedKey], sp = SPECIES[br.species], k = br.scale || 1;
  const [cBody, cBelly, cFace, cEar] = br.coat.map(rgb);
  const out = { sp, br, k };
  if (sp.bird) {
    const L = sp.len * k, H = sp.h * k;
    const body = new GeoBuilder();
    ellipsoid(body, 0, H * 0.55, 0, L * 0.22, L * 0.2, L * 0.38, cBody);
    ellipsoid(body, 0, H * 0.45, -0.02, L * 0.18, L * 0.14, L * 0.3, cBelly);
    ellipsoid(body, 0, H * 0.9, L * 0.3, L * 0.13, L * 0.13, L * 0.14, cFace);
    body.set('color', ...rgb(br.species === 'seagull' ? 0xe8b020 : br.species === 'crow' ? 0x202024 : 0x3a3a3a));
    body.addGeometry(new THREE.ConeGeometry(L * 0.04, L * 0.16, 6), mat4(0, H * 0.88, L * 0.48, Math.PI / 2, 0, 0));
    body.set('color', 0.05, 0.05, 0.05);
    for (const s of [-1, 1]) body.addGeometry(new THREE.SphereGeometry(L * 0.025, 5, 4), mat4(s * L * 0.1, H * 0.95, L * 0.38));
    body.set('color', ...cEar);
    body.addGeometry(new THREE.BoxGeometry(L * 0.22, L * 0.03, L * 0.32), mat4(0, H * 0.6, -L * 0.45, -0.25, 0, 0));
    const legC = rgb(br.species === 'pigeon' ? 0xc0504a : br.species === 'seagull' ? 0xd8a060 : 0x202020);
    for (const s of [-1, 1]) cyl(body, [s * L * 0.07, H * 0.4, 0], [s * L * 0.07, 0, L * 0.02], L * 0.015, L * 0.015, legC, 4);
    const wing = new GeoBuilder();
    wing.set('color', ...cBelly);
    wing.box(0, -L * 0.02, -L * 0.28, L * 0.62, L * 0.02, L * 0.22);
    wing.set('color', ...cEar);
    wing.box(L * 0.35, -L * 0.021, -L * 0.3, L * 0.64, L * 0.021, L * 0.05);
    out.body = body.build(); out.wing = wing.build();
    out.wingY = H * 0.68; out.wingX = L * 0.16;
    CACHE.set(breedKey, out);
    return out;
  }
  const L = sp.len * k, H = sp.h * k, W = sp.w * k, BH = sp.bh * k;
  const legK = br.legK || 1;
  const hipY = (H - BH * 0.45) * legK + (1 - legK) * BH * 0.3;
  const bodyY = hipY + BH * 0.3;
  const body = new GeoBuilder();
  ellipsoid(body, 0, bodyY, 0, W * 0.5, BH * 0.5, L * 0.5, cBody, 12);
  ellipsoid(body, 0, bodyY - BH * 0.12, 0.02 * k, W * 0.44, BH * 0.4, L * 0.42, cBelly, 10);
  if (br.saddle) ellipsoid(body, 0, bodyY + BH * 0.18, -L * 0.05, W * 0.46, BH * 0.3, L * 0.36, rgb(0x1a1612), 10);
  if (br.patches) for (const [x, y, z, r] of [[0.8, 0.2, 0.2, 0.3], [-0.8, 0.1, -0.25, 0.28], [0.7, 0.3, -0.3, 0.22], [-0.7, 0.35, 0.3, 0.2]]) ellipsoid(body, x * W * 0.42, bodyY + y * BH * 0.4, z * L, W * 0.22, BH * r, L * 0.18, rgb(br.patches), 8);
  if (br.stripes) for (let s = -2; s <= 2; s++) ellipsoid(body, 0, bodyY + BH * 0.35, s * L * 0.14, W * 0.52, BH * 0.12, L * 0.04, rgb(br.stripes), 8);
  if (br.fluffy) for (const z of [-0.35, 0.35]) ellipsoid(body, 0, bodyY + BH * 0.05, z * L, W * 0.62, BH * 0.62, L * 0.2, cBody, 8);
  if (sp.name === 'Cow') ellipsoid(body, 0, bodyY - BH * 0.5, -L * 0.28, W * 0.18, BH * 0.14, L * 0.1, rgb(0xe8a8a0), 8);
  const neckTop = [0, bodyY + BH * 0.25 + sp.neck * k * (sp.name === 'Deer' ? 1 : 0.6), L * 0.5 + sp.neck * k * 0.35];
  cyl(body, [0, bodyY + BH * 0.1, L * 0.38], neckTop, W * 0.3, W * (sp.name === 'Deer' ? 0.2 : 0.26), cBody, 8);
  out.body = body.build();
  out.headPos = neckTop;
  const head = new GeoBuilder(), HS = sp.head * k;
  ellipsoid(head, 0, HS * 0.2, HS * 0.25, HS * 0.62, HS * 0.58, HS * 0.7, cFace, 10);
  const sn = sp.snout * k * (br.snoutK ?? 1);
  if (sn > 0.02) ellipsoid(head, 0, HS * 0.02, HS * 0.7 + sn * 0.45, HS * 0.34, HS * 0.3, sn * 0.62 + 0.01, cFace, 8);
  head.set('color', 0.06, 0.05, 0.05);
  head.addGeometry(new THREE.SphereGeometry(HS * 0.12, 6, 4), mat4(0, HS * 0.1, HS * 0.72 + sn * 1.05));
  for (const s of [-1, 1]) head.addGeometry(new THREE.SphereGeometry(HS * 0.09, 6, 4), mat4(s * HS * 0.3, HS * 0.35, HS * 0.72));
  const ears = br.ears || sp.ears;
  for (const s of [-1, 1]) {
    head.set('color', ...cEar);
    if (ears === 'up') head.addGeometry(new THREE.ConeGeometry(HS * 0.2, HS * 0.55, 4), mat4(s * HS * 0.34, HS * 0.8, HS * 0.1, 0, 0, -s * 0.2));
    else if (ears === 'long') head.addGeometry(new THREE.BoxGeometry(HS * 0.22, HS * 1.3, HS * 0.1), mat4(s * HS * 0.2, HS * 1.1, 0, -0.3, 0, -s * 0.12));
    else if (ears === 'side') head.addGeometry(new THREE.BoxGeometry(HS * 0.5, HS * 0.12, HS * 0.28), mat4(s * HS * 0.72, HS * 0.45, HS * 0.05, 0, 0, s * 0.3));
    else head.addGeometry(new THREE.BoxGeometry(HS * 0.18, HS * 0.6, HS * 0.4), mat4(s * HS * 0.6, HS * 0.15, HS * 0.15, 0, 0, s * 0.35));
  }
  if (sp.antlers && breedKey === 'deer') {
    const ac = rgb(0xd8c8a8);
    for (const s of [-1, 1]) { cyl(head, [s * HS * 0.2, HS * 0.7, HS * 0.1], [s * HS * 0.9, HS * 1.9, -HS * 0.1], HS * 0.06, HS * 0.04, ac, 5); cyl(head, [s * HS * 0.55, HS * 1.25, 0], [s * HS * 0.6, HS * 1.9, HS * 0.4], HS * 0.04, HS * 0.03, ac, 5); }
  }
  if (sp.horns) for (const s of [-1, 1]) cyl(head, [s * HS * 0.4, HS * 0.7, HS * 0.1], [s * HS * 0.85, HS * 0.95, HS * 0.25], HS * 0.08, HS * 0.04, rgb(0xe8dcc0), 5);
  out.head = head.build();
  const legR = W * (sp.name === 'Cow' ? 0.16 : 0.14);
  const mkLeg = (len, col, paw) => {
    const g = new GeoBuilder();
    cyl(g, [0, 0, 0], [0, -len + legR, 0], legR * 1.25, legR * 0.8, col, 6);
    ellipsoid(g, 0, -len + legR * 0.8, legR * 0.3, legR * 1.05, legR * 0.8, legR * 1.35, paw, 6);
    return g.build();
  };
  out.frontLeg = mkLeg(hipY, cBelly, sp.name === 'Cow' || sp.name === 'Deer' ? rgb(0x2a2420) : cBelly);
  out.hindLeg = sp.hop ? (() => { const g = new GeoBuilder(); ellipsoid(g, 0, -hipY * 0.35, -0.02 * k, W * 0.28, hipY * 0.45, L * 0.2, cBody, 8); ellipsoid(g, 0, -hipY + 0.02, 0.04 * k, W * 0.14, 0.025 * k, L * 0.22, cBelly, 6); return g.build(); })() : out.frontLeg;
  out.hipY = hipY;
  out.legX = W * 0.36; out.legZf = L * 0.36; out.legZh = -L * 0.36;
  const tail = new GeoBuilder(), tt = br.tail || sp.tail;
  if (tt === 'puff') ellipsoid(tail, 0, 0, -0.03 * k, 0.05 * k, 0.05 * k, 0.05 * k, cBelly, 6);
  else if (tt === 'stub') ellipsoid(tail, 0, 0.02 * k, -0.05 * k, W * 0.14, 0.08 * k, 0.05 * k, cBelly, 6);
  else if (tt === 'curl') { ellipsoid(tail, 0, 0.05 * k, -0.03 * k, W * 0.18, W * 0.18, W * 0.18, cBody, 6); }
  else {
    const tl = tt === 'long' ? L * 0.7 : tt === 'thin' ? L * 0.45 : L * 0.5;
    const up = tt === 'down' ? -0.9 : tt === 'thin' ? -1.3 : tt === 'long' ? 0.6 : 0.35;
    cyl(tail, [0, 0, 0], [0, Math.sin(up) * tl, -Math.cos(up) * tl], W * (tt === 'down' ? 0.16 : 0.08), W * (tt === 'down' ? 0.1 : 0.05), cBody, 6);
  }
  out.tail = tail.build();
  out.tailPos = [0, bodyY + BH * 0.2, -L * 0.48];
  CACHE.set(breedKey, out);
  return out;
}

const f = (v) => (v + 0).toFixed(4);
const sums = (g) => {
  const p = g.attributes.position, n = g.attributes.normal, c = g.attributes.color;
  const s = [0, 0, 0, 0, 0, 0, 0, 0, 0];
  for (let i = 0; i < p.count; i++) {
    s[0] += p.getX(i); s[1] += p.getY(i); s[2] += p.getZ(i);
    s[3] += n.getX(i); s[4] += n.getY(i); s[5] += n.getZ(i);
    if (c) { s[6] += c.getX(i); s[7] += c.getY(i); s[8] += c.getZ(i); }
  }
  const t = (a, b) => s.slice(a, b).map((v) => (Math.abs(v) < 0.005 ? 0 : v).toFixed(2)).join(' ');
  return `v ${p.count} i ${g.index ? g.index.count : 0} pos ${t(0, 3)} nor ${t(3, 6)}` + ` col ${t(6, 9)}`;
};
const order = Object.keys(BREEDS);
const out = [];
for (const key of order) {
  const P = buildParts(key);
  out.push(`breed ${key}`);
  const headPos = P.headPos || [0, 0, 0];
  const tailPos = P.tailPos || [0, 0, 0];
  out.push(`bird ${f(P.sp.bird ? 1 : 0)} k ${f(P.k)} wingY ${f(P.wingY)} wingX ${f(P.wingX)} headPos ${f(headPos[0])},${f(headPos[1])},${f(headPos[2])} hipY ${f(P.hipY || 0)} legX ${f(P.legX || 0)} legZf ${f(P.legZf || 0)} legZh ${f(P.legZh || 0)} tailPos ${f(tailPos[0])},${f(tailPos[1])},${f(tailPos[2])}`);
  const parts = { body: P.body, wing: P.wing, head: P.head, frontLeg: P.frontLeg, hindLeg: P.hindLeg, tail: P.tail };
  for (const [k, geom] of Object.entries(parts)) if (geom) out.push(`part ${k} ${sums(geom)}`);
}
console.log(out.join('\n'));