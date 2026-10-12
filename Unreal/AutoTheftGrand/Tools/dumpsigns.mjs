// Prints the billboards (frame and face sums, collision boxes) and the shops' name signs (text, centre, size,
// yaw) as city.js _billboards and _signs make them (diff against signstest.cpp). The canvases are stubbed.
//   node --import ./three-hook.mjs dumpsigns.mjs > jssigns.txt
const ctx = new Proxy({}, { get: (t, k) => (k === 'measureText' ? () => ({ width: 10 }) : k === 'createLinearGradient' || k === 'createRadialGradient' ? () => ({ addColorStop() {} }) : () => {}), set: () => true });
globalThis.document = { createElement: () => ({ width: 0, height: 0, getContext: () => ctx, style: {} }) };
const { CityMap } = await import('../../../src/world/citymap.js');
const { City } = await import('../../../src/world/city.js');
const f = (v) => (Math.abs(v) < 0.0005 ? 0 : v).toFixed(3);
// (the frame's uvs are left out: its vertex-lit material never samples them)
const sums = (g, uvs = true) => {
  const p = g.attributes.position, n = g.attributes.normal, c = g.attributes.color, uv = g.attributes.uv;
  const s = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
  for (let i = 0; i < p.count; i++) {
    s[0] += p.getX(i); s[1] += p.getY(i); s[2] += p.getZ(i);
    s[3] += n.getX(i); s[4] += n.getY(i); s[5] += n.getZ(i);
    if (c) { s[6] += c.getX(i); s[7] += c.getY(i); s[8] += c.getZ(i); }
    if (uv && uvs) { s[9] += uv.getX(i); s[10] += uv.getY(i); }
  }
  return `v ${p.count} i ${g.index ? g.index.count : 0} ` + s.map((v) => (Math.abs(v) < 0.005 ? 0 : v).toFixed(2)).join(' ');
};
const map = new CityMap();
const out = [];
const meshes = [];
const self = { map, root: { add: (m) => meshes.push(m) }, mats: { detail: null }, collision: { addBox: (b) => out.push(`box ${[b.minX, b.maxX, b.minZ, b.maxZ, b.minY, b.maxY].map(f).join(' ')}`) }, signMats: [] };
City.prototype._billboards.call(self);
out.push(`frame ${sums(meshes[0].geometry, false)}`);
out.push(`faces ${sums(meshes[1].geometry)}`);
meshes.length = 0;
City.prototype._signs.call(self);
for (const m of meshes) out.push(`sign ${m.name.slice(5)} ${f(m.position.x)} ${f(m.position.y)} ${f(m.position.z)} ${f(m.geometry.parameters.width)} ${f(m.geometry.parameters.height)} ${f(m.rotation.y)}`);
console.log(out.join('\n'));
