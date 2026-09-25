// Procedural aircraft & tank meshes: lofted fuselages and airfoils, canopies, spinning props and rotors,
// retractable gear, afterburner flames and nav lights; a tracked tank with rotating turret and elevating gun.
// Models stand on y = 0 (gear / skids / tracks), +z is the nose, +x the left (pilot door) side.
import * as THREE from 'three';
import { GeoBuilder, mat4 } from '../world/geom.js';
import { patch } from '../render/materials.js';
import { vehicleMaterials, bodyMaterial, markShared } from './vehiclemodels.js';

// ------------------------------------------------------------------ shared materials
let AM = null;
function airMats() {
  if (AM) return AM;
  const flame = (c) => markShared(new THREE.MeshBasicMaterial({ color: c, transparent: true, opacity: 0.9, depthWrite: false, blending: THREE.AdditiveBlending, side: THREE.DoubleSide }));
  const light = (c) => markShared(patch(new THREE.MeshStandardMaterial({ color: 0x111111, emissive: c, roughness: 0.3 }), { key: 'vlight' }));
  AM = {
    flame: flame(new THREE.Color(3.2, 1.3, 0.45)),
    flameCore: flame(new THREE.Color(2.4, 2.6, 4.2)),
    blur: markShared(new THREE.MeshBasicMaterial({ color: 0x1a1a1a, transparent: true, opacity: 0.18, depthWrite: false, side: THREE.DoubleSide })),
    navRed: light(new THREE.Color(6, 0.2, 0.1)),
    navGreen: light(new THREE.Color(0.2, 6, 0.6)),
    navWhite: light(new THREE.Color(5, 5, 5)),
    strobe: markShared(patch(new THREE.MeshStandardMaterial({ color: 0x111111, emissive: new THREE.Color(0, 0, 0), roughness: 0.3 }), { key: 'vlight' })),
  };
  return AM;
}

function matteMaterial() {
  return patch(new THREE.MeshStandardMaterial({ color: 0xffffff, vertexColors: true, roughness: 0.6, metalness: 0.3 }), { key: 'vmil' });
}

// ------------------------------------------------------------------ lofting helpers
// Catmull-Rom resampling of station arrays [s, ...values] (s itself interpolated linearly).
function smooth(st, step) {
  const out = [];
  for (let i = 0; i < st.length - 1; i++) {
    const p0 = st[Math.max(0, i - 1)], p1 = st[i], p2 = st[i + 1], p3 = st[Math.min(st.length - 1, i + 2)];
    const n = Math.max(1, Math.ceil(Math.abs(p2[0] - p1[0]) / step));
    for (let k = 0; k < n; k++) {
      const t = k / n, t2 = t * t, t3 = t2 * t;
      out.push(p1.map((_, j) => j === 0 ? p1[0] + (p2[0] - p1[0]) * t
        : 0.5 * (2 * p1[j] + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t2 + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t3)));
    }
  }
  out.push(st[st.length - 1].slice());
  return out;
}

// Closed tube through stations {c, a, b, foil}; ring point = c + a cos t + b sin t (foil: tapered toward the trailing edge).
function loft(stations, seg, colorFn) {
  const pos = [], idx = [];
  const n = stations.length;
  for (const s of stations) {
    for (let k = 0; k < seg; k++) {
      const t = k / seg * Math.PI * 2, ca = Math.cos(t);
      let sa = Math.sin(t);
      if (s.foil) sa *= 0.3 + 0.7 * Math.pow((1 + ca) / 2, 0.6);
      pos.push(s.c[0] + s.a[0] * ca + s.b[0] * sa, s.c[1] + s.a[1] * ca + s.b[1] * sa, s.c[2] + s.a[2] * ca + s.b[2] * sa);
    }
  }
  for (let i = 0; i < n - 1; i++) for (let k = 0; k < seg; k++) {
    const a = i * seg + k, b = i * seg + (k + 1) % seg, c = (i + 1) * seg + (k + 1) % seg, d = (i + 1) * seg + k;
    idx.push(a, b, c, a, c, d);
  }
  for (const [i, first] of [[0, true], [n - 1, false]]) {
    const ci = pos.length / 3;
    pos.push(...stations[i].c);
    for (let k = 0; k < seg; k++) { const a = i * seg + k, b = i * seg + (k + 1) % seg; if (first) idx.push(ci, b, a); else idx.push(ci, a, b); }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setIndex(idx);
  g.computeVertexNormals();
  // make sure faces point outward (the ring direction depends on the station axes)
  const mi = Math.floor(n / 2), m = stations[mi], vi = mi * seg + Math.floor(seg / 4);
  const nr = g.attributes.normal;
  if (nr.getX(vi) * (pos[vi * 3] - m.c[0]) + nr.getY(vi) * (pos[vi * 3 + 1] - m.c[1]) + nr.getZ(vi) * (pos[vi * 3 + 2] - m.c[2]) < 0) {
    for (let i = 0; i < idx.length; i += 3) { const t = idx[i + 1]; idx[i + 1] = idx[i + 2]; idx[i + 2] = t; }
    g.setIndex(idx);
    g.computeVertexNormals();
  }
  if (colorFn) {
    const col = new Float32Array(pos.length);
    for (let i = 0; i < pos.length; i += 3) { const c = colorFn(pos[i], pos[i + 1], pos[i + 2], nr.getY(i / 3)); col[i] = c[0]; col[i + 1] = c[1]; col[i + 2] = c[2]; }
    g.setAttribute('color', new THREE.BufferAttribute(col, 3));
  }
  return g;
}

// fuselage along z: [z, cy, rx, ry]
const fuselage = (st, seg = 22, step = 0.3, colorFn) => loft(smooth(st, step).map(([z, cy, rx, ry]) => ({ c: [0, cy, z], a: [Math.max(0.001, rx), 0, 0], b: [0, Math.max(0.001, ry), 0] })), seg, colorFn);
// pod along z at an x offset: [z, cy, r]
const pod = (x, st, seg = 14, colorFn) => loft(smooth(st, 0.3).map(([z, cy, r]) => ({ c: [x, cy, z], a: [Math.max(0.001, r), 0, 0], b: [0, Math.max(0.001, r), 0] })), seg, colorFn);
// wing along x (side = +1 left / -1 right): [x, y, zc, chord, thick]
const wing = (st, side, colorFn) => loft(smooth(st, 0.5).map(([x, y, zc, ch, th]) => ({ c: [x * side, y, zc], a: [0, 0, ch / 2], b: [0, th / 2, 0], foil: true })), 14, colorFn);
// fin along y: [y, x, zc, chord, thick]
const fin = (st, colorFn) => loft(smooth(st, 0.4).map(([y, x, zc, ch, th]) => ({ c: [x, y, zc], a: [0, 0, ch / 2], b: [th / 2, 0, 0], foil: true })), 12, colorFn);

const hex = (h) => { const c = new THREE.Color(h); return [c.r, c.g, c.b]; };
const mix = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];

