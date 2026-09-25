// Everything outside Los Soles that isn't terrain or road: the towns of Fern Creek, Pine Hollow and
// Dry Wells (buildings lined up along their curving streets), farmsteads in Verde County, the Fern
// Creek airfield, the Fort Carver military base, power lines along the highways and the vegetation
// scatter (pine forests, oak hills, desert cacti & rocks).
import { RNG, clamp, lerp, smoothstep, hash2 } from '../core/utils.js';
import { TOWNS, BASE, AIRFIELD, LAKE, regionWeights, cityDist, riverDist, scatter, fbmN } from './worldgen.js';

export function farmMask(x, z) {
  const w = regionWeights(x, z);
  if (w.country < 0.55) return 0;
  const dC = cityDist(x, z);
  if (dC < 1000) return 0;
  const n = fbmN(x * 0.0016 + 40, z * 0.0016 - 13, 2);
  let m = smoothstep(0.42, 0.52, n) * smoothstep(0.55, 0.85, w.country) * smoothstep(1000, 1400, dC);
  for (const t of Object.values(TOWNS)) m *= smoothstep(t.r * 0.9, t.r * 1.4, Math.hypot(x - t.x, z - t.z));
  m *= smoothstep(60, 140, riverDist(x, z));
  m *= smoothstep(LAKE.r + 60, LAKE.r + 200, Math.hypot(x - LAKE.x, z - LAKE.z));
  return m;
}

export function populateCountryside(map) {
  const rng = new RNG(9001);
  map.townAreas = [];
  map.padSurfaces = [];
  map.vegetation = { pine: [], oak: [], bush: [], cactus: [], rock: [], deadtree: [], palm: [] };
  map.fixedVehicles = [];
  const ctx = { map, rng, hf: map.hf, net: map.roads, footprints: [] };
  // existing city footprints are irrelevant out here; track what we place to avoid overlaps
  for (const [key, t] of Object.entries(map.roadInfo.towns)) town(ctx, key, t);
  farms(ctx);
  airfield(ctx);
  base(ctx);
  powerLines(ctx);
  vegetation(ctx);
}

// ------------------------------------------------------------------ helpers
function slopeAt(hf, x, z) {
  const e = 4;
  const dx = hf.sample(x + e, z) - hf.sample(x - e, z), dz = hf.sample(x, z + e) - hf.sample(x, z - e);
  return Math.hypot(dx, dz) / (2 * e);
}
// corners of a rotated rect (centre, half sizes along local x / z, yaw)
function corners(cx, cz, hx, hz, yaw) {
  const s = Math.sin(yaw), c = Math.cos(yaw);
  return [[-hx, -hz], [hx, -hz], [hx, hz], [-hx, hz]].map(([lx, lz]) => [cx + lx * c + lz * s, cz - lx * s + lz * c]);
}
function rectsOverlap(a, b) {
  // SAT between two rotated rects {cx,cz,hx,hz,yaw}
  const axes = [];
  for (const r of [a, b]) { const s = Math.sin(r.yaw), c = Math.cos(r.yaw); axes.push([c, -s], [s, c]); }
  const dx = b.cx - a.cx, dz = b.cz - a.cz;
  for (const [ax, az] of axes) {
    const proj = (r) => { const s = Math.sin(r.yaw), c = Math.cos(r.yaw); return Math.abs((c * ax - s * az)) * r.hx + Math.abs((s * ax + c * az)) * r.hz; };
    if (Math.abs(dx * ax + dz * az) > proj(a) + proj(b)) return false;
  }
  return true;
}
function canPlace(ctx, r, opts = {}) {
  const { map, hf, footprints } = ctx;
  const margin = opts.margin ?? 2;
  const grown = { ...r, hx: r.hx + margin, hz: r.hz + margin };
  for (const f of footprints) if (rectsOverlap(grown, f)) return false;
  const pts = corners(r.cx, r.cz, r.hx, r.hz, r.yaw);
  pts.push([r.cx, r.cz]);
  let lo = Infinity, hi = -Infinity;
  for (const [x, z] of pts) {
    if (map.roads.onRoad(x, z, opts.roadMargin ?? 1.5)) return false;
    const h = hf.sample(x, z);
    if (h < 0.4) return false;
    lo = Math.min(lo, h); hi = Math.max(hi, h);
  }
  // also check along the edges for roads
  for (let k = 0; k < 4; k++) { const a = pts[k], b = pts[(k + 1) % 4]; const mx = (a[0] + b[0]) / 2, mz = (a[1] + b[1]) / 2; if (map.roads.onRoad(mx, mz, opts.roadMargin ?? 1.5)) return false; }
  if (hi - lo > (opts.maxDrop ?? 3.5)) return false;
  // junction pads
  for (const n of map.roads.nodes) {
    if (n.grid || !n.e.length) continue;
    const rr = (n.kind === 'rb' ? n.rbR + 12 : n.r + 4) + Math.max(r.hx, r.hz);
    if (Math.abs(n.x - r.cx) < rr && Math.abs(n.z - r.cz) < rr && Math.hypot(n.x - r.cx, n.z - r.cz) < rr) return false;
  }
  r.y0 = lo - 0.25; r.yTop = hi;
  return true;
}
function addBld(ctx, r, height, style, o = {}) {
  const { map } = ctx;
  const b = map._addBuilding(null, r.cx - r.hx, r.cz - r.hz, r.cx + r.hx, r.cz + r.hz, height + (r.yTop - r.y0), style, {
    rot: r.yaw, y0: r.y0, tint: o.tint, seed: o.seed ?? ctx.rng.next(), roof: o.roof || 'flat', floorH: o.floorH, cell: o.cell, kind: o.kind || 'building',
    name: o.name, sign: o.sign, noCollide: o.noCollide, base: r.y0,
  });
  if (b) { b.district = o.district || 'country'; b.front = o.front || null; b.foundation = r.yTop - r.y0; }
  ctx.footprints.push(r);
  return b;
}

