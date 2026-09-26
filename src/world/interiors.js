// Walk-in shop interiors: the Gun Barn, Big Bun Burgers and Ray's Liquor are real rooms behind a
// doorway in the shop front — floor, walls, ceiling lights, a counter with a shopkeeper's spot behind it
// and a service spot in front, stock on shelves and racks, and colliders for all of it.
// planInteriors() runs with the map build (colliders must exist before the collision world);
// buildInteriorMesh() is called by the city renderer.
import * as THREE from 'three';
import { GeoBuilder, mat4 } from './geom.js';
import { createWeaponMesh } from '../game/weapondefs.js';

const DOOR_W = 2.4, DOOR_H = 2.7;

// A shop's local frame: u across the front (0 = the doorway), w into the room from the front wall.
class Frame {
  constructor(b) {
    const side = b.shop.front; // 'z0' | 'z1'
    this.y = b.y0;
    this.f = side === 'z1' ? [0, -1] : [0, 1];
    this.r = [-this.f[1], this.f[0]];
    this.ox = (b.x0 + b.x1) / 2;
    this.oz = side === 'z1' ? b.z1 : b.z0;
    this.W = b.x1 - b.x0;
    this.D = b.z1 - b.z0;
  }
  x(u, w) { return this.ox + this.r[0] * u + this.f[0] * w; }
  z(u, w) { return this.oz + this.r[1] * u + this.f[1] * w; }
  P(u, w, h = 0) { return [this.x(u, w), this.y + h, this.z(u, w)]; }
  // world AABB of a local rectangle
  rect(u0, w0, u1, w1) {
    const xs = [this.x(u0, w0), this.x(u1, w1)], zs = [this.z(u0, w0), this.z(u1, w1)];
    return [Math.min(...xs), Math.min(...zs), Math.max(...xs), Math.max(...zs)];
  }
  // yaw that points local +w (into the room); local +u is yaw - PI/2
  get yawIn() { return Math.atan2(this.f[0], this.f[1]); }
  get yawR() { return Math.atan2(this.r[0], this.r[1]); }
}

// ------------------------------------------------------------------ layouts
const LAYOUTS = {
  gunshop: (F) => {
    const D = F.D, H = F.W / 2;
    return {
      name: 'Gun Barn', ceil: 3.6,
      floor: [[0.25, 0.27, 0.25], [0.3, 0.32, 0.3]], wall: [0.62, 0.58, 0.5], dado: [0.36, 0.25, 0.16],
      counter: { u0: -6, w0: D - 3.6, u1: 6, w1: D - 2.8, top: [0.3, 0.2, 0.12], body: [0.2, 0.2, 0.22] },
      clerk: { u: 0, w: D - 1.9 }, service: { u: 0, w: D - 4.5 }, till: { u: 1.5, w: D - 3.2 },
      lights: [[-5, 4], [5, 4], [-5, 8.5], [5, 8.5]].filter(([, w]) => w < D - 1),
      hostile: true,
      extra: 'gunshop', H,
    };
  },
  burger: (F) => {
    const D = F.D, H = F.W / 2;
    return {
      name: 'Big Bun Burgers', ceil: 3.6,
      floor: [[0.78, 0.1, 0.08], [0.93, 0.92, 0.88]], wall: [0.95, 0.88, 0.7], dado: [0.7, 0.12, 0.08],
      counter: { u0: -7, w0: D - 5.2, u1: 7, w1: D - 4.4, top: [0.72, 0.72, 0.74], body: [0.78, 0.14, 0.1] },
      clerk: { u: 0, w: D - 3.5 }, service: { u: 0, w: D - 6.1 }, till: { u: -2.5, w: D - 4.8 },
      lights: [[-6, 3.5], [6, 3.5], [-6, 8], [6, 8], [0, D - 2]],
      extra: 'burger', H,
    };
  },
  liquor: (F) => {
    const D = F.D, H = F.W / 2;
    return {
      name: 'Ray\'s Liquor', ceil: 3.4,
      floor: [[0.82, 0.82, 0.8], [0.66, 0.68, 0.68]], wall: [0.72, 0.84, 0.76], dado: [0.18, 0.36, 0.26],
      counter: { u0: -H + 2.4, w0: 1.3, u1: -H + 3.2, w1: 5.2, top: [0.35, 0.22, 0.14], body: [0.28, 0.18, 0.12] },
      clerk: { u: -H + 1.3, w: 3.2, face: 'r' }, service: { u: -H + 4.5, w: 3.2 }, till: { u: -H + 2.8, w: 2.4 },
      lights: [[-5, 3], [2, 3], [-5, 8], [2, 8], [8, 6]],
      extra: 'liquor', H,
    };
  },
};

