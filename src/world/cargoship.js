// The MV Pacific Star at Port Morena: a container ship you can board. A lofted hull, a walkable main deck with
// container stacks in five bays (walkways along both sides and across between bays), a raised forecastle up a
// ramp, and a stepped superstructure whose balconies zig-zag up by ramps to the bridge (the captain's safe is
// in there). A gangway climbs from the quay. Everything is solid: walls, rails, stacks, floors.
import * as THREE from 'three';
import { CITY, WATER_Y } from './citymap.js';
import { Part, emitGrid, stations, roundBox } from '../entities/loft.js';
import { patch } from '../render/materials.js';
import { RNG } from '../core/utils.js';

const DY = WATER_Y + 10; // main deck
export const SHIP = {
  name: 'MV Pacific Star', x: CITY.maxX + 56, z: 270, L: 170, W: 26, deckY: DY,
  stern: 85, bow: -93, fc: -68,             // local z of the transom, the stem and the forecastle break
  bays: [-50, -29, -8, 13, 34], bayHalf: 9.45,
  levels: [DY, DY + 3, DY + 6, DY + 9],     // main deck, A, B, bridge
  gangway: { xFoot: -21.8, x: -17.4, z0: 17, z1: 46.3, land: [46, 49.5] },
};

// half beam at local z (a long parallel body, a fine bow)
export function shipHalfBeam(lz) {
  if (lz > 80) return 13 - (lz - 80) * 0.08;
  if (lz > -48) return 13;
  const u = (-48 - lz) / (-SHIP.bow - 48 + 1e-6);
  return Math.max(0.3, 13 * Math.sqrt(Math.max(0, 1 - u * u)));
}

