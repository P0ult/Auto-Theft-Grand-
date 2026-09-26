// AUTO THEFT GRAND — storyline. 26 missions in 6 chapters.
// Andre "Dre" Castillo returns to Los Soles after his little brother Tino is killed.
import * as THREE from 'three';
import { RouteDriver, RaceDriver, MissionFail } from './missions.js';
import { laneGeom, segmentValid, nearestLane } from './traffic.js';
import { XS, ZS, HALF_ROAD, CITY } from '../world/citymap.js';
import { randomAppearance } from '../entities/humanoid.js';
import { copAppearance } from './police.js';
import { GANGS } from './peds.js';
import { WEAPONS } from './weapondefs.js';
import { rand, randInt, pick, clamp, RNG } from '../core/utils.js';
import { TOWNS, AIRFIELD } from '../world/worldgen.js';

// ------------------------------------------------------------------ cast
const CAST = {
  lou: { female: false, skin: 0x5f3a24, hair: 0x111111, hairStyle: 'cap', hat: 0xf2b705, shirt: 0xf2b705, shirtType: 'tee', pants: 0x2b2b2b, shorts: false, shoes: 0xffffff, build: 1.22, height: 1.04, glasses: false, beard: true, jacketColor: 0x111111, bandana: null, uniform: null },
  marisol: { female: true, skin: 0x8a5536, hair: 0x1a1a1a, hairStyle: 'ponytail', hat: null, shirt: 0xf2f2f2, shirtType: 'jacket', jacketColor: 0xc9a227, pants: 0x1d3b5c, shorts: false, shoes: 0xeeeeee, build: 0.95, height: 0.96, glasses: false, beard: false, bandana: null, uniform: null },
  deacon: { female: false, skin: 0x3f2618, hair: 0x111111, hairStyle: 'short', hat: null, shirt: 0xf2b705, shirtType: 'jacket', jacketColor: 0x1a1a1a, pants: 0x222222, shorts: false, shoes: 0x111111, build: 1.0, height: 1.02, glasses: true, beard: false, bandana: null, uniform: null },
  voss: { female: false, skin: 0xf1c7a5, hair: 0x4a2c16, hairStyle: 'short', hat: null, shirt: 0xf2f2f2, shirtType: 'jacket', jacketColor: 0x5a4632, pants: 0x4a3b2a, shorts: false, shoes: 0x2b1a10, build: 1.1, height: 1.03, glasses: true, beard: false, bandana: null, uniform: null },
  ruiz: { female: false, skin: 0xc68863, hair: 0x111111, hairStyle: 'buzz', hat: null, shirt: 0x9aa7b5, shirtType: 'jacket', jacketColor: 0x3d405b, pants: 0x2b2d42, shorts: false, shoes: 0x111111, build: 1.12, height: 1.0, glasses: false, beard: true, bandana: null, uniform: null },
  rico: { female: false, skin: 0xa86b4a, hair: 0x111111, hairStyle: 'cap', hat: 0x222222, shirt: 0x2a4d69, shirtType: 'jacket', jacketColor: 0xe07a1f, pants: 0x2a4d69, shorts: false, shoes: 0x333333, build: 1.0, height: 0.98, glasses: false, beard: false, bandana: null, uniform: null },
  maddox: { female: false, skin: 0xf1c7a5, hair: 0xb08a52, hairStyle: 'long', hat: null, shirt: 0xffffff, shirtType: 'long', pants: 0xe9d8a6, shorts: false, shoes: 0x8b5a2b, build: 0.98, height: 1.0, glasses: true, beard: true, jacketColor: 0xffffff, bandana: null, uniform: null },
  salazar: { female: false, skin: 0xc68863, hair: 0x111111, hairStyle: 'bald', hat: null, shirt: 0xc1121f, shirtType: 'jacket', jacketColor: 0x111111, pants: 0x111111, shorts: false, shoes: 0x5a0a0a, build: 1.15, height: 1.02, glasses: true, beard: true, bandana: null, uniform: null },
  chino: { female: false, skin: 0xc68863, hair: 0x111111, hairStyle: 'buzz', hat: null, shirt: 0xc1121f, shirtType: 'tank', pants: 0x1d3557, shorts: true, shoes: 0xffffff, build: 1.05, height: 1.0, glasses: true, beard: false, bandana: 0xc1121f, jacketColor: 0x111111, uniform: null },
  elseco: { female: false, skin: 0xc68863, hair: 0x999999, hairStyle: 'short', hat: null, shirt: 0x7a1f1f, shirtType: 'jacket', jacketColor: 0xe9d8a6, pants: 0xe9d8a6, shorts: false, shoes: 0x8b5a2b, build: 1.12, height: 1.0, glasses: true, beard: true, bandana: null, uniform: null },
  benny: { female: false, skin: 0xe0ac87, hair: 0x111111, hairStyle: 'short', hat: null, shirt: 0x4a78b5, shirtType: 'long', pants: 0x6d5c43, shorts: false, shoes: 0x3b2f2a, build: 0.95, height: 0.97, glasses: true, beard: false, bandana: null, jacketColor: 0x111111, uniform: null },
  king: () => randomAppearance(new RNG((Math.random() * 1e9) | 0), { female: false, shirt: 0xf2b705, shirtType: pick(['tee', 'tank', 'jacket']), jacketColor: 0x1a1a1a, hairStyle: pick(['cap', 'buzz', 'short']), hat: 0xf2b705, bandana: Math.random() < 0.5 ? 0xf2b705 : null }),
};
const look = (k) => (typeof CAST[k] === 'function' ? CAST[k]() : { ...CAST[k] });

// ------------------------------------------------------------------ helpers
function laneSpot(i, j, di, dj, t = 0.5, lane = 1) {
  if (!segmentValid(i, j, di, dj)) return null;
  const g = laneGeom(i, j, di, dj, lane);
  const s = g.len * t;
  return { x: g.ax + g.dx * s, z: g.az + g.dz * s, yaw: Math.atan2(g.dx, g.dz) };
}
// Nearest point on a city street lane to (x,z)
let NET = null;
function roadNear(x, z, lane = 1) {
  if (NET) {
    const st = nearestLane(NET, x, z, null, (e) => !!e.grid);
    if (st) {
      const pts = NET.lanePath(st.e, st.dir, Math.min(lane, (st.dir === 0 ? st.e.lanesF : st.e.lanesB) - 1));
      let bi = 0, bd = Infinity;
      for (let i = 0; i < pts.length - 1; i++) { const d = (pts[i][0] - x) ** 2 + (pts[i][2] - z) ** 2; if (d < bd) { bd = d; bi = i; } }
      bi = clamp(bi, 1, Math.max(1, pts.length - 3));
      const a = pts[bi], b = pts[Math.min(pts.length - 1, bi + 1)];
      return { x: a[0], z: a[2], yaw: Math.atan2(b[0] - a[0], b[2] - a[2]) };
    }
  }
  let bi = 0, bd = Infinity;
  for (let i = 0; i < XS.length; i++) { const d = Math.abs(XS[i] - x); if (d < bd) { bd = d; bi = i; } }
  let bj = 0, bdz = Infinity;
  for (let j = 0; j < ZS.length; j++) { const d = Math.abs(ZS[j] - z); if (d < bdz) { bdz = d; bj = j; } }
  if (bd < bdz) { // N-S road
    const zz = clamp(z, ZS[0] + HALF_ROAD + 6, ZS[ZS.length - 1] - HALF_ROAD - 6);
    return { x: XS[bi] + 5.4 * (x > XS[bi] ? 1 : -1), z: zz, yaw: x > XS[bi] ? Math.PI : 0 };
  }
  const xx = clamp(x, XS[0] + HALF_ROAD + 6, XS[XS.length - 1] - HALF_ROAD - 6);
  return { x: xx, z: ZS[bj] + 5.4 * (z > ZS[bj] ? 1 : -1), yaw: z > ZS[bj] ? Math.PI / 2 : -Math.PI / 2 };
}
function offset(p, dx, dz) { return { x: p.x + dx, z: p.z + dz }; }
function ring(cx, cz, n, r, phase = 0) { const out = []; for (let i = 0; i < n; i++) { const a = phase + i / n * Math.PI * 2; out.push({ x: cx + Math.cos(a) * r, z: cz + Math.sin(a) * r }); } return out; }

// Chase driver: follow roads toward the player, then ram / pace alongside.
class ChaseDriver extends RouteDriver {
  constructor(game, veh, opts = {}) { super(game, veh, game.player.pos, { speed: opts.speed ?? 30 }); this.pace = opts.pace ?? 8; this.rev = 0; }
  _chooseNext(cur) { this.dest = this.game.player.vehicle ? this.game.player.vehicle.pos : this.game.player.pos; this.route = null; return super._chooseNext(cur); }
  update(dt) {
    const v = this.veh, g = this.game;
    const tp = g.player.vehicle ? g.player.vehicle.pos : g.player.pos;
    const d = Math.hypot(tp.x - v.pos.x, tp.z - v.pos.z);
    if (d > 70) { this.dest = tp; this.cruise = 32; this.arrived = false; LaneLike.update.call(this, dt); return; }
    const inp = v.input;
    const [lx, lz] = v.worldToLocal(tp.x, tp.z);
    const ang = Math.atan2(lx, Math.max(0.5, lz));
    let steer = ang / (v.def.steer * 0.7);
    const probe = (a) => { const h = g.collision.raycast(v.pos.x, v.pos.y + 0.8, v.pos.z, Math.sin(v.yaw + a), 0, Math.cos(v.yaw + a), 14, { ignoreProps: true }); return h ? h.t : 14; };
    if (probe(0) < 8) steer += probe(0.4) > probe(-0.4) ? 1 : -1;
    inp.steer = clamp(steer, -1, 1);
    if (this.rev > 0) { this.rev -= dt; inp.throttle = 0; inp.brake = 1; inp.steer = -inp.steer; return; }
    if (Math.abs(v.speed) < 1 && d > 10) { this.stuck += dt; if (this.stuck > 1.5) { this.rev = 1.2; this.stuck = 0; } } else this.stuck = 0;
    const tv = g.player.vehicle ? g.player.vehicle.speedAbs : 0;
    const want = d < this.pace ? tv : tv + 10 + d * 0.3;
    inp.throttle = v.speed < want ? 1 : 0; inp.brake = v.speed > want + 5 ? 0.5 : 0;
    inp.handbrake = Math.abs(ang) > 1.2 && v.speed > 10;
  }
}
const LaneLike = Object.getPrototypeOf(RouteDriver.prototype);

function chaseCar(m, type, x, z, yaw, gang, shooters = 1, lookFn = null) {
  const v = m.car(type, x, z, yaw, { color: lookFn ? 0xc2a878 : gang === 'vipers' ? 0x9d0208 : 0x1f7a8c });
  const drv = m.ped(x, z, { appearance: lookFn ? lookFn() : gangLook(gang), brain: 'script' });
  v.putIn(drv, 0);
  const guns = [];
  for (let k = 0; k < shooters; k++) {
    const s = m.ped(x, z, { appearance: lookFn ? lookFn() : gangLook(gang), brain: 'script' });
    s.giveWeapon('smg', 999); s.equip('smg');
    v.putIn(s, k + 1);
    m.driveBy(s);
    guns.push(s);
  }
  v.ai = new ChaseDriver(m.game, v);
  m.blipEntity(v, 0xff3030, 'car', true);
  return { v, drv, guns };
}
// Los Secos: the desert cartel out of Puerto Seco (fight as Cuervo gang members, dressed for the dust)
function secoLook() {
  return randomAppearance(new RNG((Math.random() * 1e9) | 0), { female: false, shirt: pick([0xc2a878, 0xe9d8a6, 0x8d6e4a]), shirtType: pick(['long', 'jacket', 'tee']), jacketColor: 0x5a4632, pants: pick([0x4e4a45, 0x6d5c43, 0x3b2f2a]), hairStyle: pick(['cap', 'short', 'buzz']), hat: 0xc2a878, bandana: Math.random() < 0.4 ? 0x7a1f1f : null, glasses: Math.random() < 0.5 });
}
function secos(m, pts, weapons, opts = {}) {
  return pts.map((p, i) => {
    const e = m.enemy(p.x, p.z, { gang: 'cuervos', weapon: weapons[i % weapons.length], guard: opts.guard ?? true, face: opts.face, health: opts.health ?? 100, appearance: secoLook(), accuracy: opts.accuracy ?? 0.4 });
    if (p.y != null) e.setPosition(p.x, p.y, p.z);
    return e;
  });
}
// A glowing checkpoint ring in the sky for aircraft (resolves when flown through)
function airRing(m, x, z, alt, opts = {}) {
  const g = m.game;
  const gy = Math.max(g.map.groundHeight(x, z), 0);
  const y = gy + alt, R = opts.r ?? 16;
  const mesh = new THREE.Mesh(new THREE.TorusGeometry(R, 0.9, 8, 40), new THREE.MeshBasicMaterial({ color: new THREE.Color(2.6, 1.9, 0.35), transparent: true, opacity: 0.85, depthWrite: false }));
  mesh.position.set(x, y, z);
  if (opts.next) mesh.rotation.y = Math.atan2(opts.next.x - x, opts.next.z - z);
  mesh.renderOrder = 6;
  g.scene.add(mesh);
  const blip = { x, z, color: 0xffd23f, icon: 'flag' };
  g.blips.add(blip);
  m.blips.push(blip);
  const c = new THREE.Vector3(x, y, z);
  m.currentRing = { c };
  if (opts.text) m.objective(opts.text);
  const done = () => { g.scene.remove(mesh); mesh.geometry.dispose(); mesh.material.dispose(); g.blips.delete(blip); m.currentRing = null; };
  return m.until(() => {
    const v = m.player.vehicle;
    const p = v ? (v.cg ? v.cg() : v.pos) : m.player.pos;
    return p.distanceTo(c) < R * 1.15;
  }).then(() => { done(); g.audio?.play('checkpoint'); }, (e) => { done(); throw e; });
}
const station = (game, key) => game.map.roadInfo?.rail?.stations.find((s) => s.key === key);

