// Boats: lofted planing hulls (a deep V that narrows to the stem, a flat transom), with cockpits, consoles,
// windscreens, cabins, outboards and a police light bar. They float on the sea (and Lake Mirador), ride
// the swell, lift onto the plane at speed, heel into turns, throw spray and leave a wake. Run them aground
// and they stop; hole them and they burn, blow up and go down.
import * as THREE from 'three';
import { Vehicle } from './vehicle.js';
import { vehicleMaterials, bodyMaterial } from './vehiclemodels.js';
import { Part, emitGrid, sweep, roundBox, stations } from './loft.js';
import { mat4 } from '../world/geom.js';
import { WATER_Y } from '../world/citymap.js';
import { clamp, damp, wrapAngle, rand } from '../core/utils.js';
import { patch } from '../render/materials.js';

const _v = new THREE.Vector3();

// ------------------------------------------------------------------ hull designs
function hullDesign(def) {
  const L = def.L, B = def.W;
  const base = { L, B, draft: def.draft, free: def.freeboard, dead: 0.38, sheerRise: 0.28, bowStart: 0.5, cockpit: [0.03, 0.62], cockDepth: 0.42, fore: 'deck', tubes: false };
  switch (def.boat) {
    case 'rib': return { ...base, dead: 0.3, sheerRise: 0.12, bowStart: 0.55, cockpit: [0.02, 0.8], cockDepth: 0.3, tubes: true };
    case 'jetski': return { ...base, dead: 0.3, sheerRise: 0.1, bowStart: 0.45, cockpit: [0, 0], cockDepth: 0 };
    case 'cruiser': return { ...base, dead: 0.32, sheerRise: 0.45, bowStart: 0.5, cockpit: [0.02, 0.3], cockDepth: 0.35 };
    case 'police': return { ...base, dead: 0.36, sheerRise: 0.3, bowStart: 0.52, cockpit: [0.03, 0.42], cockDepth: 0.4 };
    default: return base; // speedboat
  }
}

function hullShape(H) {
  const half = (t) => {
    // plan: full beam aft, an elliptic bow to a sharp stem
    const u = (t - H.bowStart) / (1 - H.bowStart);
    return H.B / 2 * (t < H.bowStart ? 1 - 0.04 * (1 - t / H.bowStart) : Math.sqrt(Math.max(0, 1 - u * u)) * (1 - 0.03 * u)) + 0.001;
  };
  const keel = (t) => { const u = clamp((t - 0.55) / 0.45, 0, 1); return -H.draft + u * u * (H.draft + H.free * 0.55); };
  const sheer = (t) => H.free + H.sheerRise * t * t;
  return { half, keel, sheer };
}