// ------------------------------------------------------------------ towns
const TOWN_STYLE = {
  fern: {
    district: 'country',
    houses: () => ({ style: 3, roof: 'gable', floors: [1, 2], tint: [[1, 0.95, 0.85], [0.92, 0.88, 0.78], [0.85, 0.9, 0.95], [1, 0.85, 0.8], [0.95, 0.95, 0.9], [0.8, 0.88, 0.78]] }),
    shops: [['FERN CREEK DINER', 'Fern Creek Diner'], ['GENERAL STORE', 'Olsen General Store'], ['FEED & SEED', 'Feed & Seed'], ['SALOON', 'Rusty Spur Saloon'], ['HARDWARE', 'Hank\'s Hardware']],
  },
  pine: {
    district: 'forest',
    houses: () => ({ style: 2, roof: 'gable', floors: [1, 2], tint: [[0.75, 0.55, 0.4], [0.62, 0.48, 0.36], [0.7, 0.62, 0.5], [0.55, 0.42, 0.32]] }),
    shops: [['LODGE', 'Pine Hollow Lodge'], ['BAIT & TACKLE', 'Bait & Tackle'], ['CAFE', 'Timberline Cafe']],
  },
  dry: {
    district: 'desert',
    houses: () => ({ style: 3, roof: 'flat', floors: [1, 1], tint: [[0.95, 0.78, 0.6], [0.9, 0.72, 0.55], [1, 0.85, 0.7], [0.85, 0.65, 0.5], [0.95, 0.9, 0.8]] }),
    shops: [['MOTEL', 'Desert Rose Motel'], ['CANTINA', 'Cantina Los Muertos'], ['TRADING POST', 'Trading Post'], ['GUNS', 'Dry Wells Guns & Ammo']],
  },
};

