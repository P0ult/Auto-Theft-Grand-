// Procedural vehicle meshes: lofted bodywork with wheel arches, a glass greenhouse and painted pillars,
// lamps laid onto the curved nose and tail, bumpers, grilles, interior, wheels, an opening driver door,
// police light bars and taxi signs. Panels that can come off in a crash are separate meshes.
import * as THREE from 'three';
import { GeoBuilder, mat4 } from '../world/geom.js';
import { Part, emitGrid, stations, smoothLine, sweep, roundBox } from './loft.js';
import { patch } from '../render/materials.js';
import { clamp } from '../core/utils.js';

// Shared materials
let MATS = null;
export function vehicleMaterials() {
  if (MATS) return MATS;
  const phys = (p, key) => patch(new THREE.MeshPhysicalMaterial(p), { key });
  MATS = {
    glass: phys({ color: 0x070a0d, metalness: 0.3, roughness: 0.04, transparent: true, opacity: 0.8, clearcoat: 1, clearcoatRoughness: 0.02 }, 'vglass'),
    trim: patch(new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.55, metalness: 0.35 }), { key: 'vtrim' }),
    wheel: patch(new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.5, metalness: 0.45 }), { key: 'vwheel' }),
    headOff: patch(new THREE.MeshStandardMaterial({ color: 0xdddddd, roughness: 0.1, metalness: 0.8, emissive: 0x222222 }), { key: 'vlight' }),
    headOn: patch(new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.1, metalness: 0.1, emissive: new THREE.Color(6, 5.6, 4.6) }), { key: 'vlight' }),
    tailOff: patch(new THREE.MeshStandardMaterial({ color: 0x5a0000, roughness: 0.2, metalness: 0.3, emissive: new THREE.Color(0.25, 0, 0) }), { key: 'vlight' }),
    tailOn: patch(new THREE.MeshStandardMaterial({ color: 0x8a0000, roughness: 0.2, metalness: 0.3, emissive: new THREE.Color(1.6, 0.05, 0.03) }), { key: 'vlight' }),
    tailBrake: patch(new THREE.MeshStandardMaterial({ color: 0xaa0000, roughness: 0.2, metalness: 0.3, emissive: new THREE.Color(5, 0.1, 0.05) }), { key: 'vlight' }),
    burnt: patch(new THREE.MeshStandardMaterial({ color: 0x151210, roughness: 0.95, metalness: 0.2 }), { key: 'burnt' }),
    chrome: patch(new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.16, metalness: 0.95 }), { key: 'vchrome', fragEnd: 'atgRefl = 0.3;' }),
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


// ================================================================== bodywork
// Every car is lofted from cross-sections along its length: a lower body (rounded shoulders, a waist,
// tumblehome, wheel arches cut around the tyres, a nose and tail rounded in plan and profile) and a
// greenhouse (windscreen, roof and rear window as one smooth surface, split into glass and painted
// pillars). The bonnet, boot lid, doors and bumpers are separate panels so they can open, dent and
// be torn off in a crash.

const NS = 7, NA = 5, NT = 6;    // lower body: side, shoulder and top segments (per half section)
const NS2 = 5, NA2 = 4, NT2 = 6; // greenhouse

function carDesign(def) {
  const L = def.L, W = def.W, H = def.H, c = def.clearance, f = L / 2, r = -L / 2;
  const D = {
    f, r, L, W, H, c, R: def.wheelR, wb: def.wheelbase, track: def.track, tireW: def.body === 'truck' ? 0.32 : 0.24,
    rt: 0.1, tumble: 0.05, under: 0.06, belly: 0.45, crown: 0.03, clad: 0,
    nose: { z: 0.42, x: 0.16, vz: 0.14, top: 0.05, chin: 0.1 },
    tail: { z: 0.32, x: 0.1, vz: 0.12, top: 0.04, chin: 0.12 },
    cab: { belt: 0.1, inset: 0.2, rr: 0.08, crown: 0.035 },
    doors: 4, doorLen: 1.12, pillarC: 0.34, rearGlass: true, sideGlass: null,
    bumper: 'body', bumperH: 0.22, grille: 'wide', lamps: 'wide', tails: 'wide', trunk: true, bed: null, cargo: null,
    fender: 0, extras: {},
  };
  const set = (o) => { for (const k in o) D[k] = typeof o[k] === 'object' && o[k] && !Array.isArray(o[k]) && D[k] && typeof D[k] === 'object' ? { ...D[k], ...o[k] } : o[k]; };
  switch (def.body) {
    case 'coupe': set({
      line: [[f, 0.6], [f - 0.4, 0.69], [f - 1.45, 0.81], [r + 0.9, 0.86], [r + 0.25, 0.87], [r, 0.82]],
      cab: { bf: f - 1.45, tf: f - 2.15, tr: r + 1.55, br: r + 0.8, top: H, inset: 0.24, rr: 0.1 },
      rt: 0.13, tumble: 0.07, doors: 2, doorLen: 1.3, pillarC: 0.3, nose: { z: 0.5, x: 0.22, vz: 0.18, top: 0.05, chin: 0.08 },
      grille: 'slim', lamps: 'swept', tails: 'slim', fender: 0.02, extras: { lip: true, sideskirt: true, twinExhaust: true },
    }); break;
    case 'muscle': set({
      line: [[f, 0.79], [f - 0.3, 0.85], [f - 1.9, 0.9], [r + 1.25, 0.92], [r + 0.2, 0.93], [r, 0.9]],
      cab: { bf: f - 1.9, tf: f - 2.45, tr: r + 1.65, br: r + 1.05, top: H, inset: 0.17, rr: 0.05, crown: 0.02 },
      rt: 0.05, tumble: 0.025, under: 0.035, belly: 0.55, doors: 2, doorLen: 1.3, pillarC: 0.38,
      nose: { z: 0.2, x: 0.07, vz: 0.07, top: 0.02, chin: 0.05 }, tail: { z: 0.16, x: 0.05, vz: 0.06, top: 0.02, chin: 0.06 },
      bumper: 'chrome', bumperH: 0.16, grille: 'muscle', lamps: 'round2', tails: 'bar', extras: { scoop: true, stripes: true, twinExhaust: true },
    }); break;
    case 'suv': set({
      line: [[f, 0.98], [f - 0.35, 1.05], [f - 1.3, 1.11], [r + 0.2, 1.13], [r, 1.1]],
      cab: { bf: f - 1.3, tf: f - 1.95, tr: r + 0.28, br: r + 0.12, top: H, belt: 0.09, inset: 0.16, rr: 0.09 },
      rt: 0.09, tumble: 0.04, clad: 0.2, doors: 4, doorLen: 1.05, pillarC: 0.26, trunk: false,
      nose: { z: 0.36, x: 0.13, vz: 0.12, top: 0.05, chin: 0.1 }, tail: { z: 0.22, x: 0.08, vz: 0.1, top: 0.03, chin: 0.1 },
      bumper: 'dark', bumperH: 0.26, grille: 'tall', lamps: 'wide', tails: 'tall', fender: 0.02, extras: { rails: true, steps: true, spare: def.military },
    }); break;
    case 'pickup': set({
      line: [[f, 0.98], [f - 0.35, 1.05], [f - 1.45, 1.1], [f - 3.0, 1.12], [r + 0.05, 1.14], [r, 1.12]],
      cab: { bf: f - 1.45, tf: f - 2.05, tr: f - 2.92, br: f - 3.0, top: H, belt: 0.09, inset: 0.14, rr: 0.08 },
      rt: 0.08, tumble: 0.035, clad: 0.12, doors: 2, doorLen: 1.2, pillarC: 0.12, trunk: false, bed: { z0: r + 0.12, z1: f - 3.1, floor: 0.74 },
      nose: { z: 0.3, x: 0.1, vz: 0.1, top: 0.04, chin: 0.1 }, tail: { z: 0.1, x: 0.04, vz: 0.06, top: 0.02, chin: 0.1 },
      bumper: 'chrome', bumperH: 0.24, grille: 'truck', lamps: 'tall', tails: 'tall', extras: { steps: true },
    }); break;
    case 'van': set({
      line: [[f, 0.92], [f - 0.4, 1.0], [f - 0.95, 1.06], [r + 0.05, 1.08], [r, 1.05]],
      cab: { bf: f - 0.95, tf: f - 1.6, tr: r + 0.07, br: r, top: H, belt: 0.08, inset: 0.12, rr: 0.1 },
      rt: 0.08, tumble: 0.03, doors: 2, doorLen: 0.92, pillarC: 0.0, trunk: false, rearGlass: false, sideGlass: [f - 2.05, f],
      nose: { z: 0.36, x: 0.16, vz: 0.16, top: 0.06, chin: 0.1 }, tail: { z: 0.12, x: 0.05, vz: 0.05, top: 0.02, chin: 0.08 },
      bumper: 'dark', bumperH: 0.24, grille: 'tall', lamps: 'tall', tails: 'tall', extras: { slider: !def.armored },
    }); break;
    case 'truck': set({
      line: [[f, 1.2], [f - 0.3, 1.3], [f - 0.6, 1.35], [f - 2.3, 1.36], [r, 1.36]],
      cab: { bf: f - 0.6, tf: f - 1.0, tr: f - 2.18, br: f - 2.25, top: 2.6, belt: 0.07, inset: 0.12, rr: 0.12 },
      rt: 0.08, tumble: 0.03, under: 0.04, doors: 2, doorLen: 0.95, pillarC: 0.0, trunk: false, rearGlass: true,
      nose: { z: 0.22, x: 0.12, vz: 0.12, top: 0.04, chin: 0.06 }, tail: { z: 0.06, x: 0.03, vz: 0.04, top: 0.01, chin: 0.04 },
      bumper: 'dark', bumperH: 0.3, grille: 'truck', lamps: 'tall', tails: 'tall',
      cargo: { z0: r + 0.02, z1: f - 2.4, y0: 1.2, y1: H, canvas: !!def.military },
      extras: { mudflaps: true, stacks: !def.military },
    }); break;
    case 'super': set({
      line: [[f, 0.48], [f - 0.6, 0.6], [f - 1.45, 0.75], [r + 0.6, 0.86], [r + 0.1, 0.86], [r, 0.8]],
      cab: { bf: f - 1.45, tf: f - 2.15, tr: r + 1.45, br: r + 0.65, top: H, belt: 0.14, inset: 0.3, rr: 0.12, crown: 0.05 },
      rt: 0.16, tumble: 0.09, under: 0.05, belly: 0.35, doors: 2, doorLen: 1.2, pillarC: 0.45, trunk: false,
      nose: { z: 0.55, x: 0.26, vz: 0.22, top: 0.04, chin: 0.04 }, tail: { z: 0.3, x: 0.12, vz: 0.12, top: 0.03, chin: 0.1 },
      grille: 'intake', lamps: 'slit', tails: 'slit', bumperH: 0.16, fender: 0.05, extras: { wing: true, intakes: true, louvres: true, twinExhaust: true, sideskirt: true },
    }); break;
    case 'lowrider': set({
      line: [[f, 0.74], [f - 0.3, 0.8], [f - 1.7, 0.84], [r + 1.35, 0.86], [r + 0.2, 0.87], [r, 0.82]],
      cab: { bf: f - 1.7, tf: f - 2.35, tr: r + 1.85, br: r + 1.25, top: H, inset: 0.15, rr: 0.07, crown: 0.025 },
      rt: 0.06, tumble: 0.03, under: 0.04, belly: 0.55, doors: 2, doorLen: 1.4, pillarC: 0.42,
      nose: { z: 0.24, x: 0.08, vz: 0.08, top: 0.02, chin: 0.05 }, tail: { z: 0.2, x: 0.06, vz: 0.07, top: 0.02, chin: 0.07 },
      bumper: 'chrome', bumperH: 0.15, grille: 'chrome', lamps: 'quad', tails: 'round', extras: { trimLine: true, whitewall: true },
    }); break;
    case 'hatch': set({
      line: [[f, 0.7], [f - 0.3, 0.79], [f - 1.15, 0.9], [r + 0.25, 0.98], [r, 0.94]],
      cab: { bf: f - 1.15, tf: f - 1.88, tr: r + 0.42, br: r + 0.12, top: H, belt: 0.1, inset: 0.19, rr: 0.1 },
      rt: 0.12, tumble: 0.05, doors: 4, doorLen: 1.0, pillarC: 0.34, trunk: false,
      nose: { z: 0.36, x: 0.16, vz: 0.14, top: 0.05, chin: 0.08 }, tail: { z: 0.18, x: 0.08, vz: 0.08, top: 0.03, chin: 0.1 },
      grille: 'slim', lamps: 'swept', tails: 'tall',
    }); break;
    default: set({ // sedan (Meridian, cab, police cruiser)
      line: [[f, 0.76], [f - 0.35, 0.83], [f - 1.0, 0.88], [f - 1.45, 0.93], [r + 0.85, 0.97], [r + 0.35, 0.98], [r, 0.93]],
      cab: { bf: f - 1.45, tf: f - 2.2, tr: r + 1.4, br: r + 0.85, top: H },
      doors: 4, extras: { pushbar: def.police },
    });
  }
  D.line = smoothLine(D.line);
  D.zf = D.wb / 2; D.zr = -D.wb / 2;
  D.aR = D.R + 0.07;
  D.xi = D.track / 2 - D.tireW / 2 - 0.05; // inner wall of the wheel wells
  // doors: front door from just behind the windscreen base; rear door on four-doors
  const C = D.cab;
  D.door = { z0: C.bf - 0.02, z1: Math.max(C.bf - 0.02 - D.doorLen, C.br + 0.12) };
  if (D.doors === 4) D.door2 = { z0: D.door.z1, z1: Math.max(D.door.z1 - 0.95, D.zr + D.aR + 0.03, C.br + 0.05) };
  // the roofline: windscreen, roof, rear window; corners rounded by local averaging
  const top = C.top;
  const pl = (z) => {
    if (z >= C.tf) return top + (D.line(C.bf) - top) * clamp((z - C.tf) / Math.max(0.01, C.bf - C.tf), 0, 1);
    if (z <= C.tr) return top + (D.line(C.br) - top) * clamp((C.tr - z) / Math.max(0.01, C.tr - C.br), 0, 1);
    return top;
  };
  D.roof = (z) => {
    const rad = Math.min(0.14, (C.bf - z) * 0.7, (z - C.br) * 0.7);
    if (rad < 0.01) return pl(z);
    let s = 0, n = 0;
    for (let k = -4; k <= 4; k++) { const w = 5 - Math.abs(k); s += pl(z + k / 4 * rad) * w; n += w; }
    return s / n;
  };
  return D;
}