function cyl(r0, r1, len, seg = 12) { const g = new THREE.CylinderGeometry(r1, r0, len, seg); g.rotateX(Math.PI / 2); return g; } // along +z, r0 at -z end
function strut(gb, a, b, t) {
  const d = new THREE.Vector3().subVectors(b, a), len = d.length();
  const q = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 1, 0), d.clone().normalize());
  gb.addGeometry(new THREE.CylinderGeometry(t, t, len, 6), new THREE.Matrix4().compose(a.clone().addScaledVector(d, 0.5), q, new THREE.Vector3(1, 1, 1)));
}
function wheel(gb, x, y, z, r, w) {
  gb.set('color', 0.06, 0.06, 0.06);
  gb.addGeometry(new THREE.CylinderGeometry(r, r, w, 16), mat4(x, y, z, 0, 0, Math.PI / 2));
  gb.set('color', 0.55, 0.56, 0.58);
  gb.addGeometry(new THREE.CylinderGeometry(r * 0.55, r * 0.55, w + 0.02, 12), mat4(x, y, z, 0, 0, Math.PI / 2));
}
// n blades radiating from the hub, spinning about `axis`; blades have a little pitch and optional painted tips
function blades(n, len, chord, thick, axis = 'y', color = 0x1c1c1c, tip = null) {
  const gb = new GeoBuilder();
  const c = new THREE.Color(color), t = tip != null ? new THREE.Color(tip) : null;
  for (let k = 0; k < n; k++) {
    const a = k / n * Math.PI * 2;
    const rot = new THREE.Matrix4();
    let geo, off, tipGeo, tipOff;
    if (axis === 'x') {
      rot.makeRotationX(a);
      geo = new THREE.BoxGeometry(thick, len, chord); off = mat4(0, len / 2, 0, 0, 0.3, 0);
      tipGeo = new THREE.BoxGeometry(thick * 1.2, len * 0.12, chord * 1.04); tipOff = mat4(0, len * 0.94, 0, 0, 0.3, 0);
    } else if (axis === 'y') {
      rot.makeRotationY(a);
      geo = new THREE.BoxGeometry(len, thick, chord); off = mat4(len / 2, 0, 0, 0.08, 0, 0);
      tipGeo = new THREE.BoxGeometry(len * 0.08, thick * 1.2, chord * 1.04); tipOff = mat4(len * 0.96, 0, 0, 0.08, 0, 0);
    } else {
      rot.makeRotationZ(a);
      geo = new THREE.BoxGeometry(len, chord, thick); off = mat4(len / 2, 0, 0, 0.45, 0, 0);
      tipGeo = new THREE.BoxGeometry(len * 0.12, chord * 1.04, thick * 1.2); tipOff = mat4(len * 0.94, 0, 0, 0.45, 0, 0);
    }
    gb.set('color', c.r, c.g, c.b);
    gb.addGeometry(geo, rot.clone().multiply(off));
    if (t) { gb.set('color', t.r, t.g, t.b); gb.addGeometry(tipGeo, rot.clone().multiply(tipOff)); }
  }
  return gb.build();
}

function navLight(parent, mat, x, y, z, r = 0.08) {
  const m = new THREE.Mesh(new THREE.SphereGeometry(r, 8, 6), mat);
  m.position.set(x, y, z);
  parent.add(m);
  return m;
}

// Generic container shared by every model
function shell(cgY) {
  const group = new THREE.Group();
  const bodyGroup = new THREE.Group();
  group.add(bodyGroup);
  const doorPivot = new THREE.Group();
  bodyGroup.add(doorPivot);
  return { group, bodyGroup, cgY, wheels: [], props: [], door: { pivot: doorPivot, mesh: null, open: 0 }, head: null, tail: null, lightbar: null, beam: null };
}

function finish(model, parts) {
  const { bodyGroup } = model;
  for (const [geo, mat, cast = true] of parts) {
    if (!geo) continue;
    const m = new THREE.Mesh(geo, mat);
    m.castShadow = cast; m.receiveShadow = true;
    bodyGroup.add(m);
  }
  model.meshes = [];
  model.group.traverse((o) => { if (o.isMesh) model.meshes.push(o); });
  return model;
}

// ====================================================================================== aircraft
export function buildAircraftModel(def, color, type) {
  switch (type) {
    case 'skipper': return skipper(def, color);
    case 'raptor': return raptor(def, color);
    case 'hercules': return hercules(def, color);
    case 'warhawk': return warhawk(def, color);
    case 'skylark': return skylark(def, color);
    case 'mammoth': return mammoth(def, color);
    default: return def.kind === 'heli' ? skylark(def, color) : def.kind === 'tank' ? mammoth(def, color) : skipper(def, color);
  }
}

