// Prints the skateboard model's parts (vertex / index counts, position, normal and colour sums) and wheel pivots
// (diff against boardtest.cpp):
//   node --import ./three-hook.mjs dumpboard.mjs > jsboard.txt
import { VEHICLES } from '../../../src/entities/vehicledefs.js';
import { buildBoardModel } from '../../../src/entities/skateboard.js';
const f = (v) => (v + 0).toFixed(4);
const out = [];
for (const [id, def] of Object.entries(VEHICLES)) {
  if (!def.board) continue;
  const m = buildBoardModel(def, 0xffffff);
  out.push(`board ${id} deckY ${f(m.deck.position.y)}`);
  for (const w of m.wheels) out.push(`wheel ${f(w.pivot.position.x)},${f(w.pivot.position.y)},${f(w.pivot.position.z)} front ${w.front ? 1 : 0}`);
  const parts = { paint: m.body.geometry, trim: m.trim.geometry, wheel: m.wheels[0].spin.children[0].geometry };
  for (const [k, g] of Object.entries(parts)) {
    const p = g.attributes.position, n = g.attributes.normal, c = g.attributes.color;
    let sx = 0, sy = 0, sz = 0, nx = 0, ny = 0, nz = 0, cr = 0, cg = 0, cb = 0;
    for (let i = 0; i < p.count; i++) {
      sx += p.getX(i); sy += p.getY(i); sz += p.getZ(i);
      nx += n.getX(i); ny += n.getY(i); nz += n.getZ(i);
      if (c) { cr += c.getX(i); cg += c.getY(i); cb += c.getZ(i); }
    }
    out.push(`part ${k} v ${p.count} i ${g.index ? g.index.count : 0} pos ${sx.toFixed(3)} ${sy.toFixed(3)} ${sz.toFixed(3)} nor ${nx.toFixed(3)} ${ny.toFixed(3)} ${nz.toFixed(3)} col ${cr.toFixed(3)} ${cg.toFixed(3)} ${cb.toFixed(3)}`);
  }
}
console.log(out.join('\n'));
