// Builds all renderable geometry for the city from the CityMap: roads, sidewalks, buildings,
// rooftop details, street furniture (instanced per chunk), vegetation, terrain, ocean, pools.
import * as THREE from 'three';
import { GeoBuilder, mat4 } from './geom.js';
import { std, patch, U } from '../render/materials.js';
import { ROAD_EXT, GROUND_EXT, BUILDING_EXT, PROP_EXT, NOISE_GLSL, intersectionPhase } from './shaders.js';
import * as P from './props.js';
import { palmFrondTexture, adAtlas, signTexture, softDotTexture } from './textures.js';
import { CITY, CURB_H, WATER_Y, XS, ZS, WORLD } from './citymap.js';
import { RNG, clamp } from '../core/utils.js';
import { buildLandmarks } from './landmarks.js';
import { TerrainMesh } from './terrainmesh.js';
import { RoadMeshes } from './roadmesh.js';
import { Vegetation } from './vegetation.js';
import { LAKE } from './worldgen.js';

const CHUNK = 200;
const GROUND_TYPES = { concrete: 0, grass: 1, dirt: 2, asphalt: 3, plaza: 4, driveway: 5, court: 6, path: 7, sand: 8, runway: 9, taxiway: 10, apron: 11, helipad: 12, dirtpad: 13 };

export class City {
  constructor(scene, map, collision) {
    this.scene = scene;
    this.map = map;
    this.collision = collision;
    this.root = new THREE.Group();
    this.root.name = 'city';
    scene.add(this.root);
    this.chunks = new Map();
    this.propColliders = [];
    this.dynamicDebris = [];
    this.billboardMat = null;
    this.signMats = [];
    this.animated = [];
    this.build();
  }