// cross-section parameters at station z
function sectionAt(D, z) {
  const ne = (d, e) => (d >= e ? 0 : 1 - Math.sqrt(Math.max(0, 1 - ((e - d) / e) ** 2)));
  const dF = D.f - z, dR = z - D.r;
  const w = D.W / 2 - Math.max(ne(dF, D.nose.z) * D.nose.x, ne(dR, D.tail.z) * D.tail.x);
  const vF = ne(dF, D.nose.vz), vR = ne(dR, D.tail.vz);
  let y1 = D.line(z) - vF * D.nose.top - vR * D.tail.top;
  const y0 = D.c + vF * D.nose.chin + vR * D.tail.chin;
  let rt = Math.min(D.rt, (y1 - y0) * 0.35), crown = D.crown;
  // wings swell over the wheels where the bonnet line is low: the shoulders rise around the arch while
  // the bonnet between them stays put (a valley between the wings)
  for (const zc of [D.zf, D.zr]) {
    const dz = Math.abs(z - zc), rr = D.aR + 0.12;
    if (dz >= rr) continue;
    const need = D.R + Math.sqrt(rr * rr - dz * dz) - 0.06 + D.fender;
    rt = Math.min(rt, Math.max(0.035, y1 - need));
    if (need + 0.035 > y1) { const d = need + 0.035 - y1; y1 += d; crown -= d; }
  }
  return { z, w, y0, y1, rt, crown, yB: y0 + (y1 - rt - y0) * D.belly };
}
function sideX(D, s, y) {
  const top = s.y1 - s.rt;
  if (y <= s.yB) { const t = clamp((y - s.y0) / Math.max(1e-4, s.yB - s.y0), 0, 1); return s.w - D.under * (1 - t) * (1 - t); }
  const t = clamp((y - s.yB) / Math.max(1e-4, top - s.yB), 0, 1);
  return s.w - D.tumble * t * t;
}
function topY(D, s, x) {
  const xs = sideX(D, s, s.y1 - s.rt), xt = xs - s.rt;
  x = Math.abs(x);
  if (x <= xt) return s.y1 + s.crown * (1 - (x / Math.max(0.01, xt)) ** 2);
  const dx = Math.min(s.rt, x - xt);
  return s.y1 - s.rt + Math.sqrt(Math.max(0, s.rt * s.rt - dx * dx));
}
// lower-body half ring from the side bottom (ys) up and over to the centre line
function lowerRing(D, s, ys) {
  const pts = [];
  const top = s.y1 - s.rt;
  ys = Math.min(ys, top - 0.005);
  for (let j = 0; j <= NS; j++) { const y = ys + (top - ys) * j / NS; pts.push([sideX(D, s, y), y]); }
  const cx = sideX(D, s, top) - s.rt;
  for (let k = 1; k <= NA; k++) { const a = (k / NA) * Math.PI / 2; pts.push([cx + Math.cos(a) * s.rt, top + Math.sin(a) * s.rt]); }
  for (let k = 1; k <= NT; k++) { const x = cx * (1 - k / NT); pts.push([x, s.y1 + s.crown * (1 - (x / Math.max(0.01, cx)) ** 2)]); }
  return pts;
}
function cabRing(D, s) {
  const C = D.cab;
  const xb = s.w - C.belt, yb = topY(D, s, xb) - 0.004;
  const yc = Math.max(yb, D.roof(s.z));
  const xr = Math.min(xb - 0.01, s.w - C.inset);
  const rr = Math.max(0, Math.min(C.rr, (yc - yb) * 0.6, xr * 0.4));
  const pts = [];
  for (let j = 0; j <= NS2; j++) { const t = j / NS2; pts.push([xb + (xr - xb) * (0.75 * t + 0.25 * t * t), yb + (yc - rr - yb) * t]); }
  for (let k = 1; k <= NA2; k++) { const a = (k / NA2) * Math.PI / 2; pts.push([xr - rr + Math.cos(a) * rr, yc - rr + Math.sin(a) * rr]); }
  const x0 = xr - rr;
  for (let k = 1; k <= NT2; k++) { const x = x0 * (1 - k / NT2); pts.push([x, yc + C.crown * (1 - (x / Math.max(0.01, x0)) ** 2) * clamp((yc - yb) / 0.2, 0, 1)]); }
  return { pts, yb, yc, xb, xr };
}