// ------------------------------------------------------------------ plan (map build time)
export function planInteriors(map) {
  map.interiors = [];
  for (const b of map.buildings) {
    if (!b.shop) continue;
    const F = new Frame(b);
    const L = LAYOUTS[b.shop.key](F);
    const it = { key: b.shop.key, name: L.name, b, F, L, colliders: [], furniture: [] };
    const y0 = F.y, y1 = Math.max(b.y1, F.y + L.ceil + 0.6);
    const box = (u0, w0, u1, w1, h0, h1, type = 'wall') => {
      const [x0, z0, x1, z1] = F.rect(u0, w0, u1, w1);
      const c = { minX: x0, minZ: z0, maxX: x1, maxZ: z1, minY: y0 + h0, maxY: y0 + h1, type };
      it.colliders.push(c);
      map.colliders.push(c);
      return c;
    };
    const T = 0.25, H = F.W / 2, D = F.D;
    // shell: back, sides, and the front either side of the doorway (plus the lintel over it)
    box(-H, D - T, H, D, -0.3, y1 - y0, 'building');
    box(-H, 0, -H + T, D, -0.3, y1 - y0, 'building');
    box(H - T, 0, H, D, -0.3, y1 - y0, 'building');
    box(-H, 0, -DOOR_W / 2, T, -0.3, y1 - y0, 'building');
    box(DOOR_W / 2, 0, H, T, -0.3, y1 - y0, 'building');
    box(-DOOR_W / 2, 0, DOOR_W / 2, T, DOOR_H, y1 - y0, 'building');
    // roof slab over the room (bullets / the camera stop at it)
    box(-H, 0, H, D, L.ceil, L.ceil + 0.3, 'building');
    // counter
    const c = L.counter;
    box(c.u0, c.w0, c.u1, c.w1, 0, 1.05, 'counter');
    // per-shop furniture
    furniture(it, box);
    // spots in world space
    const sp = (s) => ({ x: F.x(s.u, s.w), z: F.z(s.u, s.w), y: F.y });
    it.clerk = { ...sp(L.clerk), yaw: L.clerk.face === 'r' ? F.yawR : F.yawIn + Math.PI };
    it.service = sp(L.service);
    it.till = { ...sp(L.till), y: F.y + 1.1 };
    it.door = { x: F.x(0, -1.2), z: F.z(0, -1.2), y: F.y };
    it.center = { x: F.x(0, D / 2), z: F.z(0, D / 2) };
    it.light = { x: F.x(0, D / 2), y: F.y + L.ceil - 0.4, z: F.z(0, D / 2) };
    it.inside = (x, z, pad = 0) => {
      const [x0, z0, x1, z1] = F.rect(-H, 0, H, D);
      return x > x0 - pad && x < x1 + pad && z > z0 - pad && z < z1 + pad;
    };
    map.interiors.push(it);
  }
}

