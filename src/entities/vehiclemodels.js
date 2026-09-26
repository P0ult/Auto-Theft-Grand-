// Procedural vehicle meshes: extruded side profiles with wheel arches, glass greenhouse, pillars,
// lights, bumpers, interior, wheels, opening driver door, police light bars, taxi signs.
import * as THREE from 'three';
import { GeoBuilder, mat4 } from '../world/geom.js';
import { patch } from '../render/materials.js';
import { clamp } from '../core/utils.js';

// Shared materials
let MATS = null;
export function vehicleMaterials() {
  if (MATS) return MATS;
  const phys = (p, key) => patch(new THREE.MeshPhysicalMaterial(p), { key });
  MATS = {
    glass: phys({ color: 0x0a0f14, metalness: 0.2, roughness: 0.04, transparent: true, opacity: 0.62, clearcoat: 1, clearcoatRoughness: 0.02 }, 'vglass'),
    trim: patch(new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.55, metalness: 0.35 }), { key: 'vtrim' }),
    wheel: patch(new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.5, metalness: 0.45 }), { key: 'vwheel' }),
    headOff: patch(new THREE.MeshStandardMaterial({ color: 0xdddddd, roughness: 0.1, metalness: 0.8, emissive: 0x222222 }), { key: 'vlight' }),
    headOn: patch(new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.1, metalness: 0.1, emissive: new THREE.Color(6, 5.6, 4.6) }), { key: 'vlight' }),
    tailOff: patch(new THREE.MeshStandardMaterial({ color: 0x5a0000, roughness: 0.2, metalness: 0.3, emissive: new THREE.Color(0.25, 0, 0) }), { key: 'vlight' }),
    tailOn: patch(new THREE.MeshStandardMaterial({ color: 0x8a0000, roughness: 0.2, metalness: 0.3, emissive: new THREE.Color(1.6, 0.05, 0.03) }), { key: 'vlight' }),
    tailBrake: patch(new THREE.MeshStandardMaterial({ color: 0xaa0000, roughness: 0.2, metalness: 0.3, emissive: new THREE.Color(5, 0.1, 0.05) }), { key: 'vlight' }),
    burnt: patch(new THREE.MeshStandardMaterial({ color: 0x151210, roughness: 0.95, metalness: 0.2 }), { key: 'burnt' }),
  };
  return MATS;
}

let _beamGeo = null, _beamMat = null;
function beamGeometry() {
  if (_beamGeo) return _beamGeo;
  const g = new THREE.PlaneGeometry(7, 11);
  g.rotateX(-Math.PI / 2);
  g.userData.shared = true;
  _beamGeo = g;
  return g;
}
function beamMaterial() {
  if (_beamMat) return _beamMat;
  _beamMat = new THREE.ShaderMaterial({
    vertexShader: 'varying vec2 vUv; void main(){ vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.0); }',
    fragmentShader: `varying vec2 vUv; void main(){
      float x = (vUv.x - 0.5) * 2.0; float y = vUv.y; // y: 0 far .. 1 near
      float w = mix(1.0, 0.3, y);
      float a = smoothstep(w, w * 0.2, abs(x)) * smoothstep(0.0, 0.5, y) * smoothstep(1.0, 0.85, y);
      float twin = 0.75 + 0.25 * smoothstep(0.1, 0.35, abs(x));
      gl_FragColor = vec4(vec3(1.0, 0.92, 0.75) * a * twin * 0.55, 1.0); }`,
    transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, polygonOffset: true, polygonOffsetFactor: -4, polygonOffsetUnits: -8,
  });
  return _beamMat;
}

// materials owned by other model builders that must survive a vehicle's disposal
const EXTRA_SHARED = new Set();
export function markShared(m) { EXTRA_SHARED.add(m); return m; }

export function isSharedMaterial(m) {
  if (m === _beamMat || EXTRA_SHARED.has(m)) return true;
  if (!MATS) return false;
  for (const k in MATS) if (MATS[k] === m) return true;
  return false;
}

