// Instanced vegetation: pine forests, oaks, bushes, saguaros, rocks, dead trees and palms, grouped in
// 512 m chunks with a detailed mesh up close and a cheap one in the distance. Trunks, cacti and
// boulders get collision circles.
import * as THREE from 'three';
import { std } from '../render/materials.js';
import * as P from './props.js';
import { palmFrondTexture } from './textures.js';

const CH = 512;

export class Vegetation {
  constructor(scene, map, collision) {
    this.root = new THREE.Group();
    this.root.name = 'vegetation';
    scene.add(this.root);
    this.chunks = new Map();
    const mat = std({ color: 0xffffff, vertexColors: true, roughness: 0.9 }, { key: 'veg' });
    const frond = std({ color: 0xffffff, vertexColors: true, map: palmFrondTexture(), alphaTest: 0.45, side: THREE.DoubleSide, roughness: 0.8 }, { key: 'frond' });
    const pine = P.pineGeos(), oak = P.oakGeos(), cactus = P.cactusGeos(), rock = P.rockGeos(), dead = P.deadtreeGeos(), bush = P.bushGeos(), palm = P.palmGeos(1);
    this.kinds = {
      pine: { near: [[pine.near, mat]], far: [[pine.far, mat]], r: 0.45, h: 12, nearD: 260, farD: 2200 },
      oak: { near: [[oak.near[0], mat], [oak.near[1], mat]], far: [[oak.far, mat]], r: 0.4, h: 6, nearD: 220, farD: 1700 },
      bush: { near: [[bush.near, mat]], far: [[bush.far, mat]], r: 0, h: 1.2, nearD: 140, farD: 600 },
      cactus: { near: [[cactus.near, mat]], far: [[cactus.far, mat]], r: 0.4, h: 5, nearD: 180, farD: 1200 },
      rock: { near: [[rock.near, mat]], far: [[rock.far, mat]], r: 1.3, h: 1.4, nearD: 200, farD: 1500 },
      deadtree: { near: [[dead.near, mat]], far: [[dead.far, mat]], r: 0.3, h: 4, nearD: 180, farD: 1000 },
      palm: { near: [[palm.trunk, mat], [palm.fronds, frond]], far: [[palm.trunk, mat], [palm.fronds, frond]], r: 0.35, h: 9, nearD: 400, farD: 1200 },
    };
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), s = new THREE.Vector3(), p = new THREE.Vector3(), up = new THREE.Vector3(0, 1, 0);
    for (const kind in map.vegetation) {
      const K = this.kinds[kind];
      const arr = map.vegetation[kind];
      if (!K || !arr) continue;
      // bucket by chunk
      const buckets = new Map();
      for (let i = 0; i < arr.length; i += 5) {
        const key = Math.floor(arr[i] / CH) + ',' + Math.floor(arr[i + 2] / CH);
        let b = buckets.get(key);
        if (!b) { b = []; buckets.set(key, b); }
        b.push(i);
        if (K.r > 0) {
          const sc = arr[i + 4];
          collision.addCircle({ x: arr[i], z: arr[i + 2], r: K.r * (kind === 'rock' ? sc : Math.min(1.3, sc)), h: arr[i + 1] + K.h * sc, y0: arr[i + 1] - 1, type: kind === 'rock' ? 'rock' : 'tree' });
        }
      }
      for (const [key, idxs] of buckets) {
        let c = this.chunks.get(key);
        if (!c) {
          const [ci, cj] = key.split(',').map(Number);
          c = { cx: (ci + 0.5) * CH, cz: (cj + 0.5) * CH, sets: [] };
          this.chunks.set(key, c);
        }
        const make = (parts, near) => parts.map(([geo, material]) => {
          const im = new THREE.InstancedMesh(geo, material, idxs.length);
          idxs.forEach((i, k) => {
            q.setFromAxisAngle(up, arr[i + 3]);
            const sc = arr[i + 4];
            s.set(sc, sc * (kind === 'rock' ? 0.8 + (i % 7) * 0.06 : 1), sc);
            p.set(arr[i], arr[i + 1], arr[i + 2]);
            m.compose(p, q, s);
            im.setMatrixAt(k, m);
          });
          im.castShadow = near;
          im.receiveShadow = true;
          im.computeBoundingSphere();
          im.visible = false;
          im.name = kind + (near ? ':near' : ':far');
          this.root.add(im);
          return im;
        });
        c.sets.push({ K, near: make(K.near, true), far: make(K.far, false) });
      }
    }
    this._t = 0;
  }

  update(dt, cam) {
    this._t -= dt;
    if (this._t > 0) return;
    this._t = 0.25;
    const alt = Math.max(0, cam.y - 40);
    for (const c of this.chunks.values()) {
      const dx = Math.max(Math.abs(cam.x - c.cx) - CH / 2, 0), dz = Math.max(Math.abs(cam.z - c.cz) - CH / 2, 0);
      const d = Math.hypot(dx, dz);
      for (const set of c.sets) {
        const nearOn = d < set.K.nearD && alt < 260;
        const farOn = !nearOn && d < set.K.farD + alt * 1.5;
        for (const im of set.near) im.visible = nearOn;
        for (const im of set.far) im.visible = farOn;
      }
    }
  }
}