export function buildBoatModel(def, color) {
  const M = vehicleMaterials();
  const H = hullDesign(def);
  const S = hullShape(H);
  const L = H.L, z0 = -L / 2;
  const group = new THREE.Group(), bodyGroup = new THREE.Group();
  group.add(bodyGroup);
  const P = { hull: new Part(), deck: new Part(), trim: new Part(), glass: new Part(), chrome: new Part(), head: new Part(), tail: new Part() };
  const police = !!def.police;
  const bottom = police ? [0.12, 0.14, 0.2] : def.boat === 'rib' ? [0.25, 0.25, 0.27] : [0.55, 0.12, 0.1];
  // hull shell: keel -> chine -> topsides -> sheer, right half then mirrored
  const zs = stations(0, 1, 0.025, [H.bowStart, H.cockpit[0], H.cockpit[1]]);
  const ring = (t) => {
    const b = S.half(t), k = S.keel(t), sh = S.sheer(t);
    const chineX = b * 0.86, chineY = k + chineX * Math.tan(H.dead) * (1 - 0.5 * clamp((t - 0.6) / 0.4, 0, 1)) ;
    const pts = [];
    for (let i = 0; i <= 4; i++) { const f = i / 4; pts.push([chineX * f, k + (chineY - k) * (f * 0.85 + 0.15 * f * f)]); }
    pts.push([b * 0.9, Math.min(sh - 0.02, chineY + 0.04)]);
    for (let i = 1; i <= 4; i++) { const f = i / 4; const y = Math.min(sh, chineY + 0.04 + (sh - chineY - 0.04) * f); pts.push([b * (0.9 + 0.1 * Math.sin(f * Math.PI / 2)), y]); }
    return pts;
  };
  const hullCol = (x, y) => (y < 0.06 ? bottom : y < 0.13 ? [0.95, 0.95, 0.95] : police && y > H.free * 0.55 && y < H.free * 0.8 ? [0.1, 0.25, 0.8] : [1, 1, 1]);
  for (const side of [1, -1]) {
    const rows = zs.map((t) => ring(t).map(([x, y]) => [x * side, y, z0 + t * L]));
    emitGrid(rows, () => P.hull, { ref: [0, -0.2, 0], colorAt: (x, y) => hullCol(x, y) });
  }
  // transom
  {
    const r0 = ring(0);
    const pts = r0.map(([x, y]) => [x, y, z0]).concat(r0.slice().reverse().map(([x, y]) => [-x, y, z0]));
    P.hull.color(1, 1, 1);
    P.hull.poly(pts, [0, 0, -1]);
  }
  // decks: flush foredeck, a sunk cockpit with its floor, side walls and gunwale caps
  const deckY = (t) => S.sheer(t) - 0.015;
  const cockY = (t) => S.sheer(t) - H.cockDepth;
  const [c0, c1] = H.cockpit;
  const inCock = (t) => t > c0 && t < c1 && H.cockDepth > 0;
  {
    const deckCol = def.boat === 'cruiser' || police ? [0.85, 0.84, 0.8] : def.boat === 'rib' ? [0.2, 0.2, 0.22] : [0.72, 0.52, 0.32];
    P.deck.color(...deckCol);
    // foredeck and aft deck (outside the cockpit)
    for (const [ta, tb] of [[c1, 0.995], [0, c0]]) {
      if (tb - ta < 0.01) continue;
      const ts = stations(ta, tb, 0.03);
      const rows = ts.map((t) => { const b = S.half(t); const row = []; for (let i = -6; i <= 6; i++) { const x = b * i / 6 * 0.99; row.push([x, deckY(t) + 0.035 * (1 - (x / Math.max(b, 0.01)) ** 2), z0 + t * L]); } return row; });
      emitGrid(rows, () => P.deck, { ref: [0, -2, 0] });
    }
    if (c1 > c0 && H.cockDepth > 0) {
      const ts = stations(c0, c1, 0.03);
      const w = (t) => S.half(t) - 0.11;
      const floor = ts.map((t) => [[-w(t), cockY(t), z0 + t * L], [w(t), cockY(t), z0 + t * L]]);
      emitGrid(floor, () => P.deck, { ref: [0, -2, 0] });
      P.trim.color(0.92, 0.92, 0.9);
      for (const sd of [1, -1]) {
        const wall = ts.map((t) => [[sd * w(t), cockY(t), z0 + t * L], [sd * w(t), deckY(t), z0 + t * L]]);
        emitGrid(wall, () => P.trim, { ref: [0, 0.3, z0 + (c0 + c1) / 2 * L], inward: true });
        const cap = ts.map((t) => [[sd * w(t), deckY(t), z0 + t * L], [sd * (S.half(t) + 0.005), deckY(t) + 0.01, z0 + t * L]]);
        emitGrid(cap, () => P.deck, { ref: [0, -2, 0] });
      }
      // cockpit ends
      for (const t of [c0, c1]) { const b = w(t), zz = z0 + t * L; P.trim.poly([[-b, cockY(t), zz], [b, cockY(t), zz], [b, deckY(t), zz], [-b, deckY(t), zz]], [0, 0, t === c0 ? 1 : -1]); }
    }
  }
  // rubbing strake along the sheer
  {
    const path = [];
    for (const t of stations(0.01, 0.985, 0.04)) path.push([S.half(t) + 0.01, S.sheer(t) - 0.06, z0 + t * L]);
    for (const sd of [1, -1]) { P.trim.color(police ? 0.1 : 0.15, police ? 0.25 : 0.15, police ? 0.7 : 0.17); sweep(P.trim, path.map(([x, y, z]) => [x * sd, y, z]), [[-0.02, -0.03], [0.03, -0.03], [0.03, 0.03], [-0.02, 0.03]], {}); }
  }
  // inflatable tubes on the RIB
  if (H.tubes) {
    const path = [];
    for (const t of stations(0.0, 0.97, 0.03)) path.push([S.half(t) + 0.05, S.sheer(t) - 0.05, z0 + t * L]);
    const tube = [];
    for (let k = 0; k < 12; k++) { const a = k / 12 * Math.PI * 2; tube.push([Math.cos(a) * 0.22, Math.sin(a) * 0.22]); }
    P.trim.color(0.12, 0.12, 0.13);
    const full = path.slice().reverse().map(([x, y, z]) => [-x, y, z]).concat(path.slice(1));
    sweep(P.trim, full, tube, {});
  }
  const hullMat = bodyMaterial(color);
  const seats = [];
  let seatHip = 0.4;
  const grips = null;
  let lightbar = null, gun = null;
  const zAt = (t) => z0 + t * L;
  // ---------------- per-type fit-out
  const console = (t, w, h) => {
    const y = cockY(t), z = zAt(t);
    P.trim.color(0.92, 0.92, 0.9);
    roundBox(P.trim, -w, w, y, y + h, z - 0.35, z + 0.25, 0.08, 2);
    P.trim.color(0.1, 0.1, 0.1); P.trim.box(-w * 0.7, y + h - 0.02, z - 0.2, w * 0.7, y + h + 0.01, z + 0.15);
    P.chrome.color(0.8, 0.8, 0.82); P.chrome.geo(new THREE.TorusGeometry(0.15, 0.02, 6, 16), mat4(0.25, y + h + 0.08, z - 0.25, -0.9, 0, 0));
  };
  const windscreen = (t, w, h, rake = 0.5) => {
    const y = deckY(t), z = zAt(t);
    const pts = [];
    for (let k = 0; k <= 8; k++) { const a = -1 + 2 * k / 8; pts.push([a * w, y, z + 0.18 * (1 - a * a)]); }
    const rows = [pts, pts.map(([x, yy, zz]) => [x * 0.94, yy + h, zz - h * rake])];
    emitGrid([rows[0], rows[1]].map((r) => r).map((r) => r), () => P.glass, { ref: [0, y, z - 2] });
    P.chrome.color(0.75, 0.75, 0.78);
    sweep(P.chrome, rows[1], [[-0.015, -0.015], [0.015, -0.015], [0.015, 0.015], [-0.015, 0.015]], {});
  };
  const seat = (x, t, w = 0.26) => {
    const y = cockY(t), z = zAt(t);
    P.trim.color(0.88, 0.86, 0.8);
    roundBox(P.trim, x - w, x + w, y, y + 0.42, z - 0.25, z + 0.25, 0.06, 2);
    roundBox(P.trim, x - w, x + w, y + 0.42, y + 0.95, z - 0.3, z - 0.18, 0.05, 2);
    seats.push(new THREE.Vector3(x, y + 0.42, z));
  };
  const outboard = (x, n = 1) => {
    for (let k = 0; k < n; k++) {
      const xx = x + (k - (n - 1) / 2) * 0.55, y = S.sheer(0) - 0.15, z = z0 - 0.25;
      P.trim.color(0.1, 0.1, 0.11); roundBox(P.trim, xx - 0.2, xx + 0.2, y, y + 0.55, z - 0.35, z + 0.2, 0.08, 2);
      P.trim.color(0.2, 0.2, 0.22); P.trim.box(xx - 0.06, -H.draft - 0.35, z - 0.12, xx + 0.06, y, z + 0.08);
      P.chrome.color(0.6, 0.6, 0.6); P.chrome.box(xx - 0.02, -H.draft - 0.4, z - 0.02, xx + 0.02, -H.draft - 0.25, z + 0.15);
    }
  };
  const rail = (t0, t1, hgt = 0.6) => {
    for (const sd of [1, -1]) {
      const path = [];
      for (const t of stations(t0, t1, 0.05)) path.push([sd * (S.half(t) - 0.08), deckY(t) + hgt, zAt(t)]);
      P.chrome.color(0.82, 0.82, 0.85);
      sweep(P.chrome, path, [[-0.015, -0.015], [0.015, -0.015], [0.015, 0.015], [-0.015, 0.015]], {});
      for (let k = 0; k < path.length; k += 3) { const [x, y, z] = path[k]; P.chrome.box(x - 0.012, y - hgt, z - 0.012, x + 0.012, y, z + 0.012); }
    }
  };
  const cabin = (ta, tb, h, glassCol = true) => {
    // a boxy cabin with a rounded top and wrap-round windows
    const ts = stations(ta, tb, 0.04);
    const w = (t) => S.half(t) - 0.2;
    const yb = (t) => deckY(t);
    const rows = ts.map((t) => {
      const W = w(t), y = yb(t), z = zAt(t);
      return [[W, y, z], [W * 0.98, y + h * 0.45, z], [W * 0.92, y + h * 0.9, z], [W * 0.8, y + h, z], [0, y + h + 0.05, z]];
    });
    for (const sd of [1, -1]) {
      const rr = rows.map((r) => r.map(([x, y, z]) => [x * sd, y, z]));
      emitGrid(rr, (i, j) => (j === 1 ? P.glass : P.hull), { ref: [0, deckY((ta + tb) / 2) + h * 0.4, zAt((ta + tb) / 2)], colorAt: () => [1, 1, 1] });
    }
    for (const t of [ta, tb]) {
      const r = rows[t === ta ? 0 : rows.length - 1];
      const ring2 = r.concat(r.slice(0, -1).reverse().map(([x, y, z]) => [-x, y, z]));
      P.hull.color(1, 1, 1);
      P.hull.poly(ring2, [0, 0, t === ta ? -1 : 1]);
      // windscreen / rear windows on the end faces
      const W = w(t) * 0.8, y = yb(t) + h * 0.5, z = zAt(t) + (t === ta ? -0.01 : 0.01);
      if (glassCol) P.glass.poly([[-W, y, z], [W, y, z], [W * 0.9, y + h * 0.35, z], [-W * 0.9, y + h * 0.35, z]], [0, 0, t === ta ? -1 : 1]);
    }
    return { top: yb((ta + tb) / 2) + h + 0.05 };
  };
  switch (def.boat) {
    case 'rib': {
      console(0.52, 0.28, 0.85);
      windscreen(0.6, 0.35, 0.3, 0.3);
      seats.push(new THREE.Vector3(0, cockY(0.4) + 0.62, zAt(0.4)));
      P.trim.color(0.15, 0.15, 0.16); roundBox(P.trim, -0.3, 0.3, cockY(0.4), cockY(0.4) + 0.6, zAt(0.4) - 0.3, zAt(0.4) + 0.3, 0.1, 2);
      seats.push(new THREE.Vector3(0, cockY(0.2) + 0.45, zAt(0.2)));
      P.trim.box(-0.45, cockY(0.2), zAt(0.2) - 0.2, 0.45, cockY(0.2) + 0.43, zAt(0.2) + 0.2);
      outboard(0, 1);
      seatHip = 0.08;
      break;
    }
    case 'jetski': {
      const y = deckY(0.4);
      P.hull.color(1, 1, 1); roundBox(P.hull, -0.3, 0.3, y - 0.05, y + 0.25, zAt(0.15), zAt(0.75), 0.12, 3);
      P.trim.color(0.1, 0.1, 0.1); roundBox(P.trim, -0.2, 0.2, y + 0.24, y + 0.36, zAt(0.18), zAt(0.6), 0.08, 3);
      P.hull.color(1, 1, 1); roundBox(P.hull, -0.25, 0.25, y + 0.1, y + 0.55, zAt(0.7), zAt(0.82), 0.1, 2);
      P.chrome.color(0.7, 0.7, 0.72); P.chrome.geo(new THREE.CylinderGeometry(0.018, 0.018, 0.7, 8), mat4(0, y + 0.62, zAt(0.74), 0, 0, Math.PI / 2));
      seats.push(new THREE.Vector3(0, y + 0.36, zAt(0.42)));
      seatHip = 0.04;
      break;
    }
    case 'cruiser': {
      const cb = cabin(0.3, 0.7, 1.45);
      // flybridge with a second helm, a radar arch and rails
      P.hull.color(1, 1, 1); roundBox(P.hull, -1.1, 1.1, cb.top, cb.top + 0.5, zAt(0.36), zAt(0.6), 0.1, 2);
      P.glass.poly([[-1.05, cb.top + 0.5, zAt(0.6)], [1.05, cb.top + 0.5, zAt(0.6)], [0.95, cb.top + 0.85, zAt(0.57)], [-0.95, cb.top + 0.85, zAt(0.57)]], [0, 0.3, 1]);
      P.chrome.color(0.85, 0.85, 0.88);
      for (const sd of [1, -1]) P.chrome.geo(new THREE.CylinderGeometry(0.04, 0.04, 1.3, 8), mat4(sd * 1.0, cb.top + 1.1, zAt(0.4), 0.15, 0, sd * 0.2));
      P.chrome.box(-1.05, cb.top + 1.72, zAt(0.4) - 0.1, 1.05, cb.top + 1.8, zAt(0.4) + 0.12);
      rail(0.72, 0.97, 0.55);
      seat(0.45, 0.2); seat(-0.45, 0.2);
      seats.unshift(new THREE.Vector3(0.35, cb.top + 0.5, zAt(0.48)));
      P.trim.color(0.88, 0.86, 0.8); roundBox(P.trim, 0.1, 0.6, cb.top, cb.top + 0.45, zAt(0.46), zAt(0.52), 0.05, 2);
      seatHip = 0.06;
      break;
    }
    case 'police': {
      const cb = cabin(0.4, 0.7, 1.25);
      // light bar and searchlight on the cabin roof; a gun mount on the foredeck
      const mk = (col, x0, x1) => {
        const g = new THREE.CylinderGeometry(0.1, 0.1, x1 - x0, 12); g.rotateZ(Math.PI / 2); g.scale(1, 0.75, 1.3);
        const m = new THREE.Mesh(g, patch(new THREE.MeshStandardMaterial({ color: col, emissive: new THREE.Color(col).multiplyScalar(0.2), roughness: 0.2, transparent: true, opacity: 0.92 }), { key: 'vlight' }));
        m.position.set((x0 + x1) / 2, cb.top + 0.12, zAt(0.55));
        bodyGroup.add(m);
        return m;
      };
      P.trim.color(0.1, 0.1, 0.1); P.trim.box(-0.7, cb.top, zAt(0.55) - 0.15, 0.7, cb.top + 0.05, zAt(0.55) + 0.15);
      lightbar = { red: mk(0xff1a1a, 0.03, 0.65), blue: mk(0x1a4dff, -0.65, -0.03) };
      P.chrome.color(0.35, 0.35, 0.37);
      P.chrome.geo(new THREE.CylinderGeometry(0.12, 0.16, 0.5, 10), mat4(0, deckY(0.82) + 0.25, zAt(0.82)));
      gun = new THREE.Group();
      gun.position.set(0, deckY(0.82) + 0.55, zAt(0.82));
      const gg = new Part();
      gg.color(0.18, 0.18, 0.2); gg.box(-0.14, -0.1, -0.35, 0.14, 0.12, 0.2);
      gg.geo(new THREE.CylinderGeometry(0.035, 0.035, 1.1, 8), mat4(0, 0, 0.7, Math.PI / 2));
      const gm = new THREE.Mesh(gg.build(), M.trim); gm.castShadow = true;
      gun.add(gm);
      bodyGroup.add(gun);
      rail(0.72, 0.96, 0.5);
      seat(0.45, 0.28); seat(-0.45, 0.28);
      seats.unshift(new THREE.Vector3(0.4, deckY(0.55) + 0.45, zAt(0.52)));
      seats.push(new THREE.Vector3(-0.4, deckY(0.55) + 0.45, zAt(0.52)));
      P.trim.color(0.2, 0.2, 0.22); roundBox(P.trim, 0.15, 0.65, deckY(0.55), deckY(0.55) + 0.42, zAt(0.5), zAt(0.56), 0.05, 2);
      outboard(0, 2);
      seatHip = 0.06;
      break;
    }
    default: { // speedboat
      windscreen(0.64, H.B / 2 - 0.15, 0.42, 0.9);
      console(0.58, 0.32, 0.72);
      seat(0.42, 0.5); seat(-0.42, 0.5);
      P.trim.color(0.88, 0.86, 0.8); roundBox(P.trim, -H.B / 2 + 0.2, H.B / 2 - 0.2, cockY(0.12), cockY(0.12) + 0.42, zAt(0.06), zAt(0.22), 0.06, 2);
      seats.push(new THREE.Vector3(0.45, cockY(0.14) + 0.42, zAt(0.14)), new THREE.Vector3(-0.45, cockY(0.14) + 0.42, zAt(0.14)));
      // engine hatch and twin exhausts in the transom
      P.deck.color(0.95, 0.95, 0.95); roundBox(P.deck, -0.7, 0.7, deckY(0.02), deckY(0.02) + 0.12, z0 + 0.1, z0 + 0.6, 0.05, 2);
      P.chrome.color(0.7, 0.7, 0.72);
      for (const sd of [1, -1]) P.chrome.geo(new THREE.CylinderGeometry(0.06, 0.06, 0.1, 10), mat4(sd * 0.5, 0.25, z0 - 0.03, Math.PI / 2));
      rail(0.78, 0.97, 0.25);
      seatHip = 0.06;
    }
  }
  // navigation lights: red port, green starboard at the bow, white stern light
  P.head.color(1, 1, 1);
  const bowT = 0.9;
  P.head.box(-0.04, deckY(bowT) + 0.05, zAt(bowT) - 0.04, 0.04, deckY(bowT) + 0.13, zAt(bowT) + 0.04);
  P.tail.color(1, 1, 1);
  P.tail.box(-0.05, deckY(0.02) + 0.05, z0 + 0.02, 0.05, deckY(0.02) + 0.15, z0 + 0.1);
  const mesh = (part, mat, cast = true) => { const m = new THREE.Mesh(part.build(), mat); m.castShadow = cast; m.receiveShadow = true; return m; };
  const body = mesh(P.hull, hullMat);
  bodyGroup.add(body);
  if (!P.deck.empty) bodyGroup.add(mesh(P.deck, patch(new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.7 }), { key: 'boatdeck' })));
  const trim = mesh(P.trim, M.trim);
  bodyGroup.add(trim);
  if (!P.chrome.empty) bodyGroup.add(mesh(P.chrome, M.chrome, false));
  const glass = P.glass.empty ? new THREE.Mesh(new THREE.BoxGeometry(0.01, 0.01, 0.01), M.glass) : mesh(P.glass, M.glass, false);
  glass.renderOrder = 3;
  bodyGroup.add(glass);
  const head = mesh(P.head, M.headOff, false), tail = mesh(P.tail, M.tailOff, false);
  bodyGroup.add(head, tail);
  // the propeller wash / prop, spun by the engine
  const prop = new THREE.Group();
  prop.position.set(0, -H.draft - 0.3, z0 - 0.2);
  bodyGroup.add(prop);
  if (!seats.length) seats.push(new THREE.Vector3(0.4, cockY(0.5) + 0.4, zAt(0.5)));
  const door = { pivot: new THREE.Group(), mesh: head, open: 0 };
  return {
    group, bodyGroup, body, bodyMat: hullMat, glass, head, tail, lightbar, gun, wheels: [], door, prop, trim,
    seats, seatHip, doorPos: new THREE.Vector3(H.B / 2 + 0.7, 0, seats[0].z), grips,
    hull: { draft: H.draft, free: H.free, half: S.half, L, z0 },
  };
}