const TPL = new Map();
function carTemplate(def) {
  let T = TPL.get(def);
  if (T) return T;
  const D = carDesign(def);
  const { f, r, W, c, R, zf, zr, aR, xi } = D;
  const C = D.cab;
  const P = {
    body: new Part(), hood: new Part(), trunk: new Part(), door: new Part(), door2: new Part(),
    glass: new Part(), doorGlass: new Part(), trim: new Part(), chrome: new Part(), dark: new Part(),
    head: new Part(), tail: new Part(), bumperF: new Part(), bumperR: new Part(),
  };
  const DARK = [0.035, 0.035, 0.037];
  P.dark.color(...DARK);
  const inDoor = (z) => (z < D.door.z0 && z > D.door.z1) || (D.door2 && z < D.door2.z0 && z > D.door2.z1);
  const inFront = (z) => z < D.door.z0 && z > D.door.z1;
  // livery / cladding colours for painted panels (multiplied with the paint colour)
  const police = !!def.police;
  const bodyCol = (x, y, z) => {
    if (police) {
      const white = (y > D.line(z) - 0.06 && z < C.bf + 0.05 && z > C.br - 0.05) || (z < D.door.z0 + 0.02 && z > (D.door2 ? D.door2.z1 : D.door.z1 - 0.8));
      return white && y > c + 0.1 ? [1, 1, 1] : [0.045, 0.045, 0.05];
    }
    if (D.clad && y < c + D.clad) return [0.09, 0.09, 0.09];
    return [1, 1, 1];
  };

  // ---------------- lower body, split at the wheel arches
  const breaks = [r, zr - aR, zr + aR, zf - aR, zf + aR, f];
  const feat = [C.bf, C.br, C.tf, C.tr, D.door.z0, D.door.z1, D.door2?.z0, D.door2?.z1, f - D.nose.z, r + D.tail.z, f - D.nose.vz, r + D.tail.vz, D.bed?.z0, D.bed?.z1, D.cargo?.z1].filter((v) => v != null);
  for (let k = 0; k < 5; k++) {
    const z0 = breaks[k], z1 = breaks[k + 1];
    const arch = k === 1 || k === 3;
    const zc = k === 1 ? zr : zf;
    const zsL = stations(z0, z1, 0.075, feat);
    const S = zsL.map((z) => sectionAt(D, z));
    const ysOf = (s) => (arch ? Math.max(s.y0, R + Math.sqrt(Math.max(0, aR * aR - (s.z - zc) ** 2))) : s.y0);
    const rings = S.map((s) => lowerRing(D, s, ysOf(s)));
    for (const side of [1, -1]) {
      const rows = rings.map((ring, i) => ring.map(([x, y]) => [x * side, y, S[i].z]));
      emitGrid(rows, (i, j) => {
        const z = (S[i].z + S[i + 1].z) / 2;
        const kind = j < NS ? 0 : j < NS + NA ? 1 : 2;
        if (kind < 2 || (kind === 2 && j < NS + NA + 2 && z < C.bf && z > C.br)) {
          if (inFront(z)) return side > 0 ? P.door : P.door2;
          if (inDoor(z) && kind < 2) return P.body;
        }
        if (kind === 2) {
          if (z < C.bf - 0.01 && z > C.br + 0.01) return null; // under the greenhouse (the cabin tub shows through the glass)
          if (D.bed && z > D.bed.z0 && z < D.bed.z1) return null;
          if (D.cargo && z < D.cargo.z1 + 0.02) return null;
          if (z > C.bf + 0.02 && z < f - D.nose.vz * 0.5 && def.body !== 'truck') return P.hood;
          if (D.trunk && z < C.br - 0.02 && z > r + D.tail.vz * 0.5) return P.trunk;
        }
        return P.body;
      }, { ref: [0, (c + 1) / 2, rows[0][0][2]], colorAt: (x, y, z) => bodyCol(x, y, z) });
      // wheel well: inner wall and arch lining
      if (arch) {
        const wall = S.map((s) => [[xi * side, s.y0, s.z], [xi * side, ysOf(s), s.z]]);
        emitGrid(wall, () => P.dark, { ref: [0, 0.8, zc] });
        const lin = S.map((s, i) => [[xi * side, ysOf(s), s.z], [rings[i][0][0] * side, ysOf(s), s.z]]);
        emitGrid(lin, () => P.dark, { ref: [0, 2, zc] });
        // the side panel's end faces where the arch starts and stops
        for (const e of [0, S.length - 1]) {
          const s = S[e], ys = ysOf(s);
          if (ys - s.y0 < 0.01) continue;
          const pts = [[xi * side, s.y0, s.z]];
          for (let q = 0; q <= 4; q++) { const y = s.y0 + (ys - s.y0) * q / 4; pts.push([sideX(D, s, y) * side, y, s.z]); }
          pts.push([xi * side, ys, s.z]);
          P.dark.poly(pts, [0, 0, s.z > zc ? -1 : 1]);
        }
      }
      // underbody
      const fl = S.map((s) => [[0, s.y0, s.z], [(arch ? xi : sideX(D, s, s.y0)) * side, s.y0, s.z]]);
      emitGrid(fl, () => P.dark, { ref: [0, 3, 0] });
    }
    // nose and tail caps
    for (const e of (k === 0 ? [0] : k === 4 ? [S.length - 1] : [])) {
      const s = S[e], ring = rings[e];
      const pts = ring.map(([x, y]) => [x, y, s.z]);
      for (let q = ring.length - 2; q >= 0; q--) pts.push([-ring[q][0], ring[q][1], s.z]);
      pts.push([-ring[0][0], s.y0, s.z], [ring[0][0], s.y0, s.z]);
      P.body.poly(pts.map((p) => p), [0, 0, e === 0 ? -1 : 1]);
    }
  }

  // ---------------- greenhouse
  {
    const pillars = [];
    if (D.doors === 4 && D.door2) pillars.push([D.door.z1 - 0.05, D.door.z1 + 0.07]);
    else if (D.door.z1 > C.br + 0.25 && D.pillarC > 0) pillars.push([D.door.z1 - 0.04, D.door.z1 + 0.05]);
    if (D.pillarC > 0) pillars.push([C.br - 0.01, C.br + D.pillarC]);
    if (def.body === 'suv') pillars.push([D.door2.z1 - 0.05, D.door2.z1 + 0.05]);
    const zsC = stations(C.br, C.bf, 0.06, [C.tf, C.tr, D.door.z0, D.door.z1, D.door2?.z0, D.door2?.z1, ...pillars.flat(), ...(D.sideGlass || [])].filter((v) => v != null));
    const S = zsC.map((z) => sectionAt(D, z));
    const rings = S.map((s) => cabRing(D, s));
    const inPillar = (z) => pillars.some(([a, b]) => z > a && z < b) || (D.sideGlass && (z < D.sideGlass[0] || z > D.sideGlass[1]));
    for (const side of [1, -1]) {
      const rows = rings.map((rg, i) => rg.pts.map(([x, y]) => [x * side, y, S[i].z]));
      emitGrid(rows, (i, j) => {
        const z = (S[i].z + S[i + 1].z) / 2;
        const h = rings[i].yc - rings[i].yb;
        if (j < NS2) {
          if (h < 0.03) return P.body;
          if (inPillar(z)) return P.body;
          return inFront(z) && side > 0 ? P.doorGlass : P.glass;
        }
        if (j < NS2 + NA2) return P.body;
        if (z > C.tr + 0.005 && z < C.tf - 0.005) return P.body; // roof
        if (z >= C.tf) return P.glass;
        return D.rearGlass ? P.glass : P.body;
      }, { ref: [0, 0.8, (C.bf + C.br) / 2], colorAt: (x, y, z) => bodyCol(x, y, z), offset: (part) => (part === P.glass || part === P.doorGlass ? -0.008 : 0) });
    }
    // headliner and pillar trim on the inside (seen through the glass)
    for (const side of [1, -1]) {
      const rows = rings.map((rg, i) => rg.pts.map(([x, y]) => [x * side, y, S[i].z]));
      emitGrid(rows, (i, j) => {
        const z = (S[i].z + S[i + 1].z) / 2;
        if (j < NS2) return inPillar(z) ? P.trim : null;
        if (j < NS2 + NA2) return P.trim;
        return z > C.tr + 0.005 && z < C.tf - 0.005 ? P.trim : null;
      }, { ref: [0, 0.8, (C.bf + C.br) / 2], inward: true, offset: () => 0.02, colorAt: () => [0.55, 0.53, 0.5] });
    }
    // window seals along the belt, and the cowl below the windscreen
    for (const side of [1, -1]) {
      const path = [];
      for (let i = 0; i < S.length; i++) { const rg = rings[i]; if (rg.yc - rg.yb > 0.05) path.push([rg.xb * side, rg.yb + 0.004, S[i].z]); }
      if (path.length > 2) sweep(P.dark, path, [[-0.012, -0.004], [0.012, -0.004], [0.012, 0.018], [-0.012, 0.018]], {});
    }
    D._cabS = S; D._cabR = rings;
  }

  // ---------------- pickup bed / cargo box / interior tub
  if (D.bed) {
    const b = D.bed;
    const s0 = sectionAt(D, (b.z0 + b.z1) / 2);
    const xin = sideX(D, s0, s0.y1 - s0.rt) - 0.07;
    P.dark.hex(0x1d1d1f).box(-xin, b.floor - 0.03, b.z0, xin, b.floor, b.z1);
    P.body.color(1, 1, 1);
    for (const sd of [1, -1]) P.body.box(sd > 0 ? xin - 0.012 : -xin, b.floor, b.z0, sd > 0 ? xin : -xin + 0.012, s0.y1 + 0.005, b.z1);
    P.body.box(-xin, b.floor, b.z1 - 0.012, xin, s0.y1 + 0.005, b.z1);
    P.body.box(-xin, b.floor, b.z0, xin, s0.y1 + 0.005, b.z0 + 0.012);
    // wheel tubs in the bed
    P.dark.hex(0x151515);
    for (const sd of [1, -1]) roundBox(P.dark, sd > 0 ? xi - 0.02 : -xin, sd > 0 ? xin : -xi + 0.02, b.floor, R + aR - 0.06, zr - aR + 0.02, zr + aR - 0.02, 0.1, 3);
    P.dark.color(...DARK);
  }
  if (D.cargo) {
    const g = D.cargo, cw = W / 2 + 0.02;
    if (g.canvas) {
      // army truck: a canvas tilt on hoops
      P.trim.hex(0x4a5230);
      const sec = [];
      for (let k = 0; k <= 8; k++) { const a = Math.PI * k / 8; sec.push([Math.cos(a) * cw, g.y1 - 0.45 + Math.sin(a) * 0.45]); }
      sec.push([-cw, g.y0 + 0.35], [cw, g.y0 + 0.35]);
      const rows = [g.z0, g.z1].map((z) => { const row = sec.map(([x, y]) => [x, y, z]); row.push(row[0].slice()); return row; });
      emitGrid(rows, () => P.trim, { ref: [0, (g.y0 + g.y1) / 2, (g.z0 + g.z1) / 2] });
      P.trim.poly(sec.map(([x, y]) => [x, y, g.z1]), [0, 0, 1]);
      P.trim.poly(sec.map(([x, y]) => [x, y, g.z0]).reverse(), [0, 0, -1]);
      P.body.color(1, 1, 1);
      P.body.box(-cw, g.y0, g.z0, cw, g.y0 + 0.36, g.z1, true);
      P.trim.color(1, 1, 1);
    } else {
      P.body.color(0.96, 0.96, 0.96);
      roundBox(P.body, -cw, cw, g.y0, g.y1, g.z0, g.z1, 0.06, 2);
      P.body.color(1, 1, 1);
      // roll-up door and the box's corner posts
      P.trim.hex(0x9a9a9a);
      for (let k = 1; k < 10; k++) { const y = g.y0 + 0.15 + (g.y1 - g.y0 - 0.3) * k / 10; P.trim.box(-cw + 0.12, y - 0.006, g.z0 - 0.012, cw - 0.12, y + 0.006, g.z0 - 0.002); }
      P.trim.hex(0x2a2a2a);
      P.trim.box(-cw + 0.1, g.y0 + 0.1, g.z0 - 0.02, -cw + 0.16, g.y1 - 0.08, g.z0);
      P.trim.box(cw - 0.16, g.y0 + 0.1, g.z0 - 0.02, cw - 0.1, g.y1 - 0.08, g.z0);
    }
    P.dark.hex(0x151515).box(-0.5, c + 0.1, g.z0 + 0.3, 0.5, g.y0, g.z1 - 0.2); // chassis rails
    P.dark.color(...DARK);
  }
  {
    // cabin tub: inner door cards, floor, firewall and rear bulkhead (seen through the glass / open door)
    const zb = C.br + 0.02, zf2 = C.bf - 0.04;
    const sm = sectionAt(D, (zb + zf2) / 2);
    const xw = sideX(D, sm, sm.y1 - sm.rt) - 0.06, yt = sm.y1 - 0.02;
    P.trim.hex(0x2b2724);
    for (const sd of [1, -1]) {
      const pts = [[sd * xw, c + 0.06, zb], [sd * xw, c + 0.06, zf2], [sd * xw, yt, zf2], [sd * xw, yt, zb]];
      P.trim.poly(pts, [-sd, 0, 0]);
    }
    P.trim.hex(0x1c1a19);
    P.trim.poly([[-xw, c + 0.06, zb], [xw, c + 0.06, zb], [xw, c + 0.06, zf2], [-xw, c + 0.06, zf2]], [0, 1, 0]);
    P.trim.poly([[-xw, c + 0.06, zf2], [xw, c + 0.06, zf2], [xw, yt, zf2], [-xw, yt, zf2]], [0, 0, -1]);
    if (!D.cargo && def.body !== 'van') P.trim.poly([[-xw, c + 0.06, zb], [xw, c + 0.06, zb], [xw, yt, zb], [-xw, yt, zb]], [0, 0, 1]);
    D._tub = { xw, yt };
  }

  // ---------------- bumpers (swept around the nose and tail)
  const mkBumper = (front) => {
    const part = front ? P.bumperF : P.bumperR;
    const yb0 = c + 0.03 + (front ? 0 : 0.03), yb1 = yb0 + D.bumperH, ym = (yb0 + yb1) / 2;
    const zEnd = front ? f : r;
    const ez = front ? D.nose.z : D.tail.z;
    const path = [];
    const zSide = front ? f - ez - 0.12 : r + ez + 0.12;
    const n = 10;
    for (let k = 0; k <= n; k++) {
      const z = zSide + (zEnd - zSide) * (k / n) ** 0.7;
      const s = sectionAt(D, Math.max(r, Math.min(f, z)));
      path.push([sideX(D, s, Math.max(s.y0 + 0.01, ym)), 0, z]);
    }
    const xe = path[path.length - 1][0];
    const nx = 6;
    for (let k = 1; k < nx; k++) path.push([xe * (1 - 2 * k / nx), 0, zEnd]);
    for (let k = n; k >= 0; k--) path.push([-path[k][0], 0, path[k][2]]);
    // the end faces sit against the body; the outer face is rounded
    const t = D.bumper === 'chrome' ? 0.075 : 0.05, rr = D.bumper === 'chrome' ? 0.05 : 0.035;
    const sec = [];
    for (let k = 0; k <= 4; k++) { const a = -Math.PI / 2 + (k / 4) * Math.PI / 2; sec.push([t - rr + Math.cos(a) * rr, yb0 + rr + Math.sin(a) * rr]); }
    for (let k = 0; k <= 4; k++) { const a = (k / 4) * Math.PI / 2; sec.push([t - rr + Math.cos(a) * rr, yb1 - rr + Math.sin(a) * rr]); }
    sec.push([-0.04, yb1], [-0.04, yb0]);
    const col = D.bumper === 'chrome' ? [0.9, 0.9, 0.92] : D.bumper === 'dark' ? [0.1, 0.1, 0.1] : [1, 1, 1];
    part.color(...col);
    sweep(part, path, sec, { left: !front });
    // make sure the section's u axis pointed outwards (flip if the front centre moved backwards)
    const mid = path[n + Math.floor(nx / 2)];
    const k0 = part.n - (sec.length + 1) * path.length;
    void mid; void k0;
    // lower valance / intake strip in dark plastic
    if (D.bumper !== 'dark') {
      P.dark.color(0.05, 0.05, 0.05);
      const zz = zEnd + (front ? 0.035 : -0.035);
      P.dark.box(-xe * 0.72, yb0 - 0.01, front ? zz - 0.03 : zz, xe * 0.72, yb0 + 0.05, front ? zz : zz + 0.03);
      P.dark.color(...DARK);
    }
    return { yb0, yb1, xe, zFace: zEnd + (front ? t : -t) };
  };
  const BF = mkBumper(true), BR = mkBumper(false);

  // ---------------- lamps: lenses laid onto the curved nose / tail
  const sF = sectionAt(D, f), sR = sectionAt(D, r);
  const endZ = (front, x, y) => {
    const e = front ? D.nose : D.tail, zE = front ? f : r, s = front ? sF : sR;
    const k = clamp((D.W / 2 - Math.abs(x)) / e.x, 0, 1);
    const dp = e.z * (1 - Math.sqrt(Math.max(0, 1 - (1 - k) ** 2)));
    const yTopFull = D.line(zE);
    const kv = clamp((yTopFull - y) / Math.max(0.005, e.top), 0, 1);
    const dv = y > s.y1 - 0.02 ? e.vz * (1 - Math.sqrt(Math.max(0, 1 - (1 - kv) ** 2))) : 0;
    return zE + (front ? -1 : 1) * Math.max(dp, dv);
  };
  const lens = (part, front, cx, cy, hx, hy, shape = 6, skew = 0, wrap = 0) => {
    const N = 8, rows = [];
    for (let i = 0; i <= N; i++) {
      const row = [];
      for (let j = 0; j <= N; j++) {
        let a = i / N * 2 - 1, b = j / N * 2 - 1;
        const m = Math.max(Math.abs(a), Math.abs(b));
        if (m > 0) { const sa = Math.abs(a) / m, sb = Math.abs(b) / m; const q = (sa ** shape + sb ** shape) ** (1 / shape); a /= q; b /= q; }
        const x = cx + a * hx, y = cy + b * hy + skew * a * hx;
        row.push([x, y, endZ(front, x + wrap * Math.sign(x), y) + (front ? 0.006 : -0.006)]);
      }
      rows.push(row);
    }
    emitGrid(rows, () => part, { ref: [cx * 0.5, cy, front ? 0 : 0] });
  };
  const yF = D.line(f) - D.nose.top, yR = D.line(r) - D.tail.top;
  const hw = W / 2;
  // headlights
  for (const sd of [1, -1]) {
    const L = D.lamps;
    if (L === 'round2' || L === 'quad') {
      for (const k of [0, 1]) lens(P.head, true, sd * (hw - 0.2 - k * 0.22), yF - 0.12, 0.085, 0.085, 2);
    } else if (L === 'slit') lens(P.head, true, sd * (hw - 0.3), yF - 0.05, 0.22, 0.035, 5, 0.12 * sd, 0.05);
    else if (L === 'swept') lens(P.head, true, sd * (hw - 0.27), yF - 0.08, 0.2, 0.055, 5, 0.1 * sd, 0.04);
    else if (L === 'tall') lens(P.head, true, sd * (hw - 0.22), yF - 0.16, 0.14, 0.11, 6);
    else lens(P.head, true, sd * (hw - 0.26), yF - 0.1, 0.2, 0.07, 6, 0, 0.03);
    // tail lights
    const T = D.tails;
    if (T === 'round') { for (const k of [0, 1]) lens(P.tail, false, sd * (hw - 0.18 - k * 0.2), yR - 0.14, 0.07, 0.07, 2); }
    else if (T === 'bar') lens(P.tail, false, sd * (hw * 0.5), yR - 0.12, hw * 0.44, 0.05, 8);
    else if (T === 'slit') lens(P.tail, false, sd * (hw - 0.34), yR - 0.06, 0.26, 0.03, 6, 0.05 * sd, 0.04);
    else if (T === 'tall') lens(P.tail, false, sd * (hw - 0.12), yR - 0.25, 0.07, 0.18, 6);
    else lens(P.tail, false, sd * (hw - 0.3), yR - 0.12, 0.24, 0.065, 6, 0, 0.03);
    // indicators and reverse lamps
    P.trim.hex(0xff8a00);
    const ix = sd * (hw - 0.12), iy = BF.yb1 + 0.04;
    P.trim.box(ix - 0.05, iy - 0.02, endZ(true, ix, iy) - 0.02, ix + 0.05, iy + 0.02, endZ(true, ix, iy) + 0.008);
    P.trim.hex(0xe0e0e0);
    const rx = sd * (hw * 0.32), ry = yR - 0.13;
    if (T !== 'bar') P.trim.box(rx - 0.06, ry - 0.025, endZ(false, rx, ry) - 0.008, rx + 0.06, ry + 0.025, endZ(false, rx, ry) + 0.02);
  }
  // chrome projectors in the headlights
  P.chrome.hex(0xffffff);
  if (D.lamps === 'wide' || D.lamps === 'tall' || D.lamps === 'swept') for (const sd of [1, -1]) for (const k of [0.35, 0.72]) {
    const x = sd * (hw - 0.26 - 0.2 + 0.4 * k), y = yF - (D.lamps === 'tall' ? 0.16 : D.lamps === 'swept' ? 0.08 : 0.1);
    P.chrome.geo(new THREE.CylinderGeometry(0.035, 0.035, 0.01, 12), mat4(x, y, endZ(true, x, y) + 0.01, Math.PI / 2));
  }

  // ---------------- grille
  {
    const zg = f + 0.003, gy1 = Math.min(yF - 0.06, BF.yb1 + 0.3), gy0 = BF.yb1 + 0.01;
    const G = D.grille;
    const frame = (gx, y0, y1, chrome) => {
      (chrome ? P.chrome : P.trim).hex(chrome ? 0xdddddd : 0x151515);
      const pp = chrome ? P.chrome : P.trim;
      pp.box(-gx - 0.025, y0 - 0.025, zg - 0.01, gx + 0.025, y1 + 0.025, zg + 0.02);
      P.dark.color(0.02, 0.02, 0.02).box(-gx, y0, zg, gx, y1, zg + 0.025);
      P.dark.color(...DARK);
    };
    const slats = (gx, y0, y1, n, chrome, vertical = false) => {
      const pp = chrome ? P.chrome : P.trim;
      pp.hex(chrome ? 0xe8e8e8 : 0x303030);
      if (vertical) for (let i = 1; i < n; i++) { const x = -gx + 2 * gx * i / n; pp.box(x - 0.008, y0, zg + 0.02, x + 0.008, y1, zg + 0.035); }
      else for (let i = 1; i < n; i++) { const y = y0 + (y1 - y0) * i / n; pp.box(-gx, y - 0.007, zg + 0.02, gx, y + 0.007, zg + 0.035); }
    };
    if (G === 'intake') { frame(hw * 0.62, c + 0.06, c + 0.2, false); slats(hw * 0.62, c + 0.06, c + 0.2, 3, false); }
    else if (G === 'muscle') { frame(hw - 0.08, gy0, yF - 0.04, false); slats(hw - 0.08, gy0, yF - 0.04, 5, true); }
    else if (G === 'chrome') { frame(hw * 0.55, gy0, gy1, true); slats(hw * 0.55, gy0, gy1, 14, true, true); }
    else if (G === 'truck') { frame(hw * 0.55, gy0, Math.min(yF - 0.04, gy0 + 0.55), true); slats(hw * 0.55, gy0, Math.min(yF - 0.04, gy0 + 0.55), 6, true); }
    else if (G === 'tall') { frame(hw * 0.42, gy0, gy1, false); slats(hw * 0.42, gy0, gy1, 5, false); }
    else if (G === 'slim') { frame(hw * 0.4, yF - 0.14, yF - 0.08, false); frame(hw * 0.55, c + 0.07, BF.yb0 + 0.1, false); }
    else { frame(hw * 0.45, gy0 + 0.02, gy1, false); slats(hw * 0.45, gy0 + 0.02, gy1, 4, false); }
    // badge
    P.chrome.hex(0xdcdcdc).geo(new THREE.CylinderGeometry(0.035, 0.035, 0.012, 14), mat4(0, (gy0 + gy1) / 2 + 0.02, zg + 0.04, Math.PI / 2));
  }

  // ---------------- number plates
  const plate = (front) => {
    const z = front ? BF.zFace + 0.004 : endZ(false, 0, yR - 0.3) - 0.01, y = front ? (BF.yb0 + BF.yb1) / 2 - 0.065 : yR - 0.34, sg = front ? 1 : -1;
    P.trim.hex(0xeeeeee).box(-0.26, y, front ? z : z - 0.012, 0.26, y + 0.13, front ? z + 0.012 : z);
    P.trim.hex(0x1b2a55);
    for (let k = 0; k < 6; k++) { const x = -0.2 + k * 0.08 + (k > 2 ? 0.02 : 0); P.trim.box(x - 0.022, y + 0.03, z + sg * 0.012 - 0.003, x + 0.022, y + 0.1, z + sg * 0.012 + 0.003); }
  };
  plate(true); plate(false);

  // ---------------- panel seams, handles, mirrors, wipers
  const seamSide = (z, y0, y1, part = P.dark) => {
    const s = sectionAt(D, z);
    for (const sd of [1, -1]) {
      const path = [];
      for (let q = 0; q <= 6; q++) { const y = y0 + (y1 - y0) * q / 6; path.push([(sideX(D, s, Math.min(y, s.y1 - s.rt)) + 0.002) * sd, y, z]); }
      // a thin strip along the side, as a flat ribbon (two rows of the grid, 8 mm apart)
      const rows = [path.map(([x, y, zz]) => [x, y, zz - 0.004]), path.map(([x, y, zz]) => [x, y, zz + 0.004])];
      emitGrid(rows, () => part, { ref: [0, (y0 + y1) / 2, z] });
    }
  };
  const sill = c + 0.08;
  const beltAt = (z) => { const s = sectionAt(D, z); return s.y1 - s.rt * 0.5; };
  seamSide(D.door.z0, sill, beltAt(D.door.z0));
  seamSide(D.door.z1, sill, beltAt(D.door.z1));
  if (D.door2) seamSide(D.door2.z1, sill, beltAt(D.door2.z1));
  if (D.extras.slider) { seamSide(D.door.z1 - 1.2, sill, D.line(D.door.z1) + 0.9); }
  // hood / boot shut lines along the top
  const topSeam = (z0, z1, xOf) => {
    for (const sd of [1, -1]) {
      const path = [];
      for (let q = 0; q <= 10; q++) { const z = z0 + (z1 - z0) * q / 10; const s = sectionAt(D, z); const x = xOf(s); path.push([x * sd, topY(D, s, x) + 0.002, z]); }
      const rows = [path.map(([x, y, z]) => [x - 0.004, y, z]), path.map(([x, y, z]) => [x + 0.004, y, z])];
      emitGrid(rows, () => P.dark, { ref: [0, 0, 0] });
    }
  };
  const edgeX = (s) => sideX(D, s, s.y1 - s.rt) - s.rt;
  if (def.body !== 'truck') topSeam(C.bf + 0.03, f - D.nose.vz * 0.5, edgeX);
  if (D.trunk) topSeam(r + D.tail.vz * 0.5, C.br - 0.03, edgeX);
  // door handles
  const handle = (z) => {
    const y = beltAt(z) - 0.14, s = sectionAt(D, z);
    for (const sd of [1, -1]) {
      const x = sideX(D, s, y) * sd;
      const pp = D.bumper === 'chrome' ? P.chrome : P.trim;
      pp.hex(D.bumper === 'chrome' ? 0xdddddd : 0x151515);
      pp.box(Math.min(x, x + sd * 0.025), y - 0.018, z - 0.13, Math.max(x, x + sd * 0.025), y + 0.018, z - 0.01);
    }
  };
  handle(D.door.z1 + 0.02);
  if (D.door2) handle(D.door2.z1 + 0.02);
  // mirrors (body colour) on the doors by the windscreen pillars
  {
    const z = C.bf - 0.2, s = sectionAt(D, z), y = beltAt(z) + 0.1, x = sideX(D, s, s.y1 - s.rt) + 0.1;
    for (const sd of [1, -1]) {
      const part = sd > 0 ? P.door : P.door2;
      part.color(1, 1, 1);
      part.geo(new THREE.SphereGeometry(0.1, 12, 8), mat4(sd * (x + 0.04), y, z - 0.02, 0, 0, 0, 0.75, 0.7, 0.5));
      part.box(sd > 0 ? x - 0.1 : -x, y - 0.03, z - 0.04, sd > 0 ? x : -x + 0.1, y + 0.01, z + 0.02);
      P.chrome.hex(0x9aa4ae).box(sd * (x + 0.04) - 0.065, y - 0.045, z - 0.075, sd * (x + 0.04) + 0.065, y + 0.045, z - 0.065);
    }
  }
  // wipers and the cowl
  {
    const s = sectionAt(D, C.bf), y = topY(D, s, 0) + 0.02;
    P.dark.color(0.03, 0.03, 0.03).box(-s.w + C.belt + 0.05, y - 0.03, C.bf - 0.02, s.w - C.belt - 0.05, y + 0.005, C.bf + 0.1);
    P.dark.color(...DARK);
    for (const x0 of [-0.55, 0.05]) P.dark.geo(new THREE.BoxGeometry(0.5, 0.015, 0.02), mat4(x0 + 0.24, y + 0.02, C.bf - 0.05, 0, -0.12, 0.04));
  }

  // ---------------- extras
  const X = D.extras;
  if (X.scoop) { P.hood.color(1, 1, 1); const s = sectionAt(D, f - 1.0), y = topY(D, s, 0); roundBox(P.hood, -0.28, 0.28, y - 0.02, y + 0.07, f - 1.35, f - 0.75, 0.05, 2); P.dark.color(0.02, 0.02, 0.02).box(-0.24, y + 0.005, f - 0.752, 0.24, y + 0.05, f - 0.745); P.dark.color(...DARK); }
  if (X.stripes) {
    P.trim.hex(0xf2f2f2);
    for (const sx of [-0.2, 0.2]) {
      const path = [];
      for (let q = 0; q <= 24; q++) { const z = r + 0.05 + (f - 0.05 - r - 0.05) * q / 24; if (z < C.bf + 0.01 && z > C.br - 0.01) continue; const s = sectionAt(D, z); path.push([sx, topY(D, s, sx) + 0.003, z]); }
      // bonnet and boot pieces (the roof gets its own)
      const pieces = [path.filter((p) => p[2] > C.bf), path.filter((p) => p[2] < C.br)];
      for (const pc of pieces) if (pc.length > 1) emitGrid([pc.map(([x, y, z]) => [x - 0.07, y, z]), pc.map(([x, y, z]) => [x + 0.07, y, z])], () => P.trim, { ref: [0, 0, 0] });
      const rp = [];
      for (let i = 0; i < D._cabS.length; i++) { const z = D._cabS[i].z; if (z > C.tr && z < C.tf) rp.push([sx, D._cabR[i].yc + C.crown * (1 - (sx / (D._cabR[i].xr)) ** 2) + 0.004, z]); }
      if (rp.length > 1) emitGrid([rp.map(([x, y, z]) => [x - 0.07, y, z]), rp.map(([x, y, z]) => [x + 0.07, y, z])], () => P.trim, { ref: [0, 0, 0] });
    }
  }
  if (X.wing) {
    const z = r + 0.2, y = D.line(z) + 0.28, span = W / 2 - 0.08;
    P.trim.hex(0x151515);
    const path = [];
    for (let q = 0; q <= 8; q++) path.push([-span + 2 * span * q / 8, y, z]);
    const foil = [[-0.18, 0], [-0.1, 0.03], [0.1, 0.02], [0.2, 0], [0.1, -0.01], [-0.1, -0.012]];
    // (the wing runs across the car: sweep along x with the aerofoil in the z-y plane)
    const rows = path.map(([x, yy, zz]) => { const row = foil.map(([u, v]) => [x, yy + v, zz + u]); row.push(row[0].slice()); return row; });
    emitGrid(rows, () => P.trim, { ref: [0, y, z] });
    for (const sd of [1, -1]) P.trim.box(sd * 0.5 - 0.02, D.line(z), z - 0.08, sd * 0.5 + 0.02, y, z + 0.05);
  }
  if (X.lip) { P.trim.hex(0x151515); const z = r + 0.06, y = D.line(z) + 0.01; P.trim.box(-W / 2 + 0.2, y, z - 0.02, W / 2 - 0.2, y + 0.04, z + 0.08); }
  if (X.intakes) {
    for (const sd of [1, -1]) {
      const zc = zr + aR + 0.3, s = sectionAt(D, zc);
      const rows = [];
      for (let i = 0; i <= 4; i++) { const z = zc - 0.25 + 0.5 * i / 4; const ss = sectionAt(D, z); const row = []; for (let j = 0; j <= 3; j++) { const y = c + 0.25 + 0.2 * j / 3 + (z - zc) * 0.3; row.push([(sideX(D, ss, y) + 0.004) * sd, y, z]); } rows.push(row); }
      emitGrid(rows, () => P.dark, { ref: [0, 0.5, zc] });
      void s;
    }
  }
  if (X.louvres) { P.dark.color(0.02, 0.02, 0.02); for (let k = 0; k < 6; k++) { const z = r + 0.35 + k * 0.1; const s = sectionAt(D, z); const y = topY(D, s, 0); P.dark.box(-0.4, y - 0.01, z - 0.02, 0.4, y + 0.012, z + 0.02); } P.dark.color(...DARK); }
  if (X.sideskirt) { P.dark.color(0.03, 0.03, 0.03); for (const sd of [1, -1]) { const s = sectionAt(D, 0); const x = sideX(D, s, c + 0.02) * sd; P.dark.box(Math.min(x, x + sd * 0.05), c - 0.03, zr + aR + 0.05, Math.max(x, x + sd * 0.05), c + 0.06, zf - aR - 0.05); } P.dark.color(...DARK); }
  if (X.rails) {
    P.chrome.hex(0x8a8a8a);
    for (const sd of [1, -1]) {
      const path = [];
      for (let i = 0; i < D._cabS.length; i++) { const z = D._cabS[i].z; if (z < C.tr + 0.05 || z > C.tf - 0.1) continue; path.push([sd * (D._cabR[i].xr - 0.1), D._cabR[i].yc + 0.07, z]); }
      if (path.length > 2) sweep(P.chrome, path, [[-0.02, -0.02], [0.02, -0.02], [0.02, 0.02], [-0.02, 0.02]], {});
      for (const z of [C.tr + 0.1, C.tf - 0.15]) P.chrome.box(sd * (C.top > 0 ? 1 : 1) * (sectionAt(D, z).w - C.inset - 0.1) - 0.02, C.top, z - 0.03, sd * (sectionAt(D, z).w - C.inset - 0.1) + 0.02, C.top + 0.07, z + 0.03);
    }
  }
  if (X.steps) { P.dark.color(0.06, 0.06, 0.06); for (const sd of [1, -1]) { const s = sectionAt(D, 0); const x = sideX(D, s, c) * sd; P.dark.box(Math.min(x - sd * 0.02, x + sd * 0.12), c - 0.06, zr + aR + 0.08, Math.max(x - sd * 0.02, x + sd * 0.12), c - 0.02, zf - aR - 0.08); } P.dark.color(...DARK); }
  if (X.pushbar) { P.trim.hex(0x252525); P.trim.box(-0.62, c + 0.08, BF.zFace, -0.52, c + 0.62, BF.zFace + 0.14); P.trim.box(0.52, c + 0.08, BF.zFace, 0.62, c + 0.62, BF.zFace + 0.14); P.trim.box(-0.62, c + 0.5, BF.zFace + 0.1, 0.62, c + 0.58, BF.zFace + 0.16); P.trim.box(-0.62, c + 0.22, BF.zFace + 0.1, 0.62, c + 0.3, BF.zFace + 0.16); }
  if (X.mudflaps) { P.dark.color(0.03, 0.03, 0.03); for (const sd of [1, -1]) P.dark.box(sd * D.track / 2 - 0.18, c - 0.1, zr - aR - 0.03, sd * D.track / 2 + 0.18, c + 0.3, zr - aR - 0.01); P.dark.color(...DARK); }
  if (X.stacks) { P.chrome.hex(0xcccccc); for (const sd of [1, -1]) P.chrome.geo(new THREE.CylinderGeometry(0.06, 0.06, 1.3, 10), mat4(sd * (W / 2 - 0.05), C.top - 0.35, C.br - 0.1)); }
  if (X.spare) { P.dark.color(0.05, 0.05, 0.05).geo(new THREE.CylinderGeometry(D.R, D.R, 0.24, 18), mat4(0, D.line(r) - 0.1, r - 0.14, Math.PI / 2)); P.dark.color(...DARK); }
  if (X.trimLine) { P.chrome.hex(0xe0e0e0); for (const sd of [1, -1]) { const path = []; for (let q = 0; q <= 20; q++) { const z = r + 0.2 + (f - 0.4 - r) * q / 20; const s = sectionAt(D, z); const y = s.yB + 0.05; path.push([(sideX(D, s, y) + 0.004) * sd, y, z]); } sweep(P.chrome, path, [[0, -0.012], [0.012, 0], [0, 0.012], [-0.004, 0]], {}); } }
  // exhausts
  P.chrome.hex(0xb0b0b0);
  for (const x of X.twinExhaust ? [-W * 0.28, W * 0.28] : [-W * 0.3]) P.chrome.geo(new THREE.CylinderGeometry(0.045, 0.05, 0.22, 10, 1, true), mat4(x, c + 0.06, r - 0.02, Math.PI / 2));

  // ---------------- interior: seats, dash, wheel, console, rear bench
  const seatY = clamp(C.top - 1.02, c + 0.08, Math.max(c + 0.25, D.line(C.bf) - 0.01 - 0.45));
  const seatZ = (C.bf + C.br) / 2 + 0.25;
  {
    const leather = 0x2a2622;
    P.trim.hex(leather);
    for (const sx of [0.38, -0.38]) {
      roundBox(P.trim, sx - 0.27, sx + 0.27, seatY, seatY + 0.16, seatZ - 0.36, seatZ + 0.16, 0.05, 2);
      P.trim.geo(new THREE.BoxGeometry(0.52, 0.64, 0.14), mat4(sx, seatY + 0.48, seatZ - 0.4, -0.18));
      P.trim.geo(new THREE.BoxGeometry(0.28, 0.2, 0.1), mat4(sx, seatY + 0.9, seatZ - 0.48, -0.18));
    }
    if (D.doors === 4 || def.body === 'suv' || def.police || def.taxi) {
      const rz = seatZ - 0.95;
      if (rz > C.br + 0.2) { roundBox(P.trim, -0.66, 0.66, seatY - 0.02, seatY + 0.14, rz - 0.3, rz + 0.18, 0.05, 2); P.trim.geo(new THREE.BoxGeometry(1.3, 0.58, 0.14), mat4(0, seatY + 0.42, rz - 0.36, -0.16)); }
    }
    // dash with an instrument hood and centre console
    const dy = D.line(C.bf) - 0.04, tw = D._tub.xw;
    P.trim.hex(0x1a1a1a);
    roundBox(P.trim, -tw + 0.02, tw - 0.02, dy - 0.26, dy, C.bf - 0.5, C.bf - 0.05, 0.08, 2);
    P.trim.geo(new THREE.CylinderGeometry(0.12, 0.12, 0.2, 12, 1, false, 0, Math.PI), mat4(0.38, dy, C.bf - 0.45, Math.PI / 2, 0, Math.PI / 2));
    P.trim.box(-0.1, c + 0.1, seatZ - 0.3, 0.1, seatY + 0.2, C.bf - 0.45);
    P.trim.hex(0x111111).geo(new THREE.TorusGeometry(0.17, 0.022, 6, 18), mat4(0.38, dy - 0.05, C.bf - 0.58, -0.35, 0, 0));
    P.trim.geo(new THREE.CylinderGeometry(0.02, 0.02, 0.3, 6), mat4(0.38, dy - 0.08, C.bf - 0.46, Math.PI / 2 - 0.35));
  }

  // ---------------- extras on the roof
  const cabMid = (C.tf + C.tr) / 2;
  // one draw call for all the plain trim (dark plastics merge into the trim mesh)
  { const t = P.trim, d = P.dark, base = t.n; t.P.push(...d.P); t.N.push(...d.N); t.C.push(...d.C); for (const i of d.I) t.I.push(i + base); t.n += d.n; P.dark = new Part(); }
  T = { D, P, seatY, seatZ, cabMid, geos: {} };
  for (const k in P) if (!P[k].empty) T.geos[k] = P[k].build();
  // the doors are built in the car's frame; move them to their hinges
  const hingeZ = D.door.z0, hs = sectionAt(D, hingeZ), hingeY = (c + D.line(hingeZ)) / 2, hingeX = sideX(D, hs, hs.yB);
  T.hinge = [hingeX, hingeY, hingeZ];
  for (const k of ['door', 'doorGlass']) if (T.geos[k]) T.geos[k].translate(-hingeX, -hingeY, -hingeZ);
  for (const k of ['glass', 'trim', 'chrome', 'dark', 'head', 'tail', 'doorGlass']) if (T.geos[k]) T.geos[k].userData.shared = true;
  TPL.set(def, T);
  return T;
}

