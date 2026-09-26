// The actual road network of the world: the Los Soles street grid (with a few roundabouts and
// merged super-blocks), the elevated Sol Freeway through the city with diamond interchanges, rural
// highways, winding hill roads, town streets with roundabouts, dirt tracks and the base access road.
import { RoadNet, RT, catmull, cumLen, pointAt, tangentAt, offsetLine, project, intersect, solveProfile, resample } from './roadnet.js';
import { TOWNS, BASE, AIRFIELD, coastZ, fbmN } from './worldgen.js';
import { buildRailway } from './railway.js';
import { clamp, lerp, smoothstep } from '../core/utils.js';

// grid segments removed to form super-blocks: 'h:i,j' = E-W street ZS[j] between XS[i] and XS[i+1];
// 'v:i,j' = N-S street XS[i] between ZS[j] and ZS[j+1]
export const REMOVED_SEGMENTS = new Set(['h:12,10', 'v:7,3', 'v:3,0', 'v:6,6']);
export const SUPERBLOCKS = [
  { key: 'h:12,10', cells: [[12, 9], [12, 10]], kind: 'stadium', name: 'Los Soles Coliseum' },
  { key: 'v:7,3', cells: [[6, 3], [7, 3]], kind: 'mall', name: 'Market Street Mall' },
  { key: 'v:3,0', cells: [[2, 0], [3, 0]], kind: 'golf', name: 'Vistawood Country Club' },
  { key: 'v:6,6', cells: [[5, 6], [6, 6]], kind: 'park', name: 'Glen Park' },
];
export const CITY_ROUNDABOUTS = [[3, 2], [15, 3], [6, 11], [11, 12], [16, 5], [13, 9]];
export const FW = { y: 9, carriage: 6.7, adj: 15.4 };

// Planned routes (control points). Used to build the roads and, beforehand, to carve valleys and
// passes into the terrain so the roads can follow the land instead of sitting in trenches.
export const ROUTES = (() => {
  const T = TOWNS;
  const cityW = [-830, 540], cityN = [15, -770], cityE = [870, -455];
  const jDw = [-3700, -2480];
  return {
    coast: { type: 'highway', ends: [0, 'pine'], ctrl: [[cityW[0] - 10, cityW[1]], [-950, 545], [-1100, 560], [-1250, 520], [-1360, 400], [-1400, 220], [-1380, 60], [-1350, -60], [-1330, -200], [-1300, -450], [-1290, -700], [-1330, -950], [-1420, -1200], [-1520, -1450], [-1560, -1640], [-1380, -1700], [T.mirador.x, T.mirador.z], [-1180, -1980], [-1150, -2100], [-950, -2250], [-650, -2330], [T.pine.x - 25, T.pine.z]] },
    fernN: { type: 'road', ends: ['fern', null], ctrl: [[T.fern.x, T.fern.z - 25], [-2525, -400], [-2520, -560], [-2540, -700], [-2600, -900], [-2680, -1100]] },
    vista: { type: 'road', ends: [0, 'pine'], grade: 0.1, ctrl: [[cityN[0], cityN[1] - 10], [30, -830], [70, -900], [95, -1000], [40, -1120], [-80, -1200], [-60, -1320], [80, -1430], [45, -1560], [-110, -1700], [-200, -1900], [-260, -2100], [T.pine.x, T.pine.z + 23]] },
    bay: { type: 'highway', ends: [0, 'pine'], ctrl: [[cityE[0] + 10, cityE[1]], [950, -520], [1000, -700], [T.hale.x, T.hale.z], [990, -1250], [950, -1550], [880, -1850], [720, -2100], [450, -2280], [150, -2345], [T.pine.x + 23, T.pine.z]] },
    freeway: { type: 'freeway', ends: ['dry', 0], grade: 0.04, width: 30, ctrl: [jDw, [-3665, -2330], [-3560, -1900], [-3380, -1480], [-3120, -1080], [-2830, -770], [-2520, -560], [-2300, -400], [-2100, -230], [-1860, -150], [-1600, -100], [-1350, -60], [-1180, -25], [-1000, 4], [-880, 10], [-840, 10], [-600, 10], [-300, 10], [-100, 10], [15, 10]] },
    fernS: { type: 'road', ends: ['fern', null], ctrl: [[T.fern.x, T.fern.z + 25], [-2530, -60], [-2560, 150], [-2590, 380], [-2620, coastZ(-2620) - 60]] },
    fernW: { type: 'road', ends: ['fern', null], ctrl: [[T.fern.x - 25, T.fern.z], [-2700, -230], [-2850, -150], [-2955, 40], [AIRFIELD.x - 40, AIRFIELD.z - 55]] },
    fernE: { type: 'road', ends: ['fern', null], ctrl: [[T.fern.x + 25, T.fern.z], [-2380, -180], [-2230, -60], [-2090, 40], [-1900, 180], [-1700, 240], [-1500, 262], [-1398, 250]] },
    dryE: { type: 'road', ends: ['dry', null], ctrl: [[T.dry.x + 25, T.dry.z], [-3780, -2480], jDw] },
    dryE2: { type: 'road', ends: [null, null], ctrl: [jDw, [-3550, -2470], [-3350, -2500], [-3150, -2560]] },
    dryW: { type: 'road', ends: ['dry', null], ctrl: [[T.dry.x - 25, T.dry.z], [-3980, -2470], [-4200, -2450], [-4500, -2380], [-4800, -2300]] },
    costa: { type: 'road', ends: [null, 'seco'], ctrl: [[AIRFIELD.x - 40, AIRFIELD.z - 55], [-3150, 320], [-3400, 390], [-3700, 440], [T.seco.x + 22, T.seco.z]] },
    seco: { type: 'highway', ends: ['seco', null], ctrl: [[T.seco.x, T.seco.z - 22], [-4020, 200], [-4100, -300], [-4180, -900], [-4150, -1450], [-4080, -1900], [-4150, -2250], [-4200, -2445]] },
    baseRd: { type: 'highway', ends: ['dry', 'base'], ctrl: [[T.dry.x, T.dry.z + 25], [-3875, -2650], [-3900, -2900], [-3960, -3250], [-3930, -3600], [-3800, -3860], [-3700, -3960], [-3720, BASE.gateZ], [BASE.maxX - 2, BASE.gateZ]] },
  };
})();

