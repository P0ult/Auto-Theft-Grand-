// Renders the non-grid road network: road ribbons with painted markings (freeway lanes, rural centre
// lines, dirt tracks), junction pads, roundabouts with planted islands, and for elevated sections
// concrete decks, jersey barriers and pillars. Also builds the collision surfaces for decks.
import * as THREE from 'three';
import { std } from '../render/materials.js';
import { NOISE_GLSL } from './shaders.js';
import { DECK_H } from './roadnet.js';
import { clamp, lerp } from '../core/utils.js';

const CHUNK = 400;
const MARK = { freeway: 1, ramp: 2, highway: 3, road: 4, dirt: 5, junction: 6, street: 0 };

export const ROADNET_EXT = {
  key: 'roadnet',
  vertexPars: 'attribute vec4 aR; varying vec4 vR; varying vec2 vRU;',
  vertexMain: 'vR = aR; vRU = uv;',
  fragPars: NOISE_GLSL + `
varying vec4 vR; varying vec2 vRU;
float atgRough = 0.9;
float stripeR(float x, float c, float w, float aa) { return 1.0 - smoothstep(w - aa, w + aa, abs(x - c)); }
`,
  fragColor: `
{
  vec2 wp = vAtgWorld.xz;
  float type = floor(vR.x + 0.5);
  float u = vRU.x, v = vRU.y;
  float len = vR.y;
  float n = fbm3(wp * 0.25);
  float fine = vn2(wp * 6.0);
  float aa = fwidth(u) * 0.9 + 0.01;
  vec3 col;
  float paintW = 0.0, paintY = 0.0;
  if (type > 4.5 && type < 5.5) {
    // dirt track with tyre ruts and gravel
    col = vec3(0.42, 0.33, 0.22) * (0.75 + 0.35 * n) * (0.85 + 0.25 * fine);
    float rut = stripeR(abs(u), 1.0, 0.35, 0.3) * 0.25;
    col *= 1.0 - rut;
    col = mix(col, vec3(0.33, 0.38, 0.2) * (0.8 + 0.4 * n), smoothstep(0.55, 0.0, abs(u)) * 0.5 * smoothstep(0.4, 0.7, vn2(wp * 0.7)));
    float edgeFade = smoothstep(vR.w - 0.9, vR.w, abs(u));
    col = mix(col, vec3(0.4, 0.36, 0.26), edgeFade * 0.6);
    atgRough = 1.0;
  } else {
    col = vec3(0.09, 0.09, 0.095) * (0.72 + 0.45 * n) * (0.9 + 0.2 * fine);
    if (type > 5.5) {
      col *= 0.97;
    } else if (type < 1.5) {
      // freeway carriageway: lanes at u = -3.7..0..+3.7 (left edge yellow, divider dashed, right edge white)
      paintY += stripeR(u, -3.72, 0.08, aa);
      paintW += stripeR(u, 3.72, 0.08, aa);
      paintW += stripeR(u, 0.0, 0.07, aa) * step(0.55, fract(v / 12.0));
      float track = stripeR(abs(u), 1.85 - 0.8, 0.35, 0.4) + stripeR(abs(u), 1.85 + 0.8, 0.35, 0.4);
      col *= 1.0 - track * 0.1;
      // concrete-coloured shoulders
      col = mix(col, vec3(0.2, 0.2, 0.2) * (0.8 + 0.3 * n), step(3.9, abs(u)) * 0.5);
    } else if (type < 2.5) {
      paintY += stripeR(u, -2.05, 0.08, aa);
      paintW += stripeR(u, 2.05, 0.08, aa);
    } else if (type < 3.5) {
      // rural highway: double yellow centre, white edge lines, gravel shoulders beyond
      paintY += stripeR(u, 0.16, 0.07, aa) + stripeR(u, -0.16, 0.07, aa);
      paintW += stripeR(abs(u), 3.78, 0.08, aa);
      col = mix(col, vec3(0.14, 0.13, 0.12) * (0.8 + 0.4 * n), step(3.95, abs(u)) * 0.6);
    } else {
      // town / country road: dashed yellow centre
      paintY += stripeR(u, 0.0, 0.08, aa) * step(0.5, fract(v / 9.0));
      paintW += stripeR(abs(u), 3.45, 0.06, aa) * 0.7;
    }
    // stop line where a minor road meets a junction (flag bits in vR.z: 1 = at start, 2 = at end)
    float flags = vR.z;
    float atStart = mod(flags, 2.0), atEnd = step(1.5, mod(flags, 4.0));
    float fromEnd = len - v;
    paintW += atEnd * step(0.3, u) * step(u, 3.6) * step(0.4, fromEnd) * step(fromEnd, 0.95);
    paintW += atStart * step(-3.6, u) * step(u, -0.3) * step(0.4, v) * step(v, 0.95);
    float wear = smoothstep(0.25, 0.7, vn2(wp * 1.3 + 5.0)) * 0.55 + 0.45;
    paintW *= wear; paintY *= wear;
    float patchN = smoothstep(0.62, 0.66, vn2(wp * 0.08 + 13.0));
    col = mix(col, col * 0.78, patchN);
    float crack = smoothstep(0.02, 0.0, abs(vn2(wp * 0.7) - 0.5)) * 0.6;
    col *= 1.0 - crack * 0.5;
    col = mix(col, vec3(0.75, 0.62, 0.18), clamp(paintY, 0.0, 1.0));
    col = mix(col, vec3(0.8, 0.8, 0.78), clamp(paintW, 0.0, 1.0));
    float paint = clamp(paintW + paintY, 0.0, 1.0);
    float puddle = smoothstep(0.45, 0.6, fbm3(wp * 0.12 + 2.0)) * uWet;
    col *= 1.0 - uWet * 0.35 - puddle * 0.25;
    atgRough = mix(mix(0.92, 0.6, paint), 0.12, clamp(uWet * 0.55 + puddle, 0.0, 1.0));
  }
  diffuseColor.rgb = col;
}
`,
  fragRoughness: 'roughnessFactor = atgRough;',
};

