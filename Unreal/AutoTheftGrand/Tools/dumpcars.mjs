// Prints each car model's seats, door, hull and per-part vertex / index counts (diff against carstest.cpp).
//   node --import ./three-hook.mjs dumpcars.mjs > jscars.txt
import { VEHICLES } from '../../../src/entities/vehicledefs.js';
import { buildVehicleModel } from '../../../src/entities/vehiclemodels.js';
const f = (v) => v.toFixed(4);
const out = [];
for (const [id, def] of Object.entries(VEHICLES)) {
  if (def.kind || def.bike) continue;
  const m = buildVehicleModel(def, 0xffffff);
  out.push(`car ${id}`);
  out.push(`seats ${m.seats.map((s) => `${f(s.x)},${f(s.y)},${f(s.z)}`).join(' ')}`);
  out.push(`door ${f(m.doorPos.x)},${f(m.doorPos.z)} hinge ${f(m.door.pivot.position.x)},${f(m.door.pivot.position.y)},${f(m.door.pivot.position.z)}`);
  const h = m.hull;
  out.push(`hull ${f(h.roofX)} ${f(h.roofY)} ${f(h.roofZ0)} ${f(h.roofZ1)} ${f(h.beltY)} ${f(h.cgH)}`);
  const parts = { body: m.panels.body, hood: m.panels.hood, trunk: m.panels.trunk, door2: m.panels.door2, bumperF: m.panels.bumperF, bumperR: m.panels.bumperR, glass: m.glass, trim: m.trim, head: m.head, tail: m.tail, door: m.panels.door };
  for (const [k, mesh] of Object.entries(parts)) {
    if (!mesh) continue;
    const g = mesh.geometry, p = g.attributes.position;
    let sx = 0, sy = 0, sz = 0;
    for (let i = 0; i < p.count; i++) { sx += p.getX(i); sy += p.getY(i); sz += p.getZ(i); }
    out.push(`part ${k} v ${p.count} i ${g.index ? g.index.count : 0} sum ${sx.toFixed(2)} ${sy.toFixed(2)} ${sz.toFixed(2)}`);
  }
  const w = m.wheels[0].spin.children[0].geometry;
  out.push(`wheel v ${w.attributes.position.count}`);
}
console.log(out.join('\n'));
