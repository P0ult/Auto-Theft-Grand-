// Deterministic layout of the city of Los Soles: road grid, districts, blocks, lots, buildings,
// terrain height, water, sidewalk & lane graphs and story landmarks. Pure data (no rendering).
import { RNG, clamp, smoothstep, fbm, lerp } from '../core/utils.js';
import { WORLD as WORLD_BOUNDS, Heightfield, landHeight, TOWNS, BASE, AIRFIELD, LAKE, cityDist, regionWeights, riverDist } from './worldgen.js';
import { buildRoadNetwork, REMOVED_SEGMENTS, SUPERBLOCKS, CITY_ROUNDABOUTS, ROUTES } from './roadlayout.js';
import { shapeTerrain } from './roadnet.js';
import { populateCountryside } from './countryside.js';

export const ROAD_W = 20;
export const HALF_ROAD = ROAD_W / 2;
export const SIDEWALK_W = 4;
export const CURB_H = 0.15;
export const LANES = [1.9, 5.4];
export const PARK_OFF = 8.5;
export const WATER_Y = -0.6;

// Road centerlines (non-uniform spacing for a more organic grid)
export const XS = [-830, -730, -630, -535, -440, -345, -255, -165, -75, 15, 105, 195, 285, 380, 480, 580, 680, 775, 870];
export const ZS = [-770, -665, -560, -455, -350, -245, -140, -40, 60, 160, 260, 360, 450, 540, 625];

export const CITY = {
  minX: XS[0] - HALF_ROAD, maxX: XS[XS.length - 1] + HALF_ROAD,
  minZ: ZS[0] - HALF_ROAD, maxZ: ZS[ZS.length - 1] + HALF_ROAD,
};
export const WORLD = WORLD_BOUNDS;
const cellPhase = (i, j) => ((i * 7 + j * 13) % 17) * 2.0; // mirrors shaders.intersectionPhase

export const DISTRICTS = {
  hills: { name: 'Vistawood Hills', color: '#6c8f4e' },
  docks: { name: 'Port Morena', color: '#7d7f86' },
  beach: { name: 'Santa Luz Beach', color: '#d8c38f' },
  hood: { name: 'Cedar Row', color: '#a58a66' },
  downtown: { name: 'Downtown', color: '#8a93a8' },
  corona: { name: 'El Corona', color: '#b88d6a' },
  westside: { name: 'Rosewood', color: '#9c8f86' },
  midtown: { name: 'Market District', color: '#9a9488' },
  country: { name: 'Verde County', color: '#8f9a5a' },
  forest: { name: 'Pinewood Forest', color: '#4f6b3a' },
  desert: { name: 'Tierra Seca Desert', color: '#c9ad7a' },
  base: { name: 'Fort Carver', color: '#8a8d73' },
};

function districtFor(cx, cz) {
  if (cz < -560) return 'hills';
  if (cx > 480 && cz > -40) return 'docks';
  if (cz > 450 && cx > -300) return 'beach';
  if (cx < -255 && cz > 60) return 'hood';
  if (cx >= -75 && cx <= 380 && cz >= -560 && cz <= -140) return 'downtown';
  if (cx > 380) return 'corona';
  if (cx < -255) return 'westside';
  return 'midtown';
}

// Special blocks (by block indices i,j) used by the story & world.
// Block (i,j) spans XS[i]..XS[i+1], ZS[j]..ZS[j+1].
export const SPECIAL_BLOCKS = {
  '2,10': 'home',         // Castillo family house, Cedar Row
  '3,11': 'court',        // basketball court / park in Cedar Row
  '1,9': 'burger',        // Big Bun Burgers
  '8,6': 'gunshop',       // Gun Barn (Market)
  '5,4': 'spray',         // Spray Shack (Rosewood)
  '12,7': 'spray2',       // Spray Shack (Corona edge)
  '9,4': 'hospital',      // All Saints General
  '10,7': 'police',       // LSPD Central
  '11,3': 'tower',        // Deacon's tower (Downtown)
  '10,4': 'plaza',        // Pershing Plaza
  '5,6': 'park',          // Glen Park (large)
  '6,6': 'park',
  '14,5': 'vipers',       // Vipers HQ (El Corona)
  '16,10': 'warehouse',   // docks warehouse for missions
  '9,0': 'mansion_boss',  // Vistawood mansion
  '7,12': 'pierfront',    // beach front near pier
  '13,2': 'garage',       // Hot-wheels garage
  '0,12': 'projects',
  '4,12': 'liquor',
};

const RB_SET = new Set(CITY_ROUNDABOUTS.map(([i, j]) => i + ',' + j));

export class CityMap {
  constructor(seed = 1337) {
    this.seed = seed;
    this.blocks = [];
    this.buildings = [];   // {x0,z0,x1,z1,y0,y1,style,tint,seed,roof,district,kind}
    this.lotSurfaces = []; // extra ground surfaces on top of blocks: {x0,z0,x1,z1,type}
    this.props = [];       // {type,x,z,rot,scale,...}
    this.fences = [];      // thin walls {x0,z0,x1,z1,h,type}
    this.parkingSpots = []; // {x,z,rot,district}
    this.landmarks = {};
    this.pools = [];
    this.containers = [];
    this.build();
  }

  blockAt(x, z) {
    const i = this._idx(XS, x), j = this._idx(ZS, z);
    if (i < 0 || j < 0) return null;
    return this.cellBlocks[j * (XS.length - 1) + i] || null;
  }
  // a walkable area (city block or town) with sidewalk graph nodes
  walkAreaAt(x, z) {
    const b = this.blockAt(x, z);
    if (b) return b;
    if (this.townAreas) for (const t of this.townAreas) if (Math.hypot(x - t.x, z - t.z) < t.r && t.nodeIds.length) return t;
    return null;
  }
  getBlock(i, j) { if (i < 0 || j < 0 || i >= XS.length - 1 || j >= ZS.length - 1) return null; return this.cellBlocks[j * (XS.length - 1) + i] || null; }
  _idx(arr, v) {
    if (v < arr[0] || v > arr[arr.length - 1]) return -1;
    let lo = 0, hi = arr.length - 1;
    while (hi - lo > 1) { const m = (lo + hi) >> 1; if (arr[m] <= v) lo = m; else hi = m; }
    return lo;
  }

  // Nearest road centerline distances
  nearestX(x) { let best = 1e9, bi = 0; for (let i = 0; i < XS.length; i++) { const d = Math.abs(x - XS[i]); if (d < best) { best = d; bi = i; } } return bi; }
  nearestZ(z) { let best = 1e9, bi = 0; for (let j = 0; j < ZS.length; j++) { const d = Math.abs(z - ZS[j]); if (d < best) { best = d; bi = j; } } return bi; }

  // any paved road: city streets or the regional network
  isOnRoad(x, z) {
    if (this.isOnCityStreet(x, z)) return true;
    return !!(this.roads && this.roads.onRoad(x, z));
  }
  isOnCityStreet(x, z, margin = 0) {
    if (x < CITY.minX - 1 || x > CITY.maxX + 1 || z < CITY.minZ - 1 || z > CITY.maxZ + 1) return false;
    const i = this.nearestX(x), j = this.nearestZ(z);
    const dx = Math.abs(x - XS[i]), dz = Math.abs(z - ZS[j]);
    if (dx > HALF_ROAD + margin && dz > HALF_ROAD + margin) return false;
    // inside a merged super-block (the removed street is built over)
    const b = this.blockAt(x, z);
    if (b && b.merged && x > b.x0 && x < b.x1 && z > b.z0 && z < b.z1) return false;
    return true;
  }
  isOnRoadNet(x, z, margin = 0, except = null) {
    if (!this.roads) return false;
    const h = this.roads.onRoad(x, z, margin);
    return !!(h && h.e !== except);
  }