export function bodyMaterial(color) {
  const m = new THREE.MeshPhysicalMaterial({ color, metalness: 0.55, roughness: 0.32, clearcoat: 1.0, clearcoatRoughness: 0.06, vertexColors: true });
  // clear-coated paint mirrors its surroundings (screen-space reflections), more so in the rain
  return patch(m, { key: 'vbody', fragEnd: 'atgRefl = 0.22 + uWet * 0.18;' });
}

// profile descriptions (fractions of length for z, absolute heights for y)
function profile(def) {
  const L = def.L, H = def.H, c = def.clearance;
  const f = L / 2, r = -L / 2;
  switch (def.body) {
    case 'coupe': return {
      top: [[f, c + 0.25], [f - 0.05, 0.66], [f - 0.4, 0.74], [f - 1.45, 0.84], [r + 0.9, 0.88], [r + 0.2, 0.86], [r, 0.66], [r, c + 0.25]],
      cabin: { bf: f - 1.45, br: r + 0.8, tf: f - 2.15, tr: r + 1.55, inset: 0.18 },
    };
    case 'muscle': return {
      top: [[f, c + 0.28], [f - 0.04, 0.78], [f - 0.3, 0.84], [f - 1.9, 0.9], [r + 1.25, 0.92], [r + 0.2, 0.92], [r, 0.78], [r, c + 0.25]],
      cabin: { bf: f - 1.9, br: r + 1.25, tf: f - 2.55, tr: r + 1.85, inset: 0.16 },
    };
    case 'suv': return {
      top: [[f, c + 0.3], [f - 0.05, 0.98], [f - 0.35, 1.07], [f - 1.3, 1.12], [r + 0.2, 1.14], [r, 1.1], [r, c + 0.3]],
      cabin: { bf: f - 1.3, br: r + 0.12, tf: f - 1.95, tr: r + 0.28, inset: 0.1 },
    };
    case 'pickup': return {
      top: [[f, c + 0.3], [f - 0.05, 0.98], [f - 0.35, 1.06], [f - 1.45, 1.1], [f - 3.05, 1.12], [r + 0.05, 1.12], [r, 1.0], [r, c + 0.3]],
      cabin: { bf: f - 1.45, br: f - 3.0, tf: f - 2.05, tr: f - 2.95, inset: 0.1 },
      bed: { z0: r + 0.05, z1: f - 3.1 },
    };
    case 'van': return {
      top: [[f, c + 0.3], [f - 0.05, 0.9], [f - 0.4, 1.0], [f - 0.95, 1.08], [r + 0.05, 1.1], [r, 1.0], [r, c + 0.3]],
      cabin: { bf: f - 0.95, br: f - 2.05, tf: f - 1.6, tr: f - 2.05, inset: 0.06 },
      cargo: { z0: r, z1: f - 2.05, h: H },
    };
    case 'truck': return {
      top: [[f, c + 0.35], [f - 0.05, 1.2], [f - 0.3, 1.3], [f - 0.6, 1.35], [f - 2.3, 1.35], [r, 1.35], [r, c + 0.35]],
      cabin: { bf: f - 0.6, br: f - 2.25, tf: f - 1.0, tr: f - 2.25, inset: 0.06, top: 2.6 },
      cargo: { z0: r, z1: f - 2.4, h: H, y0: 1.2 },
    };
    case 'super': return {
      top: [[f, c + 0.2], [f - 0.08, 0.5], [f - 0.6, 0.62], [f - 1.45, 0.76], [r + 0.6, 0.86], [r + 0.1, 0.86], [r, 0.7], [r, c + 0.22]],
      cabin: { bf: f - 1.45, br: r + 0.65, tf: f - 2.15, tr: r + 1.45, inset: 0.22 },
    };
    case 'lowrider': return {
      top: [[f, c + 0.28], [f - 0.04, 0.74], [f - 0.3, 0.8], [f - 1.7, 0.84], [r + 1.35, 0.86], [r + 0.2, 0.86], [r, 0.74], [r, c + 0.25]],
      cabin: { bf: f - 1.7, br: r + 1.35, tf: f - 2.4, tr: r + 2.0, inset: 0.14 },
    };
    default: return { // sedan
      top: [[f, c + 0.28], [f - 0.05, 0.8], [f - 0.35, 0.87], [f - 1.55, 0.94], [r + 1.2, 0.97], [r + 0.3, 0.97], [r + 0.02, 0.86], [r, c + 0.26]],
      cabin: { bf: f - 1.55, br: r + 1.2, tf: f - 2.35, tr: r + 1.85, inset: 0.15 },
    };
  }
}

