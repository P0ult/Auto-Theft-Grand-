// Prints sums over the humanoid mesh (src/entities/humanoid.js) for a set of appearances that cover every
// branch: sleeves, jackets, tank tops, shorts, every hair style, hats, uniforms, vests, masks. Diff against
// humantest.cpp:
//   node --import ./three-hook.mjs dumphuman.mjs > jshuman.txt
//   ./native.sh humantest.exe humantest.cpp && ./humantest.exe > cpphuman.txt && diff jshuman.txt cpphuman.txt
import { buildHumanoidGeometry } from '../../../src/entities/humanoid.js';

const base = { female: false, skin: 0x8a5536, hair: 0x111111, hairStyle: 'short', shirt: 0xffffff, shirtType: 'tee', pants: 0x1f2a44, shorts: false, shoes: 0x111111, hat: null, build: 1, height: 1, glasses: false, beard: false, jacketColor: 0x222222, bandana: null, uniform: null };
const looks = [
  {},
  { female: true, skin: 0xf1c7a5, hair: 0x7a5230, hairStyle: 'ponytail', shirtType: 'long', shirt: 0x8b1e1e, pants: 0x6d5c43, shorts: true, glasses: true, build: 0.95 },
  { hairStyle: 'cap', hat: 0x8b1e1e, shirtType: 'jacket', jacketColor: 0x455a64, beard: true, build: 1.12, pants: 0x2b2b2b, shoes: 0xeeeeee },
  { hairStyle: 'afro', shirtType: 'tank', shirt: 0xd4a017, pants: 0x4e4a45, bandana: 0x1e3f8b, skin: 0x5f3a24 },
  { hairStyle: 'buzz', shirtType: 'long', uniform: { shirt: 0x1f2f55, pants: 0x1a2440, hat: 0x141d33, band: 0xd4af37 }, vest: { kind: 'tactical' }, mask: 0x111111 },
  { female: true, hairStyle: 'bun', shirtType: 'long', vest: { kind: 'hivis' }, hardhat: 0xffcc00, build: 0.9 },
  { female: true, hairStyle: 'long', shirt: 0x4db6ac, pants: 0x1d3b5c },
  { hairStyle: 'bald', vest: { kind: 'hivis', color: 0xff6600 }, uniform: { shirt: 0x333333, pants: 0x222222 }, noBadge: true },
];
const f = (v) => v.toFixed(3);
for (const [k, o] of looks.entries()) {
  const a = { ...base, ...o };
  const g = buildHumanoidGeometry(a).geo;
  const P = g.attributes.position, N = g.attributes.normal, C = g.attributes.color, M = g.attributes.aMat, SI = g.attributes.skinIndex, SW = g.attributes.skinWeight;
  const s = { p: [0, 0, 0], n: [0, 0, 0], c: [0, 0, 0], m: 0 };
  const bw = new Array(17).fill(0);
  for (let i = 0; i < P.count; i++) {
    s.p[0] += P.getX(i); s.p[1] += P.getY(i); s.p[2] += P.getZ(i);
    s.n[0] += N.getX(i); s.n[1] += N.getY(i); s.n[2] += N.getZ(i);
    s.c[0] += C.getX(i); s.c[1] += C.getY(i); s.c[2] += C.getZ(i);
    s.m += M.getX(i);
    for (let j = 0; j < 4; j++) bw[[SI.getX(i), SI.getY(i), SI.getZ(i), SI.getW(i)][j]] += [SW.getX(i), SW.getY(i), SW.getZ(i), SW.getW(i)][j];
  }
  let isum = 0;
  for (let i = 0; i < g.index.count; i++) isum += g.index.getX(i);
  console.log(`look ${k} v ${P.count} i ${g.index.count} isum ${isum}`);
  console.log(`  pos ${s.p.map(f).join(' ')} nor ${s.n.map(f).join(' ')}`);
  console.log(`  col ${s.c.map(f).join(' ')} mat ${s.m}`);
  console.log(`  bones ${bw.map((v) => v.toFixed(2)).join(' ')}`);
}
