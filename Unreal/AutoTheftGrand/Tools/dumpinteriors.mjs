// Prints each walk-in shop: its spots and furniture, the room's meshes (vertex and index counts, sums of
// positions, normals and colours), the glass boxes, weapons on display and the posters / menu boards
// (diff against interiorstest.cpp):
//   node --import ./three-hook.mjs dumpinteriors.mjs > jsinteriors.txt
// (the canvases for the posters are stubbed: only the boards' places are compared)
const ctx2d = new Proxy({}, { get: () => () => {}, set: () => true });
globalThis.document = { createElement: () => ({ getContext: () => ctx2d, width: 0, height: 0 }) };
const { CityMap } = await import('../../../src/world/citymap.js');
const { buildInteriorMesh } = await import('../../../src/world/interiors.js');
const f = (v) => (v + 0).toFixed(3);
const sums = (g) => {
  const p = g.attributes.position, n = g.attributes.normal, c = g.attributes.color;
  const s = [0, 0, 0, 0, 0, 0, 0, 0, 0];
  for (let i = 0; i < p.count; i++) {
    s[0] += p.getX(i); s[1] += p.getY(i); s[2] += p.getZ(i);
    s[3] += n.getX(i); s[4] += n.getY(i); s[5] += n.getZ(i);
    if (c) { s[6] += c.getX(i); s[7] += c.getY(i); s[8] += c.getZ(i); }
  }
  return `v ${p.count} i ${g.index ? g.index.count : 0} pos ${s.slice(0, 3).map((v) => (Math.abs(v) < 0.05 ? 0 : v).toFixed(1)).join(' ')} nor ${s.slice(3, 6).map((v) => (Math.abs(v) < 0.05 ? 0 : v).toFixed(1)).join(' ')} col ${s.slice(6, 9).map((v) => (Math.abs(v) < 0.05 ? 0 : v).toFixed(1)).join(' ')}`;
};
const m = new CityMap();
const out = [];
for (const it of m.interiors) {
  out.push(`shop ${it.key} ${it.name} colliders ${it.colliders.length} furniture ${it.furniture.length}`);
  out.push(`  clerk ${f(it.clerk.x)} ${f(it.clerk.z)} yaw ${f(it.clerk.yaw)} service ${f(it.service.x)} ${f(it.service.z)} till ${f(it.till.x)} ${f(it.till.y)} ${f(it.till.z)}`);
  out.push(`  door ${f(it.door.x)} ${f(it.door.z)} center ${f(it.center.x)} ${f(it.center.z)} light ${f(it.light.x)} ${f(it.light.y)} ${f(it.light.z)}`);
  for (const q of it.furniture) out.push(`  ${q.kind} ${['u', 'w', 'u0', 'w0', 'u1', 'w1', 'h', 'face', 'i'].map((k) => `${k}=${f(q[k] ?? 0)}`).join(' ')}`);
  const g = buildInteriorMesh(it);
  const kids = g.children;
  const n = kids.length;
  const glow = kids[n - 1].material.type === 'MeshBasicMaterial' && kids[n - 1].material.vertexColors ? kids[n - 1] : null;
  const solid = glow ? kids[n - 2] : kids[n - 1];
  out.push(`  solid ${sums(solid.geometry)}`);
  if (glow) out.push(`  glow ${sums(glow.geometry)}`);
  for (const k of kids) {
    if (k === solid || k === glow) continue;
    if (k.material?.transparent && k.material.opacity < 0.3) { const p = k.geometry.parameters; out.push(`  glass ${f(k.position.x)} ${f(k.position.y)} ${f(k.position.z)} size ${f(p.width)} ${f(p.height)} ${f(p.depth)}`); }
    else if (k.geometry?.type === 'PlaneGeometry') { const p = k.geometry.parameters; out.push(`  panel ${f(k.position.x)} ${f(k.position.y)} ${f(k.position.z)} size ${f(p.width)} ${f(p.height)} yaw ${f(k.rotation.y)} lit ${k.material.type === 'MeshStandardMaterial' ? 1 : 0}`); }
    else out.push(`  weapon ${f(k.position.x)} ${f(k.position.y)} ${f(k.position.z)} rot ${f(k.rotation.y)} ${f(k.rotation.z)}`);
  }
}
console.log(out.join('\n'));
