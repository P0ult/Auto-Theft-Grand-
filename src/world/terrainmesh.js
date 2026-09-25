// Terrain renderer: the heightfield split into 256 m chunks with three levels of detail (4 / 16 / 64 m)
// and skirts to hide the seams between levels. A biome shader paints golden California grass, crop
// fields with rows, forest floor, desert sand with ripples, striped mesa cliffs, rock and beaches.
import * as THREE from 'three';
import { std } from '../render/materials.js';
import { NOISE_GLSL } from './shaders.js';
import { WORLD, CITY_RECT, regionWeights, cityDist, riverDist } from './worldgen.js';
import { farmMask } from './countryside.js';
import { clamp, smoothstep } from '../core/utils.js';

const CH = 256;
const LODS = [4, 16, 64];

const TERRAIN_EXT = {
  key: 'terrain2',
  vertexPars: 'attribute vec4 aT; varying vec4 vT; varying vec3 vTN;',
  vertexMain: 'vT = aT; vTN = normal;',
  fragPars: NOISE_GLSL + `
varying vec4 vT; varying vec3 vTN;
float atgRough = 0.95;
vec3 fields(vec2 wp, float n) {
  // rotated patchwork of fields with crop rows and dirt tracks between them
  float a = 0.33; mat2 R = mat2(cos(a), -sin(a), sin(a), cos(a));
  vec2 q = R * wp;
  vec2 size = vec2(130.0, 85.0);
  vec2 cell = floor(q / size);
  vec2 f = fract(q / size) * size;
  float h = h21(cell * 1.37 + 3.1);
  float edge = min(min(f.x, size.x - f.x), min(f.y, size.y - f.y));
  vec3 c;
  float rows = 0.5 + 0.5 * sin((h > 0.5 ? f.x : f.y) * 6.2831 / 1.7);
  if (h < 0.28) c = mix(vec3(0.62, 0.5, 0.22), vec3(0.78, 0.65, 0.3), rows * 0.6 + n * 0.3);          // wheat
  else if (h < 0.5) c = mix(vec3(0.14, 0.26, 0.07), vec3(0.24, 0.38, 0.1), rows);                        // green crop rows
  else if (h < 0.66) c = mix(vec3(0.3, 0.21, 0.14), vec3(0.4, 0.3, 0.2), rows);                           // ploughed
  else if (h < 0.82) c = mix(vec3(0.34, 0.42, 0.16), vec3(0.28, 0.34, 0.12), n);                          // pasture
  else c = mix(vec3(0.5, 0.52, 0.2), vec3(0.18, 0.4, 0.12), rows * 0.8);                                  // lettuce / hay
  c = mix(vec3(0.42, 0.34, 0.24), c, smoothstep(1.2, 3.0, edge));                                          // tracks
  return c * (0.88 + 0.24 * n);
}
`,
  fragColor: `
{
  vec2 wp = vAtgWorld.xz;
  float n = fbm3(wp * 0.05);
  float n2 = vn2(wp * 0.9);
  float n3 = fbm3(wp * 0.35);
  float farm = vT.x, sand = vT.y, forest = vT.z, beach = vT.w;
  vec3 base = diffuseColor.rgb;
  // grass: golden / green mix
  vec3 grass = mix(vec3(0.26, 0.3, 0.11), vec3(0.46, 0.39, 0.18), smoothstep(0.35, 0.7, n)) * (0.8 + 0.35 * n3) * (0.9 + 0.2 * n2);
  grass = mix(grass, base, 0.35);
  vec3 col = grass;
  // forest floor
  vec3 ff = mix(vec3(0.16, 0.2, 0.09), vec3(0.24, 0.2, 0.12), smoothstep(0.4, 0.7, n3)) * (0.85 + 0.3 * n2);
  col = mix(col, ff, forest);
  // desert sand with ripples and pebbles
  float rip = sin(wp.x * 0.9 + vn2(wp * 0.05) * 9.0) * 0.5 + 0.5;
  vec3 ds = mix(vec3(0.5, 0.34, 0.2), vec3(0.6, 0.44, 0.28), smoothstep(0.3, 0.7, n)) * (0.92 + 0.08 * rip) * (0.9 + 0.15 * n3);
  ds *= 1.0 - smoothstep(0.82, 0.9, vn2(wp * 3.0)) * 0.3;
  col = mix(col, ds, sand);
  // fields
  if (farm > 0.01) col = mix(col, fields(wp, n3), smoothstep(0.1, 0.6, farm));
  // beach sand, darker when wet by the water line
  vec3 bs = mix(vec3(0.66, 0.56, 0.4), vec3(0.5, 0.42, 0.3), smoothstep(1.2, 0.2, vAtgWorld.y));
  col = mix(col, bs * (0.92 + 0.12 * n3), beach);
  // rock on steep slopes (mesa cliffs get red strata)
  vec3 wn = normalize(vTN);
  float steep = smoothstep(0.78, 0.55, wn.y);
  float strata = 0.5 + 0.5 * sin(vAtgWorld.y * 1.3 + n * 4.0);
  vec3 rock = mix(vec3(0.38, 0.35, 0.31), vec3(0.62, 0.36, 0.22) * (0.8 + 0.3 * strata), sand);
  rock *= 0.72 + 0.45 * n3;
  col = mix(col, rock, steep);
  // bare rock high up
  col = mix(col, vec3(0.5, 0.48, 0.45) * (0.8 + 0.3 * n3), smoothstep(470.0, 620.0, vAtgWorld.y) * (1.0 - steep) * 0.7);
  col *= 1.0 - uWet * 0.22;
  diffuseColor.rgb = col;
  atgRough = mix(0.96, 0.9, sand);
}
`,
  fragRoughness: 'roughnessFactor = atgRough;',
};