  districtAt(x, z) {
    const b = this.blockAt(x, z);
    if (b) return b.district;
    if (cityDist(x, z) > 700) {
      if (x > BASE.minX - 100 && x < BASE.maxX + 100 && z > BASE.minZ - 100 && z < BASE.maxZ + 100) return 'base';
      const w = regionWeights(x, z);
      return w.desert > 0.5 ? 'desert' : w.mountain > 0.5 ? 'forest' : 'country';
    }
    if (z > CITY.maxZ && x < 480) return 'beach';
    if (x > CITY.maxX && z > -250) return 'docks';
    if (z < CITY.minZ) return 'hills';
    const i = clamp(this._idx(XS, clamp(x, XS[0] + 1, XS[XS.length - 1] - 1)), 0, XS.length - 2);
    const j = clamp(this._idx(ZS, clamp(z, ZS[0] + 1, ZS[ZS.length - 1] - 1)), 0, ZS.length - 2);
    const bb = this.getBlock(i, j);
    return bb ? bb.district : 'midtown';
  }
  zoneName(x, z) {
    for (const t of Object.values(TOWNS)) if (Math.hypot(x - t.x, z - t.z) < t.r + 60) return t.name;
    if (x > BASE.minX && x < BASE.maxX && z > BASE.minZ && z < BASE.maxZ) return 'Fort Carver';
    if (Math.hypot(x - AIRFIELD.x, z - AIRFIELD.z) < AIRFIELD.len / 2 + 80) return AIRFIELD.name;
    if (Math.hypot(x - LAKE.x, z - LAKE.z) < LAKE.r + 120) return 'Lake Mirador';
    if (this.hf && this.hf.sample(x, z) < -0.5 && cityDist(x, z) > 0) {
      if (riverDist(x, z) < 60) return 'Rio Verde';
      return 'Pacific Ocean';
    }
    if (z > CITY.maxZ + 70 && x > 60 && x < 150 && z < 960) return 'Santa Luz Pier';
    const dC = cityDist(x, z);
    if (dC > 0 && dC < 700) {
      if (z < CITY.minZ - 60 && x > -700 && x < 700) return dC < 360 ? 'Vistawood Hills' : 'Mount Vista';
      if (x < CITY.minX - 60) return 'Red Canyon';
      if (x > CITY.maxX + 60 && z < -250) return 'Bayshore';
    }
    if (dC >= 700) {
      if (riverDist(x, z) < 90) return 'Rio Verde';
      if (Math.hypot(x + 900, z + 3250) < 700) return 'Mount Cedro';
      const w = regionWeights(x, z);
      if (w.desert > 0.5) return x < -4600 ? 'Bone Flats' : 'Tierra Seca Desert';
      if (w.mountain > 0.5) return x > 400 ? 'Bayshore' : 'Pinewood Forest';
      return 'Verde County';
    }
    return DISTRICTS[this.districtAt(x, z)].name;
  }

  // ---------------------------------------------------------------- terrain
  terrainHeight(x, z) { return this.hf ? this.hf.sample(x, z) : landHeight(x, z); }
  _oldTerrainHeight(x, z) {
    const eW = CITY.minX - 6, eE = CITY.maxX + 6, eN = CITY.minZ - 6, eS = CITY.maxZ + 6;
    let h = 0;
    // hills north
    if (z < eN) {
      const d = eN - z;
      h = Math.max(h, 5 * smoothstep(0, 50, d) + Math.max(0, d - 30) * 0.32 + fbm(x * 0.004, z * 0.004, 5) * 90 * smoothstep(40, 260, d));
    }
    // hills west (fade out near the south coast)
    if (x < eW) {
      const d = eW - x;
      const fade = 1 - smoothstep(420, 620, z);
      h = Math.max(h, (5 * smoothstep(0, 50, d) + Math.max(0, d - 30) * 0.3 + fbm(x * 0.004 + 7, z * 0.004, 5) * 80 * smoothstep(40, 260, d)) * fade);
    }
    // hills east (north part only)
    if (x > eE && z < -250) {
      const d = x - eE;
      const fade = smoothstep(-250, -420, z);
      h = Math.max(h, (5 * smoothstep(0, 50, d) + Math.max(0, d - 30) * 0.3 + fbm(x * 0.004, z * 0.004 + 3, 5) * 80 * smoothstep(40, 260, d)) * fade);
    }
    // south coast: beach west of the docks, concrete quay at the docks
    if (z > eS) {
      const d = z - eS;
      if (x < 470) {
        const beach = -d * 0.012 - Math.max(0, d - 70) * 0.05;
        h = z > eS + 2 ? Math.min(h, beach) : h;
        if (x < eW) h = Math.min(h, beach) + Math.max(0, (eW - x) * 0.0) ;
      } else {
        h = d < 34 ? 0 : -9;
      }
    }
    // east coast (docks)
    if (x > eE && z > -250) {
      const d = x - eE;
      if (d > 36) h = Math.min(h, -9);
    }
    // blend at the corner of beach and quay
    return h;
  }

  // Full ground height used by physics (terrain + curbs + pier)
  groundHeight(x, z) {
    // pier
    const P = this.landmarks.pier;
    if (P && x > P.x0 && x < P.x1 && z > P.z0 && z < P.z1) {
      const t = clamp((z - P.z0) / 24, 0, 1);
      return lerp(Math.max(this.terrainHeight(x, z), 0.1), P.y, t);
    }
    if (x >= CITY.minX && x <= CITY.maxX && z >= CITY.minZ && z <= CITY.maxZ) {
      const b = this.blockAt(x, z);
      if (b && x > b.x0 && x < b.x1 && z > b.z0 && z < b.z1) return CURB_H;
      return 0;
    }
    return this.hf.sample(x, z);
  }

  waterDepth(x, z) { return WATER_Y - this.groundHeight(x, z); }
  isWater(x, z) { return this.groundHeight(x, z) < WATER_Y - 0.3; }

  // ---------------------------------------------------------------- build
  build() {
    const nI = XS.length - 1, nJ = ZS.length - 1;
    // terrain first (roads follow it), with flat pads for the towns, the base and the airstrip
    this.hf = new Heightfield();
    const pads = [
      ...Object.entries(TOWNS).map(([k, t]) => ({ key: k, x: t.x, z: t.z, r: t.r, blend: 140 })),
      { key: 'base', minX: BASE.minX, maxX: BASE.maxX, minZ: BASE.minZ, maxZ: BASE.maxZ, blend: 160 },
      { key: 'air', minX: AIRFIELD.x - 60, maxX: AIRFIELD.x + 60, minZ: AIRFIELD.z - AIRFIELD.len / 2 - 20, maxZ: AIRFIELD.z + AIRFIELD.len / 2 + 20, blend: 80 },
    ];
    this.hf.generate(landHeight, 3, pads);
    // valleys & passes for the roads, then re-flatten the pads
    this.hf.carveRoutes(ROUTES, (key) => pads.find((p) => p.key === key)?.y ?? null);
    for (const p of pads) this.hf.pad(p);
    const built = buildRoadNetwork({ XS, ZS, intersectionPhase: cellPhase }, this.hf);
    this.roads = built.net;
    this.roadInfo = built.info;
    this.cellBlocks = [];
    for (let j = 0; j < nJ; j++) {
      for (let i = 0; i < nI; i++) {
        const x0 = XS[i] + HALF_ROAD, x1 = XS[i + 1] - HALF_ROAD;
        const z0 = ZS[j] + HALF_ROAD, z1 = ZS[j + 1] - HALF_ROAD;
        const cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
        const district = districtFor(cx, cz);
        const special = SPECIAL_BLOCKS[`${i},${j}`] || null;
        const b = {
          i, j, x0, z0, x1, z1, cx, cz, district, special,
          ix0: x0 + SIDEWALK_W, iz0: z0 + SIDEWALK_W, ix1: x1 - SIDEWALK_W, iz1: z1 - SIDEWALK_W,
          ground: 'concrete', corners: [], seed: (i * 7919 + j * 104729 + this.seed) >>> 0,
        };
        b.corners = [
          { x: x0 + 2, z: z0 + 2 }, { x: x1 - 2, z: z0 + 2 }, { x: x1 - 2, z: z1 - 2 }, { x: x0 + 2, z: z1 - 2 },
        ];
        this.blocks.push(b);
        this.cellBlocks.push(b);
      }
    }
    // merge pairs of blocks across a removed street into super-blocks
    for (const sb of SUPERBLOCKS) {
      const cells = sb.cells.map(([i, j]) => this.getBlock(i, j));
      const m = cells[0];
      const x0 = Math.min(...cells.map((c) => c.x0)), x1 = Math.max(...cells.map((c) => c.x1));
      const z0 = Math.min(...cells.map((c) => c.z0)), z1 = Math.max(...cells.map((c) => c.z1));
      Object.assign(m, { x0, x1, z0, z1, cx: (x0 + x1) / 2, cz: (z0 + z1) / 2, ix0: x0 + SIDEWALK_W, iz0: z0 + SIDEWALK_W, ix1: x1 - SIDEWALK_W, iz1: z1 - SIDEWALK_W, merged: true, special: 'super', superKind: sb.kind, superName: sb.name });
      m.corners = [{ x: x0 + 2, z: z0 + 2 }, { x: x1 - 2, z: z0 + 2 }, { x: x1 - 2, z: z1 - 2 }, { x: x0 + 2, z: z1 - 2 }];
      for (const c of cells.slice(1)) {
        this.blocks.splice(this.blocks.indexOf(c), 1);
        this.cellBlocks[c.j * nI + c.i] = m;
      }
    }
    this.landmarks.pier = { x0: 92, x1: 120, z0: CITY.maxZ + 8, z1: 960, y: 2.6 };
    for (const b of this.blocks) this._fillBlock(b);
    this._clearUnderFreeway();
    this._streetProps();
    this._buildSidewalkGraph();
    // roads cut into the hills, sit on embankments or become bridges
    shapeTerrain(this.roads, this.hf, (x, z) => (cityDist(x, z) < 0.5 ? this._cityGround(x, z) : null));
    populateCountryside(this);
    this._buildLandmarks();
  }

  _cityGround(x, z) {
    const b = this.blockAt(x, z);
    if (b && x > b.x0 && x < b.x1 && z > b.z0 && z < b.z1) return CURB_H;
    return 0;
  }

