// Visual effects: GPU-instanced billboard particles (smoke, fire, sparks, blood, dust, water),
// bullet tracers, skid marks, decals (bullet holes, blood pools, scorch marks), flash lights, prop debris.
import * as THREE from 'three';
import { U, FOG_GLSL } from '../render/materials.js';
import { smokeTexture, softDotTexture } from '../world/textures.js';
import { clamp, rand } from '../core/utils.js';

const PVERT = /* glsl */`
attribute vec3 iPos; attribute vec4 iColor; attribute vec2 iSizeRot;
varying vec2 vUv; varying vec4 vColor; varying vec3 vWorld;
void main() {
  vUv = uv;
  vColor = iColor;
  vec4 mv = viewMatrix * vec4(iPos, 1.0);
  float s = iSizeRot.x, r = iSizeRot.y;
  vec2 p = position.xy;
  p = vec2(p.x * cos(r) - p.y * sin(r), p.x * sin(r) + p.y * cos(r));
  mv.xy += p * s;
  vWorld = iPos;
  gl_Position = projectionMatrix * mv;
}
`;
const PFRAG = /* glsl */`
uniform sampler2D map; uniform vec3 uLight; uniform float uAdditive;
varying vec2 vUv; varying vec4 vColor; varying vec3 vWorld;
${FOG_GLSL}
void main() {
  vec4 t = texture2D(map, vUv);
  vec3 col = vColor.rgb * (uAdditive > 0.5 ? vec3(1.0) : uLight);
  float a = t.a * vColor.a;
  if (a < 0.003) discard;
  if (uAdditive > 0.5) {
    gl_FragColor = vec4(col * t.rgb * a, 1.0);
  } else {
    gl_FragColor = vec4(atgFog(col * t.rgb, vWorld), a);
  }
}
`;

class ParticlePool {
  constructor(scene, max, texture, additive) {
    this.max = max;
    this.n = 0;
    const g = new THREE.InstancedBufferGeometry();
    const base = new THREE.PlaneGeometry(1, 1);
    g.index = base.index;
    g.setAttribute('position', base.attributes.position);
    g.setAttribute('uv', base.attributes.uv);
    this.pos = new Float32Array(max * 3);
    this.col = new Float32Array(max * 4);
    this.sr = new Float32Array(max * 2);
    this.aPos = new THREE.InstancedBufferAttribute(this.pos, 3).setUsage(THREE.DynamicDrawUsage);
    this.aCol = new THREE.InstancedBufferAttribute(this.col, 4).setUsage(THREE.DynamicDrawUsage);
    this.aSR = new THREE.InstancedBufferAttribute(this.sr, 2).setUsage(THREE.DynamicDrawUsage);
    g.setAttribute('iPos', this.aPos);
    g.setAttribute('iColor', this.aCol);
    g.setAttribute('iSizeRot', this.aSR);
    g.instanceCount = 0;
    this.uniforms = {
      map: { value: texture }, uLight: { value: new THREE.Color(1, 1, 1) }, uAdditive: { value: additive ? 1 : 0 },
      uFogColor: U.uFogColor, uFogSunColor: U.uFogSunColor, uSunDir: U.uSunDir, uFogDensity: U.uFogDensity, uFogHeightFalloff: U.uFogHeightFalloff,
    };
    const m = new THREE.ShaderMaterial({
      vertexShader: PVERT, fragmentShader: PFRAG, uniforms: this.uniforms,
      transparent: true, depthWrite: false,
      blending: additive ? THREE.AdditiveBlending : THREE.NormalBlending,
    });
    this.mesh = new THREE.Mesh(g, m);
    this.mesh.frustumCulled = false;
    this.mesh.renderOrder = additive ? 6 : 5;
    scene.add(this.mesh);
    this.parts = [];
  }
  spawn(p) {
    if (this.parts.length >= this.max) this.parts.shift();
    this.parts.push(p);
  }
  update(dt) {
    const P = this.parts;
    let w = 0;
    for (let i = 0; i < P.length; i++) {
      const p = P[i];
      p.age += dt;
      if (p.age >= p.life) continue;
      const t = p.age / p.life;
      p.vx *= 1 - p.drag * dt; p.vy *= 1 - p.drag * dt; p.vz *= 1 - p.drag * dt;
      p.vy += p.grav * dt;
      p.x += p.vx * dt; p.y += p.vy * dt; p.z += p.vz * dt;
      if (p.floor != null && p.y < p.floor) { p.y = p.floor; p.vy *= -0.2; p.vx *= 0.6; p.vz *= 0.6; if (p.onFloor) { p.onFloor(p); p.onFloor = null; } }
      p.rot += p.spin * dt;
      P[w++] = p;
      const k = (w - 1);
      this.pos[k * 3] = p.x; this.pos[k * 3 + 1] = p.y; this.pos[k * 3 + 2] = p.z;
      const size = p.size0 + (p.size1 - p.size0) * t;
      const a = p.alpha * (t < p.fadeIn ? t / p.fadeIn : 1) * (1 - Math.pow(t, p.fadePow));
      const c = p.color;
      const c1 = p.color1 || c;
      this.col[k * 4] = c[0] + (c1[0] - c[0]) * t; this.col[k * 4 + 1] = c[1] + (c1[1] - c[1]) * t; this.col[k * 4 + 2] = c[2] + (c1[2] - c[2]) * t; this.col[k * 4 + 3] = a;
      this.sr[k * 2] = size; this.sr[k * 2 + 1] = p.rot;
    }
    P.length = w;
    this.mesh.geometry.instanceCount = w;
    this.aPos.needsUpdate = true; this.aCol.needsUpdate = true; this.aSR.needsUpdate = true;
    this.aPos.clearUpdateRanges?.(); this.aCol.clearUpdateRanges?.(); this.aSR.clearUpdateRanges?.();
    this.aPos.addUpdateRange?.(0, w * 3); this.aCol.addUpdateRange?.(0, w * 4); this.aSR.addUpdateRange?.(0, w * 2);
  }
}