export class TerrainMesh {
  constructor(scene, map) {
    this.map = map;
    this.hf = map.hf;
    this.root = new THREE.Group();
    this.root.name = 'terrain';
    scene.add(this.root);
    this.mat = std({ color: 0xffffff, vertexColors: true, roughness: 0.95 }, TERRAIN_EXT);
    this.chunks = [];
    this.nx = Math.ceil((WORLD.maxX - WORLD.minX) / CH);
    this.nz = Math.ceil((WORLD.maxZ - WORLD.minZ) / CH);
    this.buildQueue = [];
    for (let j = 0; j < this.nz; j++) for (let i = 0; i < this.nx; i++) {
      const x0 = WORLD.minX + i * CH, z0 = WORLD.minZ + j * CH;
      const c = { i, j, x0, z0, cx: x0 + CH / 2, cz: z0 + CH / 2, meshes: [null, null, null], lod: -1 };
      // chunks fully inside the city are covered by the city ground
      const inside = x0 > CITY_RECT.minX + 2 && x0 + CH < CITY_RECT.maxX - 2 && z0 > CITY_RECT.minZ + 2 && z0 + CH < CITY_RECT.maxZ - 2;
      if (inside) continue;
      c.meshes[1] = this._mesh(c, 1);
      c.meshes[2] = this._mesh(c, 2);
      this.root.add(c.meshes[1], c.meshes[2]);
      c.meshes[1].visible = false;
      this.chunks.push(c);
    }
    this._t = 0;
  }

  // biome weights & colour per vertex
  _attrs(x, z, h) {
    const w = regionWeights(x, z);
    const farm = farmMask(x, z);
    const dC = cityDist(x, z);
    const beachZone = (z > CITY_RECT.maxZ && x < 480 && x > -1100 && h < 4.5) || (h < 2.2 && dC > 300 && riverDist(x, z) > 60);
    const beach = beachZone ? smoothstep(4.5, 1.5, h) : 0;
    // base tint: greener in the ring hills near the coast, drier inland
    const dry = clamp(0.5 + 0.5 * Math.sin(x * 0.0021) * Math.cos(z * 0.0017), 0, 1);
    const col = [0.36 + dry * 0.12, 0.38 + dry * 0.05, 0.17 + dry * 0.02];
    return { col, t: [farm, w.desert, w.mountain * (h < 520 ? 1 : 0.6), beach] };
  }