  // Buildings, fences, lots, props and parking under the city freeway and its ramps are cleared away
  _clearUnderFreeway() {
    const net = this.roads;
    const hit = (x0, z0, x1, z1, pad = 1.5) => {
      const list = net.edgesIn(x0 - 30, z0 - 30, x1 + 30, z1 + 30);
      for (const e of list) {
        if (e.grid || (e.type !== 'freeway' && e.type !== 'ramp')) continue;
        for (let i = 0; i < e.n - 1; i++) {
          const ax = e.p[i * 3], az = e.p[i * 3 + 2], bx = e.p[i * 3 + 3], bz = e.p[i * 3 + 5];
          const w = Math.max(e.wL, e.wR) + pad;
          if (Math.max(ax, bx) + w < x0 || Math.min(ax, bx) - w > x1 || Math.max(az, bz) + w < z0 || Math.min(az, bz) - w > z1) continue;
          // distance from the rect to the segment (sample)
          for (let k = 0; k <= 4; k++) {
            const t = k / 4, px = ax + (bx - ax) * t, pz = az + (bz - az) * t;
            const dx = Math.max(x0 - px, 0, px - x1), dz = Math.max(z0 - pz, 0, pz - z1);
            if (Math.hypot(dx, dz) < w) return true;
          }
        }
      }
      return false;
    };
    const inCity = (x, z) => x > CITY.minX && x < CITY.maxX && z > CITY.minZ && z < CITY.maxZ;
    this.buildings = this.buildings.filter((b) => !(inCity(b.x0, b.z0) && hit(b.x0, b.z0, b.x1, b.z1)));
    this.fences = this.fences.filter((f) => !(inCity(f.x0, f.z0) && hit(Math.min(f.x0, f.x1), Math.min(f.z0, f.z1), Math.max(f.x0, f.x1), Math.max(f.z0, f.z1))));
    this.props = this.props.filter((p) => !(inCity(p.x, p.z) && p.type !== 'trafficlight' && hit(p.x - 0.5, p.z - 0.5, p.x + 0.5, p.z + 0.5, 1)));
    this.parkingSpots = this.parkingSpots.filter((p) => !(inCity(p.x, p.z) && !p.curb && hit(p.x - 2.5, p.z - 2.5, p.x + 2.5, p.z + 2.5, 0.5)));
    this.containers = this.containers.filter((c) => !hit(c.x - 3, c.z - 3, c.x + 3, c.z + 3));
    this.pools = this.pools.filter((pl) => !hit(pl.x0, pl.z0, pl.x1, pl.z1));
    // what's left under the viaduct is gravel / parking
    for (const l of this.lotSurfaces) if (inCity(l.x0, l.z0) && hit(l.x0, l.z0, l.x1, l.z1, 0)) l.type = l.type === 'grass' ? 'dirt' : l.type;
  }

  _addBuilding(b, x0, z0, x1, z1, height, style, opts = {}) {
    if (x1 - x0 < 2 || z1 - z0 < 2) return null;
    const bld = {
      x0, z0, x1, z1, y0: opts.y0 ?? CURB_H, y1: (opts.y0 ?? CURB_H) + height, style, rot: opts.rot || 0, base: opts.base ?? null,
      tint: opts.tint ?? [1, 1, 1], seed: opts.seed ?? Math.random(), roof: opts.roof || 'flat',
      district: b ? b.district : 'midtown', kind: opts.kind || 'building', floorH: opts.floorH || (style === 4 ? 6 : 3.4),
      cell: opts.cell || 3.2, name: opts.name, sign: opts.sign, noCollide: opts.noCollide,
    };
    this.buildings.push(bld);
    return bld;
  }

  _tower(b, rng, x0, z0, x1, z1, H, style, tint) {
    const seed = rng.next();
    const podiumH = rng.range(10, 18);
    const w = x1 - x0, d = z1 - z0;
    if (H < 35 || rng.chance(0.25)) {
      this._addBuilding(b, x0, z0, x1, z1, H, style, { tint, seed, roof: rng.chance(0.3) ? 'antenna' : 'flat' });
      return;
    }
    const podStyle = rng.chance(0.5) ? 5 : style;
    this._addBuilding(b, x0, z0, x1, z1, podiumH, podStyle, { tint, seed, roof: 'flat' });
    const inset = Math.min(w, d) * rng.range(0.1, 0.2);
    const tx0 = x0 + inset, tz0 = z0 + inset, tx1 = x1 - inset, tz1 = z1 - inset;
    const mainH = H - podiumH;
    if (rng.chance(0.5)) {
      const h1 = mainH * rng.range(0.55, 0.8);
      this._addBuilding(b, tx0, tz0, tx1, tz1, h1, style, { tint, seed, y0: CURB_H + podiumH, roof: 'flat' });
      const in2 = Math.min(tx1 - tx0, tz1 - tz0) * rng.range(0.12, 0.22);
      this._addBuilding(b, tx0 + in2, tz0 + in2, tx1 - in2, tz1 - in2, mainH - h1, style, { tint, seed, y0: CURB_H + podiumH + h1, roof: rng.chance(0.5) ? 'spire' : 'helipad' });
    } else {
      this._addBuilding(b, tx0, tz0, tx1, tz1, mainH, style, { tint, seed, y0: CURB_H + podiumH, roof: rng.chance(0.4) ? 'antenna' : rng.chance(0.5) ? 'helipad' : 'flat' });
    }
  }

  _perimeter(b, rng, opts) {
    // Buildings lining the block edges with a service yard in the middle.
    const { ix0, iz0, ix1, iz1 } = b;
    const depth = rng.range(opts.depth[0], opts.depth[1]);
    const segs = [];
    // north and south rows span full width
    const rows = [
      { x0: ix0, z0: iz0, x1: ix1, z1: iz0 + depth, along: 'x' },
      { x0: ix0, z0: iz1 - depth, x1: ix1, z1: iz1, along: 'x' },
      { x0: ix0, z0: iz0 + depth, x1: ix0 + depth, z1: iz1 - depth, along: 'z' },
      { x0: ix1 - depth, z0: iz0 + depth, x1: ix1, z1: iz1 - depth, along: 'z' },
    ];
    for (const r of rows) {
      const len = r.along === 'x' ? r.x1 - r.x0 : r.z1 - r.z0;
      let pos = 0;
      while (pos < len - 4) {
        let w = rng.range(opts.width[0], opts.width[1]);
        if (len - pos - w < opts.width[0]) w = len - pos;
        const gap = rng.chance(opts.gapChance || 0.15) ? rng.range(3, 6) : 0;
        const a = pos, c = Math.min(len, pos + w);
        const style = rng.pick(opts.styles);
        const floors = rng.int(opts.floors[0], opts.floors[1]);
        const fh = style === 5 ? 4.2 : 3.4;
        const h = floors * fh + (style === 5 ? 0.6 : 0);
        const tint = opts.tint(rng);
        if (r.along === 'x') segs.push([r.x0 + a, r.z0, r.x0 + c - gap, r.z1, h, style, tint]);
        else segs.push([r.x0, r.z0 + a, r.x1, r.z0 + c - gap, h, style, tint]);
        pos = c;
      }
    }
    for (const s of segs) this._addBuilding(b, s[0], s[1], s[2], s[3], s[4], s[5], { tint: s[6], seed: rng.next(), roof: rng.chance(0.35) ? 'ac' : 'flat', floorH: s[5] === 5 ? 4.2 : 3.4 });
    // yard
    this.lotSurfaces.push({ x0: ix0 + depth, z0: iz0 + depth, x1: ix1 - depth, z1: iz1 - depth, type: 'asphalt' });
    const yx = (ix0 + ix1) / 2, yz = (iz0 + iz1) / 2;
    for (let k = 0; k < 3; k++) {
      if (rng.chance(0.6)) this.parkingSpots.push({ x: yx + rng.range(-12, 12), z: yz + rng.range(-12, 12), rot: rng.pick([0, Math.PI / 2, Math.PI, -Math.PI / 2]), district: b.district, lot: true });
    }
    if (rng.chance(0.7)) this.props.push({ type: 'dumpster', x: ix0 + depth + 2.5, z: yz + rng.range(-8, 8), rot: Math.PI / 2 });
  }

