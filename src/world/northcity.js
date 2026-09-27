// San Aurelio, the second city: a bay city on the north-east coast. Its streets are not a grid. Three
// wobbly ring roads circle the Plaza de Aurelio, seven curving avenues run out from it, and side streets
// split the wedges between them, so every block is a different lopsided, curved shape. Towers crowd the
// centre, mid-rise brick and art deco the inner rings, and low stucco shops and houses the outer ring.
// Roads are built into the regional network (traffic, GPS, police all just work); this file also lines the
// streets with raised pavements (collision decks), buildings, walk paths, lamps and trees.
import { NCITY, ncRingR, ncEdgeDist, TOWNS, coastX } from './worldgen.js';
import { RNG, clamp, lerp } from '../core/utils.js';

const C = NCITY, TAU = Math.PI * 2;
export const NC_MAIN = [
  { th: -0.05, A: 0.10, ph: 0.3, name: 'Avenida del Mar', out: 'harbor' },
  { th: 0.86, A: 0.12, ph: 1.4, name: 'Bay Avenue', out: 'bay' },
  { th: 1.62, A: 0.09, ph: 2.2, name: 'Mission Street' },
  { th: 2.33, A: 0.11, ph: 0.9, name: 'Ridge Avenue', out: 'ridge' },
  { th: 3.2, A: 0.10, ph: 2.9, name: 'Timber Avenue', out: 'timber' },
  { th: 4.08, A: 0.13, ph: 1.7, name: 'Cathedral Way' },
  { th: 5.1, A: 0.10, ph: 0.4, name: 'Northgate Avenue' },
];
const RING_NAMES = ['Plaza Circle', 'Crescent Boulevard', 'Hillcrest Parkway'];
const STREETS = ['Alma Street', 'Calle Rosa', 'Vine Street', 'Chapel Lane', 'Olive Street', 'Harbor Row', 'Tanner Street', 'Lark Street', 'Bell Street',
  'Cypress Lane', 'Mercer Street', 'Quarry Street', 'Pilgrim Way', 'Ash Street', 'Loma Street', 'Fountain Street', 'Sparrow Lane', 'Orchard Street',
  'Linden Street', 'Canal Street', 'Poet\'s Walk', 'Cooper Street', 'Silver Street', 'Dover Street', 'Kestrel Lane', 'Juniper Street', 'Sable Street'];
// side streets left out of the outer band: the big blocks they leave hold the park and the arena
const SKIP = new Set(['2:0.67', '5:0.34']);
export const NC_WALK = { avenue: 3.6, road: 2.8 };

export function mainTh(k, r) {
  const s = NC_MAIN[k % 7];
  return s.th + (k >= 7 ? TAU : 0) + s.A * Math.sin(r / 190 + s.ph) * Math.min(1, r / 160);
}
const P = (r, th) => [C.x + r * Math.cos(th), C.z + r * Math.sin(th)];
const wrap = (a) => ((a % TAU) + TAU) % TAU;
// radius at which a spoke (angle as a function of radius) crosses ring j
function crossR(f, j) { let r = C.rings[j]; for (let i = 0; i < 16; i++) r = ncRingR(j, f(r)); return r; }
export function ncSpokeEnd(key) {
  const k = NC_MAIN.findIndex((s) => s.out === key);
  const f = (r) => mainTh(k, r);
  const r = crossR(f, 2) + 62;
  return P(r, f(r));
}

function spokeDefs() {
  const out = [];
  for (let k = 0; k < 7; k++) out.push({ f: (r) => mainTh(k, r), j0: -1, j1: 2, main: true, k, type: 'avenue', name: NC_MAIN[k].name, ext: NC_MAIN[k].out ? 62 : 0 });
  let si = 0;
  const between = (k, frac, wob) => (r) => { const a = mainTh(k, r), b = mainTh(k + 1, r); return a + (b - a) * (frac + wob * Math.sin(r / 85 + k * 1.7 + frac * 5)); };
  for (let k = 0; k < 7; k++) {
    out.push({ f: between(k, 0.5, 0.05), j0: 0, j1: 1, type: 'road', name: STREETS[si++ % STREETS.length], k, frac: 0.5 });
    for (const fr of [0.34, 0.67]) {
      if (SKIP.has(`${k}:${fr}`)) continue;
      out.push({ f: between(k, fr, 0.04), j0: 1, j1: 2, type: 'road', name: STREETS[si++ % STREETS.length], k, frac: fr });
    }
  }
  return out;
}

