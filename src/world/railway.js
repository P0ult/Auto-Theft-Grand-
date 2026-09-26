// The Sol Line: a railway from Union Station (just west of Los Soles) past Fern Creek to Dry Wells,
// running parallel to the Sol Freeway about 150 m to its south / west. It is built as 'rail' edges in the
// road network so terrain shaping, bridges and pillars come for free; road crossings are pinned to the
// road's height (level crossings) or passed under / over where the road is up on a bridge.
import { RT, offsetLine, resample, cumLen, pointAt, project, intersect, solveProfile } from './roadnet.js';
import { clamp, lerp, smoothstep } from '../core/utils.js';

export const RAIL = { gauge: 1.435, offset: 150, grade: 0.026, cruise: 24 };

export function buildRailway(net, info, fwC, fwCum, terrain, hf) {
  // offset polyline of the freeway centreline, trimmed at Dry Wells and just outside the city
  const off = offsetLine(fwC, RAIL.offset);
  let s0 = 335, s1 = fwCum[fwCum.length - 1];
  // east terminus: where the offset line reaches x = -965
  for (let i = 0; i < off.length; i++) if (off[i][0] > -965) { s1 = fwCum[i]; break; }
  const raw = [];
  for (let i = 0; i < off.length; i++) if (fwCum[i] >= s0 && fwCum[i] <= s1) raw.push([off[i][0], off[i][1]]);
  const pts2 = resample(raw, 6);
  const cum = cumLen(pts2);
  const L = cum[cum.length - 1];

  // ---------------------------------------------------------------- road crossings
  const crossings = [];
  for (const e of net.edges) {
    if (e.removed || e.grid || e.type === 'rail') continue;
    const ep = [];
    for (let i = 0; i < e.n; i++) ep.push([e.p[i * 3], e.p[i * 3 + 2], e.p[i * 3 + 1]]);
    const ec = cumLen(ep);
    const hit = intersect(pts2, cum, ep, ec);
    if (!hit) continue;
    const ry = pointAt(ep, ec, hit.sb)[2];
    const g = terrain(hit.x, hit.z);
    // minor roads get level crossings; highways and anything up on a bridge are grade separated
    let kind = 'level', y = ry;
    if (ry - g > 5 || e.T.cls >= 2) { kind = 'under'; y = ry - 7.4; }
    if (kind === 'under' && y < g - 22) { kind = 'over'; y = ry + 7.6; }
    const ang = Math.abs(Math.atan2(...tangentXZ(pts2, cum, hit.sa)) - Math.atan2(...tangentXZ(ep, ec, hit.sb)));
    const skew = Math.max(0.35, Math.abs(Math.sin(ang)));
    crossings.push({ s: hit.sa, x: hit.x, z: hit.z, y, roadY: ry, kind, edge: e, halfW: (Math.max(e.wL, e.wR) + 1.5) / skew, name: e.name });
  }
  crossings.sort((a, b) => a.s - b.s);

  // ---------------------------------------------------------------- stations
  const fernX = crossings.find((c) => c.name === 'Main Street' && c.kind === 'level' && c.x < -2300 && c.x > -2700);
  const stations = [
    { key: 'dry', name: 'Dry Wells', s: 110 },
    { key: 'fern', name: 'Fern Creek', s: fernX ? fernX.s - 150 : L * 0.45 },
    { key: 'union', name: 'Union Station', s: L - 80 },
  ];

  // ---------------------------------------------------------------- profile
  const pins = crossings.map((c) => ({ s: c.s, y: c.y, r: 60 }));
  let prof = solveProfile(pts2, terrain, { window: 380, maxGrade: RAIL.grade, maxCut: 26, pins });
  const y = prof.y;
  // stations sit on level ground
  for (const st of stations) {
    const i0 = nearest(cum, st.s);
    st.y = y[i0];
    for (let i = 0; i < cum.length; i++) {
      const d = Math.abs(cum[i] - st.s);
      if (d < 70) y[i] = st.y;
      else if (d < 190) y[i] = lerp(st.y, y[i], smoothstep(70, 190, d));
    }
  }
  for (const c of crossings) { const i = nearest(cum, c.s); y[i] = c.y; }
  // relax to the grade limit, keeping crossings and stations
  const fixed = new Uint8Array(cum.length);
  for (const c of crossings) fixed[nearest(cum, c.s)] = 1;
  for (const st of stations) for (let i = 0; i < cum.length; i++) if (Math.abs(cum[i] - st.s) < 65) fixed[i] = 1;
  for (let it = 0; it < 6; it++) {
    for (let i = 1; i < cum.length; i++) if (!fixed[i]) { const ds = cum[i] - cum[i - 1]; y[i] = clamp(y[i], y[i - 1] - RAIL.grade * ds, y[i - 1] + RAIL.grade * ds); }
    for (let i = cum.length - 2; i >= 0; i--) if (!fixed[i]) { const ds = cum[i + 1] - cum[i]; y[i] = clamp(y[i], y[i + 1] - RAIL.grade * ds, y[i + 1] + RAIL.grade * ds); }
  }
  // anything still over the limit (two fixed heights too close together): spread the step over its neighbours
  for (let it = 0; it < 40; it++) {
    let worst = 0;
    for (let i = 1; i < cum.length; i++) {
      const ds = cum[i] - cum[i - 1], dy = y[i] - y[i - 1];
      if (Math.abs(dy) > RAIL.grade * ds * 1.6) { worst = Math.max(worst, Math.abs(dy)); const m = (y[i] + y[i - 1]) / 2; y[i - 1] = lerp(y[i - 1], m, 0.5); y[i] = lerp(y[i], m, 0.5); }
    }
    if (!worst) break;
  }
  const pts = pts2.map((p, i) => [p[0], p[1], y[i]]);

  // station geometry: platform on the right of increasing s (south / east side of the track)
  for (const st of stations) {
    const i = nearest(cum, st.s);
    const [tx, tz] = tangentXZ(pts2, cum, st.s);
    st.x = pts2[i][0]; st.z = pts2[i][1];
    st.tx = tx; st.tz = tz;
    st.yaw = Math.atan2(tx, tz);
    // flatten the ground under the platform & forecourt
    hf.pad({ x: st.x - tz * 12, z: st.z + tx * 12, r: 42, y: st.y - 0.07, blend: 30 });
  }

  // ---------------------------------------------------------------- graph edges (split at stations)
  const edges = [];
  const mid = stations[1];
  const cuts = [0, nearest(cum, mid.s), pts.length - 1];
  // the termini sit a little in from the ends of the line (buffer stops beyond them)
  const nA = net.addNode(pts[0][0], pts[0][1], pts[0][2], { kind: 'via', r: 0, name: 'Sol Line west end', rail: true });
  const nM = net.addNode(mid.x, mid.z, mid.y, { kind: 'via', r: 0, name: mid.name + ' station', rail: true });
  const nB = net.addNode(pts[pts.length - 1][0], pts[pts.length - 1][1], pts[pts.length - 1][2], { kind: 'via', r: 0, name: 'Sol Line east end', rail: true });
  const ends = [nA, nM, nB];
  for (let q = 0; q < cuts.length - 1; q++) {
    const e = net.addEdge(ends[q], ends[q + 1], pts.slice(cuts[q], cuts[q + 1] + 1), 'rail', { name: 'Sol Line', barrierL: true, barrierR: true });
    e.rail = true;
    edges.push(e);
  }
  // crossing zones per edge (in each edge's own arc length) so the renderer can leave the road surface alone
  for (let q = 0; q < edges.length; q++) {
    const e = edges[q];
    const sA = cum[cuts[q]], sB = cum[cuts[q + 1]];
    e.crossings = crossings.filter((c) => c.kind === 'level' && c.s >= sA - 1 && c.s <= sB + 1).map((c) => ({ s: c.s - sA, halfW: c.halfW }));
  }
  // ---------------------------------------------------------------- passing loop at Fern Creek
  // A second track on the far side from the platform, so two trains can pass mid-line (the rest of the
  // line is single track: a train may only enter it once the other has cleared it).
  let l0 = mid.s - 240, l1 = mid.s + 125;
  for (const c of crossings) { if (c.s >= mid.s && c.s - 45 < l1) l1 = c.s - 45; if (c.s < mid.s && c.s + 45 > l0) l0 = c.s + 45; }
  const loop = { s0: l0, s1: l1, taper: 55, off: -4.6 };
  loop.m0 = loop.s0 + loop.taper; loop.m1 = loop.s1 - loop.taper;
  // its ballast / terrain / building clearance come from a (non-routing) rail edge along the offset line
  const loopPts = [];
  for (let sl = l0; sl <= l1 + 0.01; sl += 4) {
    const q = pointAt(pts2, cum, sl), [tx, tz] = tangentXZ(pts2, cum, sl), o = loopOffsetAt(loop, sl);
    loopPts.push([q[0] - tz * o, q[1] + tx * o, interpY(cum, y, sl)]);
  }
  const la = net.addNode(loopPts[0][0], loopPts[0][1], loopPts[0][2], { kind: 'via', r: 0, name: 'Fern Creek loop', rail: true });
  const lb = net.addNode(loopPts[loopPts.length - 1][0], loopPts[loopPts.length - 1][1], loopPts[loopPts.length - 1][2], { kind: 'via', r: 0, name: 'Fern Creek loop', rail: true });
  const le = net.addEdge(la, lb, loopPts, 'rail', { name: 'Sol Line (Fern Creek loop)', wL: 4.2, wR: 2.2 });
  le.rail = true; le.loop = true;
  loop.edge = le;

  const rail = { pts, cum, length: L, stations, crossings, edges, nodes: ends, loop };
  info.rail = rail;
  return rail;
}