function P(o) {
  return Object.assign({ x: 0, y: 0, z: 0, vx: 0, vy: 0, vz: 0, age: 0, life: 1, size0: 0.5, size1: 1, rot: Math.random() * 6.28, spin: rand(-1, 1), grav: 0, drag: 0.5, alpha: 1, fadeIn: 0.05, fadePow: 1.5, color: [1, 1, 1], color1: null, floor: null }, o);
}

// ------------------------------------------------------------------ decals
function decalAtlas() {
  const c = document.createElement('canvas');
  c.width = 512; c.height = 128;
  const g = c.getContext('2d');
  g.clearRect(0, 0, 512, 128);
  // 0: bullet hole
  let grd = g.createRadialGradient(64, 64, 0, 64, 64, 60);
  grd.addColorStop(0, 'rgba(10,10,10,1)'); grd.addColorStop(0.18, 'rgba(20,20,20,1)'); grd.addColorStop(0.3, 'rgba(60,60,60,0.7)'); grd.addColorStop(1, 'rgba(60,60,60,0)');
  g.fillStyle = grd; g.beginPath(); g.arc(64, 64, 60, 0, Math.PI * 2); g.fill();
  // 1: blood pool
  for (let i = 0; i < 14; i++) {
    const x = 192 + (Math.random() - 0.5) * 60, y = 64 + (Math.random() - 0.5) * 60, r = 10 + Math.random() * 30;
    grd = g.createRadialGradient(x, y, 0, x, y, r);
    grd.addColorStop(0, 'rgba(90,0,0,0.95)'); grd.addColorStop(0.7, 'rgba(70,0,0,0.9)'); grd.addColorStop(1, 'rgba(60,0,0,0)');
    g.fillStyle = grd; g.beginPath(); g.arc(x, y, r, 0, Math.PI * 2); g.fill();
  }
  // 2: scorch
  grd = g.createRadialGradient(320, 64, 0, 320, 64, 62);
  grd.addColorStop(0, 'rgba(5,5,5,0.95)'); grd.addColorStop(0.6, 'rgba(15,12,10,0.7)'); grd.addColorStop(1, 'rgba(20,15,10,0)');
  g.fillStyle = grd; g.beginPath(); g.arc(320, 64, 62, 0, Math.PI * 2); g.fill();
  // 3: blood splat (small)
  for (let i = 0; i < 20; i++) {
    const a = Math.random() * 6.28, d = Math.random() * 45, r = 3 + Math.random() * 10;
    g.fillStyle = 'rgba(110,0,0,0.9)'; g.beginPath(); g.arc(448 + Math.cos(a) * d, 64 + Math.sin(a) * d, r, 0, Math.PI * 2); g.fill();
  }
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

class Decals {
  constructor(scene, max = 400) {
    this.max = max;
    const g = new THREE.PlaneGeometry(1, 1);
    this.uvOff = new Float32Array(max);
    this.alpha = new Float32Array(max);
    g.setAttribute('iTile', new THREE.InstancedBufferAttribute(this.uvOff, 1).setUsage(THREE.DynamicDrawUsage));
    g.setAttribute('iAlpha', new THREE.InstancedBufferAttribute(this.alpha, 1).setUsage(THREE.DynamicDrawUsage));
    const m = new THREE.ShaderMaterial({
      uniforms: { map: { value: decalAtlas() }, uLight: { value: new THREE.Color(1, 1, 1) } },
      vertexShader: `attribute float iTile; attribute float iAlpha; varying vec2 vUv; varying float vA;
        void main(){ vUv = vec2((uv.x + iTile) / 4.0, uv.y); vA = iAlpha; gl_Position = projectionMatrix * modelViewMatrix * instanceMatrix * vec4(position, 1.0); }`,
      fragmentShader: `uniform sampler2D map; uniform vec3 uLight; varying vec2 vUv; varying float vA;
        void main(){ vec4 t = texture2D(map, vUv); float a = t.a * vA; if (a < 0.01) discard; gl_FragColor = vec4(t.rgb * uLight, a); }`,
      transparent: true, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -4, polygonOffsetUnits: -4,
    });
    this.mesh = new THREE.InstancedMesh(g, m, max);
    this.mesh.count = 0;
    this.mesh.frustumCulled = false;
    this.mesh.renderOrder = 2;
    scene.add(this.mesh);
    this.next = 0;
    this.items = [];
    this._m = new THREE.Matrix4(); this._q = new THREE.Quaternion(); this._s = new THREE.Vector3(); this._p = new THREE.Vector3();
  }
  add(pos, normal, size, tile, opts = {}) {
    const i = this.next;
    this.next = (this.next + 1) % this.max;
    this.mesh.count = Math.max(this.mesh.count, i + 1);
    this._q.setFromUnitVectors(new THREE.Vector3(0, 0, 1), normal);
    const spin = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 0, 1), Math.random() * 6.28);
    this._q.multiply(spin);
    this._p.copy(pos).addScaledVector(normal, 0.02);
    this._s.set(size, size, 1);
    this._m.compose(this._p, this._q, this._s);
    this.mesh.setMatrixAt(i, this._m);
    this.uvOff[i] = tile;
    this.alpha[i] = opts.alpha ?? 1;
    this.items[i] = { t: 0, grow: opts.grow || 0, size, target: opts.targetSize || size, life: opts.life || 120, pos: this._p.clone(), q: this._q.clone() };
    this.mesh.instanceMatrix.needsUpdate = true;
    this.mesh.geometry.attributes.iTile.needsUpdate = true;
    this.mesh.geometry.attributes.iAlpha.needsUpdate = true;
  }
  update(dt, light) {
    this.mesh.material.uniforms.uLight.value.copy(light);
    let dirty = false;
    for (let i = 0; i < this.items.length; i++) {
      const it = this.items[i];
      if (!it) continue;
      it.t += dt;
      if (it.grow && it.size < it.target) {
        it.size = Math.min(it.target, it.size + it.grow * dt);
        this._s.set(it.size, it.size, 1);
        this._m.compose(it.pos, it.q, this._s);
        this.mesh.setMatrixAt(i, this._m);
        dirty = true;
      }
      if (it.t > it.life) {
        const a = Math.max(0, 1 - (it.t - it.life) / 5);
        this.alpha[i] = a;
        this.mesh.geometry.attributes.iAlpha.needsUpdate = true;
        if (a <= 0) this.items[i] = null;
      }
    }
    if (dirty) this.mesh.instanceMatrix.needsUpdate = true;
  }
}