  _houses(b, rng) {
    const { ix0, iz0, ix1, iz1 } = b;
    const mid = (iz0 + iz1) / 2;
    const pastel = () => rng.pick([[1, 0.93, 0.8], [0.95, 0.85, 0.75], [0.8, 0.88, 0.95], [0.95, 0.8, 0.8], [0.85, 0.95, 0.82], [1, 1, 0.95], [0.9, 0.82, 0.95], [0.98, 0.9, 0.7]]);
    for (const side of [0, 1]) {
      let x = ix0;
      while (x < ix1 - 10) {
        let w = rng.range(15, 19);
        if (ix1 - x - w < 12) w = ix1 - x;
        const lz0 = side === 0 ? iz0 : mid, lz1 = side === 0 ? mid : iz1;
        const front = side === 0 ? lz0 : lz1; // facing street
        const hw = Math.min(w - 5, rng.range(9, 12));
        const hd = rng.range(8, 11);
        const hx0 = x + 1.5, hx1 = hx0 + hw;
        const setback = rng.range(5, 8);
        const hz0 = side === 0 ? front + setback : front - setback - hd;
        const hz1 = hz0 + hd;
        const floors = rng.chance(0.3) ? 2 : 1;
        const h = floors * 3.1 + 0.4;
        this._addBuilding(b, hx0, hz0, hx1, hz1, h, 3, { tint: pastel(), seed: rng.next(), roof: 'gable', floorH: 3.1, cell: 3.0, kind: 'house' });
        // yard grass + driveway
        this.lotSurfaces.push({ x0: x, z0: lz0, x1: x + w, z1: lz1, type: rng.chance(0.25) ? 'dirt' : 'grass' });
        const dx0 = hx1 + 0.5, dx1 = Math.min(x + w - 0.5, dx0 + 3.6);
        if (dx1 - dx0 > 2.6) {
          this.lotSurfaces.push({ x0: dx0, z0: side === 0 ? lz0 : lz1 - setback - hd, x1: dx1, z1: side === 0 ? lz0 + setback + hd : lz1, type: 'concrete' });
          if (rng.chance(0.55)) this.parkingSpots.push({ x: (dx0 + dx1) / 2, z: side === 0 ? lz0 + setback * 0.6 + 2 : lz1 - setback * 0.6 - 2, rot: side === 0 ? Math.PI : 0, district: b.district, driveway: true });
        }
        // fences between lots and at back
        const fz0 = lz0, fz1 = lz1;
        if (rng.chance(0.8)) this.fences.push({ x0: x + w - 0.05, z0: side === 0 ? fz0 + 2 : fz0, x1: x + w + 0.05, z1: side === 0 ? fz1 : fz1 - 2, h: 1.5, type: rng.chance(0.5) ? 'chain' : 'wood' });
        if (rng.chance(0.5)) this.fences.push({ x0: x + 0.5, z0: side === 0 ? front + 0.3 : front - 0.4, x1: hx0 + hw * 0.5, z1: side === 0 ? front + 0.4 : front - 0.3, h: 1.0, type: 'wood' });
        if (rng.chance(0.4)) this.props.push({ type: rng.chance(0.6) ? 'palm' : 'tree', x: x + rng.range(2, w - 2), z: side === 0 ? front + 2.5 : front - 2.5, rot: rng.range(0, 6.28), scale: rng.range(0.8, 1.2) });
        if (rng.chance(0.3)) this.props.push({ type: 'trashcan', x: dx0 + 1, z: side === 0 ? front + 1 : front - 1, rot: 0 });
        x += w;
      }
    }
    // back fence
    this.fences.push({ x0: ix0, z0: mid - 0.05, x1: ix1, z1: mid + 0.05, h: 1.8, type: 'wood' });
  }

  _fillBlock(b) {
    const rng = new RNG(b.seed);
    const { ix0, iz0, ix1, iz1, district } = b;
    const W = ix1 - ix0, D = iz1 - iz0;
    const sp = b.special;
    const warm = () => rng.pick([[1, 0.95, 0.88], [0.93, 0.9, 0.86], [0.85, 0.8, 0.75], [1, 0.88, 0.75], [0.8, 0.82, 0.86], [0.95, 0.95, 0.95], [0.95, 0.75, 0.6], [0.75, 0.85, 0.95], [0.85, 0.95, 0.85], [1, 0.82, 0.82], [0.9, 0.8, 0.6], [0.7, 0.72, 0.78]]);

    if (sp === 'super') { this._superBlock(b, rng); return; }
    if (sp === 'park' || sp === 'court' || sp === 'plaza') {
      b.ground = sp === 'plaza' ? 'plaza' : 'grass';
      b.park = true;
      this._park(b, rng, sp);
      return;
    }
    if (sp === 'home') {
      b.ground = 'grass';
      this._houses(b, rng);
      // Castillo house is the first house on the south side
      const home = this.buildings.filter((x) => x.kind === 'house' && x.z0 > (iz0 + iz1) / 2 && x.x0 < ix0 + 20 && x.district === 'hood').pop();
      if (home) { home.name = 'Castillo House'; home.tint = [0.85, 0.95, 0.8]; this.landmarks.home = { x: (home.x0 + home.x1) / 2, z: home.z1 + 3.5, door: { x: (home.x0 + home.x1) / 2, z: home.z1 + 0.6 } }; }
      return;
    }
    if (sp === 'hospital' || sp === 'police' || sp === 'gunshop' || sp === 'spray' || sp === 'spray2' || sp === 'burger' || sp === 'tower' || sp === 'vipers' || sp === 'warehouse' || sp === 'mansion_boss' || sp === 'garage' || sp === 'liquor' || sp === 'projects' || sp === 'pierfront') {
      this._specialBlock(b, rng, sp);
      return;
    }

    switch (district) {
      case 'downtown': {
        const r = rng.next();
        const distC = Math.hypot(b.cx - 150, b.cz + 350);
        const hmax = 60 + 180 * clamp(1 - distC / 320, 0, 1);
        if (r < 0.12) { b.ground = 'plaza'; this._park(b, rng, 'plaza'); return; }
        const style = () => rng.weighted([[1, 5], [0, 3], [6, 2]]);
        const glassTint = () => rng.pick([[0.7, 0.85, 1.0], [0.6, 0.9, 0.85], [1.0, 0.85, 0.65], [0.8, 0.8, 0.85], [0.55, 0.7, 0.95], [0.9, 0.95, 1.0]]);
        if (r < 0.35) {
          { const st = style(); this._tower(b, rng, ix0 + 2, iz0 + 2, ix1 - 2, iz1 - 2, rng.range(hmax * 0.5, hmax), st, st === 1 ? glassTint() : warm()); }
        } else {
          const mx = (ix0 + ix1) / 2 + rng.range(-8, 8), mz = (iz0 + iz1) / 2 + rng.range(-8, 8);
          const lots = [[ix0, iz0, mx - 2, mz - 2], [mx + 2, iz0, ix1, mz - 2], [ix0, mz + 2, mx - 2, iz1], [mx + 2, mz + 2, ix1, iz1]];
          for (const L of lots) {
            if (rng.chance(0.12)) { this.lotSurfaces.push({ x0: L[0], z0: L[1], x1: L[2], z1: L[3], type: 'plaza' }); this.props.push({ type: 'tree', x: (L[0] + L[2]) / 2, z: (L[1] + L[3]) / 2, scale: 1.2 }); continue; }
            { const st = style(); this._tower(b, rng, L[0] + 1, L[1] + 1, L[2] - 1, L[3] - 1, rng.range(hmax * 0.3, hmax), st, st === 1 ? glassTint() : warm()); }
          }
          this.lotSurfaces.push({ x0: mx - 2, z0: iz0, x1: mx + 2, z1: iz1, type: 'asphalt' });
          this.lotSurfaces.push({ x0: ix0, z0: mz - 2, x1: ix1, z1: mz + 2, type: 'asphalt' });
        }
        break;
      }
      case 'midtown':
        this._perimeter(b, rng, { depth: [16, 24], width: [12, 26], floors: [2, 9], styles: [0, 2, 5, 5, 6, 2], tint: warm, gapChance: 0.2 });
        if (rng.chance(0.25)) { const bx = (ix0 + ix1) / 2, bz = (iz0 + iz1) / 2; this._addBuilding(b, bx - 10, bz - 8, bx + 10, bz + 8, rng.range(30, 60), rng.pick([0, 1, 2]), { tint: warm(), seed: rng.next(), roof: 'ac' }); }
        break;
      case 'westside':
        if (rng.chance(0.45)) this._houses(b, rng);
        else this._perimeter(b, rng, { depth: [14, 20], width: [14, 24], floors: [2, 5], styles: [2, 3, 3, 5], tint: warm, gapChance: 0.3 });
        break;
      case 'hood':
        b.ground = 'grass';
        if (rng.chance(0.82)) this._houses(b, rng);
        else this._perimeter(b, rng, { depth: [14, 18], width: [18, 30], floors: [2, 4], styles: [2, 3, 5], tint: warm, gapChance: 0.35 });
        break;
      case 'corona':
        if (rng.chance(0.4)) { b.ground = 'dirt'; this._houses(b, rng); }
        else this._perimeter(b, rng, { depth: [14, 20], width: [10, 20], floors: [1, 3], styles: [3, 5, 5, 3], tint: () => rng.pick([[1, 0.85, 0.7], [0.95, 0.75, 0.65], [0.9, 0.95, 0.75], [0.8, 0.9, 1], [1, 0.95, 0.6], [0.95, 0.8, 0.9]]), gapChance: 0.3 });
        break;
      case 'beach': {
        const nearSand = b.j === ZS.length - 2;
        if (nearSand && rng.chance(0.6)) {
          this._tower(b, rng, ix0 + 4, iz0 + 6, ix1 - 4, iz1 - 10, rng.range(25, 70), rng.pick([7, 7, 3, 1]), [1, 0.97, 0.9]);
          this.lotSurfaces.push({ x0: ix0, z0: iz1 - 10, x1: ix1, z1: iz1, type: 'plaza' });
          for (let x = ix0 + 4; x < ix1; x += 9) this.props.push({ type: 'palm', x, z: iz1 - 4, scale: rng.range(1.1, 1.4) });
        } else {
          this._perimeter(b, rng, { depth: [14, 20], width: [12, 22], floors: [1, 4], styles: [5, 3, 7, 5], tint: () => rng.pick([[1, 0.95, 0.85], [0.9, 0.97, 1], [1, 0.9, 0.9], [0.95, 1, 0.9]]), gapChance: 0.25 });
        }
        break;
      }
      case 'docks': {
        b.ground = 'asphalt';
        const n = rng.chance(0.5) ? 1 : 2;
        if (n === 1) {
          this._addBuilding(b, ix0 + 4, iz0 + 4, ix1 - 4, iz0 + D * 0.55, rng.range(9, 15), 4, { tint: rng.pick([[0.75, 0.8, 0.85], [0.85, 0.7, 0.6], [0.7, 0.75, 0.7], [0.9, 0.88, 0.8]]), seed: rng.next(), roof: 'flat', floorH: 6, cell: 5, kind: 'warehouse' });
        } else {
          const mx = (ix0 + ix1) / 2;
          this._addBuilding(b, ix0 + 3, iz0 + 4, mx - 3, iz0 + D * 0.6, rng.range(8, 13), 4, { tint: [0.8, 0.8, 0.82], seed: rng.next(), floorH: 6, cell: 5, kind: 'warehouse' });
          this._addBuilding(b, mx + 3, iz0 + 4, ix1 - 3, iz0 + D * 0.6, rng.range(8, 13), 4, { tint: [0.72, 0.62, 0.55], seed: rng.next(), floorH: 6, cell: 5, kind: 'warehouse' });
        }
        // container stacks
        const cz = iz0 + D * 0.72;
        for (let x = ix0 + 4; x < ix1 - 8; x += 7) {
          if (rng.chance(0.3)) continue;
          const stack = rng.int(1, 3);
          for (let s = 0; s < stack; s++) this.containers.push({ x: x + 1.2, z: cz + rng.range(-1, 6), rot: Math.PI / 2 + rng.range(-0.04, 0.04), level: s, color: rng.int(0, 5) });
        }
        this.fences.push({ x0: ix0, z0: iz1 - 0.05, x1: ix1 - 10, z1: iz1 + 0.05, h: 2.4, type: 'chain' });
        for (let k = 0; k < 2; k++) this.parkingSpots.push({ x: ix0 + 10 + k * 8, z: iz1 - 6, rot: 0, district: 'docks', lot: true });
        break;
      }
      case 'hills': {
        b.ground = 'grass';
        this._mansion(b, rng);
        break;
      }
      default:
        this._perimeter(b, rng, { depth: [14, 20], width: [12, 24], floors: [2, 6], styles: [0, 2, 5], tint: warm });
    }
  }