export function buildRoadNetwork(C, hf) {
  const { XS, ZS, intersectionPhase } = C;
  const net = new RoadNet();
  const terrain = (x, z) => hf.sample(x, z);
  const avgTerrain = (x, z, r = 14) => { let s = 0, n = 0; for (let a = 0; a < 8; a++) { s += terrain(x + Math.cos(a * 0.785) * r, z + Math.sin(a * 0.785) * r); n++; } return (s / n + terrain(x, z)) / 2; };
  const info = { interchanges: [], towns: {}, rbs: [], cityRb: new Set(CITY_ROUNDABOUTS.map(([i, j]) => i + ',' + j)), labels: [] };

  // ------------------------------------------------------------------ city grid
  const G = [];
  for (let i = 0; i < XS.length; i++) {
    G.push([]);
    for (let j = 0; j < ZS.length; j++) {
      const rb = info.cityRb.has(i + ',' + j);
      G[i].push(net.addNode(XS[i], ZS[j], 0, { kind: rb ? 'rb' : 'x', r: 10, rbR: 7.6, sig: rb ? null : intersectionPhase(i, j), grid: [i, j] }));
    }
  }
  const gridEdge = (a, b, grid) => net.addEdge(a, b, [[a.x, a.z, 0], [b.x, b.z, 0]], 'street', { render: false, grid, city: true });
  // N-S streets with ramp junctions spliced in
  const splices = {
    '4,7': [{ z: -10, name: 'WB ramps' }, { z: 30, name: 'EB ramps' }],
    '9,7': [{ z: 10, name: 'Sol Freeway' }],
  };
  const spliceNodes = {};
  for (let i = 0; i < XS.length; i++) for (let j = 0; j < ZS.length - 1; j++) {
    if (REMOVED_SEGMENTS.has(`v:${i},${j}`)) continue;
    const sp = splices[`${i},${j}`];
    if (!sp) { gridEdge(G[i][j], G[i][j + 1], { i, j, di: 0, dj: 1 }); continue; }
    let prev = G[i][j];
    for (const s of sp) {
      const n = net.addNode(XS[i], s.z, 0, { kind: 'x', r: 10, sig: intersectionPhase(i + 20, j + 7), name: s.name });
      n.city = true;
      spliceNodes[`${i},${s.z}`] = n;
      gridEdge(prev, n, { i, j, di: 0, dj: 1 });
      prev = n;
    }
    gridEdge(prev, G[i][j + 1], { i, j, di: 0, dj: 1 });
  }
  for (let j = 0; j < ZS.length; j++) for (let i = 0; i < XS.length - 1; i++) {
    if (REMOVED_SEGMENTS.has(`h:${i},${j}`)) continue;
    gridEdge(G[i][j], G[i + 1][j], { i, j, di: 1, dj: 0 });
  }

  // ------------------------------------------------------------------ generic road builder
  const mk = (x, z, opts = {}) => net.addNode(x, z, opts.y ?? avgTerrain(x, z), opts);
  function road(ctrl, type, o = {}) {
    let pts = catmull(ctrl, o.spacing ?? 6);
    const T = RT[type];
    const prof = solveProfile(pts, terrain, {
      y0: o.start ? o.start.y : o.y0, y1: o.end ? o.end.y : o.y1, window: o.window ?? (type === 'dirt' ? 50 : type === 'road' ? 80 : 160),
      maxGrade: o.maxGrade ?? (type === 'dirt' ? 0.14 : type === 'road' ? 0.1 : 0.075), maxCut: o.maxCut ?? (type === 'dirt' ? 4 : 12), pins: o.pins, fixed: o.fixed, minY: o.minY,
    });
    const cum = prof.cum;
    pts = pts.map((p, i) => [p[0], p[1], prof.y[i]]);
    // split stations
    const stops = [{ s: 0, node: o.start || null }, { s: cum[cum.length - 1], node: o.end || null }];
    for (const sp of o.splits || []) {
      const pr = project(pts, cum, sp.x, sp.z);
      const s = sp.s ?? pr.s;
      const k = nearestIdx(cum, s);
      const node = sp.node || net.addNode(pts[k][0], pts[k][1], pts[k][2], { kind: sp.kind || 'x', r: sp.r ?? 8, rbR: sp.rbR ?? 0, name: sp.name, sig: sp.sig ?? null });
      if (sp.node) { /* existing node keeps its own position */ }
      stops.push({ s: cum[k], node, k });
    }
    stops.sort((a, b) => a.s - b.s);
    if (!stops[0].node) stops[0].node = net.addNode(pts[0][0], pts[0][1], pts[0][2], { kind: 'end', r: 0 });
    const last = stops[stops.length - 1];
    if (!last.node) last.node = net.addNode(pts[pts.length - 1][0], pts[pts.length - 1][1], pts[pts.length - 1][2], { kind: 'end', r: 0 });
    const edges = [];
    for (let q = 0; q < stops.length - 1; q++) {
      const k0 = stops[q].k ?? nearestIdx(cum, stops[q].s), k1 = stops[q + 1].k ?? nearestIdx(cum, stops[q + 1].s);
      if (k1 <= k0) continue;
      const seg = pts.slice(k0, k1 + 1);
      edges.push(net.addEdge(stops[q].node, stops[q + 1].node, seg, type, { name: o.name, ...o.edgeOpts }));
    }
    return { pts, cum, prof, edges, nodes: stops.map((s) => s.node) };
  }

  // ------------------------------------------------------------------ key junctions
  const rbFern = mk(TOWNS.fern.x, TOWNS.fern.z, { kind: 'rb', rbR: 17, r: 25, name: 'Fern Creek' });
  const rbDry = mk(TOWNS.dry.x, TOWNS.dry.z, { kind: 'rb', rbR: 17, r: 25, name: 'Dry Wells' });
  const rbPine = mk(TOWNS.pine.x, TOWNS.pine.z, { kind: 'rb', rbR: 15, r: 23, name: 'Pine Hollow' });
  info.rbs.push(rbFern, rbDry, rbPine);
  const jDw = mk(-3700, -2480, { kind: 'x', r: 12, name: 'Sol Freeway west end' });

  // ------------------------------------------------------------------ crossroads (built before the freeway)
  const cityW = G[0][13], cityN = G[9][0], cityE = G[18][3];
  const R = ROUTES;
  const coast = road(R.coast.ctrl, 'highway', { start: cityW, end: rbPine, name: 'Coast Highway', splits: [{ x: -1398, z: 250, name: 'Farm Road jct', r: 9 }, { x: TOWNS.mirador.x, z: TOWNS.mirador.z, kind: 'rb', rbR: 15, r: 23, name: 'Mirador' }] });
  const fernN = road(R.fernN.ctrl, 'road', { start: rbFern, name: 'Main Street' });
  const vista = road(R.vista.ctrl, 'road', { start: cityN, end: rbPine, name: 'Vistawood Drive', maxGrade: 0.11 });
  const bay = road(R.bay.ctrl, 'highway', { start: cityE, end: rbPine, name: 'Bayshore Road', splits: [{ x: TOWNS.hale.x, z: TOWNS.hale.z, kind: 'rb', rbR: 15, r: 23, name: 'Port Hale' }] });

  // ------------------------------------------------------------------ freeway centerline + profile
  const fwCtrl = R.freeway.ctrl;
  const fwC = resample(catmull(fwCtrl, 4), 6);
  const fwCum = cumLen(fwC);
  const crossCoast = intersect(fwC, fwCum, coast.pts, coast.cum);
  const crossFern = intersect(fwC, fwCum, fernN.pts, fernN.cum);
  const yAt = (r, s) => { const p = pointAt(r.pts, r.cum, s); return p[2]; };
  const cityProfile = (x) => {
    if (x <= -200) return FW.y;
    if (x <= -75) return lerp(FW.y, 6.6, smoothstep(-200, -75, x));
    if (x <= 2) return lerp(6.6, 0.16, (x + 75) / 77);
    return 0.12;
  };
  const fwProf = solveProfile(fwC, terrain, {
    window: 260, maxGrade: 0.045, maxCut: 18, y0: jDw.y,
    fixed: (x) => (x > -842 ? cityProfile(x) : null),
    pins: [crossCoast && { s: crossCoast.sa, y: yAt(coast, crossCoast.sb) + 8.6 }, crossFern && { s: crossFern.sa, y: yAt(fernN, crossFern.sb) + 8.6 }].filter(Boolean),
  });
  const fwY = fwProf.y;
  const fwAt = (s) => { const p = pointAt(fwC, fwCum, s); const k = nearestIdx(fwCum, s); const [tx, tz] = tangentAt(fwC, k); return { x: p[0], z: p[1], y: interp(fwCum, fwY, s), tx, tz }; };
  const frame = (s, lat) => { const f = fwAt(s); return [f.x - f.tz * lat, f.z + f.tx * lat]; }; // right-positive lateral
  const sOfX = (x) => project(fwC, fwCum, x, 10).s;

  // stations of splits/merges on each carriageway
  const EB = [], WB = []; // {s, kind, key}
  const addIc = (sc, name) => {
    EB.push({ s: sc - 380, kind: 'split', key: name + ':EBoff' }, { s: sc + 380, kind: 'merge', key: name + ':EBon' });
    WB.push({ s: sc + 380, kind: 'split', key: name + ':WBoff' }, { s: sc - 380, kind: 'merge', key: name + ':WBon' });
  };
  if (crossFern) addIc(crossFern.sa, 'fern');
  if (crossCoast) addIc(crossCoast.sa, 'coast');
  EB.push({ s: sOfX(-620), kind: 'split', key: 'city:EBoff' }, { s: sOfX(-190), kind: 'merge', key: 'city:EBon' });
  WB.push({ s: sOfX(-260), kind: 'split', key: 'city:WBoff' }, { s: sOfX(-690), kind: 'merge', key: 'city:WBon' });

  // carriageways (EB runs west->east on the right of the centerline, WB east->west on the left)
  const jDt = spliceNodes['9,10'];
  const cw = (side) => fwC.map((p, i) => { const [tx, tz] = tangentAt(fwC, i); const o = side * FW.carriage; return [p[0] - tz * o, p[1] + tx * o, fwY[i]]; });
  const ebPts = cw(1), wbPts = cw(-1).reverse();
  const L = fwCum[fwCum.length - 1];
  const wbCum = fwCum.map((c) => L - c).reverse();
  const stationNodes = {};
  const buildCarriage = (pts, cumArr, stations, start, end, isWB) => {
    const st = stations.map((q) => ({ ...q, s: isWB ? L - q.s : q.s })).sort((a, b) => a.s - b.s);
    let prevNode = start, prevK = 0;
    const edges = [];
    for (const q of st) {
      const k = nearestIdx(cumArr, q.s);
      const n = net.addNode(pts[k][0], pts[k][1], pts[k][2], { kind: q.kind, r: 0, name: 'Sol Freeway' });
      stationNodes[q.key] = { node: n, k, pts, cum: cumArr };
      edges.push(net.addEdge(prevNode, n, pts.slice(prevK, k + 1), 'freeway', { name: 'Sol Freeway', barrierL: true, barrierR: true, city: pts[prevK][0] > -842 }));
      prevNode = n; prevK = k;
    }
    edges.push(net.addEdge(prevNode, end, pts.slice(prevK), 'freeway', { name: 'Sol Freeway', barrierL: true, barrierR: true }));
    return edges;
  };
  info.freewayEB = buildCarriage(ebPts, fwCum, EB, jDw, jDt, false);
  info.freewayWB = buildCarriage(wbPts, wbCum, WB, jDt, jDw, true);
  info.freewayCenter = { pts: fwC, cum: fwCum, y: fwY };
  // no outer barrier where ramps run alongside: traffic merges over the last stretch before an on-ramp's node
  // and peels off over the first stretch after an off-ramp's node (the ramps' lane paths are trimmed to match)
  for (const key in stationNodes) {
    const { node } = stationNodes[key];
    for (const eid of node.e) {
      const e = net.edges[eid];
      if (e.type !== 'freeway') continue;
      if (e.a === node.id && key.includes('off')) e.noBarrierA = 75;
      if (e.b === node.id && key.includes('on')) e.noBarrierB = 75;
      if (e.b === node.id && key.includes('off')) e.noBarrierB = 5;
      if (e.a === node.id && key.includes('on')) e.noBarrierA = 5;
    }
  }

  // ------------------------------------------------------------------ ramps
  // a ramp runs alongside the carriageway (adjacent) from its split / merge station, then curves off
  const rampEdge = (fromNode, toNode, ctrl, profileFn, name, opts = {}) => {
    const pts = resample(catmull(ctrl, 3), 4);
    const cum = cumLen(pts);
    const y = pts.map((p, i) => profileFn(p[0], p[1], cum[i], cum[cum.length - 1]));
    const e = net.addEdge(fromNode, toNode, pts.map((p, i) => [p[0], p[1], y[i]]), 'ramp', { name, barrierL: true, barrierR: true, ...opts });
    return e;
  };
  const rampY = (fromY, toY, adjLen, stationY) => (x, z, s, len) => {
    // follow the freeway while alongside, then ease to the junction height
    const fy = stationY(x, z);
    if (s < adjLen) return fy;
    const t = smoothstep(adjLen, len, s);
    return lerp(fy, toY, t);
  };
  const fwYnear = (x, z) => interp(fwCum, fwY, project(fwC, fwCum, x, z).s);
  const diamond = (sc, crossRoad, crossName, jOffset = 70) => {
    // junctions on the crossroad either side of the freeway
    const cr = crossRoad;
    const c = intersect(fwC, fwCum, cr.pts, cr.cum);
    const fA = fwAt(c.sa);
    const p1 = pointAt(cr.pts, cr.cum, c.sb - jOffset), p2 = pointAt(cr.pts, cr.cum, c.sb + jOffset);
    // which of the two is on the EB (right) side?
    const side = (p) => (p[0] - fA.x) * -fA.tz + (p[1] - fA.z) * fA.tx; // >0: right
    const jE = side(p1) > 0 ? p1 : p2, jW = jE === p1 ? p2 : p1;
    return { c, jE, jW, fA };
  };
  const icRamps = (sc, jE, jW, name) => {
    const ebOff = stationNodes[name + ':EBoff'], ebOn = stationNodes[name + ':EBon'], wbOff = stationNodes[name + ':WBoff'], wbOn = stationNodes[name + ':WBon'];
    const a = FW.adj;
    const off = (st, j, sgn, nm) => {
      const s0 = st.node === ebOff?.node ? sc - 380 : sc + 380;
      const dir = sgn > 0 ? 1 : -1; // EB travels +s
      const ctrl = [frame(s0, sgn * a), frame(s0 + dir * 60, sgn * a), frame(s0 + dir * 170, sgn * (a + 14)), frame(sc - dir * 110, sgn * (a + 34)), [j.x, j.z]];
      return rampEdge(st.node, j.node, ctrl, rampY(st.node.y, j.node.y, 60, fwYnear), nm, { trimA: 55 });
    };
    const on = (st, j, sgn, nm) => {
      const s1 = st.node === ebOn?.node ? sc + 380 : sc - 380;
      const dir = sgn > 0 ? 1 : -1;
      const ctrl = [[j.x, j.z], frame(sc + dir * 110, sgn * (a + 34)), frame(s1 - dir * 170, sgn * (a + 14)), frame(s1 - dir * 60, sgn * a), frame(s1, sgn * a)];
      const pts = resample(catmull(ctrl, 3), 4);
      const cum = cumLen(pts);
      const len = cum[cum.length - 1];
      return net.addEdge(j.node, st.node, pts.map((p, i) => [p[0], p[1], cum[i] > len - 60 ? fwYnear(p[0], p[1]) : lerp(j.node.y, fwYnear(p[0], p[1]), smoothstep(0, len - 60, cum[i]))]), 'ramp', { name: nm, barrierL: true, barrierR: true, trimB: 55 });
    };
    const out = [];
    if (ebOff) out.push(off(ebOff, jE, 1, name + ' exit'));
    if (ebOn) out.push(on(ebOn, jE, 1, name + ' on-ramp'));
    if (wbOff) out.push(off(wbOff, jW, -1, name + ' exit'));
    if (wbOn) out.push(on(wbOn, jW, -1, name + ' on-ramp'));
    return out;
  };

  if (crossCoast) info.coastIc = diamond(crossCoast.sa, coast, 'Coast Highway');
  if (crossFern) info.fernIc = diamond(crossFern.sa, fernN, 'Main Street');
  // (simpler: split the existing edges at the junction points in place)
  const splitAtPoint = (r, p, name) => {
    const pr = project(r.pts, r.cum, p[0], p[1]);
    const k = nearestIdx(r.cum, pr.s);
    const pt = r.pts[k];
    // find the edge containing this station
    for (const e of r.edges) {
      const a = net.nodes[e.a], b = net.nodes[e.b];
      const sa = project(r.pts, r.cum, e.p[0], e.p[2]).s, sb = project(r.pts, r.cum, e.p[(e.n - 1) * 3], e.p[(e.n - 1) * 3 + 2]).s;
      if (pr.s <= sa || pr.s >= sb) continue;
      const n = net.addNode(pt[0], pt[1], pt[2], { kind: 'x', r: 9, name });
      const local = [];
      for (let i = 0; i < e.n; i++) local.push([e.p[i * 3], e.p[i * 3 + 2], e.p[i * 3 + 1]]);
      const lc = cumLen(local);
      const kk = nearestIdx(lc, project(local, lc, pt[0], pt[1]).s);
      removeEdge(net, e);
      const e1 = net.addEdge(a, n, local.slice(0, kk + 1), e.type, { name: e.name });
      const e2 = net.addEdge(n, b, local.slice(kk), e.type, { name: e.name });
      r.edges.splice(r.edges.indexOf(e), 1, e1, e2);
      return n;
    }
    return null;
  };
  const addIcRamps = (d, r, sc, name) => {
    const nE = splitAtPoint(r, d.jE, name + ' ramps');
    const nW = splitAtPoint(r, d.jW, name + ' ramps');
    if (!nE || !nW) return;
    info.interchanges.push({ name, x: d.c.x, z: d.c.z });
    icRamps(sc, { x: nE.x, z: nE.z, node: nE }, { x: nW.x, z: nW.z, node: nW }, name);
  };
  if (info.coastIc) addIcRamps(info.coastIc, coast, crossCoast.sa, 'coast');
  if (info.fernIc) addIcRamps(info.fernIc, fernN, crossFern.sa, 'fern');

  // city diamond at XS[4] (Rosewood): ramps land on the street at z = 30 (EB) and z = -10 (WB)
  {
    const jEB = spliceNodes['4,30'], jWB = spliceNodes['4,-10'];
    const kf = (keys) => (x) => { // piecewise linear by x
      if (x <= keys[0][0]) return keys[0][1];
      for (let i = 0; i < keys.length - 1; i++) if (x <= keys[i + 1][0]) return lerp(keys[i][1], keys[i + 1][1], (x - keys[i][0]) / (keys[i + 1][0] - keys[i][0]));
      return keys[keys.length - 1][1];
    };
    const n = stationNodes;
    const mkR = (from, to, ctrl, keys, nm, o) => {
      const pts = resample(catmull(ctrl, 3), 4);
      const f = kf(keys);
      return net.addEdge(from, to, pts.map((p) => [p[0], p[1], f(p[0])]), 'ramp', { name: nm, barrierL: true, barrierR: true, city: true, ...o });
    };
    const y9 = FW.y;
    mkR(n['city:EBoff'].node, jEB, [[-620, 10 + FW.adj], [-560, 10 + FW.adj], [-520, 27], [-480, 29.6], [-455, 30], [-440, 30]], [[-620, y9], [-560, y9 - 0.1], [-535, 7.4], [-458, 0.16], [-440, 0.12]], 'Rosewood exit', { trimA: 55 });
    mkR(jEB, n['city:EBon'].node, [[-440, 30], [-425, 30], [-400, 29.6], [-355, 27.6], [-300, 25.8], [-250, 10 + FW.adj], [-190, 10 + FW.adj]], [[-440, 0.12], [-425, 0.16], [-355, 7.0], [-320, 8.4], [-260, y9], [-190, cityProfile(-190)]], 'Rosewood on-ramp', { trimB: 55 });
    mkR(n['city:WBoff'].node, jWB, [[-260, 10 - FW.adj], [-320, 10 - FW.adj], [-360, -7], [-400, -9.6], [-425, -10], [-440, -10]], [[-440, 0.12], [-425, 0.16], [-345, 7.3], [-320, y9], [-260, y9]], 'Rosewood exit', { trimA: 55 });
    mkR(jWB, n['city:WBon'].node, [[-440, -10], [-455, -10], [-480, -9.6], [-525, -7.6], [-580, -5.8], [-630, 10 - FW.adj], [-690, 10 - FW.adj]], [[-690, y9], [-620, y9], [-560, 8.6], [-525, 7.0], [-455, 0.16], [-440, 0.12]], 'Rosewood on-ramp', { trimB: 55 });
    info.interchanges.push({ name: 'Rosewood', x: -440, z: 10 });
  }

  // ------------------------------------------------------------------ Fern Creek
  const fernJ = net.nodes.find((q) => q.name === 'Farm Road jct');
  const fernS = road(R.fernS.ctrl, 'road', { start: rbFern, name: 'Main Street' });
  const fernW = road(R.fernW.ctrl, 'road', { start: rbFern, name: 'Airfield Road' });
  const fernE = road([...R.fernE.ctrl.slice(0, -1), [fernJ.x, fernJ.z]], 'road', { start: rbFern, end: fernJ, name: 'Farm Road' });
  info.towns.fern = { center: rbFern, roads: [fernN, fernS, fernW, fernE] };
  // side streets in town
  const fs1 = road([[-2525, -330], [-2620, -330], [-2700, -350]], 'road', { splits: [], start: null, name: 'Oak Street' });
  const fs2 = road([[-2530, -120], [-2440, -110], [-2360, -130]], 'road', { name: 'Elm Street' });
  const fs3 = road([[-2545, 40], [-2650, 60], [-2720, 20]], 'road', { name: 'Mill Lane' });
  // connect side streets to Main Street with junction splits
  const joinTo = (r, sideRoad, atStart = true) => {
    const p = atStart ? sideRoad.pts[0] : sideRoad.pts[sideRoad.pts.length - 1];
    const n = splitAtPoint(r, p, 'jct');
    if (!n) return;
    const e = atStart ? sideRoad.edges[0] : sideRoad.edges[sideRoad.edges.length - 1];
    const endNode = atStart ? net.nodes[e.a] : net.nodes[e.b];
    retarget(net, e, endNode, n);
  };
  joinTo(fernN, fs1); joinTo(fernS, fs2); joinTo(fernS, fs3);
  info.towns.fern.roads.push(fs1, fs2, fs3);
  // farm tracks
  const farm1 = road([[-2680, -1100], [-2900, -1180], [-3100, -1100]], 'dirt', { start: net.nodes[fernN.edges[fernN.edges.length - 1].b], name: 'Farm Track' });
  const farm2 = road([[-2230, -60], [-2250, -380], [-2180, -700], [-2100, -1000]], 'dirt', { name: 'River Track' });
  joinTo(fernE, farm2);

  // ------------------------------------------------------------------ Dry Wells + base road
  const dryE = road(R.dryE.ctrl, 'road', { start: rbDry, end: jDw, name: 'Main Street' });
  const dryE2 = road(R.dryE2.ctrl, 'road', { start: jDw, name: 'Desert Road' });
  const dryW = road(R.dryW.ctrl, 'road', { start: rbDry, name: 'Main Street' });
  const baseGate = mk(BASE.maxX - 2, BASE.gateZ, { kind: 'x', r: 8, name: 'Fort Carver gate' });
  const baseRd = road(R.baseRd.ctrl, 'highway', { start: rbDry, end: baseGate, name: 'Carver Road' });
  const dryN = road([[rbDry.x, rbDry.z - 25], [-3860, -2330], [-3780, -2150], [-3600, -2050]], 'dirt', { start: rbDry, name: 'Mesa Track' });
  const dryS1 = road([[-3860, -2600], [-3960, -2610], [-4050, -2580]], 'road', { name: 'Adobe Street' });
  joinTo(baseRd, dryS1);
  info.towns.dry = { center: rbDry, roads: [dryE, dryW, baseRd, dryN, dryS1] };
  const mesaTrack = road([[-4500, -2380], [-4520, -2650], [-4580, -2900], [-4480, -3180]], 'dirt', { name: 'Mesa Track', maxGrade: 0.3 });
  joinTo(dryW, mesaTrack);

  // ------------------------------------------------------------------ Pine Hollow
  const pineN = road([[rbPine.x, rbPine.z - 23], [-320, -2440], [-400, -2520], [-520, -2560]], 'dirt', { start: rbPine, name: 'Logging Track', maxGrade: 0.25, window: 30, maxCut: 3 });
  const pineS1 = road([[-300, -2250], [-200, -2260], [-110, -2240]], 'road', { name: 'Cedar Lane' });
  joinTo(vista, pineS1, true);
  info.towns.pine = { center: rbPine, roads: [pineN, pineS1, coast, vista, bay] };

  // ------------------------------------------------------------------ Mirador (lake town on the Coast Highway)
  const rbMir = net.nodes.find((q) => q.name === 'Mirador' && q.kind === 'rb');
  if (rbMir) {
    info.rbs.push(rbMir);
    const lakeDr = road([[rbMir.x - 18, rbMir.z + 12], [-1300, -1760], [-1330, -1830], [-1320, -1900], [-1280, -1960]], 'road', { start: rbMir, name: 'Lakeshore Drive' });
    const summit = road([[rbMir.x + 20, rbMir.z - 4], [-1150, -1805], [-1070, -1830], [-990, -1840]], 'road', { start: rbMir, name: 'Summit Road', maxGrade: 0.13 });
    const pier = road([[-1310, -1880], [-1250, -1880], [-1215, -1905]], 'road', { name: 'Marina Way' });
    joinTo(lakeDr, pier, true);
    info.towns.mirador = { center: rbMir, roads: [lakeDr, summit, pier] };
  }

  // ------------------------------------------------------------------ Port Hale (harbour on Bayshore Road)
  const rbHale = net.nodes.find((q) => q.name === 'Port Hale' && q.kind === 'rb');
  if (rbHale) {
    info.rbs.push(rbHale);
    const harbor = road([[rbHale.x + 20, rbHale.z], [1040, -950], [1062, -945]], 'road', { start: rbHale, name: 'Harbor Road' });
    const cliff = road([[rbHale.x - 20, rbHale.z], [950, -975], [900, -1000], [860, -1040]], 'road', { start: rbHale, name: 'Cliff Street', maxGrade: 0.14 });
    const quay = road([[1020, -1060], [1035, -1000], [1040, -950]], 'road', { name: 'Quay Street' });
    joinTo(bay, quay, true);
    info.towns.hale = { center: rbHale, roads: [harbor, cliff, quay, bay] };
  }

  // ------------------------------------------------------------------ Puerto Seco (desert bluff town on the south coast)
  const rbSeco = mk(TOWNS.seco.x, TOWNS.seco.z, { kind: 'rb', rbR: 16, r: 24, name: 'Puerto Seco' });
  info.rbs.push(rbSeco);
  {
    const airEnd = net.nodes[fernW.edges[fernW.edges.length - 1].b];
    const costa = road(R.costa.ctrl, 'road', { start: airEnd, end: rbSeco, name: 'Costa Road' });
    const seco = road(R.seco.ctrl, 'highway', { start: rbSeco, name: 'Seco Highway' });
    joinTo(dryW, seco, false);
    const mayor = road([[rbSeco.x, rbSeco.z + 22], [-3995, 560], [-3990, 650]], 'road', { start: rbSeco, name: 'Calle Mayor' });
    const sol = road([[rbSeco.x - 22, rbSeco.z], [-4110, 480], [-4220, 505]], 'road', { start: rbSeco, name: 'Calle del Sol' });
    const mar = road([[-4110, 480], [-4120, 580], [-4100, 660]], 'road', { name: 'Calle del Mar' });
    joinTo(sol, mar, true);
    info.towns.seco = { center: rbSeco, roads: [costa, seco, mayor, sol, mar] };
  }

  // ------------------------------------------------------------------ Sol Line railway
  buildRailway(net, info, fwC, fwCum, terrain, hf);

  // ------------------------------------------------------------------ base interior roads
  const bY = hf.sample((BASE.minX + BASE.maxX) / 2, (BASE.minZ + BASE.maxZ) / 2);
  const baseMain = road([[baseGate.x, baseGate.z], [-4000, BASE.gateZ], [-4300, BASE.gateZ], [-4700, BASE.gateZ], [-5100, BASE.gateZ]], 'road', { start: baseGate, name: 'Fort Carver', edgeOpts: { base: true } });
  info.base = { gate: baseGate, road: baseMain, y: bY };

  // ------------------------------------------------------------------ node clearances from the widest road
  for (const n of net.nodes) {
    if (n.grid || n.city) continue;
    if (n.kind === 'x') { let w = 6; for (const eid of n.e) { const e = net.edges[eid]; w = Math.max(w, e.wL, e.wR); } n.r = Math.max(n.r, w + 3); }
  }
  // traffic priority: a node's major class
  for (const n of net.nodes) { let c = 0; for (const eid of n.e) c = Math.max(c, net.edges[eid].T.cls); n.maxCls = c; }
  return { net, info };
}

