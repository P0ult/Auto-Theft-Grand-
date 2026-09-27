// Surface helpers for smooth procedural bodywork: lofted grids with smooth normals that can be split
// into several meshes (paint, glass, panels that open or break off), swept tubes and rounded boxes.
import * as THREE from 'three';

// Monotone cubic through [[x, y], ...] (no overshoot), clamped at the ends
export function smoothLine(pts) {
  const P = pts.slice().sort((a, b) => a[0] - b[0]);
  const n = P.length, xs = P.map((p) => p[0]), ys = P.map((p) => p[1]);
  if (n === 1) return () => ys[0];
  const d = [], m = new Array(n).fill(0);
  for (let i = 0; i < n - 1; i++) d.push((ys[i + 1] - ys[i]) / Math.max(1e-6, xs[i + 1] - xs[i]));
  m[0] = d[0]; m[n - 1] = d[n - 2];
  for (let i = 1; i < n - 1; i++) m[i] = d[i - 1] * d[i] <= 0 ? 0 : (d[i - 1] + d[i]) / 2;
  for (let i = 0; i < n - 1; i++) {
    if (d[i] === 0) { m[i] = 0; m[i + 1] = 0; continue; }
    const a = m[i] / d[i], b = m[i + 1] / d[i], s = a * a + b * b;
    if (s > 9) { const t = 3 / Math.sqrt(s); m[i] = t * a * d[i]; m[i + 1] = t * b * d[i]; }
  }
  return (x) => {
    if (x <= xs[0]) return ys[0];
    if (x >= xs[n - 1]) return ys[n - 1];
    let i = 0;
    while (x > xs[i + 1]) i++;
    const h = xs[i + 1] - xs[i], t = (x - xs[i]) / h, t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * ys[i] + (t3 - 2 * t2 + t) * h * m[i] + (-2 * t3 + 3 * t2) * ys[i + 1] + (t3 - t2) * h * m[i + 1];
  };
}

// sorted, de-duplicated stations from a..b every `step`, always including the given feature positions
export function stations(a, b, step, extra = []) {
  const out = [];
  const n = Math.max(1, Math.ceil((b - a) / step));
  for (let i = 0; i <= n; i++) out.push(a + (b - a) * i / n);
  for (const e of extra) if (e > a + 1e-4 && e < b - 1e-4) out.push(e);
  out.sort((p, q) => p - q);
  const res = [];
  for (const z of out) if (!res.length || z - res[res.length - 1] > 0.012) res.push(z);
  res[res.length - 1] = b;
  return res;
}