  // merged blocks: stadium, mall, country club, big park
  _superBlock(b, rng) {
    const { ix0, iz0, ix1, iz1 } = b;
    const cx = (ix0 + ix1) / 2, cz = (iz0 + iz1) / 2;
    const W = ix1 - ix0, D = iz1 - iz0;
    this.landmarks[b.superKind] = { x: cx, z: cz, name: b.superName };
    switch (b.superKind) {
      case 'stadium': {
        b.ground = 'asphalt';
        this.lotSurfaces.push({ x0: ix0, z0: iz0, x1: ix1, z1: iz1, type: 'asphalt' });
        // oval bowl of rotated stand sections around a pitch
        const ra = Math.min(W, D) * 0.42, rb = Math.max(W, D) * 0.4;
        const alongZ = D > W;
        const n = 22;
        for (let k = 0; k < n; k++) {
          const a0 = k / n * Math.PI * 2, a1 = (k + 1) / n * Math.PI * 2, am = (a0 + a1) / 2;
          const ex = alongZ ? ra : rb, ez = alongZ ? rb : ra;
          const p0 = [cx + Math.cos(a0) * ex, cz + Math.sin(a0) * ez], p1 = [cx + Math.cos(a1) * ex, cz + Math.sin(a1) * ez];
          const len = Math.hypot(p1[0] - p0[0], p1[1] - p0[1]) + 0.6;
          const mx = (p0[0] + p1[0]) / 2, mz = (p0[1] + p1[1]) / 2;
          const yaw = Math.atan2(p1[0] - p0[0], p1[1] - p0[1]);
          const dep = 9;
          const nx = Math.cos(am), nz = Math.sin(am);
          const ccx = mx + nx * dep * 0.1, ccz = mz + nz * dep * 0.1;
          this._addBuilding(b, ccx - dep / 2, ccz - len / 2, ccx + dep / 2, ccz + len / 2, 16 + (k % 2) * 0.01, 6, { rot: yaw, tint: [0.92, 0.9, 0.86], seed: 0.66 + k * 0.001, roof: 'flat', kind: 'stand', name: k === 0 ? b.superName : undefined });
        }
        const px = alongZ ? ra * 0.72 : rb * 0.7, pz = alongZ ? rb * 0.7 : ra * 0.72;
        this.lotSurfaces.push({ x0: cx - px, z0: cz - pz, x1: cx + px, z1: cz + pz, type: 'grass' });
        for (let k = 0; k < 6; k++) this.parkingSpots.push({ x: ix0 + 8 + k * 7, z: iz0 + 6, rot: 0, district: b.district, lot: true });
        for (const [x, z] of [[ix0 + 4, iz0 + 4], [ix1 - 4, iz0 + 4], [ix0 + 4, iz1 - 4], [ix1 - 4, iz1 - 4]]) this.props.push({ type: 'streetlight', x, z, rot: 0 });
        break;
      }
      case 'mall': {
        b.ground = 'asphalt';
        this.lotSurfaces.push({ x0: ix0, z0: iz0, x1: ix1, z1: iz1, type: 'asphalt' });
        this._addBuilding(b, ix0 + 12, iz0 + 8, ix1 - 12, iz0 + D * 0.55, 13, 5, { tint: [0.95, 0.9, 0.82], seed: 0.71, roof: 'ac', floorH: 6.5, name: b.superName, sign: 'MALL' });
        this._addBuilding(b, cx - 14, iz0 + D * 0.55 - 1, cx + 14, iz0 + D * 0.55 + 7, 8, 1, { tint: [0.7, 0.85, 1.0], seed: 0.72, roof: 'flat' });
        for (let x = ix0 + 8; x < ix1 - 8; x += 6) for (const z of [iz1 - 8, iz1 - 22]) if (rng.chance(0.45)) this.parkingSpots.push({ x, z, rot: Math.PI / 2 * (rng.chance(0.5) ? 1 : -1), district: b.district, lot: true });
        for (let x = ix0 + 10; x < ix1; x += 26) this.props.push({ type: 'streetlight', x, z: iz1 - 15, rot: 0 });
        break;
      }
      case 'golf': {
        b.ground = 'grass';
        this._addBuilding(b, cx - 14, iz0 + 6, cx + 14, iz0 + 20, 7, 7, { tint: [1, 0.97, 0.9], seed: 0.73, roof: 'gable', floorH: 3.5, kind: 'house', name: b.superName });
        this.lotSurfaces.push({ x0: cx - 18, z0: iz0 + 20, x1: cx + 18, z1: iz0 + 34, type: 'asphalt' });
        for (let k = 0; k < 4; k++) this.parkingSpots.push({ x: cx - 12 + k * 7, z: iz0 + 27, rot: Math.PI / 2, district: 'hills', lot: true, fancy: true });
        for (let k = 0; k < 40; k++) {
          const x = rng.range(ix0 + 4, ix1 - 4), z = rng.range(iz0 + 40, iz1 - 4);
          if (rng.chance(0.4)) this.props.push({ type: rng.chance(0.5) ? 'palm' : 'tree', x, z, rot: rng.range(0, 6.28), scale: rng.range(1, 1.4) });
        }
        this.lotSurfaces.push({ x0: cx - 30, z0: iz1 - 40, x1: cx + 10, z1: iz1 - 25, type: 'sand' });
        break;
      }
      default: {
        b.ground = 'grass'; b.park = true;
        this._park(b, rng, 'park');
      }
    }
  }