// ------------------------------------------------------------------ Skipper: high-wing light plane
function skipper(def, color) {
  const M = vehicleMaterials(), A = airMats();
  const model = shell(1.45);
  const paint = hex(color), cream = hex(0xf4f1de), win = [0.05, 0.07, 0.09];
  const body = new GeoBuilder();
  const fcol = (x, y, z) => {
    if (z > 0.05 && z < 2.25 && y > 1.62 && y < 2.12) return win;                  // cabin windows
    if (z > 2.25 && z < 2.7 && y > 1.72 && Math.abs(x) < 0.5) return win;          // windscreen
    if (y > 1.18 && y < 1.34 && z < 2.3) return paint;                               // cheat line
    if (z > 3.75) return paint;                                                       // cowling nose
    return cream;
  };
  body.addGeometry(fuselage([[4.12, 1.3, 0.02, 0.02], [4.02, 1.3, 0.24, 0.24], [3.75, 1.3, 0.48, 0.5], [3.1, 1.33, 0.58, 0.62], [2.2, 1.4, 0.6, 0.68], [1.0, 1.45, 0.6, 0.72], [0.0, 1.5, 0.56, 0.65], [-1.2, 1.6, 0.42, 0.48], [-2.6, 1.72, 0.26, 0.3], [-3.7, 1.8, 0.13, 0.16], [-4.12, 1.82, 0.04, 0.05]], 22, 0.3, fcol), new THREE.Matrix4());
  const wcol = (x) => Math.abs(x) > 4.7 ? paint : cream;
  for (const s of [1, -1]) {
    body.addGeometry(wing([[0, 2.22, 1.15, 1.55, 0.22], [2.6, 2.25, 1.15, 1.5, 0.19], [5.2, 2.28, 1.12, 1.3, 0.13], [5.5, 2.28, 1.1, 0.9, 0.06]], s, wcol), new THREE.Matrix4());
    body.addGeometry(wing([[0, 1.8, -3.7, 0.95, 0.09], [1.75, 1.8, -3.8, 0.55, 0.05]], s, () => cream), new THREE.Matrix4());
    // wheel pants
    body.set('color', ...paint);
    body.addGeometry(new THREE.SphereGeometry(0.3, 12, 8), mat4(s * 1.18, 0.34, 0.8, 0, 0, 0, 0.55, 0.75, 1.5));
    body.set('color', 1, 1, 1);
  }
  body.addGeometry(fin([[1.7, 0, -3.35, 1.75, 0.12], [2.2, 0, -3.6, 1.3, 0.1], [2.95, 0, -3.95, 0.72, 0.06]], (x, y) => y > 2.5 ? paint : cream), new THREE.Matrix4());
  const trim = new GeoBuilder();
  trim.set('color', 0.72, 0.72, 0.74);
  for (const s of [1, -1]) strut(trim, new THREE.Vector3(s * 0.52, 0.95, 1.25), new THREE.Vector3(s * 2.7, 2.18, 1.2), 0.035);
  // fixed tricycle gear
  trim.set('color', 0.2, 0.2, 0.22);
  strut(trim, new THREE.Vector3(0, 0.85, 3.3), new THREE.Vector3(0, 0.28, 3.35), 0.04);
  for (const s of [1, -1]) strut(trim, new THREE.Vector3(s * 0.4, 0.85, 0.85), new THREE.Vector3(s * 1.12, 0.32, 0.8), 0.045);
  wheel(trim, 0, 0.26, 3.35, 0.26, 0.12);
  for (const s of [1, -1]) wheel(trim, s * 1.18, 0.3, 0.8, 0.3, 0.14);
  // spinner + prop
  const prop = new THREE.Group();
  prop.position.set(0, 1.3, 4.18);
  const blade = new THREE.Mesh(blades(2, 0.95, 0.14, 0.04, 'z', 0x1a1a1a, 0xffe14d), M.trim);
  prop.add(blade);
  const disc = new THREE.Mesh(new THREE.CircleGeometry(1.0, 24), A.blur);
  disc.visible = false;
  prop.add(disc);
  model.bodyGroup.add(prop);
  model.props.push({ obj: prop, axis: 'z', disc, blade, rate: 1 });
  navLight(model.bodyGroup, A.navRed, 5.52, 2.28, 1.1);
  navLight(model.bodyGroup, A.navGreen, -5.52, 2.28, 1.1);
  model.strobe = navLight(model.bodyGroup, A.strobe, 0, 2.98, -4.0, 0.07);
  model.seats = [new THREE.Vector3(0.3, 1.02, 1.25), new THREE.Vector3(-0.3, 1.02, 1.25), new THREE.Vector3(0.3, 1.02, 0.3), new THREE.Vector3(-0.3, 1.02, 0.3)];
  model.visibleSeats = 0;
  model.doorPos = new THREE.Vector3(1.35, 0, 1.2);
  model.muzzles = [];
  return finish(model, [[body.build(), bodyMaterial(0xffffff)], [trim.build(), M.trim]]);
}

