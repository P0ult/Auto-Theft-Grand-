// Renders the map used by the radar and the pause-menu map: a whole-state layer (terrain, water,
// fields, highways, towns, the base) and a sharper city layer drawn on top of it.
import { XS, ZS, HALF_ROAD, CITY, WATER_Y, DISTRICTS } from '../world/citymap.js';
import { WORLD, TOWNS, BASE, AIRFIELD, LAKE, regionWeights } from '../world/worldgen.js';
import { farmMask } from '../world/countryside.js';

export const MAP_EXTENT = WORLD;
const CITY_EXT = { minX: -900, maxX: 1000, minZ: -860, maxZ: 1000 };

export function buildMapImage(map, size = 2048) {
  const world = worldLayer(map, size);
  const city = cityLayer(map, 2048);
  const labels = [];
  const bl = {};
  for (const b of map.blocks) { const d = b.district; if (!bl[d]) bl[d] = { x: 0, z: 0, n: 0 }; bl[d].x += b.cx; bl[d].z += b.cz; bl[d].n++; }
  for (const [d, v] of Object.entries(bl)) labels.push({ name: DISTRICTS[d].name, x: v.x / v.n, z: v.z / v.n });
  const P = map.landmarks.pier;
  labels.push({ name: 'Santa Luz Pier', x: (P.x0 + P.x1) / 2, z: P.z1 - 80 }, { name: 'Mount Vista', x: 0, z: -1250 }, { name: 'Red Canyon', x: -1250, z: -250 }, { name: 'Pacific Ocean', x: -1500, z: 1150 });
  for (const t of Object.values(TOWNS)) labels.push({ name: t.name, x: t.x, z: t.z - t.r - 40, big: true });
  labels.push({ name: 'Fort Carver', x: (BASE.minX + BASE.maxX) / 2, z: BASE.minZ - 50, big: true }, { name: 'Tierra Seca Desert', x: -4600, z: -1500 }, { name: 'Pinewood Forest', x: -300, z: -3500 },
    { name: 'Mount Cedro', x: -900, z: -3250 }, { name: 'Verde County', x: -2400, z: 450 }, { name: 'Lake Mirador', x: LAKE.x, z: LAKE.z }, { name: 'Bayshore', x: 1050, z: -1300 }, { name: AIRFIELD.name, x: AIRFIELD.x - 120, z: AIRFIELD.z + 80 });
  return { ...world, city, labels };
}

// draw both layers into a context whose transform maps world-layer pixels
export function drawMapLayers(ctx, img) {
  ctx.drawImage(img.canvas, 0, 0);
  const c = img.city;
  const [x0, y0] = img.toPx(c.minX, c.minZ), [x1, y1] = img.toPx(c.maxX, c.maxZ);
  ctx.drawImage(c.canvas, x0, y0, x1 - x0, y1 - y0);
}