// the two superblocks left by SKIP: centre and facing
export function ncSuperblocks() {
  const out = [];
  for (const key of SKIP) {
    const [k, fr] = key.split(':').map(Number);
    const r = (C.rings[1] + C.rings[2]) / 2 + 8;
    // halfway between the neighbouring streets that remain (the main spoke and the other side street)
    const lo = fr < 0.5 ? 0 : 0.34, hi = fr < 0.5 ? 0.67 : 1;
    const a = mainTh(k, r), b = mainTh(k + 1, r), th = a + (b - a) * (lo + hi) / 2;
    const rr = (ncRingR(1, th) + ncRingR(2, th)) / 2;
    const [x, z] = P(rr, th);
    out.push({ x, z, th, r: rr, kind: out.length ? 'arena' : 'park', span: (b - a) * (hi - lo) * rr, depth: ncRingR(2, th) - ncRingR(1, th) });
  }
  return out;
}

// districts (for zone names and ped density)
export function ncDistrict(x, z) {
  if (ncEdgeDist(x, z) > 60) return null;
  const dx = x - C.x, dz = z - C.z, r = Math.hypot(dx, dz), th = wrap(Math.atan2(dz, dx));
  if (r < ncRingR(0, th) + 8) return { key: 'aurcentro', name: 'Centro' };
  const inner = r < ncRingR(1, th) + 8;
  if (th < 0.75 || th > 5.6) return inner ? { key: 'aurharbor', name: 'Harborside' } : { key: 'aurbay', name: 'Bayview' };
  if (th < 2.4) return inner ? { key: 'aurmission', name: 'Mission' } : { key: 'aurbay', name: 'Bayview' };
  if (th < 3.9) return inner ? { key: 'aurcathedral', name: 'Cathedral Hill' } : { key: 'aurheights', name: 'Aurelio Heights' };
  return inner ? { key: 'aurnorth', name: 'Northgate' } : { key: 'aurheights', name: 'Aurelio Heights' };
}

// ------------------------------------------------------------------ roads
export function buildNorthCity(net, info) {
  const y = C.y;
  const defs = spokeDefs();
  const ringNodes = [[], [], []];
  const mkNode = (x, z, o) => { const n = net.addNode(x, z, y, o); n.ncity = true; return n; };
  const center = mkNode(C.x, C.z, { kind: 'rb', rbR: 24, r: 32, name: 'Plaza de Aurelio' });
  info.rbs.push(center);
  const edges = [];
  const addEdge = (a, b, pts, type, name) => {
    const e = net.addEdge(a, b, pts.map(([x, z]) => [x, z, y]), type, { name });
    e.ncity = true; e.walk = NC_WALK[type] || 3;
    edges.push(e);
    return e;
  };
  const ends = {};
  for (const d of defs) {
    const stops = [];
    if (d.j0 < 0) stops.push({ r: 0, node: center });
    for (let j = Math.max(0, d.j0); j <= d.j1; j++) {
      const r = crossR(d.f, j), th = d.f(r);
      const [x, z] = P(r, th);
      const rb = d.main && j === 1;
      const node = mkNode(x, z, rb ? { kind: 'rb', rbR: 12.5, r: 20, name: d.name } : { kind: 'x', r: 10, name: d.name });
      if (rb) info.rbs.push(node);
      ringNodes[j].push({ th: wrap(th), node });
      stops.push({ r, node });
    }
    if (d.ext) {
      const r = stops[stops.length - 1].r + d.ext, [x, z] = P(r, d.f(r));
      const node = mkNode(x, z, { kind: 'x', r: 10, name: d.name });
      stops.push({ r, node });
      ends[NC_MAIN[d.k].out] = node;
    }
    for (let i = 0; i < stops.length - 1; i++) {
      const a = stops[i], b = stops[i + 1];
      const n = Math.max(2, Math.ceil((b.r - a.r) / 6));
      const pts = [[a.node.x, a.node.z]];
      for (let q = 1; q < n; q++) { const r = a.r + (b.r - a.r) * q / n; pts.push(P(r, d.f(r))); }
      pts.push([b.node.x, b.node.z]);
      const e = addEdge(a.node, b.node, pts, d.type, d.name);
      if (d.main && stops[i + 1].r > C.rings[2] + 20) e.ext = true;
    }
  }
  for (let j = 0; j < 3; j++) {
    const L = ringNodes[j].sort((a, b) => a.th - b.th);
    for (let i = 0; i < L.length; i++) {
      const A = L[i], B = L[(i + 1) % L.length];
      const a = A.th; let b = B.th; if (b <= a) b += TAU;
      const n = Math.max(2, Math.ceil((b - a) * C.rings[j] / 6));
      const pts = [[A.node.x, A.node.z]];
      for (let q = 1; q < n; q++) { const th = a + (b - a) * q / n; pts.push(P(ncRingR(j, th), th)); }
      pts.push([B.node.x, B.node.z]);
      addEdge(A.node, B.node, pts, 'avenue', RING_NAMES[j]);
    }
  }
  info.ncity = { center, edges, ends };
  return info.ncity;
}