  _mansion(b, rng, boss = false) {
    const { ix0, iz0, ix1, iz1 } = b;
    const cx = (ix0 + ix1) / 2 + rng.range(-6, 6), cz = (iz0 + iz1) / 2 + rng.range(-4, 4);
    const w = boss ? 34 : rng.range(22, 30), d = boss ? 22 : rng.range(14, 18);
    const tint = rng.pick([[1, 1, 0.97], [0.97, 0.93, 0.85], [0.92, 0.92, 0.9]]);
    this._addBuilding(b, cx - w / 2, cz - d / 2, cx + w / 2, cz + d / 2, boss ? 10 : 7.5, 7, { tint, seed: rng.next(), roof: 'flat', floorH: 3.7, cell: 3.6, kind: 'mansion', name: boss ? 'Salazar Estate' : undefined });
    this._addBuilding(b, cx + w / 2, cz - d / 2 + 3, cx + w / 2 + 10, cz + d / 2 - 3, 4, 7, { tint, seed: rng.next(), roof: 'flat', floorH: 3.7, cell: 3.6, kind: 'mansion' });
    // pool
    const pz = cz + d / 2 + 5;
    const pool = { x0: cx - 8, z0: pz, x1: cx + 6, z1: pz + 7 };
    this.pools.push(pool);
    this.lotSurfaces.push({ x0: pool.x0 - 2, z0: pool.z0 - 2, x1: pool.x1 + 2, z1: pool.z1 + 2, type: 'plaza' });
    this.lotSurfaces.push({ x0: cx - w / 2 - 12, z0: iz0, x1: cx - w / 2 - 7, z1: cz, type: 'concrete' });
    this.parkingSpots.push({ x: cx - w / 2 - 9.5, z: cz - 6, rot: Math.PI, district: 'hills', driveway: true, fancy: true });
    for (let k = 0; k < 8; k++) this.props.push({ type: 'palm', x: rng.range(ix0 + 3, ix1 - 3), z: rng.chance(0.5) ? iz0 + rng.range(2, 8) : iz1 - rng.range(2, 8), scale: rng.range(1.1, 1.5) });
    // hedge walls along the perimeter with a gate gap
    const hH = 2.2;
    this.fences.push({ x0: ix0, z0: iz1 - 0.6, x1: ix1, z1: iz1, h: hH, type: 'hedge' });
    this.fences.push({ x0: ix0, z0: iz0, x1: cx - w / 2 - 13, z1: iz0 + 0.6, h: hH, type: 'hedge' });
    this.fences.push({ x0: cx - w / 2 - 6, z0: iz0, x1: ix1, z1: iz0 + 0.6, h: hH, type: 'hedge' });
    this.fences.push({ x0: ix0, z0: iz0, x1: ix0 + 0.6, z1: iz1, h: hH, type: 'hedge' });
    this.fences.push({ x0: ix1 - 0.6, z0: iz0, x1: ix1, z1: iz1, h: hH, type: 'hedge' });
    return { cx, cz, w, d };
  }

  _park(b, rng, kind) {
    const { ix0, iz0, ix1, iz1 } = b;
    const cx = (ix0 + ix1) / 2, cz = (iz0 + iz1) / 2;
    if (kind === 'plaza') {
      this.lotSurfaces.push({ x0: ix0, z0: iz0, x1: ix1, z1: iz1, type: 'plaza' });
      this.props.push({ type: 'fountain', x: cx, z: cz });
      for (let a = 0; a < 8; a++) { const ang = a / 8 * Math.PI * 2; this.props.push({ type: 'tree', x: cx + Math.cos(ang) * 22, z: cz + Math.sin(ang) * 22, scale: 1.1 }); }
      for (let a = 0; a < 6; a++) { const ang = a / 6 * Math.PI * 2 + 0.3; this.props.push({ type: 'bench', x: cx + Math.cos(ang) * 12, z: cz + Math.sin(ang) * 12, rot: -ang + Math.PI / 2 }); }
      if (b.special === 'plaza') this.landmarks.plaza = { x: cx, z: cz };
      return;
    }
    if (kind === 'court') {
      this.lotSurfaces.push({ x0: cx - 15, z0: cz - 9, x1: cx + 15, z1: cz + 9, type: 'court' });
      this.props.push({ type: 'hoop', x: cx - 13, z: cz, rot: Math.PI / 2 });
      this.props.push({ type: 'hoop', x: cx + 13, z: cz, rot: -Math.PI / 2 });
      this.fences.push({ x0: cx - 16, z0: cz - 10, x1: cx + 16, z1: cz - 9.9, h: 3, type: 'chain' });
      this.fences.push({ x0: cx - 16, z0: cz + 9.9, x1: cx - 3, z1: cz + 10, h: 3, type: 'chain' });
      this.fences.push({ x0: cx + 3, z0: cz + 9.9, x1: cx + 16, z1: cz + 10, h: 3, type: 'chain' });
      this.landmarks.court = { x: cx, z: cz };
    }
    // paths
    this.lotSurfaces.push({ x0: cx - 2, z0: iz0, x1: cx + 2, z1: iz1, type: 'path' });
    this.lotSurfaces.push({ x0: ix0, z0: cz - 2, x1: ix1, z1: cz + 2, type: 'path' });
    const n = kind === 'park' ? 26 : 10;
    for (let k = 0; k < n; k++) {
      const x = rng.range(ix0 + 3, ix1 - 3), z = rng.range(iz0 + 3, iz1 - 3);
      if (Math.abs(x - cx) < 4 || Math.abs(z - cz) < 4) continue;
      if (kind === 'court' && Math.abs(x - cx) < 18 && Math.abs(z - cz) < 12) continue;
      this.props.push({ type: rng.chance(0.3) ? 'palm' : 'tree', x, z, rot: rng.range(0, 6.28), scale: rng.range(0.9, 1.5) });
    }
    for (let k = 0; k < 4; k++) this.props.push({ type: 'bench', x: cx + 3.2, z: iz0 + 10 + k * 15, rot: -Math.PI / 2 });
  }