// ------------------------------------------------------------------ water
// the swell (metres) at a point; bigger at sea than on the lake, and in a storm
export function waveHeight(game, x, z, t, lake = false) {
  const storm = game.env?.weather === 'storm' ? 2.2 : (game.env?.rain ?? 0) > 0.3 ? 1.4 : 1;
  const A = (lake ? 0.25 : 1) * storm;
  return A * (0.16 * Math.sin(x * 0.083 + t * 0.9) + 0.11 * Math.sin(z * 0.061 - t * 1.2 + 1.1) + 0.06 * Math.sin((x + z) * 0.19 + t * 1.8));
}

const GUN = { id: 'boatgun', damage: 22, range: 300, spread: 0.02, sound: 'smg' };

export class Boat extends Vehicle {
  buildModel(def, color) { return buildBoatModel(def, color); }

  setup() {
    this.floating = true;
    this.pitch = 0; this.roll = 0; this.heave = 0; this.heaveV = 0;
    this.propSpin = 0; this.gunT = 0; this.wakeT = 0; this.sinkT = 0;
    this.I = this.mass * (this.def.L * this.def.L + this.def.W * this.def.W) / 12;
    this.pos.y = this.waterLevel() - this.def.draft * 0.6;
  }
  get armed() { return !!this.def.weapons; }
  get showCrosshair() { return this.armed; }
  get isBoat() { return true; }