  chunk(x, z) {
    const cx = Math.floor(x / CHUNK), cz = Math.floor(z / CHUNK);
    const k = cx + ',' + cz;
    let c = this.chunks.get(k);
    if (!c) {
      c = {
        key: k, cx: (cx + 0.5) * CHUNK, cz: (cz + 0.5) * CHUNK,
        bld: new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3, aB: 4 }),
        det: new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 }),
        props: {}, group: new THREE.Group(), propGroup: new THREE.Group(),
      };
      c.group.add(c.propGroup);
      this.root.add(c.group);
      this.chunks.set(k, c);
    }
    return c;
  }

  build() {
    this._materials();
    this._roads();
    this._ground();
    this._buildings();
    this._fences();
    this._props();
    this._containers();
    this.terrainMesh = new TerrainMesh(this.root, this.map);
    this.roadMeshes = new RoadMeshes(this.root, this.map, this.collision);
    this.vegetation = new Vegetation(this.root, this.map, this.collision);
    this._pads();
    this._water();
    this._billboards();
    this._signs();
    this.landmarks = buildLandmarks(this, this.map);
    this._finalizeChunks();
  }

  _materials() {
    this.mats = {
      road: std({ color: 0xffffff, roughness: 0.9 }, ROAD_EXT),
      ground: std({ color: 0xffffff, roughness: 0.9 }, GROUND_EXT),
      lot: std({ color: 0xffffff, roughness: 0.9, polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -4 }, { ...GROUND_EXT, key: 'ground' }),
      building: std({ color: 0xffffff, vertexColors: true, roughness: 0.85 }, BUILDING_EXT),
      detail: std({ color: 0xffffff, vertexColors: true, roughness: 0.8, metalness: 0.1 }),
      prop: std({ color: 0xffffff, vertexColors: true, roughness: 0.6, metalness: 0.2 }, PROP_EXT),
      frond: std({ color: 0xffffff, vertexColors: true, map: palmFrondTexture(), alphaTest: 0.45, side: THREE.DoubleSide, roughness: 0.8 }, { key: 'frond' }),
      leaves: std({ color: 0xffffff, vertexColors: true, roughness: 0.85, flatShading: false }, { key: 'leaves' }),
      trunk: std({ color: 0xffffff, vertexColors: true, roughness: 0.95 }, { key: 'trunk' }),
      chain: std({ color: 0x9aa0a6, roughness: 0.5, metalness: 0.6, transparent: true, opacity: 0.35, depthWrite: false, side: THREE.DoubleSide }, { key: 'chain' }),
    };
  }

  // -------------------------------------------------------------- roads
  _roads() {
    const w = CITY.maxX - CITY.minX, d = CITY.maxZ - CITY.minZ;
    const geo = new THREE.PlaneGeometry(w, d, 32, 32);
    geo.rotateX(-Math.PI / 2);
    geo.translate((CITY.minX + CITY.maxX) / 2, 0, (CITY.minZ + CITY.maxZ) / 2);
    const mesh = new THREE.Mesh(geo, this.mats.road);
    mesh.receiveShadow = true;
    mesh.name = 'roads';
    this.root.add(mesh);
  }

  // -------------------------------------------------------------- blocks & lot surfaces
  _ground() {
    const attrs = { position: 3, normal: 3, uv: 2, aRect: 4, aLot: 1 };
    const gb = new GeoBuilder(attrs);
    for (const b of this.map.blocks) {
      gb.set('aRect', b.x0, b.z0, b.x1, b.z1);
      gb.set('aLot', GROUND_TYPES[b.ground] ?? 0);
      gb.box(b.x0, 0, b.z0, b.x1, CURB_H, b.z1, { top: true, sides: true });
    }
    const blocks = new THREE.Mesh(gb.build(), this.mats.ground);
    blocks.receiveShadow = true;
    blocks.name = 'blocks';
    this.root.add(blocks);

    const lb = new GeoBuilder(attrs);
    lb.set('aRect', 0, 0, 0, 0);
    // Overlapping lot surfaces (yard + driveway, crossing paths...) are stacked in 1 cm layers so they
    // never share a plane (that is what made them flicker).
    const layers = this._lotLayers(this.map.lotSurfaces);
    this.map.lotSurfaces.forEach((s, i) => {
      const y = CURB_H + 0.01 + layers[i] * 0.01;
      s.y = y;
      lb.set('aLot', GROUND_TYPES[s.type] ?? 0);
      lb.quad([s.x0, y, s.z1], [s.x1, y, s.z1], [s.x1, y, s.z0], [s.x0, y, s.z0], [0, 1, 0]);
    });
    // beach sand strip handled by terrain; pier handled by landmarks
    const lots = new THREE.Mesh(lb.build(), this.mats.lot);
    lots.receiveShadow = true;
    lots.name = 'lots';
    this.root.add(lots);
  }

  _lotLayers(list) {
    const cell = 40, grid = new Map(), layers = new Array(list.length).fill(0);
    list.forEach((s, i) => {
      let layer = 0;
      const seen = new Set();
      for (let gx = Math.floor(s.x0 / cell); gx <= Math.floor(s.x1 / cell); gx++) for (let gz = Math.floor(s.z0 / cell); gz <= Math.floor(s.z1 / cell); gz++) {
        const arr = grid.get(gx * 7919 + gz);
        if (!arr) continue;
        for (const j of arr) {
          if (seen.has(j)) continue;
          seen.add(j);
          const o = list[j];
          if (o.x0 < s.x1 - 0.01 && o.x1 > s.x0 + 0.01 && o.z0 < s.z1 - 0.01 && o.z1 > s.z0 + 0.01) layer = Math.max(layer, layers[j] + 1);
        }
      }
      layers[i] = Math.min(layer, 4);
      for (let gx = Math.floor(s.x0 / cell); gx <= Math.floor(s.x1 / cell); gx++) for (let gz = Math.floor(s.z0 / cell); gz <= Math.floor(s.z1 / cell); gz++) {
        const k = gx * 7919 + gz;
        if (!grid.has(k)) grid.set(k, []);
        grid.get(k).push(i);
      }
    });
    return layers;
  }

  // -------------------------------------------------------------- buildings
  _buildings() {
    const rng = new RNG(4242);
    for (const b of this.map.buildings) {
      const bcx = (b.x0 + b.x1) / 2, bcz = (b.z0 + b.z1) / 2;
      const c = this.chunk(bcx, bcz);
      const gb = c.bld;
      const start = gb.count, startD = c.det.count;
      gb.set('color', b.tint[0], b.tint[1], b.tint[2]);
      const onGround = b.y0 < CURB_H + 0.5 || (b.base != null && Math.abs(b.y0 - b.base) < 0.01);
      const ground = onGround ? (b.kind === 'house' || b.kind === 'mansion' || b.kind === 'barn' ? 2 : 1) : 0;
      const floorH = b.floorH;
      const h = b.y1 - b.y0;
      const nx = Math.max(1, Math.round((b.x1 - b.x0) / b.cell));
      const nz = Math.max(1, Math.round((b.z1 - b.z0) / b.cell));
      const floors = Math.max(1, Math.round(h / floorH));
      const fH = h / floors;
      const v0 = 0, v1 = h / fH;
      gb.set('aB', b.style, b.seed, 0, ground);
      const { x0, z0, x1, z1, y0, y1 } = b;
      // walls (u in cells, v in floors)
      gb.quad([x1, y0, z0], [x0, y0, z0], [x0, y1, z0], [x1, y1, z0], [0, 0, -1], [0, v0], [nx, v0], [nx, v1], [0, v1]);
      gb.quad([x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1], [0, 0, 1], [0, v0], [nx, v0], [nx, v1], [0, v1]);
      gb.quad([x0, y0, z0], [x0, y0, z1], [x0, y1, z1], [x0, y1, z0], [-1, 0, 0], [0, v0], [nz, v0], [nz, v1], [0, v1]);
      gb.quad([x1, y0, z1], [x1, y0, z0], [x1, y1, z0], [x1, y1, z1], [1, 0, 0], [0, v0], [nz, v0], [nz, v1], [0, v1]);
      if (b.roof === 'gable') {
        this._gable(gb, b);
      } else {
        gb.set('aB', b.style, b.seed, 1, 0);
        gb.quad([x0, y1, z1], [x1, y1, z1], [x1, y1, z0], [x0, y1, z0], [0, 1, 0], [x0, z1], [x1, z1], [x1, z0], [x0, z0]);
        // parapet on taller buildings
        if (h > 7 && b.kind !== 'house') {
          const p = 0.35, ph = 0.9;
          gb.set('aB', b.style, b.seed, 1, 0);
          gb.box(x0, y1, z0, x1, y1 + ph, z0 + p, { top: true });
          gb.box(x0, y1, z1 - p, x1, y1 + ph, z1, { top: true });
          gb.box(x0, y1, z0 + p, x0 + p, y1 + ph, z1 - p, { top: true });
          gb.box(x1 - p, y1, z0 + p, x1, y1 + ph, z1 - p, { top: true });
        }
        this._roofDetails(c, b, rng);
      }
      if (b.rot) { gb.rotateFrom(start, bcx, bcz, b.rot); c.det.rotateFrom(startD, bcx, bcz, b.rot); }
    }
  }

  _gable(gb, b) {
    const { x0, z0, x1, z1, y1 } = b;
    const alongX = (x1 - x0) >= (z1 - z0);
    const over = 0.5;
    const rh = Math.min(x1 - x0, z1 - z0) * 0.32;
    gb.set('aB', b.style, b.seed, 2, 0);
    if (alongX) {
      const zm = (z0 + z1) / 2;
      const X0 = x0 - over, X1 = x1 + over, Z0 = z0 - over, Z1 = z1 + over;
      const ny = (zm - Z0), l = Math.hypot(ny, rh);
      // south slope (+z)
      gb.quad([X0, y1 - 0.2, Z1], [X1, y1 - 0.2, Z1], [X1, y1 + rh, zm], [X0, y1 + rh, zm], [0, ny / l, rh / l], [0, 0], [(X1 - X0) / 3, 0], [(X1 - X0) / 3, l / 3], [0, l / 3]);
      // north slope (-z)
      gb.quad([X1, y1 - 0.2, Z0], [X0, y1 - 0.2, Z0], [X0, y1 + rh, zm], [X1, y1 + rh, zm], [0, ny / l, -rh / l], [0, 0], [(X1 - X0) / 3, 0], [(X1 - X0) / 3, l / 3], [0, l / 3]);
      // gable ends (wall material)
      gb.set('aB', b.style, b.seed, 0, 0);
      const a = gb.vertex(x0, y1, z1, -1, 0, 0, 0, 0), bb = gb.vertex(x0, y1, z0, -1, 0, 0, 1, 0), cc = gb.vertex(x0, y1 + rh, zm, -1, 0, 0, 0.5, 0.3);
      gb.tri(bb, a, cc);
      const a2 = gb.vertex(x1, y1, z0, 1, 0, 0, 0, 0), b2 = gb.vertex(x1, y1, z1, 1, 0, 0, 1, 0), c2 = gb.vertex(x1, y1 + rh, zm, 1, 0, 0, 0.5, 0.3);
      gb.tri(b2, a2, c2);
    } else {
      const xm = (x0 + x1) / 2;
      const X0 = x0 - over, X1 = x1 + over, Z0 = z0 - over, Z1 = z1 + over;
      const nx = (xm - X0), l = Math.hypot(nx, rh);
      gb.quad([X1, y1 - 0.2, Z1], [X1, y1 - 0.2, Z0], [xm, y1 + rh, Z0], [xm, y1 + rh, Z1], [rh / l, nx / l, 0], [0, 0], [(Z1 - Z0) / 3, 0], [(Z1 - Z0) / 3, l / 3], [0, l / 3]);
      gb.quad([X0, y1 - 0.2, Z0], [X0, y1 - 0.2, Z1], [xm, y1 + rh, Z1], [xm, y1 + rh, Z0], [-rh / l, nx / l, 0], [0, 0], [(Z1 - Z0) / 3, 0], [(Z1 - Z0) / 3, l / 3], [0, l / 3]);
      gb.set('aB', b.style, b.seed, 0, 0);
      const a = gb.vertex(x0, y1, z0, 0, 0, -1, 0, 0), bb = gb.vertex(x1, y1, z0, 0, 0, -1, 1, 0), cc = gb.vertex(xm, y1 + rh, z0, 0, 0, -1, 0.5, 0.3);
      gb.tri(bb, a, cc);
      const a2 = gb.vertex(x1, y1, z1, 0, 0, 1, 0, 0), b2 = gb.vertex(x0, y1, z1, 0, 0, 1, 1, 0), c2 = gb.vertex(xm, y1 + rh, z1, 0, 0, 1, 0.5, 0.3);
      gb.tri(b2, a2, c2);
    }
  }

  _roofDetails(c, b, rng) {
    const det = c.det;
    const { x0, z0, x1, z1, y1 } = b;
    const w = x1 - x0, d = z1 - z0;
    const cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
    const col = (hex) => { const cc = new THREE.Color(hex); det.set('color', cc.r, cc.g, cc.b); };
    const boxG = (bx0, by0, bz0, bx1, by1, bz1) => det.box(bx0, by0, bz0, bx1, by1, bz1, { top: true });
    if (b.roof === 'ac' || (b.roof === 'flat' && rng.chance(0.5) && w > 8 && d > 8)) {
      const n = rng.int(1, 4);
      for (let k = 0; k < n; k++) {
        const ax = rng.range(x0 + 2, x1 - 4), az = rng.range(z0 + 2, z1 - 4);
        col(0x8c9196); boxG(ax, y1, az, ax + rng.range(1.2, 2.6), y1 + rng.range(0.8, 1.6), az + rng.range(1.2, 2.2));
      }
      if (rng.chance(0.3) && b.district !== 'downtown' && b.y1 < 40) {
        // water tower
        const tx = rng.range(x0 + 3, x1 - 3), tz = rng.range(z0 + 3, z1 - 3);
        col(0x5a4a3a);
        det.addGeometry(new THREE.CylinderGeometry(1.4, 1.4, 2.6, 12), mat4(tx, y1 + 3.3, tz));
        col(0x3a3230);
        det.addGeometry(new THREE.ConeGeometry(1.55, 1.0, 12), mat4(tx, y1 + 5.1, tz));
        col(0x333333);
        for (const [lx, lz] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) det.addGeometry(new THREE.CylinderGeometry(0.07, 0.07, 2.2, 5), mat4(tx + lx, y1 + 1.1, tz + lz));
      }
    }
    if (b.roof === 'antenna') {
      col(0x777777);
      det.addGeometry(new THREE.CylinderGeometry(0.08, 0.2, 14, 6), mat4(cx, y1 + 7, cz));
      col(0x999999); boxG(cx - 1.5, y1, cz - 1.5, cx + 1.5, y1 + 2.5, cz + 1.5);
      this._beacon(cx, y1 + 14.2, cz);
    }
    if (b.roof === 'helipad') {
      col(0x3a3c40); boxG(cx - 7, y1, cz - 7, cx + 7, y1 + 0.25, cz + 7);
      col(0xe7c22a);
      det.addGeometry(new THREE.RingGeometry(4.6, 5.2, 32).rotateX(-Math.PI / 2), mat4(cx, y1 + 0.27, cz));
      col(0xf0f0f0);
      boxG(cx - 1.8, y1 + 0.25, cz - 2.2, cx - 1.1, y1 + 0.29, cz + 2.2);
      boxG(cx + 1.1, y1 + 0.25, cz - 2.2, cx + 1.8, y1 + 0.29, cz + 2.2);
      boxG(cx - 1.1, y1 + 0.25, cz - 0.35, cx + 1.1, y1 + 0.29, cz + 0.35);
      this._beacon(x0 + 0.5, y1 + 1.2, z0 + 0.5);
      this._beacon(x1 - 0.5, y1 + 1.2, z1 - 0.5);
    }
    if (b.roof === 'spire') {
      col(0xb0b8c0);
      det.addGeometry(new THREE.ConeGeometry(Math.min(w, d) * 0.35, Math.min(w, d) * 1.2, 4), mat4(cx, y1 + Math.min(w, d) * 0.6, cz, 0, Math.PI / 4, 0));
      this._beacon(cx, y1 + Math.min(w, d) * 1.2 + 0.3, cz);
    }
    if (b.y1 > 60 && b.roof === 'flat') this._beacon(cx, y1 + 1.0, cz);
  }

  _beacon(x, y, z) {
    if (!this.beacons) this.beacons = [];
    this.beacons.push(new THREE.Vector3(x, y, z));
  }

  // flat surfaces outside the city (runways, taxiways, aprons, helipads, gas station forecourts)
  _pads() {
    const list = this.map.padSurfaces;
    if (!list || !list.length) return;
    const gb = new GeoBuilder({ position: 3, normal: 3, uv: 2, aRect: 4, aLot: 1 });
    for (const p of list) {
      const s = Math.sin(p.yaw), c = Math.cos(p.yaw);
      const L = (lx, lz) => [p.cx + lx * c + lz * s, p.y, p.cz - lx * s + lz * c];
      gb.set('aRect', -1, 0, p.hx, p.hz);
      gb.set('aLot', GROUND_TYPES[p.type] ?? 0);
      // split long pads so they follow the (flat) ground in manageable pieces
      const nz = Math.max(1, Math.ceil(p.hz * 2 / 60));
      for (let k = 0; k < nz; k++) {
        const z0 = -p.hz + (k / nz) * p.hz * 2, z1 = -p.hz + ((k + 1) / nz) * p.hz * 2;
        gb.quad(L(-p.hx, z1), L(p.hx, z1), L(p.hx, z0), L(-p.hx, z0), [0, 1, 0], [-p.hx, z1], [p.hx, z1], [p.hx, z0], [-p.hx, z0]);
      }
    }
    const m = new THREE.Mesh(gb.build(), this.mats.lot);
    m.receiveShadow = true;
    m.name = 'pads';
    this.root.add(m);
  }

  // -------------------------------------------------------------- fences / hedges
  _fences() {
    const chainGB = new GeoBuilder({ position: 3, normal: 3, uv: 2 });
    for (const f of this.map.fences) {
      if (f.rot) { this._rotFence(f); continue; }
      const c = this.chunk((f.x0 + f.x1) / 2, (f.z0 + f.z1) / 2);
      const det = c.det;
      const y0 = (f.y ?? 0) + CURB_H, y1 = y0 + f.h;
      if (f.type === 'hedge') {
        det.set('color', 0.16, 0.3, 0.12);
        det.box(f.x0, y0, f.z0, f.x1, y1, f.z1, { top: true });
      } else if (f.type === 'wood') {
        det.set('color', 0.45, 0.33, 0.22);
        const alongX = Math.abs(f.x1 - f.x0) > Math.abs(f.z1 - f.z0);
        if (alongX) det.box(f.x0, y0, (f.z0 + f.z1) / 2 - 0.04, f.x1, y1, (f.z0 + f.z1) / 2 + 0.04, { top: true });
        else det.box((f.x0 + f.x1) / 2 - 0.04, y0, f.z0, (f.x0 + f.x1) / 2 + 0.04, y1, f.z1, { top: true });
      } else {
        det.set('color', 0.45, 0.47, 0.5);
        const alongX = Math.abs(f.x1 - f.x0) > Math.abs(f.z1 - f.z0);
        const len = alongX ? f.x1 - f.x0 : f.z1 - f.z0;
        const n = Math.max(1, Math.round(len / 3));
        for (let k = 0; k <= n; k++) {
          const t = k / n;
          const px = alongX ? f.x0 + (f.x1 - f.x0) * t : (f.x0 + f.x1) / 2;
          const pz = alongX ? (f.z0 + f.z1) / 2 : f.z0 + (f.z1 - f.z0) * t;
          det.box(px - 0.04, y0, pz - 0.04, px + 0.04, y1, pz + 0.04, { top: true });
        }
        const mx = (f.x0 + f.x1) / 2, mz = (f.z0 + f.z1) / 2;
        if (alongX) {
          det.box(f.x0, y1 - 0.05, mz - 0.03, f.x1, y1, mz + 0.03, { top: true });
          chainGB.quad([f.x0, y0, mz], [f.x1, y0, mz], [f.x1, y1, mz], [f.x0, y1, mz], [0, 0, 1], [0, 0], [len / 0.1, 0], [len / 0.1, f.h / 0.1], [0, f.h / 0.1]);
        } else {
          det.box(mx - 0.03, y1 - 0.05, f.z0, mx + 0.03, y1, f.z1, { top: true });
          chainGB.quad([mx, y0, f.z0], [mx, y0, f.z1], [mx, y1, f.z1], [mx, y1, f.z0], [1, 0, 0], [0, 0], [len / 0.1, 0], [len / 0.1, f.h / 0.1], [0, f.h / 0.1]);
        }
      }
    }
    if (!chainGB.empty) {
      const m = new THREE.Mesh(chainGB.build(), this.mats.chain);
      m.name = 'chainlink';
      this.root.add(m);
    }
  }

  _rotFence(f) {
    // rotated rail fence (farms): posts + two rails along local z
    const c = this.chunk(f.cx, f.cz);
    const det = c.det;
    const start = det.count;
    const y0 = f.y + CURB_H, y1 = y0 + f.h;
    det.set('color', 0.48, 0.36, 0.25);
    const n = Math.max(1, Math.round(f.hz * 2 / 3));
    for (let k = 0; k <= n; k++) { const z = f.cz - f.hz + (k / n) * f.hz * 2; det.box(f.cx - 0.07, y0 - 0.3, z - 0.07, f.cx + 0.07, y1, z + 0.07, { top: true }); }
    det.box(f.cx - 0.04, y1 - 0.25, f.cz - f.hz, f.cx + 0.04, y1 - 0.1, f.cz + f.hz, { top: true });
    det.box(f.cx - 0.04, y0 + 0.35, f.cz - f.hz, f.cx + 0.04, y0 + 0.5, f.cz + f.hz, { top: true });
    det.rotateFrom(start, f.cx, f.cz, f.rot);
  }

  // -------------------------------------------------------------- props
  _props() {
    const defs = {
      streetlight: { parts: [[P.streetlightGeo(), 'prop']], r: 0.18, h: 8, breakable: true, mass: 1 },
      trafficlight: { parts: [[P.trafficLightGeo(), 'prop']], r: 0.18, h: 5, breakable: true, mass: 1 },
      hydrant: { parts: [[P.hydrantGeo(), 'prop']], r: 0.25, h: 0.8, breakable: true, mass: 0.3, water: true },
      trashcan: { parts: [[P.trashcanGeo(), 'prop']], r: 0.35, h: 1, breakable: true, mass: 0.2 },
      dumpster: { parts: [[P.dumpsterGeo(), 'prop']], r: 1.0, h: 1.4, breakable: false },
      bench: { parts: [[P.benchGeo(), 'prop']], r: 0.6, h: 0.9, breakable: true, mass: 0.3 },
      busstop: { parts: [[P.busstopGeo(), 'prop']], r: 0.5, h: 2.6, breakable: true, mass: 1 },
      phonebooth: { parts: [[P.phoneboothGeo(), 'prop']], r: 0.55, h: 2.4, breakable: true, mass: 0.8 },
      hoop: { parts: [[P.hoopGeo(), 'prop']], r: 0.2, h: 4, breakable: false },
      fountain: { parts: [[P.fountainGeo(), 'prop']], r: 5.2, h: 0.7, breakable: false },
      silo: { parts: [[P.siloGeo(), 'prop']], r: 3.3, h: 19, breakable: false },
      pump: { parts: [[P.pumpGeo(), 'prop']], r: 0.5, h: 1.9, breakable: true, mass: 0.6 },
      post: { parts: [[P.postGeo(4.6), 'prop']], r: 0.25, h: 4.6, breakable: false },
      stilt: { parts: [[P.postGeo(7), 'prop']], r: 0.25, h: 7, breakable: false },
      haybale: { parts: [[P.haybaleGeo(), 'prop']], r: 0.75, h: 1.5, breakable: false },
      windsock: { parts: [[P.windsockGeo(), 'prop']], r: 0.15, h: 6, breakable: true, mass: 0.2 },
      fueltank: { parts: [[P.fueltankGeo(), 'prop']], r: 8.1, h: 9.3, breakable: false },
      radar: { parts: [[P.radarGeo(), 'prop']], r: 3.6, h: 18, breakable: false },
      boothbar: { parts: [[P.boothbarGeo(), 'prop']], r: 1.6, h: 3, breakable: false },
      sandbags: { parts: [[P.sandbagsGeo(), 'prop']], r: 0.9, h: 1.1, breakable: false },
      powerpole: { parts: [[P.powerpoleGeo(), 'prop']], r: 0.25, h: 11, breakable: true, mass: 1.4 },
      boat: { parts: [[P.boatGeo(), 'prop']], r: 1.4, h: 3.2, breakable: false },
      crossbuck: { parts: [[P.crossbuckGeo(), 'prop']], r: 0.15, h: 4.3, breakable: true, mass: 0.8 },
    };
    for (let v = 0; v < 3; v++) {
      const g = P.palmGeos(v);
      defs['palm' + v] = { parts: [[g.trunk, 'trunk'], [g.fronds, 'frond']], r: 0.32, h: g.height, breakable: false };
    }
    for (let v = 0; v < 2; v++) {
      const g = P.treeGeos(v);
      defs['tree' + v] = { parts: [[g.trunk, 'trunk'], [g.leaves, 'leaves']], r: 0.3, h: g.height, breakable: false };
    }
    this.propDefs = defs;
    const rng = new RNG(777);
    for (const p of this.map.props) {
      let type = p.type;
      if (type === 'palm') type = 'palm' + rng.int(0, 2);
      else if (type === 'tree') type = 'tree' + rng.int(0, 1);
      const def = defs[type];
      if (!def) continue;
      const c = this.chunk(p.x, p.z);
      if (!c.props[type]) c.props[type] = [];
      const y = p.y ?? this.map.groundHeight(p.x, p.z);
      const inst = { x: p.x, y, z: p.z, rot: p.rot || 0, scale: p.scale || 1, type, chunk: c, index: c.props[type].length, broken: false };
      if (type === 'trafficlight') {
        const i = this.map.nearestX(p.x), j = this.map.nearestZ(p.z);
        inst.phase = intersectionPhase(i, j);
      }
      c.props[type].push(inst);
      const multi = type === 'boothbar' || type === 'sandbags';
      const col = { x: p.x, z: p.z, r: def.r, h: y + def.h, y0: y - 0.5, type: 'prop', prop: inst, breakable: def.breakable };
      if (multi) { col.r = 1.4; }
      inst.collider = col;
      this.collision.addCircle(col);
      this.propColliders.push(col);
    }
  }

  _containers() {
    const list = this.map.containers;
    if (!list.length) return;
    const geo = P.containerGeo();
    const mesh = new THREE.InstancedMesh(geo, this.mats.detail, list.length);
    const colors = [0xb03a2e, 0x1f618d, 0x117a65, 0xd4ac0d, 0x6c3483, 0xba4a00];
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), s = new THREE.Vector3(1, 1, 1), p = new THREE.Vector3();
    list.forEach((c, i) => {
      q.setFromAxisAngle(new THREE.Vector3(0, 1, 0), c.rot);
      p.set(c.x, (c.y ?? 0) + CURB_H + c.level * 2.6, c.z);
      m.compose(p, q, s);
      mesh.setMatrixAt(i, m);
      mesh.setColorAt(i, new THREE.Color(colors[c.color % colors.length]));
    });
    mesh.castShadow = true; mesh.receiveShadow = true;
    mesh.name = 'containers';
    this.root.add(mesh);
  }

  // -------------------------------------------------------------- water
  _water() {
    // depth texture over the world for shoreline effects
    const res = 1024;
    const data = new Uint8Array(res * res * 4);
    for (let j = 0; j < res; j++) for (let i = 0; i < res; i++) {
      const x = WORLD.minX + (i + 0.5) / res * (WORLD.maxX - WORLD.minX);
      const z = WORLD.minZ + (j + 0.5) / res * (WORLD.maxZ - WORLD.minZ);
      const dpt = WATER_Y - this.map.hf.sample(x, z);
      const k = (j * res + i) * 4;
      data[k] = clamp(Math.round(dpt / 10 * 255), 0, 255);
      data[k + 1] = 0; data[k + 2] = 0; data[k + 3] = 255;
    }
    const depthTex = new THREE.DataTexture(data, res, res);
    depthTex.magFilter = THREE.LinearFilter; depthTex.minFilter = THREE.LinearFilter;
    depthTex.needsUpdate = true;
    this.waterUniforms = { uDepthTex: { value: depthTex }, uWorldRect: { value: new THREE.Vector4(WORLD.minX, WORLD.minZ, WORLD.maxX - WORLD.minX, WORLD.maxZ - WORLD.minZ) } };
    const waterExt = (key, pool) => ({
      key,
      uniforms: this.waterUniforms,
      fragPars: NOISE_GLSL + `
        uniform sampler2D uDepthTex; uniform vec4 uWorldRect;
        vec3 waveN(vec2 p, float t) {
          vec2 g = vec2(0.0);
          g += vec2(0.6, 0.8) * cos(dot(p, vec2(0.6, 0.8)) * 0.35 + t * 1.3) * 0.35;
          g += vec2(-0.7, 0.7) * cos(dot(p, vec2(-0.7, 0.7)) * 0.62 + t * 1.7) * 0.2;
          g += vec2(0.2, -1.0) * cos(dot(p, vec2(0.2, -1.0)) * 1.3 + t * 2.3) * 0.12;
          g += vec2(0.9, 0.3) * cos(dot(p, vec2(0.9, 0.3)) * 2.7 + t * 3.1) * 0.07;
          g += (vec2(vn2(p * 1.9 + t * 0.6), vn2(p * 1.9 - t * 0.5 + 4.0)) - 0.5) * 0.25;
          return normalize(vec3(-g.x * 0.35, 1.0, -g.y * 0.35));
        }
        float atgFoam = 0.0; float atgDepth = 10.0;
      `,
      fragColor: `
        {
          vec2 wp = vAtgWorld.xz;
          ${pool ? 'atgDepth = 2.0;' : `vec2 duv = (wp - uWorldRect.xy) / uWorldRect.zw;
          atgDepth = texture2D(uDepthTex, duv).r * 10.0;
          // no sea under dry land (it could otherwise z-fight through roads laid right at sea level)
          if (atgDepth < 0.04 && duv.x > 0.0 && duv.x < 1.0 && duv.y > 0.0 && duv.y < 1.0) discard;`}
          vec3 deep = ${pool ? 'vec3(0.02, 0.25, 0.32)' : 'vec3(0.01, 0.06, 0.08)'};
          vec3 shallow = ${pool ? 'vec3(0.1, 0.55, 0.6)' : 'vec3(0.05, 0.28, 0.27)'};
          diffuseColor.rgb = mix(shallow, deep, smoothstep(0.0, 6.0, atgDepth));
          ${pool ? '' : `
          float shore = 1.0 - smoothstep(0.0, 1.4, atgDepth);
          float wave = smoothstep(0.75, 1.0, sin(atgDepth * 5.0 - uTime * 1.6 + vn2(wp * 0.15) * 4.0));
          atgFoam = clamp(shore * (0.55 + 0.45 * wave) * smoothstep(0.35, 0.65, vn2(wp * 0.9 + uTime * 0.2)) + shore * shore * 0.6, 0.0, 1.0);
          diffuseColor.rgb = mix(diffuseColor.rgb, vec3(0.85, 0.88, 0.9), atgFoam);
          diffuseColor.a = mix(0.72, 0.98, smoothstep(0.0, 2.5, atgDepth));
          diffuseColor.a = max(diffuseColor.a, atgFoam);`}
        }
      `,
      fragNormal: `
        {
          vec3 wn = waveN(vAtgWorld.xz * ${pool ? '2.0' : '1.0'}, uTime);
          wn = normalize(mix(wn, vec3(0.0, 1.0, 0.0), atgFoam * 0.8 + smoothstep(200.0, 900.0, length(vAtgWorld - cameraPosition)) * 0.6));
          normal = normalize((viewMatrix * vec4(wn, 0.0)).xyz);
        }
      `,
      fragRoughness: 'roughnessFactor = mix(0.04, 0.6, atgFoam);',
    });
    const mat = std({ color: 0xffffff, roughness: 0.05, metalness: 0.0, transparent: true, opacity: 1, envMapIntensity: 1.8 }, waterExt('water', false));
    // subdivided: huge single triangles interpolate depth poorly right at the shoreline
    const geo = new THREE.PlaneGeometry(40000, 40000, 40, 40);
    geo.rotateX(-Math.PI / 2);
    const water = new THREE.Mesh(geo, mat);
    water.position.set(-2000, WATER_Y, -2000);
    water.frustumCulled = false;
    water.renderOrder = 1;
    water.receiveShadow = true;
    water.name = 'ocean';
    this.root.add(water);
    this.water = water;

    // Lake Mirador up in the hills
    {
      const lg = new THREE.CircleGeometry(LAKE.r + 24, 64);
      lg.rotateX(-Math.PI / 2);
      const lakeMat = std({ color: 0xffffff, roughness: 0.05, metalness: 0.0, envMapIntensity: 1.6 }, waterExt('lake', true));
      const lake = new THREE.Mesh(lg, lakeMat);
      lake.position.set(LAKE.x, LAKE.y, LAKE.z);
      lake.name = 'lake';
      this.root.add(lake);
    }
    const poolMat = std({ color: 0xffffff, roughness: 0.05, metalness: 0.0 }, waterExt('pool', true));
    const pg = new GeoBuilder({ position: 3, normal: 3, uv: 2 });
    for (const pl of this.map.pools) {
      const y = (pl.y ?? 0) + CURB_H + 0.08;
      pg.quad([pl.x0, y, pl.z1], [pl.x1, y, pl.z1], [pl.x1, y, pl.z0], [pl.x0, y, pl.z0], [0, 1, 0]);
    }
    if (!pg.empty) {
      const pm = new THREE.Mesh(pg.build(), poolMat);
      poolMat.polygonOffset = true; poolMat.polygonOffsetFactor = -3; poolMat.polygonOffsetUnits = -6;
      pm.name = 'pools';
      this.root.add(pm);
    }
  }

  // -------------------------------------------------------------- billboards & signs
  _billboards() {
    const atlas = adAtlas();
    const rng = new RNG(555);
    const frame = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
    const faces = new GeoBuilder({ position: 3, normal: 3, uv: 2 });
    const bgeo = P.billboardGeo();
    const cands = this.map.buildings.filter((b) => !b.rot && b.base == null && b.roof === 'flat' && b.y1 > 10 && b.y1 < 45 && (b.x1 - b.x0) > 14 && (b.z1 - b.z0) > 8 && b.district !== 'hills');
    let count = 0;
    for (const b of cands) {
      if (!rng.chance(0.18) || count > 70) continue;
      count++;
      const alongX = rng.chance(0.5);
      const cx = (b.x0 + b.x1) / 2, cz = (b.z0 + b.z1) / 2;
      const rot = alongX ? (rng.chance(0.5) ? 0 : Math.PI) : (rng.chance(0.5) ? Math.PI / 2 : -Math.PI / 2);
      const m = mat4(cx, b.y1, cz, 0, rot, 0);
      frame.set('color', 1, 1, 1);
      frame.addGeometry(bgeo, m);
      const ad = rng.int(0, atlas.count - 1);
      const u0 = (ad % atlas.cols) / atlas.cols, v0 = 1 - (Math.floor(ad / atlas.cols) + 1) / atlas.rows;
      const u1 = u0 + 1 / atlas.cols, v1 = v0 + 1 / atlas.rows;
      // face at local z = 0.36 (front), size 10 x 5
      const pts = [[-5, 5.0, 0.37], [5, 5.0, 0.37], [5, 10.0, 0.37], [-5, 10.0, 0.37]].map((p) => new THREE.Vector3(...p).applyMatrix4(m).toArray());
      const n = new THREE.Vector3(0, 0, 1).applyAxisAngle(new THREE.Vector3(0, 1, 0), rot).toArray();
      faces.quad(pts[0], pts[1], pts[2], pts[3], n, [u0, v0], [u1, v0], [u1, v1], [u0, v1]);
      this.collision.addBox({ minX: cx - 5, maxX: cx + 5, minZ: cz - 5, maxZ: cz + 5, minY: b.y1 + 4.5, maxY: b.y1 + 10.2, type: 'billboard' });
    }
    if (count) {
      const fm = new THREE.Mesh(frame.build(), this.mats.detail);
      fm.castShadow = true;
      this.root.add(fm);
      this.billboardMat = std({ map: atlas.texture, emissiveMap: atlas.texture, emissive: 0xffffff, emissiveIntensity: 0.1, roughness: 0.5 }, { key: 'billboard' });
      const faceMesh = new THREE.Mesh(faces.build(), this.billboardMat);
      faceMesh.name = 'billboards';
      this.root.add(faceMesh);
    }
  }

  _signs() {
    const styles = {
      'GUN BARN': { bg: '#1b1b1b', fg: '#ff5a36' }, HOSPITAL: { bg: '#f4f4f4', fg: '#d62828', glow: false },
      POLICE: { bg: '#0b1f4d', fg: '#e8eefc' }, 'SPRAY SHACK': { bg: '#222', fg: '#6df0ff' },
      'BIG BUN': { bg: '#6b1a00', fg: '#ffd166' }, GARAGE: { bg: '#222', fg: '#ffffff' }, LIQUOR: { bg: '#1d0826', fg: '#ff5ec4' },
      GAS: { bg: '#b3121b', fg: '#ffffff' }, MOTEL: { bg: '#123d4a', fg: '#ff6fb5' }, SALOON: { bg: '#3b2412', fg: '#ffcc66' }, CANTINA: { bg: '#5a1a4a', fg: '#ffd166' },
      'FORT CARVER': { bg: '#2f3a26', fg: '#e9e4c8' }, AIRFIELD: { bg: '#1c2f4a', fg: '#ffffff' }, MALL: { bg: '#20252b', fg: '#7ee0ff' }, LODGE: { bg: '#2d3d22', fg: '#f4e3b2' },
    };
    for (const b of this.map.buildings) {
      if (!b.sign) continue;
      const st = styles[b.sign] || { bg: '#111', fg: '#fff' };
      const tex = signTexture(b.sign, st);
      const mat = patch(new THREE.MeshBasicMaterial({ map: tex, color: 0xffffff }), { key: 'sign' });
      this.signMats.push(mat);
      if (b.rot) {
        // countryside: sign on the face towards the road (b.front)
        const hx = (b.x1 - b.x0) / 2, hz = (b.z1 - b.z0) / 2;
        const w = Math.min(hz * 2 - 1, 12), h = w * 0.19;
        const mesh = new THREE.Mesh(new THREE.PlaneGeometry(w, h), mat);
        const f = b.front || [Math.cos(b.rot), -Math.sin(b.rot)];
        const cx = (b.x0 + b.x1) / 2, cz = (b.z0 + b.z1) / 2;
        mesh.position.set(cx + f[0] * (hx + 0.08), Math.min(b.y1 - h * 0.6, b.y0 + (b.foundation || 0) + 4.2 + h / 2), cz + f[1] * (hx + 0.08));
        mesh.rotation.y = Math.atan2(f[0], f[1]);
        mesh.name = 'sign:' + b.sign;
        this.root.add(mesh);
        continue;
      }
      const w = Math.min(b.x1 - b.x0 - 1, 14), h = w * 0.19;
      const geo = new THREE.PlaneGeometry(w, h);
      const mesh = new THREE.Mesh(geo, mat);
      // sign on the south face if the block's street is south, else north face
      const blk = this.map.blockAt((b.x0 + b.x1) / 2, (b.z0 + b.z1) / 2);
      const faceSouth = blk ? (blk.iz1 - b.z1) < (b.z0 - blk.iz0) : true;
      const y = Math.min(b.y1 - h * 0.6, b.y0 + 4.2 + h / 2);
      if (faceSouth) { mesh.position.set((b.x0 + b.x1) / 2, y, b.z1 + 0.06); }
      else { mesh.position.set((b.x0 + b.x1) / 2, y, b.z0 - 0.06); mesh.rotation.y = Math.PI; }
      mesh.name = 'sign:' + b.sign;
      this.root.add(mesh);
    }
  }

  // -------------------------------------------------------------- finalize chunks: build meshes + instanced props
  _finalizeChunks() {
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), s = new THREE.Vector3(), p = new THREE.Vector3(), up = new THREE.Vector3(0, 1, 0);
    const zero = new THREE.Matrix4().makeScale(0, 0, 0);
    this.zeroMatrix = zero;
    for (const c of this.chunks.values()) {
      if (!c.bld.empty) {
        const mesh = new THREE.Mesh(c.bld.build(), this.mats.building);
        mesh.castShadow = true; mesh.receiveShadow = true;
        mesh.name = 'buildings:' + c.key;
        c.group.add(mesh);
      }
      if (!c.det.empty) {
        const mesh = new THREE.Mesh(c.det.build(), this.mats.detail);
        mesh.castShadow = true; mesh.receiveShadow = true;
        mesh.name = 'details:' + c.key;
        c.group.add(mesh);
      }
      c.instMeshes = {};
      for (const type in c.props) {
        const list = c.props[type];
        const def = this.propDefs[type];
        c.instMeshes[type] = [];
        for (const [geo, matKey] of def.parts) {
          const g = type === 'trafficlight' ? geo.clone() : geo;
          if (type === 'trafficlight') g.setAttribute('aPhase', new THREE.InstancedBufferAttribute(new Float32Array(list.map((i) => i.phase)), 1));
          const im = new THREE.InstancedMesh(g, this.mats[matKey], list.length);
          list.forEach((inst, i) => {
            q.setFromAxisAngle(up, inst.rot);
            s.setScalar(inst.scale);
            p.set(inst.x, inst.y, inst.z);
            m.compose(p, q, s);
            im.setMatrixAt(i, m);
          });
          im.castShadow = true;
          im.receiveShadow = matKey !== 'frond';
          im.computeBoundingSphere();
          im.name = type + ':' + c.key;
          c.propGroup.add(im);
          c.instMeshes[type].push(im);
        }
      }
    }
    this._lightPools();
  }

  _lightPools() {
    // fake street light illumination decals (additive) + beacon glows
    const lamps = [];
    for (const c of this.chunks.values()) {
      for (const inst of c.props.streetlight || []) {
        const fx = Math.sin(inst.rot), fz = Math.cos(inst.rot);
        lamps.push([inst.x + fx * 3.1, inst.z + fz * 3.1, inst.y]);
      }
    }
    this.lampPositions = lamps;
    const geo = new THREE.PlaneGeometry(1, 1);
    geo.rotateX(-Math.PI / 2);
    const mat = new THREE.ShaderMaterial({
      uniforms: { uStreetLights: U.uStreetLights, uWet: U.uWet },
      vertexShader: `varying vec2 vUv; void main(){ vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * instanceMatrix * vec4(position, 1.0); }`,
      fragmentShader: `varying vec2 vUv; uniform float uStreetLights; uniform float uWet;
        void main(){ float d = length(vUv - 0.5) * 2.0; float f = max(0.0, 1.0 - d);
          float a = pow(f, 1.6) * 0.7 + pow(f, 5.0) * 0.8;
          gl_FragColor = vec4(vec3(1.0, 0.74, 0.42) * a * uStreetLights * (1.05 + uWet * 0.8), 1.0); }`,
      transparent: true, depthWrite: false, blending: THREE.AdditiveBlending,
      polygonOffset: true, polygonOffsetFactor: -4, polygonOffsetUnits: -8,
    });
    const im = new THREE.InstancedMesh(geo, mat, lamps.length);
    const m = new THREE.Matrix4();
    lamps.forEach((l, i) => { m.makeScale(24, 1, 24); m.setPosition(l[0], (this.map.groundHeight(l[0], l[1]) || 0) + 0.03, l[1]); im.setMatrixAt(i, m); });
    im.frustumCulled = false;
    im.renderOrder = 2;
    im.name = 'lightpools';
    this.root.add(im);
    this.lightPools = im;

    // halos round the lamp heads (camera-facing, additive; they carry the lamps' glow into the distance)
    const hm = new THREE.Matrix4();
    const haloGeo = new THREE.PlaneGeometry(1, 1);
    const haloMat = new THREE.ShaderMaterial({
      uniforms: { uStreetLights: U.uStreetLights, uWet: U.uWet },
      vertexShader: `varying vec2 vUv; varying float vFade;
        void main(){ vUv = uv; vec4 c = modelViewMatrix * instanceMatrix * vec4(0.0, 0.0, 0.0, 1.0);
          float d = -c.z; float size = 2.6 + d * 0.012; vFade = clamp(1.0 - d / 1400.0, 0.0, 1.0);
          c.xy += position.xy * size; gl_Position = projectionMatrix * c; }`,
      fragmentShader: `varying vec2 vUv; varying float vFade; uniform float uStreetLights; uniform float uWet;
        void main(){ float d = length(vUv - 0.5) * 2.0; float f = max(0.0, 1.0 - d);
          float a = pow(f, 2.5) * 0.9 + pow(f, 12.0) * 2.5;
          gl_FragColor = vec4(vec3(1.0, 0.8, 0.52) * a * uStreetLights * vFade * (1.0 + uWet * 0.6), 1.0); }`,
      transparent: true, depthWrite: false, blending: THREE.AdditiveBlending,
    });
    const halos = new THREE.InstancedMesh(haloGeo, haloMat, lamps.length);
    lamps.forEach((l, i) => { hm.makeTranslation(l[0], l[2] + 7.86, l[1]); halos.setMatrixAt(i, hm); });
    halos.frustumCulled = false;
    halos.renderOrder = 3;
    halos.name = 'lamphalos';
    this.root.add(halos);
    // soft cones of light under each lamp (stronger in rain)
    const coneGeo = new THREE.CylinderGeometry(0.28, 3.6, 7.7, 14, 1, true);
    coneGeo.translate(0, -7.7 / 2, 0);
    const coneMat = new THREE.ShaderMaterial({
      uniforms: { uStreetLights: U.uStreetLights, uWet: U.uWet },
      vertexShader: `varying float vY; varying float vEdge;
        void main(){ vY = -position.y / 7.7; vec4 mv = modelViewMatrix * instanceMatrix * vec4(position, 1.0);
          vec3 n = normalize(normalMatrix * mat3(instanceMatrix) * normal); vEdge = abs(dot(n, normalize(-mv.xyz)));
          gl_Position = projectionMatrix * mv; }`,
      fragmentShader: `varying float vY; varying float vEdge; uniform float uStreetLights; uniform float uWet;
        void main(){ float a = (1.0 - vY) * (1.0 - vY) * pow(vEdge, 1.5) * (0.07 + uWet * 0.1);
          gl_FragColor = vec4(vec3(1.0, 0.78, 0.48) * a * uStreetLights, 1.0); }`,
      transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, side: THREE.DoubleSide,
    });
    const cones = new THREE.InstancedMesh(coneGeo, coneMat, lamps.length);
    lamps.forEach((l, i) => { hm.makeTranslation(l[0], l[2] + 7.9, l[1]); cones.setMatrixAt(i, hm); });
    cones.renderOrder = 3;
    cones.name = 'lampcones';
    cones.computeBoundingSphere();
    this.root.add(cones);
    this.lampFx = [halos, cones];

    // a few real point lights that follow the nearest street lamps at night
    this.lampLights = [];
    for (let i = 0; i < 6; i++) {
      const l = new THREE.PointLight(0xffc27a, 0, 34, 1.6);
      l.position.set(0, -100, 0);
      this.root.add(l);
      this.lampLights.push(l);
    }
    this._lampTimer = 0;

    // aviation beacons (blinking red)
    if (this.beacons && this.beacons.length) {
      const bg = new THREE.SphereGeometry(0.35, 8, 6);
      this.beaconMat = new THREE.MeshBasicMaterial({ color: new THREE.Color(8, 0.3, 0.2) });
      const bm = new THREE.InstancedMesh(bg, this.beaconMat, this.beacons.length);
      this.beacons.forEach((b, i) => { m.makeTranslation(b.x, b.y, b.z); bm.setMatrixAt(i, m); });
      bm.name = 'beacons';
      this.root.add(bm);
    }
  }

  // Hide a prop instance (after it's knocked over). Returns the instance.
  breakProp(collider) {
    const inst = collider.prop;
    if (!inst || inst.broken) return null;
    inst.broken = true;
    collider.broken = true;
    const meshes = inst.chunk.instMeshes[inst.type];
    for (const im of meshes) { im.setMatrixAt(inst.index, this.zeroMatrix); im.instanceMatrix.needsUpdate = true; }
    return inst;
  }
  restoreProp(inst) {
    if (!inst.broken) return;
    inst.broken = false;
    inst.collider.broken = false;
    const m = new THREE.Matrix4().compose(new THREE.Vector3(inst.x, inst.y, inst.z), new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 1, 0), inst.rot), new THREE.Vector3().setScalar(inst.scale));
    for (const im of inst.chunk.instMeshes[inst.type]) { im.setMatrixAt(inst.index, m); im.instanceMatrix.needsUpdate = true; }
  }
  propGeometry(type) { return this.propDefs[type]?.parts; }

  update(dt, camPos, night) {
    // prop visibility by distance
    for (const c of this.chunks.values()) {
      const d = Math.hypot(c.cx - camPos.x, c.cz - camPos.z);
      c.propGroup.visible = d < 520 + Math.max(0, camPos.y - 60);
    }
    this.terrainMesh?.update(dt, camPos);
    this.vegetation?.update(dt, camPos);
    // keep the (huge) ocean plane centred near the camera
    if (this.water) this.water.position.set(Math.round(camPos.x / 500) * 500, WATER_Y, Math.round(camPos.z / 500) * 500);
    if (this.billboardMat) this.billboardMat.emissiveIntensity = 0.05 + night * 1.6;
    for (const m of this.signMats) m.color.setScalar(0.55 + night * 2.2);
    if (this.beaconMat) {
      const on = (performance.now() / 1000) % 1.6 < 0.25 ? 1 : 0.05;
      this.beaconMat.color.setRGB(8 * on, 0.3 * on, 0.2 * on);
    }
    if (this.landmarks) this.landmarks.update(dt, night);
    this._lampTimer -= dt;
    const sl = U.uStreetLights.value;
    if (this._lampTimer <= 0 && this.lampPositions) {
      this._lampTimer = 0.4;
      const near = [];
      for (const l of this.lampPositions) {
        const d = (l[0] - camPos.x) ** 2 + (l[1] - camPos.z) ** 2;
        if (d > 110 * 110) continue;
        near.push([d, l]);
      }
      near.sort((a, b) => a[0] - b[0]);
      this.lampLights.forEach((L, i) => { const n = near[i]; if (n) L.position.set(n[1][0], n[1][2] + 7.6, n[1][1]); else L.position.set(0, -100, 0); });
    }
    for (const L of this.lampLights) L.intensity = sl * 140;
    if (this.lampFx) for (const m of this.lampFx) m.visible = sl > 0.01;
  }
}