// ------------------------------------------------------------------ everything along the streets
const SIGNS = [['AURELIO HOTEL', 'Hotel Aurelio'], ['CAFE ROMA', 'Cafe Roma'], ['BAYVIEW DINER', 'Bayview Diner'], ['PHARMACY', 'Mission Pharmacy'],
  ['BOOKS', 'Old Harbor Books'], ['PIZZA', 'Nonna\'s Pizza'], ['BANK', 'Pacific Trust Bank'], ['CINEMA', 'The Aurelio Cinema'], ['MARKET', 'Northgate Market'],
  ['BAR', 'The Lantern Bar'], ['TATTOO', 'Anchor Tattoo'], ['BAKERY', 'Panaderia Sol'], ['GYM', 'Iron Bay Gym'], ['JEWELER', 'Castell Jewelers']];

export function populateNorthCity(ctx) {
  const { map, net, hf } = ctx;
  const rng = new RNG(5151);
  const nc = map.roadInfo.ncity;
  if (!nc) return;
  const y0 = C.y;
  // spatial hash of footprints placed here (the countryside's own list is only for its towns)
  const FC = 48, FH = new Map();
  const fkey = (gx, gz) => gx * 73856093 ^ gz * 19349663;
  const addFP = (r) => {
    const R = Math.hypot(r.hx, r.hz);
    for (let gx = Math.floor((r.cx - R) / FC); gx <= Math.floor((r.cx + R) / FC); gx++) for (let gz = Math.floor((r.cz - R) / FC); gz <= Math.floor((r.cz + R) / FC); gz++) {
      const k = fkey(gx, gz); let a = FH.get(k); if (!a) FH.set(k, a = []); a.push(r);
    }
    ctx.footprints.push(r);
  };
  const nodes = net.nodes.filter((n) => n.ncity && n.e.length);
  const NH = new Map();
  for (const n of nodes) { const k = fkey(Math.floor(n.x / FC), Math.floor(n.z / FC)); let a = NH.get(k); if (!a) NH.set(k, a = []); a.push(n); }
  // does the rect (grown by m) hit a footprint, a road (with its pavement), or a junction?
  const free = (r, m = 0.6) => {
    const R = Math.hypot(r.hx, r.hz) + m;
    const g = { ...r, hx: r.hx + m, hz: r.hz + m };
    for (let gx = Math.floor((r.cx - R) / FC); gx <= Math.floor((r.cx + R) / FC); gx++) for (let gz = Math.floor((r.cz - R) / FC); gz <= Math.floor((r.cz + R) / FC); gz++) {
      const a = FH.get(fkey(gx, gz));
      if (a) for (const f of a) if (Math.abs(f.cx - r.cx) < R + f.hx + f.hz && Math.abs(f.cz - r.cz) < R + f.hx + f.hz && rectsOverlap(g, f)) return false;
      const ns = NH.get(fkey(gx, gz));
      if (ns) for (const n of ns) { const rad = n.kind === 'rb' ? n.rbR + 5.4 + 4.2 : n.r + 4.2; if (rectCircle(r, n.x, n.z, rad)) return false; }
    }
    const s = Math.sin(r.yaw), c = Math.cos(r.yaw);
    for (const [lx, lz] of [[-1, -1], [1, -1], [1, 1], [-1, 1], [0, -1], [0, 1], [-1, 0], [1, 0], [0, 0]]) {
      const x = r.cx + lx * r.hx * c + lz * r.hz * s, z = r.cz - lx * r.hx * s + lz * r.hz * c;
      if (net.onRoad(x, z, 3.9)) return false;
      if (ncEdgeDist(x, z) > 20) return false;
    }
    return true;
  };
  const addB = (r, height, style, o = {}) => {
    const b = map._addBuilding(null, r.cx - r.hx, r.cz - r.hz, r.cx + r.hx, r.cz + r.hz, height, style, {
      rot: r.yaw, y0: o.y0 ?? y0 - 0.3, tint: o.tint, seed: o.seed ?? rng.next(), roof: o.roof || 'flat', floorH: o.floorH, cell: o.cell, kind: o.kind || 'building',
      name: o.name, sign: o.sign, base: o.base ?? y0 - 0.3,
    });
    if (b) { b.district = 'aurelio'; b.front = o.front || null; b.foundation = 0.3; }
    return b;
  };
  const warm = () => rng.pick([[1, 0.95, 0.86], [0.96, 0.9, 0.8], [0.92, 0.86, 0.78], [1, 0.9, 0.78], [0.88, 0.84, 0.8], [0.95, 0.93, 0.9]]);
  const glass = () => rng.pick([[0.75, 0.85, 0.95], [0.7, 0.8, 0.85], [0.85, 0.9, 0.95], [0.65, 0.75, 0.8], [0.8, 0.8, 0.85]]);
  const brick = () => rng.pick([[0.95, 0.75, 0.65], [0.85, 0.65, 0.55], [1, 0.85, 0.72], [0.8, 0.7, 0.65], [0.9, 0.8, 0.72]]);
  const pastel = () => rng.pick([[1, 0.93, 0.82], [0.93, 0.97, 0.95], [1, 0.86, 0.78], [0.9, 0.93, 1], [1, 0.96, 0.75], [0.95, 0.85, 0.9], [0.86, 0.95, 0.86]]);
  const rOf = (x, z) => Math.hypot(x - C.x, z - C.z);

  // ---- landmarks first, so the street frontage packs round them
  const plaza = { cx: C.x, cz: C.z, hx: 26, hz: 26, yaw: 0 };
  addFP(plaza);
  map.props.push({ type: 'fountain', x: C.x, z: C.z, rot: 0, y: y0 + 0.3 });
  for (let k = 0; k < 8; k++) { const a = k / 8 * TAU; map.props.push({ type: 'palm', x: C.x + Math.cos(a) * 12.5, z: C.z + Math.sin(a) * 12.5, rot: a, scale: 1.1, y: y0 + 0.3 }); }
  const supers = ncSuperblocks();
  map.ncity = { supers, plaza: { x: C.x, z: C.z } };
  for (const sb of supers) {
    const yaw = Math.atan2(Math.cos(sb.th), -Math.sin(sb.th)); // local z along the ring
    const hz = Math.min(80, sb.span * 0.36), hx = Math.min(58, sb.depth * 0.3);
    const r = { cx: sb.x, cz: sb.z, hx, hz, yaw };
    addFP(r);
    if (sb.kind === 'park') {
      map.padSurfaces.push({ cx: sb.x, cz: sb.z, hx: hx + 2, hz: hz + 2, yaw, y: y0 + 0.05, type: 'grass' });
      map.padSurfaces.push({ cx: sb.x, cz: sb.z, hx: 2.2, hz: hz + 2, yaw, y: y0 + 0.07, type: 'path' });
      map.padSurfaces.push({ cx: sb.x, cz: sb.z, hx: hx + 2, hz: 2.2, yaw, y: y0 + 0.07, type: 'path' });
      map.props.push({ type: 'fountain', x: sb.x, z: sb.z, rot: 0, y: y0 + 0.1 });
      const s = Math.sin(yaw), c = Math.cos(yaw);
      for (let k = 0; k < 34; k++) {
        const lx = rng.range(-hx, hx), lz = rng.range(-hz, hz);
        if (Math.abs(lx) < 5 || Math.abs(lz) < 5) continue;
        map.props.push({ type: rng.chance(0.2) ? 'palm' : 'tree', x: sb.x + lx * c + lz * s, z: sb.z - lx * s + lz * c, rot: rng.range(0, 6.28), scale: rng.range(0.9, 1.4), y: y0 + 0.05 });
      }
      for (let k = -3; k <= 3; k++) if (k) for (const sd of [-1, 1]) {
        const lx = sd * 3.4, lz = k * hz / 4;
        map.props.push({ type: 'bench', x: sb.x + lx * c + lz * s, z: sb.z - lx * s + lz * c, rot: yaw + (sd > 0 ? -Math.PI / 2 : Math.PI / 2), y: y0 + 0.07 });
      }
      map.landmarks.aurPark = { x: sb.x, z: sb.z, name: 'Bayview Park' };
    } else {
      // the arena: a low bowl of a building with a lit roof
      map.padSurfaces.push({ cx: sb.x, cz: sb.z, hx: hx + 3, hz: hz + 3, yaw, y: y0 + 0.05, type: 'plaza' });
      addB({ cx: sb.x, cz: sb.z, hx: hx - 6, hz: hz - 8, yaw }, 24, 0, { tint: [0.85, 0.87, 0.92], roof: 'ac', name: 'Aurelio Arena', floorH: 6 });
      map.landmarks.aurArena = { x: sb.x, z: sb.z, name: 'Aurelio Arena' };
    }
  }

  // ---- frontage along every street
  const tmp = [0, 0, 0, 0, 0];
  let signI = 0, towers = 0;
  const cathedral = { done: false };
  for (const e of nc.edges) {
    const na = net.nodes[e.a], nb = net.nodes[e.b];
    const clr = (n) => (n.kind === 'rb' ? n.rbR + 10 : n.r + 3);
    const sA = clr(na), sB = e.len - clr(nb);
    for (const side of [-1, 1]) {
      let s = sA + rng.range(0, 3);
      while (s < sB - 6) {
        net.at(e, s, tmp);
        const r0 = rOf(tmp[0], tmp[2]);
        const core = r0 < 205, inner = !core && r0 < 360;
        const w0 = core ? rng.range(20, 34) : inner ? rng.range(14, 26) : rng.range(10, 20);
        const d0 = core ? rng.range(26, 40) : inner ? rng.range(18, 30) : rng.range(12, 22);
        // too wide for the inside of a bend or too deep for the block? try smaller before moving on
        let r = null, w = 0, rx = 0, rz = 0;
        for (const [kw, kd] of [[1, 1], [0.62, 1], [1, 0.55], [0.62, 0.55], [0.45, 0.4]]) {
          w = w0 * kw; const d = d0 * kd;
          if (s + w > sB) continue;
          net.at(e, s + w / 2, tmp);
          const tx = tmp[3], tz = tmp[4];
          rx = -tz * side; rz = tx * side;
          const off = Math.max(e.wL, e.wR) + 4.3 + d / 2 + (w * w) / (8 * 110);
          const q = { cx: tmp[0] + rx * off, cz: tmp[2] + rz * off, hx: d / 2, hz: w / 2, yaw: Math.atan2(tx, tz) };
          if (free(q)) { r = q; break; }
        }
        if (!r) { s += 4; continue; }
        const front = [-rx, -rz];
        const rc = rOf(r.cx, r.cz);
        if (core) {
          // towers: tallest by the plaza
          const H = lerp(175, 40, clamp(rc / 215, 0, 1)) * rng.range(0.55, 1.1) + (towers === 2 ? 70 : 0);
          towers++;
          const st = rng.weighted([[1, 5], [0, 3], [6, 2]]);
          const tint = st === 1 ? glass() : warm();
          const podH = rng.range(9, 15);
          if (H < 50 || rng.chance(0.3)) addB(r, H, st, { tint, roof: rng.chance(0.4) ? 'antenna' : 'helipad', front, floorH: st === 1 ? 3.8 : 3.4 });
          else {
            addB(r, podH, rng.chance(0.5) ? 5 : st, { tint, front, floorH: 4.2 });
            const ins = Math.min(r.hx, r.hz) * rng.range(0.18, 0.3);
            const t1 = { ...r, hx: r.hx - ins, hz: r.hz - ins };
            const top = y0 - 0.3 + podH;
            if (rng.chance(0.5)) {
              const h1 = (H - podH) * rng.range(0.6, 0.8), in2 = Math.min(t1.hx, t1.hz) * 0.2;
              addB(t1, h1, st, { tint, y0: top, base: y0 - 0.3, roof: 'flat' });
              addB({ ...t1, hx: t1.hx - in2, hz: t1.hz - in2 }, H - podH - h1, st, { tint, y0: top + h1, base: y0 - 0.3, roof: rng.chance(0.5) ? 'spire' : 'helipad' });
            } else addB(t1, H - podH, st, { tint, y0: top, base: y0 - 0.3, roof: rng.chance(0.5) ? 'antenna' : 'helipad' });
          }
        } else if (inner) {
          const th = wrap(Math.atan2(r.cz - C.z, r.cx - C.x));
          if (!cathedral.done && th > 2.6 && th < 3.7 && w > 20) {
            // the cathedral on its hill: tall nave with a spire
            cathedral.done = true;
            addB(r, 26, 6, { tint: [0.93, 0.9, 0.84], roof: 'spire', name: 'Cathedral of San Aurelio', front, floorH: 6.5 });
            map.landmarks.aurCathedral = { x: r.cx, z: r.cz, name: 'Cathedral of San Aurelio' };
          } else {
            const shop = rng.chance(0.35) && signI < SIGNS.length;
            const st = shop ? 5 : rng.weighted([[2, 4], [6, 2], [0, 2], [5, 2], [3, 1]]);
            const floors = rng.int(3, 9);
            const fh = st === 5 ? 4.2 : 3.4;
            const tint = st === 2 ? brick() : warm();
            if (shop) { const [sign, name] = SIGNS[signI++]; addB(r, floors * fh + 0.6, st, { tint, sign, name, front, floorH: fh, roof: 'ac' }); }
            else addB(r, floors * fh + (st === 5 ? 0.6 : 0), st, { tint, front, floorH: fh, roof: rng.chance(0.35) ? 'ac' : 'flat' });
          }
        } else {
          const house = rng.chance(0.3) && e.type === 'road';
          if (house) addB(r, rng.int(1, 2) * 3.1 + 0.4, 3, { tint: pastel(), roof: 'gable', floorH: 3.1, cell: 3.0, kind: 'house', front });
          else {
            const shop = rng.chance(0.3) && signI < SIGNS.length;
            const st = shop ? 5 : rng.weighted([[3, 4], [5, 3], [2, 2]]);
            const fh = st === 5 ? 4.2 : 3.3;
            const floors = rng.int(2, 5);
            if (shop) { const [sign, name] = SIGNS[signI++]; addB(r, floors * fh + 0.6, 5, { tint: pastel(), sign, name, front, floorH: fh }); }
            else addB(r, floors * fh, st, { tint: st === 2 ? brick() : pastel(), front, floorH: fh, roof: rng.chance(0.4) ? 'ac' : 'flat' });
          }
        }
        addFP(r);
        s += w + (rng.chance(core ? 0.1 : 0.25) ? rng.range(2, 6) : 0.4);
      }
    }
  }
  // ---- back lots: courtyard buildings, car parks and gardens in the hearts of the bigger blocks
  const B = 16;
  for (let gx = Math.floor((C.x - 620) / B); gx <= Math.ceil((C.x + 620) / B); gx++) for (let gz = Math.floor((C.z - 620) / B); gz <= Math.ceil((C.z + 620) / B); gz++) {
    const x = gx * B + rng.range(-3, 3), z = gz * B + rng.range(-3, 3);
    if (ncEdgeDist(x, z) > -10) continue;
    const c = net.closest(x, z, (e) => e.ncity, 80);
    if (!c || c.d < 22) continue;
    net.at(c.e, c.s, tmp);
    const yaw = Math.atan2(tmp[3], tmp[4]);
    const rc = rOf(x, z);
    const k = rng.next();
    const sz = rc < 175 ? rng.range(10, 16) : rng.range(8, 13);
    const r = { cx: x, cz: z, hx: sz, hz: sz * rng.range(0.8, 1.3), yaw };
    if (!free(r, 1.5)) continue;
    if (k < 0.55) {
      const floors = rc < 175 ? rng.int(5, 12) : rc < 335 ? rng.int(2, 5) : rng.int(1, 3);
      addB(r, floors * 3.4, rng.pick([0, 2, 3, 2]), { tint: rng.chance(0.5) ? warm() : brick(), roof: rng.chance(0.5) ? 'ac' : 'flat', floorH: 3.4 });
    } else if (k < 0.82) {
      map.padSurfaces.push({ cx: x, cz: z, hx: r.hx, hz: r.hz, yaw, y: y0 + 0.04, type: 'asphalt' });
      const s = Math.sin(yaw), cc = Math.cos(yaw);
      for (let q = -1; q <= 1; q += 2) if (rng.chance(0.7)) map.parkingSpots.push({ x: x + q * r.hx * 0.45 * cc, z: z - q * r.hx * 0.45 * s, rot: yaw, district: 'aurelio', lot: true });
    } else {
      map.padSurfaces.push({ cx: x, cz: z, hx: r.hx, hz: r.hz, yaw, y: y0 + 0.04, type: 'grass' });
      for (let q = 0; q < 3; q++) map.props.push({ type: 'tree', x: x + rng.range(-r.hx, r.hx) * 0.7, z: z + rng.range(-r.hz, r.hz) * 0.7, rot: rng.range(0, 6.28), scale: rng.range(0.8, 1.2), y: y0 + 0.04 });
      if (rng.chance(0.5)) map.props.push({ type: 'bench', x, z, rot: yaw, y: y0 + 0.04 });
    }
    addFP(r);
  }

  // ---- pavements: walk graph, lamps, trees, bins, hydrants, bus stops
  const walk = map.walkNodes;
  const areaCells = new Map();
  const cellOf = (x, z) => {
    const k = Math.floor(x / 140) + ',' + Math.floor(z / 140);
    let a = areaCells.get(k);
    if (!a) { const cx = (Math.floor(x / 140) + 0.5) * 140, cz = (Math.floor(z / 140) + 0.5) * 140; areaCells.set(k, a = { district: ncDistrict(cx, cz)?.key || 'aurelio', nodeIds: [], name: C.name, x: cx, z: cz, r: 100, town: true }); }
    return a;
  };
  const endNodes = new Map(); // node id -> [{wid, e}]
  const yWalk = y0 + 0.12;
  for (const e of nc.edges) {
    const na = net.nodes[e.a], nb = net.nodes[e.b];
    const clr = (n) => (n.kind === 'rb' ? n.rbR + 6.5 : n.r + 1);
    const s0 = clr(na) + 1, s1 = e.len - clr(nb) - 1;
    if (s1 - s0 < 4) continue;
    const off = Math.max(e.wL, e.wR) + e.walk * 0.55;
    const n = Math.max(1, Math.round((s1 - s0) / 18));
    const lists = [[], []];
    for (let q = 0; q <= n; q++) {
      const s = s0 + (s1 - s0) * q / n;
      net.at(e, s, tmp);
      for (const [li, side] of [[0, -1], [1, 1]]) {
        const x = tmp[0] - tmp[4] * off * side, z = tmp[2] + tmp[3] * off * side;
        const id = walk.length;
        walk.push({ id, x, z, links: [], town: 'aurelio', y: yWalk });
        cellOf(x, z).nodeIds.push(id);
        const L = lists[li];
        if (L.length) { const p = L[L.length - 1]; walk[p].links.push(id); walk[id].links.push(p); }
        L.push(id);
      }
    }
    // crossings at both ends
    for (const q of [0, lists[0].length - 1]) {
      const a = lists[0][q], b = lists[1][q];
      walk[a].links.push(b); walk[b].links.push(a); walk[a].cross = [b]; walk[b].cross = [a];
    }
    const push = (nid, wid) => { let a = endNodes.get(nid); if (!a) endNodes.set(nid, a = []); a.push({ wid, e }); };
    push(e.a, lists[0][0]); push(e.a, lists[1][0]);
    push(e.b, lists[0][lists[0].length - 1]); push(e.b, lists[1][lists[1].length - 1]);
    // street furniture
    const lampStep = e.type === 'avenue' ? 30 : 38;
    let flip = 1;
    for (let s = s0 + 4; s < s1 - 2; s += lampStep) {
      net.at(e, s, tmp);
      flip = -flip;
      const side = e.type === 'avenue' ? flip : 1;
      const w = Math.max(e.wL, e.wR);
      const lx = tmp[0] - tmp[4] * (w + 0.55) * side, lz = tmp[2] + tmp[3] * (w + 0.55) * side;
      map.props.push({ type: 'streetlight', x: lx, z: lz, rot: Math.atan2(side * tmp[4], -side * tmp[3]), y: yWalk });
      // trees between the lamps on the avenues
      if (e.type === 'avenue') for (const sd of [-1, 1]) {
        net.at(e, s + lampStep / 2, tmp);
        const o = w + e.walk - 1.0;
        map.props.push({ type: rOf(tmp[0], tmp[2]) > 380 ? 'palm' : 'tree', x: tmp[0] - tmp[4] * o * sd, z: tmp[2] + tmp[3] * o * sd, rot: rng.range(0, 6.28), scale: rng.range(0.85, 1.15), y: yWalk, street: true });
      }
    }
    const w = Math.max(e.wL, e.wR);
    const put = (type, s, o, rot = 0) => { net.at(e, s, tmp); const sd = rng.chance(0.5) ? 1 : -1; map.props.push({ type, x: tmp[0] - tmp[4] * (w + o) * sd, z: tmp[2] + tmp[3] * (w + o) * sd, rot: rot === 'face' ? Math.atan2(sd * tmp[4], -sd * tmp[3]) + Math.PI : 0, y: yWalk }); };
    if (rng.chance(0.6)) put('hydrant', rng.range(s0 + 3, s1 - 3), 0.8);
    if (rng.chance(0.55)) put('trashcan', rng.range(s0 + 3, s1 - 3), 1.0);
    if (e.type === 'avenue' && e.len > 90 && rng.chance(0.45)) put('busstop', (s0 + s1) / 2, e.walk - 1.1, 'face');
    if (rng.chance(0.25)) put('bench', rng.range(s0 + 3, s1 - 3), e.walk - 0.8, 'face');
  }
  // corners: join the pavement ends of neighbouring streets round each junction
  for (const [, list] of endNodes) {
    for (const a of list) {
      let best = null, bd = 30;
      for (const b of list) {
        if (b.e === a.e) continue;
        const A = walk[a.wid], Bn = walk[b.wid], d = Math.hypot(A.x - Bn.x, A.z - Bn.z);
        if (d < bd) { bd = d; best = b; }
      }
      if (best && !walk[a.wid].links.includes(best.wid)) { walk[a.wid].links.push(best.wid); walk[best.wid].links.push(a.wid); }
    }
  }
  for (const a of areaCells.values()) { a.nodeIds = a.nodeIds.filter((id) => walk[id].links.length); if (a.nodeIds.length) map.townAreas.push(a); }
  map.landmarks.aurelio = { x: C.x + 30, z: C.z + 32, name: C.name };
  const he = nc.ends.harbor;
  if (he) map.landmarks.aurHarbor = { x: he.x + 14, z: he.z - 60, name: 'Harbor Drive' };
  // palms, lamps and benches down Harbor Drive
  for (const r of [nc.roads?.harborN, nc.roads?.harborS]) for (const e of r?.edges || []) {
    for (let s = 12; s < e.len - 8; s += 24) {
      net.at(e, s, tmp);
      for (const sd of [-1, 1]) {
        const o = Math.max(e.wL, e.wR) + 2.2, x = tmp[0] - tmp[4] * o * sd, z = tmp[2] + tmp[3] * o * sd;
        map.props.push({ type: 'palm', x, z, rot: rng.range(0, 6.28), scale: rng.range(0.95, 1.25), y: hf.sample(x, z) });
      }
      if ((s / 24 | 0) % 2 === 0) { const o = Math.max(e.wL, e.wR) + 1.0, x = tmp[0] + tmp[4] * o, z = tmp[2] - tmp[3] * o; map.props.push({ type: 'streetlight', x, z, rot: Math.atan2(-tmp[4], tmp[3]), y: hf.sample(x, z) }); }
    }
  }
}