export function buildCargoShip(game) {
  const S = SHIP, col = game.collision;
  const X = (lx) => S.x + lx, Z = (lz) => S.z + lz;
  const root = new THREE.Group();
  const hull = new Part(), deck = new Part(), steel = new Part(), white = new Part(), glass = new Part(), lamp = new Part();
  const rng = new RNG(19);

  // ---------------- hull (lofted: flat bottom, round bilge, flared bow, raised forecastle)
  const keel = WATER_Y - 6.5;
  const topAt = (lz) => (lz < S.fc - 1 ? DY + 2.4 : lz < S.fc + 1 ? DY + 2.4 * (S.fc + 1 - lz) / 2 : DY) + 1.1;
  const zs = stations(S.bow, S.stern, 3, [S.fc - 1, S.fc + 1, -48, 80]);
  const ring = (lz) => {
    const b = shipHalfBeam(lz), top = topAt(lz);
    const fore = lz < -60 ? (-60 - lz) / 33 : 0;
    const y0 = keel + fore * fore * 5.5; // the forefoot rises
    const br = Math.min(1.6, b * 0.5);
    const pts = [[0, y0]];
    pts.push([Math.max(0, b - br), y0]);
    for (let k = 1; k <= 4; k++) { const a = -Math.PI / 2 + k / 4 * Math.PI / 2; pts.push([b - br + Math.cos(a) * br, y0 + br + Math.sin(a) * br]); }
    for (let k = 1; k <= 4; k++) { const y = y0 + br + (top - y0 - br) * k / 4; pts.push([b * (1 + fore * 0.08 * k / 4), y]); }
    return pts;
  };
  const hullCol = (x, y) => (y < WATER_Y + 0.4 ? [0.45, 0.12, 0.08] : y < WATER_Y + 0.9 ? [0.9, 0.9, 0.88] : y > DY + 0.6 ? [0.85, 0.85, 0.82] : [0.1, 0.18, 0.3]);
  for (const sd of [1, -1]) emitGrid(zs.map((lz) => ring(lz).map(([x, y]) => [X(x * sd), y, Z(lz)])), () => hull, { ref: [S.x, 0, S.z], colorAt: (x, y) => hullCol(x, y) });
  { const r0 = ring(S.stern); hull.color(0.1, 0.18, 0.3); hull.poly(r0.map(([x, y]) => [X(x), y, Z(S.stern)]).concat(r0.slice().reverse().map(([x, y]) => [X(-x), y, Z(S.stern)])), [0, 0, 1]); }
  // inner face of the bulwark (so the rail reads from the deck)
  for (const sd of [1, -1]) {
    const rows = zs.filter((lz) => lz > S.bow + 4).map((lz) => { const b = shipHalfBeam(lz) - 0.25, t = topAt(lz); return [[X(sd * b), t - 1.1, Z(lz)], [X(sd * b), t, Z(lz)]]; });
    emitGrid(rows, () => hull, { ref: [S.x, DY + 0.5, S.z], inward: true, colorAt: () => [0.85, 0.85, 0.82] });
  }

  // ---------------- decks
  deck.color(0.42, 0.18, 0.14);
  const deckRows = stations(S.fc + 1, S.stern - 0.6, 4).map((lz) => { const b = shipHalfBeam(lz) - 0.26; return [[X(-b), DY, Z(lz)], [X(b), DY, Z(lz)]]; });
  emitGrid(deckRows, () => deck, { ref: [S.x, 0, S.z] });
  const fcRows = stations(S.bow + 2, S.fc + 1, 2).map((lz) => { const b = Math.max(0.2, shipHalfBeam(lz) - 0.26); return [[X(-b), DY + 2.4, Z(lz)], [X(b), DY + 2.4, Z(lz)]]; });
  emitGrid(fcRows, () => deck, { ref: [S.x, 0, S.z] });
  // floors (walkable) and the hull's sides (solid to boats, swimmers and anyone on deck)
  col.addBox({ minX: X(-12.8), maxX: X(12.8), minZ: Z(S.fc), maxZ: Z(S.stern - 0.4), minY: DY - 1.2, maxY: DY, type: 'deck' });
  for (let k = 0; k < 4; k++) {
    const z0 = S.bow + 3 + k * (S.fc - S.bow - 3) / 4, z1 = z0 + (S.fc - S.bow - 3) / 4;
    const b = shipHalfBeam(z0 + (z1 - z0) * 0.3) - 0.3;
    col.addBox({ minX: X(-b), maxX: X(b), minZ: Z(z0), maxZ: Z(z1 + 0.5), minY: DY + 1.2, maxY: DY + 2.4, type: 'deck' });
  }
  const g = S.gangway;
  const wall = (x0, z0, x1, z1, y0, y1, type = 'hull') => col.addBox({ minX: Math.min(x0, x1), maxX: Math.max(x0, x1), minZ: Math.min(z0, z1), maxZ: Math.max(z0, z1), minY: y0, maxY: y1, type });
  for (const sd of [1, -1]) {
    // parallel body: straight walls (a gap in the port rail for the gangway)
    wall(X(sd * 12.7), Z(-48), X(sd * 13.4), Z(S.stern), -9, DY, 'hull');
    if (sd < 0) { wall(X(-12.7), Z(-48), X(-13.4), Z(g.land[0]), DY - 0.1, DY + 1.1, 'rail'); wall(X(-12.7), Z(g.land[1]), X(-13.4), Z(S.stern), DY - 0.1, DY + 1.1, 'rail'); }
    else wall(X(12.7), Z(-48), X(13.4), Z(S.stern), DY - 0.1, DY + 1.1, 'rail');
    // the bow: angled segments
    const segs = 5;
    for (let k = 0; k < segs; k++) {
      const za = -48 + (S.bow + 2 + 48) * k / segs, zb = -48 + (S.bow + 2 + 48) * (k + 1) / segs;
      const xa = shipHalfBeam(za), xb = shipHalfBeam(zb);
      const cx = (xa + xb) / 2, cz = (za + zb) / 2, len = Math.hypot(xb - xa, zb - za);
      const yaw = Math.atan2((xb - xa) * sd, zb - za);
      col.addOBox({ cx: X(sd * cx), cz: Z(cz), hx: 0.35, hz: len / 2 + 0.3, yaw, minY: -9, maxY: topAt(cz), type: 'hull' });
    }
  }
  wall(X(-13.4), Z(S.stern - 0.4), X(13.4), Z(S.stern + 0.4), -9, DY + 1.1, 'hull');
  // the forecastle break: a wall with a ramp up the middle
  wall(X(-12), Z(S.fc - 0.4), X(-2.2), Z(S.fc + 0.4), DY - 0.2, DY + 2.4 + 1.1, 'rail'); wall(X(2.2), Z(S.fc - 0.4), X(12), Z(S.fc + 0.4), DY - 0.2, DY + 2.4 + 1.1, 'rail');
  col.addDeck({ ax: X(0), az: Z(S.fc + 8), ay: DY, bx: X(0), bz: Z(S.fc - 0.2), by: DY + 2.4, hl: 2, hr: 2 });
  steel.color(0.35, 0.36, 0.38);
  { const P = (lx, lz, y) => [X(lx), y, Z(lz)]; steel.poly([P(-2, S.fc + 8, DY + 0.02), P(2, S.fc + 8, DY + 0.02), P(2, S.fc - 0.2, DY + 2.42), P(-2, S.fc - 0.2, DY + 2.42)], [0, 0.95, 0.3]); }
  // the bulwark rails, drawn: posts and two bars
  steel.color(0.8, 0.8, 0.78);
  for (const sd of [1, -1]) for (let lz = S.bow + 6; lz < S.stern - 1; lz += 2.5) {
    if (sd < 0 && lz > g.land[0] - 0.5 && lz < g.land[1] + 0.5) continue;
    const b = shipHalfBeam(lz) - 0.2, t = topAt(lz);
    steel.box(X(sd * b) - 0.04, t, Z(lz) - 0.04, X(sd * b) + 0.04, t + 0.5, Z(lz) + 0.04);
  }

  // ---------------- cargo: hatch covers and container stacks, five bays
  const COLORS = [[0.69, 0.23, 0.18], [0.12, 0.38, 0.55], [0.07, 0.48, 0.4], [0.83, 0.67, 0.05], [0.42, 0.2, 0.51], [0.73, 0.29, 0], [0.55, 0.55, 0.55]];
  const stacks = [];
  const cont = new Part();
  for (const bz of S.bays) {
    steel.color(0.25, 0.3, 0.33);
    steel.box(X(-10.3), DY, Z(bz - S.bayHalf), X(10.3), DY + 0.8, Z(bz + S.bayHalf));
    wall(X(-10.3), Z(bz - S.bayHalf), X(10.3), Z(bz + S.bayHalf), DY - 0.5, DY + 0.8, 'hatch');
    for (let row = -1; row <= 1; row++) for (let c = -3; c <= 3; c++) {
      const cx = c * 2.6, cz = bz + row * 6.3;
      const n = rng.int(1, 4);
      const base = DY + 0.8;
      for (let k = 0; k < n; k++) {
        const cc = rng.pick(COLORS);
        cont.color(...cc);
        const y0 = base + k * 2.6;
        cont.box(X(cx - 1.21), y0, Z(cz - 3.02), X(cx + 1.21), y0 + 2.58, Z(cz + 3.02));
        // corrugation ribs and the door end
        cont.color(cc[0] * 0.8, cc[1] * 0.8, cc[2] * 0.8);
        for (let r = -2; r <= 2; r++) cont.box(X(cx - 1.23), y0 + 0.15, Z(cz + r * 1.1 - 0.05), X(cx + 1.23), y0 + 2.43, Z(cz + r * 1.1 + 0.05));
      }
      stacks.push({ lx: cx, lz: cz, n, bay: bz, row });
      wall(X(cx - 1.22), Z(cz - 3.03), X(cx + 1.22), Z(cz + 3.03), DY + 0.7, base + n * 2.6, 'container');
    }
  }
  // lashing bridges between the bays (walkways across the ship between the stacks)
  steel.color(0.75, 0.55, 0.12);
  for (let k = 0; k < S.bays.length - 1; k++) { const gz = (S.bays[k] + S.bays[k + 1]) / 2; for (const sd of [1, -1]) steel.box(X(sd * 10.4) - 0.06, DY, Z(gz) - 0.06, X(sd * 10.4) + 0.06, DY + 3.2, Z(gz) + 0.06); }

  // ---------------- forecastle kit: windlasses, bollards, a foremast
  steel.color(0.2, 0.22, 0.24);
  for (const sd of [1, -1]) {
    steel.geo(new THREE.CylinderGeometry(0.6, 0.6, 1.4, 12), new THREE.Matrix4().makeRotationZ(Math.PI / 2).setPosition(X(sd * 3.5), DY + 3.1, Z(-80)));
    for (const lz of [-74, -86]) steel.geo(new THREE.CylinderGeometry(0.25, 0.3, 0.7, 10), new THREE.Matrix4().setPosition(X(sd * shipHalfBeam(lz) - sd * 1.6), DY + 2.75, Z(lz)));
  }
  steel.color(0.85, 0.85, 0.82);
  steel.geo(new THREE.CylinderGeometry(0.25, 0.35, 12, 10), new THREE.Matrix4().setPosition(X(0), DY + 8.4, Z(-86)));
  lamp.color(1, 1, 0.9); lamp.box(X(-0.2), DY + 14.4, Z(-86.2), X(0.2), DY + 14.8, Z(-85.8));
  col.addCircle({ x: X(0), z: Z(-86), r: 0.4, h: DY + 14, y0: DY + 2 });

  // ---------------- superstructure: four stepped levels, balconies, ramps, the bridge
  const L = S.levels, front = [56, 58, 60, 62], back = 74;
  const windows = (y0, y1, zFront, xw) => {
    glass.color(1, 1, 1);
    for (let x = -xw + 1.2; x <= xw - 1.2; x += 2.2) glass.box(X(x - 0.6), y0 + 1.0, Z(zFront) - 0.03, X(x + 0.6), y1 - 0.8, Z(zFront) + 0.01);
    for (const sd of [1, -1]) for (let lz = zFront + 1.5; lz < back - 1; lz += 2.4) glass.box(X(sd * xw) - 0.01, y0 + 1.0, Z(lz - 0.4), X(sd * xw) + 0.03 * sd + 0.01, y1 - 0.8, Z(lz + 0.4));
  };
  for (let k = 0; k < 3; k++) {
    white.color(0.93, 0.93, 0.9);
    white.box(X(-10), L[k], Z(front[k]), X(10), L[k] + 3, Z(back));
    wall(X(-10), Z(front[k]), X(10), Z(back), L[k] - 0.2, L[k] + 3, 'superstructure');
    windows(L[k], L[k] + 3, front[k], 10);
    // balcony rail along the front edge of the level above's floor (gaps where a ramp arrives)
  }
  // balconies (the stepped roofs): rails down the sides and along the front, with gaps where the ramps arrive
  steel.color(0.85, 0.85, 0.82);
  const rail = (x0, x1, lz0, lz1, y) => {
    wall(X(x0), Z(lz0), X(x1), Z(lz1), y - 0.1, y + 1.1, 'rail');
    const alongX = Math.abs(x1 - x0) > Math.abs(lz1 - lz0);
    const n = Math.max(1, Math.round((alongX ? Math.abs(x1 - x0) : Math.abs(lz1 - lz0)) / 1.2));
    for (let k = 0; k <= n; k++) { const t = k / n, x = X(x0 + (x1 - x0) * t), z = Z(lz0 + (lz1 - lz0) * t); steel.box(x - 0.03, y, z - 0.03, x + 0.03, y + 1.05, z + 0.03); }
    steel.box(X(Math.min(x0, x1)) - 0.03, y + 1.0, Z(Math.min(lz0, lz1)) - 0.03, X(Math.max(x0, x1)) + 0.03, y + 1.06, Z(Math.max(lz0, lz1)) + 0.03);
  };
  const gapsAt = [null, [0, 2], [8.4, 10], [-10, -8.4]];
  for (let k = 1; k <= 3; k++) {
    const y = L[k], z0 = front[k - 1], z1 = front[k];
    for (const sd of [1, -1]) rail(sd * 10, sd * 10.05, z0, z1 + (k === 3 ? 3 : 0), y);
    const [ga, gb] = gapsAt[k];
    if (ga > -10) rail(-10, ga, z0, z0 + 0.05, y);
    if (gb < 10) rail(gb, 10, z0, z0 + 0.05, y);
  }
  // the bridge: a room (walls with a door, big windows), wings, a roof with the radar mast
  {
    const y = L[3], z0 = front[3], H = 3;
    white.color(0.93, 0.93, 0.9);
    // front wall: below the windows, the window mullions, above; a door in the middle
    white.box(X(-10), y, Z(z0), X(-1.1), y + 1.1, Z(z0 + 0.25)); white.box(X(1.1), y, Z(z0), X(10), y + 1.1, Z(z0 + 0.25));
    white.box(X(-10), y + 2.5, Z(z0), X(10), y + H, Z(z0 + 0.25));
    for (const x of [-10, -6, -1.3, 1.1, 6, 9.8]) white.box(X(x), y + 1.1, Z(z0), X(x + 0.2), y + 2.5, Z(z0 + 0.25));
    glass.color(1, 1, 1);
    for (const [a, b] of [[-9.8, -6], [-5.8, -1.3], [1.3, 6], [6.2, 9.8]]) glass.box(X(a), y + 1.1, Z(z0 + 0.08), X(b), y + 2.5, Z(z0 + 0.14));
    white.box(X(-10), y, Z(back - 0.25), X(10), y + H, Z(back));
    for (const sd of [1, -1]) { white.box(X(sd > 0 ? 9.75 : -10), y, Z(z0), X(sd > 0 ? 10 : -9.75), y + H, Z(back)); glass.box(X(sd > 0 ? 10 : -10.05), y + 1.1, Z(z0 + 1), X(sd > 0 ? 10.05 : -10), y + 2.5, Z(back - 2)); }
    white.box(X(-10.5), y + H, Z(z0 - 0.5), X(10.5), y + H + 0.3, Z(back + 0.3));
    // wings
    for (const sd of [1, -1]) { white.box(X(sd > 0 ? 10 : -13), y - 0.3, Z(z0), X(sd > 0 ? 13 : -10), y, Z(z0 + 3)); }
    // walls for the collision (the door gap is x -1.1..1.1)
    wall(X(-10), Z(z0), X(-1.1), Z(z0 + 0.25), y - 0.1, y + H, 'wall'); wall(X(1.1), Z(z0), X(10), Z(z0 + 0.25), y - 0.1, y + H, 'wall');
    wall(X(-10), Z(back - 0.25), X(10), Z(back), y - 0.1, y + H, 'wall');
    wall(X(-10), Z(z0), X(-9.75), Z(back), y - 0.1, y + H, 'wall'); wall(X(9.75), Z(z0), X(10), Z(back), y - 0.1, y + H, 'wall');
    wall(X(-10.5), Z(z0 - 0.5), X(10.5), Z(back + 0.3), y + H, y + H + 0.3, 'roof');
    // inside: the helm console, a chart table, the wheel, and the safe against the back wall
    steel.color(0.18, 0.2, 0.22);
    steel.box(X(-6), y, Z(z0 + 0.6), X(6), y + 1.05, Z(z0 + 1.5));
    steel.color(0.12, 0.35, 0.2); steel.box(X(-5.5), y + 1.05, Z(z0 + 0.7), X(5.5), y + 1.1, Z(z0 + 1.3));
    steel.color(0.5, 0.35, 0.2); steel.box(X(-7), y, Z(back - 5), X(-4), y + 1.0, Z(back - 3));
    steel.color(0.3, 0.3, 0.32); steel.geo(new THREE.TorusGeometry(0.35, 0.04, 6, 16), new THREE.Matrix4().makeRotationX(-0.3).setPosition(X(0), y + 1.3, Z(z0 + 1.9)));
    steel.color(0.24, 0.26, 0.28); steel.box(X(3.8), y, Z(back - 1.2), X(5.2), y + 1.4, Z(back - 0.25));
    steel.color(0.6, 0.6, 0.62); steel.geo(new THREE.CylinderGeometry(0.12, 0.12, 0.05, 12), new THREE.Matrix4().makeRotationX(Math.PI / 2).setPosition(X(4.5), y + 0.8, Z(back - 1.23)));
    wall(X(-6), Z(z0 + 0.6), X(6), Z(z0 + 1.5), y - 0.1, y + 1.05, 'console');
    wall(X(3.8), Z(back - 1.2), X(5.2), Z(back - 0.25), y - 0.1, y + 1.4, 'safe');
    // radar mast and a horn on the roof
    steel.color(0.85, 0.85, 0.82); steel.geo(new THREE.CylinderGeometry(0.15, 0.2, 5, 8), new THREE.Matrix4().setPosition(X(0), y + H + 2.8, Z(back - 4)));
    steel.color(0.2, 0.2, 0.22); steel.box(X(-1.8), y + H + 4.8, Z(back - 4.15), X(1.8), y + H + 5.05, Z(back - 3.85));
    lamp.color(1, 0.2, 0.1); lamp.box(X(-0.12), y + H + 5.3, Z(back - 4.12), X(0.12), y + H + 5.55, Z(back - 3.88));
    S.safe = { x: X(4.5), z: Z(back - 2.2), y };
    S.bridgeDoor = { x: X(0), z: Z(z0 - 0.8), y };
  }
  // funnel with the company colours
  white.color(0.72, 0.1, 0.08); white.geo(new THREE.CylinderGeometry(2.6, 3, 9, 16), new THREE.Matrix4().makeScale(1, 1, 1.5).setPosition(X(0), L[3] + 7.5, Z(79)));
  white.color(0.08, 0.08, 0.08); white.geo(new THREE.CylinderGeometry(2.62, 2.62, 1.2, 16), new THREE.Matrix4().makeScale(1, 1, 1.5).setPosition(X(0), L[3] + 12.4, Z(79)));
  wall(X(-3), Z(74), X(3), Z(84), DY, L[3] + 13, 'funnel');
  // lifeboats on davits at the stern quarters
  for (const sd of [1, -1]) {
    white.color(0.95, 0.45, 0.1);
    roundBox(white, X(sd * 11.2) - 1.1, X(sd * 11.2) + 1.1, L[1] + 0.4, L[1] + 2.2, Z(76), Z(83), 0.6, 3);
    steel.color(0.8, 0.8, 0.8);
    for (const lz of [76.5, 82.5]) steel.box(X(sd * 10.4) - 0.1, L[0] + 3, Z(lz) - 0.1, X(sd * 10.4) + 0.1, L[1] + 3.2, Z(lz) + 0.1);
  }

  // ramps: main deck -> A (in front of the block), A -> B and B -> bridge along the balconies
  const ramp = (x0, x1, lz, y0, y1, w = 1) => {
    col.addDeck({ ax: X(x0), az: Z(lz), ay: y0, bx: X(x1), bz: Z(lz), by: y1, hl: w, hr: w });
    steel.color(0.4, 0.42, 0.45);
    steel.poly([[X(x0), y0 + 0.03, Z(lz - w)], [X(x1), y1 + 0.03, Z(lz - w)], [X(x1), y1 + 0.03, Z(lz + w)], [X(x0), y0 + 0.03, Z(lz + w)]], [0, 1, 0]);
    steel.color(0.8, 0.8, 0.78);
    const n = Math.ceil(Math.abs(x1 - x0) / 1.5);
    for (let k = 0; k <= n; k++) { const t = k / n, x = X(x0 + (x1 - x0) * t), y = y0 + (y1 - y0) * t; steel.box(x - 0.03, y, Z(lz - w) - 0.03, x + 0.03, y + 1.0, Z(lz - w) + 0.03); }
    const P = (t) => [X(x0 + (x1 - x0) * t), y0 + (y1 - y0) * t + 1.0, Z(lz - w)];
    steel.poly([[...P(0)], [...P(1)], [P(1)[0], P(1)[1] + 0.06, P(1)[2]], [P(0)[0], P(0)[1] + 0.06, P(0)[2]]], [0, 0, -1]);
  };
  ramp(-9, 1, 55, L[0], L[1]);
  ramp(2, 10, 57, L[1], L[2]);
  ramp(-2, -10, 59, L[2], L[3]);
  S.route = [[-9, 55, L[0]], [1, 55, L[1]], [1.5, 57, L[1]], [2, 57, L[1]], [10, 57, L[2]], [9, 59, L[2]], [-2, 59, L[2]], [-10, 59, L[3]], [-9, 61, L[3]], [0, 61, L[3]], [0, 64, L[3]]];

  // ---------------- the gangway: from the quay edge, slanting out over the water to a landing at the rail
  {
    const ax = X(g.xFoot), az = Z(g.z0), bx = X(g.x), bz = Z(g.z1);
    col.addDeck({ ax, az, ay: 0.2, bx, bz, by: DY, hl: 1.2, hr: 1.2 });
    const dx = bx - ax, dz = bz - az, len = Math.hypot(dx, dz), ux = dx / len, uz = dz / len, nx = -uz, nz = ux;
    const P = (t, s, y) => [ax + dx * t + nx * s, y, az + dz * t + nz * s];
    steel.color(0.55, 0.56, 0.58);
    steel.poly([P(0, -1.2, 0.23), P(0, 1.2, 0.23), P(1, 1.2, DY + 0.03), P(1, -1.2, DY + 0.03)], [0, 1, 0]);
    steel.box(ax - 1.3, -3, az - 0.3, ax + 1.3, 0.2, az + 0.3); // (the foot, on the quay)
    steel.color(0.85, 0.85, 0.82);
    for (const sd of [-1.2, 1.2]) {
      for (let k = 0; k <= 14; k++) { const t = k / 14, [x, y, z] = P(t, sd, 0.2 + (DY - 0.2) * t); steel.box(x - 0.03, y, z - 0.03, x + 0.03, y + 1.0, z + 0.03); }
      steel.poly([P(0, sd, 1.2), P(1, sd, DY + 1.0), P(1, sd, DY + 1.06), P(0, sd, 1.26)], [nx, 0, nz]);
    }
    // the landing across to the deck
    const lx0 = bx - 1.3, lx1 = X(-11.8);
    steel.color(0.55, 0.56, 0.58); steel.box(lx0, DY - 0.15, Z(g.land[0]), lx1, DY, Z(g.land[1]));
    col.addBox({ minX: lx0, maxX: lx1, minZ: Z(g.land[0]), maxZ: Z(g.land[1]), minY: DY - 1, maxY: DY, type: 'deck' });
    // supports down into the water
    steel.color(0.3, 0.3, 0.32);
    for (const t of [0.35, 0.7]) { const [x, y, z] = P(t, 0, 0.2 + (DY - 0.2) * t); steel.box(x - 0.1, -3, z - 0.1, x + 0.1, y, z + 0.1); }
    S.gangwayFoot = { x: ax, z: az - 2.5, y: 0 };
    S.gangwayTop = { x: X(-10.5), z: Z((g.land[0] + g.land[1]) / 2), y: DY };
  }

  // ---------------- deck lights (they glow at night)
  for (const lz of [-60, -18, 25, 50]) for (const sd of [1, -1]) {
    steel.color(0.3, 0.3, 0.32); steel.box(X(sd * 12.2) - 0.08, DY, Z(lz) - 0.08, X(sd * 12.2) + 0.08, DY + 4, Z(lz) + 0.08);
    lamp.color(1, 0.95, 0.8); lamp.box(X(sd * 11.9) - 0.25, DY + 3.8, Z(lz) - 0.18, X(sd * 11.9) + 0.25, DY + 4.0, Z(lz) + 0.18);
  }

  const mat = (key, o = {}) => patch(new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.7, metalness: 0.25, ...o }), { key });
  const mk = (part, m, cast = true) => { const me = new THREE.Mesh(part.build(), m); me.castShadow = cast; me.receiveShadow = true; root.add(me); return me; };
  mk(hull, mat('shiphull', { roughness: 0.55, metalness: 0.35 }));
  mk(deck, mat('shipdeck2', { roughness: 0.85, metalness: 0.1 }), false);
  mk(steel, mat('shipsteel', { roughness: 0.6, metalness: 0.4 }));
  mk(white, mat('shipwhite', { roughness: 0.6, metalness: 0.1 }));
  mk(cont, mat('shipcontainers', { roughness: 0.75, metalness: 0.3 }));
  const glassMat = patch(new THREE.MeshStandardMaterial({ vertexColors: true, color: 0x1a2733, roughness: 0.08, metalness: 0.6 }), { key: 'shipglass', fragEnd: 'atgRefl = 0.35;' });
  mk(glass, glassMat, false);
  const lampMat = new THREE.MeshBasicMaterial({ vertexColors: true, color: 0xffffff });
  const lampMesh = mk(lamp, lampMat, false);
  S.stacks = stacks;
  game.scene.add(root);
  return {
    root, spec: S,
    update(dt, night) { lampMat.color.setScalar(0.4 + night * 3); },
    // on board: inside the hull's footprint and up on a deck
    aboard(p) { const lz = p.z - S.z, lx = p.x - S.x; return lz > S.bow && lz < S.stern && Math.abs(lx) < shipHalfBeam(lz) && p.y > DY - 1.5; },
    toWorld: (lx, lz) => [X(lx), Z(lz)],
  };
}