function worldLayer(map, size) {
  const E = WORLD;
  const W = size, H = Math.round(size * (E.maxZ - E.minZ) / (E.maxX - E.minX));
  const c = document.createElement('canvas');
  c.width = W; c.height = H;
  const g = c.getContext('2d');
  const sx = W / (E.maxX - E.minX), sz = H / (E.maxZ - E.minZ);
  const X = (x) => (x - E.minX) * sx, Z = (z) => (z - E.minZ) * sz;
  const img = g.createImageData(W, H);
  const d = img.data, hf = map.hf;
  const px = 1 / sx;
  for (let py = 0; py < H; py++) {
    const z = E.minZ + (py + 0.5) / sz;
    for (let pxl = 0; pxl < W; pxl++) {
      const x = E.minX + (pxl + 0.5) / sx;
      const h = hf.sample(x, z);
      let r, gg, b;
      const lake = Math.hypot(x - LAKE.x, z - LAKE.z) < LAKE.r + 10 && h < LAKE.y;
      if (h < WATER_Y - 0.2 || lake) { const k = Math.min(1, (WATER_Y - h) / 12); r = 38 - k * 18; gg = 92 - k * 32; b = 132 - k * 20; }
      else {
        const w = regionWeights(x, z);
        const fm = (pxl + py) % 2 === 0 ? farmMask(x, z) : 0;
        r = 128 * w.country + 70 * w.mountain + 206 * w.desert;
        gg = 136 * w.country + 104 * w.mountain + 176 * w.desert;
        b = 76 * w.country + 58 * w.mountain + 120 * w.desert;
        if (fm > 0.3) { r = r * 0.6 + 170 * 0.4; gg = gg * 0.6 + 160 * 0.4; b = b * 0.6 + 70 * 0.4; }
        if (h < 2.2 && w.desert < 0.5) { r = 214; gg = 196; b = 146; }
        const hx = hf.sample(x + px, z) - h, hz = hf.sample(x, z + px) - h;
        const shade = Math.max(0.55, Math.min(1.25, 1 - (hx + hz) / px * 0.9));
        const alt = Math.min(1, Math.max(0, (h - 250) / 400));
        r = (r * (1 - alt) + 190 * alt) * shade; gg = (gg * (1 - alt) + 185 * alt) * shade; b = (b * (1 - alt) + 175 * alt) * shade;
      }
      const i = (py * W + pxl) * 4;
      d[i] = r; d[i + 1] = gg; d[i + 2] = b; d[i + 3] = 255;
    }
  }
  g.putImageData(img, 0, 0);
  // pads (runways etc.)
  for (const p of map.padSurfaces || []) {
    g.save(); g.translate(X(p.cx), Z(p.cz)); g.rotate(-p.yaw);
    g.fillStyle = p.type === 'runway' || p.type === 'taxiway' ? '#3c3d42' : p.type === 'dirtpad' ? '#8c7a5c' : '#9a978f';
    g.fillRect(-p.hx * sx, -p.hz * sz, p.hx * 2 * sx, p.hz * 2 * sz);
    g.restore();
  }
  // roads
  drawRoads(g, map, X, Z, sx, false);
  // countryside buildings
  for (const b of map.buildings) {
    if (!b.rot && b.base == null) continue;
    polyBuilding(g, b, X, Z, sx, sz, 'rgba(88,84,80,0.95)');
  }
  // base perimeter
  g.strokeStyle = 'rgba(40,50,30,0.9)'; g.lineWidth = 1.5;
  g.strokeRect(X(BASE.minX), Z(BASE.minZ), (BASE.maxX - BASE.minX) * sx, (BASE.maxZ - BASE.minZ) * sz);
  return { canvas: c, W, H, sx, sz, toPx: (x, z) => [X(x), Z(z)] };
}

function drawRoads(g, map, X, Z, sx, city) {
  const style = { freeway: ['#e0a340', 9], ramp: ['#e0b050', 5], highway: ['#e8e2d0', 8], road: ['#e8e6de', 6.5], dirt: ['#a58a64', 4] };
  g.lineCap = 'round'; g.lineJoin = 'round';
  for (const pass of [0, 1]) {
    for (const e of map.roads.edges) {
      if (e.removed || e.grid) continue;
      const st = style[e.type];
      if (!st) continue;
      g.beginPath();
      for (let i = 0; i < e.n; i++) { const x = X(e.p[i * 3]), y = Z(e.p[i * 3 + 2]); if (i) g.lineTo(x, y); else g.moveTo(x, y); }
      const w = Math.max(city ? 1.5 : 1.2, st[1] * sx * (city ? 1 : 1.6));
      if (pass === 0) { g.strokeStyle = 'rgba(30,30,32,0.85)'; g.lineWidth = w + (city ? 2 : 1.5); }
      else { g.strokeStyle = st[0]; g.lineWidth = w; }
      g.stroke();
    }
  }
}

function polyBuilding(g, b, X, Z, sx, sz, col) {
  const cx = (b.x0 + b.x1) / 2, cz = (b.z0 + b.z1) / 2, hx = (b.x1 - b.x0) / 2, hz = (b.z1 - b.z0) / 2;
  const s = Math.sin(b.rot || 0), c = Math.cos(b.rot || 0);
  g.fillStyle = col;
  g.beginPath();
  [[-hx, -hz], [hx, -hz], [hx, hz], [-hx, hz]].forEach(([lx, lz], i) => { const x = X(cx + lx * c + lz * s), y = Z(cz - lx * s + lz * c); if (i) g.lineTo(x, y); else g.moveTo(x, y); });
  g.closePath(); g.fill();
}