function furniture(it, box) {
  const { F, L } = it;
  const D = F.D, H = F.W / 2;
  const add = (kind, o) => it.furniture.push({ kind, ...o });
  if (L.extra === 'gunshop') {
    // ammo shelving down both sides, armour case, pegboard racks on the back wall
    box(-H + 0.25, 2, -H + 0.85, D - 4.2, 0, 2.2, 'shelf'); add('shelf', { u0: -H + 0.25, w0: 2, u1: -H + 0.85, w1: D - 4.2, h: 2.2, face: 1 });
    box(H - 0.85, 2, H - 0.25, D - 4.2, 0, 2.2, 'shelf'); add('shelf', { u0: H - 0.85, w0: 2, u1: H - 0.25, w1: D - 4.2, h: 2.2, face: -1 });
    box(3.4, 3.2, 7.4, 4.2, 0, 1.0, 'case'); add('case', { u0: 3.4, w0: 3.2, u1: 7.4, w1: 4.2 }); // (clear of the way to the counter)
  } else if (L.extra === 'burger') {
    // kitchen line along the back wall
    box(-9, D - 1.1, 9, D - 0.25, 0, 1.0, 'kitchen'); add('kitchen', {});
    // tables (not in the aisle to the counter)
    for (const u of [-7.5, -4, 4, 7.5]) for (const w of [2.8, 6]) {
      if (w > D - 6.4) continue;
      box(u - 0.45, w - 0.45, u + 0.45, w + 0.45, 0, 0.8, 'table');
      add('table', { u, w });
    }
  } else if (L.extra === 'liquor') {
    for (const u of [-2.5, 1.5, 5.5]) { box(u - 0.5, 3.4, u + 0.5, Math.min(8.6, D - 2.2), 0, 1.75, 'shelf'); add('aisle', { u, w0: 3.4, w1: Math.min(8.6, D - 2.2) }); }
    box(-H + 2, D - 0.95, H - 0.3, D - 0.25, 0, 2.2, 'fridge'); add('fridges', { u0: -H + 2, u1: H - 0.3 });
    box(-H + 0.25, 1.4, -H + 0.7, 5.0, 0.9, 2.4, 'shelf'); add('smokes', {});
  }
}

// ------------------------------------------------------------------ meshes
let _mats = null;
function mats() {
  if (_mats) return _mats;
  _mats = {
    solid: new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.78, metalness: 0.02 }),
    glow: new THREE.MeshBasicMaterial({ vertexColors: true }),
    glass: new THREE.MeshStandardMaterial({ color: 0xbfe6ff, roughness: 0.05, metalness: 0.1, transparent: true, opacity: 0.22, depthWrite: false }),
  };
  return _mats;
}

function rnd(seed) { let s = seed >>> 0; return () => { s = (s * 1664525 + 1013904223) >>> 0; return s / 4294967296; }; }

// canvas texture helper for signs, menus and posters
function canvasTex(w, h, draw) {
  const cv = document.createElement('canvas');
  cv.width = w; cv.height = h;
  draw(cv.getContext('2d'), w, h);
  const t = new THREE.CanvasTexture(cv);
  t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = 4;
  return t;
}

