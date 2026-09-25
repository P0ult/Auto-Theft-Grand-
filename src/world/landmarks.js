// Hand-placed landmarks: Santa Luz pier + Ferris wheel, Vistawood sign, dock cranes, cargo ship.
import * as THREE from 'three';
import { GeoBuilder, mat4 } from './geom.js';
import { std } from '../render/materials.js';
import { NOISE_GLSL } from './shaders.js';
import { letterTexture } from './textures.js';
import { CITY, WATER_Y } from './citymap.js';
import { RNG } from '../core/utils.js';

export function buildLandmarks(city, map) {
  const root = new THREE.Group();
  root.name = 'landmarks';
  city.root.add(root);
  const updaters = [];
  const col = city.collision;
  const detail = city.mats.detail;

  // ---------------------------------------------------------- Pier
  const P = map.landmarks.pier;
  {
    const wood = std({ color: 0xffffff, roughness: 0.85 }, {
      key: 'pierwood', fragPars: NOISE_GLSL,
      fragColor: `{ vec2 wp = vAtgWorld.xz; float plank = fract(wp.y / 0.3); float n = vn2(vec2(floor(wp.y / 0.3) * 3.1, wp.x * 0.3));
        vec3 c = vec3(0.42, 0.31, 0.21) * (0.7 + 0.5 * n) * (1.0 - 0.35 * step(plank, 0.08)); diffuseColor.rgb = c * (1.0 - uWet * 0.3); }`,
    });
    const gb = new GeoBuilder({ position: 3, normal: 3, uv: 2 });
    const ramp = 24;
    const y = P.y;
    const x0 = P.x0, x1 = P.x1;
    // ramp (sloped quad) from ground to deck height
    const zA = P.z0, zB = P.z0 + ramp;
    const yA = Math.max(map.terrainHeight((x0 + x1) / 2, zA), 0.1);
    const nrm = new THREE.Vector3(0, ramp, -(y - yA)).normalize().toArray();
    gb.quad([x0, yA, zB - ramp], [x1, yA, zB - ramp], [x1, y, zB], [x0, y, zB], nrm);
    gb.quad([x0, y, P.z1], [x1, y, P.z1], [x1, y, zB], [x0, y, zB], [0, 1, 0]);
    // deck thickness edges
    gb.box(x0, y - 0.5, zB, x1, y, P.z1, { top: false, sides: true });
    const deck = new THREE.Mesh(gb.build(), wood);
    deck.receiveShadow = true; deck.castShadow = true;
    root.add(deck);
    // posts & rails
    const posts = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
    posts.set('color', 0.3, 0.24, 0.18);
    for (let z = zB; z < P.z1; z += 8) for (const x of [x0 + 0.6, (x0 + x1) / 2, x1 - 0.6]) posts.addGeometry(new THREE.CylinderGeometry(0.3, 0.3, y + 10, 8), mat4(x, (y - 10) / 2, z));
    posts.set('color', 0.85, 0.85, 0.82);
    for (const x of [x0 + 0.1, x1 - 0.1]) {
      posts.box(x - 0.06, y, zB, x + 0.06, y + 1.1, P.z1, { top: true });
      for (let z = zB; z < P.z1; z += 2.5) posts.box(x - 0.05, y, z - 0.05, x + 0.05, y + 1.1, z + 0.05, { top: true });
      col.addBox({ minX: x - 0.2, maxX: x + 0.2, minZ: zB, maxZ: P.z1, minY: y - 0.5, maxY: y + 1.1, type: 'fence', soft: true });
    }
    col.addBox({ minX: x0, maxX: x1, minZ: P.z1 - 0.3, maxZ: P.z1 + 0.3, minY: y - 0.5, maxY: y + 1.1, type: 'fence', soft: true });
    // food booths along the pier
    const rng = new RNG(31);
    for (let z = zB + 30; z < P.z1 - 70; z += 36) {
      const side = rng.chance(0.5) ? x0 + 2 : x1 - 6;
      const c = new THREE.Color().setHSL(rng.next(), 0.6, 0.55);
      posts.set('color', c.r, c.g, c.b);
      posts.box(side, y, z, side + 4, y + 3, z + 5, { top: true });
      posts.set('color', 0.95, 0.95, 0.95);
      posts.box(side - 0.3, y + 3, z - 0.3, side + 4.3, y + 3.3, z + 5.3, { top: true });
      col.addBox({ minX: side, maxX: side + 4, minZ: z, maxZ: z + 5, minY: y, maxY: y + 3.3, type: 'building' });
    }
    const pm = new THREE.Mesh(posts.build(), detail);
    pm.castShadow = true;
    root.add(pm);
    // pier lamps
    const lampGeo = new THREE.SphereGeometry(0.28, 10, 8);
    const lampMat = new THREE.MeshBasicMaterial({ color: 0xffffff });
    const lamps = new THREE.InstancedMesh(lampGeo, lampMat, 40);
    const poleGB = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
    poleGB.set('color', 0.2, 0.22, 0.25);
    let li = 0;
    const m = new THREE.Matrix4();
    for (let z = zB + 10; z < P.z1 && li < 40; z += 20) {
      for (const x of [x0 + 0.4, x1 - 0.4]) {
        if (li >= 40) break;
        poleGB.addGeometry(new THREE.CylinderGeometry(0.07, 0.09, 4, 6), mat4(x, y + 2, z));
        m.makeTranslation(x, y + 4.1, z); lamps.setMatrixAt(li++, m);
      }
    }
    lamps.count = li;
    root.add(lamps);
    root.add(new THREE.Mesh(poleGB.build(), detail));
    updaters.push((dt, night) => { lampMat.color.setRGB(0.4 + night * 5, 0.35 + night * 4, 0.25 + night * 2.5); });

    // ------------------------------------------------------- Ferris wheel
    const F = map.landmarks.ferris;
    const R = 20;
    const hubY = y + R + 4;
    const wheel = new THREE.Group();
    wheel.position.set(F.x, hubY, F.z);
    const steel = std({ color: 0xf2f2f2, roughness: 0.4, metalness: 0.6 }, { key: 'steel' });
    const wg = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
    wg.set('color', 1, 1, 1);
    for (const zOff of [-1.6, 1.6]) {
      wg.addGeometry(new THREE.TorusGeometry(R, 0.25, 6, 64), mat4(0, 0, zOff));
      wg.addGeometry(new THREE.TorusGeometry(R * 0.55, 0.15, 6, 48), mat4(0, 0, zOff));
      for (let k = 0; k < 16; k++) {
        const a = k / 16 * Math.PI * 2;
        wg.addGeometry(new THREE.CylinderGeometry(0.1, 0.1, R, 4), mat4(Math.cos(a) * R / 2, Math.sin(a) * R / 2, zOff, 0, 0, a - Math.PI / 2));
      }
    }
    wg.addGeometry(new THREE.CylinderGeometry(1.0, 1.0, 4.4, 12), mat4(0, 0, 0, Math.PI / 2, 0, 0));
    const wheelMesh = new THREE.Mesh(wg.build(), steel);
    wheelMesh.castShadow = true;
    wheel.add(wheelMesh);
    // neon rim lights
    const neonGeo = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
    for (let k = 0; k < 96; k++) {
      const a = k / 96 * Math.PI * 2;
      const c = new THREE.Color().setHSL(k / 96, 1, 0.5);
      neonGeo.set('color', c.r, c.g, c.b);
      neonGeo.addGeometry(new THREE.SphereGeometry(0.22, 6, 4), mat4(Math.cos(a) * R, Math.sin(a) * R, 1.9));
      neonGeo.addGeometry(new THREE.SphereGeometry(0.22, 6, 4), mat4(Math.cos(a) * R, Math.sin(a) * R, -1.9));
    }
    const neonMat = new THREE.MeshBasicMaterial({ vertexColors: true, color: 0xffffff });
    wheel.add(new THREE.Mesh(neonGeo.build(), neonMat));
    // gondolas
    const gondolas = [];
    const gGeo = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
    gGeo.set('color', 1, 1, 1);
    gGeo.box(-1.1, -2.6, -1.1, 1.1, -0.6, 1.1, { top: true, bottom: true });
    gGeo.set('color', 0.3, 0.3, 0.3);
    gGeo.box(-1.3, -0.6, -1.3, 1.3, -0.4, 1.3, { top: true, bottom: true });
    gGeo.addGeometry(new THREE.CylinderGeometry(0.05, 0.05, 0.6, 4), mat4(0, -0.3, 0));
    const gondGeo = gGeo.build();
    for (let k = 0; k < 16; k++) {
      const c = new THREE.Color().setHSL(k / 16, 0.7, 0.5);
      const gm = new THREE.Mesh(gondGeo, std({ color: c, roughness: 0.5, metalness: 0.2 }, { key: 'gondola' }));
      gm.castShadow = true;
      root.add(gm);
      gondolas.push({ mesh: gm, a: k / 16 * Math.PI * 2 });
    }
    // support A-frame
    const sup = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
    sup.set('color', 0.95, 0.95, 0.95);
    for (const zOff of [-3.4, 3.4]) for (const sx of [-1, 1]) {
      const bx = sx * 9, by = -(R + 4) + y * 0;
      const len = Math.hypot(bx, R + 4);
      const ang = Math.atan2(bx, R + 4);
      sup.addGeometry(new THREE.CylinderGeometry(0.35, 0.5, len, 8), mat4(F.x + bx / 2, hubY - (R + 4) / 2, F.z + zOff, 0, 0, ang));
    }
    sup.addGeometry(new THREE.CylinderGeometry(0.4, 0.4, 7.2, 8), mat4(F.x, hubY, F.z, Math.PI / 2, 0, 0));
    const supMesh = new THREE.Mesh(sup.build(), steel);
    supMesh.castShadow = true;
    root.add(supMesh);
    root.add(wheel);
    col.addCircle({ x: F.x - 9, z: F.z - 3.4, r: 0.8, h: 30, type: 'prop' });
    col.addCircle({ x: F.x + 9, z: F.z - 3.4, r: 0.8, h: 30, type: 'prop' });
    col.addCircle({ x: F.x - 9, z: F.z + 3.4, r: 0.8, h: 30, type: 'prop' });
    col.addCircle({ x: F.x + 9, z: F.z + 3.4, r: 0.8, h: 30, type: 'prop' });
    let wheelA = 0;
    updaters.push((dt, night) => {
      wheelA += dt * 0.05;
      wheel.rotation.z = wheelA;
      for (const g of gondolas) {
        const a = g.a + wheelA;
        g.mesh.position.set(F.x + Math.cos(a) * R, hubY + Math.sin(a) * R, F.z);
      }
      const k = 0.3 + night * 3.5;
      const t = performance.now() / 1000;
      neonMat.color.setRGB(k * (0.8 + 0.2 * Math.sin(t * 3)), k, k * (0.8 + 0.2 * Math.cos(t * 2)));
    });
  }

  // ---------------------------------------------------------- Vistawood sign
  {
    const S = map.landmarks.sign;
    const word = 'VISTAWOOD';
    const group = new THREE.Group();
    const lg = new THREE.PlaneGeometry(13, 15);
    const scaff = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
    scaff.set('color', 0.35, 0.33, 0.3);
    for (let i = 0; i < word.length; i++) {
      const x = S.x + (i - (word.length - 1) / 2) * 15.5;
      const z = S.z + Math.sin(i * 0.7) * 3;
      const h = map.terrainHeight(x, z);
      const mat = std({ map: letterTexture(word[i]), alphaTest: 0.5, color: 0xf4f4f0, side: THREE.DoubleSide, roughness: 0.6, emissive: 0xffffff, emissiveIntensity: 0.0 }, { key: 'letter' });
      mat.emissiveMap = mat.map;
      const mesh = new THREE.Mesh(lg, mat);
      mesh.position.set(x, h + 8.5, z);
      mesh.rotation.x = -0.12;
      mesh.castShadow = true;
      group.add(mesh);
      for (const dx of [-4, 0, 4]) scaff.addGeometry(new THREE.CylinderGeometry(0.15, 0.15, 12, 5), mat4(x + dx, h + 5, z - 1.5, 0.15, 0, 0));
      updaters.push((dt, night) => { mat.emissiveIntensity = night * 0.9; });
    }
    group.add(new THREE.Mesh(scaff.build(), detail));
    root.add(group);
  }

  // ---------------------------------------------------------- Dock cranes & cargo ship
  {
    const steelRed = std({ color: 0xc0392b, roughness: 0.5, metalness: 0.4 }, { key: 'crane' });
    const cg = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
    cg.set('color', 1, 1, 1);
    const quayX = CITY.maxX + 34;
    const cranes = [60, 200, 340, 480];
    for (const z of cranes) {
      const x = quayX - 8;
      for (const [dx, dz] of [[-6, -5], [6, -5], [-6, 5], [6, 5]]) {
        cg.box(x + dx - 0.6, 0, z + dz - 0.6, x + dx + 0.6, 32, z + dz + 0.6, { top: true });
        col.addBox({ minX: x + dx - 0.6, maxX: x + dx + 0.6, minZ: z + dz - 0.6, maxZ: z + dz + 0.6, minY: 0, maxY: 32, type: 'building' });
      }
      cg.box(x - 7, 30, z - 6, x + 7, 34, z + 6, { top: true, bottom: true });
      cg.box(x - 30, 34, z - 2, x + 55, 37, z + 2, { top: true, bottom: true });
      cg.box(x + 2, 26, z - 3, x + 8, 30, z + 3, { top: true, bottom: true });
      for (let k = 0; k < 6; k++) cg.box(x - 2 + k * 0.1, 37, z - 0.2, x - 1.6 + k * 0.1, 48 - k * 2, z + 0.2, { top: true });
      cg.box(x - 3, 44, z - 1, x + 1, 50, z + 1, { top: true });
      city._beacon(x - 1, 50.6, z);
      city._beacon(x + 55, 37.6, z);
    }
    const cm = new THREE.Mesh(cg.build(), steelRed);
    cm.castShadow = true;
    root.add(cm);

    // cargo ship
    const shipX = quayX + 22, shipZ = 270, L = 170, W = 26;
    const hullShape = new THREE.Shape();
    hullShape.moveTo(-W / 2, -L / 2);
    hullShape.lineTo(W / 2, -L / 2);
    hullShape.lineTo(W / 2, L / 2 - 30);
    hullShape.quadraticCurveTo(W / 2, L / 2, 0, L / 2 + 8);
    hullShape.quadraticCurveTo(-W / 2, L / 2, -W / 2, L / 2 - 30);
    hullShape.lineTo(-W / 2, -L / 2);
    const hullGeo = new THREE.ExtrudeGeometry(hullShape, { depth: 16, bevelEnabled: false });
    hullGeo.rotateX(-Math.PI / 2);
    const hullMat = std({ color: 0x1d2b3a, roughness: 0.6, metalness: 0.3 }, { key: 'hull' });
    const deckMat = std({ color: 0x5a2a22, roughness: 0.8 }, { key: 'shipdeck' });
    const hull = new THREE.Mesh(hullGeo, [deckMat, hullMat]);
    hull.position.set(shipX, WATER_Y - 6, shipZ);
    hull.rotation.y = 0;
    hull.castShadow = true; hull.receiveShadow = true;
    root.add(hull);
    const deckGB = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
    deckGB.set('color', 0.92, 0.92, 0.9);
    deckGB.box(shipX - W / 2 + 1, WATER_Y + 10, shipZ + L / 2 - 18, shipX + W / 2 - 1, WATER_Y + 32, shipZ + L / 2 - 4, { top: true });
    deckGB.set('color', 0.2, 0.25, 0.3);
    deckGB.box(shipX - W / 2 + 1.5, WATER_Y + 28, shipZ + L / 2 - 18.2, shipX + W / 2 - 1.5, WATER_Y + 30, shipZ + L / 2 - 17.9, { top: true });
    deckGB.set('color', 0.8, 0.1, 0.1);
    deckGB.addGeometry(new THREE.CylinderGeometry(2.2, 2.6, 8, 12), mat4(shipX, WATER_Y + 36, shipZ + L / 2 - 10));
    const colors = [[0.69, 0.23, 0.18], [0.12, 0.38, 0.55], [0.07, 0.48, 0.4], [0.83, 0.67, 0.05], [0.42, 0.2, 0.51], [0.73, 0.29, 0]];
    const rng = new RNG(9);
    for (let z = shipZ - L / 2 + 32; z < shipZ + L / 2 - 26; z += 6.4) {
      for (let x = -3; x <= 3; x++) {
        const stack = rng.int(1, 4);
        for (let s = 0; s < stack; s++) {
          const c = rng.pick(colors);
          deckGB.set('color', c[0], c[1], c[2]);
          deckGB.box(shipX + x * 2.6 - 1.2, WATER_Y + 10 + s * 2.6, z, shipX + x * 2.6 + 1.2, WATER_Y + 12.55 + s * 2.6, z + 6, { top: true });
        }
      }
    }
    const deckMesh = new THREE.Mesh(deckGB.build(), detail);
    deckMesh.castShadow = true;
    root.add(deckMesh);
    col.addBox({ minX: shipX - W / 2, maxX: shipX + W / 2, minZ: shipZ - L / 2 - 8, maxZ: shipZ + L / 2, minY: -10, maxY: WATER_Y + 22, type: 'building' });
  }

  return {
    root,
    update(dt, night) { for (const u of updaters) u(dt, night); },
  };
}
