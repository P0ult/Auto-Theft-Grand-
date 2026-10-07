// Prints each aircraft (and the tank) model's numbers and per-mesh vertex counts and sums (diff against
// aircrafttest.cpp):
//   node --import ./three-hook.mjs dumpaircraft.mjs > jsair.txt
import * as THREE from 'three';
import { VEHICLES } from '../../../src/entities/vehicledefs.js';
import { buildAircraftModel } from '../../../src/entities/aircraftmodels.js';
const f = (v) => v.toFixed(4);
const p3 = (v) => `${f(v.x)},${f(v.y)},${f(v.z)}`;
const out = [];
const sums = (k, g) => {
  const p = g.attributes.position, c = g.attributes.color;
  let sx = 0, sy = 0, sz = 0, cr = 0, cg = 0, cb = 0;
  for (let i = 0; i < p.count; i++) { sx += p.getX(i); sy += p.getY(i); sz += p.getZ(i); if (c) { cr += c.getX(i); cg += c.getY(i); cb += c.getZ(i); } }
  out.push(`  ${k} v ${p.count} i ${g.index ? g.index.count : 0} sum ${sx.toFixed(2)} ${sy.toFixed(2)} ${sz.toFixed(2)} col ${cr.toFixed(2)} ${cg.toFixed(2)} ${cb.toFixed(2)}`);
};
for (const id of ['skipper', 'raptor', 'hercules', 'warhawk', 'skylark', 'mammoth']) {
  const def = VEHICLES[id];
  const m = buildAircraftModel(def, def.colors[0], id);
  out.push(`model ${id} cgY ${f(m.cgY)} visibleSeats ${m.visibleSeats ?? 0}`);
  out.push(`  seats ${m.seats.map(p3).join(' ')}`);
  out.push(`  doorPos ${p3(m.doorPos)}`);
  if (m.muzzles?.length) out.push(`  muzzles ${m.muzzles.map(p3).join(' ')}`);
  if (m.pylons) out.push(`  pylons ${m.pylons.map(p3).join(' ')}`);
  if (m.pods) out.push(`  pods ${m.pods.map(p3).join(' ')}`);
  // the body group's own meshes, in order: the two finish() parts come last
  const bg = m.bodyGroup;
  const own = bg.children.filter((o) => o.isMesh && o !== m.gear && o !== m.glass && !m.flames?.includes(o) && o.geometry.type !== 'SphereGeometry' && !(m.wheelSets && m.wheelSets.flat().includes(o)));
  own.forEach((o, i) => sums(`part${i}`, o.geometry));
  if (m.gear) sums('gear', m.gear.geometry);
  if (m.glass) { sums('glass', m.glass.geometry); out.push(`  glassAt ${p3(m.glass.position)}`); }
  for (const pr of m.props) { out.push(`  prop at ${p3(pr.obj.position)} axis ${pr.axis} rate ${f(pr.rate)} disc ${f(pr.disc.geometry.parameters.radius)}`); sums('blade', pr.blade.geometry); }
  if (m.rotor) {
    out.push(`  rotor at ${p3(m.rotor.obj.position)} disc ${f(m.rotor.disc.geometry.parameters.radius)}`); sums('rotorBlade', m.rotor.blade.geometry);
    out.push(`  tail at ${p3(m.tailRotor.obj.position)} disc ${f(m.tailRotor.disc.geometry.parameters.radius)}`); sums('tailBlade', m.tailRotor.blade.geometry);
  }
  if (m.flames) for (const fl of m.flames) { out.push(`  flame at ${p3(fl.position)}`); sums('flame', fl.geometry); }
  for (const o of bg.children) if (o.isMesh && o.geometry.type === 'SphereGeometry') out.push(`  light at ${p3(o.position)} r ${f(o.geometry.parameters.radius)}${o === m.strobe ? ' strobe' : ''}`);
  if (m.gun && !m.turret) { out.push(`  gun at ${p3(m.gun.position)}`); sums('gunMesh', m.gun.children[0].geometry); }
  if (m.turret) {
    out.push(`  turret at ${p3(m.turret.position)} gunPivot ${p3(m.gunPivot.position)}`);
    sums('turretMesh', m.turret.children[0].geometry);
    sums('gunMesh', m.gun.geometry);
    out.push(`  hatch pivot ${p3(m.door.pivot.position)} axis ${m.door.axis} max ${f(m.door.max)}`);
    sums('hatch', m.door.pivot.children[0].geometry);
    for (const set of m.wheelSets) out.push(`  wheels ${set.map((w) => p3(w.position)).join(' ')}`);
    sums('roadWheel', m.wheelSets[0][0].geometry);
  } else if (m.door.axis) out.push(`  door pivot ${p3(m.door.pivot.position)} axis ${m.door.axis} max ${f(m.door.max)}`);
}
console.log(out.join('\n'));