// Target mesh accumulator (positions, normals, colors, optional uv) with per-grid vertex sharing
export class Part {
  constructor() { this.P = []; this.N = []; this.C = []; this.I = []; this.n = 0; this.col = [1, 1, 1]; }
  color(r, g, b) { this.col = [r, g, b]; return this; }
  hex(h) { const c = new THREE.Color(h); this.col = [c.r, c.g, c.b]; return this; }
  v(x, y, z, nx, ny, nz, col = this.col) { this.P.push(x, y, z); this.N.push(nx, ny, nz); this.C.push(col[0], col[1], col[2]); return this.n++; }
  tri(a, b, c) { this.I.push(a, b, c); }
  // flat polygon (convex, in order) with a given normal
  poly(pts, n) {
    const i0 = this.v(pts[0][0], pts[0][1], pts[0][2], n[0], n[1], n[2]);
    let prev = this.v(pts[1][0], pts[1][1], pts[1][2], n[0], n[1], n[2]);
    for (let k = 2; k < pts.length; k++) {
      const cur = this.v(pts[k][0], pts[k][1], pts[k][2], n[0], n[1], n[2]);
      // wind so the face points along n
      const ax = pts[k - 1][0] - pts[0][0], ay = pts[k - 1][1] - pts[0][1], az = pts[k - 1][2] - pts[0][2];
      const bx = pts[k][0] - pts[0][0], by = pts[k][1] - pts[0][1], bz = pts[k][2] - pts[0][2];
      const cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
      if (cx * n[0] + cy * n[1] + cz * n[2] >= 0) this.tri(i0, prev, cur); else this.tri(i0, cur, prev);
      prev = cur;
    }
  }
  // axis-aligned box (flat shaded)
  box(x0, y0, z0, x1, y1, z1, bottom = false) {
    const q = (a, b, c, d, n) => this.poly([a, b, c, d], n);
    q([x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1], [0, 0, 1]);
    q([x1, y0, z0], [x0, y0, z0], [x0, y1, z0], [x1, y1, z0], [0, 0, -1]);
    q([x1, y0, z1], [x1, y0, z0], [x1, y1, z0], [x1, y1, z1], [1, 0, 0]);
    q([x0, y0, z0], [x0, y0, z1], [x0, y1, z1], [x0, y1, z0], [-1, 0, 0]);
    q([x0, y1, z1], [x1, y1, z1], [x1, y1, z0], [x0, y1, z0], [0, 1, 0]);
    if (bottom) q([x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1], [0, -1, 0]);
  }
  // merge a THREE geometry (keeps its smooth normals) through a matrix
  geo(g, matrix) {
    const pos = g.attributes.position, nor = g.attributes.normal;
    const v = new THREE.Vector3(), nn = new THREE.Vector3();
    const nm = new THREE.Matrix3().getNormalMatrix(matrix);
    const base = this.n;
    for (let i = 0; i < pos.count; i++) {
      v.fromBufferAttribute(pos, i).applyMatrix4(matrix);
      nn.fromBufferAttribute(nor, i).applyMatrix3(nm).normalize();
      this.v(v.x, v.y, v.z, nn.x, nn.y, nn.z);
    }
    if (g.index) for (let i = 0; i < g.index.count; i++) this.I.push(base + g.index.getX(i));
    else for (let i = 0; i < pos.count; i++) this.I.push(base + i);
  }
  get empty() { return this.n === 0; }
  build() {
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(this.P, 3));
    g.setAttribute('normal', new THREE.Float32BufferAttribute(this.N, 3));
    g.setAttribute('color', new THREE.Float32BufferAttribute(this.C, 3));
    g.setAttribute('uv', new THREE.Float32BufferAttribute(new Float32Array(this.n * 2), 2));
    g.setIndex(this.n > 65535 ? new THREE.Uint32BufferAttribute(this.I, 1) : new THREE.Uint16BufferAttribute(this.I, 1));
    g.computeBoundingSphere(); g.computeBoundingBox();
    return g;
  }
}