// ------------------------------------------------------------------ skid marks
class SkidMarks {
  constructor(scene, max = 1500) {
    this.max = max;
    this.pos = new Float32Array(max * 4 * 3);
    this.alpha = new Float32Array(max * 4);
    const idx = [];
    for (let i = 0; i < max; i++) { const b = i * 4; idx.push(b, b + 1, b + 2, b, b + 2, b + 3); }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(this.pos, 3).setUsage(THREE.DynamicDrawUsage));
    g.setAttribute('aAlpha', new THREE.BufferAttribute(this.alpha, 1).setUsage(THREE.DynamicDrawUsage));
    g.setIndex(idx);
    const m = new THREE.ShaderMaterial({
      vertexShader: 'attribute float aAlpha; varying float vA; void main(){ vA = aAlpha; gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.0); }',
      fragmentShader: 'varying float vA; void main(){ gl_FragColor = vec4(0.02,0.02,0.02, vA * 0.75); }',
      transparent: true, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -3, polygonOffsetUnits: -6,
    });
    this.mesh = new THREE.Mesh(g, m);
    this.mesh.frustumCulled = false;
    this.mesh.renderOrder = 1;
    scene.add(this.mesh);
    this.next = 0;
    this.last = new Map();
  }
  add(key, x, y, z, w, strength) {
    const prev = this.last.get(key);
    this.last.set(key, { x, y, z, t: performance.now() });
    if (!prev || performance.now() - prev.t > 120) return;
    const dx = x - prev.x, dz = z - prev.z;
    const len = Math.hypot(dx, dz);
    if (len < 0.05 || len > 4) return;
    const nx = -dz / len * w / 2, nz = dx / len * w / 2;
    const i = this.next;
    this.next = (this.next + 1) % this.max;
    const p = this.pos, b = i * 12;
    const y0 = prev.y + 0.02, y1 = y + 0.02;
    p[b] = prev.x - nx; p[b + 1] = y0; p[b + 2] = prev.z - nz;
    p[b + 3] = prev.x + nx; p[b + 4] = y0; p[b + 5] = prev.z + nz;
    p[b + 6] = x + nx; p[b + 7] = y1; p[b + 8] = z + nz;
    p[b + 9] = x - nx; p[b + 10] = y1; p[b + 11] = z - nz;
    const a = clamp(strength, 0, 1);
    this.alpha.fill(a, i * 4, i * 4 + 4);
    this.mesh.geometry.attributes.position.needsUpdate = true;
    this.mesh.geometry.attributes.aAlpha.needsUpdate = true;
  }
  break(key) { this.last.delete(key); }
}