// ------------------------------------------------------------------ Raptor: twin-tail fighter jet
function raptor(def, color) {
  const M = vehicleMaterials(), A = airMats();
  const model = shell(2.3);
  const base = hex(color), under = mix(base, [0.8, 0.82, 0.84], 0.35), dark = [0.12, 0.13, 0.14], radome = [0.32, 0.34, 0.36];
  const col = (x, y, z, ny) => {
    if (z > 7.3) return radome;
    if (z > 6.0 && z < 7.4 && y > 2.45 && Math.abs(x) < 0.35) return dark;          // anti-glare panel
    return ny < -0.3 ? under : base;
  };
  const body = new GeoBuilder();
  body.addGeometry(fuselage([[8.25, 2.1, 0.02, 0.02], [7.9, 2.12, 0.17, 0.15], [7.2, 2.16, 0.38, 0.32], [6.2, 2.22, 0.56, 0.46], [5.0, 2.28, 0.72, 0.5], [3.8, 2.3, 0.95, 0.54], [2.4, 2.3, 1.38, 0.64], [0.5, 2.3, 1.5, 0.66], [-2, 2.3, 1.46, 0.62], [-4.5, 2.3, 1.3, 0.58], [-6.5, 2.3, 1.08, 0.52], [-7.7, 2.3, 0.98, 0.48], [-8.1, 2.3, 0.92, 0.44]], 24, 0.3, col), new THREE.Matrix4());
  for (const s of [1, -1]) {
    body.addGeometry(wing([[1.2, 2.16, -0.7, 7.0, 0.3], [3.4, 2.13, -2.1, 5.0, 0.2], [5.55, 2.1, -3.7, 2.4, 0.09], [5.8, 2.1, -3.8, 1.6, 0.05]], s, col), new THREE.Matrix4());
    body.addGeometry(wing([[0.8, 2.22, -6.8, 2.7, 0.14], [3.3, 2.2, -7.85, 1.1, 0.05]], s, col), new THREE.Matrix4());
    // canted twin fins
    const cant = 0.42;
    body.addGeometry(fin([[2.72, s * 1.05, -5.9, 3.3, 0.16], [3.6, s * (1.05 + 0.88 * cant), -6.6, 2.3, 0.1], [4.4, s * (1.05 + 1.68 * cant), -7.25, 1.3, 0.06]], col), new THREE.Matrix4());
    // intakes (body-colour housings with dark mouths)
    body.set('color', ...base);
    body.box(s > 0 ? 1.1 : -1.62, 1.78, 0.2, s > 0 ? 1.62 : -1.1, 2.48, 3.2, { top: true, bottom: true });
    body.set('color', 1, 1, 1);
  }
  const trim = new GeoBuilder();
  for (const s of [1, -1]) {
    trim.set('color', 0.03, 0.03, 0.035);
    trim.quad([s > 0 ? 1.62 : -1.1, 1.8, 3.21], [s > 0 ? 1.1 : -1.62, 1.8, 3.21], [s > 0 ? 1.1 : -1.62, 2.46, 3.21], [s > 0 ? 1.62 : -1.1, 2.46, 3.21], [0, 0, 1]);
    // nozzles
    trim.set('color', 0.28, 0.27, 0.26);
    trim.addGeometry(cyl(0.46, 0.4, 1.1, 16), mat4(s * 0.52, 2.22, -8.2));
    trim.set('color', 0.05, 0.05, 0.05);
    trim.addGeometry(new THREE.CircleGeometry(0.36, 16).rotateY(Math.PI), mat4(s * 0.52, 2.22, -8.76));
    // missiles on pylons
    for (const [mx, mz] of [[2.6, -1.1], [4.1, -2.5]]) {
      trim.set('color', 0.4, 0.42, 0.44);
      trim.box(s * mx - 0.04, 1.98, mz - 0.6, s * mx + 0.04, 2.1, mz + 0.6);
      trim.set('color', 0.92, 0.92, 0.9);
      trim.addGeometry(cyl(0.09, 0.09, 3.0, 8), mat4(s * mx, 1.88, mz + 0.2));
      trim.set('color', 0.8, 0.15, 0.1);
      trim.addGeometry(new THREE.ConeGeometry(0.09, 0.3, 8).rotateX(Math.PI / 2), mat4(s * mx, 1.88, mz + 1.85));
    }
  }
  // cockpit interior tub + seat
  trim.set('color', 0.1, 0.1, 0.11);
  trim.box(-0.42, 2.45, 2.4, 0.42, 2.78, 5.6);
  // landing gear (retracts)
  const gear = new GeoBuilder();
  gear.set('color', 0.75, 0.76, 0.78);
  strut(gear, new THREE.Vector3(0, 1.75, 5.3), new THREE.Vector3(0, 0.34, 5.3), 0.07);
  for (const s of [1, -1]) strut(gear, new THREE.Vector3(s * 1.1, 1.9, -1.4), new THREE.Vector3(s * 1.3, 0.4, -1.6), 0.09);
  wheel(gear, 0, 0.32, 5.3, 0.32, 0.18);
  for (const s of [1, -1]) wheel(gear, s * 1.32, 0.4, -1.6, 0.4, 0.28);
  const gearMesh = new THREE.Mesh(gear.build(), M.trim);
  gearMesh.castShadow = true;
  model.bodyGroup.add(gearMesh);
  model.gear = gearMesh;
  // canopy (opens as the "door")
  const pivot = model.door.pivot;
  pivot.position.set(0, 2.86, 1.95);
  const glass = new THREE.Mesh(fuselage([[6.1, 2.72, 0.04, 0.03], [5.6, 2.76, 0.34, 0.3], [4.6, 2.8, 0.47, 0.45], [3.4, 2.8, 0.46, 0.44], [2.4, 2.75, 0.34, 0.33], [1.95, 2.7, 0.08, 0.1]], 18, 0.25), M.glass);
  glass.position.set(0, -2.86, -1.95);
  glass.renderOrder = 3;
  pivot.add(glass);
  model.glass = glass;
  model.door.axis = 'x'; model.door.max = -0.75;
  // afterburners
  model.flames = [];
  for (const s of [1, -1]) {
    const f = new THREE.Mesh(new THREE.ConeGeometry(0.36, 3.2, 14, 1, true).rotateX(-Math.PI / 2).translate(0, 0, -1.6), A.flame);
    const core = new THREE.Mesh(new THREE.ConeGeometry(0.22, 1.6, 12, 1, true).rotateX(-Math.PI / 2).translate(0, 0, -0.8), A.flameCore);
    f.position.set(s * 0.52, 2.22, -8.7); core.position.copy(f.position);
    f.renderOrder = 4; core.renderOrder = 4;
    model.bodyGroup.add(f, core);
    model.flames.push(f, core);
  }
  navLight(model.bodyGroup, A.navRed, 5.8, 2.1, -3.85);
  navLight(model.bodyGroup, A.navGreen, -5.8, 2.1, -3.85);
  model.strobe = navLight(model.bodyGroup, A.strobe, 0, 1.64, -2.0, 0.07);
  model.seats = [new THREE.Vector3(0, 2.12, 4.05), new THREE.Vector3(0, 1.6, 0.6), new THREE.Vector3(0, 1.6, -0.4), new THREE.Vector3(0, 1.6, -1.4)];
  model.visibleSeats = 1;
  model.doorPos = new THREE.Vector3(2.05, 0, 3.8);
  model.muzzles = [new THREE.Vector3(0.9, 2.55, 3.0)];
  model.pylons = [new THREE.Vector3(2.6, 1.88, 1.2), new THREE.Vector3(-2.6, 1.88, 1.2), new THREE.Vector3(4.1, 1.88, -0.2), new THREE.Vector3(-4.1, 1.88, -0.2)];
  return finish(model, [[body.build(), matteMaterial()], [trim.build(), M.trim]]);
}

