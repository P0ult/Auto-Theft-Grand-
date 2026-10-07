// Prints each boat model: seats, door, hull numbers, the prop, and per part the vertex and index counts with the
// sums of positions, normals and colours (0 without vertex colours) (diff against boatstest.cpp).
//   node --import ./three-hook.mjs dumpboats.mjs > jsboats.txt
import { VEHICLES } from '../../../src/entities/vehicledefs.js';
import { buildBoatModel } from '../../../src/entities/boat.js';
const f = (v) => (v + 0).toFixed(4);
const sums = (g) => {
  const p = g.attributes.position, n = g.attributes.normal, c = g.attributes.color;
  const s = [0, 0, 0, 0, 0, 0, 0, 0, 0];
  for (let i = 0; i < p.count; i++) {
    s[0] += p.getX(i); s[1] += p.getY(i); s[2] += p.getZ(i);
    s[3] += n.getX(i); s[4] += n.getY(i); s[5] += n.getZ(i);
    if (c) { s[6] += c.getX(i); s[7] += c.getY(i); s[8] += c.getZ(i); }
  }
  const t = (a, b) => s.slice(a, b).map((v) => (v + 0).toFixed(2)).join(' ');
  return `v ${p.count} i ${g.index ? g.index.count : 0} pos ${t(0, 3)} nor ${t(3, 6)}` + ` col ${t(6, 9)}`;
};
const out = [];
for (const [id, def] of Object.entries(VEHICLES)) {
  if (!def.boat) continue;
  const m = buildBoatModel(def, 0xffffff);
  out.push(`boat ${id}`);
  out.push(`seats ${m.seats.map((s) => `${f(s.x)},${f(s.y)},${f(s.z)}`).join(' ')}`);
  out.push(`seatHip ${f(m.seatHip)}`);
  out.push(`doorPos ${f(m.doorPos.x)},${f(m.doorPos.y)},${f(m.doorPos.z)}`);
  out.push(`hull draft ${f(m.hull.draft)} free ${f(m.hull.free)} L ${f(m.hull.L)} z0 ${f(m.hull.z0)}`);
  out.push(`propPos ${f(m.prop.position.x)},${f(m.prop.position.y)},${f(m.prop.position.z)}`);
  // the deck (boatdeck material) and chrome (M.chrome) meshes are the body group's other children
  const known = new Set([m.body, m.trim, m.glass, m.head, m.tail]);
  if (m.lightbar) { known.add(m.lightbar.red); known.add(m.lightbar.blue); }
  let deck = null, chrome = null;
  for (const c of m.bodyGroup.children) {
    if (!c.isMesh || known.has(c)) continue;
    if (c.material.metalness < 0.1) deck = c; else if (c.material.metalness > 0.8) chrome = c;
  }
  const parts = { hull: m.body, trim: m.trim, glass: m.glass, head: m.head, tail: m.tail, deck, chrome };
  for (const [k, mesh] of Object.entries(parts)) if (mesh) out.push(`part ${k} ${sums(mesh.geometry)}`);
  if (m.lightbar) for (const [col, mesh] of Object.entries({ red: m.lightbar.red, blue: m.lightbar.blue })) out.push(`light ${col} at ${f(mesh.position.x)},${f(mesh.position.y)},${f(mesh.position.z)} ${sums(mesh.geometry)}`);
  if (m.gun) out.push(`gun at ${f(m.gun.position.x)},${f(m.gun.position.y)},${f(m.gun.position.z)} ${sums(m.gun.children[0].geometry)}`);
}
console.log(out.join('\n'));