function cityLayer(map, size) {
  const E = CITY_EXT;
  const W = size, H = Math.round(size * (E.maxZ - E.minZ) / (E.maxX - E.minX));
  const c = document.createElement('canvas');
  c.width = W; c.height = H;
  const g = c.getContext('2d');
  const sx = W / (E.maxX - E.minX), sz = H / (E.maxZ - E.minZ);
  const X = (x) => (x - E.minX) * sx, Z = (z) => (z - E.minZ) * sz;
  // transparent outside the city rect so the world layer shows through
  g.fillStyle = '#3a3b3f';
  g.fillRect(X(CITY.minX), Z(CITY.minZ), (CITY.maxX - CITY.minX) * sx, (CITY.maxZ - CITY.minZ) * sz);
  for (const b of map.blocks) {
    g.fillStyle = '#b9b6ad';
    g.fillRect(X(b.x0), Z(b.z0), (b.x1 - b.x0) * sx, (b.z1 - b.z0) * sz);
    const base = b.park ? '#6f8f4e' : b.ground === 'grass' ? '#8e9a6d' : DISTRICTS[b.district].color;
    g.fillStyle = base;
    g.fillRect(X(b.ix0), Z(b.iz0), (b.ix1 - b.ix0) * sx, (b.iz1 - b.iz0) * sz);
  }
  for (const s of map.lotSurfaces) {
    const col = { grass: '#7e9a5a', dirt: '#9c8468', asphalt: '#6f6f72', plaza: '#b3a58f', concrete: '#a19d95', court: '#4a6f8f', path: '#b5a27f', sand: '#d8c38f' }[s.type];
    if (!col) continue;
    g.fillStyle = col;
    g.fillRect(X(s.x0), Z(s.z0), (s.x1 - s.x0) * sx, (s.z1 - s.z0) * sz);
  }
  for (const pl of map.pools) { g.fillStyle = '#4fb3c8'; g.fillRect(X(pl.x0), Z(pl.z0), (pl.x1 - pl.x0) * sx, (pl.z1 - pl.z0) * sz); }
  for (const b of map.buildings) {
    if (b.y0 > 1 && b.base == null) continue;
    if (b.base != null && !(b.x0 > E.minX && b.x1 < E.maxX && b.z0 > E.minZ && b.z1 < E.maxZ)) continue;
    const h = b.y1 - b.y0;
    const k = Math.min(1, h / 120);
    const col = `rgba(${Math.round(70 - k * 30)},${Math.round(72 - k * 30)},${Math.round(80 - k * 25)},0.85)`;
    if (b.rot) polyBuilding(g, b, X, Z, sx, sz, col);
    else { g.fillStyle = col; g.fillRect(X(b.x0), Z(b.z0), (b.x1 - b.x0) * sx, (b.z1 - b.z0) * sz); }
  }
  g.strokeStyle = 'rgba(230,200,80,0.35)'; g.lineWidth = Math.max(1, sx * 0.6);
  g.beginPath();
  for (const e of map.roads.edges) { if (e.removed || !e.grid) continue; g.moveTo(X(e.p[0]), Z(e.p[2])); g.lineTo(X(e.p[(e.n - 1) * 3]), Z(e.p[(e.n - 1) * 3 + 2])); }
  g.stroke();
  // freeway & ramps over the city, beach & pier
  drawRoads(g, map, X, Z, sx, true);
  const P = map.landmarks.pier;
  g.fillStyle = '#8a6a4a';
  g.fillRect(X(P.x0), Z(P.z0), (P.x1 - P.x0) * sx, (P.z1 - P.z0) * sz);
  return { canvas: c, minX: E.minX, minZ: E.minZ, maxX: E.maxX, maxZ: E.maxZ };
}
