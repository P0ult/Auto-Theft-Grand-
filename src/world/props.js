// Street furniture & vegetation geometry generators (merged, vertex-colored, with per-vertex glow).
import * as THREE from 'three';
import { GeoBuilder, mat4 } from './geom.js';
import { RNG } from '../core/utils.js';

// attribute spec for props: glow = emissive color (linear), sig = traffic-signal code
export const PROP_ATTRS = { position: 3, normal: 3, uv: 2, color: 3, aGlow: 3, aSig: 1 };

function builder() { return new GeoBuilder(PROP_ATTRS); }
function put(gb, geo, color, m, glow = [0, 0, 0], sig = 0) {
  const c = new THREE.Color(color);
  gb.set('color', c.r, c.g, c.b);
  gb.set('aGlow', glow[0], glow[1], glow[2]);
  gb.set('aSig', sig);
  gb.addGeometry(geo, m);
}

const cyl = (rt, rb, h, s = 8) => new THREE.CylinderGeometry(rt, rb, h, s);
const box = (w, h, d) => new THREE.BoxGeometry(w, h, d);
const sph = (r, w = 8, h = 6) => new THREE.SphereGeometry(r, w, h);

export function streetlightGeo() {
  const gb = builder();
  const metal = 0x4a4f55;
  put(gb, cyl(0.09, 0.14, 8, 8), metal, mat4(0, 4, 0));
  put(gb, cyl(0.2, 0.24, 0.5, 8), metal, mat4(0, 0.25, 0));
  // arm reaching out over the road (+z local = toward road)
  put(gb, cyl(0.06, 0.06, 3.2, 6), metal, mat4(0, 7.9, 1.5, Math.PI / 2 - 0.12, 0, 0));
  put(gb, box(0.5, 0.18, 0.9), 0x33373b, mat4(0, 8.05, 3.1));
  put(gb, box(0.38, 0.05, 0.7), 0xfff0d0, mat4(0, 7.94, 3.1), [5.0, 3.6, 1.9]);
  return gb.build();
}

export function trafficLightGeo() {
  const gb = builder();
  const metal = 0x2e3236;
  put(gb, cyl(0.1, 0.12, 5.2, 8), metal, mat4(0, 2.6, 0));
  // head A faces -z/+z traffic (N-S road), head B faces x traffic (E-W road)
  const heads = [
    { m: (y) => mat4(0.0, y, 0.0), face: [0, 0, 1], axis: 0 },
    { m: (y) => mat4(0.0, y, 0.0), face: [1, 0, 0], axis: 1 },
  ];
  for (const h of heads) {
    const ox = h.face[0] * 0.22, oz = h.face[2] * 0.22;
    put(gb, box(h.axis ? 0.3 : 0.4, 1.1, h.axis ? 0.4 : 0.3), 0x1c1e20, mat4(ox * 1.2, 4.6, oz * 1.2));
    for (let k = 0; k < 3; k++) {
      const col = [0xff2a1a, 0xffb000, 0x2aff6a][k];
      const y = 4.95 - k * 0.34;
      const sig = 1 + h.axis * 3 + k; // 1..3 NS(red,yellow,green) 4..6 EW
      const g = new THREE.CircleGeometry(0.12, 10);
      const rot = h.axis ? mat4(ox * 1.2 + h.face[0] * 0.21, y, 0, 0, Math.PI / 2, 0) : mat4(0, y, oz * 1.2 + h.face[2] * 0.21);
      put(gb, g, col, rot, [0, 0, 0], sig);
      // back side for the opposite direction
      const rot2 = h.axis ? mat4(ox * 1.2 - h.face[0] * 0.51, y, 0, 0, -Math.PI / 2, 0) : mat4(0, y, oz * 1.2 - h.face[2] * 0.51, 0, Math.PI, 0);
      put(gb, g, col, rot2, [0, 0, 0], sig);
    }
  }
  return gb.build();
}

export function hydrantGeo() {
  const gb = builder();
  const c = 0xc8281e;
  put(gb, cyl(0.16, 0.18, 0.6, 10), c, mat4(0, 0.3, 0));
  put(gb, sph(0.17, 10, 6), c, mat4(0, 0.62, 0));
  put(gb, cyl(0.06, 0.06, 0.46, 6), c, mat4(0, 0.42, 0, 0, 0, Math.PI / 2));
  put(gb, cyl(0.2, 0.2, 0.06, 10), 0x9a1d15, mat4(0, 0.04, 0));
  return gb.build();
}

export function trashcanGeo() {
  const gb = builder();
  put(gb, cyl(0.3, 0.26, 0.95, 12), 0x2f4f3a, mat4(0, 0.475, 0));
  put(gb, cyl(0.33, 0.33, 0.08, 12), 0x223a2b, mat4(0, 0.98, 0));
  return gb.build();
}