function gangLook(gang) {
  const g = GANGS[gang];
  return randomAppearance(new RNG((Math.random() * 1e9) | 0), { female: false, shirt: g.color, shirtType: pick(['tee', 'tank', 'jacket']), jacketColor: 0x1a1a1a, bandana: Math.random() < 0.6 ? g.color : null, hairStyle: pick(['cap', 'buzz', 'bald']), hat: g.color });
}
function crew(m, gang, pts, weapons, opts = {}) {
  return pts.map((p, i) => m.enemy(p.x, p.z, { gang, weapon: weapons[i % weapons.length], guard: opts.guard ?? true, face: opts.face, health: opts.health ?? 100, appearance: gangLook(gang), accuracy: opts.accuracy }));
}
// Guards stay put until the player comes within range (or gets shot at)
function aggroWhenNear(m, list, range = 30) {
  return m.tick(() => {
    const p = m.player.vehicle ? m.player.vehicle.pos : m.player.pos;
    for (const e of list) {
      if (e.dead || e.state === 'attack') continue;
      if (Math.hypot(e.pos.x - p.x, e.pos.z - p.z) < range || e.health < e.maxHealth) { for (const o of list) if (!o.dead) { o.threat = m.player; o.setState('attack'); } break; }
    }
  });
}
// Followers with guns shoot at mission enemies from cars or on foot
function crewSupport(m, crewList, enemiesFn) {
  let t = 0;
  return m.tick((dt) => {
    t -= dt;
    if (t > 0) return;
    t = 0.45;
    const enemies = enemiesFn().filter((e) => !e.dead && !e.removed);
    for (const c of crewList) {
      if (c.dead) continue;
      let best = null, bd = 35;
      const cp = c.vehicle ? c.vehicle.pos : c.pos;
      for (const e of enemies) { const d = Math.hypot(e.pos.x - cp.x, e.pos.z - cp.z); if (d < bd) { bd = d; best = e; } }
      if (!best) { c.threat = null; c.aiming = false; continue; }
      if (!c.vehicle) { c.threat = best; continue; }
      if (Math.random() < 0.6) {
        const from = cp.clone().add(new THREE.Vector3(0, 1.3, 0));
        const to = best.pos.clone().add(new THREE.Vector3(rand(-0.6, 0.6), 1.2, rand(-0.6, 0.6)));
        const dir = to.sub(from).normalize();
        const hit = m.game.combat.raycast(from.x, from.y, from.z, dir.x, dir.y, dir.z, 60, c);
        m.game.effects.tracers.add(from, hit ? hit.point : from.clone().addScaledVector(dir, 60));
        m.game.effects.muzzleFlash(from, dir);
        if (hit && hit.kind === 'char' && hit.obj.isPlayer) continue;
        if (hit) m.game.combat.applyHit(hit, { id: 'smg', damage: 22 }, m.player, dir);
        m.game.audio?.playAt('smg', from, 0.8, { gun: true });
      }
    }
  });
}
async function countdown(m) {
  for (const n of ['3', '2', '1']) { m.hud.bigMessage(n, 'hint', 0.9); m.game.audio?.play('ui'); await m.wait(1); }
  m.hud.bigMessage('GO!', 'passed', 1.2);
  m.game.audio?.play('checkpoint');
}
function teleport(game, x, z, yaw = 0, y) {
  const p = game.player;
  if (p.vehicle) { const v = p.vehicle; v.pos.set(x, y ?? game.map.groundHeight(x, z), z); v.yaw = yaw; v.vel.set(0, 0, 0); v.r = 0; }
  else { p.setPosition(x, y, z); p.setYaw(yaw); p.vel.set(0, 0, 0); }
  game.rig.yaw = yaw + Math.PI;
}
async function fadeTeleport(m, x, z, yaw, y) {
  m.hud.fadeTo(1, 0.4);
  await m.wait(0.5);
  teleport(m.game, x, z, yaw, y);
  if (y == null) { m.game.traffic.populate(10); m.game.peds.populate(10); }
  await m.wait(0.3);
  m.hud.fadeTo(0, 0.5);
}

