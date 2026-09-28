// Prints a summary of the JavaScript world (run from Unreal/AutoTheftGrand/Tools: node dumpworld.mjs > js.txt) to diff
// against gentest.cpp, which prints the same for the C++ port.
import { CityMap } from '../../../src/world/citymap.js';
const t0 = Date.now();
const m = new CityMap();
const ms = Date.now() - t0;
const net = m.roads;
const L = [];
const f = (v) => (typeof v === 'number' ? v.toFixed(3) : String(v));
L.push(`nodes ${net.nodes.length} edges ${net.edges.length} live ${net.edges.filter(e=>!e.removed).length}`);
let hsum = 0; for (let k = 0; k < m.hf.h.length; k += 7) hsum += m.hf.h[k];
L.push(`hsum ${hsum.toFixed(2)}`);
L.push(`blocks ${m.blocks.length} buildings ${m.buildings.length} lots ${m.lotSurfaces.length} pads ${m.padSurfaces.length} props ${m.props.length} fences ${m.fences.length} parking ${m.parkingSpots.length} pools ${m.pools.length} containers ${m.containers.length} colliders ${m.colliders.length} walk ${m.walkNodes.length} towns ${m.townAreas.length} fixed ${m.fixedVehicles.length}`);
L.push(`veg ${Object.entries(m.vegetation).map(([k,v])=>k+'='+v.length/5).join(' ')}`);
for (const n of net.nodes) L.push(`N ${n.id} ${f(n.x)} ${f(n.z)} ${f(n.y)} ${n.kind} ${f(n.r)} ${n.e.join(',')} ${n.name}`);
for (const e of net.edges) { let ys = 0; for (let i = 0; i < e.n; i++) ys += e.p[i*3+1]; let dk = 0; for (let i = 0; i < e.n; i++) dk += e.deck[i]; L.push(`E ${e.id} ${e.a} ${e.b} ${e.type} ${e.n} ${f(e.len)} ${f(ys)} ${dk} ${e.removed?1:0} ${e.name}`); }
for (const b of m.buildings) L.push(`B ${f(b.x0)} ${f(b.z0)} ${f(b.x1)} ${f(b.z1)} ${f(b.y0)} ${f(b.y1)} ${b.style} ${f(b.rot)} ${b.roof} ${b.district} ${b.kind} ${b.name||''}`);
for (const p of m.props) L.push(`P ${p.type} ${f(p.x)} ${f(p.z)} ${f(p.rot||0)}`);
for (const k of Object.keys(m.landmarks).sort()) { const v = m.landmarks[k]; if (v && typeof v.x === 'number') L.push(`L ${k} ${f(v.x)} ${f(v.z)}`); }
console.log(L.join('\n'));
console.error('js ms', ms);
