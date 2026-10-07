// Prints the cargo ship: every collision call in order, each part's vertex and index counts with the sums of
// positions, normals and colours, the spots and the container stacks (diff against shiptest.cpp).
//   node --import ./three-hook.mjs dumpship.mjs > jsship.txt
import { buildCargoShip, SHIP, shipHalfBeam } from '../../../src/world/cargoship.js';
const f = (v) => (Math.abs(v) < 0.0005 ? 0 : v).toFixed(3);
const out = [];
const game = {
  collision: {
    addBox(b) { out.push(`box ${b.type} ${[b.minX, b.maxX, b.minZ, b.maxZ, b.minY, b.maxY].map(f).join(' ')}`); },
    addOBox(o) { out.push(`obox ${o.type} ${[o.cx, o.cz, o.hx, o.hz, o.yaw, o.minY, o.maxY].map(f).join(' ')}`); },
    addDeck(d) { out.push(`deck ${[d.ax, d.az, d.ay, d.bx, d.bz, d.by, d.hl, d.hr].map(f).join(' ')}`); },
    addCircle(c) { out.push(`circle ${[c.x, c.z, c.r, c.h, c.y0].map(f).join(' ')}`); },
  },
  scene: { add() {} },
};
const sums = (g) => {
  const p = g.attributes.position, n = g.attributes.normal, c = g.attributes.color;
  const s = [0, 0, 0, 0, 0, 0, 0, 0, 0];
  for (let i = 0; i < p.count; i++) {
    s[0] += p.getX(i); s[1] += p.getY(i); s[2] += p.getZ(i);
    s[3] += n.getX(i); s[4] += n.getY(i); s[5] += n.getZ(i);
    if (c) { s[6] += c.getX(i); s[7] += c.getY(i); s[8] += c.getZ(i); }
  }
  const t = (a, b) => s.slice(a, b).map((v) => (Math.abs(v) < 0.005 ? 0 : v).toFixed(2)).join(' ');
  return `v ${p.count} i ${g.index ? g.index.count : 0} pos ${t(0, 3)} nor ${t(3, 6)} col ${t(6, 9)}`;
};
const ship = buildCargoShip(game);
const names = ['hull', 'deck', 'steel', 'white', 'cont', 'glass', 'lamp'];
ship.root.children.forEach((m, i) => out.push(`part ${names[i]} ${sums(m.geometry)}`));
const S = ship.spec;
const pt = (k) => out.push(`${k} ${f(S[k].x)} ${f(S[k].y)} ${f(S[k].z)}`);
for (const k of ['safe', 'bridgeDoor', 'gangwayFoot', 'gangwayTop']) pt(k);
out.push(`route ${S.route.map((r) => r.map(f).join(',')).join(' ')}`);
for (const s of S.stacks) out.push(`stack ${f(s.lx)} ${f(s.lz)} ${s.n} ${f(s.bay)} ${s.row}`);
for (const lz of [-93, -80, -60, -48, 0, 80, 85]) out.push(`beam ${lz} ${f(shipHalfBeam(lz))}`);
out.push(`ship ${SHIP.name} ${f(SHIP.x)} ${f(SHIP.z)} ${f(SHIP.deckY)}`);
console.log(out.join('\n'));
