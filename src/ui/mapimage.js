// Renders the city layout into a canvas used by the radar and the pause-menu map.
import { XS, ZS, HALF_ROAD, CITY, WORLD, WATER_Y, DISTRICTS } from '../world/citymap.js';

export const MAP_EXTENT = { minX: -1500, maxX: 1500, minZ: -1500, maxZ: 1350 };

export function buildMapImage(map, size = 2048) {
  const W = size, H = Math.round(size * (MAP_EXTENT.maxZ - MAP_EXTENT.minZ) / (MAP_EXTENT.maxX - MAP_EXTENT.minX));
  const c = document.createElement('canvas');
  c.width = W; c.height = H;
  const g = c.getContext('2d');
  const sx = W / (MAP_EXTENT.maxX - MAP_EXTENT.minX), sz = H / (MAP_EXTENT.maxZ - MAP_EXTENT.minZ);
  const X = (x) => (x - MAP_EXTENT.minX) * sx, Z = (z) => (z - MAP_EXTENT.minZ) * sz;
  // terrain / water from a coarse sample grid
  const res = 6;
  for (let py = 0; py < H; py += res) {
    for (let px = 0; px < W; px += res) {
      const x = MAP_EXTENT.minX + (px + res / 2) / sx, z = MAP_EXTENT.minZ + (py + res / 2) / sz;
      const h = map.terrainHeight(x, z);
      let col;
      if (h < WATER_Y - 0.2) { const d = Math.min(1, (WATER_Y - h) / 9); col = `rgb(${Math.round(40 - d * 20)},${Math.round(92 - d * 30)},${Math.round(130 - d * 20)})`; }
      else if (z > CITY.maxZ && x < 480 && h < 3) col = '#cdb98a';
      else { const t = Math.min(1, h / 140); col = `rgb(${Math.round(92 + t * 40)},${Math.round(104 + t * 10)},${Math.round(64 + t * 20)})`; }
      g.fillStyle = col;
      g.fillRect(px, py, res, res);
    }
  }
  // blocks
  for (const b of map.blocks) {
    g.fillStyle = '#b9b6ad';
    g.fillRect(X(b.x0), Z(b.z0), (b.x1 - b.x0) * sx, (b.z1 - b.z0) * sz);
    const base = b.park ? '#6f8f4e' : b.ground === 'grass' ? '#8e9a6d' : DISTRICTS[b.district].color;
    g.fillStyle = base;
    g.fillRect(X(b.ix0), Z(b.iz0), (b.ix1 - b.ix0) * sx, (b.iz1 - b.iz0) * sz);
  }
  for (const s of map.lotSurfaces) {
    const col = { grass: '#7e9a5a', dirt: '#9c8468', asphalt: '#6f6f72', plaza: '#b3a58f', concrete: '#a19d95', court: '#4a6f8f', path: '#b5a27f' }[s.type];
    if (!col) continue;
    g.fillStyle = col;
    g.fillRect(X(s.x0), Z(s.z0), (s.x1 - s.x0) * sx, (s.z1 - s.z0) * sz);
  }
  for (const pl of map.pools) { g.fillStyle = '#4fb3c8'; g.fillRect(X(pl.x0), Z(pl.z0), (pl.x1 - pl.x0) * sx, (pl.z1 - pl.z0) * sz); }
  // buildings
  for (const b of map.buildings) {
    if (b.y0 > 1) continue;
    const h = b.y1 - b.y0;
    const k = Math.min(1, h / 120);
    g.fillStyle = `rgba(${Math.round(70 - k * 30)},${Math.round(72 - k * 30)},${Math.round(80 - k * 25)},0.85)`;
    g.fillRect(X(b.x0), Z(b.z0), (b.x1 - b.x0) * sx, (b.z1 - b.z0) * sz);
  }
  // roads
  g.fillStyle = '#3a3b3f';
  for (const x of XS) g.fillRect(X(x - HALF_ROAD), Z(CITY.minZ), HALF_ROAD * 2 * sx, (CITY.maxZ - CITY.minZ) * sz);
  for (const z of ZS) g.fillRect(X(CITY.minX), Z(z - HALF_ROAD), (CITY.maxX - CITY.minX) * sx, HALF_ROAD * 2 * sz);
  g.strokeStyle = 'rgba(230,200,80,0.35)'; g.lineWidth = Math.max(1, sx * 0.6);
  g.beginPath();
  for (const x of XS) { g.moveTo(X(x), Z(CITY.minZ)); g.lineTo(X(x), Z(CITY.maxZ)); }
  for (const z of ZS) { g.moveTo(X(CITY.minX), Z(z)); g.lineTo(X(CITY.maxX), Z(z)); }
  g.stroke();
  // pier
  const P = map.landmarks.pier;
  g.fillStyle = '#8a6a4a';
  g.fillRect(X(P.x0), Z(P.z0), (P.x1 - P.x0) * sx, (P.z1 - P.z0) * sz);
  // district labels (for the big map)
  const labels = {};
  for (const b of map.blocks) { const d = b.district; if (!labels[d]) labels[d] = { x: 0, z: 0, n: 0 }; labels[d].x += b.cx; labels[d].z += b.cz; labels[d].n++; }
  const labelData = Object.entries(labels).map(([d, v]) => ({ name: DISTRICTS[d].name, x: v.x / v.n, z: v.z / v.n }));
  labelData.push({ name: 'Santa Luz Pier', x: (P.x0 + P.x1) / 2, z: P.z1 - 80 }, { name: 'Mount Vista', x: 0, z: -1150 }, { name: 'Red Canyon', x: -1200, z: -200 }, { name: 'Pacific Ocean', x: -300, z: 1100 });
  return { canvas: c, W, H, sx, sz, toPx: (x, z) => [X(x), Z(z)], labels: labelData };
}