  waterLevel() { return this.game.map.waterLevel ? this.game.map.waterLevel(this.pos.x, this.pos.z) : WATER_Y; }
  get lake() { return this.waterLevel() > WATER_Y + 1; }

  // climb aboard from whichever side you're on (from a pontoon or out of the water)
  doorFor(seat, char) {
    const s = this.model.seats[seat] || this.model.seats[0];
    const side = char ? (this.worldToLocal(char.pos.x, char.pos.z)[0] >= 0 ? 1 : -1) : 1;
    return this.localToWorld(side * (this.def.W / 2 + 0.6), 0, s.z, new THREE.Vector3());
  }
  nearestDoor(pos) { const p = this.doorFor(0, { pos }); p.seat = 0; return p; }
  // jet ski: astride, feet on the footwells, hands on the bars
  feetFor() { return [[0.26, -0.28, 0.1], [-0.26, -0.28, 0.1]]; }
  gripsFor() { return [0.26, 0.5]; }

  playerControl(input, dt) {
    super.playerControl(input, dt);
    this.input.handbrake = false;
    if (this.armed && input.vehFire?.() && this.gunT <= 0) this.fireGun();
  }

  takeOut(char, pos) {
    super.takeOut(char, pos);
    // stepping off into open water: you're swimming (not standing on the seabed)
    const wl = this.waterLevel(), bed = this.game.map.terrainHeight?.(char.pos.x, char.pos.z) ?? this.game.map.groundHeight(char.pos.x, char.pos.z);
    if (wl - bed > 1.3 && char.pos.y < wl + 0.2) { char.pos.y = wl - 0.5; char.vel.y = 0; char.grounded = false; }
  }