function town(ctx, key, t) {
  const { map, rng, net, hf } = ctx;
  const T = TOWNS[key];
  const S = TOWN_STYLE[key];
  const area = { district: S.district, nodeIds: [], name: T.name, x: T.x, z: T.z, r: T.r + 40, town: true };
  map.townAreas.push(area);
  let shopIdx = 0;
  const walk = map.walkNodes;
  const edges = new Set();
  for (const r of t.roads) for (const e of r.edges) if (!e.removed) edges.add(e);
  // include every non-grid edge that passes through the town
  for (const e of net.edgesIn(T.x - T.r, T.z - T.r, T.x + T.r, T.z + T.r)) if (!e.grid && e.type !== 'freeway' && e.type !== 'ramp') edges.add(e);
  const tmp = [0, 0, 0, 0, 0];
  let gasDone = false;
  for (const e of edges) {
    if (e.type === 'dirt' && key !== 'pine') continue;
    // walk nodes along both sides of the street
    const sideNodes = [[], []];
    for (let s = 6; s < e.len - 6; s += 22) {
      net.at(e, s, tmp);
      const d = Math.hypot(tmp[0] - T.x, tmp[2] - T.z);
      if (d > T.r + 30) continue;
      for (const side of [-1, 1]) {
        const off = (side < 0 ? e.wL : e.wR) + 2.2;
        const x = tmp[0] - tmp[4] * off * side, z = tmp[2] + tmp[3] * off * side;
        if (net.onRoad(x, z, 0.3)) continue;
        const id = walk.length;
        walk.push({ id, x, z, links: [], town: key });
        area.nodeIds.push(id);
        const list = sideNodes[side < 0 ? 0 : 1];
        if (list.length) { const p = list[list.length - 1]; if (Math.hypot(walk[p].x - x, walk[p].z - z) < 40) { walk[p].links.push(id); walk[id].links.push(p); } }
        list.push(id);
      }
    }
    // cross links every few nodes
    for (let k = 0; k < Math.min(sideNodes[0].length, sideNodes[1].length); k += 3) {
      const a = sideNodes[0][k], b = sideNodes[1][k];
      walk[a].links.push(b); walk[b].links.push(a);
      walk[a].cross = [b]; walk[b].cross = [a];
    }
    // buildings along the street
    let s = 10 + rng.range(0, 8);
    while (s < e.len - 10) {
      net.at(e, s, tmp);
      const dCenter = Math.hypot(tmp[0] - T.x, tmp[2] - T.z);
      if (dCenter > T.r) { s += 20; continue; }
      const core = dCenter < T.r * 0.45;
      const side = rng.chance(0.5) ? 1 : -1;
      for (const sd of [side, -side]) {
        const shop = core && rng.chance(0.55) && shopIdx < S.shops.length;
        const gas = !gasDone && dCenter > T.r * 0.4 && e.T.cls >= 1 && rng.chance(0.25);
        const w = gas ? 26 : shop ? rng.range(14, 20) : rng.range(9, 13);
        const dpt = gas ? 22 : shop ? rng.range(12, 16) : rng.range(8, 11);
        const setback = gas ? 5 : shop ? 3.5 : rng.range(5, 9);
        const off = (sd < 0 ? e.wL : e.wR) + setback + dpt / 2;
        const rx = -tmp[4] * sd, rz = tmp[3] * sd; // outward normal
        const cx = tmp[0] + rx * off, cz = tmp[2] + rz * off;
        const yaw = Math.atan2(tmp[3], tmp[4]);
        const r = { cx, cz, hx: dpt / 2, hz: w / 2, yaw };
        if (!canPlace(ctx, r, { margin: gas ? 3 : 1.5 })) continue;
        const front = [-rx, -rz];
        if (gas) { gasStation(ctx, r, front, S.district); gasDone = true; continue; }
        if (shop) {
          const [sign, name] = S.shops[shopIdx++];
          const floors = key === 'dry' ? 1 : rng.int(1, 2);
          addBld(ctx, r, floors * 4.2 + 0.6, 5, { tint: rng.pick([[0.95, 0.9, 0.8], [0.9, 0.8, 0.7], [0.85, 0.88, 0.92], [1, 0.95, 0.85]]), roof: key === 'pine' ? 'gable' : 'flat', floorH: 4.2, name, sign, front, district: S.district });
          // parking in front
          for (let k = -1; k <= 1; k += 2) if (rng.chance(0.5)) map.parkingSpots.push({ x: cx - rx * (dpt / 2 + 2.2) + tmp[3] * k * 4, z: cz - rz * (dpt / 2 + 2.2) + tmp[4] * k * 4, rot: yaw, district: S.district, lot: true });
        } else {
          const H = S.houses();
          const floors = rng.int(H.floors[0], H.floors[1]);
          addBld(ctx, r, floors * 3.1 + 0.4, H.style, { tint: rng.pick(H.tint), roof: H.roof, floorH: 3.1, cell: 3.0, kind: 'house', front, district: S.district });
          if (rng.chance(0.5)) map.parkingSpots.push({ x: cx - rx * (dpt / 2 + 2.6) + tmp[3] * (w / 2 + 2), z: cz - rz * (dpt / 2 + 2.6) + tmp[4] * (w / 2 + 2), rot: yaw + Math.PI / 2 * sd, district: S.district, driveway: true });
          if (key !== 'dry' && rng.chance(0.6)) map.props.push({ type: key === 'pine' ? 'tree' : rng.chance(0.3) ? 'palm' : 'tree', x: cx + tmp[3] * (w / 2 + 3), z: cz + tmp[4] * (w / 2 + 3), rot: rng.range(0, 6.28), scale: rng.range(0.9, 1.3) });
        }
      }
      // street lights in the town core
      if (core && rng.chance(0.7)) {
        const off = e.wR + 1.2;
        map.props.push({ type: 'streetlight', x: tmp[0] - tmp[4] * off, z: tmp[2] + tmp[3] * off, rot: Math.atan2(tmp[4], -tmp[3]) + Math.PI, y: hf.sample(tmp[0] - tmp[4] * off, tmp[2] + tmp[3] * off) });
      }
      s += rng.range(18, 26);
    }
  }
  // link walk nodes near the roundabout into a loop
  const ids = area.nodeIds;
  for (let k = 0; k < ids.length; k++) {
    const a = walk[ids[k]];
    if (a.links.length > 1) continue;
    let best = null, bd = 45;
    for (const id of ids) { if (id === a.id || a.links.includes(id)) continue; const b = walk[id]; const d = Math.hypot(a.x - b.x, a.z - b.z); if (d < bd) { bd = d; best = b; } }
    if (best) { a.links.push(best.id); best.links.push(a.id); }
  }
  // drop orphan nodes from the spawn list
  area.nodeIds = ids.filter((id) => walk[id].links.length);
}