// ------------------------------------------------------------------ wheels (shared per design)
const WHEELS = new Map();
function wheelGeometry(def) {
  const key = `${def.wheelR}|${def.body}`;
  let g = WHEELS.get(key);
  if (g) return g;
  const R = def.wheelR;
  const wgb = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
  const tireW = def.body === 'truck' ? 0.32 : def.body === 'super' ? 0.27 : 0.24;
  {
    const pts = [];
    const hwT = tireW / 2, rIn = R * 0.64;
    pts.push(new THREE.Vector2(rIn, -hwT + 0.01));
    for (let i = 0; i <= 6; i++) { const a = -Math.PI / 2 + (i / 6) * Math.PI / 2; pts.push(new THREE.Vector2(R - 0.05 + Math.cos(a) * 0.05, -hwT + 0.05 + Math.sin(a) * 0.05)); }
    for (let i = 0; i <= 6; i++) { const a = (i / 6) * Math.PI / 2; pts.push(new THREE.Vector2(R - 0.05 + Math.cos(a) * 0.05, hwT - 0.05 + Math.sin(a) * 0.05)); }
    pts.push(new THREE.Vector2(rIn, hwT - 0.01));
    const tyre = new THREE.LatheGeometry(pts, 28);
    wgb.set('color', 0.055, 0.055, 0.058);
    wgb.addGeometry(tyre, mat4(0, 0, 0, 0, 0, Math.PI / 2));
    wgb.set('color', 0.02, 0.02, 0.02);
    for (const gg of [-0.3, 0, 0.3]) wgb.addGeometry(new THREE.TorusGeometry(R + 0.001, 0.008, 3, 28), mat4(gg * tireW, 0, 0, 0, Math.PI / 2, 0));
    // sidewall lettering band / whitewalls
    if (def.body === 'lowrider') { wgb.set('color', 0.92, 0.92, 0.9); wgb.addGeometry(new THREE.RingGeometry(R * 0.7, R * 0.86, 28), mat4(tireW / 2 + 0.002, 0, 0, 0, Math.PI / 2, 0)); }
  }
  const style = def.body === 'lowrider' ? 'wire' : def.body === 'super' ? 'mesh' : def.body === 'coupe' ? 'split' : ['truck', 'van'].includes(def.body) ? 'steel' : def.military ? 'steel' : 'alloy';
  const rimCol = style === 'wire' ? [0.9, 0.9, 0.9] : style === 'mesh' || style === 'split' ? [0.22, 0.22, 0.24] : style === 'steel' ? [0.35, 0.36, 0.38] : [0.62, 0.62, 0.64];
  // brake disc & caliper (behind the spokes)
  wgb.set('color', 0.32, 0.32, 0.33);
  wgb.addGeometry(new THREE.CylinderGeometry(R * 0.55, R * 0.55, 0.02, 18), mat4(tireW * 0.1, 0, 0, 0, 0, Math.PI / 2));
  if (style === 'mesh' || style === 'split') { wgb.set('color', 0.8, 0.1, 0.08); wgb.addGeometry(new THREE.BoxGeometry(0.06, 0.12, 0.08), mat4(tireW * 0.18, R * 0.38, R * 0.12)); }
  wgb.set('color', ...rimCol);
  wgb.addGeometry(new THREE.TorusGeometry(R * 0.645, 0.018, 6, 24), mat4(tireW / 2 - 0.005, 0, 0, 0, Math.PI / 2, 0));
  wgb.addGeometry(new THREE.CylinderGeometry(R * 0.64, R * 0.64, 0.02, 20, 1, true), mat4(tireW / 2 - 0.02, 0, 0, 0, 0, Math.PI / 2));
  wgb.set('color', rimCol[0] * 0.35, rimCol[1] * 0.35, rimCol[2] * 0.35);
  wgb.addGeometry(new THREE.CylinderGeometry(R * 0.62, R * 0.62, 0.01, 20), mat4(tireW / 2 - (style === 'steel' ? 0.02 : 0.05), 0, 0, 0, 0, Math.PI / 2));
  wgb.set('color', ...rimCol);
  const spoke = (a, w, len, depth, r0) => wgb.addGeometry(new THREE.BoxGeometry(depth, len, w), new THREE.Matrix4().makeRotationX(a).multiply(mat4(tireW / 2 - 0.02, r0 + len / 2, 0, 0, 0, 0.2)));
  if (style === 'wire') { for (let k = 0; k < 36; k++) { const a = k / 36 * Math.PI * 2; wgb.addGeometry(new THREE.BoxGeometry(0.006, R * 0.5, 0.006), new THREE.Matrix4().makeRotationX(a).multiply(mat4(tireW / 2 - 0.025 + (k % 2) * 0.02, R * 0.36, 0, 0.35 * (k % 2 ? 1 : -1), 0, 0))); } }
  else if (style === 'mesh') { for (let k = 0; k < 10; k++) { spoke(k / 10 * Math.PI * 2, 0.028, R * 0.5, 0.03, R * 0.1); } }
  else if (style === 'split') { for (let k = 0; k < 5; k++) { const a = k / 5 * Math.PI * 2; spoke(a - 0.1, 0.025, R * 0.5, 0.04, R * 0.1); spoke(a + 0.1, 0.025, R * 0.5, 0.04, R * 0.1); } }
  else if (style === 'steel') { wgb.addGeometry(new THREE.CylinderGeometry(R * 0.5, R * 0.56, 0.04, 20), mat4(tireW / 2 - 0.03, 0, 0, 0, 0, Math.PI / 2)); wgb.set('color', 0.12, 0.12, 0.13); for (let k = 0; k < 8; k++) { const a = k / 8 * Math.PI * 2; wgb.addGeometry(new THREE.CylinderGeometry(0.035, 0.035, 0.05, 8), mat4(tireW / 2 - 0.01, Math.cos(a) * R * 0.38, Math.sin(a) * R * 0.38, 0, 0, Math.PI / 2)); } wgb.set('color', ...rimCol); }
  else { const n = def.body === 'suv' || def.body === 'pickup' ? 6 : 5; for (let k = 0; k < n; k++) spoke(k / n * Math.PI * 2, 0.06, R * 0.5, 0.035, R * 0.1); }
  wgb.addGeometry(new THREE.CylinderGeometry(R * 0.17, R * 0.2, 0.05, 14), mat4(tireW / 2 - 0.01, 0, 0, 0, 0, Math.PI / 2));
  wgb.set('color', rimCol[0] * 0.55, rimCol[1] * 0.55, rimCol[2] * 0.55);
  for (let k = 0; k < 5; k++) { const a = k / 5 * Math.PI * 2 + 0.3; wgb.addGeometry(new THREE.CylinderGeometry(0.011, 0.011, 0.02, 6), mat4(tireW / 2 + 0.016, Math.cos(a) * R * 0.1, Math.sin(a) * R * 0.1, 0, 0, Math.PI / 2)); }
  g = wgb.build();
  g.userData.shared = true;
  WHEELS.set(key, g);
  return g;
}