export function dumpsterGeo() {
  const gb = builder();
  put(gb, box(1.9, 1.2, 1.1), 0x2d5e34, mat4(0, 0.7, 0));
  put(gb, box(2.0, 0.08, 1.2), 0x244a2a, mat4(0, 1.34, -0.05, 0.08, 0, 0));
  for (const x of [-0.8, 0.8]) for (const z of [-0.4, 0.4]) put(gb, cyl(0.08, 0.08, 0.12, 8), 0x111111, mat4(x, 0.06, z, Math.PI / 2, 0, 0));
  return gb.build();
}

export function benchGeo() {
  const gb = builder();
  const wood = 0x7a5132, metal = 0x333333;
  for (let k = 0; k < 3; k++) put(gb, box(1.8, 0.05, 0.12), wood, mat4(0, 0.45, -0.14 + k * 0.14));
  for (let k = 0; k < 2; k++) put(gb, box(1.8, 0.12, 0.04), wood, mat4(0, 0.65 + k * 0.16, -0.26, -0.15, 0, 0));
  for (const x of [-0.8, 0.8]) { put(gb, box(0.06, 0.45, 0.4), metal, mat4(x, 0.22, -0.05)); put(gb, box(0.06, 0.5, 0.05), metal, mat4(x, 0.7, -0.25, -0.15, 0, 0)); }
  return gb.build();
}

export function busstopGeo() {
  const gb = builder();
  const metal = 0x5a6068;
  for (const x of [-1.6, 1.6]) put(gb, box(0.08, 2.5, 0.08), metal, mat4(x, 1.25, -0.6));
  put(gb, box(3.4, 0.08, 1.6), 0x3a4048, mat4(0, 2.52, 0));
  put(gb, box(3.3, 2.0, 0.04), 0x8fb3c7, mat4(0, 1.35, -0.62));
  put(gb, box(1.2, 1.8, 0.1), 0xffffff, mat4(1.9, 1.2, -0.6), [1.2, 1.2, 1.1]);
  for (let k = 0; k < 2; k++) put(gb, box(2.4, 0.05, 0.3), 0x6b4a2e, mat4(0, 0.5, -0.35 + k * 0.001));
  return gb.build();
}

export function phoneboothGeo() {
  const gb = builder();
  put(gb, box(0.9, 2.3, 0.9), 0x7e8a95, mat4(0, 1.15, 0));
  put(gb, box(0.95, 0.25, 0.95), 0x2255aa, mat4(0, 2.2, 0), [0.25, 0.5, 1.2]);
  put(gb, box(0.7, 1.4, 0.02), 0x9ec3d8, mat4(0, 1.2, 0.46));
  return gb.build();
}

export function hoopGeo() {
  const gb = builder();
  put(gb, cyl(0.08, 0.08, 3.1, 8), 0x444444, mat4(0, 1.55, -0.6));
  put(gb, box(1.8, 1.05, 0.05), 0xf2f2f2, mat4(0, 3.3, -0.35));
  put(gb, new THREE.TorusGeometry(0.23, 0.02, 6, 16), 0xe05a1c, mat4(0, 3.05, -0.05, Math.PI / 2, 0, 0));
  put(gb, box(0.05, 0.6, 0.7), 0x444444, mat4(0, 3.1, -0.55, 0.5, 0, 0));
  return gb.build();
}

export function fountainGeo() {
  const gb = builder();
  const stone = 0xb7ada0;
  put(gb, cyl(5, 5.2, 0.7, 28), stone, mat4(0, 0.35, 0));
  put(gb, cyl(4.6, 4.6, 0.1, 28), 0x2a6f86, mat4(0, 0.62, 0));
  put(gb, cyl(0.6, 0.9, 2.2, 12), stone, mat4(0, 1.3, 0));
  put(gb, cyl(1.8, 1.4, 0.35, 16), stone, mat4(0, 2.4, 0));
  put(gb, cyl(0.3, 0.4, 1.2, 10), stone, mat4(0, 3.1, 0));
  return gb.build();
}

