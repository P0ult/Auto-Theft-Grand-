// GPS routing: A* over the road-grid intersections.
import { XS, ZS } from '../world/citymap.js';

const NI = XS.length, NJ = ZS.length;

function nearestNode(x, z) {
  let bi = 0, bj = 0, bd = Infinity;
  for (let i = 0; i < NI; i++) for (let j = 0; j < NJ; j++) {
    const d = (XS[i] - x) ** 2 + (ZS[j] - z) ** 2;
    if (d < bd) { bd = d; bi = i; bj = j; }
  }
  return [bi, bj];
}

export function route(fromX, fromZ, toX, toZ) {
  const [si, sj] = nearestNode(fromX, fromZ);
  const [ti, tj] = nearestNode(toX, toZ);
  const key = (i, j) => i * 100 + j;
  const open = new Map(), g = new Map(), came = new Map();
  const h = (i, j) => Math.hypot(XS[i] - XS[ti], ZS[j] - ZS[tj]);
  open.set(key(si, sj), [si, sj, h(si, sj)]);
  g.set(key(si, sj), 0);
  const closed = new Set();
  let found = false;
  while (open.size) {
    let bestK = null, best = null;
    for (const [k, v] of open) if (!best || v[2] < best[2]) { best = v; bestK = k; }
    open.delete(bestK);
    const [i, j] = best;
    if (i === ti && j === tj) { found = true; break; }
    closed.add(bestK);
    for (const [di, dj] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
      const ni = i + di, nj = j + dj;
      if (ni < 0 || nj < 0 || ni >= NI || nj >= NJ) continue;
      const nk = key(ni, nj);
      if (closed.has(nk)) continue;
      const cost = g.get(bestK) + Math.hypot(XS[ni] - XS[i], ZS[nj] - ZS[j]);
      if (!g.has(nk) || cost < g.get(nk)) {
        g.set(nk, cost); came.set(nk, bestK);
        open.set(nk, [ni, nj, cost + h(ni, nj)]);
      }
    }
  }
  const pts = [[toX, toZ]];
  if (found) {
    let k = key(ti, tj);
    while (k !== undefined) { const i = Math.floor(k / 100), j = k % 100; pts.push([XS[i], ZS[j]]); k = came.get(k); }
  }
  pts.push([fromX, fromZ]);
  pts.reverse();
  return pts;
}