  _specialBlock(b, rng, sp) {
    const { ix0, iz0, ix1, iz1 } = b;
    const cx = (ix0 + ix1) / 2, cz = (iz0 + iz1) / 2;
    const warm = [0.95, 0.92, 0.86];
    switch (sp) {
      case 'hospital': {
        this._addBuilding(b, ix0 + 4, iz0 + 4, ix1 - 4, iz0 + 36, 34, 0, { tint: [0.95, 0.96, 1], seed: 0.3, roof: 'helipad', name: 'All Saints General', sign: 'HOSPITAL' });
        this._addBuilding(b, ix0 + 4, iz0 + 36, ix0 + 30, iz1 - 16, 14, 0, { tint: [0.95, 0.96, 1], seed: 0.31, roof: 'ac' });
        this.lotSurfaces.push({ x0: ix0 + 30, z0: iz0 + 36, x1: ix1, z1: iz1, type: 'asphalt' });
        this.landmarks.hospital = { x: cx + 10, z: iz1 + 1, respawn: { x: cx + 10, z: iz1 + 1.5, rot: 0 } };
        break;
      }
      case 'police': {
        this._addBuilding(b, ix0 + 6, iz0 + 4, ix1 - 6, iz0 + 30, 16, 6, { tint: [0.85, 0.83, 0.78], seed: 0.4, roof: 'antenna', name: 'LSPD Central', sign: 'POLICE' });
        this.lotSurfaces.push({ x0: ix0, z0: iz0 + 30, x1: ix1, z1: iz1, type: 'asphalt' });
        this.fences.push({ x0: ix0, z0: iz1 - 0.1, x1: cx - 6, z1: iz1, h: 2.4, type: 'chain' });
        this.fences.push({ x0: cx + 6, z0: iz1 - 0.1, x1: ix1, z1: iz1, h: 2.4, type: 'chain' });
        for (let k = 0; k < 5; k++) this.parkingSpots.push({ x: ix0 + 10 + k * 8, z: iz0 + 40, rot: 0, district: b.district, police: true, lot: true });
        this.landmarks.police = { x: cx, z: iz0 - 1, respawn: { x: cx, z: iz0 - 2, rot: Math.PI } };
        break;
      }
      case 'gunshop': {
        this._perimeter(b, rng, { depth: [16, 20], width: [14, 22], floors: [2, 6], styles: [2, 5, 0], tint: () => warm });
        // replace first building name
        const shop = this._addBuilding(b, ix0 + 2, iz1 - 14, ix0 + 20, iz1 - 2, 6, 5, { tint: [0.6, 0.6, 0.55], seed: 0.77, roof: 'flat', name: 'Gun Barn', sign: 'GUN BARN', noCollide: false });
        this.buildings = this.buildings.filter((x) => x === shop || !(x.x0 < ix0 + 21 && x.x1 > ix0 + 1 && x.z1 > iz1 - 15 && x.z0 < iz1 - 1 && x.district === b.district && x.y0 < 1));
        this.landmarks.gunshop = { x: ix0 + 11, z: iz1 + 1.5 };
        break;
      }
      case 'spray':
      case 'spray2': {
        this._perimeter(b, rng, { depth: [14, 18], width: [14, 22], floors: [1, 3], styles: [3, 5], tint: () => warm });
        const x0 = ix0 + 22, x1 = ix0 + 36;
        this.buildings = this.buildings.filter((x) => !(x.x0 < x1 + 1 && x.x1 > x0 - 1 && x.z0 < iz0 + 20 && x.z1 > iz0 && x.y0 < 1));
        this._addBuilding(b, x0, iz0 + 6, x0 + 1, iz0 + 18, 6, 4, { tint: [0.9, 0.6, 0.3], seed: 0.5 });
        this._addBuilding(b, x1 - 1, iz0 + 6, x1, iz0 + 18, 6, 4, { tint: [0.9, 0.6, 0.3], seed: 0.5 });
        this._addBuilding(b, x0, iz0 + 17, x1, iz0 + 18, 6, 4, { tint: [0.9, 0.6, 0.3], seed: 0.5 });
        this._addBuilding(b, x0 - 0.3, iz0 + 5.5, x1 + 0.3, iz0 + 18.3, 1.2, 4, { tint: [0.9, 0.6, 0.3], seed: 0.5, y0: CURB_H + 5, name: 'Spray Shack', sign: 'SPRAY SHACK', noCollide: true });
        this.lotSurfaces.push({ x0, z0: iz0, x1, z1: iz0 + 18, type: 'concrete' });
        this.landmarks[sp] = { x: (x0 + x1) / 2, z: iz0 + 12, entry: { x: (x0 + x1) / 2, z: iz0 - 3 } };
        break;
      }
      case 'burger': {
        this._houses(b, rng);
        const bx0 = ix1 - 30, bz0 = iz0;
        this.buildings = this.buildings.filter((x) => !(x.x1 > bx0 - 2 && x.z0 < iz0 + 32 && x.district === b.district && x.x0 < ix1 && x.z1 > iz0 && x.z1 < (iz0 + iz1) / 2 + 1));
        this.fences = this.fences.filter((f) => !(f.x1 > bx0 - 2 && f.z0 < iz0 + 32 && f.x0 < ix1 && f.z1 > iz0 - 1));
        this.lotSurfaces.push({ x0: bx0 - 2, z0: iz0, x1: ix1, z1: iz0 + 32, type: 'asphalt' });
        this._addBuilding(b, bx0 + 4, iz0 + 8, ix1 - 4, iz0 + 22, 5, 5, { tint: [1, 0.85, 0.5], seed: 0.9, roof: 'flat', floorH: 5, name: 'Big Bun Burgers', sign: 'BIG BUN' });
        this.landmarks.burger = { x: bx0 + 13, z: iz0 + 5 };
        break;
      }
      case 'tower': {
        this._addBuilding(b, ix0 + 2, iz0 + 2, ix1 - 2, iz1 - 2, 18, 5, { tint: [0.9, 0.9, 0.95], seed: 0.12, roof: 'flat', floorH: 4.5 });
        this._addBuilding(b, ix0 + 12, iz0 + 12, ix1 - 12, iz1 - 12, 150, 1, { tint: [0.75, 0.85, 0.9], seed: 0.13, y0: CURB_H + 18, roof: 'flat', name: 'Deacon Tower' });
        this._addBuilding(b, ix0 + 20, iz0 + 20, ix1 - 20, iz1 - 20, 40, 1, { tint: [0.75, 0.85, 0.9], seed: 0.14, y0: CURB_H + 168, roof: 'helipad' });
        this.landmarks.tower = { x: cx, z: iz1 + 1, top: { x: cx, y: CURB_H + 208, z: cz } };
        break;
      }
      case 'vipers': {
        this._perimeter(b, rng, { depth: [16, 20], width: [14, 22], floors: [1, 3], styles: [3, 5], tint: () => [0.95, 0.75, 0.7] });
        this.landmarks.vipers = { x: cx, z: cz };
        break;
      }
      case 'warehouse': {
        b.ground = 'asphalt';
        this._addBuilding(b, ix0 + 6, iz0 + 6, ix1 - 6, iz0 + 36, 12, 4, { tint: [0.6, 0.65, 0.7], seed: 0.55, floorH: 6, cell: 5, kind: 'warehouse', name: 'Pier 9 Warehouse' });
        for (let x = ix0 + 6; x < ix1 - 8; x += 6.5) this.containers.push({ x, z: iz1 - 12, rot: Math.PI / 2, level: 0, color: (x | 0) % 6 });
        this.landmarks.warehouse = { x: cx, z: iz0 + 42 };
        break;
      }
      case 'mansion_boss': {
        b.ground = 'grass';
        const m = this._mansion(b, rng, true);
        this.landmarks.mansion = { x: m.cx, z: m.cz + m.d / 2 + 2, gate: { x: m.cx - m.w / 2 - 9.5, z: iz0 - 2 } };
        break;
      }
      case 'garage': {
        this._perimeter(b, rng, { depth: [14, 18], width: [14, 22], floors: [2, 5], styles: [2, 3], tint: () => warm });
        this.buildings = this.buildings.filter((x) => !(x.x0 < ix0 + 30 && x.x1 > ix0 && x.z1 > iz1 - 20 && x.y0 < 1));
        this._addBuilding(b, ix0 + 4, iz1 - 16, ix0 + 26, iz1 - 15, 6, 4, { tint: [0.4, 0.45, 0.5], seed: 0.61 });
        this._addBuilding(b, ix0 + 4, iz1 - 16, ix0 + 5, iz1 - 2, 6, 4, { tint: [0.4, 0.45, 0.5], seed: 0.61 });
        this._addBuilding(b, ix0 + 25, iz1 - 16, ix0 + 26, iz1 - 2, 6, 4, { tint: [0.4, 0.45, 0.5], seed: 0.61 });
        this._addBuilding(b, ix0 + 3.7, iz1 - 16.3, ix0 + 26.3, iz1 - 2, 1, 4, { tint: [0.4, 0.45, 0.5], seed: 0.61, y0: CURB_H + 5.9, name: 'Lock-Up Garage', sign: 'GARAGE', noCollide: true });
        this.lotSurfaces.push({ x0: ix0 + 4, z0: iz1 - 16, x1: ix0 + 26, z1: iz1, type: 'concrete' });
        this.landmarks.garage = { x: ix0 + 15, z: iz1 - 8, entry: { x: ix0 + 15, z: iz1 + 3 } };
        break;
      }
      case 'liquor': {
        this._houses(b, rng);
        // corner store on the north street with a small parking apron out front
        const sx0 = cx - 13, sx1 = cx + 13, mid = (iz0 + iz1) / 2;
        const clear = (o) => o.x1 > sx0 - 1 && o.x0 < sx1 + 1 && o.z0 < mid - 0.2 && o.z1 > iz0 - 1;
        this.buildings = this.buildings.filter((x) => !(clear(x) && x.district === b.district && x.y0 < 1));
        this.fences = this.fences.filter((f) => !(Math.max(f.x0, f.x1) > sx0 - 1 && Math.min(f.x0, f.x1) < sx1 + 1 && Math.min(f.z0, f.z1) < mid - 0.2 && Math.max(f.z0, f.z1) > iz0 - 1));
        this.lotSurfaces = this.lotSurfaces.filter((l) => !clear(l));
        this.props = this.props.filter((pr) => !(pr.x > sx0 - 1 && pr.x < sx1 + 1 && pr.z > iz0 - 1 && pr.z < mid - 0.2));
        this.parkingSpots = this.parkingSpots.filter((pr) => !(pr.x > sx0 - 1 && pr.x < sx1 + 1 && pr.z > iz0 - 1 && pr.z < mid - 0.2));
        this.lotSurfaces.push({ x0: sx0, z0: iz0, x1: sx1, z1: mid - 0.2, type: 'asphalt' });
        this._addBuilding(b, cx - 11, iz0 + 9, cx + 11, iz0 + 21, 5, 5, { tint: [0.95, 0.88, 0.7], seed: 0.83, roof: 'ac', floorH: 5, name: "Ray's Liquor", sign: 'LIQUOR' });
        this.props.push({ type: 'trashcan', x: cx + 12, z: iz0 + 8, rot: 0 });
        this.props.push({ type: 'phonebooth', x: cx - 12.2, z: iz0 + 7.5, rot: 0 });
        this.landmarks.liquor = { x: cx, z: iz0 - 1 };
        break;
      }
      case 'projects': {
        b.ground = 'grass';
        this._addBuilding(b, ix0 + 4, iz0 + 4, ix1 - 4, iz0 + 20, 13.6, 2, { tint: [0.8, 0.6, 0.5], seed: 0.2, roof: 'ac', name: 'Cedar Row Projects' });
        this._addBuilding(b, ix0 + 4, iz1 - 20, ix1 - 4, iz1 - 4, 13.6, 2, { tint: [0.8, 0.6, 0.5], seed: 0.21, roof: 'ac' });
        this.lotSurfaces.push({ x0: ix0 + 4, z0: iz0 + 26, x1: ix1 - 4, z1: iz1 - 26, type: 'asphalt' });
        this.landmarks.projects = { x: cx, z: cz };
        for (let k = 0; k < 3; k++) this.parkingSpots.push({ x: ix0 + 14 + k * 9, z: cz, rot: Math.PI / 2, district: 'hood', lot: true });
        break;
      }
      case 'pierfront': {
        this._perimeter(b, rng, { depth: [14, 18], width: [12, 20], floors: [1, 3], styles: [5, 7, 3], tint: () => [1, 0.95, 0.88] });
        this.landmarks.pierfront = { x: cx, z: iz1 + 1 };
        break;
      }
    }
  }