function gasStation(ctx, r, front, district) {
  const { map, rng } = ctx;
  // store at the back, canopy over the pumps in front
  const s = Math.sin(r.yaw), c = Math.cos(r.yaw);
  const L = (lx, lz) => [r.cx + lx * c + lz * s, r.cz - lx * s + lz * c];
  const back = { cx: 0, cz: 0, hx: 4, hz: 7, yaw: r.yaw, y0: r.y0, yTop: r.yTop };
  [back.cx, back.cz] = L(r.hx - 4.2, 0);
  map._addBuilding(null, back.cx - 4, back.cz - 7, back.cx + 4, back.cz + 7, 4.4 + (r.yTop - r.y0), 5, { rot: r.yaw, y0: r.y0, tint: [0.95, 0.95, 0.95], seed: rng.next(), roof: 'flat', floorH: 4.4, name: 'Gas Station', sign: 'GAS', district }).front = front;
  const [ccx, ccz] = L(-3.5, 0);
  map._addBuilding(null, ccx - 5, ccz - 9, ccx + 5, ccz + 9, 0.7, 4, { rot: r.yaw, y0: r.yTop + 4.6, tint: [0.85, 0.1, 0.1], seed: rng.next(), roof: 'flat', noCollide: true, district });
  for (const lz of [-6, 0, 6]) { const [px, pz] = L(-3.5, lz); map.props.push({ type: 'pump', x: px, z: pz, rot: r.yaw + Math.PI / 2, y: r.yTop }); }
  for (const lz of [-8.5, 8.5]) for (const lx of [-8, 1]) { const [px, pz] = L(lx, lz); map.props.push({ type: 'post', x: px, z: pz, rot: 0, y: r.yTop, h: 4.6 }); }
  map.padSurfaces.push({ cx: r.cx, cz: r.cz, hx: r.hx + 3, hz: r.hz + 2, yaw: r.yaw, y: r.yTop + 0.03, type: 'concrete' });
  for (let k = 0; k < 2; k++) { const [px, pz] = L(-2, -10 + k * 20); map.parkingSpots.push({ x: px, z: pz, rot: r.yaw, district, lot: true }); }
  ctx.footprints.push(r);
}