function nearest(cum, s) {
  let lo = 0, hi = cum.length - 1;
  while (hi - lo > 1) { const m = (lo + hi) >> 1; if (cum[m] <= s) lo = m; else hi = m; }
  return s - cum[lo] < cum[hi] - s ? lo : hi;
}
function tangentXZ(pts, cum, s) {
  const a = pointAt(pts, cum, Math.max(0, s - 3)), b = pointAt(pts, cum, Math.min(cum[cum.length - 1], s + 3));
  const dx = b[0] - a[0], dz = b[1] - a[1], l = Math.hypot(dx, dz) || 1;
  return [dx / l, dz / l];
}

// Position along the line: [x, y, z, tx, tz, grade]
function interpY(cum, y, s) {
  const i = nearest(cum, s);
  const j = cum[i] > s ? Math.max(0, i - 1) : Math.min(cum.length - 1, i + 1);
  if (i === j) return y[i];
  const t = (s - cum[i]) / (cum[j] - cum[i]);
  return y[i] + (y[j] - y[i]) * t;
}

// lateral offset of the passing-loop track from the main line at arc length s (0 where they're joined)
export function loopOffsetAt(loop, s) {
  if (!loop || s <= loop.s0 || s >= loop.s1) return 0;
  return loop.off * smoothstep(loop.s0, loop.s0 + loop.taper, s) * (1 - smoothstep(loop.s1 - loop.taper, loop.s1, s));
}