// ------------------------------------------------------------------ geometry helpers
function rectsOverlap(a, b) {
  const axes = [];
  for (const r of [a, b]) { const s = Math.sin(r.yaw), c = Math.cos(r.yaw); axes.push([c, -s], [s, c]); }
  const dx = b.cx - a.cx, dz = b.cz - a.cz;
  for (const [ax, az] of axes) {
    const proj = (r) => { const s = Math.sin(r.yaw), c = Math.cos(r.yaw); return Math.abs((c * ax - s * az)) * r.hx + Math.abs((s * ax + c * az)) * r.hz; };
    if (Math.abs(dx * ax + dz * az) > proj(a) + proj(b)) return false;
  }
  return true;
}
function rectCircle(r, x, z, rad) {
  const s = Math.sin(r.yaw), c = Math.cos(r.yaw), dx = x - r.cx, dz = z - r.cz;
  const lx = dx * c - dz * s, lz = dx * s + dz * c;
  const qx = clamp(lx, -r.hx, r.hx), qz = clamp(lz, -r.hz, r.hz);
  return Math.hypot(lx - qx, lz - qz) < rad;
}

// the regional roads that tie San Aurelio and its towns into the network (control points; also carved into
// the terrain beforehand)
export function ncRoutes() {
  const T = TOWNS;
  const se = ncSpokeEnd('bay'), sw = ncSpokeEnd('ridge'), w = ncSpokeEnd('timber'), e = ncSpokeEnd('harbor');
  return {
    aurelio: { type: 'highway', ends: [null, C.y], ctrl: [[905, -1905], [coastX(-2120) - 330, -2120], [coastX(-2440) - 305, -2440], [T.gull.x, T.gull.z], [coastX(-3060) - 300, -3060], [coastX(-3360) - 330, -3360], [se[0] + 60, se[1] + 45], se] },
    ridgeRd: { type: 'road', ends: [null, C.y], ctrl: [[300, -2335], [262, -2560], [160, -2830], [T.ridge.x, T.ridge.z], [-10, -3420], [60, -3620], sw] },
    timberRd: { type: 'road', ends: [C.y, 'timber'], ctrl: [w, [w[0] - 90, w[1] - 30], [-330, -4190], [-490, -4300], [T.timber.x, T.timber.z]] },
    harborDr: { type: 'road', ends: [null, null], ctrl: [[e[0] + 20, e[1] - 380], [e[0] + 30, e[1] - 200], [e[0] + 36, e[1]], [e[0] + 20, e[1] + 200], [e[0] - 20, e[1] + 360]] },
  };
}