// ------------------------------------------------------------------ Hercules: four-prop cargo plane
function hercules(def, color) {
  const M = vehicleMaterials(), A = airMats();
  const model = shell(3.4);
  const base = hex(color), under = mix(base, [0.55, 0.57, 0.55], 0.4), win = [0.04, 0.05, 0.06], nose = [0.16, 0.17, 0.16];
  const col = (x, y, z, ny) => {
    if (z > 13.9) return nose;
    if (z > 11.7 && z < 13.7 && y > 4.05 && y < 4.95) return win;
    return ny < -0.5 ? under : base;
  };
  const body = new GeoBuilder();
  body.addGeometry(fuselage([[14.5, 3.0, 0.05, 0.05], [14.2, 3.05, 0.75, 0.8], [13.5, 3.2, 1.4, 1.55], [12.3, 3.35, 1.88, 2.02], [10.5, 3.4, 2.1, 2.2], [-5, 3.4, 2.1, 2.2], [-7.5, 3.7, 1.95, 1.95], [-10, 4.4, 1.5, 1.35], [-12.5, 5.1, 0.9, 0.75], [-14.5, 5.6, 0.3, 0.3]], 26, 0.5, col), new THREE.Matrix4());
  for (const s of [1, -1]) {
    body.addGeometry(wing([[0, 5.75, 1.2, 4.6, 0.72], [2.2, 5.78, 1.2, 4.6, 0.7], [11, 5.85, 1.0, 3.8, 0.5], [19.5, 5.9, 0.8, 2.4, 0.26], [20, 5.9, 0.8, 1.7, 0.1]], s, col), new THREE.Matrix4());
    body.addGeometry(wing([[0, 5.95, -12.5, 3.6, 0.3], [7.8, 5.95, -13.0, 1.8, 0.12]], s, col), new THREE.Matrix4());
    for (const ex of [5.6, 10.6]) body.addGeometry(pod(s * ex, [[5.25, 5.45, 0.2], [4.95, 5.45, 0.55], [4.0, 5.4, 0.68], [1.0, 5.35, 0.62], [-1.8, 5.5, 0.36], [-2.4, 5.6, 0.1]], 14, () => base), new THREE.Matrix4());
    // main gear sponsons
    body.set('color', ...base);
    body.addGeometry(pod(s * 1.95, [[3.2, 1.55, 0.1], [2.6, 1.5, 0.62], [-2.6, 1.5, 0.62], [-3.4, 1.7, 0.1]], 12), mat4(0, 0, 0, 0, 0, 0, 1, 1, 1));
    body.set('color', 1, 1, 1);
  }
  body.addGeometry(fin([[5.2, 0, -10.8, 6.5, 0.55], [8, 0, -12.2, 4.3, 0.36], [10.95, 0, -13.3, 2.4, 0.2]], col), new THREE.Matrix4());
  const trim = new GeoBuilder();
  const gear = new GeoBuilder();
  for (const s of [1, -1]) for (const z of [-1.1, 1.1]) wheel(gear, s * 1.95, 0.6, z, 0.6, 0.42);
  for (const s of [1, -1]) wheel(gear, s * 0.28, 0.5, 11.2, 0.5, 0.3);
  gear.set('color', 0.6, 0.6, 0.62);
  strut(gear, new THREE.Vector3(0, 1.4, 11.2), new THREE.Vector3(0, 0.5, 11.2), 0.1);
  const gearMesh = new THREE.Mesh(gear.build(), M.trim);
  gearMesh.castShadow = true;
  model.bodyGroup.add(gearMesh);
  model.gear = gearMesh;
  for (const s of [1, -1]) for (const ex of [5.6, 10.6]) {
    const prop = new THREE.Group();
    prop.position.set(s * ex, 5.45, 5.3);
    trim.set('color', 0.15, 0.15, 0.16);
    trim.addGeometry(new THREE.ConeGeometry(0.28, 0.6, 12).rotateX(Math.PI / 2), mat4(s * ex, 5.45, 5.55));
    const blade = new THREE.Mesh(blades(4, 2.0, 0.34, 0.06, 'z', 0x1a1a1a, 0xe8d23a), M.trim);
    const disc = new THREE.Mesh(new THREE.CircleGeometry(2.1, 28), A.blur);
    disc.visible = false;
    prop.add(blade, disc);
    model.bodyGroup.add(prop);
    model.props.push({ obj: prop, axis: 'z', disc, blade, rate: 0.7 * (s > 0 ? 1 : -1) });
  }
  navLight(model.bodyGroup, A.navRed, 20, 5.9, 0.8, 0.12);
  navLight(model.bodyGroup, A.navGreen, -20, 5.9, 0.8, 0.12);
  model.strobe = navLight(model.bodyGroup, A.strobe, 0, 11.1, -13.6, 0.12);
  model.seats = [new THREE.Vector3(0.7, 3.75, 12.3), new THREE.Vector3(-0.7, 3.75, 12.3), new THREE.Vector3(0.6, 2.2, 4), new THREE.Vector3(-0.6, 2.2, 4)];
  model.visibleSeats = 2;
  model.doorPos = new THREE.Vector3(2.8, 0, 10.3);
  model.muzzles = [];
  return finish(model, [[body.build(), matteMaterial()], [trim.build(), M.trim]]);
}

// ------------------------------------------------------------------ helicopters
function rotorAssembly(model, M, A, { y, z, r, n, chord, tailZ, tailY, tailR, tailN, tailX }) {
  const rotor = new THREE.Group();
  rotor.position.set(0, y, z);
  const blade = new THREE.Mesh(blades(n, r, chord, 0.07, 'y', 0x1c1c1c), M.trim);
  blade.castShadow = true;
  const disc = new THREE.Mesh(new THREE.CircleGeometry(r + 0.1, 40).rotateX(-Math.PI / 2), A.blur);
  disc.visible = false;
  rotor.add(blade, disc);
  const hub = new THREE.Mesh(new THREE.CylinderGeometry(0.22, 0.28, 0.3, 10), M.trim);
  rotor.add(hub);
  model.bodyGroup.add(rotor);
  model.rotor = { obj: rotor, blade, disc };
  const tail = new THREE.Group();
  tail.position.set(tailX, tailY, tailZ);
  const tb = new THREE.Mesh(blades(tailN, tailR, 0.2, 0.04, 'x', 0x1c1c1c, 0xffffff), M.trim);
  const tdisc = new THREE.Mesh(new THREE.CircleGeometry(tailR + 0.05, 24).rotateY(Math.PI / 2), A.blur);
  tdisc.visible = false;
  tail.add(tb, tdisc);
  model.bodyGroup.add(tail);
  model.tailRotor = { obj: tail, blade: tb, disc: tdisc };
}