  _mesh(c, lod) {
    const step = LODS[lod];
    const n = CH / step + 1;
    const hf = this.hf;
    const verts = n * n + 4 * n;
    const pos = new Float32Array(verts * 3), nor = new Float32Array(verts * 3), col = new Float32Array(verts * 3), at = new Float32Array(verts * 4);
    const cityDrop = (x, z) => (x > CITY_RECT.minX + 0.5 && x < CITY_RECT.maxX - 0.5 && z > CITY_RECT.minZ + 0.5 && z < CITY_RECT.maxZ - 0.5 ? 0.7 : 0);
    let k = 0;
    const put = (x, z, y, i0) => {
      pos[k * 3] = x; pos[k * 3 + 1] = y; pos[k * 3 + 2] = z;
      const e = Math.max(step, 4);
      const dx = hf.sample(x + e, z) - hf.sample(x - e, z), dz = hf.sample(x, z + e) - hf.sample(x, z - e);
      const l = Math.hypot(dx, 2 * e, dz);
      nor[k * 3] = -dx / l; nor[k * 3 + 1] = 2 * e / l; nor[k * 3 + 2] = -dz / l;
      const a = i0 || this._attrs(x, z, y);
      col[k * 3] = a.col[0]; col[k * 3 + 1] = a.col[1]; col[k * 3 + 2] = a.col[2];
      at[k * 4] = a.t[0]; at[k * 4 + 1] = a.t[1]; at[k * 4 + 2] = a.t[2]; at[k * 4 + 3] = a.t[3];
      return { a };
    };
    // biome attributes: computed on the 16 m grid, interpolated for the fine level (much cheaper)
    const A1 = c.attr1;
    const interp = (i, j) => {
      const f = step / LODS[1];
      const fi = i * f, fj = j * f, i0 = Math.min(A1.n - 2, Math.floor(fi)), j0 = Math.min(A1.n - 2, Math.floor(fj));
      const ti = fi - i0, tj = fj - j0;
      const w00 = (1 - ti) * (1 - tj), w10 = ti * (1 - tj), w01 = (1 - ti) * tj, w11 = ti * tj;
      const a = j0 * A1.n + i0, b = a + 1, cc = a + A1.n, d = cc + 1;
      const col = [0, 0, 0], t = [0, 0, 0, 0];
      for (let q = 0; q < 3; q++) col[q] = A1.col[a * 3 + q] * w00 + A1.col[b * 3 + q] * w10 + A1.col[cc * 3 + q] * w01 + A1.col[d * 3 + q] * w11;
      for (let q = 0; q < 4; q++) t[q] = A1.t[a * 4 + q] * w00 + A1.t[b * 4 + q] * w10 + A1.t[cc * 4 + q] * w01 + A1.t[d * 4 + q] * w11;
      return { col, t };
    };
    for (let j = 0; j < n; j++) for (let i = 0; i < n; i++) {
      const x = c.x0 + i * step, z = c.z0 + j * step;
      const y = hf.sample(x, z) - cityDrop(x, z);
      put(x, z, y, lod === 0 && A1 ? interp(i, j) : null); k++;
    }
    if (lod === 1) c.attr1 = { n, col: col.slice(0, n * n * 3), t: at.slice(0, n * n * 4) };
    const idx = [];
    for (let j = 0; j < n - 1; j++) for (let i = 0; i < n - 1; i++) {
      const a = j * n + i, b = a + 1, cc = a + n, d = cc + 1;
      // alternate the diagonal to reduce directional artefacts
      if ((i + j) % 2) idx.push(a, cc, b, b, cc, d); else idx.push(a, cc, d, a, d, b);
    }
    // skirts hanging down along the four edges
    const skirt = lod === 0 ? 3 : lod === 1 ? 10 : 30;
    const edgeIdx = [[...Array(n).keys()].map((i) => i), [...Array(n).keys()].map((i) => (n - 1) * n + i), [...Array(n).keys()].map((j) => j * n), [...Array(n).keys()].map((j) => j * n + n - 1)];
    const flips = [false, true, true, false]; // keeps each skirt facing outward
    edgeIdx.forEach((list, e) => {
      const base = k;
      for (const vi of list) {
        const x = pos[vi * 3], y = pos[vi * 3 + 1], z = pos[vi * 3 + 2];
        pos[k * 3] = x; pos[k * 3 + 1] = y - skirt; pos[k * 3 + 2] = z;
        nor[k * 3] = nor[vi * 3]; nor[k * 3 + 1] = nor[vi * 3 + 1]; nor[k * 3 + 2] = nor[vi * 3 + 2];
        col[k * 3] = col[vi * 3]; col[k * 3 + 1] = col[vi * 3 + 1]; col[k * 3 + 2] = col[vi * 3 + 2];
        for (let q = 0; q < 4; q++) at[k * 4 + q] = at[vi * 4 + q];
        k++;
      }
      for (let q = 0; q < list.length - 1; q++) {
        const a = list[q], b = list[q + 1], a2 = base + q, b2 = base + q + 1;
        if (flips[e]) idx.push(a, a2, b, b, a2, b2); else idx.push(a, b, a2, b, b2, a2);
      }
    });
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    g.setAttribute('normal', new THREE.BufferAttribute(nor, 3));
    g.setAttribute('color', new THREE.BufferAttribute(col, 3));
    g.setAttribute('aT', new THREE.BufferAttribute(at, 4));
    g.setIndex(k > 65535 ? new THREE.Uint32BufferAttribute(idx, 1) : new THREE.Uint16BufferAttribute(idx, 1));
    g.computeBoundingSphere();
    const m = new THREE.Mesh(g, this.mat);
    m.receiveShadow = true;
    m.castShadow = lod === 0;
    m.name = 'terrain' + lod;
    return m;
  }

  // pick levels of detail around the camera; build the finest one lazily (a couple per frame)
  update(dt, cam, far = false) {
    this._t -= dt;
    if (this._t > 0) return;
    this._t = 0.2;
    const alt = Math.max(0, cam.y - this.hf.sample(cam.x, cam.z));
    const r0 = 330 + alt * 0.6, r1 = 1500 + alt * 1.5;
    let built = 0;
    for (const c of this.chunks) {
      const dx = Math.max(Math.abs(cam.x - c.cx) - CH / 2, 0), dz = Math.max(Math.abs(cam.z - c.cz) - CH / 2, 0);
      const d = Math.hypot(dx, dz);
      let want = d < r0 ? 0 : d < r1 ? 1 : 2;
      if (want === 0 && !c.meshes[0]) {
        if (built < 2) { c.meshes[0] = this._mesh(c, 0); this.root.add(c.meshes[0]); built++; }
        else want = 1;
      }
      if (c.meshes[0] && d > r0 + 400) { this.root.remove(c.meshes[0]); c.meshes[0].geometry.dispose(); c.meshes[0] = null; if (want === 0) want = 1; }
      if (want !== c.lod) {
        for (let l = 0; l < 3; l++) if (c.meshes[l]) c.meshes[l].visible = l === want;
        c.lod = want;
      }
    }
  }
}