// concrete (decks, barriers, pillars, curbs): vertex colours + grime
const CONCRETE_EXT = {
  key: 'concrete',
  fragPars: NOISE_GLSL,
  fragColor: `
  {
    vec2 wp = vAtgWorld.xz + vAtgWorld.y * 0.7;
    float n = fbm3(wp * 0.6);
    diffuseColor.rgb *= 0.78 + 0.32 * n;
    diffuseColor.rgb *= 1.0 - smoothstep(0.55, 0.8, vn2(vec2(vAtgWorld.x + vAtgWorld.z, vAtgWorld.y * 3.0) * 0.3)) * 0.25;
    diffuseColor.rgb *= 1.0 - uWet * 0.25;
  }`,
};

class Acc {
  constructor(withRoad) {
    this.pos = []; this.nor = []; this.uv = []; this.idx = []; this.aR = withRoad ? [] : null; this.col = withRoad ? null : [];
    this.n = 0;
  }
  v(x, y, z, nx, ny, nz, u, v, a) {
    this.pos.push(x, y, z); this.nor.push(nx, ny, nz); this.uv.push(u, v);
    if (this.aR) this.aR.push(a[0], a[1], a[2], a[3]); else this.col.push(a[0], a[1], a[2]);
    return this.n++;
  }
  // winding picked from the intended normal stored on vertex a (so callers can't get it wrong)
  quad(a, b, c, d) {
    const P = this.pos, N = this.nor;
    const ax = P[a * 3], ay = P[a * 3 + 1], az = P[a * 3 + 2];
    const ux = P[b * 3] - ax, uy = P[b * 3 + 1] - ay, uz = P[b * 3 + 2] - az;
    let vx = P[c * 3] - ax, vy = P[c * 3 + 1] - ay, vz = P[c * 3 + 2] - az;
    if (vx * vx + vy * vy + vz * vz < 1e-10) { vx = P[d * 3] - ax; vy = P[d * 3 + 1] - ay; vz = P[d * 3 + 2] - az; }
    const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    if (nx * N[a * 3] + ny * N[a * 3 + 1] + nz * N[a * 3 + 2] >= 0) this.idx.push(a, b, c, a, c, d);
    else this.idx.push(a, c, b, a, d, c);
  }
  build() {
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(this.pos, 3));
    g.setAttribute('normal', new THREE.Float32BufferAttribute(this.nor, 3));
    g.setAttribute('uv', new THREE.Float32BufferAttribute(this.uv, 2));
    if (this.aR) g.setAttribute('aR', new THREE.Float32BufferAttribute(this.aR, 4));
    else g.setAttribute('color', new THREE.Float32BufferAttribute(this.col, 3));
    g.setIndex(this.n > 65535 ? new THREE.Uint32BufferAttribute(this.idx, 1) : new THREE.Uint16BufferAttribute(this.idx, 1));
    g.computeBoundingSphere();
    return g;
  }
}