// Emit a grid rows[i][j] = [x, y, z] (i: along the body, j: around the section) with smooth normals.
// pick(i, j) -> Part (or null to leave the quad out). ref: a point inside the solid, to face the normals out
// (inward = true faces them in). colorAt(x, y, z, part) -> [r, g, b] optional. offset: push along the normal.
export function emitGrid(rows, pick, { ref = [0, 0.8, 0], inward = false, colorAt = null, offset = null } = {}) {
  const ni = rows.length, nj = rows[0].length;
  if (ni < 2 || nj < 2) return;
  const N = [];
  const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
  const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  for (let i = 0; i < ni; i++) {
    const row = [];
    for (let j = 0; j < nj; j++) {
      let du = sub(rows[Math.min(ni - 1, i + 1)][j], rows[Math.max(0, i - 1)][j]);
      let dv = sub(rows[i][Math.min(nj - 1, j + 1)], rows[i][Math.max(0, j - 1)]);
      // degenerate (a collapsed edge): look one step further in
      if (Math.hypot(...du) < 1e-6) du = sub(rows[Math.min(ni - 1, i + 1)][Math.min(nj - 1, j + 1)], rows[Math.max(0, i - 1)][Math.max(0, j - 1)]);
      if (Math.hypot(...dv) < 1e-6) dv = sub(rows[Math.min(ni - 1, i + 1)][Math.min(nj - 1, j + 1)], rows[Math.max(0, i - 1)][Math.max(0, j - 1)]);
      let n = cross(du, dv);
      const l = Math.hypot(...n) || 1;
      row.push([n[0] / l, n[1] / l, n[2] / l]);
    }
    N.push(row);
  }
  // orientation from the middle of the grid
  let s = 0;
  for (let i = 0; i < ni; i += Math.max(1, ni >> 2)) for (let j = 0; j < nj; j += Math.max(1, nj >> 2)) {
    const p = rows[i][j], n = N[i][j];
    s += (p[0] - ref[0]) * n[0] + (p[1] - ref[1]) * n[1] + (p[2] - ref[2]) * n[2];
  }
  const flip = (s < 0) !== inward;
  if (flip) for (const row of N) for (const n of row) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
  const maps = new Map();
  const vid = (part, i, j) => {
    let m = maps.get(part);
    if (!m) { m = new Map(); maps.set(part, m); }
    const k = i * 4096 + j;
    let id = m.get(k);
    if (id === undefined) {
      const p = rows[i][j], n = N[i][j];
      const o = offset ? offset(part) : 0;
      const x = p[0] + n[0] * o, y = p[1] + n[1] * o, z = p[2] + n[2] * o;
      id = part.v(x, y, z, n[0], n[1], n[2], colorAt ? colorAt(x, y, z, part) : part.col);
      m.set(k, id);
    }
    return id;
  };
  for (let i = 0; i < ni - 1; i++) for (let j = 0; j < nj - 1; j++) {
    const part = pick(i, j);
    if (!part) continue;
    const a = vid(part, i, j), b = vid(part, i + 1, j), c = vid(part, i + 1, j + 1), d = vid(part, i, j + 1);
    // wind to match the normals
    const pa = rows[i][j], pb = rows[i + 1][j], pc = rows[i + 1][j + 1];
    let g = cross(sub(pb, pa), sub(pc, pa));
    if (Math.hypot(...g) < 1e-9) g = cross(sub(pc, pa), sub(rows[i][j + 1], pa));
    const nn = N[i][j];
    if (g[0] * nn[0] + g[1] * nn[1] + g[2] * nn[2] >= 0) { part.tri(a, b, c); part.tri(a, c, d); } else { part.tri(a, c, b); part.tri(a, d, c); }
  }
}

// rounded rectangle outline (half width hw, from y0 to y1, corner radius r), counter-clockwise, as [x, y]
export function roundRect(hw, y0, y1, r, seg = 4) {
  r = Math.min(r, hw, (y1 - y0) / 2);
  const out = [];
  const arc = (cx, cy, a0) => { for (let k = 0; k <= seg; k++) { const a = a0 + (k / seg) * Math.PI / 2; out.push([cx + Math.cos(a) * r, cy + Math.sin(a) * r]); } };
  arc(hw - r, y0 + r, -Math.PI / 2); arc(hw - r, y1 - r, 0); arc(-hw + r, y1 - r, Math.PI / 2); arc(-hw + r, y0 + r, Math.PI);
  return out;
}