// ------------------------------------------------------------------ tracers
class Tracers {
  constructor(scene, max = 64) {
    this.max = max;
    this.pos = new Float32Array(max * 6);
    this.col = new Float32Array(max * 6);
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(this.pos, 3).setUsage(THREE.DynamicDrawUsage));
    g.setAttribute('color', new THREE.BufferAttribute(this.col, 3).setUsage(THREE.DynamicDrawUsage));
    this.mesh = new THREE.LineSegments(g, new THREE.LineBasicMaterial({ vertexColors: true, transparent: true, blending: THREE.AdditiveBlending, depthWrite: false }));
    this.mesh.frustumCulled = false;
    this.mesh.renderOrder = 7;
    scene.add(this.mesh);
    this.items = [];
    this.next = 0;
  }
  add(a, b) {
    const i = this.next; this.next = (this.next + 1) % this.max;
    this.items[i] = { a: a.clone(), b: b.clone(), t: 0 };
  }
  update(dt) {
    for (let i = 0; i < this.max; i++) {
      const it = this.items[i];
      const k = i * 6;
      if (!it) { this.col.fill(0, k, k + 6); continue; }
      it.t += dt;
      const life = 0.07;
      const f = Math.max(0, 1 - it.t / life);
      if (f <= 0) { this.items[i] = null; this.col.fill(0, k, k + 6); continue; }
      // a short streak moving from a to b
      const s = Math.min(1, it.t / life * 1.4);
      const x0 = it.a.x + (it.b.x - it.a.x) * Math.max(0, s - 0.35), y0 = it.a.y + (it.b.y - it.a.y) * Math.max(0, s - 0.35), z0 = it.a.z + (it.b.z - it.a.z) * Math.max(0, s - 0.35);
      const x1 = it.a.x + (it.b.x - it.a.x) * s, y1 = it.a.y + (it.b.y - it.a.y) * s, z1 = it.a.z + (it.b.z - it.a.z) * s;
      this.pos[k] = x0; this.pos[k + 1] = y0; this.pos[k + 2] = z0; this.pos[k + 3] = x1; this.pos[k + 4] = y1; this.pos[k + 5] = z1;
      this.col[k] = 2.5 * f; this.col[k + 1] = 1.8 * f; this.col[k + 2] = 0.8 * f;
      this.col[k + 3] = 3 * f; this.col[k + 4] = 2.4 * f; this.col[k + 5] = 1.2 * f;
    }
    this.mesh.geometry.attributes.position.needsUpdate = true;
    this.mesh.geometry.attributes.color.needsUpdate = true;
  }
}