// ------------------------------------------------------------------ farms
function farms(ctx) {
  const { map, rng, net, hf } = ctx;
  const cands = [];
  for (const e of net.edges) {
    if (e.removed || e.grid || e.type === 'freeway' || e.type === 'ramp') continue;
    for (let s = 40; s < e.len - 40; s += 90) {
      const p = net.at(e, s);
      if (farmMask(p[0], p[2]) < 0.4 && !(e.type === 'dirt' && regionWeights(p[0], p[2]).country > 0.6)) continue;
      cands.push({ e, s, x: p[0], z: p[2], tx: p[3], tz: p[4] });
    }
  }
  let made = 0;
  for (let i = 0; i < cands.length && made < 14; i++) {
    const c = cands[Math.floor(hash2(i, 55) * cands.length)];
    if (!c || c.used) continue;
    c.used = true;
    const side = hash2(i, 9) < 0.5 ? 1 : -1;
    const rx = -c.tz * side, rz = c.tx * side;
    const yaw = Math.atan2(c.tx, c.tz);
    // farmhouse near the road, barn + silo behind
    const off = (side < 0 ? c.e.wL : c.e.wR);
    const house = { cx: c.x + rx * (off + 14), cz: c.z + rz * (off + 14), hx: 5.5, hz: 7, yaw };
    if (!canPlace(ctx, house, { margin: 3 })) continue;
    addBld(ctx, house, rng.chance(0.5) ? 6.6 : 3.5, 3, { tint: rng.pick([[1, 0.97, 0.9], [0.95, 0.92, 0.85], [0.85, 0.9, 0.95]]), roof: 'gable', floorH: 3.1, cell: 3, kind: 'house', front: [-rx, -rz] });
    const barn = { cx: house.cx + rx * 30 + c.tx * 16, cz: house.cz + rz * 30 + c.tz * 16, hx: 9, hz: 13, yaw };
    if (canPlace(ctx, barn, { margin: 2, maxDrop: 5 })) addBld(ctx, barn, 7.5, 4, { tint: [0.78, 0.22, 0.16], roof: 'gable', floorH: 7.5, cell: 5, kind: 'barn' });
    const sx = house.cx + rx * 30 - c.tx * 6, sz = house.cz + rz * 30 - c.tz * 6;
    if (!net.onRoad(sx, sz, 3)) map.props.push({ type: 'silo', x: sx, z: sz, rot: 0, y: hf.sample(sx, sz) });
    // hay bales and a fence line along the road
    for (let k = 0; k < 5; k++) { const hx = house.cx + rx * rng.range(18, 45) + c.tx * rng.range(-30, 30), hz = house.cz + rz * rng.range(18, 45) + c.tz * rng.range(-30, 30); if (!net.onRoad(hx, hz, 3)) map.props.push({ type: 'haybale', x: hx, z: hz, rot: rng.range(0, 6.28), y: hf.sample(hx, hz) }); }
    for (let k = -3; k <= 3; k++) {
      const fx = c.x + rx * (off + 3.5) + c.tx * k * 12, fz = c.z + rz * (off + 3.5) + c.tz * k * 12;
      if (Math.abs(k) <= 0 || net.onRoad(fx, fz, 1)) continue;
      const y = hf.sample(fx, fz);
      map.fences.push({ rot: yaw, cx: fx, cz: fz, hx: 0.06, hz: 6, h: 1.2, type: 'wood', y: y - 0.15, x0: fx, z0: fz, x1: fx, z1: fz });
    }
    map.parkingSpots.push({ x: house.cx - rx * 9 + c.tx * 9, z: house.cz - rz * 9 + c.tz * 9, rot: yaw, district: 'country', driveway: true, rural: true });
    made++;
  }
}

// ------------------------------------------------------------------ airfield
function airfield(ctx) {
  const { map, hf, rng } = ctx;
  const A = AIRFIELD;
  const y = hf.sample(A.x, A.z);
  map.padSurfaces.push({ cx: A.x, cz: A.z, hx: 15, hz: A.len / 2, yaw: 0, y: y + 0.05, type: 'runway' });
  map.padSurfaces.push({ cx: A.x - 34, cz: A.z - 150, hx: 18, hz: 26, yaw: 0, y: y + 0.05, type: 'apron' });
  const hg = { cx: A.x - 45, cz: A.z - 150, hx: 11, hz: 14, yaw: Math.PI / 2 };
  if (canPlace(ctx, hg, { margin: 0.5, maxDrop: 6 })) addBld(ctx, hg, 8, 4, { tint: [0.75, 0.78, 0.8], roof: 'flat', floorH: 8, cell: 6, kind: 'hangar', name: A.name, sign: 'AIRFIELD' });
  map.props.push({ type: 'windsock', x: A.x + 24, z: A.z - A.len / 2 + 40, rot: 0, y });
  map.fixedVehicles.push({ type: 'skipper', x: A.x - 22, z: A.z - 150, yaw: Math.PI, y: y + 0.05, respawn: 180 });
  map.landmarks.airfield = { x: A.x, z: A.z, y };
}