function warhawk(def, color) {
  const M = vehicleMaterials(), A = airMats();
  const model = shell(1.8);
  const base = hex(color), under = mix(base, [0.3, 0.32, 0.3], 0.3);
  const col = (x, y, z, ny) => ny < -0.5 ? under : base;
  const body = new GeoBuilder();
  body.addGeometry(fuselage([[5.7, 1.25, 0.05, 0.05], [5.3, 1.28, 0.3, 0.36], [4.4, 1.36, 0.55, 0.62], [3.0, 1.45, 0.62, 0.62], [1.4, 1.62, 0.72, 0.9], [-0.6, 1.85, 0.72, 0.95], [-2.0, 2.08, 0.45, 0.6], [-3.5, 2.25, 0.26, 0.32], [-8.2, 2.4, 0.18, 0.22], [-9.35, 2.45, 0.12, 0.15]], 20, 0.3, col), new THREE.Matrix4());
  for (const s of [1, -1]) {
    body.addGeometry(pod(s * 0.74, [[0.9, 2.55, 0.05], [0.6, 2.55, 0.3], [-1.2, 2.58, 0.33], [-2.0, 2.6, 0.15]], 12, col), new THREE.Matrix4());
    body.addGeometry(wing([[0.55, 1.9, 0.15, 1.3, 0.16], [1.95, 1.95, 0.15, 1.0, 0.1]], s, col), new THREE.Matrix4());
    body.addGeometry(wing([[0, 2.36, -8.3, 0.95, 0.09], [1.7, 2.36, -8.4, 0.7, 0.05]], s, col), new THREE.Matrix4());
  }
  body.addGeometry(fin([[2.2, 0, -8.7, 1.7, 0.16], [3.2, 0, -9.0, 1.3, 0.12], [4.15, 0, -9.3, 0.9, 0.08]], col), new THREE.Matrix4());
  const trim = new GeoBuilder();
  trim.set('color', 0.12, 0.13, 0.12);
  trim.addGeometry(new THREE.CylinderGeometry(0.16, 0.2, 0.7, 10), mat4(0, 3.0, 0.3));
  for (const s of [1, -1]) {
    // rocket pods and missile rails
    trim.set('color', 0.25, 0.27, 0.24);
    trim.addGeometry(cyl(0.25, 0.25, 1.5, 12), mat4(s * 1.3, 1.6, 0.25));
    trim.set('color', 0.05, 0.05, 0.05);
    trim.addGeometry(new THREE.CircleGeometry(0.22, 12), mat4(s * 1.3, 1.6, 1.01));
    trim.set('color', 0.3, 0.3, 0.3);
    trim.box(s * 1.85 - 0.18, 1.62, -0.4, s * 1.85 + 0.18, 1.72, 0.7);
    trim.set('color', 0.85, 0.85, 0.82);
    for (const dx of [-0.1, 0.1]) trim.addGeometry(cyl(0.07, 0.07, 1.2, 8), mat4(s * 1.85 + dx, 1.53, 0.15));
    // exhausts
    trim.set('color', 0.08, 0.08, 0.08);
    trim.addGeometry(cyl(0.16, 0.18, 0.4, 10), mat4(s * 0.95, 2.6, -2.1, 0, s * 0.5, 0));
  }
  // gear: two mains + tail wheel on a long strut
  trim.set('color', 0.5, 0.5, 0.5);
  for (const s of [1, -1]) strut(trim, new THREE.Vector3(s * 0.55, 1.1, 2.4), new THREE.Vector3(s * 1.15, 0.36, 2.45), 0.06);
  strut(trim, new THREE.Vector3(0, 2.2, -8.4), new THREE.Vector3(0, 0.26, -8.7), 0.05);
  for (const s of [1, -1]) wheel(trim, s * 1.2, 0.36, 2.45, 0.36, 0.22);
  wheel(trim, 0, 0.24, -8.7, 0.24, 0.12);
  // canopy
  const glass = new THREE.Mesh(fuselage([[5.05, 1.68, 0.05, 0.04], [4.6, 1.85, 0.42, 0.33], [3.7, 2.0, 0.55, 0.52], [2.4, 2.12, 0.58, 0.56], [1.5, 2.12, 0.46, 0.44], [1.05, 2.05, 0.1, 0.1]], 18, 0.25), M.glass);
  glass.renderOrder = 3;
  model.bodyGroup.add(glass);
  model.glass = glass;
  // chin gun turret
  const gun = new THREE.Group();
  gun.position.set(0, 0.86, 4.35);
  const gt = new GeoBuilder();
  gt.set('color', 0.2, 0.21, 0.2);
  gt.addGeometry(new THREE.SphereGeometry(0.3, 12, 8), new THREE.Matrix4());
  gt.set('color', 0.1, 0.1, 0.1);
  gt.addGeometry(cyl(0.06, 0.05, 1.5, 8), mat4(0, -0.05, 0.85));
  const gunMesh = new THREE.Mesh(gt.build(), M.trim);
  gun.add(gunMesh);
  model.bodyGroup.add(gun);
  model.gun = gun;
  rotorAssembly(model, M, A, { y: 3.42, z: 0.3, r: 7.3, n: 4, chord: 0.55, tailX: 0.26, tailY: 3.6, tailZ: -9.1, tailR: 1.4, tailN: 4 });
  navLight(model.bodyGroup, A.navRed, 1.98, 1.95, 0.15);
  navLight(model.bodyGroup, A.navGreen, -1.98, 1.95, 0.15);
  model.strobe = navLight(model.bodyGroup, A.strobe, 0, 4.2, -9.35, 0.07);
  model.seats = [new THREE.Vector3(0, 1.28, 2.25), new THREE.Vector3(0, 1.08, 3.55), new THREE.Vector3(0, 1.3, 0), new THREE.Vector3(0, 1.3, -0.8)];
  model.visibleSeats = 2;
  model.doorPos = new THREE.Vector3(1.75, 0, 2.6);
  model.muzzles = [new THREE.Vector3(0, 0.82, 5.8)];
  model.pods = [new THREE.Vector3(1.3, 1.6, 1.1), new THREE.Vector3(-1.3, 1.6, 1.1)];
  return finish(model, [[body.build(), matteMaterial()], [trim.build(), M.trim]]);
}