// ------------------------------------------------------------------ main effects system
export class Effects {
  constructor(game) {
    this.game = game;
    const scene = game.scene;
    const smoke = smokeTexture(), dot = softDotTexture();
    this.alphaPool = new ParticlePool(scene, 2500, smoke, false);
    this.addPool = new ParticlePool(scene, 2000, dot, true);
    this.dotAlpha = new ParticlePool(scene, 1200, dot, false);
    this.decals = new Decals(scene);
    this.skids = new SkidMarks(scene);
    this.tracers = new Tracers(scene);
    this.lights = [];
    for (let i = 0; i < 3; i++) {
      const l = new THREE.PointLight(0xffaa55, 0, 30, 1.6);
      scene.add(l);
      this.lights.push({ light: l, t: 0, life: 0, peak: 0 });
    }
    this.debris = [];
    this.emitters = [];
    this.lightColor = new THREE.Color();
  }

  flash(pos, color, intensity, life, range = 30) {
    let best = this.lights[0];
    for (const l of this.lights) if (l.light.intensity < best.light.intensity) best = l;
    best.light.position.copy(pos);
    best.light.color.set(color);
    best.light.distance = range;
    best.peak = intensity; best.t = 0; best.life = life;
    best.light.intensity = intensity;
  }

  // ----------------------------------------------------------- emitters
  muzzleFlash(pos, dir, big = false) {
    const s = big ? 1.4 : 1;
    for (let i = 0; i < 3; i++) {
      this.addPool.spawn(P({ x: pos.x + dir.x * i * 0.08, y: pos.y + dir.y * i * 0.08, z: pos.z + dir.z * i * 0.08, life: 0.05, size0: (0.35 - i * 0.08) * s, size1: (0.5 - i * 0.1) * s, color: [6, 3.6, 1.2], alpha: 1, fadePow: 1, spin: 0 }));
    }
    this.alphaPool.spawn(P({ x: pos.x, y: pos.y, z: pos.z, vx: dir.x * 1.5, vy: 0.4, vz: dir.z * 1.5, life: 0.6, size0: 0.15, size1: 0.6, color: [0.6, 0.6, 0.6], alpha: 0.25 }));
    this.flash(pos, 0xffb060, big ? 18 : 10, 0.06, 14);
  }

  impact(pos, normal, kind = 'concrete') {
    const n = normal;
    if (kind === 'metal') {
      for (let i = 0; i < 6; i++) this.addPool.spawn(P({ x: pos.x, y: pos.y, z: pos.z, vx: n.x * 3 + rand(-3, 3), vy: n.y * 3 + rand(0, 4), vz: n.z * 3 + rand(-3, 3), life: rand(0.15, 0.4), size0: 0.06, size1: 0.02, color: [5, 3, 1], grav: -12, drag: 1 }));
    } else {
      for (let i = 0; i < 4; i++) this.alphaPool.spawn(P({ x: pos.x, y: pos.y, z: pos.z, vx: n.x * 2 + rand(-1, 1), vy: n.y * 2 + rand(0, 1.5), vz: n.z * 2 + rand(-1, 1), life: rand(0.5, 0.9), size0: 0.1, size1: 0.55, color: [0.55, 0.52, 0.48], alpha: 0.5, grav: -1.5, drag: 2.5 }));
      for (let i = 0; i < 3; i++) this.dotAlpha.spawn(P({ x: pos.x, y: pos.y, z: pos.z, vx: n.x * 3 + rand(-2, 2), vy: n.y * 3 + rand(1, 3), vz: n.z * 3 + rand(-2, 2), life: 0.5, size0: 0.05, size1: 0.03, color: [0.3, 0.28, 0.25], grav: -12, drag: 0.5 }));
    }
    this.decals.add(pos, n, 0.14 + Math.random() * 0.05, 0, { life: 60 });
  }