// railAt on a given track: 0 = main line, 1 = the passing loop (joins the main line at its ends)
export function railAtTrack(rail, s, track, out = [0, 0, 0, 0, 0, 0]) {
  railAt(rail, s, out);
  if (!track || !rail.loop) return out;
  const o = loopOffsetAt(rail.loop, s);
  const d = loopOffsetAt(rail.loop, s + 1) - o;
  if (!o && !d) return out;
  const tx = out[3], tz = out[4];
  out[0] += -tz * o; out[2] += tx * o;
  const nx = tx - tz * d, nz = tz + tx * d, l = Math.hypot(nx, nz) || 1;
  out[3] = nx / l; out[4] = nz / l;
  return out;
}

export function railAt(rail, s, out = [0, 0, 0, 0, 0, 0]) {
  const { pts, cum } = rail;
  const L = cum[cum.length - 1];
  s = clamp(s, 0, L);
  let lo = 0, hi = cum.length - 1;
  while (hi - lo > 1) { const m = (lo + hi) >> 1; if (cum[m] <= s) lo = m; else hi = m; }
  const t = (s - cum[lo]) / (cum[hi] - cum[lo] || 1);
  const a = pts[lo], b = pts[hi];
  const dx = b[0] - a[0], dz = b[1] - a[1], l = Math.hypot(dx, dz) || 1;
  out[0] = a[0] + dx * t; out[2] = a[1] + dz * t; out[1] = a[2] + (b[2] - a[2]) * t;
  out[3] = dx / l; out[4] = dz / l; out[5] = (b[2] - a[2]) / l;
  return out;
}

export { RT };