export class RoadMeshes {
  constructor(scene, map, collision) {
    this.map = map;
    this.net = map.roads;
    this.collision = collision;
    this.root = new THREE.Group();
    this.root.name = 'roadnet';
    scene.add(this.root);
    this.chunks = new Map();
    this.roadMat = std({ color: 0xffffff, roughness: 0.9, polygonOffset: true, polygonOffsetFactor: -1, polygonOffsetUnits: -2 }, ROADNET_EXT);
    this.concMat = std({ color: 0xffffff, vertexColors: true, roughness: 0.92 }, CONCRETE_EXT);
    this.islandMat = std({ color: 0xffffff, vertexColors: true, roughness: 0.95 }, { key: 'island' });
    this.decks = [];
    this.pillarSpots = [];
    this.build();
  }

  chunk(x, z) {
    const k = Math.floor(x / CHUNK) + ',' + Math.floor(z / CHUNK);
    let c = this.chunks.get(k);
    if (!c) { c = { road: new Acc(true), conc: new Acc(false), cx: (Math.floor(x / CHUNK) + 0.5) * CHUNK, cz: (Math.floor(z / CHUNK) + 0.5) * CHUNK }; this.chunks.set(k, c); }
    return c;
  }

  ground(x, z) { return this.map.groundHeight(x, z); }

  // distance along an edge from each end that the ribbon should leave to the junction pad
  trims(e) {
    const t = (n) => {
      if (n.grid || n.city) return 10.2;
      if (n.kind === 'x') return n.r;
      if (n.kind === 'rb') return n.rbR + 5.4;
      return 0;
    };
    return [t(this.net.nodes[e.a]), t(this.net.nodes[e.b])];
  }

  build() {
    const net = this.net;
    for (const e of net.edges) if (!e.removed && e.render && !e.grid) this._edge(e);
    for (const n of net.nodes) if (!n.dead && n.e.length && !n.grid && !n.city) {
      if (n.kind === 'x' && n.e.length > 1) this._junction(n);
      else if (n.kind === 'rb') this._roundabout(n);
    }
    // mini roundabouts in the city grid: just the planted island (the street plane paints the ring)
    for (const n of net.nodes) if (n.grid && n.kind === 'rb') this._island(n.x, n.z, 0.03, 5.6, true);
    for (const c of this.chunks.values()) {
      if (c.road.n) { const m = new THREE.Mesh(c.road.build(), this.roadMat); m.receiveShadow = true; m.name = 'roads'; this.root.add(m); }
      if (c.conc.n) { const m = new THREE.Mesh(c.conc.build(), this.concMat); m.receiveShadow = true; m.castShadow = true; m.name = 'decks'; this.root.add(m); }
    }
    // collision: deck surfaces for vehicles / characters
    for (const d of this.decks) this.collision.addDeck(d);
  }