  blood(pos, dir, amount = 8) {
    const floor = this.game.map.groundHeight(pos.x, pos.z) + 0.02;
    for (let i = 0; i < amount; i++) {
      const sp = rand(1, 4);
      this.dotAlpha.spawn(P({
        x: pos.x, y: pos.y, z: pos.z,
        vx: dir.x * sp + rand(-1.2, 1.2), vy: dir.y * sp + rand(0, 2), vz: dir.z * sp + rand(-1.2, 1.2),
        life: rand(0.5, 1.0), size0: rand(0.05, 0.1), size1: 0.04, color: [0.45, 0.0, 0.0], alpha: 0.95, grav: -12, drag: 0.4, floor,
        onFloor: (p) => { if (Math.random() < 0.3) this.decals.add(new THREE.Vector3(p.x, floor, p.z), UP, rand(0.15, 0.35), 3, { life: 90 }); },
      }));
    }
    this.alphaPool.spawn(P({ x: pos.x, y: pos.y, z: pos.z, vx: dir.x, vy: 0.2, vz: dir.z, life: 0.35, size0: 0.15, size1: 0.6, color: [0.5, 0.0, 0.0], alpha: 0.6 }));
  }

  bloodPool(pos) {
    const y = this.game.map.groundHeight(pos.x, pos.z) + 0.01;
    this.decals.add(new THREE.Vector3(pos.x, y, pos.z), UP, 0.3, 1, { grow: 0.18, targetSize: rand(1.4, 2.2), life: 150 });
  }

  sparks(pos, strength = 8) {
    const n = Math.min(24, 4 + strength);
    for (let i = 0; i < n; i++) this.addPool.spawn(P({ x: pos.x, y: pos.y, z: pos.z, vx: rand(-5, 5), vy: rand(0, 5), vz: rand(-5, 5), life: rand(0.2, 0.6), size0: 0.08, size1: 0.02, color: [6, 3.5, 1], grav: -14, drag: 0.8, floor: this.game.map.groundHeight(pos.x, pos.z) }));
    this.flash(pos, 0xffc070, 4, 0.08, 8);
  }

  tireSmoke(pos, amount = 1, color = 0.85) {
    this.alphaPool.spawn(P({ x: pos.x + rand(-0.2, 0.2), y: pos.y + 0.2, z: pos.z + rand(-0.2, 0.2), vx: rand(-0.5, 0.5), vy: rand(0.3, 1.0), vz: rand(-0.5, 0.5), life: rand(1.2, 2.2), size0: 0.5, size1: 3.2 * amount, color: [color, color, color], alpha: 0.35 * amount, drag: 1.2, fadeIn: 0.1 }));
  }

  dust(pos, amount = 1) {
    this.alphaPool.spawn(P({ x: pos.x, y: pos.y + 0.1, z: pos.z, vx: rand(-1, 1), vy: rand(0.2, 0.8), vz: rand(-1, 1), life: rand(0.8, 1.5), size0: 0.4, size1: 2.2 * amount, color: [0.55, 0.48, 0.38], alpha: 0.4, drag: 1.5 }));
  }

  engineSmoke(pos, dark = 0) {
    const c = 0.7 - dark * 0.6;
    this.alphaPool.spawn(P({ x: pos.x + rand(-0.3, 0.3), y: pos.y, z: pos.z + rand(-0.3, 0.3), vx: rand(-0.3, 0.3), vy: rand(1.2, 2.2), vz: rand(-0.3, 0.3), life: rand(1.5, 2.5), size0: 0.4, size1: 2.5, color: [c, c, c], alpha: 0.4 + dark * 0.3, drag: 0.6 }));
  }

  fire(pos, size = 1) {
    this.addPool.spawn(P({ x: pos.x + rand(-0.3, 0.3) * size, y: pos.y, z: pos.z + rand(-0.3, 0.3) * size, vx: rand(-0.3, 0.3), vy: rand(1.5, 3), vz: rand(-0.3, 0.3), life: rand(0.35, 0.7), size0: 0.9 * size, size1: 0.2 * size, color: [4, 1.6, 0.35], color1: [2, 0.4, 0.1], alpha: 0.9, drag: 0.5, fadeIn: 0.1 }));
    if (Math.random() < 0.5) this.alphaPool.spawn(P({ x: pos.x, y: pos.y + 0.8 * size, z: pos.z, vx: rand(-0.3, 0.3), vy: rand(1.5, 2.5), vz: rand(-0.3, 0.3), life: rand(1.5, 2.5), size0: 0.6 * size, size1: 3 * size, color: [0.12, 0.11, 0.1], alpha: 0.55, drag: 0.4 }));
  }