function topAt(top, z) {
  // interpolate top line height at z (top sorted front->rear, descending z)
  for (let i = 0; i < top.length - 1; i++) {
    const [z0, y0] = top[i], [z1, y1] = top[i + 1];
    if (z <= z0 && z >= z1) { const t = (z0 - z) / Math.max(1e-6, z0 - z1); return y0 + (y1 - y0) * t; }
  }
  return top[top.length - 1][1];
}

function beam(gb, a, b, t) {
  const d = new THREE.Vector3().subVectors(b, a);
  const len = d.length();
  const g = new THREE.BoxGeometry(t, len, t);
  const q = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 1, 0), d.clone().normalize());
  const m = new THREE.Matrix4().compose(a.clone().addScaledVector(d, 0.5), q, new THREE.Vector3(1, 1, 1));
  gb.addGeometry(g, m);
}

export function buildVehicleModel(def, color) {
  const M = vehicleMaterials();
  const L = def.L, W = def.W, H = def.H, c = def.clearance, R = def.wheelR, wb = def.wheelbase;
  const pr = profile(def);
  const group = new THREE.Group();
  const bodyGroup = new THREE.Group(); // tilts with suspension
  group.add(bodyGroup);

  // ---------------- lower body (extruded side profile)
  const shape = new THREE.Shape();
  const top = pr.top;
  const archR = R + 0.07;
  const zf = wb / 2, zr = -wb / 2;
  shape.moveTo(top[0][0], top[0][1]);
  for (let i = 1; i < top.length; i++) shape.lineTo(top[i][0], top[i][1]);
  // bottom from rear to front with arches
  shape.lineTo(-L / 2 + 0.12, c);
  shape.lineTo(zr - archR, c);
  shape.lineTo(zr - archR, R);
  shape.absarc(zr, R, archR, Math.PI, 0, true);
  shape.lineTo(zr + archR, c);
  shape.lineTo(zf - archR, c);
  shape.lineTo(zf - archR, R);
  shape.absarc(zf, R, archR, Math.PI, 0, true);
  shape.lineTo(zf + archR, c);
  shape.lineTo(L / 2 - 0.12, c);
  shape.lineTo(top[0][0], top[0][1]);
  const bev = 0.06;
  const bodyGeo = new THREE.ExtrudeGeometry(shape, { depth: W - bev * 2, bevelEnabled: true, bevelThickness: bev, bevelSize: 0.045, bevelSegments: 2, curveSegments: 10 });
  bodyGeo.rotateY(-Math.PI / 2);
  bodyGeo.translate(W / 2 - bev, 0, 0);
  // vertex colors: body white (tinted by material) + dark lower sills
  const bp = bodyGeo.attributes.position;
  const bcol = new Float32Array(bp.count * 3);
  for (let i = 0; i < bp.count; i++) {
    const y = bp.getY(i);
    const k = y < c + 0.12 ? 0.25 : 1;
    bcol[i * 3] = k; bcol[i * 3 + 1] = k; bcol[i * 3 + 2] = k;
  }
  bodyGeo.setAttribute('color', new THREE.BufferAttribute(bcol, 3));
  bodyGeo.computeVertexNormals();

  const bodyParts = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
  bodyParts.addGeometry(bodyGeo, new THREE.Matrix4());

  // ---------------- cabin
  const cb = pr.cabin;
  const yb = (z) => topAt(top, z) - 0.01;
  const yTop = cb.top || H;
  const hw = W / 2 - 0.08, tw = W / 2 - cb.inset;
  const v = (x, y, z) => new THREE.Vector3(x, y, z);
  const b0 = v(hw, yb(cb.bf), cb.bf), b1 = v(-hw, yb(cb.bf), cb.bf), b2 = v(-hw, yb(cb.br), cb.br), b3 = v(hw, yb(cb.br), cb.br);
  const t0 = v(tw, yTop, cb.tf), t1 = v(-tw, yTop, cb.tf), t2 = v(-tw, yTop, cb.tr), t3 = v(tw, yTop, cb.tr);
  const glassGB = new GeoBuilder({ position: 3, normal: 3, uv: 2 });
  const quad = (gb, a, b, cc, d) => {
    const n = new THREE.Vector3().crossVectors(new THREE.Vector3().subVectors(b, a), new THREE.Vector3().subVectors(cc, a)).normalize();
    gb.quad(a.toArray(), b.toArray(), cc.toArray(), d.toArray(), n.toArray());
  };
  quad(glassGB, b1, b0, t0, t1); // windshield (front faces +z)
  quad(glassGB, b3, b2, t2, t3); // rear window
  quad(glassGB, b0, b3, t3, t0); // left side
  quad(glassGB, b2, b1, t1, t2); // right side
  const glassMesh = new THREE.Mesh(glassGB.build(), M.glass);
  glassMesh.renderOrder = 3;
  bodyGroup.add(glassMesh);
  // roof panel & pillars in body paint
  bodyParts.set('color', 1, 1, 1);
  const roofT = 0.05;
  const rp = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
  {
    const r0 = t0.clone().add(v(0.02, 0, 0.02)), r1 = t1.clone().add(v(-0.02, 0, 0.02)), r2 = t2.clone().add(v(-0.02, 0, -0.02)), r3 = t3.clone().add(v(0.02, 0, -0.02));
    const u = v(0, roofT, 0);
    quad(bodyParts, r3, r0, r1, r2); // bottom not needed; top:
    quad(bodyParts, r0.clone().add(u), r3.clone().add(u), r2.clone().add(u), r1.clone().add(u));
    quad(bodyParts, r1, r0, r0.clone().add(u), r1.clone().add(u));
    quad(bodyParts, r3, r2, r2.clone().add(u), r3.clone().add(u));
    quad(bodyParts, r0, r3, r3.clone().add(u), r0.clone().add(u));
    quad(bodyParts, r2, r1, r1.clone().add(u), r2.clone().add(u));
  }
  const pt = 0.07;
  beam(bodyParts, b0, t0, pt); beam(bodyParts, b1, t1, pt);
  beam(bodyParts, b3, t3, pt); beam(bodyParts, b2, t2, pt);
  if (cb.bf - cb.br > 1.8) {
    const mz = (cb.bf + cb.br) / 2 - 0.1, tz = (cb.tf + cb.tr) / 2 - 0.1;
    beam(bodyParts, v(hw + 0.005, yb(mz), mz), v(tw + 0.005, yTop, tz), pt);
    beam(bodyParts, v(-hw - 0.005, yb(mz), mz), v(-tw - 0.005, yTop, tz), pt);
  }
  // cargo box / pickup bed
  if (pr.cargo) {
    const y0 = pr.cargo.y0 ?? yb(pr.cargo.z1) - 0.05;
    bodyParts.set('color', def.body === 'truck' ? 0.95 : 1, def.body === 'truck' ? 0.95 : 1, def.body === 'truck' ? 0.95 : 1);
    bodyParts.box(-W / 2 + 0.02, y0, pr.cargo.z0, W / 2 - 0.02, pr.cargo.h, pr.cargo.z1, { top: true });
  }
  if (pr.bed) {
    const y0 = 1.12;
    bodyParts.set('color', 1, 1, 1);
    bodyParts.box(W / 2 - 0.08, y0, pr.bed.z0, W / 2, y0 + 0.4, pr.bed.z1, { top: true });
    bodyParts.box(-W / 2, y0, pr.bed.z0, -W / 2 + 0.08, y0 + 0.4, pr.bed.z1, { top: true });
    bodyParts.box(-W / 2, y0, pr.bed.z0, W / 2, y0 + 0.4, pr.bed.z0 + 0.08, { top: true });
    bodyParts.set('color', 0.25, 0.25, 0.25);
    bodyParts.box(-W / 2 + 0.08, y0 - 0.02, pr.bed.z0 + 0.08, W / 2 - 0.08, y0 + 0.01, pr.bed.z1, { top: true });
  }
  const bodyMat = bodyMaterial(color);
  const body = new THREE.Mesh(bodyParts.build(), bodyMat);
  body.castShadow = true; body.receiveShadow = true;
  bodyGroup.add(body);

  // ---------------- trim: bumpers, grille, mirrors, plates, interior
  const trim = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
  const col = (hex) => { const cc = new THREE.Color(hex); trim.set('color', cc.r, cc.g, cc.b); };
  const chrome = def.body === 'lowrider' || def.body === 'muscle';
  col(chrome ? 0xd0d0d0 : 0x222222);
  trim.box(-W / 2 + 0.05, c + 0.05, L / 2 - 0.02, W / 2 - 0.05, c + 0.28, L / 2 + 0.07, { top: true });
  trim.box(-W / 2 + 0.05, c + 0.05, -L / 2 - 0.07, W / 2 - 0.05, c + 0.28, -L / 2 + 0.02, { top: true });
  col(0x111111);
  const fy = topAt(top, L / 2 - 0.03);
  trim.box(-W * 0.22, c + 0.3, L / 2 - 0.02, W * 0.22, Math.max(c + 0.45, fy - 0.1), L / 2 + 0.03, { top: true });
  col(0xeeeeee);
  trim.box(-0.26, c + 0.1, L / 2 + 0.07, 0.26, c + 0.23, L / 2 + 0.09, { top: false });
  trim.box(-0.26, c + 0.32, -L / 2 - 0.09, 0.26, c + 0.45, -L / 2 - 0.07, { top: false });
  // mirrors
  col(0x222222);
  const my = yb(cb.bf) + 0.12;
  trim.box(W / 2 - 0.02, my, cb.bf - 0.25, W / 2 + 0.16, my + 0.1, cb.bf - 0.12, { top: true });
  trim.box(-W / 2 - 0.16, my, cb.bf - 0.25, -W / 2 + 0.02, my + 0.1, cb.bf - 0.12, { top: true });
  // exhaust
  col(0x777777);
  trim.addGeometry(new THREE.CylinderGeometry(0.045, 0.045, 0.25, 8), mat4(-W * 0.3, c + 0.08, -L / 2, Math.PI / 2));
  // interior
  col(0x2a2622);
  const seatY = Math.max(c + 0.25, yb(cb.bf) - 0.45);
  const seatZ = (cb.bf + cb.br) / 2 + 0.25;
  trim.box(0.08, seatY, seatZ - 0.35, 0.68, seatY + 0.18, seatZ + 0.15, { top: true });
  trim.box(-0.68, seatY, seatZ - 0.35, -0.08, seatY + 0.18, seatZ + 0.15, { top: true });
  trim.box(0.1, seatY + 0.18, seatZ - 0.45, 0.66, seatY + 0.8, seatZ - 0.3, { top: true });
  trim.box(-0.66, seatY + 0.18, seatZ - 0.45, -0.1, seatY + 0.8, seatZ - 0.3, { top: true });
  col(0x1a1a1a);
  trim.box(-hw + 0.05, yb(cb.bf) - 0.25, cb.bf - 0.45, hw - 0.05, yb(cb.bf) - 0.02, cb.bf - 0.05, { top: true });
  trim.addGeometry(new THREE.TorusGeometry(0.17, 0.025, 6, 16), mat4(0.38, yb(cb.bf) - 0.05, cb.bf - 0.55, -0.35, 0, 0));
  if (def.police) {
    col(0x333333);
    trim.box(-0.7, c + 0.1, L / 2 + 0.07, 0.7, c + 0.5, L / 2 + 0.2, { top: true }); // push bar
  }
  const trimMesh = new THREE.Mesh(trim.build(), M.trim);
  trimMesh.castShadow = true;
  bodyGroup.add(trimMesh);

  // ---------------- lights
  const hl = new GeoBuilder({ position: 3, normal: 3, uv: 2 });
  const hy = Math.max(c + 0.35, fy - 0.22);
  hl.box(W / 2 - 0.42, hy, L / 2 - 0.02, W / 2 - 0.1, hy + 0.13, L / 2 + 0.035, { top: true });
  hl.box(-W / 2 + 0.1, hy, L / 2 - 0.02, -W / 2 + 0.42, hy + 0.13, L / 2 + 0.035, { top: true });
  const head = new THREE.Mesh(hl.build(), M.headOff);
  bodyGroup.add(head);
  const tl = new GeoBuilder({ position: 3, normal: 3, uv: 2 });
  const ty = topAt(top, -L / 2 + 0.03) - 0.2;
  tl.box(W / 2 - 0.45, ty, -L / 2 - 0.035, W / 2 - 0.08, ty + 0.14, -L / 2 + 0.02, { top: true });
  tl.box(-W / 2 + 0.08, ty, -L / 2 - 0.035, -W / 2 + 0.45, ty + 0.14, -L / 2 + 0.02, { top: true });
  const tail = new THREE.Mesh(tl.build(), M.tailOff);
  bodyGroup.add(tail);

  // ---------------- extras
  let lightbar = null;
  if (def.police) {
    const barY = yTop + roofT;
    const mk = (color, x0, x1) => {
      const m = new THREE.Mesh(new THREE.BoxGeometry(x1 - x0, 0.14, 0.28), patch(new THREE.MeshStandardMaterial({ color, emissive: new THREE.Color(color).multiplyScalar(0.2), roughness: 0.2 }), { key: 'vlight' }));
      m.position.set((x0 + x1) / 2, barY + 0.07, (cb.tf + cb.tr) / 2 + 0.2);
      bodyGroup.add(m);
      return m;
    };
    const base = new THREE.Mesh(new THREE.BoxGeometry(1.3, 0.05, 0.32), M.trim);
    base.position.set(0, barY + 0.01, (cb.tf + cb.tr) / 2 + 0.2);
    bodyGroup.add(base);
    lightbar = { red: mk(0xff1a1a, 0.02, 0.62), blue: mk(0x1a4dff, -0.62, -0.02) };
  }
  if (def.taxi) {
    const sign = new THREE.Mesh(new THREE.BoxGeometry(0.7, 0.22, 0.22), patch(new THREE.MeshStandardMaterial({ color: 0xffe066, emissive: new THREE.Color(1.2, 1.0, 0.3), roughness: 0.3 }), { key: 'vlight' }));
    sign.position.set(0, yTop + roofT + 0.11, (cb.tf + cb.tr) / 2);
    bodyGroup.add(sign);
    // checker stripe
    const st = new THREE.Mesh(new THREE.BoxGeometry(W + 0.01, 0.08, 1.8), patch(new THREE.MeshStandardMaterial({ color: 0x111111, roughness: 0.5 }), { key: 'vtrim2' }));
    st.position.set(0, yb(0) - 0.12, 0);
    bodyGroup.add(st);
  }

  // ---------------- driver door (left side, +x), hinge at front
  const dz0 = cb.bf - 0.05, dz1 = Math.max(cb.br + 0.2, cb.bf - 1.25);
  const dy0 = c + 0.12, dy1 = yb((dz0 + dz1) / 2) - 0.02;
  const doorGeo = new THREE.BoxGeometry(0.05, dy1 - dy0, dz0 - dz1);
  const doorColors = new Float32Array(doorGeo.attributes.position.count * 3).fill(1);
  doorGeo.setAttribute('color', new THREE.BufferAttribute(doorColors, 3));
  doorGeo.translate(0, 0, -(dz0 - dz1) / 2);
  const doorPivot = new THREE.Group();
  doorPivot.position.set(W / 2 + 0.005, (dy0 + dy1) / 2, dz0);
  const doorMesh = new THREE.Mesh(doorGeo, def.police ? bodyMaterial(0xffffff) : bodyMat);
  doorMesh.castShadow = true;
  doorPivot.add(doorMesh);
  bodyGroup.add(doorPivot);
  if (def.police) {
    // white panel on the other side too
    const p2 = new THREE.Mesh(new THREE.BoxGeometry(0.05, dy1 - dy0, dz0 - dz1), bodyMaterial(0xffffff));
    p2.geometry.setAttribute('color', new THREE.BufferAttribute(new Float32Array(p2.geometry.attributes.position.count * 3).fill(1), 3));
    p2.position.set(-W / 2 - 0.005, (dy0 + dy1) / 2, (dz0 + dz1) / 2);
    bodyGroup.add(p2);
  }

  // ---------------- wheels
  const wheels = [];
  const wgb = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
  const tireW = def.body === 'truck' ? 0.32 : 0.24;
  wgb.set('color', 0.06, 0.06, 0.06);
  wgb.addGeometry(new THREE.CylinderGeometry(R, R, tireW, 20, 1), mat4(0, 0, 0, 0, 0, Math.PI / 2));
  wgb.addGeometry(new THREE.TorusGeometry(R - 0.035, 0.04, 6, 20), mat4(tireW / 2 - 0.01, 0, 0, 0, Math.PI / 2, 0));
  const rimCol = def.body === 'lowrider' ? [0.85, 0.85, 0.85] : def.body === 'super' || def.body === 'coupe' ? [0.25, 0.25, 0.27] : [0.6, 0.6, 0.62];
  if (def.body === 'lowrider') { wgb.set('color', 0.9, 0.9, 0.9); wgb.addGeometry(new THREE.CylinderGeometry(R - 0.03, R - 0.03, tireW + 0.005, 20, 1, true), mat4(0, 0, 0, 0, 0, Math.PI / 2, 0.98, 0.3, 0.98)); }
  wgb.set('color', ...rimCol);
  wgb.addGeometry(new THREE.CylinderGeometry(R * 0.66, R * 0.66, tireW + 0.02, 16), mat4(0, 0, 0, 0, 0, Math.PI / 2));
  wgb.set('color', rimCol[0] * 0.5, rimCol[1] * 0.5, rimCol[2] * 0.5);
  for (let k = 0; k < 5; k++) {
    const a = k / 5 * Math.PI * 2;
    wgb.addGeometry(new THREE.BoxGeometry(0.03, R * 1.2, 0.07), mat4(tireW / 2 + 0.012, 0, 0, a, 0, 0));
  }
  const wheelGeo = wgb.build();
  const wy = R;
  for (const [x, z, front] of [[def.track / 2, zf, true], [-def.track / 2, zf, true], [def.track / 2, zr, false], [-def.track / 2, zr, false]]) {
    const pivot = new THREE.Group();
    pivot.position.set(x, wy, z);
    const m = new THREE.Mesh(wheelGeo, M.wheel);
    m.castShadow = true;
    if (x < 0) m.rotation.y = Math.PI;
    const spin = new THREE.Group();
    spin.add(m);
    pivot.add(spin);
    group.add(pivot);
    wheels.push({ pivot, spin, x, z, front, baseY: wy, comp: 0 });
  }

  // headlight glow on the road ahead (visible at night)
  const beamMesh = new THREE.Mesh(beamGeometry(), beamMaterial());
  beamMesh.position.set(0, 0.03, L / 2 + 5.5);
  beamMesh.renderOrder = 2;
  beamMesh.visible = false;
  group.add(beamMesh);

  const seatBase = new THREE.Vector3(0.38, seatY + 0.05 - 0.02, seatZ - 0.1);
  // rear row: under the roof (short-roofed bodies would otherwise have heads through the back window)
  const rearZ = clamp(seatBase.z - 0.9, cb.tr + 0.14, seatBase.z - 0.55);
  return {
    group, bodyGroup, body, bodyMat, glass: glassMesh, head, tail, lightbar, wheels,
    door: { pivot: doorPivot, mesh: doorMesh, open: 0 },
    seats: [seatBase.clone(), seatBase.clone().setX(-0.38), seatBase.clone().setZ(rearZ).setY(seatBase.y - 0.04), seatBase.clone().set(-0.38, seatBase.y - 0.04, rearZ)],
    doorPos: new THREE.Vector3(W / 2 + 0.55, 0, (dz0 + dz1) / 2 - 0.15),
    trim: trimMesh, beam: beamMesh,
  };
}