  _edge(e) {
    const net = this.net, p = e.p, n = e.n;
    const [t0, t1] = this.trims(e);
    const s0 = t0, s1 = e.len - t1;
    if (s1 - s0 < 0.5) return;
    const type = MARK[e.type] ?? 4;
    // stop-line flags: minor road meeting a node of a higher class
    const na = net.nodes[e.a], nb = net.nodes[e.b];
    let flags = 0;
    if (na.kind === 'x' && !na.city && na.maxCls > e.T.cls) flags |= 1;
    if (nb.kind === 'x' && !nb.city && nb.maxCls > e.T.cls) flags |= 2;
    // stations to emit: trimmed ends + interior points
    const st = [s0];
    for (let i = 1; i < n - 1; i++) if (e.cum[i] > s0 + 0.3 && e.cum[i] < s1 - 0.3) st.push(e.cum[i]);
    st.push(s1);
    const tmp = [0, 0, 0, 0, 0];
    const rows = st.map((s) => {
      net.at(e, s, tmp);
      const i = idxAt(e.cum, s);
      const deck = e.deck[i] || e.deck[Math.min(n - 1, i + 1)];
      return { s, x: tmp[0], y: tmp[1], z: tmp[2], tx: tmp[3], tz: tmp[4], deck, i };
    });
    // smooth tangents at interior rows
    for (let k = 1; k < rows.length - 1; k++) {
      const a = rows[k - 1], b = rows[k + 1];
      const dx = b.x - a.x, dz = b.z - a.z, l = Math.hypot(dx, dz) || 1;
      rows[k].tx = dx / l; rows[k].tz = dz / l;
    }
    const wL = e.wL, wR = e.wR;
    const lift = 0.035;
    // ribbon in pieces per chunk
    let prev = null;
    for (const r of rows) {
      const rx = -r.tz, rz = r.tx; // right
      const lx = r.x - rx * wL, lz = r.z - rz * wL, Rx = r.x + rx * wR, Rz = r.z + rz * wR;
      const y = r.y + lift;
      // slight cross-fall on ground roads: follow the terrain a little at the edges on dirt
      let yl = y, yr = y;
      if (e.type === 'dirt' && !r.deck) { yl = Math.max(y - 0.25, Math.min(y + 0.25, this.ground(lx, lz) + 0.05)); yr = Math.max(y - 0.25, Math.min(y + 0.25, this.ground(Rx, Rz) + 0.05)); }
      const cur = { r, lx, lz, Rx, Rz, yl, yr };
      if (prev) {
        const c = this.chunk((prev.r.x + r.x) / 2, (prev.r.z + r.z) / 2);
        const A = c.road;
        const a = [type, e.len, flags, Math.max(wL, wR)];
        const i0 = A.v(prev.lx, prev.yl, prev.lz, 0, 1, 0, -wL, prev.r.s, a), i1 = A.v(prev.Rx, prev.yr, prev.Rz, 0, 1, 0, wR, prev.r.s, a);
        const i2 = A.v(cur.Rx, cur.yr, cur.Rz, 0, 1, 0, wR, r.s, a), i3 = A.v(cur.lx, cur.yl, cur.lz, 0, 1, 0, -wL, r.s, a);
        A.quad(i0, i1, i2, i3);
        // deck / embankment sides + barriers
        const deck = prev.r.deck && r.deck;
        if (deck) this._deckSegment(c.conc, e, prev, cur);
        const bL = e.barrierL && (e.type === 'freeway' || deck) , bR = (e.barrierR && deck) || (e.type === 'freeway' && e.barrierR && deck);
        const noB = (s) => (e.noBarrierA && s < e.noBarrierA) || (e.noBarrierB && s > e.len - e.noBarrierB);
        if (bL && !(e.type === 'ramp' && noB(r.s))) this._barrier(c.conc, prev.lx, prev.yl, prev.lz, cur.lx, cur.yl, cur.lz, prev.r, r, -1);
        if ((bR || (deck && e.type !== 'dirt')) && !noB(r.s) && !noB(prev.r.s)) this._barrier(c.conc, prev.Rx, prev.yr, prev.Rz, cur.Rx, cur.yr, cur.Rz, prev.r, r, 1);
      }
      prev = cur;
    }
    // pillars under deck runs
    let acc = 14;
    for (let k = 1; k < rows.length; k++) {
      const a = rows[k - 1], b = rows[k];
      if (!(a.deck && b.deck)) { acc = 14; continue; }
      acc += b.s - a.s;
      if (acc < 26) continue;
      const x = b.x, z = b.z, y = b.y;
      const g = this.ground(x, z);
      if (y - DECK_H - g < 2.2) { acc = 20; continue; }
      if (this.map.isOnCityStreet(x, z, 3)) continue;
      if (this.map.isOnRoadNet && this.map.isOnRoadNet(x, z, 1.5, e)) continue;
      acc = 0;
      this._pillar(this.chunk(x, z).conc, e, b, g);
    }
  }