  fireGun(target = null) {
    const m = this.model;
    if (!m.gun) return;
    this.gunT = 0.1;
    m.gun.updateMatrixWorld(true);
    const muzzle = new THREE.Vector3(0, 0, 1.3).applyMatrix4(m.gun.matrixWorld);
    let aim;
    if (target) aim = target.clone();
    else { const cam = this.game.camera, dir = this.game.rig.lookDir(new THREE.Vector3()); const hit = this.game.combat.raycast(cam.position.x, cam.position.y, cam.position.z, dir.x, dir.y, dir.z, 300, this); aim = hit ? hit.point : cam.position.clone().addScaledVector(dir, 300); }
    const dir = aim.sub(muzzle).normalize();
    this.game.combat.vehicleGun(this.driver, muzzle, dir, GUN);
    if (this.driver?.isPlayer) this.game.rig.addShake(0.05);
  }

  update(dt) {
    if (this.removed) return;
    this.gunT -= dt;
    const inp = this.input, d = this.def;
    if (this.isWrecked || !this.driver) { inp.throttle = 0; inp.brake = 0; inp.steer *= 0.9; }
    const steps = 2, h = dt / steps;
    for (let i = 0; i < steps; i++) this._boatStep(h);
    this._boatAfter(dt);
    this._updateVisual(dt);
  }