function skylark(def, color) {
  const M = vehicleMaterials(), A = airMats();
  const model = shell(1.5);
  const paint = hex(color), white = hex(0xf2f2f2), win = [0.04, 0.06, 0.08];
  const col = (x, y, z) => {
    if (z > 1.05 && y > 1.12) return win;                              // bubble windscreen
    if (z > 0.1 && z < 1.05 && y > 1.62 && Math.abs(x) > 0.3) return win; // door windows
    if (y < 1.15 && z > -1.2) return paint;
    if (z < -1.6) return (Math.floor((z + 20) / 1.4) % 2) ? paint : white;
    return white;
  };
  const body = new GeoBuilder();
  body.addGeometry(fuselage([[2.55, 1.22, 0.05, 0.05], [2.35, 1.24, 0.46, 0.5], [1.8, 1.3, 0.76, 0.82], [0.8, 1.42, 0.86, 0.9], [-0.3, 1.5, 0.82, 0.86], [-1.2, 1.72, 0.55, 0.6], [-1.8, 1.96, 0.25, 0.3], [-7.2, 2.18, 0.12, 0.14], [-8.3, 2.25, 0.08, 0.1]], 22, 0.25, col), new THREE.Matrix4());
  body.addGeometry(pod(0, [[0.5, 2.25, 0.05], [0.2, 2.28, 0.45], [-1.2, 2.3, 0.42], [-1.8, 2.25, 0.1]], 14, () => white), mat4(0, 0, 0, 0, 0, 0, 1.2, 0.8, 1));
  for (const s of [1, -1]) body.addGeometry(wing([[0, 2.1, -6.0, 0.6, 0.06], [1.0, 2.1, -6.05, 0.45, 0.04]], s, () => paint), new THREE.Matrix4());
  body.addGeometry(fin([[1.85, 0, -7.9, 0.95, 0.09], [2.95, 0, -8.25, 0.55, 0.05]], () => paint), new THREE.Matrix4());
  const trim = new GeoBuilder();
  trim.set('color', 0.14, 0.14, 0.15);
  trim.addGeometry(new THREE.CylinderGeometry(0.12, 0.15, 0.5, 10), mat4(0, 2.62, -0.1));
  // skids
  trim.set('color', 0.62, 0.63, 0.65);
  for (const s of [1, -1]) {
    trim.addGeometry(cyl(0.05, 0.05, 3.1, 8), mat4(s * 0.98, 0.06, 0.25));
    strut(trim, new THREE.Vector3(s * 0.98, 0.06, 1.8), new THREE.Vector3(s * 0.98, 0.28, 2.15), 0.05);
    for (const z of [1.0, -0.7]) strut(trim, new THREE.Vector3(s * 0.98, 0.06, z), new THREE.Vector3(s * 0.5, 0.78, z), 0.045);
  }
  rotorAssembly(model, M, A, { y: 2.9, z: -0.1, r: 5.4, n: 2, chord: 0.36, tailX: 0.18, tailY: 2.4, tailZ: -8.05, tailR: 0.72, tailN: 2 });
  navLight(model.bodyGroup, A.navRed, 1.0, 2.1, -6.05, 0.06);
  navLight(model.bodyGroup, A.navGreen, -1.0, 2.1, -6.05, 0.06);
  model.strobe = navLight(model.bodyGroup, A.strobe, 0, 2.95, -8.3, 0.06);
  model.seats = [new THREE.Vector3(0.36, 0.78, 1.15), new THREE.Vector3(-0.36, 0.78, 1.15), new THREE.Vector3(0.36, 0.78, 0.1), new THREE.Vector3(-0.36, 0.78, 0.1)];
  model.visibleSeats = 0;
  model.doorPos = new THREE.Vector3(1.45, 0, 0.8);
  model.muzzles = [];
  return finish(model, [[body.build(), bodyMaterial(0xffffff)], [trim.build(), M.trim]]);
}