// Sweep a closed 2D section [[u, v]] (u: sideways/outward, v: up) along a path of [x, y, z] points.
// The section's u axis lies along the path's horizontal outward normal (to the path's left when `left`).
export function sweep(part, path, section, { closeEnds = true, left = false } = {}) {
  const rows = [];
  for (let i = 0; i < path.length; i++) {
    const a = path[Math.max(0, i - 1)], b = path[Math.min(path.length - 1, i + 1)];
    let tx = b[0] - a[0], tz = b[2] - a[2];
    const l = Math.hypot(tx, tz) || 1; tx /= l; tz /= l;
    const ox = left ? -tz : tz, oz = left ? tx : -tx; // horizontal normal
    const row = [];
    for (const [u, v] of section) row.push([path[i][0] + ox * u, path[i][1] + v, path[i][2] + oz * u]);
    row.push(row[0].slice());
    rows.push(row);
  }
  const c = [0, 0, 0];
  for (const p of path) { c[0] += p[0] / path.length; c[1] += p[1] / path.length; c[2] += p[2] / path.length; }
  // normals per ring point from the section (outward from the ring's own centre)
  const N = [];
  let su = 0, sv = 0;
  for (const [u, v] of section) { su += u / section.length; sv += v / section.length; }
  const base = part.n;
  const idx = [];
  for (let i = 0; i < rows.length; i++) {
    const a = path[Math.max(0, i - 1)], b = path[Math.min(path.length - 1, i + 1)];
    let tx = b[0] - a[0], tz = b[2] - a[2];
    const l = Math.hypot(tx, tz) || 1; tx /= l; tz /= l;
    const ox = left ? -tz : tz, oz = left ? tx : -tx;
    const ir = [];
    for (let j = 0; j <= section.length; j++) {
      const [u, v] = section[j % section.length];
      // section normal: from the neighbours around the ring
      const p0 = section[(j - 1 + section.length) % section.length], p1 = section[(j + 1) % section.length];
      let nu = p1[1] - p0[1], nv = -(p1[0] - p0[0]);
      if (nu * (u - su) + nv * (v - sv) < 0) { nu = -nu; nv = -nv; }
      const ll = Math.hypot(nu, nv) || 1; nu /= ll; nv /= ll;
      const p = rows[i][j];
      ir.push(part.v(p[0], p[1], p[2], ox * nu, nv, oz * nu));
    }
    idx.push(ir);
  }
  for (let i = 0; i < rows.length - 1; i++) for (let j = 0; j < section.length; j++) {
    const a = idx[i][j], b = idx[i + 1][j], cc = idx[i + 1][j + 1], d = idx[i][j + 1];
    // orient by the first vertex normal
    const pa = rows[i][j], pb = rows[i + 1][j], pc = rows[i + 1][j + 1];
    const g = [(pb[1] - pa[1]) * (pc[2] - pa[2]) - (pb[2] - pa[2]) * (pc[1] - pa[1]), (pb[2] - pa[2]) * (pc[0] - pa[0]) - (pb[0] - pa[0]) * (pc[2] - pa[2]), (pb[0] - pa[0]) * (pc[1] - pa[1]) - (pb[1] - pa[1]) * (pc[0] - pa[0])];
    const k = a * 3, n = [part.N[k], part.N[k + 1], part.N[k + 2]];
    if (g[0] * n[0] + g[1] * n[1] + g[2] * n[2] >= 0) { part.tri(a, b, cc); part.tri(a, cc, d); } else { part.tri(a, cc, b); part.tri(a, d, cc); }
  }
  if (closeEnds) {
    for (const i of [0, rows.length - 1]) {
      const ring = rows[i].slice(0, section.length);
      const a = path[Math.max(0, i - 1)], b = path[Math.min(path.length - 1, i + 1)];
      let tx = b[0] - a[0], ty = b[1] - a[1], tz = b[2] - a[2];
      const l = Math.hypot(tx, ty, tz) || 1;
      const s = i === 0 ? -1 : 1;
      part.poly(ring, [tx / l * s, ty / l * s, tz / l * s]);
    }
  }
  return base;
}

// A box with rounded vertical edges along z (a lofted rounded rectangle with flat ends)
export function roundBox(part, x0, x1, y0, y1, z0, z1, r, seg = 3) {
  const hw = (x1 - x0) / 2, cx = (x0 + x1) / 2;
  const sec = roundRect(hw, y0, y1, r, seg).map(([x, y]) => [x + cx, y]);
  const rows = [z0, z1].map((z) => { const row = sec.map(([x, y]) => [x, y, z]); row.push(row[0].slice()); return row; });
  const ref = [cx, (y0 + y1) / 2, (z0 + z1) / 2];
  emitGrid(rows, () => part, { ref });
  // flat ends
  part.poly(sec.map(([x, y]) => [x, y, z1]), [0, 0, 1]);
  part.poly(sec.map(([x, y]) => [x, y, z0]).reverse(), [0, 0, -1]);
}