export function buildInteriorMesh(it) {
  const { F, L } = it;
  const M = mats();
  const group = new THREE.Group();
  group.name = 'interior-' + it.key;
  const gb = new GeoBuilder(), em = new GeoBuilder();
  const H = F.W / 2, D = F.D, C = L.ceil;
  const col = (g, c) => g.set('color', c[0], c[1], c[2]);
  // a quad in local coordinates, wound to face `n` (a local [u, h, w] direction)
  const face = (g, a, b, c, d, n) => {
    const P = (q) => F.P(q[0], q[2], q[1]);
    const p = [a, b, c, d].map(P);
    const nw = [F.r[0] * n[0] + F.f[0] * n[2], n[1], F.r[1] * n[0] + F.f[1] * n[2]];
    const e1 = [p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]], e2 = [p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]];
    const cr = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]];
    if (cr[0] * nw[0] + cr[1] * nw[1] + cr[2] * nw[2] < 0) p.reverse();
    g.quad(p[0], p[1], p[2], p[3], nw);
  };
  // local box -> world box
  const lbox = (g, u0, w0, u1, w1, h0, h1, c, opts) => {
    const [x0, z0, x1, z1] = F.rect(u0, w0, u1, w1);
    col(g, c); g.box(x0, F.y + h0, z0, x1, F.y + h1, z1, opts);
  };
  const wallQuad = (u0, u1, w, h0, h1, n) => face(gb, [u0, h0, w], [u1, h0, w], [u1, h1, w], [u0, h1, w], n);
  const sideQuad = (u, w0, w1, h0, h1, n) => face(gb, [u, h0, w0], [u, h0, w1], [u, h1, w1], [u, h1, w0], n);
  const dado = 1.0, e = 0.004;

  // floor tiles
  for (let u = -H; u < H - 1e-3; u += 1) for (let w = 0; w < D - 1e-3; w += 1) {
    col(gb, L.floor[(Math.floor(u + H) + Math.floor(w)) & 1]);
    const u1 = Math.min(H, u + 1), w1 = Math.min(D, w + 1);
    face(gb, [u, 0.06, w], [u1, 0.06, w], [u1, 0.06, w1], [u, 0.06, w1], [0, 1, 0]); // (above any car-park surface under the building)
  }
  // ceiling
  col(gb, [0.8, 0.8, 0.78]);
  face(gb, [-H, C, 0], [H, C, 0], [H, C, D], [-H, C, D], [0, -1, 0]);
  // walls: dado band and upper wall, inward facing; the front has the doorway
  for (const [h0, h1, c] of [[0, dado, L.dado], [dado, C, L.wall]]) {
    col(gb, c);
    wallQuad(-H, H, D - e, h0, h1, [0, 0, -1]);
    sideQuad(-H + e, 0, D, h0, h1, [1, 0, 0]);
    sideQuad(H - e, 0, D, h0, h1, [-1, 0, 0]);
    wallQuad(-H, -DOOR_W / 2, e, h0, h1, [0, 0, 1]);
    wallQuad(DOOR_W / 2, H, e, h0, h1, [0, 0, 1]);
    if (h1 > DOOR_H) wallQuad(-DOOR_W / 2, DOOR_W / 2, e, Math.max(h0, DOOR_H), h1, [0, 0, 1]);
  }
  // door frame and a mat
  lbox(gb, -DOOR_W / 2 - 0.12, -0.12, -DOOR_W / 2, 0.14, 0, DOOR_H, [0.15, 0.15, 0.16]);
  lbox(gb, DOOR_W / 2, -0.12, DOOR_W / 2 + 0.12, 0.14, 0, DOOR_H, [0.15, 0.15, 0.16]);
  lbox(gb, -DOOR_W / 2 - 0.12, -0.12, DOOR_W / 2 + 0.12, 0.14, DOOR_H, DOOR_H + 0.14, [0.15, 0.15, 0.16]);
  lbox(gb, -1.1, 0.4, 1.1, 1.7, 0.06, 0.075, [0.12, 0.12, 0.13]);
  // ceiling light panels
  for (const [u, w] of L.lights) {
    lbox(gb, u - 0.9, w - 0.35, u + 0.9, w + 0.35, C - 0.06, C, [0.6, 0.6, 0.6], { top: false, bottom: true });
    col(em, [1.0, 0.97, 0.9]);
    face(em, [u - 0.8, C - 0.065, w - 0.28], [u + 0.8, C - 0.065, w - 0.28], [u + 0.8, C - 0.065, w + 0.28], [u - 0.8, C - 0.065, w + 0.28], [0, -1, 0]);
  }
  // counter
  const c = L.counter;
  lbox(gb, c.u0, c.w0, c.u1, c.w1, 0, 0.98, c.body);
  lbox(gb, c.u0 - 0.05, c.w0 - 0.05, c.u1 + 0.05, c.w1 + 0.05, 0.98, 1.05, c.top);
  // till
  const tu = L.till.u, tw = L.till.w;
  lbox(gb, tu - 0.22, tw - 0.18, tu + 0.22, tw + 0.18, 1.05, 1.2, [0.15, 0.15, 0.16]);
  lbox(gb, tu - 0.18, tw - 0.02, tu + 0.18, tw + 0.14, 1.2, 1.42, [0.2, 0.2, 0.22]);
  col(em, [0.3, 1.0, 0.5]);
  face(em, [tu - 0.14, 1.24, tw - 0.03], [tu + 0.14, 1.24, tw - 0.03], [tu + 0.14, 1.38, tw - 0.03], [tu - 0.14, 1.38, tw - 0.03], [0, 0, -1]);

  const R = rnd(it.key.length * 977 + Math.round(F.ox));
  const products = (u0, w0, u1, w1, levels, h0, dh, faceDir, depth = 0.35) => {
    // rows of small coloured boxes on shelves
    for (let k = 0; k < levels; k++) {
      const y = h0 + k * dh;
      lbox(gb, u0, w0, u1, w1, y - 0.03, y, [0.45, 0.42, 0.38]);
      const alongU = Math.abs(u1 - u0) > Math.abs(w1 - w0);
      const len = alongU ? u1 - u0 : w1 - w0;
      for (let s = 0.08; s < len - 0.12;) {
        const wdt = 0.08 + R() * 0.18, hgt = 0.12 + R() * dh * 0.55;
        const cc = [[0.8, 0.15, 0.1], [0.1, 0.4, 0.8], [0.95, 0.75, 0.1], [0.2, 0.6, 0.25], [0.9, 0.9, 0.85], [0.55, 0.2, 0.6], [0.95, 0.45, 0.1]][Math.floor(R() * 7)];
        if (alongU) { const a = u0 + s; lbox(gb, a, w0 + 0.03, a + wdt, w0 + 0.03 + depth * (0.6 + R() * 0.4), y, y + hgt, cc); }
        else { const a = w0 + s; const uu = faceDir > 0 ? u0 + 0.03 : u1 - 0.03 - depth; lbox(gb, uu, a, uu + depth * (0.6 + R() * 0.4), a + wdt, y, y + hgt, cc); }
        s += wdt + 0.03 + R() * 0.05;
      }
    }
  };

  if (L.extra === 'gunshop') {
    // pegboard with guns on the back wall
    lbox(gb, -8, D - 0.1, 8, D - 0.02, 1.0, 3.3, [0.2, 0.3, 0.22]);
    const rows = [[2.95, ['rifle', 'rifle', 'rifle', 'rifle', 'rifle']], [2.35, ['shotgun', 'shotgun', 'shotgun', 'shotgun']], [1.75, ['smg', 'smg', 'pistol', 'pistol', 'smg', 'pistol']], [1.3, ['rpg', 'bat', 'bat', 'knife', 'knife']]];
    for (const [h, ids] of rows) ids.forEach((id, k) => {
      const m = createWeaponMesh(id);
      if (!m) return;
      const n = ids.length, u = -6.5 + (k + 0.5) * 13 / n;
      m.position.set(...F.P(u - 0.25, D - 0.2, h));
      m.rotation.set(0, F.yawR, 0);
      m.castShadow = false;
      group.add(m);
    });
    // counter-top guns
    for (const [id, u] of [['pistol', -4.5], ['pistol', -3.7], ['smg', -2.4], ['grenade', 3], ['grenade', 3.2], ['knife', 4.3]]) {
      const m = createWeaponMesh(id);
      if (!m) continue;
      m.position.set(...F.P(u, c.w0 + 0.4, 1.07));
      m.rotation.set(0, F.yawR, Math.PI / 2);
      m.castShadow = false;
      group.add(m);
    }
    // ammo shelves
    for (const s of it.furniture.filter((q) => q.kind === 'shelf')) {
      lbox(gb, s.u0, s.w0, s.u1, s.w1, 0, 0.1, [0.25, 0.22, 0.2]);
      products(s.u0, s.w0, s.u1, s.w1, 4, 0.45, 0.5, s.face, 0.4);
    }
    // glass display case with pistols
    const cs = it.furniture.find((q) => q.kind === 'case');
    lbox(gb, cs.u0, cs.w0, cs.u1, cs.w1, 0, 0.7, [0.18, 0.18, 0.2]);
    for (let k = 0; k < 4; k++) { const m = createWeaponMesh(k % 2 ? 'pistol' : 'smg'); m.position.set(...F.P(cs.u0 + 0.6 + k * 1.0, (cs.w0 + cs.w1) / 2, 0.74)); m.rotation.set(0, F.yawR, Math.PI / 2); m.castShadow = false; group.add(m); }
    const [gx0, gz0, gx1, gz1] = F.rect(cs.u0, cs.w0, cs.u1, cs.w1);
    const glass = new THREE.Mesh(new THREE.BoxGeometry(gx1 - gx0, 0.3, gz1 - gz0), M.glass);
    glass.position.set((gx0 + gx1) / 2, F.y + 0.85, (gz0 + gz1) / 2);
    group.add(glass);
    // posters: a paper target and the house rules
    const target = canvasTex(256, 320, (g, w, h) => {
      g.fillStyle = '#f4efe4'; g.fillRect(0, 0, w, h);
      for (let k = 6; k >= 1; k--) { g.beginPath(); g.arc(w / 2, h * 0.45, k * 18, 0, Math.PI * 2); g.fillStyle = k % 2 ? '#111' : '#f4efe4'; g.fill(); }
      g.fillStyle = '#c1121f'; g.beginPath(); g.arc(w / 2, h * 0.45, 12, 0, Math.PI * 2); g.fill();
      g.fillStyle = '#111'; g.font = 'bold 26px Impact, sans-serif'; g.textAlign = 'center'; g.fillText('GUN BARN RANGE', w / 2, h - 22);
    });
    const rules = canvasTex(320, 200, (g, w, h) => {
      g.fillStyle = '#1b1b1b'; g.fillRect(0, 0, w, h);
      g.fillStyle = '#ff5a36'; g.font = 'bold 40px Impact, sans-serif'; g.textAlign = 'center'; g.fillText('NO REFUNDS', w / 2, 70);
      g.fillStyle = '#eee'; g.font = 'bold 22px Arial, sans-serif'; g.fillText('No questions asked.', w / 2, 118); g.fillText('Shoplifters will be shot.', w / 2, 152);
    });
    poster(group, F, target, -H + 0.02, 7.2, 2.2, 1.0, 1.25, 'r');
    poster(group, F, rules, H - 0.02, 6.0, 2.2, 1.6, 1.0, 'l');
  } else if (L.extra === 'burger') {
    // kitchen: grill, fryers, fridge and extractor hood
    lbox(gb, -9, D - 1.1, 9, D - 0.25, 0, 0.92, [0.62, 0.63, 0.65]);
    lbox(gb, -8.6, D - 1.05, -4, D - 0.3, 0.92, 0.98, [0.12, 0.12, 0.12]);
    for (const u of [-2.5, -1.2]) { lbox(gb, u - 0.5, D - 1.0, u + 0.5, D - 0.35, 0.92, 1.0, [0.5, 0.5, 0.5]); lbox(gb, u - 0.42, D - 0.95, u + 0.42, D - 0.4, 1.0, 1.02, [0.75, 0.55, 0.15]); }
    lbox(gb, 6.2, D - 1.1, 8.8, D - 0.25, 0, 2.3, [0.72, 0.73, 0.75]);
    lbox(gb, -9, D - 1.25, 0.5, D - 0.25, 2.3, 2.9, [0.6, 0.61, 0.63]);
    // tables and stools
    for (const t of it.furniture.filter((q) => q.kind === 'table')) {
      const p = F.P(t.u, t.w, 0);
      const top = new THREE.CylinderGeometry(0.55, 0.55, 0.05, 18), leg = new THREE.CylinderGeometry(0.06, 0.18, 0.74, 8);
      col(gb, [0.85, 0.8, 0.72]); gb.addGeometry(top, mat4(p[0], p[1] + 0.76, p[2]));
      col(gb, [0.3, 0.3, 0.32]); gb.addGeometry(leg, mat4(p[0], p[1] + 0.37, p[2]));
      for (const a of [0, Math.PI]) {
        const sx = p[0] + Math.cos(a) * 0.95, sz = p[2] + Math.sin(a) * 0.95;
        col(gb, [0.78, 0.12, 0.08]); gb.addGeometry(new THREE.CylinderGeometry(0.22, 0.22, 0.08, 14), mat4(sx, p[1] + 0.5, sz));
        col(gb, [0.3, 0.3, 0.32]); gb.addGeometry(new THREE.CylinderGeometry(0.04, 0.12, 0.48, 8), mat4(sx, p[1] + 0.24, sz));
      }
    }
    // trays and drinks on the counter
    for (const u of [-5, 1.5, 4.5]) { lbox(gb, u - 0.25, c.w0 + 0.15, u + 0.25, c.w0 + 0.55, 1.05, 1.07, [0.75, 0.2, 0.12]); lbox(gb, u - 0.12, c.w0 + 0.25, u + 0.02, c.w0 + 0.4, 1.07, 1.25, [0.95, 0.95, 0.95]); }
    // the menu board over the counter
    const menu = canvasTex(1024, 256, (g, w, h) => {
      g.fillStyle = '#2a0d05'; g.fillRect(0, 0, w, h);
      g.fillStyle = '#ffd166'; g.font = 'bold 76px Impact, sans-serif'; g.textAlign = 'left'; g.fillText('BIG BUN', 30, 96);
      g.fillStyle = '#ff6b3d'; g.font = 'italic bold 24px Arial, sans-serif'; g.fillText('Order at the counter', 34, 140);
      g.font = 'bold 23px Arial, sans-serif';
      const items = [['Double Stack Combo', '$10'], ['Big Bun Classic', '$6'], ['Cluckin\' Wings', '$7'], ['Fries & Shake', '$4']];
      items.forEach(([n, p], k) => { const x = 340 + (k % 2) * 340, y = 70 + Math.floor(k / 2) * 70; g.textAlign = 'left'; g.fillStyle = '#fff'; g.fillText(n, x, y); g.textAlign = 'right'; g.fillStyle = '#ffd166'; g.fillText(p, x + 318, y); });
      g.textAlign = 'left'; g.fillStyle = '#ffffff'; g.font = 'bold 22px Arial, sans-serif'; g.fillText('Flame grilled since 1983 · Open 24 hours', 340, 225);
    });
    const mm = new THREE.Mesh(new THREE.PlaneGeometry(6.4, 1.6), new THREE.MeshBasicMaterial({ map: menu }));
    mm.position.set(...F.P(0, c.w1 + 0.45, 2.7));
    mm.rotation.y = F.yawIn + Math.PI;
    group.add(mm);
  } else if (L.extra === 'liquor') {
    // aisles of shelves stocked both sides
    for (const a of it.furniture.filter((q) => q.kind === 'aisle')) {
      lbox(gb, a.u - 0.5, a.w0, a.u + 0.5, a.w1, 0, 0.12, [0.3, 0.3, 0.32]);
      lbox(gb, a.u - 0.04, a.w0, a.u + 0.04, a.w1, 0, 1.75, [0.5, 0.5, 0.52]);
      products(a.u - 0.48, a.w0 + 0.05, a.u - 0.06, a.w1 - 0.05, 4, 0.4, 0.42, 1, 0.38);
      products(a.u + 0.06, a.w0 + 0.05, a.u + 0.48, a.w1 - 0.05, 4, 0.4, 0.42, -1, 0.38);
    }
    // wall of fridges with lit bottles
    const fr = it.furniture.find((q) => q.kind === 'fridges');
    lbox(gb, fr.u0, D - 0.95, fr.u1, D - 0.25, 0, 2.2, [0.92, 0.92, 0.94]);
    for (let u = fr.u0 + 0.1; u < fr.u1 - 0.9; u += 1.05) {
      col(em, [0.55, 0.7, 0.8]);
      face(em, [u, 0.3, D - 0.955], [u + 0.95, 0.3, D - 0.955], [u + 0.95, 2.05, D - 0.955], [u, 2.05, D - 0.955], [0, 0, -1]);
      for (let k = 0; k < 4; k++) for (let j = 0; j < 6; j++) {
        const cc = [[0.2, 0.55, 0.15], [0.55, 0.35, 0.1], [0.8, 0.1, 0.1], [0.9, 0.8, 0.2], [0.15, 0.3, 0.7]][Math.floor(R() * 5)];
        const bu = u + 0.1 + j * 0.14, bh = 0.4 + k * 0.42;
        col(em, cc);
        face(em, [bu, bh, D - 0.96], [bu + 0.09, bh, D - 0.96], [bu + 0.09, bh + 0.3, D - 0.96], [bu, bh + 0.3, D - 0.96], [0, 0, -1]);
      }
      lbox(gb, u + 0.93, D - 0.99, u + 1.03, D - 0.9, 0.2, 2.1, [0.7, 0.7, 0.72]);
    }
    // cigarettes behind the counter
    products(-H + 0.25, 1.4, -H + 0.7, 5.0, 3, 1.05, 0.42, 1, 0.3);
    // scratch cards and a lottery sign by the till
    const lotto = canvasTex(256, 128, (g, w, h) => { g.fillStyle = '#ffd60a'; g.fillRect(0, 0, w, h); g.fillStyle = '#c1121f'; g.font = 'bold 44px Impact, sans-serif'; g.textAlign = 'center'; g.fillText('LOTTO', w / 2, 58); g.fillStyle = '#111'; g.font = 'bold 22px Arial'; g.fillText('Win big in Los Soles!', w / 2, 100); });
    poster(group, F, lotto, -H + 0.03, 3.2, 2.5, 1.2, 0.6, 'r');
  }

  const solid = new THREE.Mesh(gb.build(), M.solid);
  solid.receiveShadow = true; solid.castShadow = false;
  group.add(solid);
  if (em.count) { const g = new THREE.Mesh(em.build(), M.glow); group.add(g); }
  return group;
}

// a flat poster on a side wall (at u = uWall) or the back wall
function poster(group, F, tex, uWall, w, h, width, height, facing) {
  const m = new THREE.Mesh(new THREE.PlaneGeometry(width, height), new THREE.MeshStandardMaterial({ map: tex, roughness: 0.9 }));
  m.position.set(...F.P(uWall, w, h));
  m.rotation.y = facing === 'r' ? F.yawR : F.yawR + Math.PI;
  group.add(m);
}

// front-wall pieces around the doorway for the building renderer: [xa, xb, ya, yb] in world x / y
export function doorwayPieces(b) {
  const cx = (b.x0 + b.x1) / 2;
  const dl = cx - DOOR_W / 2, dr = cx + DOOR_W / 2;
  return [[b.x0, dl, b.y0, b.y1], [dr, b.x1, b.y0, b.y1], [dl, dr, b.y0 + DOOR_H, b.y1]];
}