// ------------------------------------------------------------------ Mammoth: main battle tank
function mammoth(def, color) {
  const M = vehicleMaterials();
  const model = shell(1.2);
  const base = hex(color), dirt = mix(base, [0.35, 0.3, 0.22], 0.45);
  const body = new GeoBuilder();
  const extrudeSide = (pts, w, colr) => {
    const sh = new THREE.Shape();
    sh.moveTo(pts[0][0], pts[0][1]);
    for (let i = 1; i < pts.length; i++) sh.lineTo(pts[i][0], pts[i][1]);
    const g = new THREE.ExtrudeGeometry(sh, { depth: w * 2, bevelEnabled: true, bevelThickness: 0.04, bevelSize: 0.04, bevelSegments: 1 });
    g.rotateY(-Math.PI / 2);
    g.translate(w, 0, 0);
    body.set('color', ...colr);
    body.addGeometry(g, new THREE.Matrix4());
  };
  // upper hull (over the tracks) and the narrower lower hull
  extrudeSide([[4.0, 1.05], [2.85, 1.74], [-3.8, 1.78], [-4.0, 1.05]], 1.85, base);
  extrudeSide([[3.7, 0.5], [4.0, 1.06], [-4.0, 1.06], [-3.75, 0.5]], 1.2, dirt);
  // side skirts
  for (const s of [1, -1]) {
    body.set('color', ...base);
    body.box(s > 0 ? 1.84 : -1.92, 0.6, -3.95, s > 0 ? 1.92 : -1.84, 1.08, 3.85);
    body.set('color', ...dirt);
    body.box(s > 0 ? 1.84 : -1.92, 0.55, -3.95, s > 0 ? 1.93 : -1.83, 0.62, 3.85);
  }
  // rear engine grille + stowage
  const trim = new GeoBuilder();
  trim.set('color', 0.07, 0.07, 0.07);
  trim.box(-1.5, 1.79, -3.7, 1.5, 1.82, -2.4);
  for (let k = 0; k < 8; k++) { trim.set('color', 0.18, 0.18, 0.17); trim.box(-1.45, 1.82, -3.65 + k * 0.16, 1.45, 1.85, -3.6 + k * 0.16); }
  // tracks: upper & lower runs, curved ends
  for (const s of [1, -1]) {
    const x0 = s * 1.22, x1 = s * 1.82;
    trim.set('color', 0.09, 0.09, 0.085);
    trim.box(Math.min(x0, x1), 0.92, -3.6, Math.max(x0, x1), 1.02, 3.6);
    trim.box(Math.min(x0, x1), 0.02, -3.2, Math.max(x0, x1), 0.1, 3.2);
    for (const [z, r] of [[3.55, 0.48], [-3.55, 0.48]]) trim.addGeometry(new THREE.CylinderGeometry(r, r, 0.6, 16), mat4(s * 1.52, 0.52, z, 0, 0, Math.PI / 2));
  }
  // road wheels (spin) per side
  model.wheelSets = [];
  for (const s of [1, -1]) {
    const gb = new GeoBuilder();
    gb.set('color', 0.16, 0.17, 0.15);
    gb.addGeometry(new THREE.CylinderGeometry(0.36, 0.36, 0.52, 14), mat4(0, 0, 0, 0, 0, Math.PI / 2));
    gb.set('color', 0.3, 0.32, 0.27);
    gb.addGeometry(new THREE.CylinderGeometry(0.2, 0.2, 0.56, 10), mat4(0, 0, 0, 0, 0, Math.PI / 2));
    const geo = gb.build();
    const list = [];
    for (let k = 0; k < 7; k++) {
      const w = new THREE.Mesh(geo, M.trim);
      w.position.set(s * 1.52, 0.4, -2.85 + k * 0.95);
      w.castShadow = true;
      model.bodyGroup.add(w);
      list.push(w);
    }
    model.wheelSets.push(list);
  }
  // turret
  const turret = new THREE.Group();
  turret.position.set(0, 1.78, -0.35);
  const tgb = new GeoBuilder();
  const outline = [[0.95, 2.45], [1.75, 1.2], [1.78, -1.7], [1.35, -2.55], [-1.35, -2.55], [-1.78, -1.7], [-1.75, 1.2], [-0.95, 2.45]];
  const ts = new THREE.Shape();
  ts.moveTo(outline[0][0], -outline[0][1]);
  for (let i = 1; i < outline.length; i++) ts.lineTo(outline[i][0], -outline[i][1]);
  const tg = new THREE.ExtrudeGeometry(ts, { depth: 0.8, bevelEnabled: true, bevelThickness: 0.1, bevelSize: 0.1, bevelSegments: 2 });
  tg.rotateX(-Math.PI / 2);
  tg.translate(0, 0.08, 0);
  tgb.set('color', ...base);
  tgb.addGeometry(tg, new THREE.Matrix4());
  // bustle rack, cupola, smoke launchers, antennas
  tgb.set('color', ...dirt);
  tgb.box(-1.3, 0.3, -3.1, 1.3, 0.75, -2.62, { top: true });
  tgb.set('color', ...base);
  tgb.addGeometry(new THREE.CylinderGeometry(0.38, 0.42, 0.3, 12), mat4(0.8, 1.1, -0.7));
  tgb.addGeometry(new THREE.CylinderGeometry(0.3, 0.32, 0.2, 10), mat4(-0.8, 1.05, -0.4));
  for (const s of [1, -1]) for (let k = 0; k < 3; k++) { tgb.set('color', 0.2, 0.22, 0.18); tgb.addGeometry(cyl(0.07, 0.07, 0.35, 8), mat4(s * (1.55 - k * 0.14), 0.75, 1.3 + k * 0.05, -0.4, 0, 0)); }
  tgb.set('color', 0.1, 0.1, 0.1);
  for (const s of [1, -1]) tgb.addGeometry(new THREE.CylinderGeometry(0.015, 0.02, 2.2, 4), mat4(s * 1.2, 1.9, -2.3, 0.15, 0, 0));
  tgb.addGeometry(cyl(0.035, 0.035, 1.0, 6), mat4(0.8, 1.45, -0.3));
  tgb.box(0.7, 1.28, -0.9, 0.9, 1.44, -0.35);
  const turretMesh = new THREE.Mesh(tgb.build(), matteMaterial());
  turretMesh.castShadow = true; turretMesh.receiveShadow = true;
  turret.add(turretMesh);
  // gun: mantlet + barrel on an elevation pivot
  const gunPivot = new THREE.Group();
  gunPivot.position.set(0, 0.46, 2.35);
  const ggb = new GeoBuilder();
  ggb.set('color', ...base);
  ggb.box(-0.5, -0.3, -0.25, 0.5, 0.3, 0.35);
  ggb.set('color', ...mix(base, [0.2, 0.2, 0.2], 0.25));
  ggb.addGeometry(cyl(0.13, 0.11, 5.3, 14), mat4(0, 0, 3.0));
  ggb.addGeometry(cyl(0.17, 0.17, 0.8, 14), mat4(0, 0, 2.6));
  ggb.set('color', 0.08, 0.08, 0.08);
  ggb.addGeometry(cyl(0.13, 0.13, 0.25, 14), mat4(0, 0, 5.55));
  const gunMesh = new THREE.Mesh(ggb.build(), matteMaterial());
  gunMesh.castShadow = true;
  gunPivot.add(gunMesh);
  turret.add(gunPivot);
  model.bodyGroup.add(turret);
  model.turret = turret; model.gunPivot = gunPivot; model.gun = gunMesh;
  // commander hatch = the door
  const pivot = model.door.pivot;
  turret.add(pivot);
  pivot.position.set(0.8 + 0.36, 1.26, -0.7);
  const hatch = new THREE.Mesh(new THREE.CylinderGeometry(0.34, 0.34, 0.06, 12).translate(-0.36, 0, 0), matteMaterial());
  pivot.add(hatch);
  model.door.axis = 'z'; model.door.max = -1.9;
  model.seats = [new THREE.Vector3(0, 0.9, 0), new THREE.Vector3(0.4, 0.9, 1.5), new THREE.Vector3(-0.4, 0.9, -1), new THREE.Vector3(0.4, 0.9, -1)];
  model.visibleSeats = 0;
  model.doorPos = new THREE.Vector3(2.45, 0, 0.4);
  return finish(model, [[body.build(), matteMaterial()], [trim.build(), M.trim]]);
}