  _deckSegment(A, e, p, q) {
    const col = [0.46, 0.45, 0.43];
    const yb0l = p.yl - DECK_H, yb0r = p.yr - DECK_H, yb1l = q.yl - DECK_H, yb1r = q.yr - DECK_H;
    // left side (facing left): p.l -> q.l
    const ln = [p.r.tz, 0, -p.r.tx];
    let a = A.v(p.lx, p.yl, p.lz, ln[0], 0, ln[2], 0, p.yl, col), b = A.v(q.lx, q.yl, q.lz, ln[0], 0, ln[2], 1, q.yl, col);
    let c = A.v(q.lx, yb1l, q.lz, ln[0], 0, ln[2], 1, yb1l, col), d = A.v(p.lx, yb0l, p.lz, ln[0], 0, ln[2], 0, yb0l, col);
    A.quad(a, d, c, b);
    const rn = [-p.r.tz, 0, p.r.tx];
    a = A.v(p.Rx, p.yr, p.Rz, rn[0], 0, rn[2], 0, p.yr, col); b = A.v(q.Rx, q.yr, q.Rz, rn[0], 0, rn[2], 1, q.yr, col);
    c = A.v(q.Rx, yb1r, q.Rz, rn[0], 0, rn[2], 1, yb1r, col); d = A.v(p.Rx, yb0r, p.Rz, rn[0], 0, rn[2], 0, yb0r, col);
    A.quad(a, b, c, d);
    // underside
    const uc = [0.4, 0.4, 0.38];
    a = A.v(p.lx, yb0l, p.lz, 0, -1, 0, 0, 0, uc); b = A.v(p.Rx, yb0r, p.Rz, 0, -1, 0, 0, 0, uc);
    c = A.v(q.Rx, yb1r, q.Rz, 0, -1, 0, 0, 0, uc); d = A.v(q.lx, yb1l, q.lz, 0, -1, 0, 0, 0, uc);
    A.quad(a, b, c, d);
    // collision surface
    this.decks.push({ ax: p.r.x, az: p.r.z, ay: p.r.y, bx: q.r.x, bz: q.r.z, by: q.r.y, hl: e.wL + 0.1, hr: e.wR + 0.1, edge: e });
    // low solid ramps in the city (retaining walls reach the street): side walls block cars
    const ga = this.ground(p.r.x, p.r.z), gb = this.ground(q.r.x, q.r.z);
    if (p.r.y - DECK_H - ga < 2.4 || q.r.y - DECK_H - gb < 2.4) {
      // fill the gap under a low deck down to the ground with the side walls
      const fl = [0.43, 0.42, 0.4];
      for (const side of [-1, 1]) {
        const px = side < 0 ? p.lx : p.Rx, pz = side < 0 ? p.lz : p.Rz, qx = side < 0 ? q.lx : q.Rx, qz = side < 0 ? q.lz : q.Rz;
        const n = side < 0 ? ln : rn;
        const y0 = side < 0 ? yb0l : yb0r, y1 = side < 0 ? yb1l : yb1r;
        const g0 = this.ground(px, pz) - 0.2, g1 = this.ground(qx, qz) - 0.2;
        const i0 = A.v(px, y0, pz, n[0], 0, n[2], 0, 0, fl), i1 = A.v(qx, y1, qz, n[0], 0, n[2], 0, 0, fl), i2 = A.v(qx, g1, qz, n[0], 0, n[2], 0, 0, fl), i3 = A.v(px, g0, pz, n[0], 0, n[2], 0, 0, fl);
        A.quad(i0, i1, i2, i3);
        const cx = (px + qx) / 2, cz = (pz + qz) / 2;
        const len = Math.hypot(qx - px, qz - pz);
        this.collision.addOBox({ cx: cx - n[0] * 0.3, cz: cz - n[2] * 0.3, hx: 0.3, hz: len / 2 + 0.05, yaw: Math.atan2(qx - px, qz - pz), minY: Math.min(g0, g1), maxY: Math.min(p.r.y, q.r.y) - 0.35, type: 'wall' });
      }
    }
  }