// ------------------------------------------------------------------ missions
export const STORY = {
  init(game) { NET = game.map.roads; },
  missions: [
    // ================================================================ CHAPTER I — HOMECOMING
    {
      id: 'welcome', title: 'Welcome Home', contact: 'V', auto: true, reward: 100,
      log: 'Dre came home for Tino\'s funeral. Detective Voss robbed him and dumped him in Viper turf.',
      start: (L) => ({ x: L.plaza.x, z: L.plaza.z + 9 }),
      async run(m, game) {
        const L = m.L;
        game.env.setTime(18.6);
        const px = L.plaza.x - 20, pz = L.plaza.z + 39;
        const road = { x: px + 4, z: pz + 5.2 };
        teleport(game, px, pz, 0);
        const car = m.car('police', road.x, road.z, -Math.PI / 2, { locked: true });
        const voss = m.ped(px - 1.6, pz + 0.8, { appearance: look('voss'), invincible: true });
        const ruiz = m.ped(px + 1.7, pz + 0.6, { appearance: look('ruiz'), invincible: true });
        m.speakersSet({ Voss: voss, Ruiz: ruiz, Dre: m.player });
        m.face(voss, m.player); m.face(ruiz, m.player); m.face(m.player, voss);
        await m.cutscene(async () => {
          m.shot([px - 30, 18, pz - 40], [px, 2, pz], 50);
          m.hud.bigMessage('LOS SOLES, SAN MORENA', 'chapter', 4, 'Five years later');
          await m.wait(3.5);
          m.twoShot(m.player, voss, 1, 3.4);
          await m.lines([
            ['Voss', 'Well, well. Andre Castillo. Five years back East and you come crawling home.'],
            ['Dre', 'My brother\'s dead, Voss. I\'m here for the funeral. That\'s it.'],
          ]);
          m.overShoulder(m.player, ruiz);
          await m.say('Ruiz', 'Funny thing about funerals. People get emotional. Do stupid things.');
          m.overShoulder(m.player, voss);
          await m.say('Voss', 'Like carrying large amounts of cash. Which, as it happens, is evidence.');
          game.player.money = 0;
          m.overShoulder(voss, m.player);
          await m.say('Dre', 'Evidence of what?');
          m.overShoulder(m.player, voss);
          await m.lines([
            ['Voss', 'Of whatever I say it is. Tino got mixed up with the wrong people. Don\'t make his mistakes.'],
            ['Ruiz', 'Get in. We\'ll give you a lift. Scenic route.'],
          ]);
        });
        // dumped in El Corona
        m.hud.fadeTo(1, 0.5);
        await m.wait(0.7);
        voss.remove(); ruiz.remove(); car.remove();
        const drop = { x: L.vipers.x - 55, z: L.vipers.z + 55 };
        teleport(game, drop.x, drop.z, Math.PI);
        const cruiser = m.car('police', drop.x + 6, drop.z + 3, -Math.PI / 2, { locked: true });
        const v2 = m.ped(drop.x, drop.z, { appearance: look('voss') }); cruiser.putIn(v2, 0);
        const r2 = m.ped(drop.x, drop.z, { appearance: look('ruiz') }); cruiser.putIn(r2, 1);
        await m.wait(0.4);
        m.hud.fadeTo(0, 0.8);
        await m.say('Voss', 'Welcome home, Castillo. Vipers own this block now. Try not to get shot.', 4);
        cruiser.ai = new RouteDriver(game, cruiser, { x: L.police.x, z: L.police.z }, { speed: 16, ignoreLights: false });
        // hostile Vipers nearby
        const vip = crew(m, 'vipers', ring(drop.x - 30, drop.z - 20, 3, 3), ['bat', 'fist', 'fist'], { guard: true, face: 0 });
        aggroWhenNear(m, vip, 22);
        m.objective('You\'re in <span class="r">Vipers</span> territory. Steal a car and get back to <span class="y">Cedar Row</span>.');
        m.help('Walk up to any car and press <b>F</b> to steal it. <b>W/S</b> drive, <b>A/D</b> steer and <b>Space</b> is the handbrake — use it to drift.', 10);
        const car2 = m.car('meridian', drop.x - 8, drop.z + 6, 0);
        car2.ai = null;
        await m.goTo(L.home.x, L.home.z, { radius: 3.5, text: 'Get back to <span class="y">Cedar Row</span> — your family\'s house.' });
        if (m.player.vehicle) { await m.until(() => m.player.vehicle?.speedAbs < 2 || !m.player.vehicle, { timeout: 4, onTimeout: 'resolve' }); if (m.player.vehicle) game.vehicles.exit(m.player); await m.wait(1.2); }
        const mari = m.ped(L.home.door.x, L.home.door.z + 0.7, { appearance: look('marisol'), invincible: true });
        m.speakersSet({ Marisol: mari, Dre: m.player });
        game.police.clear();
        await m.cutscene(async () => {
          m.player.setPosition(L.home.x, undefined, L.home.z + 0.5);
          m.face(mari, m.player); m.face(m.player, mari);
          m.twoShot(m.player, mari, -1, 3.2);
          await m.lines([
            ['Marisol', 'Dre? Oh my god... Dre!'],
            ['Dre', 'Hey, Mari. You look... older.'],
            ['Marisol', 'Five years will do that. You weren\'t here, Dre. You weren\'t here when they shot Tino.'],
            ['Dre', 'I\'m here now.'],
          ]);
          m.overShoulder(m.player, mari);
          await m.lines([
            ['Marisol', 'Cedar Row isn\'t what it was. Vipers push their stuff on every corner and the cops just watch.'],
            ['Marisol', 'Lou\'s been asking about you. He\'s always down at the court. Here — it\'s not much.'],
            ['Dre', 'Big Lou. Good. Somebody\'s going to tell me what really happened to my little brother.'],
          ]);
        });
        mari.keep = true;
        m.help('Your home is a <b>safehouse</b>: walk into the green marker at the door to save. Yellow letters on the radar are story missions.', 9);
      },
    },
    {
      id: 'oldfriends', title: 'Old Friends', contact: 'L', requires: ['welcome'], reward: 300,
      log: 'Reunited with Big Lou and Deacon. The Vipers tried to kill them outside Big Bun Burgers.',
      start: (L) => ({ x: L.court.x, z: L.court.z + 12 }),
      async run(m, game) {
        const L = m.L;
        const c = { x: L.court.x, z: L.court.z + 12 };
        const lou = m.ped(c.x - 2, c.z - 2, { appearance: look('lou'), invincible: true });
        const deacon = m.ped(c.x + 2, c.z - 2.4, { appearance: look('deacon'), invincible: true });
        m.speakersSet({ Lou: lou, Deacon: deacon, Dre: m.player });
        const road = roadNear(c.x, c.z + 30);
        const ride = m.car('bouncer', road.x, road.z, road.yaw, { color: 0xf2b705 });
        await m.cutscene(async () => {
          m.face(lou, m.player); m.face(deacon, m.player); m.face(m.player, lou);
          m.twoShot(m.player, lou, 1, 4);
          await m.lines([
            ['Lou', 'Look what the cat dragged in! DRE! Come here, man!'],
            ['Dre', 'Big Lou. Deacon. Been a long time.'],
            ['Deacon', 'Sorry about Tino, man. Kid was family to all of us.'],
            ['Lou', 'C\'mon, let\'s roll to Big Bun. Talk over some food, like old times. You drive — see if you remember how.'],
          ]);
        });
        lou.invincible = false; deacon.invincible = false;
        m.follower(lou, 0); m.follower(deacon, 2);
        m.keepAlive(lou, 'Lou died.'); m.keepAlive(deacon, 'Deacon died.');
        await m.getIn(ride, 'Get in the <span class="b">lowrider</span>.');
        await m.until(() => lou.vehicle === ride && deacon.vehicle === ride, { timeout: 15, onTimeout: 'resolve' });
        m.help('Press <b>N</b> to change radio stations. <b>G</b> works the hydraulics on lowriders.', 7);
        const say = (who, t) => m.hud.subtitle(t, who, 4);
        m.tick(() => {});
        setTimeout(() => say('Deacon', 'Man, Vipers been getting bold. They hit Tino\'s corner three times this month.'), 3000);
        await m.goTo(L.burger.x, L.burger.z - 9, { vehicle: true, radius: 5, text: 'Drive to <span class="y">Big Bun Burgers</span>.' });
        await m.say('Lou', 'Hold up... that red Brawler. That\'s Vipers! GET DOWN!', 3);
        const r2 = roadNear(L.burger.x - 60, L.burger.z);
        const { v: vc, drv, guns } = chaseCar(m, 'brawler', r2.x, r2.z, r2.yaw, 'vipers', 2);
        game.police.maxWanted = 0;
        m.objective('Take out the <span class="r">Vipers</span> or lose them!');
        m.help('Shoot from the car: hold <b>right mouse</b> to aim and <b>left mouse</b> to fire (you need a pistol or SMG). Or just ram them.', 8);
        m.player.giveWeapon('pistol', 34);
        crewSupport(m, [lou, deacon], () => [drv, ...guns]);
        await m.until(() => vc.isWrecked || (drv.dead && guns.every((g) => g.dead)) || m.distTo(drv) > 200);
        game.police.maxWanted = null;
        await m.say('Lou', 'Everybody breathing? Dre, get us back to the Row.', 3);
        await m.goTo(L.home.x, L.home.z + 14, { vehicle: true, radius: 5, text: 'Take Lou and Deacon back to <span class="y">Cedar Row</span>.' });
        await m.lines([['Lou', 'Welcome back, homie. Just like old times, huh?'], ['Deacon', 'Yeah... just like old times.']]);
        if (m.player.vehicle) game.vehicles.exit(lou);
        if (m.player.vehicle) game.vehicles.exit(deacon);
      },
    },
    {
      id: 'cleansweep', title: 'Clean Sweep', contact: 'D', requires: ['oldfriends'], reward: 400,
      log: 'Beat three Viper dealers off the Cedar Row corner — no guns, Kings rules.',
      start: (L) => ({ x: L.projects.x + 10, z: L.projects.z }),
      async run(m, game) {
        const L = m.L;
        const s = { x: L.projects.x + 10, z: L.projects.z };
        const deacon = m.ped(s.x + 1.8, s.z - 1, { appearance: look('deacon'), invincible: true });
        m.speakersSet({ Deacon: deacon, Dre: m.player });
        await m.cutscene(async () => {
          m.face(deacon, m.player); m.face(m.player, deacon);
          m.twoShot(m.player, deacon, 1, 3.3);
          await m.lines([
            ['Deacon', 'Three Viper pushers slinging on our corner, out front of Ray\'s Liquor. Four blocks east.'],
            ['Deacon', 'Kings don\'t use guns on our own streets. Show \'em some old-fashioned Cedar Row hospitality.'],
            ['Dre', 'With my fists? Sounds like a Tuesday.'],
          ]);
        });
        m.player.switchTo(m.player.weapons.bat ? 'bat' : 'fist');
        const spot = { x: L.liquor.x, z: L.liquor.z + 3 };
        const dealers = crew(m, 'vipers', ring(spot.x, spot.z, 3, 2.5), ['fist', 'fist', 'knife'], { guard: true, health: 70, face: Math.PI });
        aggroWhenNear(m, dealers, 6);
        game.police.maxWanted = 0;
        const melee = () => { const t = WEAPONS[m.player.weapon]?.type; return t === 'melee' || !t; };
        m.failIf(() => !melee() && m.player.aiming, 'Kings rules: no guns on our own block!');
        const unsub = game.events.on('gunshot', (sh) => { if (sh === m.player) m._rejectAll(new MissionFail('Kings rules: no guns on our own block!')); });
        m.gps(spot.x, spot.z);
        m.objective('Go to <span class="y">Ray\'s Liquor</span> on Cedar Row. Three Viper <span class="r">dealers</span> are slinging out front.');
        m.help('Follow the purple GPS line on the radar. Targets have a <b style="color:#ff4a3a">red arrow</b> over their heads.', 7);
        await m.until(() => m.distTo(spot) < 28 || dealers.some((d) => d.state === 'attack'));
        m.gpsOff();
        m.help('Left mouse throws punches. Chain them into a <b>jab-cross-kick</b> combo. A bat hits harder.', 7);
        await m.killAll(dealers, 'Beat down the <span class="r">dealers</span>.', { counter: 'DEALERS' });
        unsub();
        game.police.maxWanted = null;
        await m.say('Deacon', '(phone) Heard it from here, man. That\'s how it\'s done. Tino would\'ve been proud.', 4);
      },
    },
    {
      id: 'toolingup', title: 'Tooling Up', contact: 'L', requires: ['cleansweep'], reward: 500,
      log: 'Bought a gun at the Gun Barn and hit the Viper lot in El Corona.',
      start: (L) => ({ x: L.court.x, z: L.court.z + 12 }),
      async run(m, game) {
        const L = m.L;
        const c = { x: L.court.x, z: L.court.z + 12 };
        const lou = m.ped(c.x - 2, c.z - 2, { appearance: look('lou'), invincible: true });
        m.speakersSet({ Lou: lou, Dre: m.player });
        await m.cutscene(async () => {
          m.face(lou, m.player); m.face(m.player, lou);
          m.twoShot(m.player, lou, -1, 3.3);
          await m.lines([
            ['Lou', 'Word is you put three Vipers in the hospital with your bare hands. Respect.'],
            ['Lou', 'But the Vipers won\'t send dealers next time. They\'ll send shooters. Here\'s five hundred.'],
            ['Lou', 'Gun Barn, Market District. Get yourself a piece. Then pay the Vipers a visit at their lot in El Corona.'],
          ]);
        });
        m.cash(500);
        const gs = L.gunshop;
        m.gps(gs.x, gs.z);
        m.objective('Go to the <span class="r">Gun Barn</span> in the Market District and buy a <b>pistol</b>.');
        m.help('Walk into the red marker to shop. Buy the <b>9mm Pistol</b>.', 8);
        await m.until(() => !!m.player.weapons.pistol);
        m.gpsOff();
        m.help('Hold <b>right mouse</b> to aim, <b>left mouse</b> to shoot, <b>R</b> to reload. Headshots are deadly.', 9);
        const v = L.vipers;
        const pts = ring(v.x, v.z, 6, 9, 0.4);
        const vip = crew(m, 'vipers', pts, ['pistol', 'bat', 'pistol', 'fist', 'pistol', 'knife'], { guard: true });
        aggroWhenNear(m, vip, 30);
        m.gps(v.x, v.z);
        await m.killAll(vip, 'Take out the <span class="r">Vipers</span> at their lot in El Corona.', { counter: 'VIPERS' });
        m.gpsOff();
        await m.loseWanted('Lose the heat.');
        await m.goTo(L.home.x, L.home.z + 14, { radius: 5, text: 'Head back to <span class="y">Cedar Row</span>.' });
      },
    },
    {
      id: 'driveby', title: 'Drive-By', contact: 'L', requires: ['toolingup'], reward: 1000,
      log: 'Rode through El Corona with Lou and the Kings. The Vipers felt it.',
      chapterEnd: ['CHAPTER II', 'Streets on Fire'],
      start: (L) => ({ x: L.court.x, z: L.court.z + 12 }),
      async run(m, game) {
        const L = m.L;
        const c = { x: L.court.x, z: L.court.z + 12 };
        const lou = m.ped(c.x - 2, c.z - 2, { appearance: look('lou'), invincible: true });
        const k1 = m.ped(c.x + 2, c.z - 2, { appearance: look('king') });
        const k2 = m.ped(c.x + 3.5, c.z - 1, { appearance: look('king') });
        for (const k of [lou, k1, k2]) { k.giveWeapon('smg', 999); k.equip('smg'); }
        m.speakersSet({ Lou: lou, Dre: m.player });
        const road = roadNear(c.x, c.z + 30);
        const ride = m.car('bouncer', road.x, road.z, road.yaw, { color: 0x2a9d8f });
        await m.cutscene(async () => {
          m.face(lou, m.player); m.face(m.player, lou);
          m.twoShot(m.player, lou, 1, 4);
          await m.lines([
            ['Lou', 'Tino died in a drive-by. Nobody saw nothing, cops found nothing.'],
            ['Lou', 'Time the Vipers learned what that feels like. You drive. Me and the boys handle the rest.'],
            ['Dre', 'Let\'s ride.'],
          ]);
        });
        lou.invincible = false;
        m.follower(lou, 0); m.follower(k1, 1); m.follower(k2, 2);
        m.keepAlive(lou, 'Lou died.');
        await m.getIn(ride, 'Get in the car.');
        await m.until(() => [lou, k1, k2].filter((k) => !k.dead).every((k) => k.vehicle === ride), { timeout: 15, onTimeout: 'resolve' });
        const base = L.vipers;
        const spots = [{ x: base.x - 60, z: base.z - 12 }, { x: base.x + 55, z: base.z + 12 }, { x: base.x, z: base.z + 70 }];
        const all = [];
        for (const s of spots) {
          const g = crew(m, 'vipers', ring(s.x, s.z, 3, 2.2), ['pistol', 'smg', 'bat'], { guard: true });
          aggroWhenNear(m, g, 25);
          all.push(...g);
        }
        crewSupport(m, [lou, k1, k2], () => all);
        m.help('Drive past the Viper crews slowly — the Kings will shoot. You can aim with <b>right mouse</b> and fire too.', 8);
        await m.killAll(all, 'Hit the <span class="r">Viper</span> crews in El Corona.', { counter: 'VIPERS' });
        game.police.setLevel(Math.max(game.police.level, 2));
        await m.loseWanted('The cops are onto you. Lose your wanted level.');
        await m.goTo(L.home.x, L.home.z + 14, { vehicle: true, radius: 5, text: 'Take the crew back to <span class="y">Cedar Row</span>.' });
        await m.say('Lou', 'That\'s for Tino. Tomorrow the whole city knows the Kings are back.', 4);
      },
    },

    // ================================================================ CHAPTER II — STREETS ON FIRE
    {
      id: 'race', title: 'Burning Rubber', contact: 'R', requires: ['driveby'], reward: 1500,
      log: 'Won Rico\'s street race around Los Soles and earned a Kestrel GT.',
      start: (L) => ({ x: L.garage.entry.x, z: L.garage.entry.z - 2 }),
      async run(m, game) {
        const L = m.L;
        const e = L.garage.entry;
        const rico = m.ped(e.x + 2, e.z - 3, { appearance: look('rico'), invincible: true });
        m.speakersSet({ Rico: rico, Dre: m.player });
        await m.cutscene(async () => {
          m.face(rico, m.player); m.face(m.player, rico);
          m.twoShot(m.player, rico, 1, 3.3);
          await m.lines([
            ['Rico', 'So you\'re the famous Dre. Lou says you can drive.'],
            ['Rico', 'Out here, driving\'s how you earn respect. Race tonight, me and my boys. One lap around the city.'],
            ['Rico', 'Win, and I got real work for you. Lose, and you walk home.'],
          ]);
        });
        game.env.setTime(Math.max(game.env.hours, 20.5) % 24);
        // race loop: rectangle x=380 -> z=160 -> x=-165 -> z=-455
        const pts = [];
        const add = (x, z) => pts.push({ x, z });
        const xE = XS[13], xW = XS[7], zN = ZS[3], zS = ZS[9];
        add(xE, ZS[5]); add(xE, ZS[7]); add(xE, zS);
        add(XS[10], zS); add(xW, zS);
        add(xW, ZS[7]); add(xW, ZS[5]); add(xW, zN);
        add(XS[10], zN); add(xE - 30, zN);
        const start = { x: xE - 60, z: zN };
        const grid = [[0, 1.9], [0, 5.4], [-9, 1.9], [-9, 5.4]];
        const player = m.car('kestrel', start.x + grid[0][0], start.z + grid[0][1], Math.PI / 2, { color: 0xd00000 });
        await fadeTeleport(m, start.x - 4, start.z + 1.9 + 3, Math.PI / 2);
        game.disableAmbient = true;
        const racers = [];
        const types = ['brawler', 'zenith', 'kestrel'];
        for (let i = 1; i < 4; i++) {
          const v = m.car(types[i - 1], start.x + grid[i][0], start.z + grid[i][1], Math.PI / 2);
          const d = m.ped(v.pos.x, v.pos.z, { appearance: look('king') }); v.putIn(d, 0);
          v.ai = null;
          racers.push(new RaceDriver(game, v, pts, 0.72 + i * 0.05));
        }
        m.lockedCars = null;
        await m.getIn(player, 'Get in your <span class="b">Kestrel GT</span> — it\'s on the starting grid.');
        for (const r of racers) r.veh.input.brake = 1;
        m.objective('Win the race! Follow the <span class="y">checkpoints</span>.');
        game.cutscene = true;
        await countdown(m);
        game.cutscene = false;
        let idx = 0;
        let mk = m.marker(pts[0].x, pts[0].z, { radius: 9, height: 6, color: 0xffd23f });
        m.gps(pts[0].x, pts[0].z);
        let finished = false;
        m.tick((dt) => {
          for (const r of racers) r.update(dt);
          if (finished) return;
          const pp = m.player.vehicle ? m.player.vehicle.pos : m.player.pos;
          const tp = pts[idx];
          if (Math.hypot(pp.x - tp.x, pp.z - tp.z) < 12) {
            idx++;
            game.audio?.play('checkpoint');
            m.removeMarker(mk);
            if (idx >= pts.length) { finished = true; return; }
            mk = m.marker(pts[idx].x, pts[idx].z, { radius: 9, height: 6, color: idx === pts.length - 1 ? 0xffffff : 0xffd23f });
            m.gps(pts[idx].x, pts[idx].z);
          }
          const myProg = idx * 1000 - Math.hypot(pp.x - pts[Math.min(idx, pts.length - 1)].x, pp.z - pts[Math.min(idx, pts.length - 1)].z);
          const place = 1 + racers.filter((r) => r.done || r.progress() > myProg).length;
          m.hud.setCounter('POSITION', `${place}/4`);
          m.hud.setBar('CHECKPOINTS', idx / pts.length, '#ffd23f');
        });
        m.failIf(() => racers.some((r) => r.done) && !finished, 'You lost the race.');
        m.failIf(() => m.player.vehicle !== player && !game.vehicles.isBusy(m.player), 'You left your car.');
        await m.until(() => finished);
        game.disableAmbient = false;
        m.hud.setBar(null);
        player.keep = true; player.persistent = true;
        await m.say('Rico', '(radio) Ha! Nobody beats my boys on their home streets. Keep the Kestrel, you earned it.', 4);
      },
      after(game) { game.disableAmbient = false; },
    },
    {
      id: 'hotwheels', title: 'Hot Wheels', contact: 'R', requires: ['race'], reward: 1500,
      log: 'Stole three cars to order for Rico\'s mysterious client.',
      start: (L) => ({ x: L.garage.entry.x, z: L.garage.entry.z - 2 }),
      async run(m, game) {
        const L = m.L;
        const e = L.garage.entry;
        const rico = m.ped(e.x + 2, e.z - 3, { appearance: look('rico'), invincible: true });
        m.speakersSet({ Rico: rico, Dre: m.player });
        await m.cutscene(async () => {
          m.face(rico, m.player); m.face(m.player, rico);
          m.twoShot(m.player, rico, -1, 3.3);
          await m.lines([
            ['Rico', 'Got an order from a client. Three rides.'],
            ['Rico', 'A Zenith up in Vistawood, a Kestrel down by the beach, and a Brawler over in Rosewood.'],
            ['Rico', 'Bring \'em to my lock-up. Scratch the paint too bad and the client scratches you.'],
          ]);
        });
        const hills = game.map.blocks.find((b) => b.district === 'hills' && b.i === 8);
        const targets = [
          { type: 'zenith', pos: roadNear(hills.cx, hills.cz) },
          { type: 'kestrel', pos: roadNear(L.pierfront.x + 20, L.pierfront.z - 10) },
          { type: 'brawler', pos: roadNear(L.spray.x - 40, L.spray.z + 60) },
        ];
        const cars = targets.map((t) => { const v = m.car(t.type, t.pos.x, t.pos.z, t.pos.yaw, { parked: true }); v.ai = null; return v; });
        let delivered = 0;
        for (const car of cars) m.failIf(() => car.isWrecked, 'One of the cars was destroyed.');
        while (delivered < 3) {
          const left = cars.filter((c) => !c.delivered);
          m.hud.setCounter('DELIVERED', `${delivered}/3`);
          const pp = m.player.pos;
          left.sort((a, b) => a.pos.distanceTo(pp) - b.pos.distanceTo(pp));
          const car = left[0];
          await m.getIn(car, `Steal the <span class="b">${car.def.name}</span>.`);
          if (Math.random() < 0.6) { game.police.setLevel(Math.max(1, game.police.level)); m.help('The car alarm went off! The cops are coming.', 4); }
          const gar = L.garage;
          const unf = m.failIf(() => car.health < 450, `The ${car.def.name} is too damaged. The client won't take it.`);
          m.hud.setBar('CAR CONDITION', car.health / 1000, '#7dff8a');
          const barT = m.tick(() => m.hud.setBar('CAR CONDITION', car.health / 1000, car.health < 600 ? '#ff5252' : '#7dff8a'));
          await m.goTo(gar.x, gar.z, { vehicle: true, radius: 4, inCar: car, inCarMsg: `Bring the ${car.def.name}.`, text: `Deliver the ${car.def.name} to Rico's <span class="y">lock-up</span>. Lose any cops first.`, condition: () => game.police.level === 0 });
          barT(); unf();
          m.hud.setBar(null);
          game.vehicles.exit(m.player);
          await m.wait(1.4);
          car.delivered = true;
          car.locked = true;
          delivered++;
          m.cash(500);
          game.audio?.play('cash');
        }
        m.hud.setCounter(null);
        await m.say('Rico', '(phone) Client\'s thrilled. Says you\'ve got an eye for fine machinery. Here\'s your cut.', 4);
      },
    },
    {
      id: 'bloodmoney', title: 'Blood Money', contact: 'D', requires: ['driveby'], reward: 3000,
      log: 'Robbed the Vipers\' stash house with Deacon\'s tip-off and escaped a three-star manhunt.',
      start: (L) => ({ x: L.projects.x + 10, z: L.projects.z }),
      async run(m, game) {
        const L = m.L;
        const s = { x: L.projects.x + 10, z: L.projects.z };
        const deacon = m.ped(s.x + 1.8, s.z - 1, { appearance: look('deacon'), invincible: true });
        m.speakersSet({ Deacon: deacon, Dre: m.player });
        await m.cutscene(async () => {
          m.face(deacon, m.player); m.face(m.player, deacon);
          m.twoShot(m.player, deacon, 1, 3.3);
          await m.lines([
            ['Deacon', 'Vipers stash their cash at a house in El Corona. My guy says it\'s lightly guarded tonight.'],
            ['Deacon', 'Hit it, grab the money, and meet me back here. We split it down the middle.'],
            ['Dre', 'Lightly guarded. Right.'],
          ]);
        });
        const v = L.vipers;
        const guards = crew(m, 'vipers', ring(v.x, v.z, 8, 12, 0.2), ['smg', 'pistol', 'shotgun', 'pistol', 'smg', 'pistol', 'bat', 'pistol'], { guard: true, health: 110 });
        aggroWhenNear(m, guards, 35);
        m.gps(v.x, v.z);
        await m.killAll(guards, 'Clear out the <span class="r">stash house guards</span>.', { counter: 'GUARDS' });
        const bags = ring(v.x, v.z, 3, 4, 1).map((p) => m.marker(p.x, p.z, { radius: 0.9, color: 0x44ff44, footOnly: true, icon: 'money' }));
        let got = 0;
        for (const b of bags) b.onEnter = (mk) => { got++; game.audio?.play('cash'); m.removeMarker(mk); m.hud.moneyFlash(1000); };
        m.objective('Grab the <span class="g">cash bags</span>.');
        await m.until(() => got >= 3);
        game.police.setLevel(3);
        m.help('Three stars! Break line of sight with the cops and stay hidden until the stars stop flashing. A <b>Spray Shack</b> also clears your wanted level.', 9);
        await m.loseWanted('The whole LSPD is after you. Lose your wanted level!');
        await m.goTo(s.x, s.z, { radius: 2, text: 'Bring the money back to <span class="y">Deacon</span> at the projects.' });
        await m.say('Deacon', 'Damn, Dre. Half for you, half for... the cause. You\'re a natural.', 4);
      },
    },
    {
      id: 'snitch', title: 'The Snitch', contact: 'V', requires: ['bloodmoney'], reward: 2000, allowWanted: false,
      log: 'Voss blackmailed Dre into silencing Benny Tran before he reached LSPD Central.',
      start: (L) => ({ x: L.hospital.x - 30, z: L.hospital.z - 4 }),
      async run(m, game) {
        const L = m.L;
        const s = { x: L.hospital.x - 30, z: L.hospital.z - 4 };
        const voss = m.ped(s.x + 1.8, s.z - 1.2, { appearance: look('voss'), invincible: true });
        m.speakersSet({ Voss: voss, Dre: m.player });
        await m.cutscene(async () => {
          m.face(voss, m.player); m.face(m.player, voss);
          m.twoShot(m.player, voss, 1, 3.4);
          await m.lines([
            ['Voss', 'Castillo. You\'ve been busy. Stash houses, drive-bys. Very entrepreneurial.'],
            ['Voss', 'A little bird named Benny Tran is about to make a statement about a shooting at Pershing Plaza. Your name\'s in it.'],
            ['Dre', 'I wasn\'t even in town!'],
            ['Voss', 'Tell it to a jury. Or make sure Benny never reaches LSPD Central. He\'s leaving the docks right now.'],
          ]);
        });
        const r = roadNear(L.warehouse.x - 40, L.warehouse.z);
        const car = m.car('meridian', r.x, r.z, r.yaw, { color: 0x3a5a40 });
        car.health = 1300;
        const benny = m.ped(r.x, r.z, { appearance: look('benny') }); car.putIn(benny, 0);
        const drv = new RouteDriver(game, car, { x: L.police.x, z: L.police.z - 12 }, { speed: 18, ignoreLights: false });
        car.ai = drv;
        m.blipEntity(benny, 0xff3030, 'target');
        game.police.maxWanted = 2;
        m.tick(() => { if (!benny.dead && car.driver === benny && car.ai === drv && m.distTo(benny) < 50) { drv.cruise = 28; drv.ignoreLights = true; } if (car.driver !== benny) car.ai = null; });
        m.failIf(() => drv.arrived && !benny.dead && car.driver === benny, 'Benny made it to the police station.');
        m.objective('Stop <span class="r">Benny Tran</span> before he reaches LSPD Central.');
        await m.until(() => benny.dead || car.isWrecked);
        if (!benny.dead) benny.takeDamage(1000, { source: m.player });
        game.police.maxWanted = null;
        await m.loseWanted();
        await m.say('Voss', '(phone) Benny Tran, tragic accident. You and me are going to get along fine, Castillo.', 4);
      },
    },
    {
      id: 'familyties', title: 'Family Ties', contact: 'L', requires: ['snitch'], reward: 2500,
      log: 'The Vipers kidnapped Marisol. Dre stormed the Pier 9 warehouse and brought her home.',
      chapterEnd: ['CHAPTER III', 'Betrayal'],
      start: (L) => ({ x: L.home.x + 4, z: L.home.z + 2 }),
      async run(m, game) {
        const L = m.L;
        const s = { x: L.home.x + 4, z: L.home.z + 2 };
        const lou = m.ped(s.x + 1.6, s.z - 1, { appearance: look('lou'), invincible: true });
        m.speakersSet({ Lou: lou, Dre: m.player });
        await m.cutscene(async () => {
          m.face(lou, m.player); m.face(m.player, lou);
          m.twoShot(m.player, lou, -1, 3.3);
          await m.lines([
            ['Lou', 'Dre! They took Mari! Vipers and Cuervos, they grabbed her right outside the house!'],
            ['Dre', 'Where?'],
            ['Lou', 'Word is they\'re holding her at the Pier 9 warehouse down in Port Morena. Go! I\'ll round up the boys.'],
          ]);
        });
        m.player.giveWeapon('pistol', 34);
        const w = L.warehouse;
        const enemies = [
          ...crew(m, 'cuervos', ring(w.x - 18, w.z + 5, 5, 6), ['smg', 'shotgun', 'pistol', 'smg', 'pistol'], { guard: true }),
          ...crew(m, 'vipers', ring(w.x + 8, w.z + 8, 5, 7, 0.5), ['pistol', 'smg', 'bat', 'pistol', 'shotgun'], { guard: true }),
        ];
        aggroWhenNear(m, enemies, 40);
        const mari = m.ped(w.x + 26, w.z + 2, { appearance: look('marisol') });
        mari.crouching = true; mari.animState.cower = true;
        m.keepAlive(mari, 'Marisol was killed.');
        m.gps(w.x, w.z);
        await m.killAll(enemies, 'Storm the <span class="r">Pier 9 warehouse</span>.', { counter: 'ENEMIES' });
        m.blipEntity(mari, 0x4aa3ff, 'dot');
        await m.goTo(mari.pos.x, mari.pos.z + 1.2, { radius: 1.6, text: 'Get to <span class="b">Marisol</span>.' });
        m.speakersSet({ Marisol: mari, Dre: m.player });
        mari.animState.cower = false; mari.crouching = false;
        await m.lines([['Marisol', 'Dre! I knew you\'d come. They kept saying Deacon would pay for me... Deacon, Dre!'], ['Dre', 'Deacon? We\'ll talk at home. Stay close.']]);
        m.follower(mari, 0);
        await m.goTo(L.home.x, L.home.z + 14, { radius: 5, text: 'Take <span class="b">Marisol</span> home.', condition: () => mari.vehicle === m.player.vehicle || (!m.player.vehicle && Math.hypot(mari.pos.x - m.player.pos.x, mari.pos.z - m.player.pos.z) < 8) });
        if (mari.vehicle) game.vehicles.exit(mari);
        await m.say('Marisol', 'Thank you. Dre... watch Deacon. Something\'s wrong with him.', 4);
        mari.keep = false;
      },
    },

    // ================================================================ CHAPTER III — BETRAYAL
    {
      id: 'evidence', title: 'Evidence', contact: 'V', requires: ['familyties'], reward: 3000,
      log: 'Destroyed an LSPD evidence van for Voss. The evidence tied Voss to Tino\'s murder.',
      start: (L) => ({ x: L.hospital.x - 30, z: L.hospital.z - 4 }),
      async run(m, game) {
        const L = m.L;
        const s = { x: L.hospital.x - 30, z: L.hospital.z - 4 };
        const voss = m.ped(s.x + 1.8, s.z - 1.2, { appearance: look('voss'), invincible: true });
        m.speakersSet({ Voss: voss, Dre: m.player });
        await m.cutscene(async () => {
          m.face(voss, m.player); m.face(m.player, voss);
          m.overShoulder(m.player, voss);
          await m.lines([
            ['Voss', 'The DA\'s moving an evidence van from Central to the courthouse. In it: a gun with your brother\'s blood on it.'],
            ['Voss', 'And, hypothetically, my fingerprints.'],
            ['Dre', 'You killed Tino.'],
            ['Voss', 'I said hypothetically. Blow the van. Or the next body they pull out of the ocean is your sister\'s.'],
          ]);
          await m.say('Voss', 'Here. A little something from the evidence locker.', 3);
        });
        m.player.giveWeapon('rpg', 4);
        m.player.switchTo('rpg');
        const r = roadNear(L.police.x, L.police.z - 20);
        const van = m.car('parcel', r.x, r.z, r.yaw, { color: 0xf2f2f2 });
        van.health = 2600;
        const d = m.ped(r.x, r.z, { appearance: copAppearance() }); van.putIn(d, 0);
        const drv = new RouteDriver(game, van, { x: L.tower.x, z: L.tower.z + 10 }, { speed: 15, ignoreLights: false });
        van.ai = drv;
        const esc = m.car('police', r.x - 12 * Math.sin(r.yaw), r.z - 12 * Math.cos(r.yaw), r.yaw);
        const ed = m.ped(esc.pos.x, esc.pos.z, { appearance: copAppearance() }); esc.putIn(ed, 0);
        esc.ai = new RouteDriver(game, esc, { x: L.tower.x, z: L.tower.z + 20 }, { speed: 15, ignoreLights: false });
        esc.sirenOn = true;
        m.blipEntity(van, 0xff3030, 'target');
        m.failIf(() => drv.arrived && !van.isWrecked, 'The evidence reached the courthouse.');
        m.objective('Destroy the <span class="r">evidence van</span> before it reaches the courthouse.');
        m.help('The <b>Rocket Launcher</b> is selected. Aim with right mouse, fire with left. Watch your distance!', 7);
        await m.until(() => van.isWrecked);
        game.police.setLevel(3);
        await m.loseWanted('Lose the cops.');
        await m.say('Dre', '(to himself) Voss killed Tino. And I just burned the proof. Not for long, Voss.', 4);
      },
    },
    {
      id: 'beachparty', title: 'Beach Party', contact: 'L', requires: ['evidence'], reward: 3000,
      log: 'Crashed Chino\'s party at the Santa Luz Pier. Chino, the triggerman in Tino\'s shooting, is dead.',
      start: (L) => ({ x: L.home.x + 4, z: L.home.z + 2 }),
      async run(m, game) {
        const L = m.L;
        const s = { x: L.home.x + 4, z: L.home.z + 2 };
        const lou = m.ped(s.x + 1.6, s.z - 1, { appearance: look('lou'), invincible: true });
        m.speakersSet({ Lou: lou, Dre: m.player });
        await m.cutscene(async () => {
          m.face(lou, m.player); m.face(m.player, lou);
          m.twoShot(m.player, lou, 1, 3.4);
          await m.lines([
            ['Lou', 'Chino. Salazar\'s right hand. He was the shooter in Tino\'s drive-by — three different people told me.'],
            ['Lou', 'He throws a party on the Santa Luz Pier every Friday. Guess what day it is.'],
          ]);
        });
        game.env.setTime(Math.max(18.4, game.env.hours));
        const P = L.pier;
        const party = { x: (P.x0 + P.x1) / 2, z: P.z0 + 180 };
        const py = P.y;
        const chino = m.enemy(party.x, party.z, { gang: 'vipers', weapon: 'smg', guard: true, health: 180, appearance: look('chino'), blip: false });
        m.blipEntity(chino, 0xff3030, 'target');
        const guests = crew(m, 'vipers', ring(party.x, party.z, 5, 5), ['pistol', 'bat', 'pistol', 'smg', 'fist'], { guard: true });
        for (const g of [chino, ...guests]) g.setPosition(g.pos.x, py, g.pos.z);
        const getaway = m.car('zenith', P.x0 + 6, P.z0 + 44, 0, { color: 0xc1121f });
        let fleeing = false;
        m.tick(() => {
          const p = m.player.vehicle ? m.player.vehicle.pos : m.player.pos;
          if (!fleeing && !chino.dead && (Math.hypot(chino.pos.x - p.x, chino.pos.z - p.z) < 35 || chino.health < chino.maxHealth)) {
            fleeing = true;
            for (const g of guests) { g.threat = m.player; g.setState('attack'); }
            chino.brain = 'script';
            chino.scriptThink = (dt) => { if (!chino.vehicle && !game.vehicles.isBusy(chino)) { const dp = getaway.doorWorld(); if (chino.goTo(dp.x, dp.z, 6, dt, 1.2)) game.vehicles.enter(chino, getaway, 0, { force: true }); } };
            m.hud.subtitle('Chino: It\'s Castillo! Get him! I\'m out of here!', 'Chino', 3);
          }
          if (chino.vehicle === getaway && !getaway.ai) getaway.ai = new RouteDriver(game, getaway, { x: 0, z: 0 }, { speed: 30, flee: true });
        });
        m.gps(party.x, party.z);
        m.objective('Kill <span class="r">Chino</span> at the pier party.');
        await m.until(() => chino.dead);
        await m.loseWanted();
        await m.say('Lou', '(phone) Chino\'s done? ...Good. That\'s one. Tino can rest a little easier tonight.', 4);
      },
    },
    {
      id: 'tail', title: 'Snake in the Grass', contact: 'L', requires: ['beachparty'], reward: 1000,
      log: 'Tailed Deacon to Pershing Plaza: he was working with Voss and Salazar — and set Tino up.',
      start: (L) => ({ x: L.home.x + 4, z: L.home.z + 2 }),
      async run(m, game) {
        const L = m.L;
        const s = { x: L.home.x + 4, z: L.home.z + 2 };
        const lou = m.ped(s.x + 1.6, s.z - 1, { appearance: look('lou'), invincible: true });
        m.speakersSet({ Lou: lou, Dre: m.player });
        await m.cutscene(async () => {
          m.face(lou, m.player); m.face(m.player, lou);
          m.twoShot(m.player, lou, -1, 3.2);
          await m.lines([
            ['Lou', 'Mari says the kidnappers talked about Deacon paying for her. Deacon\'s been driving a brand new Summit, too.'],
            ['Lou', 'He\'s leaving the projects right now. Follow him. Don\'t let him see you.'],
          ]);
        });
        const r = roadNear(L.projects.x + 40, L.projects.z - 50);
        const ride = m.car('meridian', L.home.x + 6, L.home.z + 22, Math.PI / 2);
        const dcar = m.car('summit', r.x, r.z, r.yaw, { color: 0x111111 });
        const deacon = m.ped(r.x, r.z, { appearance: look('deacon'), invincible: true }); dcar.putIn(deacon, 0);
        const dest = { x: L.plaza.x, z: L.plaza.z + 40 };
        const drv = new RouteDriver(game, dcar, dest, { speed: 14, ignoreLights: true });
        dcar.ai = null;
        m.blipEntity(dcar, 0x4aa3ff, 'car');
        m.objective('Follow <span class="b">Deacon</span>. Keep your distance.');
        await m.until(() => m.distTo(deacon) < 90 || m.player.vehicle, { timeout: 60, onTimeout: 'resolve' });
        dcar.ai = drv;
        let close = 0;
        m.tick((dt) => {
          const d = m.distTo(deacon);
          close = d < 14 ? close + dt : Math.max(0, close - dt);
          m.hud.setBar(d < 25 ? 'TOO CLOSE!' : d > 110 ? 'LOSING HIM' : 'DISTANCE', 1 - clamp((d - 10) / 140, 0, 1), d < 25 ? '#ff5252' : d > 110 ? '#ffd23f' : '#7fb6ff');
        });
        m.failIf(() => m.distTo(deacon) > 150, 'You lost Deacon.');
        m.failIf(() => close > 2.5, 'Deacon spotted you.');
        let spotted = false;
        const off1 = game.events.on('carCrash', (A, B) => { const pv = m.player.vehicle; if (pv && ((A === dcar && B === pv) || (B === dcar && A === pv))) spotted = true; });
        const off2 = game.events.on('vehicleShot', (v, sh) => { if (v === dcar && sh === m.player) spotted = true; });
        const off3 = game.events.on('gunshot', (sh) => { if (sh === m.player && m.distTo(deacon) < 60) spotted = true; });
        m.failIf(() => spotted, 'Deacon noticed you.');
        m.tick(() => { if (!game.missions.active) { off1(); off2(); off3(); } });
        await m.until(() => drv.arrived);
        off1(); off2(); off3();
        m.hud.setBar(null);
        const pz = L.plaza;
        const voss = m.ped(pz.x + 2, pz.z + 3, { appearance: look('voss'), invincible: true });
        const sal = m.ped(pz.x - 1.5, pz.z + 3.5, { appearance: look('salazar'), invincible: true });
        game.vehicles.exit(deacon);
        await m.wait(1.5);
        deacon.brain = 'script';
        deacon.setPosition(pz.x, undefined, pz.z + 1);
        m.speakersSet({ Voss: voss, Salazar: sal, Deacon: deacon, Dre: m.player });
        await m.cutscene(async () => {
          m.face(deacon, voss); m.face(voss, deacon); m.face(sal, deacon);
          m.shot([pz.x + 14, 6, pz.z + 16], [pz.x, 1.5, pz.z + 2], 40);
          await m.say('Salazar', 'Castillo is becoming a problem, Deacon. Chino is dead. My stash house, my van...');
          m.twoShot(deacon, sal, 1, 3.5);
          await m.say('Deacon', 'Dre\'s not like Tino. Give me time, he\'ll fall in line.');
          m.overShoulder(deacon, voss);
          await m.lines([
            ['Voss', 'He\'d better. Our little arrangement needs Cedar Row quiet. Tino wouldn\'t stay quiet either. Remember how that ended?'],
          ]);
          m.twoShot(deacon, voss, -1, 3.2);
          await m.lines([
            ['Deacon', '...I set Tino up for you. I\'m not doing that to Dre.'],
            ['Salazar', 'You\'ll do what you\'re told. Friday night we hit Cedar Row and end the Kings for good.'],
          ]);
          m.shot([m.player.pos.x + 3, m.player.pos.y + 2.2, m.player.pos.z + 3], [m.player.pos.x, m.player.pos.y + 1.5, m.player.pos.z], 45);
          await m.say('Dre', 'Deacon... you snake. You sold out my brother.', 4);
        });
        voss.remove(); sal.remove(); deacon.remove(); dcar.remove();
      },
    },
    {
      id: 'ambush', title: 'Ambush', contact: 'L', auto: true, requires: ['tail'], reward: 2000,
      start: (L) => ({ x: L.home.x + 4, z: L.home.z + 2 }),
      log: 'Survived the Vipers\' assault on Cedar Row. Lou was shot defending the block.',
      async run(m, game) {
        const L = m.L;
        game.env.setTime(21.5);
        const h = L.home;
        await fadeTeleport(m, h.x + 2, h.z + 4, Math.PI);
        const lou = m.ped(h.x + 4, h.z + 5, { appearance: look('lou'), invincible: true });
        const kings = [0, 1, 2].map((i) => m.ped(h.x - 4 + i * 4, h.z + 8, { appearance: look('king') }));
        for (const k of [lou, ...kings]) { k.giveWeapon('smg', 999); k.equip('smg'); }
        m.player.giveWeapon('smg', 96);
        m.speakersSet({ Lou: lou, Dre: m.player });
        await m.cutscene(async () => {
          m.face(lou, m.player); m.face(m.player, lou);
          m.twoShot(m.player, lou, 1, 3.6);
          await m.lines([
            ['Lou', 'Deacon sold out TINO?! To Voss and the Vipers?!'],
            ['Dre', 'And they\'re coming tonight. For all of us.'],
            ['Lou', 'Then let \'em come. KINGS! Get strapped! Nobody takes the Row!'],
          ]);
        });
        lou.invincible = false;
        for (const k of [lou, ...kings]) { k.brain = 'gang'; k.gang = 'kings'; k.state = 'guard'; k.guardFace = Math.PI / 2; }
        const enemies = [];
        crewSupport(m, [lou, ...kings], () => enemies);
        m.tick(() => { for (const k of [lou, ...kings]) if (!k.dead && k.threat && !k.threat.dead) k.setState('attack'); else if (!k.dead && k.state === 'attack') k.setState('guard'); });
        game.police.maxWanted = 0;
        const waves = [
          { n: 5, from: [-60, 25], w: ['pistol', 'bat', 'pistol', 'smg', 'knife'] },
          { n: 6, from: [65, 22], w: ['smg', 'pistol', 'shotgun', 'pistol', 'smg', 'pistol'] },
          { n: 7, from: [0, -70], w: ['smg', 'shotgun', 'rifle', 'pistol', 'smg', 'pistol', 'shotgun'], car: true },
        ];
        for (let wi = 0; wi < waves.length; wi++) {
          const W = waves[wi];
          m.hud.bigMessage(`WAVE ${wi + 1}`, 'hint', 2);
          const base = { x: h.x + W.from[0], z: h.z + W.from[1] };
          const wave = crew(m, 'vipers', ring(base.x, base.z, W.n, 4), W.w, { guard: false, health: 100 });
          for (const e of wave) { e.threat = m.player; e.setState('attack'); }
          if (W.car) { const r = roadNear(h.x + 80, h.z + 20); chaseCar(m, 'brawler', r.x, r.z, r.yaw, 'vipers', 1); }
          enemies.push(...wave);
          await m.killAll(wave, 'Defend <span class="y">Cedar Row</span>! Kill the <span class="r">Vipers</span>.', { counter: 'ATTACKERS' });
          await m.wait(2);
        }
        game.police.maxWanted = null;
        lou.health = 5;
        lou.knockDown(new THREE.Vector3(0, 1, -2));
        await m.wait(1.2);
        await m.say('Lou', 'Argh! I\'m hit... Dre... I\'m hit bad...', 3);
      },
    },
    {
      id: 'hospital', title: 'Rush to All Saints', contact: 'L', auto: true, requires: ['ambush'], reward: 2000,
      start: (L) => ({ x: L.home.x + 4, z: L.home.z + 2 }),
      log: 'Raced a bleeding Lou to All Saints General with Vipers on the bumper. He pulled through.',
      chapterEnd: ['CHAPTER IV', 'Rise'],
      async run(m, game) {
        const L = m.L;
        const h = L.home;
        const r = roadNear(h.x, h.z + 20);
        const car = m.car('summit', r.x, r.z, r.yaw, { color: 0xf2b705 });
        const lou = m.ped(h.x + 3, h.z + 6, { appearance: look('lou') });
        lou.health = 40;
        lou.invincible = true;
        m.speakersSet({ Lou: lou, Dre: m.player });
        m.objective('Get <span class="b">Lou</span> into the car!');
        m.follower(lou, 0);
        await m.getIn(car);
        await m.until(() => lou.vehicle === car, { timeout: 12, onTimeout: 'resolve' });
        if (lou.vehicle !== car) car.putIn(lou, 1);
        const stopT = m.timer(150, 'Lou didn\'t make it...');
        m.keepAlive(car, 'The car was destroyed. Lou didn\'t make it.');
        m.failIf(() => m.player.vehicle !== car && !game.vehicles.isBusy(m.player), 'You abandoned Lou.');
        const r2 = roadNear(h.x - 80, h.z);
        chaseCar(m, 'brawler', r2.x, r2.z, r2.yaw, 'vipers', 1);
        setTimeout(() => { if (m.game.missions.active === m) { const r3 = roadNear(L.hospital.x - 120, L.hospital.z + 200); chaseCar(m, 'kestrel', r3.x, r3.z, r3.yaw, 'vipers', 1); } }, 25000);
        game.police.maxWanted = 0;
        setTimeout(() => m.hud.subtitle('Stay with me, big man. Stay with me!', 'Dre', 3), 2000);
        setTimeout(() => m.hud.subtitle('Tell Mari... tell her the lasagna recipe is in the... blue book...', 'Lou', 4), 16000);
        await m.goTo(L.hospital.x, L.hospital.z + 4, { vehicle: true, radius: 5, text: 'Get Lou to <span class="y">All Saints General</span>!', inCar: car });
        stopT();
        game.police.maxWanted = null;
        car.input.brake = 1;
        await m.say('Lou', 'Man... you drive like a maniac. Thanks, D.', 3);
        game.vehicles.exit(lou);
      },
    },

    // ================================================================ CHAPTER IV — RISE
    {
      id: 'heist', title: 'Harbor Heist', contact: 'R', requires: ['hospital'], reward: 5000,
      log: 'Stole a truck full of Cuervo guns from Port Morena and delivered it to Rico\'s lock-up.',
      start: (L) => ({ x: L.garage.entry.x, z: L.garage.entry.z - 2 }),
      async run(m, game) {
        const L = m.L;
        const e = L.garage.entry;
        const rico = m.ped(e.x + 2, e.z - 3, { appearance: look('rico'), invincible: true });
        m.speakersSet({ Rico: rico, Dre: m.player });
        await m.cutscene(async () => {
          m.face(rico, m.player); m.face(m.player, rico);
          m.twoShot(m.player, rico, 1, 3.3);
          await m.lines([
            ['Rico', 'Heard about Lou. You want to hit back? Hit their wallet.'],
            ['Rico', 'Cuervos run guns for the Vipers through the docks. Tonight a Boxer truck full of hardware is sitting at Port Morena.'],
            ['Rico', 'Take it. Bring it here. The Vipers go to war with empty hands.'],
          ]);
        });
        const w = L.warehouse;
        const truck = m.car('boxer', w.x - 20, w.z + 38, Math.PI / 2, { color: 0x1d3557 });
        truck.ai = null;
        truck.health = 1600;
        const guards = crew(m, 'cuervos', ring(w.x - 20, w.z + 38, 6, 7), ['smg', 'shotgun', 'pistol', 'rifle', 'smg', 'pistol'], { guard: true });
        aggroWhenNear(m, guards, 30);
        m.keepAlive(truck, 'The truck was destroyed.');
        m.gps(truck.pos.x, truck.pos.z);
        await m.getIn(truck, 'Steal the <span class="b">weapons truck</span> at Port Morena.');
        for (let i = 0; i < 2; i++) { const r = roadNear(w.x - 100 - i * 60, w.z + 40 - i * 80); chaseCar(m, pick(['brawler', 'summit']), r.x, r.z, r.yaw, 'cuervos', 1); }
        m.tick(() => m.hud.setBar('TRUCK', truck.health / 1600, '#7dff8a'));
        await m.goTo(L.garage.x, L.garage.z, { vehicle: true, radius: 5, inCar: truck, text: 'Deliver the truck to Rico\'s <span class="y">lock-up</span>. Lose the cops first.', condition: () => game.police.level === 0 });
        m.hud.setBar(null);
        game.vehicles.exit(m.player);
        truck.locked = true;
        m.player.giveWeapon('rifle', 90);
        await m.say('Rico', 'Whoa. Christmas came early. Take a rifle, on the house.', 4);
      },
    },
    {
      id: 'vistawood', title: 'Vistawood Nights', contact: 'Z', requires: ['hospital'], reward: 5000,
      log: 'Pulled off a hills-to-pier stunt run for Hollywood producer Maddox — the client behind the Zenith theft.',
      start: (L) => { return { x: L.mansion.gate.x - 180, z: L.mansion.gate.z + 1 }; },
      async run(m, game) {
        const L = m.L;
        const s = { x: L.mansion.gate.x - 180, z: L.mansion.gate.z + 1 };
        const mad = m.ped(s.x + 1.8, s.z - 1, { appearance: look('maddox'), invincible: true });
        m.speakersSet({ Maddox: mad, Dre: m.player });
        const r = roadNear(s.x, s.z - 10);
        const car = m.car('zenith', r.x, r.z, r.yaw, { color: 0xffd60a });
        await m.cutscene(async () => {
          m.face(mad, m.player); m.face(m.player, mad);
          m.twoShot(m.player, mad, 1, 3.3);
          await m.lines([
            ['Maddox', 'So YOU\'RE the guy who jacked my Zenith! Relax, relax — Rico\'s client was me. I love it.'],
            ['Maddox', 'I\'m making a movie about this city and I need a stunt driver with... authenticity.'],
            ['Maddox', 'From here to the Santa Luz Pier in a minute forty. Cameras are rolling. Don\'t scratch the car. Much.'],
          ]);
        });
        await m.getIn(car, 'Get in the <span class="b">Zenith</span>.');
        game.cutscene = true;
        await countdown(m);
        game.cutscene = false;
        const stop = m.timer(100, 'Too slow. Maddox wanted it under a minute forty.');
        m.keepAlive(car, 'You wrecked the star car.');
        const P = L.pier;
        await m.goTo(XS[9], ZS[5], { vehicle: true, radius: 8, height: 5, text: 'Stunt run: reach the checkpoints.' });
        await m.goTo(XS[9], ZS[10], { vehicle: true, radius: 8, height: 5 });
        await m.goTo((P.x0 + P.x1) / 2, P.z0 + 30, { vehicle: true, radius: 7, height: 5, text: 'Finish on the <span class="y">Santa Luz Pier</span>!' });
        stop();
        await m.say('Maddox', '(radio) CUT! Print it! That\'s the money shot, baby! Come by the hills anytime.', 4);
      },
    },
    {
      id: 'streets', title: 'Taking Back the Streets', contact: 'L', requires: ['heist', 'vistawood'], reward: 4000,
      log: 'Lou recovered. Together they cleared three Viper crews out of Cedar Row and Rosewood.',
      start: (L) => ({ x: L.court.x, z: L.court.z + 12 }),
      async run(m, game) {
        const L = m.L;
        const c = { x: L.court.x, z: L.court.z + 12 };
        const lou = m.ped(c.x - 2, c.z - 2, { appearance: look('lou'), invincible: true });
        const k1 = m.ped(c.x + 2, c.z - 2, { appearance: look('king') });
        for (const k of [lou, k1]) { k.giveWeapon('rifle', 999); k.equip('rifle'); }
        m.speakersSet({ Lou: lou, Dre: m.player });
        await m.cutscene(async () => {
          m.face(lou, m.player); m.face(m.player, lou);
          m.twoShot(m.player, lou, 1, 3.4);
          await m.lines([
            ['Lou', 'Doc says I\'m too pretty to die. Now let\'s get our streets back.'],
            ['Lou', 'Three Viper crews are holding corners in Cedar Row and Rosewood. We clear \'em, the neighborhood breathes again.'],
          ]);
        });
        lou.invincible = false;
        m.follower(lou, 0); m.follower(k1, 1);
        m.keepAlive(lou, 'Lou died.');
        const bl = game.map.blocks.filter((b) => (b.district === 'hood' || b.district === 'westside') && !b.special);
        const rng = new RNG(99);
        const picks = [rng.pick(bl), rng.pick(bl), rng.pick(bl)];
        const all = [];
        for (const b of picks) {
          const g = crew(m, 'vipers', ring(b.x0 + 2, b.z0 + 2 + 12, 4, 3), ['smg', 'pistol', 'shotgun', 'pistol'], { guard: true, health: 120 });
          aggroWhenNear(m, g, 30);
          all.push(...g);
        }
        crewSupport(m, [lou, k1], () => all);
        await m.killAll(all, 'Clear out the <span class="r">Viper</span> crews.', { counter: 'VIPERS' });
        m.game.missions.gangDensity.vipers = 0.15;
        m.game.missions.gangDensity.kings = 0.5;
        await m.loseWanted();
        await m.say('Lou', 'Cedar Row belongs to the Kings again. Now there\'s only one Viper left who matters.', 4);
      },
    },
    {
      id: 'kingpin', title: 'Kingpin', contact: 'L', requires: ['streets'], reward: 10000,
      log: 'Stormed the Salazar Estate in Vistawood Hills. The Vipers\' boss is dead.',
      chapterEnd: ['CHAPTER V', 'End of the Line'],
      start: (L) => ({ x: L.court.x, z: L.court.z + 12 }),
      async run(m, game) {
        const L = m.L;
        const c = { x: L.court.x, z: L.court.z + 12 };
        const lou = m.ped(c.x - 2, c.z - 2, { appearance: look('lou'), invincible: true });
        m.speakersSet({ Lou: lou, Dre: m.player });
        await m.cutscene(async () => {
          m.face(lou, m.player); m.face(m.player, lou);
          m.twoShot(m.player, lou, -1, 3.4);
          await m.lines([
            ['Lou', 'Salazar\'s holed up in his mansion in Vistawood Hills. Guards, walls, the works.'],
            ['Lou', 'Every Viper in this city answers to him. We cut the head off the snake, the body dies.'],
            ['Dre', 'Then I\'m going snake hunting.'],
          ]);
        });
        const M = L.mansion;
        const guards = crew(m, 'vipers', [...ring(M.x - 30, M.z - 20, 6, 6), ...ring(M.x, M.z + 9, 6, 8, 0.3)], ['rifle', 'smg', 'shotgun', 'pistol', 'smg', 'rifle'], { guard: true, health: 130, accuracy: 0.5 });
        aggroWhenNear(m, guards, 45);
        const sal = m.enemy(M.x, M.z - 1, { gang: 'vipers', weapon: 'shotgun', guard: true, health: 450, armor: 100, appearance: look('salazar'), blip: false });
        m.blipEntity(sal, 0xff3030, 'skull');
        aggroWhenNear(m, [sal], 25);
        game.police.setLevel(Math.max(game.police.level, 1));
        m.gps(M.gate.x, M.gate.z);
        m.objective('Kill <span class="r">Salazar</span> at his Vistawood estate.');
        await m.until(() => sal.dead);
        m.hud.subtitle('Salazar: Voss... will bury you... Castillo...', 'Salazar', 3);
        await m.loseWanted();
        await m.say('Lou', '(phone) It\'s done? The Vipers are finished. Now it\'s just Deacon... and Voss.', 4);
      },
    },

    // ================================================================ CHAPTER V — END OF THE LINE
    {
      id: 'papertrail', title: 'Paper Trail', contact: 'M', requires: ['kingpin'], reward: 5000,
      log: 'Walked into LSPD Central in a stolen cruiser and walked out with Voss\'s dirty files.',
      start: (L) => ({ x: L.home.x - 3, z: L.home.z + 2 }),
      async run(m, game) {
        const L = m.L;
        const s = { x: L.home.x - 3, z: L.home.z + 2 };
        const mari = m.ped(s.x - 1.6, s.z - 1, { appearance: look('marisol'), invincible: true });
        m.speakersSet({ Marisol: mari, Dre: m.player });
        await m.cutscene(async () => {
          m.face(mari, m.player); m.face(m.player, mari);
          m.twoShot(m.player, mari, 1, 3.2);
          await m.lines([
            ['Marisol', 'Voss keeps his dirty files at LSPD Central. Payoffs, Salazar\'s money, everything. A friend at the DA told me.'],
            ['Marisol', 'Steal a police car so nobody looks twice, drive into the back lot and grab the file boxes from the loading dock.'],
            ['Dre', 'Walk into a police station with a stolen cop car. What could go wrong?'],
          ]);
        });
        const r = roadNear(L.police.x - 120, L.police.z + 80);
        const cop = m.car('police', r.x, r.z, r.yaw, { parked: true });
        cop.ai = null;
        game.police.enabled = false;
        await m.getIn(cop, 'Steal a <span class="b">police cruiser</span>.');
        game.police.clear();
        const lot = { x: L.police.x, z: L.police.z + 42 };
        await m.goTo(lot.x, lot.z, { vehicle: true, radius: 4, inCar: cop, inCarMsg: 'You need a police car.', text: 'Drive into the <span class="y">LSPD back lot</span>.' });
        game.vehicles.exit(m.player);
        await m.wait(1.3);
        await m.goTo(lot.x + 10, lot.z - 10, { onFoot: true, radius: 1.2, text: 'Grab the <span class="g">files</span> from the loading dock.' });
        game.audio?.play('pickup');
        game.police.enabled = true;
        game.police.setLevel(4);
        m.hud.subtitle('Officer: Hey! That\'s Detective Voss\'s stuff! STOP HIM!', 'Officer', 3);
        await m.loseWanted('Escape with the files! Lose the cops.');
        await m.goTo(s.x, s.z, { radius: 2, text: 'Bring the files to <span class="b">Marisol</span>.' });
        await m.say('Marisol', 'This is it, Dre. Voss is done. Now... Deacon.', 3);
      },
    },
    {
      id: 'tower', title: 'Deacon\'s Tower', contact: 'L', requires: ['papertrail'], reward: 8000,
      log: 'Fought to the roof of Deacon Tower. Deacon confessed Tino was about to go to the Feds about Voss.',
      start: (L) => ({ x: L.court.x, z: L.court.z + 12 }),
      async run(m, game) {
        const L = m.L;
        const c = { x: L.court.x, z: L.court.z + 12 };
        const lou = m.ped(c.x - 2, c.z - 2, { appearance: look('lou'), invincible: true });
        m.speakersSet({ Lou: lou, Dre: m.player });
        await m.cutscene(async () => {
          m.face(lou, m.player); m.face(m.player, lou);
          m.twoShot(m.player, lou, 1, 3.4);
          await m.lines([
            ['Lou', 'Deacon took Salazar\'s money and bought himself the penthouse on that glass tower downtown.'],
            ['Lou', 'Last of his hired guns are in the plaza out front. Go get him, D. For Tino.'],
          ]);
        });
        const T = L.tower;
        const base = { x: T.x, z: T.z + 6 };
        const g1 = crew(m, 'cuervos', ring(base.x, base.z + 6, 8, 10), ['rifle', 'smg', 'shotgun', 'pistol'], { guard: true, health: 120, accuracy: 0.5 });
        aggroWhenNear(m, g1, 45);
        m.gps(base.x, base.z);
        await m.killAll(g1, 'Clear the <span class="r">guards</span> outside Deacon Tower.', { counter: 'GUARDS' });
        await m.goTo(T.x, T.z - 1.5, { onFoot: true, radius: 1.5, text: 'Take the <span class="y">elevator</span> to the roof.' });
        game.police.clear();
        const top = T.top;
        await fadeTeleport(m, top.x - 6, top.z - 6, Math.PI * 0.25, top.y);
        const deacon = m.enemy(top.x + 5, top.z + 5, { gang: 'cuervos', weapon: 'rifle', health: 350, armor: 100, appearance: look('deacon'), blip: false, guard: false });
        deacon.setPosition(top.x + 5, top.y, top.z + 5);
        m.blipEntity(deacon, 0xff3030, 'skull');
        const g2 = crew(m, 'cuervos', ring(top.x, top.z, 4, 7, 0.8), ['smg', 'shotgun', 'rifle', 'smg'], { guard: false, health: 110 });
        for (const g of g2) g.setPosition(g.pos.x, top.y, g.pos.z);
        m.speakersSet({ Deacon: deacon, Dre: m.player });
        m.hud.subtitle('Dre... I knew you\'d come. Let\'s finish this!', 'Deacon', 3);
        await m.killAll([deacon, ...g2], 'Kill <span class="r">Deacon</span>.');
        await m.lines([
          ['Deacon', '(dying) Tino... he was going to the Feds about Voss. Voss said... he\'d kill us all... I had no choice...'],
          ['Dre', 'You always had a choice, Deacon.'],
        ]);
        await fadeTeleport(m, T.x, T.z + 3, 0);
      },
    },
    {
      id: 'finale', title: 'Grand Finale', contact: 'M', requires: ['tower'], reward: 25000,
      chapterEnd: ['CHAPTER VI', 'Out of Town'],
      log: 'Chased Voss to the Santa Luz Pier and ended him. Tino can rest. Los Soles has a new king.',
      start: (L) => ({ x: L.home.x - 3, z: L.home.z + 2 }),
      async run(m, game) {
        const L = m.L;
        const s = { x: L.home.x - 3, z: L.home.z + 2 };
        const mari = m.ped(s.x - 1.6, s.z - 1, { appearance: look('marisol'), invincible: true });
        const lou = m.ped(s.x + 1.6, s.z - 1.3, { appearance: look('lou'), invincible: true });
        m.speakersSet({ Marisol: mari, Lou: lou, Dre: m.player });
        await m.cutscene(async () => {
          m.face(mari, m.player); m.face(lou, m.player); m.face(m.player, mari);
          m.twoShot(m.player, mari, 1, 3.6);
          await m.lines([
            ['Marisol', 'The files went out to every newsroom in the city this morning. Voss is finished.'],
            ['Lou', 'He knows it too. His cruiser just went tearing down toward the beach. Ruiz is with him.'],
            ['Dre', 'Then this ends at the pier. For Tino.'],
          ]);
        });
        game.env.setTime(18.2);
        const r = roadNear(L.home.x + 140, L.home.z + 40);
        const vcar = m.car('police', r.x, r.z, r.yaw);
        vcar.health = 1800;
        const voss = m.ped(r.x, r.z, { appearance: look('voss') }); vcar.putIn(voss, 0);
        const ruiz = m.ped(r.x, r.z, { appearance: look('ruiz') }); vcar.putIn(ruiz, 1);
        ruiz.giveWeapon('smg', 999); ruiz.equip('smg');
        m.driveBy(ruiz, null, 40);
        vcar.sirenOn = true;
        const P = L.pier;
        const dest = { x: (P.x0 + P.x1) / 2, z: CITY.maxZ - 8 };
        const drv = new RouteDriver(game, vcar, dest, { speed: 26 });
        vcar.ai = drv;
        m.blipEntity(voss, 0xff3030, 'skull');
        m.player.giveWeapon('rifle', 60);
        const road2 = roadNear(L.home.x, L.home.z + 20);
        m.car('brawler', road2.x, road2.z, road2.yaw, { color: 0x111111 });
        game.police.maxWanted = 1;
        m.objective('Chase <span class="r">Voss</span> to the pier!');
        await m.until(() => drv.arrived || vcar.isWrecked || voss.dead);
        // showdown on the pier
        const py = P.y;
        const spot = { x: (P.x0 + P.x1) / 2, z: P.z0 + 120 };
        const vp = voss.dead ? null : voss;
        if (vp && vp.vehicle) { vcar.takeOut(vp, new THREE.Vector3(spot.x, 0, spot.z)); vp.setPosition(spot.x, py, spot.z); }
        if (!ruiz.dead && ruiz.vehicle) { vcar.takeOut(ruiz, new THREE.Vector3(spot.x + 3, 0, spot.z - 4)); ruiz.setPosition(spot.x + 3, py, spot.z - 4); }
        for (const p of [voss, ruiz]) if (!p.dead) { p.brain = 'gang'; p.gang = 'cuervos'; p.threat = m.player; p.setState('attack'); p.persistent = true; }
        if (!voss.dead) { voss.giveWeapon('rifle', 999); voss.equip('rifle'); voss.health = 400; voss.maxHealth = 400; voss.armor = 100; voss.accuracy = 0.55; }
        const dirty = crew(m, 'cuervos', ring(spot.x, spot.z + 20, 4, 5), ['shotgun', 'pistol', 'smg', 'pistol'], { guard: false, health: 100 });
        for (const d of dirty) { d.setPosition(d.pos.x, py, d.pos.z); d.appearanceCop = true; }
        m.objective('End it. Kill <span class="r">Voss</span>.');
        await m.until(() => voss.dead);
        game.police.maxWanted = null;
        game.police.clear();
        await m.wait(1.5);
        // epilogue
        const fx = spot.x, fz = spot.z + 150;
        mari.setPosition(fx - 2, py, fz + 1.5);
        lou.setPosition(fx + 2, py, fz + 1.2);
        await fadeTeleport(m, fx, fz, Math.PI, py);
        game.env.setTime(19.0);
        await m.cutscene(async () => {
          m.face(mari, m.player); m.face(lou, m.player); m.face(m.player, mari);
          m.shot([fx + 12, py + 4, fz - 10], [fx, py + 1.5, fz], 45);
          await m.lines([
            ['Marisol', 'It\'s over. It\'s really over.'],
            ['Lou', 'Voss, Deacon, Salazar. All gone. Cedar Row\'s free, man.'],
          ]);
          m.twoShot(m.player, mari, -1, 3.3);
          await m.lines([
            ['Dre', 'Tino used to sit right here and watch the sun go down. Said one day he\'d own this whole city.'],
            ['Marisol', 'So what now, big brother? You going back East?'],
            ['Dre', 'Nah. Somebody\'s got to keep an eye on Los Soles.'],
          ]);
          m.shot([fx - 25, py + 18, fz + 60], [fx, py + 25, fz - 80], 55);
          await m.wait(4);
        });
        mari.keep = false;
        game.hud.showCredits?.();
      },
    },
    // ================================================================ CHAPTER VI — OUT OF TOWN
    {
      id: 'faregame', title: 'Fare Game', contact: 'M', requires: ['finale'], reward: 4000,
      log: 'Drove a cab out to Mirador, picked up Benny the bookkeeper and got him to a boat at Port Hale with Los Secos on our tail.',
      after: (g) => setTimeout(() => g.hud?.help('Rico\'s next job is out at <b>Dry Wells station</b>. It\'s a long way — whistle for a cab with <b>H</b> and skip the ride with <b>Space</b>.', 9), 1500),
      start: (L) => ({ x: L.home.x - 3, z: L.home.z + 2 }),
      async run(m, game) {
        const L = m.L;
        const s = { x: L.home.x - 3, z: L.home.z + 2 };
        const mari = m.ped(s.x - 1.6, s.z - 1, { appearance: look('marisol'), invincible: true });
        m.speakersSet({ Marisol: mari, Dre: m.player });
        await m.cutscene(async () => {
          m.face(mari, m.player); m.face(m.player, mari);
          m.twoShot(m.player, mari, 1, 3.4);
          await m.lines([
            ['Marisol', 'Voss had partners. A cartel out of Puerto Seco. Los Secos. They want his money, and they want his bookkeeper.'],
            ['Dre', 'Benny? The guy with the glasses?'],
            ['Marisol', 'He\'s hiding up in Mirador. Take a cab — nobody looks twice at a cab — and get him to the boat at Port Hale.'],
          ]);
        });
        const r = roadNear(L.home.x + 10, L.home.z + 24);
        const cab = m.car('taxi', r.x, r.z, r.yaw);
        await m.getIn(cab, 'Get in the <span class="b">cab</span>.');
        m.keepAlive(cab, 'The cab was destroyed.');
        const T = TOWNS.mirador;
        const pick1 = game.freeroam.roadSpot(T.x + 25, T.z + 10, false);
        const walk = game.freeroam.roadSpot(pick1.x, pick1.z, true);
        const benny = m.ped(walk.x, walk.z, { appearance: look('benny'), y: walk.y });
        benny.setState('guard'); benny.guardFace = pick1.yaw + Math.PI / 2;
        m.blipEntity(benny, 0x4a90ff, 'person');
        m.keepAlive(benny, 'Benny is dead.');
        await m.goTo(pick1.x, pick1.z, { vehicle: true, radius: 6, slow: true, inCar: cab, inCarMsg: 'Benny will only get in the cab.', text: 'Pick up <span class="b">Benny</span> in <span class="y">Mirador</span>.' });
        m.speakersSet({ Benny: benny, Dre: m.player });
        m.follower(benny, 0);
        await m.until(() => benny.vehicle === cab, { timeout: 12, onTimeout: 'resolve' });
        if (benny.vehicle !== cab) cab.putIn(benny, 2);
        await m.say('Benny', 'Drive! They\'ve been parked outside the diner all morning — tan pickup!', 3.5);
        m.failIf(() => m.player.vehicle !== cab && !game.vehicles.isBusy(m.player) && benny.vehicle === cab, 'You left Benny behind.');
        const back = game.freeroam.roadSpot(T.x - 120, T.z - 60, false);
        chaseCar(m, 'hauler', back.x, back.z, back.yaw, 'cuervos', 2, secoLook);
        setTimeout(() => { if (m.game.missions.active === m) m.hud.subtitle('They\'re shooting at a TAXI! Who shoots at a taxi?!', 'Benny', 3); }, 9000);
        const pier = L.halePier || { x: TOWNS.hale.x + 130, z: TOWNS.hale.z };
        const dock = game.freeroam.roadSpot(pier.x - 70, pier.z, false);
        await m.goTo(dock.x, dock.z, { vehicle: true, radius: 7, inCar: cab, text: 'Get Benny to the boat at <span class="y">Port Hale</span>.' });
        cab.input.brake = 1;
        await m.say('Benny', 'I owe you, Dre. Here — Voss\'s ledger. Page forty. That\'s where the cartel\'s money goes.', 4);
        game.vehicles.exit(benny);
      },
    },
    {
      id: 'solline', title: 'Sol Line Express', contact: 'R', requires: ['faregame'], reward: 6000,
      log: 'Took the Los Secos gun train at Dry Wells, rammed through their roadblock and brought it into Union Station.',
      start: (L) => { const st = L.station_dry || L.home; return { x: st.x - Math.cos(st.rot || 0) * 26, z: st.z + Math.sin(st.rot || 0) * 26 }; },
      async run(m, game) {
        const L = m.L;
        const train = game.rail?.train && !game.rail.train.removed ? game.rail.train : game.rail?.spawnTrain();
        const dry = station(game, 'dry'), fern = station(game, 'fern'), union = station(game, 'union');
        if (!train || !dry || !union) throw new MissionFail('The Sol Line is closed today.');
        const lm = L.station_dry;
        // the evening gun train, held at the platform
        train.s = dry.s + train.len / 2; train.v = 0; train.dwell = 1e9; train._atStation = dry; train.dirS = 1; train._place();
        const s0 = { x: lm.x - Math.cos(lm.rot) * 24, z: lm.z + Math.sin(lm.rot) * 24 };
        const rico = m.ped(s0.x + 1.5, s0.z + 1, { appearance: look('rico'), invincible: true });
        m.speakersSet({ Rico: rico, Dre: m.player });
        await m.cutscene(async () => {
          m.face(rico, m.player); m.face(m.player, rico);
          m.twoShot(m.player, rico, -1, 3.4);
          await m.lines([
            ['Rico', 'Benny\'s ledger checks out. Los Secos move their guns into the city on the Sol Line. That train right there.'],
            ['Rico', 'Three of their boys on the platform. Take them out, climb in the cab up front and drive her into Union Station. I\'ll meet you there.'],
            ['Dre', 'I\'ve never driven a train.'],
            ['Rico', 'Push the lever forward. Try not to hit anything. Well — hit whatever they put in your way.'],
          ]);
        });
        rico.setPosition(rico.pos.x, undefined, rico.pos.z - 400);
        const tx = Math.sin(lm.rot), tz = Math.cos(lm.rot);
        const guards = secos(m, [-22, 0, 24].map((a) => ({ x: lm.x + tx * a, z: lm.z + tz * a, y: lm.y })), ['rifle', 'shotgun', 'pistol'], { face: lm.rot + Math.PI / 2 });
        aggroWhenNear(m, guards, 40);
        m.player.giveWeapon('smg', 90);
        await m.killAll(guards, 'Take out the <span class="r">cartel guards</span> on the platform.');
        await m.getIn(train, 'Climb into the <span class="b">train cab</span> (at the front).');
        train.dwell = 0;
        m.failIf(() => m.player.vehicle !== train && !game.vehicles.isBusy(m.player), 'You abandoned the train.');
        const stopT = m.timer(240, 'Los Secos got their guns back.');
        const ub = { x: union.x, z: union.z, color: 0xffd23f, icon: 'train' };
        game.blips.add(ub); m.blips.push(ub);
        m.objective('Drive the train to <span class="y">Union Station</span>. Hold <b>W</b> to accelerate, <b>S</b> to brake.');
        // the welcome party at Fern Creek: a truck on the Main Street crossing and gunmen on the platform
        let ambushed = false;
        const xing = game.map.roadInfo.rail.crossings.find((c) => c.kind === 'level' && c.s > (fern?.s ?? 0));
        m.tick(() => {
          if (ambushed || !fern || train.s < fern.s - 700) return;
          ambushed = true;
          if (xing) { const blk = m.car('hauler', xing.x, xing.z, Math.random() * 6, { color: 0xc2a878 }); blk.parked = true; }
          const fl = L.station_fern;
          const ftx = Math.sin(fl.rot), ftz = Math.cos(fl.rot);
          secos(m, [-30, -10, 10, 30].map((a) => ({ x: fl.x + ftx * a, z: fl.z + ftz * a, y: fl.y })), ['rifle', 'smg', 'rifle', 'shotgun'], { guard: false, accuracy: 0.3 });
          m.hud.subtitle('Rico here — they know you\'re coming. Fern Creek\'s crawling with them. Don\'t stop!', 'Rico', 4.5);
        });
        await m.until(() => train.centreS() > union.s - 30 && Math.abs(train.v) < 1.5, {});
        stopT();
        await m.say('Rico', 'Ha! Right on the platform. Two crates of rifles the cartel will never see again.', 3.5);
        train._atStation = union; train.dwell = 14; train.dirS = -1;
      },
    },
    {
      id: 'dustoff', title: 'Dust Off', contact: 'R', requires: ['solline'], reward: 5000,
      log: 'Flew the Skipper low through the canyons and buzzed the Los Secos compound so Rico\'s spotter could map it.',
      start: () => ({ x: AIRFIELD.x - 30, z: AIRFIELD.z - 130 }),
      async run(m, game) {
        const A = AIRFIELD;
        const rico = m.ped(A.x - 27, A.z - 128, { appearance: look('rico'), invincible: true });
        m.speakersSet({ Rico: rico, Dre: m.player });
        await m.cutscene(async () => {
          m.face(rico, m.player); m.face(m.player, rico);
          m.twoShot(m.player, rico, 1, 3.4);
          await m.lines([
            ['Rico', 'Their compound\'s in Puerto Seco. Walls, lookouts, the works. We need eyes on it before we hit it.'],
            ['Rico', 'Take the Skipper. Stay low through the rings so their lookouts don\'t spot you, buzz the compound, and bring her home in one piece.'],
          ]);
        });
        const plane = m.car('skipper', A.x + 230, A.z, -Math.PI / 2, { color: 0xe8e8e8 });
        await m.getIn(plane, 'Get in the <span class="b">Skipper</span>.');
        m.keepAlive(plane, 'The Skipper was destroyed.');
        m.help('W / S: throttle. Pull back (mouse or ↓) to climb, A / D to roll into turns. Fly through the rings.', 8);
        const stopT = m.timer(300, 'Their lookouts spotted you. Too slow.');
        const route = [
          { x: A.x - 620, z: A.z + 20, alt: 60 },
          { x: -3700, z: 680, alt: 70 },
          { x: -4150, z: 820, alt: 60 },
          { x: -4000, z: 470, alt: 40, buzz: true },
          { x: -3700, z: 180, alt: 70 },
          { x: A.x - 420, z: A.z, alt: 45 },
        ];
        for (let i = 0; i < route.length; i++) {
          const r = route[i];
          if (r.buzz) {
            secos(m, [[-40, 10], [30, -25], [0, 45], [55, 30]].map(([dx, dz]) => ({ x: TOWNS.seco.x + dx, z: TOWNS.seco.z + dz })), ['rifle', 'rifle', 'smg', 'rifle'], { guard: false, accuracy: 0.25 });
          }
          await airRing(m, r.x, r.z, r.alt, { next: route[i + 1] || A, text: r.buzz ? 'Buzz the <span class="r">Los Secos compound</span>!' : `Fly through the rings (${i + 1}/${route.length}).` });
        }
        await m.goTo(A.x + 40, A.z, { vehicle: true, radius: 16, slow: true, inCar: plane, text: 'Land the Skipper on the <span class="y">runway</span>.' });
        stopT();
        await m.say('Rico', 'Smooth. My guy got every wall and every window. Tomorrow, we go in.', 3);
      },
    },
    {
      id: 'secosunrise', title: 'Seco Sunrise', contact: 'R', requires: ['dustoff'], reward: 15000,
      log: 'Took a Warhawk gunship to Puerto Seco at dawn, burned the Los Secos trucks and ran El Seco down in the desert.',
      chapterEnd: ['LOS SOLES', 'Thanks for playing'],
      start: () => ({ x: AIRFIELD.x - 30, z: AIRFIELD.z - 130 }),
      async run(m, game) {
        const A = AIRFIELD;
        game.env.setTime(6.2);
        const rico = m.ped(A.x - 27, A.z - 128, { appearance: look('rico'), invincible: true });
        const mari = m.ped(A.x - 33, A.z - 127, { appearance: look('marisol'), invincible: true });
        m.speakersSet({ Rico: rico, Marisol: mari, Dre: m.player });
        await m.cutscene(async () => {
          m.face(rico, m.player); m.face(mari, m.player); m.face(m.player, rico);
          m.twoShot(m.player, rico, 1, 3.6);
          await m.lines([
            ['Marisol', 'Army base says one of their gunships is "in for maintenance". It\'s behind the hangar.'],
            ['Rico', 'Three trucks in the compound. That\'s the money, the guns, everything. Burn them.'],
            ['Dre', 'And El Seco?'],
            ['Marisol', 'He\'ll run. They always run.'],
          ]);
        });
        const heli = m.car('warhawk', A.x - 70, A.z - 110, 0);
        await m.getIn(heli, 'Get in the <span class="b">Warhawk</span>.');
        m.help('Space / Shift: climb & descend. W / S: nose down / up. A / D: turn. Left mouse: minigun, right mouse: rockets.', 9);
        await airRing(m, TOWNS.seco.x + 260, TOWNS.seco.z - 40, 70, { r: 26, next: TOWNS.seco, text: 'Fly to <span class="y">Puerto Seco</span>.' });
        // the compound: three trucks and the gunmen guarding them
        const C = TOWNS.seco;
        const trucks = [];
        for (const [dx, dz] of [[-30, 25], [35, -20], [10, 60]]) {
          const sp = game.freeroam.roadSpot(C.x + dx, C.z + dz, false);
          const t = m.car('hauler', sp.x, sp.z, sp.yaw, { color: 0xc2a878 });
          t.parked = true; t.locked = true;
          m.blipEntity(t, 0xff3030, 'car');
          trucks.push(t);
        }
        const guns = secos(m, [[-50, 0], [-20, 40], [25, 20], [50, -35], [0, -45], [60, 55], [-45, 60], [15, 90]].map(([dx, dz]) => ({ x: C.x + dx, z: C.z + dz })), ['rifle', 'rifle', 'smg', 'rpg', 'rifle', 'shotgun', 'rifle', 'rpg'], { guard: false, accuracy: 0.3 });
        m.objective('Destroy the three <span class="r">cartel trucks</span>.');
        await m.until(() => trucks.every((t) => t.isWrecked || t.exploded), {});
        m.hud.subtitle('That\'s the last of their money going up. Now where\'s the old man?', 'Dre', 3);
        // El Seco makes a run for it across the desert
        const esc = game.freeroam.roadSpot(C.x - 80, C.z + 30, false);
        const car = m.car('summit', esc.x, esc.z, esc.yaw, { color: 0xe9d8a6 });
        const boss = m.ped(esc.x, esc.z, { appearance: look('elseco') });
        car.putIn(boss, 0);
        car.health = 1500;
        car.ai = new RouteDriver(game, car, { x: TOWNS.dry.x, z: TOWNS.dry.z }, { speed: 24 });
        m.blipEntity(boss, 0xff3030, 'skull');
        m.failIf(() => !boss.dead && car.ai?.arrived, 'El Seco got away.');
        m.objective('<span class="r">El Seco</span> is running. Stop him!');
        await m.until(() => boss.dead);
        for (const g2 of guns) if (!g2.dead) g2.setState('flee');
        await m.wait(1.5);
        await m.say('Rico', 'That\'s it, D. No more Voss, no more Secos. The whole state\'s yours.', 4);
        await m.say('Dre', 'Tino would\'ve loved this. Bring it home.', 3);
      },
    },
  ],
};