export function buildVehicleModel(def, color) {
  const M = vehicleMaterials();
  const T = carTemplate(def);
  const { D, geos } = T;
  const C = D.cab;
  const L = def.L, W = def.W, c = def.clearance, R = def.wheelR, wb = def.wheelbase;
  const group = new THREE.Group();
  const bodyGroup = new THREE.Group(); // tilts with suspension
  group.add(bodyGroup);
  const bodyMat = bodyMaterial(def.police ? (def.livery ?? 0xffffff) : color);
  const mesh = (g, mat, shadow = true, cast = shadow) => { const m = new THREE.Mesh(g, mat); m.castShadow = cast; m.receiveShadow = shadow; return m; };
  // painted panels get their own geometry (they dent); everything else is shared per model
  const own = (k) => (geos[k] ? geos[k].clone() : null);
  const body = mesh(own('body'), bodyMat);
  bodyGroup.add(body);
  const panels = { body };
  const hinges = {};
  for (const k of ['hood', 'trunk', 'door2', 'bumperF', 'bumperR']) {
    const g = own(k);
    if (!g) continue;
    const mat = (k === 'bumperF' || k === 'bumperR') && D.bumper !== 'body' ? (D.bumper === 'chrome' ? M.chrome : M.trim) : bodyMat;
    const m = mesh(g, mat, true, false);
    if (k === 'hood' || k === 'trunk') {
      // bonnet and boot lid hang on hinges at the windscreen / rear window (they spring open in a crash)
      const hz = k === 'hood' ? C.bf + 0.03 : C.br - 0.03, hy = D.line(hz);
      const pv = new THREE.Group();
      pv.position.set(0, hy, hz);
      m.position.set(0, -hy, -hz);
      pv.add(m);
      bodyGroup.add(pv);
      hinges[k] = pv;
    } else bodyGroup.add(m);
    panels[k] = m;
  }
  const glass = mesh(geos.glass, M.glass, false);
  glass.renderOrder = 3;
  bodyGroup.add(glass);
  const trim = mesh(geos.trim, M.trim, true, false);
  bodyGroup.add(trim);
  const chrome = geos.chrome ? mesh(geos.chrome, M.chrome, true, false) : null;
  if (chrome) bodyGroup.add(chrome);
  const head = mesh(geos.head, M.headOff, false);
  const tail = mesh(geos.tail, M.tailOff, false);
  bodyGroup.add(head, tail);

  // ---------------- police light bar / taxi sign
  let lightbar = null;
  const roofY = C.top + C.crown;
  if (def.police) {
    const z = T.cabMid + 0.1;
    const base = mesh(new THREE.BoxGeometry(1.36, 0.06, 0.34), M.trim);
    base.position.set(0, roofY + 0.02, z);
    bodyGroup.add(base);
    const mk = (col, x0, x1) => {
      const g = new THREE.CylinderGeometry(0.1, 0.1, x1 - x0, 12);
      g.rotateZ(Math.PI / 2);
      g.scale(1, 0.75, 1.3);
      const m = new THREE.Mesh(g, patch(new THREE.MeshStandardMaterial({ color: col, emissive: new THREE.Color(col).multiplyScalar(0.2), roughness: 0.2, transparent: true, opacity: 0.92 }), { key: 'vlight' }));
      m.position.set((x0 + x1) / 2, roofY + 0.1, z);
      bodyGroup.add(m);
      return m;
    };
    lightbar = { red: mk(0xff1a1a, 0.03, 0.64), blue: mk(0x1a4dff, -0.64, -0.03) };
    // POLICE lettering stand-in: a dark band on the doors' white
  }
  if (def.taxi) {
    const sign = new THREE.Mesh(new THREE.BoxGeometry(0.7, 0.2, 0.24), patch(new THREE.MeshStandardMaterial({ color: 0xffe066, emissive: new THREE.Color(1.2, 1.0, 0.3), roughness: 0.3 }), { key: 'vlight' }));
    sign.position.set(0, roofY + 0.11, T.cabMid);
    bodyGroup.add(sign);
    // chequer band along the sides
    const cg = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
    const s0 = D._cabS;
    for (const sd of [1, -1]) {
      for (let k = 0; k < 22; k++) {
        const z = D.door.z0 - 0.02 - k * 0.1, y0 = c + 0.36;
        if (z < C.br - 0.2) break;
        for (const row of [0, 1]) {
          if ((k + row) % 2) continue;
          const s = sectionAt(D, z), y = y0 + row * 0.08, x = (sideX(D, s, y + 0.04) + 0.004) * sd;
          cg.set('color', 0.05, 0.05, 0.05);
          cg.box(Math.min(x, x + sd * 0.004), y, z - 0.1, Math.max(x, x + sd * 0.004), y + 0.08, z, { top: false });
        }
      }
    }
    void s0;
    const st = new THREE.Mesh(cg.build(), M.trim);
    bodyGroup.add(st);
  }

  // ---------------- armoured vans: a livery band, a roof beacon, push bar and twin rear doors that can be blown open
  let rearDoors = null;
  if (def.armored) {
    const band = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
    const bandCol = def.police ? [0.85, 0.85, 0.8] : [0.08, 0.22, 0.5];
    const y0 = c + 0.42, y1 = c + 0.62;
    for (const sd of [1, -1]) {
      for (let z = D.door.z0 - 0.05; z > D.r + 0.25; z -= 0.25) {
        const s = sectionAt(D, z), x = (sideX(D, s, (y0 + y1) / 2) + 0.006) * sd;
        band.set('color', ...bandCol);
        band.box(Math.min(x, x + sd * 0.006), y0, z - 0.25, Math.max(x, x + sd * 0.006), y1, z, { top: false });
      }
    }
    // front push bar
    band.set('color', 0.06, 0.06, 0.06);
    band.box(-W * 0.36, c + 0.05, L / 2 - 0.02, W * 0.36, c + 0.12, L / 2 + 0.12);
    for (const x of [-0.42, 0.42]) band.box(x - 0.04, c + 0.05, L / 2 + 0.02, x + 0.04, c + 0.62, L / 2 + 0.12);
    bodyGroup.add(mesh(band.build(), M.trim, true, false));
    // roof beacon (amber for the cash van; the Enforcer has the police light bar)
    if (!def.police) {
      const bm = new THREE.Mesh(new THREE.CylinderGeometry(0.11, 0.11, 0.14, 12), patch(new THREE.MeshStandardMaterial({ color: 0xffa31a, emissive: new THREE.Color(1.6, 0.7, 0.05), roughness: 0.3 }), { key: 'vlight' }));
      bm.position.set(0, roofY + 0.07, C.tf - 0.25);
      bodyGroup.add(bm);
    }
    // rear doors: hinged at the outer edges
    const zb = D.r - 0.004, dh = def.H - 0.42 - c;
    const dw = W / 2 - 0.14;
    // the hold behind the doors: dark, with cash bags on the shelves (seen when they're open)
    const hold = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
    hold.set('color', 0.03, 0.03, 0.035);
    hold.box(-W / 2 + 0.12, c + 0.3, zb - 0.0035, W / 2 - 0.12, c + 0.3 + dh, zb - 0.002, { top: false });
    if (!def.police) {
      hold.set('color', 0.2, 0.45, 0.16);
      for (const [x, y] of [[-0.45, 0.25], [0.05, 0.22], [0.5, 0.27], [-0.25, 0.95], [0.35, 0.92]]) hold.box(x - 0.17, c + 0.3 + y - 0.12, zb - 0.0075, x + 0.17, c + 0.3 + y + 0.12, zb - 0.004);
      hold.set('color', 0.12, 0.12, 0.13);
      hold.box(-W / 2 + 0.12, c + 0.3 + 0.66, zb - 0.0045, W / 2 - 0.12, c + 0.3 + 0.7, zb - 0.0036);
    }
    bodyGroup.add(mesh(hold.build(), M.trim, false));
    rearDoors = [];
    for (const sd of [1, -1]) {
      const pv = new THREE.Group();
      pv.position.set(sd * (W / 2 - 0.1), c + 0.3, zb);
      const g = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
      g.set('color', 1, 1, 1);
      g.box(sd > 0 ? -dw : 0, 0, -0.03, sd > 0 ? 0 : dw, dh, 0.0, { bottom: true });
      const door = mesh(g.build(), bodyMat, true, false);
      pv.add(door);
      const hg = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
      hg.set('color', 0.05, 0.05, 0.05);
      const hx = sd > 0 ? -dw + 0.08 : dw - 0.08;
      hg.box(hx - 0.03, dh * 0.45, -0.06, hx + 0.03, dh * 0.6, -0.03); // handle
      hg.box(sd > 0 ? -dw : dw - 0.012, 0, -0.035, sd > 0 ? -dw + 0.012 : dw, dh, -0.03); // seam
      pv.add(mesh(hg.build(), M.trim, false));
      bodyGroup.add(pv);
      rearDoors.push({ pivot: pv, side: sd, open: 0 });
    }
  }

  // ---------------- driver door (left side, +x), hinged at the front
  const doorPivot = new THREE.Group();
  doorPivot.position.set(...T.hinge);
  const doorMesh = mesh(own('door'), bodyMat);
  doorPivot.add(doorMesh);
  let doorGlass = null;
  if (geos.doorGlass) { doorGlass = mesh(geos.doorGlass, M.glass, false); doorGlass.renderOrder = 3; doorPivot.add(doorGlass); }
  bodyGroup.add(doorPivot);
  panels.door = doorMesh;

  // ---------------- wheels
  const wheels = [];
  const wheelGeo = wheelGeometry(def);
  const zf = wb / 2, zr = -wb / 2;
  for (const [x, z, front] of [[def.track / 2, zf, true], [-def.track / 2, zf, true], [def.track / 2, zr, false], [-def.track / 2, zr, false]]) {
    const pivot = new THREE.Group();
    pivot.position.set(x, R, z);
    const m = new THREE.Mesh(wheelGeo, M.wheel);
    m.castShadow = true;
    if (x < 0) m.rotation.y = Math.PI;
    const spin = new THREE.Group();
    spin.add(m);
    pivot.add(spin);
    group.add(pivot);
    wheels.push({ pivot, spin, x, z, front, baseY: R, comp: 0 });
  }

  // headlight glow on the road ahead (visible at night)
  const beamMesh = new THREE.Mesh(beamGeometry(), beamMaterial());
  beamMesh.position.set(0, 0.03, L / 2 + 5.5);
  beamMesh.renderOrder = 2;
  beamMesh.visible = false;
  group.add(beamMesh);

  const seatBase = new THREE.Vector3(0.38, T.seatY + 0.05 - 0.02, T.seatZ - 0.1);
  // rear row: under the roof (short-roofed bodies would otherwise have heads through the back window)
  const rearZ = clamp(seatBase.z - 0.9, C.tr + 0.14, seatBase.z - 0.55);
  const dz0 = D.door.z0, dz1 = D.door.z1;
  return {
    group, bodyGroup, body, bodyMat, glass, head, tail, lightbar, wheels, panels, rearDoors,
    door: { pivot: doorPivot, mesh: doorMesh, open: 0, max: -1.1 },
    seatHip: 0.09, // character hips sit this far above a seat point (sunk a little into the cushion)
    seats: [seatBase.clone(), seatBase.clone().setX(-0.38), seatBase.clone().setZ(rearZ).setY(seatBase.y - 0.04), seatBase.clone().set(-0.38, seatBase.y - 0.04, rearZ)],
    doorPos: new THREE.Vector3(W / 2 + 0.55, 0, (dz0 + dz1) / 2 - 0.15),
    trim, beam: beamMesh,
    // small parts hidden when the car is far from the camera
    detail: [trim, chrome].filter(Boolean),
    hinges,
    // crash-physics hull: roof extent and centre-of-gravity height
    hull: {
      roofX: sectionAt(D, (C.tf + C.tr) / 2).w - C.inset, roofY: D.cargo ? Math.max(C.top, D.cargo.y1) : C.top,
      roofZ0: D.cargo ? D.cargo.z0 : C.tr, roofZ1: C.tf, beltY: D.line(0), cgH: Math.max(0.45, (c + C.top) * 0.42),
    },
  };
}