// ------------------------------------------------------------------ helpers
function nearestIdx(cum, s) {
  let lo = 0, hi = cum.length - 1;
  while (hi - lo > 1) { const m = (lo + hi) >> 1; if (cum[m] <= s) lo = m; else hi = m; }
  return s - cum[lo] < cum[hi] - s ? lo : hi;
}
function interp(cum, arr, s) {
  if (s <= 0) return arr[0];
  if (s >= cum[cum.length - 1]) return arr[arr.length - 1];
  let lo = 0, hi = cum.length - 1;
  while (hi - lo > 1) { const m = (lo + hi) >> 1; if (cum[m] <= s) lo = m; else hi = m; }
  const t = (s - cum[lo]) / (cum[hi] - cum[lo] || 1);
  return arr[lo] + (arr[hi] - arr[lo]) * t;
}
export function removeEdge(net, e) {
  e.removed = true;
  for (const nid of [e.a, e.b]) { const n = net.nodes[nid]; const i = n.e.indexOf(e.id); if (i >= 0) n.e.splice(i, 1); }
  for (const arr of net.grid.values()) for (let i = arr.length - 1; i >= 0; i--) if (arr[i].e === e) arr.splice(i, 1);
}
// move one end of an edge from node `from` to node `to` (used to hook side streets onto a new junction)
function retarget(net, e, from, to) {
  if (e.a === from.id) e.a = to.id; else if (e.b === from.id) e.b = to.id; else return;
  const i = from.e.indexOf(e.id); if (i >= 0) from.e.splice(i, 1);
  to.e.push(e.id);
  from.dead = from.e.length === 0;
}