  _boatStep(h) {
    const d = this.def, inp = this.input, m = this.mass;
    const s = Math.sin(this.yaw), c = Math.cos(this.yaw);
    const fx = s, fz = c, rx = -c, rz = s;
    const vLong = this.vel.x * fx + this.vel.z * fz, vLat = this.vel.x * rx + this.vel.z * rz;
    if (!this.floating) {
      // aground: dragged to a stop on the sand
      const k = Math.max(0, 1 - 4 * h);
      this.vel.x *= k; this.vel.z *= k; this.r *= k;
      this.pos.x += this.vel.x * h; this.pos.z += this.vel.z * h;
      return;
    }
    const run = !this.exploded && !this.sunk && this.health > 0;
    let thrust = 0;
    if (run && inp.throttle > 0) thrust = inp.throttle * d.force;
    else if (run && inp.brake > 0) thrust = -inp.brake * d.force * (vLong > 1 ? 0.8 : 0.35);
    // hull drag: most of the thrust goes into pushing water until she climbs onto the plane
    const kq = d.force * 0.85 / (d.top * d.top), kl = d.force * 0.15 / d.top;
    const Fl = thrust - Math.sign(vLong) * kq * vLong * vLong - kl * vLong;
    // the keel resists going sideways
    const Flat = -vLat * m * 1.9;
    this.vel.x += (fx * Fl + rx * Flat) / m * h;
    this.vel.z += (fz * Fl + rz * Flat) / m * h;
    // rudder / jet steering: needs water flowing past it (or the prop turning)
    const flow = clamp((Math.abs(vLong) + (run ? inp.throttle * 3 : 0)) / 6, 0, 1);
    const wantR = inp.steer * d.turn * flow * (vLong < -0.5 ? -1 : 1) * (1 - 0.35 * clamp(Math.abs(vLong) / d.top, 0, 1));
    this.r += (wantR - this.r) * Math.min(1, h * 2.2);
    this.yaw += this.r * h;
    this.pos.x += this.vel.x * h; this.pos.z += this.vel.z * h;
    this.axLong = damp(this.axLong, Fl / m, 6, h);
    this.ayLat = damp(this.ayLat, this.r * vLong, 6, h);
  }