// ------------------------------------------------------------------ Fort Carver
function base(ctx) {
  const { map, hf, rng } = ctx;
  const B = BASE;
  const y = hf.sample((B.minX + B.maxX) / 2, (B.minZ + B.maxZ) / 2);
  const P = (x, z, hx, hz, yaw = 0) => ({ cx: x, cz: z, hx, hz, yaw, y0: y - 0.25, yTop: y });
  const bld = (r, h, style, o = {}) => addBld(ctx, r, h, style, { district: 'base', ...o });
  const runZ = -4520, runX0 = -5230, runX1 = -3950;
  map.padSurfaces.push({ cx: (runX0 + runX1) / 2, cz: runZ, hx: 26, hz: (runX1 - runX0) / 2, yaw: Math.PI / 2, y: y + 0.05, type: 'runway' });
  map.padSurfaces.push({ cx: -4575, cz: -4400, hx: 12, hz: 575, yaw: Math.PI / 2, y: y + 0.045, type: 'taxiway' });
  for (const x of [-5140, -4575, -4010]) map.padSurfaces.push({ cx: x, cz: -4460, hx: 12, hz: 48, yaw: 0, y: y + 0.042, type: 'taxiway' });
  map.padSurfaces.push({ cx: -4650, cz: -4270, hx: 350, hz: 118, yaw: 0, y: y + 0.04, type: 'apron' });
  // hangars facing the apron (door side south towards the taxiway is the -z face)
  const hangarXs = [-4900, -4780, -4660, -4540, -4420];
  hangarXs.forEach((x, i) => bld(P(x, -4125, 26, 22), 17, 4, { tint: [0.62, 0.66, 0.6], roof: 'flat', floorH: 17, cell: 7, kind: 'hangar', name: i === 0 ? 'Fort Carver' : undefined, sign: i === 2 ? 'FORT CARVER' : undefined }));
  // control tower
  bld(P(-4300, -4180, 6, 6), 26, 0, { tint: [0.85, 0.85, 0.8], roof: 'flat', floorH: 3.6 });
  map._addBuilding(null, -4308, -4188, -4292, -4172, 5, 1, { y0: y + 26, tint: [0.4, 0.6, 0.7], seed: 0.4, roof: 'antenna', noCollide: false }).district = 'base';
  // barracks, mess hall, HQ
  for (let k = 0; k < 4; k++) bld(P(-5150 + k * 90, -3960, 36, 9), 7, 2, { tint: [0.78, 0.72, 0.6], roof: 'flat', floorH: 3.5 });
  bld(P(-4200, -3960, 26, 14), 10, 0, { tint: [0.8, 0.8, 0.74], roof: 'antenna', floorH: 3.4, name: 'Fort Carver HQ' });
  // vehicle sheds + tank yard
  bld(P(-4020, -3950, 30, 10), 8, 4, { tint: [0.55, 0.6, 0.5], roof: 'flat', floorH: 8, cell: 6, kind: 'hangar' });
  map.padSurfaces.push({ cx: -4070, cz: -4150, hx: 110, hz: 60, yaw: 0, y: y + 0.04, type: 'dirtpad' });
  // helipads
  for (const x of [-4720, -4640, -4560]) map.padSurfaces.push({ cx: x, cz: -4020, hx: 13, hz: 13, yaw: 0, y: y + 0.05, type: 'helipad' });
  // fuel tanks, radar, water tower
  for (let k = 0; k < 4; k++) map.props.push({ type: 'fueltank', x: -5230 + (k % 2) * 22, z: -4230 + Math.floor(k / 2) * 22, rot: 0, y });
  map.props.push({ type: 'radar', x: -5250, z: -3930, rot: 0, y });
  // watchtowers at the corners + perimeter fence with a gate gap on the east side
  const fence = (x0, z0, x1, z1) => {
    const len = Math.hypot(x1 - x0, z1 - z0), n = Math.ceil(len / 24);
    for (let k = 0; k < n; k++) {
      const ax = lerp(x0, x1, k / n), az = lerp(z0, z1, k / n), bx = lerp(x0, x1, (k + 1) / n), bz = lerp(z0, z1, (k + 1) / n);
      const cx = (ax + bx) / 2, cz = (az + bz) / 2;
      if (x0 === x1 && Math.abs(cz - B.gateZ) < 16 && x0 === B.maxX) continue;
      map.fences.push({ x0: Math.min(ax, bx) - (x0 === x1 ? 0.06 : 0), z0: Math.min(az, bz) - (z0 === z1 ? 0.06 : 0), x1: Math.max(ax, bx) + (x0 === x1 ? 0.06 : 0), z1: Math.max(az, bz) + (z0 === z1 ? 0.06 : 0), h: 4, type: 'chain', y: hf.sample(cx, cz) - 0.15, tall: true });
    }
  };
  fence(B.minX, B.minZ, B.maxX, B.minZ); fence(B.minX, B.maxZ, B.maxX, B.maxZ); fence(B.minX, B.minZ, B.minX, B.maxZ); fence(B.maxX, B.minZ, B.maxX, B.maxZ);
  for (const [x, z] of [[B.minX + 6, B.minZ + 6], [B.maxX - 6, B.minZ + 6], [B.minX + 6, B.maxZ - 6], [B.maxX - 6, B.maxZ - 6], [B.maxX - 8, B.gateZ - 24], [B.maxX - 8, B.gateZ + 24]]) {
    map._addBuilding(null, x - 2.5, z - 2.5, x + 2.5, z + 2.5, 3.2, 4, { y0: y + 7, tint: [0.5, 0.55, 0.45], seed: 0.3, roof: 'flat', noCollide: true }).district = 'base';
    for (const [ox, oz] of [[-2, -2], [2, -2], [2, 2], [-2, 2]]) map.props.push({ type: 'stilt', x: x + ox, z: z + oz, rot: 0, y });
  }
  map.props.push({ type: 'boothbar', x: B.maxX + 6, z: B.gateZ + 8, rot: 0, y });
  map.padSurfaces.push({ cx: B.maxX - 20, cz: B.gateZ, hx: 14, hz: 30, yaw: Math.PI / 2, y: y + 0.03, type: 'concrete' });
  // sandbag walls around the helipads & tank yard
  for (let k = 0; k < 6; k++) map.props.push({ type: 'sandbags', x: -4130 + k * 22, z: -4225, rot: 0, y });
  // the hardware
  const FV = map.fixedVehicles;
  FV.push({ type: 'raptor', x: -4900, z: -4230, yaw: Math.PI, y, respawn: 240 });
  FV.push({ type: 'raptor', x: -4780, z: -4230, yaw: Math.PI, y, respawn: 240 });
  FV.push({ type: 'hercules', x: -4620, z: -4250, yaw: Math.PI, y, respawn: 300 });
  FV.push({ type: 'warhawk', x: -4720, z: -4020, yaw: Math.PI, y: y + 0.05, respawn: 240 });
  FV.push({ type: 'skylark', x: -4640, z: -4020, yaw: Math.PI, y: y + 0.05, respawn: 200 });
  FV.push({ type: 'mammoth', x: -4120, z: -4150, yaw: -Math.PI / 2, y, respawn: 240 });
  FV.push({ type: 'mammoth', x: -4060, z: -4150, yaw: -Math.PI / 2, y, respawn: 240 });
  FV.push({ type: 'ranger', x: -4000, z: -4130, yaw: -Math.PI / 2, y, respawn: 120 });
  FV.push({ type: 'ranger', x: -4000, z: -4170, yaw: -Math.PI / 2, y, respawn: 120 });
  FV.push({ type: 'barracks', x: -4180, z: -4150, yaw: -Math.PI / 2, y, respawn: 160 });
  map.landmarks.base = { x: (B.minX + B.maxX) / 2, z: (B.minZ + B.maxZ) / 2, y, gate: { x: B.maxX + 20, z: B.gateZ }, runway: { x0: runX0, x1: runX1, z: runZ } };
  // a helicopter on the hospital roof in the city and one at the police station (the city stays fun)
  const hosp = map.buildings.find((b) => b.name === 'All Saints General');
  if (hosp) FV.push({ type: 'skylark', x: (hosp.x0 + hosp.x1) / 2, z: (hosp.z0 + hosp.z1) / 2, yaw: 0, y: hosp.y1 + 0.3, respawn: 200, roof: true });
}

