// Prints each bike model's seats, door, grips, feet, head/tail and per-part vertex / index counts (diff against bikestest.cpp).
//   node --import ./three-hook.mjs dumpbikes.mjs > jsbikes.txt
import * as THREE from 'three';
import { VEHICLES } from '../../../src/entities/vehicledefs.js';
import { buildBikeModel } from '../../../src/entities/bikes.js';
const f = (v) => v.toFixed(4);
const out = [];
for (const [id, def] of Object.entries(VEHICLES)) {
  if (!def.bike || def.board) continue;
  const m = buildBikeModel(def, 0xffffff);
  out.push(`bike ${id}`);
  out.push(`seats ${m.seats.map((s) => `${f(s.x)},${f(s.y)},${f(s.z)}`).join(' ')}`);
  out.push(`door ${f(m.doorPos.x)},${f(m.doorPos.z)}`);
  out.push(`grips ${f(m.grips[0])},${f(m.grips[1])}`);
  if (m.feet) out.push(`feet ${m.feet.map((ft) => `${f(ft[0])},${f(ft[1])},${f(ft[2])}`).join(' ')}`);
  else out.push(`feet none`);
  out.push(`headY ${f(m.head.position.y)} headZ ${f(m.head.position.z)} headX ${f(m.head.geometry.parameters.width / 2)} headZSize ${f(m.head.geometry.parameters.depth / 2)}`);
  out.push(`tailY ${f(m.tail.position.y)} tailZ ${f(m.tail.position.z)} tailX ${f(m.tail.geometry.parameters.width / 2)} tailZSize ${f(m.tail.geometry.parameters.depth / 2)}`);
  // fork is the Mesh child of the front wheel's pivot (the one that's not the spin group)
  const frontWheel = m.wheels.find((w) => w.front);
  const forkMesh = frontWheel.pivot.children.find((c) => c.isMesh);
  // wheel: use the mesh from the first wheel's spin
  const wheelMesh = m.wheels[0].spin.children[0];
  // crank: for bicycles, it's a Group containing a Mesh; for motorbikes it's null
  const crankMesh = m.crank ? m.crank.children.find((c) => c.isMesh) : null;
  const parts = { body: m.body, trim: m.trim, fork: forkMesh, wheel: wheelMesh, crank: crankMesh };
  for (const [k, mesh] of Object.entries(parts)) {
    if (!mesh) continue;
    const g = mesh.geometry, p = g.attributes.position;
    let sx = 0, sy = 0, sz = 0;
    for (let i = 0; i < p.count; i++) { sx += p.getX(i); sy += p.getY(i); sz += p.getZ(i); }
    out.push(`part ${k} v ${p.count} i ${g.index ? g.index.count : 0} sum ${sx.toFixed(2)} ${sy.toFixed(2)} ${sz.toFixed(2)}`);
  }
  // glass: 8 corners of the glass box after the glass matrix (task spec)
  const glassMesh = m.glass;
  glassMesh.updateMatrixWorld(true);
  const box = glassMesh.geometry.parameters;
  const hw = box.width / 2, hh = box.height / 2, hd = box.depth / 2;
  const mat = glassMesh.matrixWorld;
  let sx = 0, sy = 0, sz = 0;
  for (let ix = -1; ix <= 1; ix += 2) for (let iy = -1; iy <= 1; iy += 2) for (let iz = -1; iz <= 1; iz += 2) {
    const v = new THREE.Vector3(ix * hw, iy * hh, iz * hd).applyMatrix4(mat);
    sx += v.x; sy += v.y; sz += v.z;
  }
  out.push(`part glass v 8 i 0 sum ${sx.toFixed(2)} ${sy.toFixed(2)} ${sz.toFixed(2)}`);
}
console.log(out.join('\n'));