  _boatAfter(dt) {
    const g = this.game, map = g.map, d = this.def;
    const wl = this.waterLevel(), lake = wl > WATER_Y + 1;
    const ground = map.terrainHeight ? map.terrainHeight(this.pos.x, this.pos.z) : map.groundHeight(this.pos.x, this.pos.z); // (the seabed, not a pier deck)
    const depth = wl - ground;
    const t = g.time;
    // aground when the keel touches bottom
    const wasFloat = this.floating;
    this.floating = depth > d.draft * 0.75;
    if (!this.floating && wasFloat && this.speedAbs > 4) { this.damage(this.speedAbs * 6); g.audio?.playAt('crash', this.pos, 0.5); }
    // ride the swell; lift onto the plane at speed
    const plane = clamp((this.speedAbs - d.top * 0.3) / (d.top * 0.4), 0, 1);
    const wv = waveHeight(g, this.pos.x, this.pos.z, t, lake);
    let ty = wl + wv - d.draft * 0.6 + plane * d.draft * 0.35;
    if (this.sinking) ty = wl - this.sinkT * 0.6;
    if (!this.floating) ty = Math.max(ty, ground + d.draft * 0.9);
    this.heaveV += ((ty - this.pos.y) * 30 - this.heaveV * 7) * dt;
    this.pos.y += this.heaveV * dt;
    // pitch & roll: the waves under bow / stern / sides, bow up under power, heel into the turn
    const L = d.L * 0.4, W = d.W * 0.45, s = Math.sin(this.yaw), c = Math.cos(this.yaw);
    const wb = waveHeight(g, this.pos.x + s * L, this.pos.z + c * L, t, lake), ws = waveHeight(g, this.pos.x - s * L, this.pos.z - c * L, t, lake);
    const wlft = waveHeight(g, this.pos.x - c * W, this.pos.z + s * W, t, lake), wrt = waveHeight(g, this.pos.x + c * W, this.pos.z - s * W, t, lake);
    const tp = Math.atan2(wb - ws, 2 * L) * 0.8 + plane * 0.07 + clamp(this.axLong * 0.01, -0.05, 0.08);
    const tr = Math.atan2(wlft - wrt, 2 * W) * 0.8 + clamp(-this.ayLat * 0.02, -0.25, 0.25) + (this.sinking ? Math.min(0.5, this.sinkT * 0.1) : 0);
    this.pitch = damp(this.pitch, tp, 3, dt);
    this.roll = damp(this.roll, tr, 3, dt);
    this.groundPitch = this.pitch; this.groundRoll = this.roll;
    this.airborne = false;
    // piers, pilings, the ship, the quay
    const list = g.collision.obbContacts(this.pos.x, this.pos.z, this.yaw, this.hx, this.hz, this.pos.y + 0.2, this.contacts);
    for (const ct of list) this._resolveStatic(ct);
    // wrecked: burn, blow up, go down
    if (!this.exploded && this.health <= 0) { this.onFire = true; this.burnTime += dt; if (this.burnTime > 4) this.explode(); }
    if (this.exploded) { this.sinking = true; this.sinkT += dt; if (this.sinkT > 12 && !this.sunk) { this.sunk = true; g.events?.emit('vehicleSunk', this); } }
    // spray, foam and the wake
    const spd = this.speedAbs;
    this.propSpin += (this.input.throttle * 40 + spd) * dt;
    if (this.floating && spd > 2.5 && g.effects) {
      this.wakeT -= dt;
      const e = g.effects;
      const stern = _v.set(this.pos.x - s * d.L * 0.5, wl + 0.05, this.pos.z - c * d.L * 0.5);
      if (e.wake) e.wake.add(this.vid, stern.x, wl + 0.04, stern.z, d.W * (0.8 + spd * 0.03), clamp(spd / 12, 0.2, 1));
      if (this.wakeT <= 0) {
        this.wakeT = 0.05;
        e.foam?.(stern, s, c, spd, d.W);
        if (spd > 8) e.bowSpray?.(new THREE.Vector3(this.pos.x + s * d.L * 0.3, wl + 0.2, this.pos.z + c * d.L * 0.3), s, c, spd, d.W);
      }
    } else g.effects?.wake?.break(this.vid);
  }