  explosion(pos, radius = 6) {
    const s = radius / 6;
    for (let i = 0; i < 40; i++) {
      const a = Math.random() * 6.28, e = rand(-0.2, 1.2), sp = rand(4, 14) * s;
      this.addPool.spawn(P({ x: pos.x, y: pos.y, z: pos.z, vx: Math.cos(a) * Math.cos(e) * sp, vy: Math.sin(e) * sp + 2, vz: Math.sin(a) * Math.cos(e) * sp, life: rand(0.4, 0.9), size0: rand(1.5, 3) * s, size1: rand(3, 5) * s, color: [6, 2.6, 0.6], color1: [2.5, 0.5, 0.1], alpha: 1, drag: 3.5, fadeIn: 0.02 }));
    }
    for (let i = 0; i < 30; i++) {
      const a = Math.random() * 6.28, sp = rand(1, 6) * s;
      this.alphaPool.spawn(P({ x: pos.x, y: pos.y + rand(0, 2), z: pos.z, vx: Math.cos(a) * sp, vy: rand(2, 7) * s, vz: Math.sin(a) * sp, life: rand(2.5, 5), size0: rand(2, 3) * s, size1: rand(7, 11) * s, color: [0.1, 0.09, 0.08], color1: [0.25, 0.24, 0.23], alpha: 0.75, drag: 1.2, fadeIn: 0.08 }));
    }
    for (let i = 0; i < 30; i++) this.addPool.spawn(P({ x: pos.x, y: pos.y, z: pos.z, vx: rand(-15, 15), vy: rand(4, 18), vz: rand(-15, 15), life: rand(0.6, 1.4), size0: 0.15, size1: 0.05, color: [6, 3, 1], grav: -16, drag: 0.3 }));
    for (let i = 0; i < 16; i++) this.dotAlpha.spawn(P({ x: pos.x, y: pos.y, z: pos.z, vx: rand(-9, 9), vy: rand(4, 14), vz: rand(-9, 9), life: rand(1, 2), size0: rand(0.1, 0.25), size1: 0.1, color: [0.05, 0.05, 0.05], grav: -16, drag: 0.2, floor: this.game.map.groundHeight(pos.x, pos.z) }));
    this.flash(pos, 0xff8a3a, 250, 0.9, 60);
    const gy = this.game.map.groundHeight(pos.x, pos.z);
    if (pos.y - gy < 3) this.decals.add(new THREE.Vector3(pos.x, gy + 0.01, pos.z), UP, radius * 0.9, 2, { life: 200 });
    // lingering fire
    this.emitters.push({ type: 'fire', pos: pos.clone().setY(gy + 0.2), t: 0, life: 6, size: s * 1.2 });
  }

  splash(pos, size = 1) {
    for (let i = 0; i < 25 * size; i++) this.dotAlpha.spawn(P({ x: pos.x, y: pos.y, z: pos.z, vx: rand(-3, 3) * size, vy: rand(3, 7), vz: rand(-3, 3) * size, life: rand(0.6, 1.2), size0: 0.2, size1: 0.1, color: [0.8, 0.9, 1], alpha: 0.7, grav: -12, drag: 0.3 }));
    for (let i = 0; i < 6 * size; i++) this.alphaPool.spawn(P({ x: pos.x, y: pos.y, z: pos.z, vx: rand(-1, 1), vy: rand(0.5, 1.5), vz: rand(-1, 1), life: 1.2, size0: 0.8, size1: 3 * size, color: [0.9, 0.95, 1], alpha: 0.4 }));
  }

  hydrantSpray(x, y, z) { this.emitters.push({ type: 'hydrant', pos: new THREE.Vector3(x, y + 0.6, z), t: 0, life: 25 }); }