  _barrier(A, x0, y0, z0, x1, y1, z1, r0, r1, side) {
    // jersey barrier: trapezoid profile 0.85 m tall, 0.6 m base, 0.2 m top, inset from the edge
    const col = [0.52, 0.51, 0.49];
    const dx = x1 - x0, dz = z1 - z0, l = Math.hypot(dx, dz) || 1;
    const rx = -dz / l * side, rz = dx / l * side; // outward
    const inset = 0.35;
    const pts = [[-0.3, 0], [-0.1, 0.85], [0.1, 0.85], [0.3, 0]];
    const P = (x, y, z, o, h) => [x - rx * (inset - o), y + h, z - rz * (inset - o)];
    const NRM = [[-0.97, 0.23], [0, 1], [0.97, 0.23]]; // inner slope, top, outer slope (outward, up)
    for (let k = 0; k < 3; k++) {
      const [oa, ha] = pts[k], [ob, hb] = pts[k + 1];
      const a0 = P(x0, y0, z0, oa, ha), b0 = P(x0, y0, z0, ob, hb), a1 = P(x1, y1, z1, oa, ha), b1 = P(x1, y1, z1, ob, hb);
      const Nx = rx * NRM[k][0], Ny = NRM[k][1], Nz = rz * NRM[k][0];
      const i0 = A.v(a0[0], a0[1], a0[2], Nx, Ny, Nz, 0, 0, col), i1 = A.v(b0[0], b0[1], b0[2], Nx, Ny, Nz, 0, 0, col);
      const i2 = A.v(b1[0], b1[1], b1[2], Nx, Ny, Nz, 0, 0, col), i3 = A.v(a1[0], a1[1], a1[2], Nx, Ny, Nz, 0, 0, col);
      A.quad(i0, i1, i2, i3);
    }
    // collision: thin oriented box on top of the road surface
    const cx = (x0 + x1) / 2 - rx * inset, cz = (z0 + z1) / 2 - rz * inset;
    this.collision.addOBox({ cx, cz, hx: 0.3, hz: l / 2 + 0.05, yaw: Math.atan2(dx, dz), minY: Math.min(y0, y1) - 0.2, maxY: Math.max(y0, y1) + 0.85, type: 'barrier', low: true });
  }

  _pillar(A, e, r, g) {
    const col = [0.44, 0.43, 0.41];
    const top = r.y - DECK_H;
    const w = Math.max(e.wL, e.wR);
    const rx = -r.tz, rz = r.tx;
    // one or two columns depending on deck width, plus a cap beam
    const cols = w > 5 ? [-w * 0.45, w * 0.45] : [0];
    for (const o of cols) {
      const cx = r.x + rx * o, cz = r.z + rz * o;
      box(A, cx, cz, 0.75, 0.75, Math.atan2(r.tx, r.tz), g - 1, top - 0.6, col);
      this.collision.addOBox({ cx, cz, hx: 0.8, hz: 0.8, yaw: Math.atan2(r.tx, r.tz), minY: g - 1, maxY: top - 0.6, type: 'pillar' });
    }
    box(A, r.x, r.z, 0.7, w * 0.9, Math.atan2(r.tx, r.tz) + Math.PI / 2 * 0, top - 0.7, top, col, true);
  }

  _junction(n) {
    const net = this.net;
    const pts = [];
    const tmp = [0, 0, 0, 0, 0];
    for (const eid of n.e) {
      const e = net.edges[eid];
      if (e.removed) continue;
      const atA = e.a === n.id;
      const s = atA ? Math.min(n.r, e.len) : Math.max(0, e.len - n.r);
      net.at(e, s, tmp);
      const rx = -tmp[4], rz = tmp[3];
      pts.push([tmp[0] - rx * e.wL, tmp[2] - rz * e.wL], [tmp[0] + rx * e.wR, tmp[2] + rz * e.wR]);
    }
    if (pts.length < 3) return;
    const hull = convexHull(pts);
    const y = n.y + 0.035;
    const A = this.chunk(n.x, n.z).road;
    const a = [MARK.junction, 0, 0, 0];
    const c = A.v(n.x, y, n.z, 0, 1, 0, 0, 0, a);
    const ids = hull.map((p) => A.v(p[0], y, p[1], 0, 1, 0, p[0] - n.x, p[1] - n.z, a));
    for (let k = 0; k < ids.length; k++) A.idx.push(c, ids[(k + 1) % ids.length], ids[k]);
  }

  _roundabout(n) {
    const A = this.chunk(n.x, n.z).road;
    const R = n.rbR, rin = R - 5.2, rout = R + 5.4;
    const y = n.y + 0.035;
    const seg = 48;
    const a = [MARK.junction, 0, 0, 0];
    const ring = [];
    for (let k = 0; k <= seg; k++) {
      const t = k / seg * Math.PI * 2;
      const cx = Math.cos(t), cz = Math.sin(t);
      ring.push([A.v(n.x + cx * rin, y, n.z + cz * rin, 0, 1, 0, 0, 0, a), A.v(n.x + cx * rout, y, n.z + cz * rout, 0, 1, 0, 0, 0, a)]);
    }
    for (let k = 0; k < seg; k++) A.quad(ring[k][0], ring[k + 1][0], ring[k + 1][1], ring[k][1]);
    this._island(n.x, n.z, y, rin, false);
  }