  _updateVisual(dt) {
    const g = this.group, m = this.model;
    g.rotation.order = 'YXZ';
    g.rotation.y = this.yaw;
    g.rotation.x = -this.pitch;
    g.rotation.z = this.roll;
    if (m.prop) m.prop.rotation.z = this.propSpin;
    // lights at night / in the rain
    const M = vehicleMaterials();
    const night = (this.game.env?.night ?? 0) > 0.4 || (this.game.env?.rain ?? 0) > 0.3;
    const on = night && !!this.driver && !this.isWrecked;
    if (on !== this.lightsOn) { this.lightsOn = on; m.head.material = on ? M.headOn : M.headOff; m.tail.material = on ? M.tailOn : M.tailOff; }
    if (m.lightbar) {
      const tt = this.game.time * 7 + this.sirenPhase;
      const onR = this.sirenOn && Math.sin(tt) > 0, onB = this.sirenOn && Math.sin(tt) <= 0;
      m.lightbar.red.material.emissive.setRGB(onR ? 9 : 0.15, onR ? 0.3 : 0, 0);
      m.lightbar.blue.material.emissive.setRGB(0, onB ? 0.6 : 0, onB ? 12 : 0.2);
    }
    if (m.gun && this.driver?.isPlayer) {
      // the bow gun follows the camera
      const dir = this.game.rig.lookDir(_v);
      const yaw = wrapAngle(Math.atan2(dir.x, dir.z) - this.yaw);
      m.gun.rotation.set(-clamp(Math.asin(clamp(dir.y, -1, 1)), -0.3, 0.5), clamp(yaw, -2.2, 2.2), 0, 'YXZ');
    }
    if (this._normalsDirty && dt > 0) { this.model.body.geometry.computeVertexNormals(); this._normalsDirty = false; }
  }

  explode() {
    if (this.exploded) return;
    super.explode();
    this.sinking = true;
    this.vy = 0; this.airborne = false;
  }
}