  // Street furniture: street lights, traffic lights, palms along sidewalks, hydrants, etc.
  _streetProps() {
    const rng = new RNG(this.seed + 99);
    for (const b of this.blocks) {
      const { x0, z0, x1, z1, district } = b;
      const edges = [
        { ax: x0, az: z0, bx: x1, bz: z0, nx: 0, nz: -1 }, // north edge (faces -z road)
        { ax: x1, az: z0, bx: x1, bz: z1, nx: 1, nz: 0 },
        { ax: x1, az: z1, bx: x0, bz: z1, nx: 0, nz: 1 },
        { ax: x0, az: z1, bx: x0, bz: z0, nx: -1, nz: 0 },
      ];
      const palmy = district === 'beach' || district === 'hills' || district === 'hood' || district === 'westside';
      for (let e = 0; e < 4; e++) {
        const E = edges[e];
        const len = Math.hypot(E.bx - E.ax, E.bz - E.az);
        const dx = (E.bx - E.ax) / len, dz = (E.bz - E.az) / len;
        // street lights every ~32m on N and E edges (other side gets them from neighbour block)
        const lightRot = Math.atan2(E.nx, E.nz);
        const offs = 0.6;
        if (e === 0 || e === 2 || b.district !== 'hood') {
          for (let t = 12; t < len - 8; t += 34) {
            this.props.push({ type: 'streetlight', x: E.ax + dx * t - E.nx * offs, z: E.az + dz * t - E.nz * offs, rot: lightRot });
          }
        }
        // trees / palms
        const treeStep = palmy ? 13 : 17;
        if (district !== 'docks' && (palmy || district === 'downtown' || district === 'midtown' || rng.chance(0.5))) {
          for (let t = 6 + (e * 3) % 7; t < len - 6; t += treeStep) {
            if (rng.chance(0.25)) continue;
            this.props.push({ type: palmy ? 'palm' : 'tree', x: E.ax + dx * t - E.nx * 1.3, z: E.az + dz * t - E.nz * 1.3, rot: rng.range(0, 6.28), scale: rng.range(0.9, 1.3), street: true });
          }
        }
        // misc props
        if (rng.chance(0.55)) this.props.push({ type: 'hydrant', x: E.ax + dx * rng.range(10, len - 10) - E.nx * 0.8, z: E.az + dz * rng.range(10, len - 10) - E.nz * 0.8, rot: 0 });
        if (district !== 'hills' && rng.chance(0.4)) this.props.push({ type: 'trashcan', x: E.ax + dx * rng.range(8, len - 8) - E.nx * 0.9, z: E.az + dz * rng.range(8, len - 8) - E.nz * 0.9, rot: 0 });
        if ((district === 'midtown' || district === 'downtown' || district === 'beach') && rng.chance(0.3)) {
          const t = rng.range(15, len - 15);
          this.props.push({ type: 'busstop', x: E.ax + dx * t - E.nx * 3.0, z: E.az + dz * t - E.nz * 3.0, rot: lightRot + Math.PI });
        }
        if (rng.chance(0.25)) this.props.push({ type: 'phonebooth', x: E.ax + dx * rng.range(10, len - 10) - E.nx * 1.0, z: E.az + dz * rng.range(10, len - 10) - E.nz * 1.0, rot: lightRot });
        // curbside parking spots (stored along the edge, pushed onto the road below)
        if (district !== 'hills' && district !== 'docks' && rng.chance(0.55)) {
          for (let t = 16; t < len - 16; t += 7) {
            if (rng.chance(0.55)) continue;
            this.parkingSpots.push({ x: E.ax + dx * t, z: E.az + dz * t, edge: e, district, curb: true, rot: 0 });
          }
        }
      }
      // traffic lights at the block's NW corner (intersection XS[i],ZS[j]) — 4 per intersection handled by corners
      const cornersInfo = [[x0, z0, -1, -1], [x1, z0, 1, -1], [x1, z1, 1, 1], [x0, z1, -1, 1]];
      for (const c of cornersInfo) {
        const ni = this.nearestX(c[0] + c[2] * 10), nj = this.nearestZ(c[1] + c[3] * 10);
        if (RB_SET.has(ni + ',' + nj)) { this.props.push({ type: 'palm', x: c[0] - c[2] * 1.4, z: c[1] - c[3] * 1.4, rot: 0, scale: 1.1 }); continue; }
        this.props.push({ type: 'trafficlight', x: c[0] - c[2] * 0.8, z: c[1] - c[3] * 0.8, rot: 0, corner: [c[2], c[3]] });
      }
    }
    // push curb parking spots onto the parking strip of the adjacent road, facing the flow of traffic
    const k = HALF_ROAD - PARK_OFF;
    for (const sp of this.parkingSpots) {
      if (!sp.curb) continue;
      if (sp.edge === 0) { sp.z -= k; sp.rot = Math.PI / 2; }
      else if (sp.edge === 1) { sp.x += k; sp.rot = 0; }
      else if (sp.edge === 2) { sp.z += k; sp.rot = -Math.PI / 2; }
      else { sp.x -= k; sp.rot = Math.PI; }
    }
    // Remove street props that collide with special fences etc. is unnecessary; remove props inside roads
    this.props = this.props.filter((p) => !this.isOnRoad(p.x, p.z) || p.type === 'trafficlight');
  }

  _buildSidewalkGraph() {
    // Nodes: 4 corners per block. Edges: perimeter + crosswalks.
    const nodes = [];
    const nI = XS.length - 1;
    for (const b of this.blocks) {
      b.nodeIds = b.corners.map((c) => { const id = nodes.length; nodes.push({ id, x: c.x, z: c.z, links: [], block: b }); return id; });
      for (let k = 0; k < 4; k++) {
        const a = b.nodeIds[k], c = b.nodeIds[(k + 1) % 4];
        nodes[a].links.push(c); nodes[c].links.push(a);
      }
    }
    for (const b of this.blocks) {
      const east = b.i + 1 < nI ? this.getBlock(b.i + 1, b.j) : null;
      const south = this.getBlock(b.i, b.j + 1);
      const link = (a, c) => { nodes[a].links.push(c); nodes[c].links.push(a); nodes[a].cross = nodes[a].cross || []; nodes[a].cross.push(c); nodes[c].cross = nodes[c].cross || []; nodes[c].cross.push(a); };
      if (east) { link(b.nodeIds[1], east.nodeIds[0]); link(b.nodeIds[2], east.nodeIds[3]); }
      if (south && south.j === b.j + 1) { link(b.nodeIds[3], south.nodeIds[0]); link(b.nodeIds[2], south.nodeIds[1]); }
    }
    this.walkNodes = nodes;
  }

  _buildLandmarks() {
    const pier = this.landmarks.pier;
    this.landmarks.ferris = { x: (pier.x0 + pier.x1) / 2, z: pier.z1 - 40 };
    this.landmarks.sign = { x: -60, z: -1010 };
    this.landmarks.beach = { x: -40, z: CITY.maxZ + 30 };
    this.landmarks.docksQuay = { x: 900, z: 300 };
    this.landmarks.airstrip = null;
    // derive building colliders
    this.colliders = [];
    for (const b of this.buildings) {
      if (b.noCollide) continue;
      const maxY = b.y1 + (b.roof === 'gable' ? 2.5 : 0);
      if (b.rot) this.colliders.push({ cx: (b.x0 + b.x1) / 2, cz: (b.z0 + b.z1) / 2, hx: (b.x1 - b.x0) / 2, hz: (b.z1 - b.z0) / 2, yaw: b.rot, minY: b.y0 - 1.2, maxY, type: 'building' });
      else this.colliders.push({ minX: b.x0, minZ: b.z0, maxX: b.x1, maxZ: b.z1, minY: b.y0 - 0.2, maxY, type: 'building' });
    }
    for (const f of this.fences) {
      const y0 = f.y ?? 0;
      if (f.rot) this.colliders.push({ cx: f.cx, cz: f.cz, hx: f.hx, hz: f.hz, yaw: f.rot, minY: y0 - 0.5, maxY: y0 + CURB_H + f.h, type: 'fence', soft: f.type !== 'hedge' && f.type !== 'wall' });
      else this.colliders.push({ minX: Math.min(f.x0, f.x1), minZ: Math.min(f.z0, f.z1), maxX: Math.max(f.x0, f.x1), maxZ: Math.max(f.z0, f.z1), minY: y0 - 0.5, maxY: y0 + CURB_H + f.h, type: 'fence', soft: f.type !== 'hedge' && f.type !== 'wall' });
    }
    for (const c of this.containers) {
      const hx = 1.25, hz = 3.05;
      const ch = Math.abs(Math.sin(c.rot)) > 0.5;
      const cy = c.y ?? 0;
      this.colliders.push({ minX: c.x - (ch ? hz : hx), maxX: c.x + (ch ? hz : hx), minZ: c.z - (ch ? hx : hz), maxZ: c.z + (ch ? hx : hz), minY: cy + c.level * 2.6, maxY: cy + (c.level + 1) * 2.6 + CURB_H, type: 'container' });
    }
  }
}
