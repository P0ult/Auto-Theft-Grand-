// Prints the road network's collision primitives in the order roadmesh.js adds them (diff against coltest.cpp).
//   node --import ./three-hook.mjs dumpcol.mjs > jscol.txt
import { CityMap } from '../../../src/world/citymap.js';
import { RoadMeshes } from '../../../src/world/roadmesh.js';
const m = new CityMap();
const f = (v) => (v == null ? 'na' : v.toFixed(3));
const out = [];
const col = {
  addOBox: (o) => out.push(`O ${o.type} ${f(o.cx)} ${f(o.cz)} ${f(o.hx)} ${f(o.hz)} ${f(o.yaw)} ${f(o.minY)} ${f(o.maxY)}`),
  addBox: (b) => out.push(`B ${b.type}`),
  addCircle: (c) => out.push(`C ${c.type} ${f(c.x)} ${f(c.z)} ${f(c.r)} ${f(c.h)}`),
  addDeck: (d) => out.push(`D ${f(d.ax)} ${f(d.az)} ${f(d.ay)} ${f(d.bx)} ${f(d.bz)} ${f(d.by)} ${f(d.hl)} ${f(d.hr)} ${d.pavement ? 1 : 0}`),
};
new RoadMeshes({ add() {} }, m, col);
console.log(out.join('\n'));
