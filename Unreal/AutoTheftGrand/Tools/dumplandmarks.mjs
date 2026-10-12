// Prints the landmarks as landmarks.js builds them (its canvases stubbed): every collision call, the beacons,
// each mesh's sums, the lamps, gondola colours and letters (diff against landmarkstest.cpp).
//   node --import ./three-hook.mjs dumplandmarks.mjs > jslandmarks.txt
const ctx = new Proxy({}, { get: (t, k) => (k === 'measureText' ? () => ({ width: 10 }) : k.startsWith && k.startsWith('create') ? () => ({ addColorStop() {} }) : () => {}), set: () => true });
globalThis.document = { createElement: () => ({ width: 0, height: 0, getContext: () => ctx, style: {} }) };
const THREE = await import('three');
const { CityMap } = await import('../../../src/world/citymap.js');
const { buildLandmarks } = await import('../../../src/world/landmarks.js');
const f = (v) => (Math.abs(v) < 0.0005 ? 0 : v).toFixed(3);
const sums = (g) => {
  const p = g.attributes.position, n = g.attributes.normal, c = g.attributes.color;
  const s = [0, 0, 0, 0, 0, 0, 0, 0, 0];
  for (let i = 0; i < p.count; i++) {
    s[0] += p.getX(i); s[1] += p.getY(i); s[2] += p.getZ(i);
    s[3] += n.getX(i); s[4] += n.getY(i); s[5] += n.getZ(i);
    if (c) { s[6] += c.getX(i); s[7] += c.getY(i); s[8] += c.getZ(i); }
  }
  return `v ${p.count} i ${g.index ? g.index.count : 0} ` + s.map((v) => (Math.abs(v) < 0.005 ? 0 : v).toFixed(2)).join(' ');
};
const map = new CityMap();
const out = [];
const city = {
  root: { add() {} }, mats: { detail: new THREE.MeshBasicMaterial() },
  collision: {
    addBox: (b) => out.push(`box ${b.type} ${b.soft ? 1 : 0} ${[b.minX, b.maxX, b.minZ, b.maxZ, b.minY, b.maxY].map(f).join(' ')}`),
    addCircle: (c) => out.push(`circle ${c.type} ${[c.x, c.z, c.r, c.h].map(f).join(' ')}`),
  },
  _beacon: (x, y, z) => out.push(`beacon ${f(x)} ${f(y)} ${f(z)}`),
};
const L = buildLandmarks(city, map);
const kids = L.root.children;
// pier: deck, posts, lamps (instanced), poles; then 16 gondolas, the support, the wheel group; the sign group; cranes
const [deck, posts, lamps, poles] = kids;
out.push(`deck ${sums(deck.geometry)}`);
out.push(`posts ${sums(posts.geometry)}`);
out.push(`poles ${sums(poles.geometry)}`);
const m = new THREE.Matrix4(), v = new THREE.Vector3();
for (let i = 0; i < lamps.count; i++) { lamps.getMatrixAt(i, m); v.setFromMatrixPosition(m); out.push(`lamp ${f(v.x)} ${f(v.y)} ${f(v.z)}`); }
const gond = kids.slice(4, 20);
out.push(`gondola ${sums(gond[0].geometry)}`);
for (const g of gond) out.push(`gcolor ${g.material.color.r.toFixed(4)} ${g.material.color.g.toFixed(4)} ${g.material.color.b.toFixed(4)}`);
out.push(`support ${sums(kids[20].geometry)}`);
const wheel = kids[21];
out.push(`hub ${f(wheel.position.x)} ${f(wheel.position.y)} ${f(wheel.position.z)}`);
out.push(`wheel ${sums(wheel.children[0].geometry)}`);
out.push(`neon ${sums(wheel.children[1].geometry)}`);
const sign = kids[22];
for (const c of sign.children) { if (c.geometry.parameters?.width === 13) out.push(`letter ${f(c.position.x)} ${f(c.position.y)} ${f(c.position.z)}`); else out.push(`scaffold ${sums(c.geometry)}`); }
out.push(`cranes ${sums(kids[23].geometry)}`);
console.log(out.join('\n'));