// Palm: curved tapered trunk + frond cards (separate geometries for different materials)
export function palmGeos(variant = 0) {
  const r = new RNG(100 + variant);
  const height = 9 + variant * 1.3;
  const segs = 10;
  const bend = r.range(0.4, 1.2);
  const trunk = builder();
  const pts = [];
  for (let i = 0; i <= segs; i++) {
    const t = i / segs;
    pts.push(new THREE.Vector3(Math.sin(t * 1.4) * bend * t, t * height, 0));
  }
  const curve = new THREE.CatmullRomCurve3(pts);
  const tube = new THREE.TubeGeometry(curve, 14, 0.22, 7, false);
  // taper: scale radius by height
  const p = tube.attributes.position;
  for (let i = 0; i < p.count; i++) {
    const y = p.getY(i);
    const t = y / height;
    const center = curve.getPoint(Math.min(1, Math.max(0, t)));
    const k = 1.25 - t * 0.5 + (Math.sin(y * 9) > 0.6 ? 0.06 : 0);
    p.setX(i, center.x + (p.getX(i) - center.x) * k);
    p.setZ(i, center.z + (p.getZ(i) - center.z) * k);
  }
  tube.computeVertexNormals();
  put(trunk, tube, 0x8a7458, new THREE.Matrix4());
  const top = curve.getPoint(1);
  put(trunk, sph(0.45, 8, 6), 0x6b5a3c, mat4(top.x, top.y - 0.1, top.z));
  // fronds
  const fr = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
  const n = 11;
  for (let k = 0; k < n; k++) {
    const ang = (k / n) * Math.PI * 2 + r.range(-0.15, 0.15);
    const len = r.range(3.6, 4.6);
    const droop = r.range(0.5, 1.1);
    const w = 1.5;
    // frond as a bent strip of 4 segments
    const seg = 5;
    const dirx = Math.cos(ang), dirz = Math.sin(ang);
    const sx = -dirz, sz = dirx;
    const shade = r.range(0.85, 1.1);
    fr.set('color', shade, shade, shade);
    let prevL = null, prevR = null;
    for (let s = 0; s <= seg; s++) {
      const t = s / seg;
      const d = t * len;
      const y = top.y + 0.3 + Math.sin(t * Math.PI * 0.6) * 0.8 - t * t * droop * 2.2;
      const cx = top.x + dirx * d, cz = top.z + dirz * d;
      const hw = w * 0.5;
      const L = [cx - sx * hw, y + 0.05, cz - sz * hw], R = [cx + sx * hw, y + 0.05, cz + sz * hw];
      if (prevL) {
        const nx = 0, ny = 1, nz = 0;
        const a = fr.vertex(prevL[0], prevL[1], prevL[2], nx, ny, nz, 0, (s - 1) / seg);
        const b = fr.vertex(prevR[0], prevR[1], prevR[2], nx, ny, nz, 1, (s - 1) / seg);
        const c = fr.vertex(R[0], R[1], R[2], nx, ny, nz, 1, t);
        const d2 = fr.vertex(L[0], L[1], L[2], nx, ny, nz, 0, t);
        fr.tri(a, b, c); fr.tri(a, c, d2);
      }
      prevL = L; prevR = R;
    }
  }
  return { trunk: trunk.build(), fronds: fr.build(), height };
}

export function treeGeos(variant = 0) {
  const r = new RNG(300 + variant);
  const trunk = builder();
  const h = 3 + variant * 0.6;
  put(trunk, cyl(0.16, 0.26, h, 7), 0x5a4330, mat4(0, h / 2, 0));
  put(trunk, cyl(0.07, 0.1, 1.8, 5), 0x5a4330, mat4(0.4, h - 0.2, 0, 0, 0, -0.6));
  put(trunk, cyl(0.07, 0.1, 1.6, 5), 0x5a4330, mat4(-0.35, h - 0.3, 0.2, 0.3, 0, 0.6));
  const leaves = builder();
  const blobs = 5 + variant;
  for (let k = 0; k < blobs; k++) {
    const g = new THREE.IcosahedronGeometry(r.range(1.3, 2.0), 1);
    const p = g.attributes.position;
    for (let i = 0; i < p.count; i++) {
      const s = 1 + (r.next() - 0.5) * 0.25;
      p.setXYZ(i, p.getX(i) * s, p.getY(i) * s * 0.85, p.getZ(i) * s);
    }
    g.computeVertexNormals();
    const col = new THREE.Color().setHSL(0.26 + r.range(-0.04, 0.03), 0.45, 0.22 + r.range(-0.04, 0.06));
    put(leaves, g, col, mat4(r.range(-1.3, 1.3), h + 0.8 + r.range(-0.3, 1.4), r.range(-1.3, 1.3)));
  }
  return { trunk: trunk.build(), leaves: leaves.build(), height: h + 3 };
}

export function containerGeo() {
  const gb = builder();
  // 2.5 x 2.6 x 6.1 corrugated container, color set per instance
  put(gb, box(2.44, 2.59, 6.06), 0xffffff, mat4(0, 1.295, 0));
  for (let k = -9; k <= 9; k++) {
    put(gb, box(2.48, 2.4, 0.08), 0xe8e8e8, mat4(0, 1.3, k * 0.32));
  }
  put(gb, box(2.3, 2.4, 0.05), 0xcccccc, mat4(0, 1.3, 3.05));
  return gb.build();
}

export function billboardGeo() {
  const gb = builder();
  const m = 0x4a4d52;
  put(gb, box(0.3, 5, 0.3), m, mat4(-3, 2.5, 0.4));
  put(gb, box(0.3, 5, 0.3), m, mat4(3, 2.5, 0.4));
  put(gb, box(10.4, 5.4, 0.3), 0x2a2c30, mat4(0, 7.5, 0.2));
  put(gb, box(10.6, 0.15, 1.0), m, mat4(0, 4.8, 0.7));
  return gb.build();
}