// ------------------------------------------------------------------ power lines along the highways
function powerLines(ctx) {
  const { map, net, hf } = ctx;
  for (const e of net.edges) {
    if (e.removed || e.grid || (e.type !== 'highway' && e.type !== 'road')) continue;
    if (e.name === 'Main Street' || e.base) continue;
    const off = e.wR + 7;
    for (let s = 20; s < e.len - 20; s += 55) {
      const p = net.at(e, s);
      if (cityDist(p[0], p[2]) < 60) continue;
      const x = p[0] - p[4] * off, z = p[2] + p[3] * off;
      if (net.onRoad(x, z, 2)) continue;
      const y = hf.sample(x, z);
      if (y < 0.5 || Math.abs(y - p[1]) > 6) continue;
      map.props.push({ type: 'powerpole', x, z, rot: Math.atan2(p[3], p[4]), y });
    }
  }
}

// ------------------------------------------------------------------ vegetation
function vegetation(ctx) {
  const { map, hf, net } = ctx;
  const V = map.vegetation;
  const W = { minX: -5550, maxX: 1500, minZ: -5350, maxZ: 1300 };
  const FG = new Map(), fc = 64;
  for (const f of ctx.footprints) {
    const R = f.hx + f.hz + 4;
    for (let gx = Math.floor((f.cx - R) / fc); gx <= Math.floor((f.cx + R) / fc); gx++) for (let gz = Math.floor((f.cz - R) / fc); gz <= Math.floor((f.cz + R) / fc); gz++) {
      const k = gx * 7919 + gz; if (!FG.has(k)) FG.set(k, []); FG.get(k).push(f);
    }
  }
  const blocked = (x, z, r) => {
    if (cityDist(x, z) < 12) return true;
    if (net.onRoad(x, z, r + 2)) return true;
    const arr = FG.get(Math.floor(x / fc) * 7919 + Math.floor(z / fc));
    if (arr) for (const f of arr) if (Math.abs(f.cx - x) < f.hx + f.hz + r + 3 && Math.abs(f.cz - z) < f.hx + f.hz + r + 3) return true;
    if (x > BASE.minX - 30 && x < BASE.maxX + 30 && z > BASE.minZ - 30 && z < BASE.maxZ + 30) return true;
    if (Math.abs(x - AIRFIELD.x) < 90 && Math.abs(z - AIRFIELD.z) < AIRFIELD.len / 2 + 60) return true;
    return false;
  };
  const push = (list, x, z, r3, scale, sink = 0.1) => {
    const y = hf.sample(x, z);
    list.push(x, y - sink, z, r3 * 6.283, scale);
  };
  const townNear = (x, z) => { for (const t of Object.values(TOWNS)) { const d = Math.hypot(x - t.x, z - t.z); if (d < t.r + 20) return true; } return false; };
  // pines: dense forests in the mountains, thinning with altitude and on steep rock
  for (const [x, z, r1, r2, r3] of scatter(W.minX, W.minZ, W.maxX, W.maxZ, 15, 11, (x, z) => {
    const w = regionWeights(x, z);
    if (w.mountain < 0.25) return 0;
    const h = hf.sample(x, z);
    if (h < 1.5 || h > 560) return 0;
    const dens = smoothstep(0.25, 0.7, w.mountain) * (0.35 + 0.65 * smoothstep(0.35, 0.6, fbmN(x * 0.003, z * 0.003, 2))) * (1 - smoothstep(420, 560, h));
    return dens * 0.85;
  })) {
    if (slopeAt(hf, x, z) > 0.75 || blocked(x, z, 0.5) || Math.hypot(x - LAKE.x, z - LAKE.z) < LAKE.r + 8) continue;
    push(V.pine, x, z, r3, 0.75 + r1 * 0.7);
  }
  // oaks & bushes on the grassy hills and farmland edges
  for (const [x, z, r1, r2, r3] of scatter(W.minX, W.minZ, W.maxX, W.maxZ, 34, 23, (x, z) => {
    const w = regionWeights(x, z);
    const dC = cityDist(x, z);
    const ringHills = dC > 20 && dC < 1300 ? 0.55 : 0;
    const d = Math.max(w.country * 0.45, ringHills, w.mountain * 0.2) * (1 - farmMask(x, z)) * (0.3 + 0.9 * smoothstep(0.4, 0.65, fbmN(x * 0.004 + 5, z * 0.004, 2)));
    return d;
  })) {
    const h = hf.sample(x, z);
    if (h < 1.2 || slopeAt(hf, x, z) > 0.6 || blocked(x, z, 0.6)) continue;
    if (townNear(x, z) && r2 < 0.6) continue;
    if (r1 < 0.62) push(V.oak, x, z, r3, 0.8 + r2 * 0.8);
    else push(V.bush, x, z, r3, 0.7 + r2 * 0.9, 0.2);
  }
  // riverside palms near the coast & lake shores
  for (const [x, z, r1, r2, r3] of scatter(-2400, -1800, -1300, 900, 28, 31, (x, z) => (riverDist(x, z) < 70 && riverDist(x, z) > 22 ? 0.35 : 0))) {
    if (hf.sample(x, z) < 0.5 || blocked(x, z, 0.5)) continue;
    push(V.palm, x, z, r3, 0.9 + r1 * 0.5);
  }
  // desert: saguaros, rocks, dead trees, scrub
  for (const [x, z, r1, r2, r3] of scatter(W.minX, W.minZ, W.maxX, W.maxZ, 30, 41, (x, z) => regionWeights(x, z).desert * 0.55)) {
    const h = hf.sample(x, z);
    if (h < 1 || blocked(x, z, 0.8)) continue;
    const sl = slopeAt(hf, x, z);
    if (r1 < 0.34) { if (sl < 0.5) push(V.cactus, x, z, r3, 0.7 + r2 * 0.7, 0.2); }
    else if (r1 < 0.62) push(V.rock, x, z, r3, 0.6 + r2 * 1.8, 0.4);
    else if (r1 < 0.68) { if (sl < 0.5) push(V.deadtree, x, z, r3, 0.8 + r2 * 0.5); }
    else if (sl < 0.6) push(V.bush, x, z, r3, 0.5 + r2 * 0.6, 0.25);
  }
  // boulders in the mountains
  for (const [x, z, r1, r2, r3] of scatter(W.minX, W.minZ, W.maxX, W.maxZ, 60, 51, (x, z) => regionWeights(x, z).mountain * 0.4)) {
    if (hf.sample(x, z) < 2 || blocked(x, z, 1)) continue;
    push(V.rock, x, z, r3, 0.8 + r2 * 2.2, 0.5);
  }
  for (const k in V) V[k] = new Float32Array(V[k]);
}