  propDebris(col, vel) {
    const inst = col.prop;
    if (!inst) return;
    const parts = this.game.city.propGeometry(inst.type);
    if (!parts) return;
    const group = new THREE.Group();
    for (const [geo, matKey] of parts) {
      const m = new THREE.Mesh(geo, this.game.city.mats[matKey]);
      m.castShadow = true;
      group.add(m);
    }
    group.position.set(inst.x, inst.y, inst.z);
    group.rotation.y = inst.rot;
    group.scale.setScalar(inst.scale);
    this.game.scene.add(group);
    const sp = Math.hypot(vel.x, vel.z);
    this.debris.push({
      obj: group, vx: vel.x * 0.8 + rand(-1, 1), vy: 2 + sp * 0.2, vz: vel.z * 0.8 + rand(-1, 1),
      ax: rand(-3, 3), az: rand(-3, 3), t: 0, inst, tall: (inst.type === 'streetlight' || inst.type === 'trafficlight'),
    });
    this.dust(new THREE.Vector3(inst.x, inst.y, inst.z), 1.5);
  }

  update(dt) {
    const env = this.game.env;
    // light colour for smoke / decals = sun + ambient approximation
    this.lightColor.copy(env.ambientColor).multiplyScalar(1.6).add(new THREE.Color().copy(env.sun.color).multiplyScalar(env.sun.intensity * 0.22));
    const lc = this.lightColor;
    const maxc = Math.max(lc.r, lc.g, lc.b);
    if (maxc > 1.4) lc.multiplyScalar(1.4 / maxc);
    this.alphaPool.uniforms.uLight.value.copy(lc).addScalar(0.03);
    this.dotAlpha.uniforms.uLight.value.copy(lc).addScalar(0.05);
    this.alphaPool.update(dt);
    this.addPool.update(dt);
    this.dotAlpha.update(dt);
    this.decals.update(dt, this.lightColor);
    this.tracers.update(dt);
    for (const l of this.lights) {
      if (l.light.intensity <= 0) continue;
      l.t += dt;
      l.light.intensity = l.t >= l.life ? 0 : l.peak * (1 - l.t / l.life);
    }
    for (let i = this.emitters.length - 1; i >= 0; i--) {
      const e = this.emitters[i];
      e.t += dt;
      if (e.t > e.life) { this.emitters.splice(i, 1); continue; }
      if (e.type === 'fire' && Math.random() < dt * 30) this.fire(e.pos, e.size * (1 - e.t / e.life));
      if (e.type === 'hydrant') for (let k = 0; k < 3; k++) this.dotAlpha.spawn(P({ x: e.pos.x, y: e.pos.y, z: e.pos.z, vx: rand(-0.6, 0.6), vy: rand(7, 10), vz: rand(-0.6, 0.6), life: 1.6, size0: 0.25, size1: 0.5, color: [0.75, 0.85, 1], alpha: 0.6, grav: -9.8, drag: 0.2, floor: e.pos.y - 0.6 }));
    }
    for (let i = this.debris.length - 1; i >= 0; i--) {
      const d = this.debris[i];
      d.t += dt;
      const o = d.obj;
      const gy = this.game.map.groundHeight(o.position.x, o.position.z);
      if (o.position.y > gy + 0.05 || d.vy > 0) {
        d.vy -= 18 * dt;
        o.position.x += d.vx * dt; o.position.y += d.vy * dt; o.position.z += d.vz * dt;
        o.rotation.x += d.ax * dt; o.rotation.z += d.az * dt;
        if (d.tall) { o.rotation.x = Math.min(Math.PI / 2, Math.abs(o.rotation.x) + dt * 2.5) * Math.sign(d.ax || 1); }
        if (o.position.y < gy) { o.position.y = gy; d.vy = Math.abs(d.vy) * 0.25; d.vx *= 0.5; d.vz *= 0.5; d.ax *= 0.5; d.az *= 0.5; }
      }
      if (d.t > 30) {
        o.parent?.remove(o);
        this.debris.splice(i, 1);
      }
    }
    // restore broken props far from the player after a while
    this._restoreTimer = (this._restoreTimer || 0) + dt;
    if (this._restoreTimer > 5) {
      this._restoreTimer = 0;
      const p = this.game.player.pos;
      for (const col of this.game.city.propColliders) {
        if (col.broken && Math.hypot(col.x - p.x, col.z - p.z) > 250) this.game.city.restoreProp(col.prop);
      }
    }
  }
}

const UP = new THREE.Vector3(0, 1, 0);