  // planted island with a curb (+ collision)
  _island(nx, nz, y, rin, city) {
    const n = { x: nx, z: nz, y };
    const seg = 48;
    const C = this.chunk(n.x, n.z).conc;
    const curbCol = [0.72, 0.71, 0.68], grass = [0.2, 0.34, 0.12];
    const top = y + 0.28;
    const cc = C.v(n.x, top, n.z, 0, 1, 0, 0, 0, grass);
    const rim = [];
    for (let k = 0; k <= seg; k++) {
      const t = k / seg * Math.PI * 2, cx = Math.cos(t), cz = Math.sin(t);
      rim.push([C.v(n.x + cx * (rin - 0.3), top, n.z + cz * (rin - 0.3), 0, 1, 0, 0, 0, grass), C.v(n.x + cx * rin, top, n.z + cz * rin, cx, 0.3, cz, 0, 0, curbCol), C.v(n.x + cx * rin, y - 0.1, n.z + cz * rin, cx, 0, cz, 0, 0, curbCol)]);
    }
    for (let k = 0; k < seg; k++) {
      C.idx.push(cc, rim[k + 1][0], rim[k][0]);
      C.quad(rim[k][1], rim[k + 1][1], rim[k + 1][2], rim[k][2]);
    }
    this.collision.addCircle({ x: n.x, z: n.z, r: rin, h: n.y + 0.4, type: 'island' });
    this.islands = this.islands || [];
    this.islands.push({ x: n.x, z: n.z, y: top, r: rin, city });
  }
}

function box(A, cx, cz, hx, hz, yaw, y0, y1, col, top = false) {
  const s = Math.sin(yaw), c = Math.cos(yaw);
  const P = (lx, lz) => [cx + lx * c + lz * s, cz - lx * s + lz * c];
  const corners = [P(-hx, -hz), P(hx, -hz), P(hx, hz), P(-hx, hz)];
  for (let k = 0; k < 4; k++) {
    const p = corners[k], q = corners[(k + 1) % 4];
    let nx = (p[0] + q[0]) / 2 - cx, nz = (p[1] + q[1]) / 2 - cz;
    const l = Math.hypot(nx, nz) || 1; nx /= l; nz /= l;
    const a = A.v(p[0], y0, p[1], nx, 0, nz, 0, 0, col), b = A.v(q[0], y0, q[1], nx, 0, nz, 0, 0, col);
    const cc = A.v(q[0], y1, q[1], nx, 0, nz, 0, 0, col), d = A.v(p[0], y1, p[1], nx, 0, nz, 0, 0, col);
    A.quad(a, b, cc, d);
  }
  if (top) {
    const ids = corners.map((p) => A.v(p[0], y1, p[1], 0, 1, 0, 0, 0, col));
    A.quad(ids[0], ids[3], ids[2], ids[1]);
    const idb = corners.map((p) => A.v(p[0], y0, p[1], 0, -1, 0, 0, 0, col));
    A.quad(idb[0], idb[1], idb[2], idb[3]);
  }
}

function idxAt(cum, s) {
  let lo = 0, hi = cum.length - 1;
  while (hi - lo > 1) { const m = (lo + hi) >> 1; if (cum[m] <= s) lo = m; else hi = m; }
  return lo;
}

function convexHull(points) {
  const pts = points.slice().sort((a, b) => a[0] - b[0] || a[1] - b[1]);
  const cross = (o, a, b) => (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
  const lower = [], upper = [];
  for (const p of pts) { while (lower.length >= 2 && cross(lower[lower.length - 2], lower[lower.length - 1], p) <= 0) lower.pop(); lower.push(p); }
  for (let i = pts.length - 1; i >= 0; i--) { const p = pts[i]; while (upper.length >= 2 && cross(upper[upper.length - 2], upper[upper.length - 1], p) <= 0) upper.pop(); upper.push(p); }
  upper.pop(); lower.pop();
  return lower.concat(upper);
}
