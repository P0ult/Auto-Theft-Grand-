// Lightweight geometry accumulator with arbitrary float attributes + helpers for boxes/quads.
import * as THREE from 'three';

export class GeoBuilder {
  constructor(attrs = { position: 3, normal: 3, uv: 2, color: 3 }) {
    this.spec = attrs;
    this.data = {};
    for (const k in attrs) this.data[k] = [];
    this.index = [];
    this.count = 0;
    this.cur = {};
    for (const k in attrs) this.cur[k] = new Array(attrs[k]).fill(k === 'color' ? 1 : 0);
  }
  set(name, ...v) { const c = this.cur[name]; for (let i = 0; i < c.length; i++) c[i] = v[i] ?? 0; return this; }
  vertex(x, y, z, nx, ny, nz, u = 0, v = 0) {
    const d = this.data;
    d.position.push(x, y, z);
    if (d.normal) d.normal.push(nx, ny, nz);
    if (d.uv) d.uv.push(u, v);
    for (const k in this.spec) {
      if (k === 'position' || k === 'normal' || k === 'uv') continue;
      const c = this.cur[k];
      for (let i = 0; i < c.length; i++) d[k].push(c[i]);
    }
    return this.count++;
  }
  tri(a, b, c) { this.index.push(a, b, c); }
  // Quad from 4 corners (counter-clockwise when viewed from the front), with uvs
  quad(p0, p1, p2, p3, n, uv0 = [0, 0], uv1 = [1, 0], uv2 = [1, 1], uv3 = [0, 1]) {
    const a = this.vertex(p0[0], p0[1], p0[2], n[0], n[1], n[2], uv0[0], uv0[1]);
    const b = this.vertex(p1[0], p1[1], p1[2], n[0], n[1], n[2], uv1[0], uv1[1]);
    const c = this.vertex(p2[0], p2[1], p2[2], n[0], n[1], n[2], uv2[0], uv2[1]);
    const d = this.vertex(p3[0], p3[1], p3[2], n[0], n[1], n[2], uv3[0], uv3[1]);
    this.index.push(a, b, c, a, c, d);
  }
  // Axis-aligned box. faces: object {top,bottom,sides} booleans. uvScale: meters per uv unit
  box(x0, y0, z0, x1, y1, z1, opts = {}) {
    const { top = true, bottom = false, sides = true, uvs = 1 } = opts;
    const u = (v) => v / uvs;
    if (sides) {
      // -z face (north), normal (0,0,-1): viewed from -z, CCW: (x1,y0,z0) (x0,y0,z0) (x0,y1,z0) (x1,y1,z0)
      this.quad([x1, y0, z0], [x0, y0, z0], [x0, y1, z0], [x1, y1, z0], [0, 0, -1], [0, u(y0)], [u(x1 - x0), u(y0)], [u(x1 - x0), u(y1)], [0, u(y1)]);
      this.quad([x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1], [0, 0, 1], [0, u(y0)], [u(x1 - x0), u(y0)], [u(x1 - x0), u(y1)], [0, u(y1)]);
      this.quad([x0, y0, z0], [x0, y0, z1], [x0, y1, z1], [x0, y1, z0], [-1, 0, 0], [0, u(y0)], [u(z1 - z0), u(y0)], [u(z1 - z0), u(y1)], [0, u(y1)]);
      this.quad([x1, y0, z1], [x1, y0, z0], [x1, y1, z0], [x1, y1, z1], [1, 0, 0], [0, u(y0)], [u(z1 - z0), u(y0)], [u(z1 - z0), u(y1)], [0, u(y1)]);
    }
    if (top) this.quad([x0, y1, z1], [x1, y1, z1], [x1, y1, z0], [x0, y1, z0], [0, 1, 0], [u(x0), u(z1)], [u(x1), u(z1)], [u(x1), u(z0)], [u(x0), u(z0)]);
    if (bottom) this.quad([x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1], [0, -1, 0]);
  }
  // Merge a THREE.BufferGeometry transformed by matrix, applying current custom attributes
  addGeometry(geo, matrix) {
    const g = geo.index ? geo.toNonIndexed() : geo;
    const pos = g.attributes.position, nor = g.attributes.normal, uv = g.attributes.uv, col = g.attributes.color;
    const v = new THREE.Vector3(), n = new THREE.Vector3();
    const nm = new THREE.Matrix3().getNormalMatrix(matrix);
    const base = this.count;
    const savedColor = this.cur.color ? this.cur.color.slice() : null;
    for (let i = 0; i < pos.count; i++) {
      v.fromBufferAttribute(pos, i).applyMatrix4(matrix);
      if (nor) n.fromBufferAttribute(nor, i).applyMatrix3(nm).normalize(); else n.set(0, 1, 0);
      if (col && this.cur.color) { this.cur.color[0] = savedColor[0] * col.getX(i); this.cur.color[1] = savedColor[1] * col.getY(i); this.cur.color[2] = savedColor[2] * col.getZ(i); }
      this.vertex(v.x, v.y, v.z, n.x, n.y, n.z, uv ? uv.getX(i) : 0, uv ? uv.getY(i) : 0);
    }
    if (savedColor) this.cur.color = savedColor;
    for (let i = 0; i < pos.count; i++) this.index.push(base + i);
    if (g !== geo) g.dispose();
  }
  build() {
    const geo = new THREE.BufferGeometry();
    for (const k in this.spec) {
      if (this.data[k].length === 0 && k !== 'position') continue;
      geo.setAttribute(k, new THREE.Float32BufferAttribute(this.data[k], this.spec[k]));
    }
    geo.setIndex(this.count > 65535 ? new THREE.Uint32BufferAttribute(this.index, 1) : new THREE.Uint16BufferAttribute(this.index, 1));
    geo.computeBoundingSphere();
    geo.computeBoundingBox();
    return geo;
  }
  get empty() { return this.count === 0; }
  // rotate the vertices added since `start` about (cx, cz) by yaw (positions + normals)
  rotateFrom(start, cx, cz, yaw) {
    if (!yaw) return;
    const s = Math.sin(yaw), c = Math.cos(yaw);
    const P = this.data.position, N = this.data.normal;
    for (let i = start; i < this.count; i++) {
      const x = P[i * 3] - cx, z = P[i * 3 + 2] - cz;
      P[i * 3] = cx + x * c + z * s; P[i * 3 + 2] = cz - x * s + z * c;
      if (N) { const nx = N[i * 3], nz = N[i * 3 + 2]; N[i * 3] = nx * c + nz * s; N[i * 3 + 2] = -nx * s + nz * c; }
    }
  }
}

// Build a geometry with vertex colors from a list of [geometry, color, matrix]
export function mergeColored(parts) {
  const gb = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
  for (const [geo, color, matrix] of parts) {
    const c = new THREE.Color(color);
    gb.set('color', c.r, c.g, c.b);
    gb.addGeometry(geo, matrix || new THREE.Matrix4());
  }
  return gb.build();
}

export function mat4(x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0, sx = 1, sy = 1, sz = 1) {
  const m = new THREE.Matrix4();
  m.compose(new THREE.Vector3(x, y, z), new THREE.Quaternion().setFromEuler(new THREE.Euler(rx, ry, rz)), new THREE.Vector3(sx, sy, sz));
  return m;
}
