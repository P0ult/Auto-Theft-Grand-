// AUTO THEFT GRAND — chapters VII and VIII: San Aurelio. Nine long missions up the coast.
// Voss's money went north. It's being washed through San Aurelio by Vincent Castell's Harbor Saints: the
// port, the arena, the logging trucks out of Timberline. His daughter Nina wants him gone.
import * as THREE from 'three';
import { RouteDriver, MissionFail } from './missions.js';
import { BoatDriver } from './boats.js';
import { saintLook } from './peds.js';
import { WEAPONS } from './weapondefs.js';
import { randomAppearance } from '../entities/humanoid.js';
import { TOWNS, NCITY } from '../world/worldgen.js';
import { WATER_Y, CITY } from '../world/citymap.js';
import { rand, pick, clamp, RNG } from '../core/utils.js';

const NCAST = {
  nina: { female: true, skin: 0xe0ac87, hair: 0x3a1f12, hairStyle: 'long', hat: null, shirt: 0x151515, shirtType: 'jacket', jacketColor: 0x7a1f2b, pants: 0x1a1a1a, shorts: false, shoes: 0x111111, build: 0.94, height: 0.98, glasses: false, beard: false, bandana: null, uniform: null },
  kiko: { female: false, skin: 0xa86b4a, hair: 0x1a1a1a, hairStyle: 'cap', hat: 0x2a9d8f, shirt: 0xf4a261, shirtType: 'tee', pants: 0x264653, shorts: true, shoes: 0xffffff, build: 0.88, height: 0.94, glasses: false, beard: false, bandana: null, jacketColor: 0x111111, uniform: null },
  castell: { female: false, skin: 0xf1c7a5, hair: 0xb0b0b0, hairStyle: 'short', hat: null, shirt: 0xffffff, shirtType: 'jacket', jacketColor: 0x1d3557, pants: 0x1d3557, shorts: false, shoes: 0x111111, build: 1.1, height: 1.02, glasses: true, beard: true, bandana: null, uniform: null },
  padre: { female: false, skin: 0xc68863, hair: 0x777777, hairStyle: 'short', hat: null, shirt: 0x111111, shirtType: 'long', pants: 0x111111, shorts: false, shoes: 0x111111, build: 1.0, height: 0.98, glasses: true, beard: false, bandana: null, jacketColor: 0x111111, uniform: null },
};

export function northMissions(H) {
  const { look, chaseCar, fadeTeleport, crewSupport } = H;
  const nlook = (k) => ({ ...NCAST[k] });
  const spot = (game, x, z, foot = false) => game.freeroam.roadSpot(x, z, foot) || { x, z, yaw: 0, y: game.map.groundHeight(x, z) };
  // nearest open water at least `depth` deep, spiralling out
  const waterSpot = (game, x, z, depth = 2.5) => {
    const map = game.map;
    const deep = (px, pz) => map.waterLevel(px, pz) - map.terrainHeight(px, pz) > depth && game.collision.floorHeight(px, pz, 2) < WATER_Y + 0.3;
    if (deep(x, z)) return { x, z };
    for (let r = 6; r < 500; r += 6) for (let a = 0; a < 20; a++) { const px = x + Math.cos(a / 20 * 6.283) * r, pz = z + Math.sin(a / 20 * 6.283) * r; if (deep(px, pz)) return { x: px, z: pz }; }
    return { x, z };
  };
  const saints = (m, pts, weapons, opts = {}) => pts.map((p, i) => {
    const e = m.enemy(p.x, p.z, { gang: 'saints', weapon: weapons[i % weapons.length], guard: opts.guard ?? true, face: opts.face, health: opts.health ?? 110, appearance: saintLook(), accuracy: opts.accuracy ?? 0.4 });
    if (p.y != null) e.setPosition(p.x, p.y, p.z);
    return e;
  });
  const around = (cx, cz, n, r0, r1, ph = 0) => { const out = []; for (let i = 0; i < n; i++) { const a = ph + i / n * Math.PI * 2 + rand(-0.2, 0.2), r = rand(r0, r1); out.push({ x: cx + Math.cos(a) * r, z: cz + Math.sin(a) * r }); } return out; };
  // a boat with a Saints driver (and a gunner), running a route
  const saintBoat = (m, type, x, z, route, pace, gunner = true, drvLook = null) => {
    const game = m.game;
    const b = m.car(type, x, z, Math.atan2(route[0].x - x, route[0].z - z));
    const drv = m.ped(x, z, { appearance: drvLook || saintLook(), brain: 'script' });
    b.putIn(drv, 0);
    let gun = null;
    if (gunner && b.def.seats?.length > 1) { gun = m.ped(x, z, { appearance: saintLook(), brain: 'script' }); gun.giveWeapon('smg', 999); gun.equip('smg'); b.putIn(gun, 1); m.driveBy(gun, null, 45); }
    b.ai = new BoatDriver(game, b, route, 0, pace);
    return { b, drv, gun };
  };
  // a kerbside lane point near (x,z) whose traffic runs towards (tx,tz)
  const laneTowards = (game, x, z, tx, tz) => {
    const net = game.map.roads, c = net.closest(x, z, (e) => !e.removed && e.type !== 'rail' && e.type !== 'dirt');
    if (!c) return spot(game, x, z);
    const e = c.e, t = [0, 0, 0, 0, 0];
    net.at(e, clamp(c.s, 8, e.len - 8), t);
    let dir = (tx - t[0]) * t[3] + (tz - t[2]) * t[4] >= 0 ? 0 : 1;
    if ((dir === 0 ? e.lanesF : e.lanesB) < 1) dir = 1 - dir;
    const fx = dir ? -t[3] : t[3], fz = dir ? -t[4] : t[4];
    const off = net.laneOffset(e, dir, 0);
    return { x: t[0] - fz * off, z: t[2] + fx * off, y: t[1], yaw: Math.atan2(fx, fz) };
  };
  const plaza = (L) => L.aurelio || { x: NCITY.x, z: NCITY.z };

  return [
    // ================================================================ CHAPTER VII — SAN AURELIO
    {
      id: 'northstar', title: 'North Star', contact: 'M', requires: ['secosunrise'], reward: 6000,
      log: 'Drove Marisol up the coast to San Aurelio, shook off a Harbor Saints ambush at Gull Bay and met Nina Castell on the Plaza.',
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
            ['Marisol', 'Benny\'s ledger, page forty. Voss\'s money didn\'t stay in Puerto Seco. It went north. San Aurelio.'],
            ['Dre', 'Never been. Big place?'],
            ['Marisol', 'Big enough. The port\'s run by a man called Vincent Castell and his Harbor Saints. They wash dirty money for half the coast.'],
            ['Marisol', 'His daughter reached out. Nina. She wants to talk. Drive me up — Bayshore Road, then the Aurelio Highway.'],
          ]);
        });
        const r = spot(game, L.home.x + 10, L.home.z + 24);
        const car = m.car('kestrel', r.x, r.z, r.yaw, { color: 0x20242c });
        await m.getIn(car, 'Get in the <span class="b">car</span>.');
        m.keepAlive(car, 'The car was destroyed.');
        m.follower(mari, 0);
        await m.until(() => mari.vehicle === car, { timeout: 12, onTimeout: 'resolve' });
        if (mari.vehicle !== car) car.putIn(mari, 1);
        m.failIf(() => m.player.vehicle !== car && !game.vehicles.isBusy(m.player) && mari.vehicle === car && m.distTo(car) > 40, 'You left Marisol behind.');
        m.help('It\'s a long drive. Follow the GPS line up <b>Bayshore Road</b> and onto the <b>Aurelio Highway</b>.', 7);
        const G = TOWNS.gull;
        const g1 = spot(game, G.x, G.z + 260);
        await m.goTo(g1.x, g1.z, { vehicle: true, radius: 9, inCar: car, text: 'Drive to <span class="y">Gull Bay</span>.' });
        await m.say('Marisol', 'Two cars coming up fast behind us. Navy jackets — that\'s the Saints. Somebody talked.', 3.5);
        const back = spot(game, G.x - 10, G.z + 420);
        const chasers = [chaseCar(m, 'brawler', back.x, back.z, back.yaw + Math.PI, 'saints', 1, saintLook)];
        const back2 = spot(game, G.x + 6, G.z + 520);
        chasers.push(chaseCar(m, 'kestrel', back2.x, back2.z, back2.yaw + Math.PI, 'saints', 1, saintLook));
        m.objective('Shake off or wreck the <span class="r">Saints</span>.');
        await m.until(() => chasers.every((c) => c.v.isWrecked || c.drv.dead || m.distTo(c.v) > 280));
        await m.say('Marisol', 'Welcome to San Aurelio. Nina said the Plaza — the fountain in the middle of town.', 3.5);
        const P = plaza(L);
        const pz = spot(game, P.x + 20, P.z + 60);
        await m.goTo(pz.x, pz.z, { vehicle: true, radius: 8, slow: true, inCar: car, text: 'Meet Nina at the <span class="y">Plaza de Aurelio</span>.' });
        car.input.brake = 1;
        const w = spot(game, pz.x, pz.z, true);
        const nina = m.ped(w.x, w.z, { appearance: nlook('nina'), invincible: true, y: w.y });
        m.speakersSet({ Nina: nina, Marisol: mari, Dre: m.player });
        await m.wait(0.8);
        game.vehicles.exit(m.player);
        await m.until(() => !m.player.vehicle && !game.vehicles.isBusy(m.player), { timeout: 5, onTimeout: 'resolve' });
        await m.cutscene(async () => {
          m.face(nina, m.player); m.face(m.player, nina);
          m.twoShot(m.player, nina, -1, 3.5);
          await m.lines([
            ['Nina', 'You\'re the one who took down Voss. You look shorter on the news.'],
            ['Dre', 'You\'re Castell\'s kid. Why would you help us?'],
            ['Nina', 'Because my father\'s going to get me killed. The Saints bring guns in by boat, cash out through the arena, product down from Timberline on the log trucks.'],
            ['Nina', 'Pull it apart one piece at a time and he\'ll have nothing left. Start with Kiko — my runner. You\'ll find him at Bayview Park.'],
          ]);
        });
      },
    },
    {
      id: 'boardmeeting', title: 'Board Meeting', contact: 'K', requires: ['northstar'], reward: 4500,
      log: 'Ran Nina\'s messages across San Aurelio on Kiko\'s skateboard against the clock, then saved Kiko from a Saints beating at Bayview Park.',
      start: (L) => { const p = L.aurPark || plaza(L); return { x: p.x + 4, z: p.z + 4 }; },
      async run(m, game) {
        const L = m.L;
        const P = L.aurPark || plaza(L);
        const kiko = m.ped(P.x + 6, P.z + 3, { appearance: nlook('kiko') });
        kiko.health = kiko.maxHealth = 400;
        m.speakersSet({ Kiko: kiko, Dre: m.player });
        await m.cutscene(async () => {
          m.face(kiko, m.player); m.face(m.player, kiko);
          m.twoShot(m.player, kiko, 1, 3.2);
          await m.lines([
            ['Kiko', 'You\'re Nina\'s guy? Cool. The Saints listen to every phone in town, so we do it old school. On wheels.'],
            ['Kiko', 'Four drops. Cathedral, the Plaza, the harbour, Northgate. Clock\'s tight — but the crew pays extra for style.'],
            ['Kiko', 'Ollie with Space, flip with A or D, shove-it with S. Land clean and I\'ll give you time back.'],
          ]);
        });
        const sp = spot(game, P.x, P.z, true);
        const board = m.car('skateboard', sp.x, sp.z, sp.yaw);
        await m.getIn(board, 'Get on the <span class="b">skateboard</span>.');
        m.failIf(() => board.removed, 'You lost the board.');
        // a stretchable clock: each clean trick buys a few seconds
        let end = m.t + 80;
        const stopClock = m.tick(() => m.hud.setTimer(end - m.t));
        m.failIf(() => m.t > end, 'Too slow. The message didn\'t get through.');
        const off = game.events.on('skateTrick', (name) => { if (m.game.missions.active === m) { end += 4; m.hud.help(`<b>${name}</b> — +4 seconds`, 1.4); } });
        m.tick(() => { if (m.game.missions.active !== m) off(); });
        const drops = [
          ['the <span class="y">Cathedral</span>', L.aurCathedral || { x: NCITY.x - 220, z: NCITY.z }],
          ['the <span class="y">Plaza</span>', { x: NCITY.x + 30, z: NCITY.z + 38 }],
          ['the <span class="y">harbour</span>', L.aurHarbor || { x: NCITY.x + 540, z: NCITY.z }],
          ['<span class="y">Northgate</span>', { x: NCITY.x + 40, z: NCITY.z - 330 }],
        ];
        for (let i = 0; i < drops.length; i++) {
          const [name, p] = drops[i];
          const d = spot(game, p.x, p.z, true);
          await m.goTo(d.x, d.z, { vehicle: true, radius: 3.5, text: `Skate to ${name} (${i + 1}/${drops.length}).`, condition: () => m.player.vehicle === board || !m.player.vehicle });
          end += 45;
          m.cash(150);
        }
        stopClock(); m.hud.setTimer(null);
        m.hud.subtitle('Dre! They followed me to the park — help!', 'Kiko', 3.5);
        const kp = { x: P.x + rand(-8, 8), z: P.z + rand(-8, 8) };
        kiko.setPosition(kp.x, undefined, kp.z);
        kiko.setState('cower'); kiko.cowerTime = 999;
        m.keepAlive(kiko, 'Kiko is dead.');
        m.blipEntity(kiko, 0x4a90ff, 'person');
        const thugs = saints(m, around(kp.x, kp.z, 5, 4, 9), ['bat', 'knife', 'pistol', 'bat', 'pistol'], { guard: false });
        for (const t of thugs) t.threat = m.player;
        m.player.giveWeapon('pistol', 60);
        await m.killAll(thugs, 'Save <span class="b">Kiko</span> from the <span class="r">Saints</span>.');
        kiko.setState('idle');
        await m.say('Kiko', 'Man, you can ride. Tell Nina the drops are done. I owe you one.', 3.5);
      },
    },
    {
      id: 'lowtide', title: 'Low Tide', contact: 'N', requires: ['northstar'], reward: 7000,
      log: 'Took a speedboat out from Aurelio Marina at dusk, ran down the Saints\' smuggling boat off the coast, fished out the package and lost the coast guard.',
      start: (L) => { const mr = L.aurMarina || plaza(L); return { x: mr.x - 4, z: mr.z + 3 }; },
      async run(m, game) {
        const L = m.L;
        const mr = L.aurMarina;
        if (!mr) throw new MissionFail('The marina is closed today.');
        game.env.setTime(19.4);
        const nina = m.ped(mr.x - 2, mr.z + 4, { appearance: nlook('nina'), invincible: true });
        m.speakersSet({ Nina: nina, Dre: m.player });
        await m.cutscene(async () => {
          m.face(nina, m.player); m.face(m.player, nina);
          m.twoShot(m.player, nina, 1, 3.4);
          await m.lines([
            ['Nina', 'Every Thursday a boat comes down the coast from the north with a package for my father. Tonight it doesn\'t arrive.'],
            ['Nina', 'Take the speedboat on the end of the pontoon. Catch it before it gets to the dock at Gull Bay.'],
            ['Dre', 'And the coast guard?'],
            ['Nina', 'Oh, they\'ll come. They always come. Don\'t let them catch you with it.'],
          ]);
        });
        const pt = mr.pontoon || { x1: mr.x + 80, z: mr.z };
        const bs = waterSpot(game, pt.x1 - 20, pt.z - 9, 1.6);
        const boat = m.car('speedboat', bs.x, bs.z, Math.PI / 2, { color: 0xf4f4f0 });
        await m.getIn(boat, 'Take the <span class="b">speedboat</span> at the end of the pontoon.');
        m.keepAlive(boat, 'Your boat sank.');
        // the smugglers: out of the north-east, hugging the coast down towards Gull Bay
        const route = [[1500, -4300], [1470, -3900], [1440, -3500], [1390, -3100], [1330, -2880], [1300, -2820]].map(([x, z]) => ({ x, z }));
        const start = waterSpot(game, 1540, -4700, 4);
        const sm = saintBoat(m, 'cruiser', start.x, start.z, route, 0.72);
        sm.b.health = sm.b.maxHealth = 900;
        m.blipEntity(sm.b, 0xff3030, 'boat');
        m.failIf(() => !sm.drv.dead && Math.hypot(sm.b.pos.x - 1300, sm.b.pos.z + 2820) < 70, 'The package reached the Saints.');
        m.objective('Intercept the <span class="r">Saints\' boat</span> before it reaches Gull Bay. Shoot the driver or sink it.');
        await m.until(() => sm.drv.dead || sm.b.isWrecked || sm.b.exploded);
        sm.b.ai = null;
        const pk = { x: sm.b.pos.x + rand(-4, 4), z: sm.b.pos.z + rand(-4, 4) };
        await m.goTo(pk.x, pk.z, { vehicle: true, radius: 7, y: WATER_Y + 0.1, color: 0x6cff6c, text: 'Grab the floating <span class="g">package</span>.' });
        m.cash(1500);
        m.wanted(2);
        m.hud.subtitle('Coast guard! Cut your lights and run!', 'Nina', 3);
        await m.loseWanted('Lose the <span class="r">coast guard</span>.');
        const home = waterSpot(game, (pt.x0 + pt.x1) / 2, pt.z + 10, 1.4);
        await m.goTo(home.x, home.z, { vehicle: true, radius: 9, y: WATER_Y + 0.1, text: 'Bring the package back to <span class="y">Aurelio Marina</span>.' });
        await m.say('Nina', 'That\'s his whole week\'s shipment in the bottom of the bay. He\'ll feel that.', 3.5);
      },
    },
    {
      id: 'sanctuary', title: 'Sanctuary', contact: 'N', requires: ['boardmeeting', 'lowtide'], reward: 8000,
      log: 'Held off three waves of Saints at the Cathedral of San Aurelio and drove Padre Ignacio and his ledger to safety in Timberline.',
      start: (L) => { const c = L.aurCathedral || plaza(L); return { x: c.x + 6, z: c.z + 6 }; },
      async run(m, game) {
        const L = m.L;
        const C = L.aurCathedral || plaza(L);
        const sp = spot(game, C.x, C.z, true);
        const padre = m.ped(sp.x, sp.z, { appearance: nlook('padre'), y: sp.y });
        padre.health = padre.maxHealth = 600;
        const nina = m.ped(sp.x + 1.5, sp.z + 1, { appearance: nlook('nina'), invincible: true, y: sp.y });
        m.speakersSet({ Padre: padre, Nina: nina, Dre: m.player });
        await m.cutscene(async () => {
          m.face(padre, m.player); m.face(m.player, padre); m.face(nina, m.player);
          m.twoShot(m.player, padre, 1, 3.6);
          await m.lines([
            ['Padre', 'Your father\'s men made their confessions here for twenty years, Nina. I wrote down the ones that mattered.'],
            ['Nina', 'He knows, Padre. They\'re on their way. Dre — keep him alive, then get him out of the city.'],
            ['Padre', 'I haven\'t held a gun since the seminary. I trust you have.'],
          ]);
        });
        nina.setPosition(nina.pos.x, undefined, nina.pos.z - 300);
        m.keepAlive(padre, 'Padre Ignacio is dead.');
        m.blipEntity(padre, 0x4a90ff, 'person');
        padre.setState('cower'); padre.cowerTime = 999;
        m.player.giveWeapon('rifle', 180); m.player.giveWeapon('shotgun', 30); m.player.switchTo('rifle');
        const all = [];
        const wave = async (n, list, text) => {
          m.hud.bigMessage(`WAVE ${n}`, 'hint', 2);
          all.push(...list);
          await m.killAll(list, text, { counter: 'SAINTS' });
        };
        await wave(1, saints(m, around(sp.x, sp.z, 5, 38, 55), ['pistol', 'smg', 'pistol', 'shotgun', 'smg'], { guard: false }), 'Defend the <span class="b">Padre</span> from the <span class="r">Saints</span>.');
        await m.wait(2);
        const cars = [];
        for (const k of [0, 1]) {
          const cs = spot(game, sp.x + (k ? 120 : -120), sp.z + (k ? -60 : 70));
          const c = chaseCar(m, 'brawler', cs.x, cs.z, cs.yaw, 'saints', 1, saintLook);
          cars.push(c.drv, ...c.guns);
        }
        await wave(2, [...cars, ...saints(m, around(sp.x, sp.z, 3, 40, 50, 1), ['rifle', 'smg', 'shotgun'], { guard: false })], 'They brought cars. Take them out.');
        await m.wait(2);
        await wave(3, saints(m, around(sp.x, sp.z, 6, 40, 60, 2), ['rifle', 'smg', 'shotgun', 'rifle', 'rpg', 'smg'], { guard: false, health: 130 }), 'Last push. Hold them off!');
        padre.setState('idle');
        await m.say('Padre', 'God forgive me, that was exhilarating. Now — the car. Timberline, there\'s a lodge up there.', 3.5);
        const cr = spot(game, sp.x + 20, sp.z + 20);
        const car = m.car('summit', cr.x, cr.z, cr.yaw, { color: 0x3a4a3a });
        await m.getIn(car, 'Get in the <span class="b">car</span>.');
        m.keepAlive(car, 'The car was destroyed.');
        m.follower(padre, 0);
        await m.until(() => padre.vehicle === car, { timeout: 14, onTimeout: 'resolve' });
        if (padre.vehicle !== car) car.putIn(padre, 1);
        m.failIf(() => m.player.vehicle !== car && !game.vehicles.isBusy(m.player) && padre.vehicle === car && m.distTo(car) > 40, 'You left the Padre behind.');
        let chased = false;
        m.tick(() => {
          if (chased) return;
          const p = car.pos;
          if (Math.hypot(p.x - NCITY.x, p.z - NCITY.z) < 560) return;
          chased = true;
          const b = spot(game, p.x + 150, p.z + 40);
          chaseCar(m, 'kestrel', b.x, b.z, b.yaw, 'saints', 1, saintLook);
          m.hud.subtitle('Behind us! They\'re not giving up!', 'Padre', 3);
        });
        const T = TOWNS.timber;
        const lodge = spot(game, T.x + 30, T.z + 20);
        await m.goTo(lodge.x, lodge.z, { vehicle: true, radius: 8, slow: true, inCar: car, text: 'Drive the Padre up the Timber Road to <span class="y">Timberline</span>.' });
        car.input.brake = 1;
        await m.say('Padre', 'Here. Every name, every payment. Give it to Nina — and tell her I\'ll pray for her father. Someone should.', 4.5);
        game.vehicles.exit(padre);
      },
    },
    {
      id: 'boxoffice', title: 'Box Office', contact: 'N', requires: ['sanctuary'], reward: 12000,
      chapterEnd: ['CHAPTER VIII', 'Harbor Saints'],
      log: 'Hit the Aurelio Arena box office on fight night, cracked the cash room, outran a three-star manhunt and laid low in Cedar Ridge.',
      start: (L) => { const a = L.aurArena || plaza(L); return { x: a.x + 60, z: a.z + 10 }; },
      async run(m, game) {
        const L = m.L;
        const A = L.aurArena || plaza(L);
        game.env.setTime(21.5);
        const ns = spot(game, A.x + 60, A.z + 10, true);
        const nina = m.ped(ns.x + 1.2, ns.z + 1, { appearance: nlook('nina'), invincible: true, y: ns.y });
        m.speakersSet({ Nina: nina, Dre: m.player });
        await m.cutscene(async () => {
          m.face(nina, m.player); m.face(m.player, nina);
          m.twoShot(m.player, nina, -1, 3.4);
          await m.lines([
            ['Nina', 'Fight night. Every dollar the Saints wash goes through that box office tonight — cash, no questions.'],
            ['Nina', 'Six guards outside, a strongroom by the south doors. Crack it, grab it, and get out of town. There\'s a garage in Cedar Ridge.'],
            ['Dre', 'The cops?'],
            ['Nina', 'Half of them are at the fight. The other half will be very, very angry.'],
          ]);
        });
        nina.setPosition(nina.pos.x, undefined, nina.pos.z - 300);
        m.player.giveWeapon('smg', 180); m.player.giveWeapon('grenade', 4);
        const guards = saints(m, around(A.x, A.z, 6, 34, 46), ['smg', 'pistol', 'shotgun', 'smg', 'rifle', 'pistol']);
        H.aggroWhenNear(m, guards, 32);
        await m.killAll(guards, 'Take out the <span class="r">arena guards</span>.', { counter: 'GUARDS' });
        // the strongroom: stand at it while the lock gives
        const sr = spot(game, A.x, A.z + 30, true);
        const mk = m.marker(sr.x, sr.z, { radius: 1.2, color: 0x6cff6c, label: 'Strongroom', footOnly: true, icon: 'money' });
        m.objective('Crack the <span class="g">strongroom</span>. Stay put while you work the lock.');
        let k = 0;
        await m.until((dt) => {
          const p = m.player.pos;
          if (Math.hypot(p.x - sr.x, p.z - sr.z) < 1.8 && !m.player.vehicle) { k = Math.min(1, k + dt / 7); m.hud.setBar('CRACKING', k, '#6cff6c'); if (Math.random() < dt * 4) game.audio?.playAt('clink', p, 0.3); }
          return k >= 1;
        });
        m.hud.setBar(null); m.removeMarker(mk);
        m.cash(6000);
        m.hud.bigMessage('CASH GRABBED', 'passed', 2.5, '$6,000 up front — the rest is in the bags');
        // a second wave while the alarm goes, and every cop in town
        saints(m, around(sr.x, sr.z, 4, 30, 40), ['smg', 'shotgun', 'rifle', 'smg'], { guard: false });
        m.wanted(3);
        const gc = spot(game, A.x + 50, A.z + 60);
        const car = m.car('zenith', gc.x, gc.z, gc.yaw, { color: 0x111111 });
        m.blipEntity(car, 0x4aa3ff, 'car');
        m.objective('Get to a car and <span class="r">lose the cops</span>.');
        await m.until(() => game.police.level === 0);
        const R = TOWNS.ridge;
        const gar = spot(game, R.x + 40, R.z + 10);
        await m.goTo(gar.x, gar.z, { vehicle: true, radius: 8, slow: true, text: 'Lay low at the garage in <span class="y">Cedar Ridge</span>.', condition: () => game.police.level === 0 });
        await m.say('Dre', 'Nina? It\'s done. The arena\'s dry. Your father\'s going to notice.', 3);
        await m.say('Nina', 'He\'s already noticed. That\'s the point. Next: the ship.', 3);
      },
    },
    // ================================================================ CHAPTER VIII — HARBOR SAINTS
    {
      id: 'pacificstar', title: 'Pacific Star', contact: 'R', requires: ['boxoffice'], reward: 15000,
      log: 'Boarded the MV Pacific Star at Port Morena, cracked the captain\'s safe for the Saints\' manifest and raced a speedboat all the way up the coast to San Aurelio with the coast guard behind.',
      start: (L) => { const s = L.cargoShip || L.docksQuay; return { x: s.x - 4, z: s.z - 8 }; },
      async run(m, game) {
        const L = m.L;
        const R = game.shipRaid;
        if (!R) throw new MissionFail('The ship has sailed.');
        const s0 = L.cargoShip;
        const rico = m.ped(s0.x - 5, s0.z - 9, { appearance: look('rico'), invincible: true });
        m.speakersSet({ Rico: rico, Dre: m.player });
        await m.cutscene(async () => {
          m.face(rico, m.player); m.face(m.player, rico);
          m.twoShot(m.player, rico, 1, 3.4);
          await m.lines([
            ['Rico', 'The Pacific Star. Castell\'s guns come in on her, and the manifest in the captain\'s safe says exactly where they go.'],
            ['Rico', 'Crew\'s armed and the security\'s ex-military. Get up to the bridge, crack the safe, then get off the ship.'],
            ['Rico', 'There\'s a speedboat tied up north of the bow. Take it up the coast to Nina at Aurelio Marina. Long way. Don\'t stop.'],
          ]);
        });
        rico.setPosition(rico.pos.x, undefined, rico.pos.z - 400);
        // a fresh crew and a full safe
        if (R.spawned) R._despawn();
        R.lootedAt = -1e9;
        R._spawn();
        m.player.giveWeapon('rifle', 240); m.player.giveWeapon('shotgun', 40); m.player.giveWeapon('grenade', 5);
        m.failIf(() => !R.spawned, 'You left the ship.');
        const bridge = R.S.safe;
        const bb = { x: bridge.x, z: bridge.z, color: 0x6cff6c, icon: 'money' };
        game.blips.add(bb); m.blips.push(bb);
        m.objective('Board the <span class="y">Pacific Star</span> and crack the <span class="g">captain\'s safe</span> on the bridge.');
        await m.until(() => R.safeDone);
        game.blips.delete(bb);
        m.hud.subtitle('Got the manifest? Then move! Coast guard\'s been called.', 'Rico', 3.5);
        const bs = waterSpot(game, CITY.maxX + 58, 140, 3);
        const boat = m.car('speedboat', bs.x, bs.z, Math.PI, { color: 0x1d3557 });
        await m.getIn(boat, 'Get off the ship and into the <span class="b">speedboat</span>.');
        m.keepAlive(boat, 'The speedboat sank.');
        m.wanted(3);
        const mr = L.aurMarina;
        const dest = waterSpot(game, mr.pontoon ? (mr.pontoon.x0 + mr.pontoon.x1) / 2 : mr.x + 50, mr.z + 10, 1.4);
        m.help('Open water all the way: out past the docks, then north up the coast. Police boats will try to ram you.', 7);
        await m.goTo(dest.x, dest.z, { vehicle: true, radius: 10, y: WATER_Y + 0.1, inCar: boat, text: 'Run the manifest up the coast to <span class="y">Aurelio Marina</span>.' });
        await m.loseWanted('Lose the <span class="r">coast guard</span>.');
        const nina = m.ped(mr.x, mr.z + 3, { appearance: nlook('nina'), invincible: true });
        m.speakersSet({ Nina: nina, Dre: m.player });
        await m.say('Nina', 'You came the whole way by sea? You\'re insane. Give me that.', 3);
        await m.say('Nina', '...Timberline. The guns go up to the sawmill and come back down hidden in the log trucks.', 4);
      },
    },
    {
      id: 'timber', title: 'Timber!', contact: 'N', requires: ['pacificstar'], reward: 9000,
      log: 'Stopped all three Saints log trucks coming down the Timber Road from the sawmill before they reached the city.',
      start: () => ({ x: TOWNS.timber.x + 26, z: TOWNS.timber.z + 12 }),
      async run(m, game) {
        const T = TOWNS.timber;
        const ns = spot(game, T.x + 26, T.z + 12, true);
        const nina = m.ped(ns.x + 1.2, ns.z + 1, { appearance: nlook('nina'), invincible: true, y: ns.y });
        m.speakersSet({ Nina: nina, Dre: m.player });
        await m.cutscene(async () => {
          m.face(nina, m.player); m.face(m.player, nina);
          m.twoShot(m.player, nina, 1, 3.4);
          await m.lines([
            ['Nina', 'Three trucks leave the sawmill this afternoon. Logs on top, rifles underneath. Each one has an escort.'],
            ['Nina', 'If they get to the city, they\'re gone into a hundred garages. Stop them on the mountain road.'],
            ['Nina', 'There\'s a truck of our own round the back with something for the job. Try not to crash it.'],
          ]);
        });
        nina.setPosition(nina.pos.x, undefined, nina.pos.z - 300);
        const cs = spot(game, T.x - 30, T.z + 10);
        const car = m.car('summit', cs.x, cs.z, cs.yaw, { color: 0x4a5a3a });
        m.player.giveWeapon('rpg', 12); m.player.giveWeapon('smg', 200);
        await m.getIn(car, 'Get in the <span class="b">4x4</span>.');
        const trucks = [];
        const launch = (i) => {
          const s = laneTowards(game, T.x + 70, T.z + 12, NCITY.x, NCITY.z);
          const t = m.car('hauler', s.x, s.z, s.yaw, { color: 0x8a5a2b });
          const d = m.ped(s.x, s.z, { appearance: saintLook(), brain: 'script' });
          t.putIn(d, 0);
          t.health = t.maxHealth = 1100;
          t.ai = new RouteDriver(game, t, { x: NCITY.x, z: NCITY.z }, { speed: 15 });
          m.blipEntity(t, 0xff3030, 'car');
          trucks.push({ t, d });
          const e = laneTowards(game, T.x + 40, T.z + 8, NCITY.x, NCITY.z);
          chaseCar(m, 'brawler', e.x, e.z, e.yaw, 'saints', 1, saintLook);
        };
        launch(0);
        let next = 1, launchT = 0;
        m.tick((dt) => { if (next < 3) { launchT += dt; if (launchT > 14) { launchT = 0; launch(next++); } } });
        const stopped = (x) => x.t.isWrecked || x.t.exploded || x.d.dead || x.d.vehicle !== x.t;
        m.failIf(() => trucks.some((x) => !stopped(x) && Math.hypot(x.t.pos.x - NCITY.x, x.t.pos.z - NCITY.z) < 330), 'A truck made it into San Aurelio.');
        m.objective('Stop the three <span class="r">log trucks</span> before they reach the city.');
        await m.until(() => {
          const n = trucks.filter(stopped).length;
          m.hud.setCounter('TRUCKS', `${n}/3`);
          return next >= 3 && trucks.length === 3 && n === 3;
        });
        m.hud.setCounter(null);
        for (const x of trucks) if (x.t.ai) x.t.ai = null;
        await m.say('Nina', 'That\'s the last of his guns. All he\'s got left is his lieutenants. And me.', 3.5);
      },
    },
    {
      id: 'highground', title: 'High Ground', contact: 'N', requires: ['timber'], reward: 10000,
      log: 'Picked off Castell\'s lieutenants from the valley side above Ridge Road, then ran the survivors down before they reached the city.',
      start: () => ({ x: TOWNS.ridge.x + 22, z: TOWNS.ridge.z - 18 }),
      async run(m, game) {
        const Rg = TOWNS.ridge;
        const ns = spot(game, Rg.x + 22, Rg.z - 18, true);
        const nina = m.ped(ns.x + 1.2, ns.z + 1, { appearance: nlook('nina'), invincible: true, y: ns.y });
        m.speakersSet({ Nina: nina, Dre: m.player });
        await m.cutscene(async () => {
          m.face(nina, m.player); m.face(m.player, nina);
          m.twoShot(m.player, nina, -1, 3.4);
          await m.lines([
            ['Nina', 'My father\'s lieutenants are meeting on the valley road to decide what to do about you. Five of them, two cars.'],
            ['Nina', 'There\'s a spot up on the valley side that looks right down on it. Take this — and take your time.'],
          ]);
        });
        nina.setPosition(nina.pos.x, undefined, nina.pos.z - 300);
        const wid = WEAPONS.sniper ? 'sniper' : 'rifle';
        m.player.giveWeapon(wid, 40); m.player.switchTo(wid);
        // the meeting: on Ridge Road down the valley from Cedar Ridge
        const meet = spot(game, -10, -3420);
        const net = game.map.roads, c = net.closest(meet.x, meet.z, (e) => e.name === 'Ridge Road');
        const tmp = [0, 0, 0, 0, 0]; net.at(c.e, c.s, tmp);
        const nx = -tmp[4], nz = tmp[3];
        const side = game.map.hf.sample(meet.x + nx * 120, meet.z + nz * 120) > game.map.hf.sample(meet.x - nx * 120, meet.z - nz * 120) ? 1 : -1;
        const vx = meet.x + nx * 125 * side, vz = meet.z + nz * 125 * side;
        await fadeTeleport(m, vx, vz, Math.atan2(meet.x - vx, meet.z - vz));
        const bike = m.car('trail', vx + 3, vz + 2, Math.atan2(meet.x - vx, meet.z - vz));
        const cars = [];
        for (const k of [-1, 1]) { const cc = m.car('kestrel', meet.x + tmp[3] * 10 * k, meet.z + tmp[4] * 10 * k, Math.atan2(tmp[3], tmp[4]), { color: 0x14213d }); cc.parked = true; cars.push(cc); }
        const lts = saints(m, around(meet.x, meet.z, 5, 2.5, 5), ['pistol', 'smg', 'pistol', 'rifle', 'pistol'], { health: 100 });
        for (const l of lts) { l.guardFace = Math.atan2(meet.x - l.pos.x, meet.z - l.pos.z); l.setState('guard'); }
        m.objective('Take out Castell\'s <span class="r">lieutenants</span>. Hold right mouse to aim down the scope.');
        // once the shooting starts the survivors run for the cars and head for the city
        let fled = false;
        m.tick(() => {
          if (fled || !lts.some((l) => l.dead || l.health < l.maxHealth)) return;
          fled = true;
          for (const l of lts.filter((q) => !q.dead)) {
            const car = cars.find((c) => c.occupants.filter(Boolean).length < 3 && c.occupants.indexOf(null) >= 0);
            if (!car) break;
            car.parked = false;
            car.putIn(l, car.occupants.indexOf(null));
          }
          for (const car of cars) {
            const drv = car.occupants[0];
            if (drv && !drv.dead) { car.ai = new RouteDriver(game, car, { x: NCITY.x, z: NCITY.z }, { speed: 24 }); m.blipEntity(car, 0xff3030, 'car'); }
          }
          m.hud.subtitle('They\'re running for the cars! Don\'t let them reach the city!', 'Nina', 3.5);
          m.help('The <b>dirt bike</b> behind you is the quickest way down the hill.', 5);
        });
        m.failIf(() => cars.some((car) => car.occupants.some((o) => o && !o.dead && !o.isPlayer) && Math.hypot(car.pos.x - NCITY.x, car.pos.z - NCITY.z) < 380), 'A lieutenant made it back to the city.');
        await m.killAll(lts, null, { counter: 'LIEUTENANTS' });
        await m.say('Nina', 'That\'s all of them. Now he\'s alone. Harborside — tomorrow night.', 3.5);
      },
    },
    {
      id: 'saintsandsinners', title: 'Saints and Sinners', contact: 'N', requires: ['highground'], reward: 30000,
      chapterEnd: ['SAN AURELIO', 'Thanks for playing'],
      log: 'Stormed the Harbor Saints\' waterfront with Rico and Nina, chased Vincent Castell out to sea and sank him off Aurelio Beach. The coast is ours.',
      start: (L) => { const h = L.aurHarbor || plaza(L); return { x: h.x - 30, z: h.z + 8 }; },
      async run(m, game) {
        const L = m.L;
        const Hh = L.aurHarbor || plaza(L);
        game.env.setTime(22);
        const st = spot(game, Hh.x - 30, Hh.z + 8, true);
        const nina = m.ped(st.x + 1.2, st.z + 1, { appearance: nlook('nina'), y: st.y });
        const rico = m.ped(st.x - 1.2, st.z + 1.4, { appearance: look('rico'), y: st.y });
        for (const c of [nina, rico]) { c.health = c.maxHealth = 800; c.keep = false; }
        m.speakersSet({ Nina: nina, Rico: rico, Dre: m.player });
        await m.cutscene(async () => {
          m.face(nina, m.player); m.face(rico, m.player); m.face(m.player, nina);
          m.twoShot(m.player, nina, 1, 3.8);
          await m.lines([
            ['Nina', 'He\'s at the marina with everyone he has left. He keeps a boat ready. He always has.'],
            ['Rico', 'Then we go through them fast, before he gets to it.'],
            ['Dre', 'Nina — he\'s your father.'],
            ['Nina', 'He stopped being that a long time ago. Let\'s go.'],
          ]);
        });
        m.player.giveWeapon('rifle', 300); m.player.giveWeapon('shotgun', 40); m.player.giveWeapon('grenade', 6); m.player.switchTo('rifle');
        for (const [c, w] of [[nina, 'smg'], [rico, 'rifle']]) { c.giveWeapon(w, 999); c.equip(w); }
        m.follower(nina, 0); m.follower(rico, 1);
        const mr = L.aurMarina || { x: Hh.x + 280, z: Hh.z + 60 };
        const foes = [];
        crewSupport(m, [nina, rico], () => foes);
        const waveAt = (x, z, n, weapons) => { const w = saints(m, around(x, z, n, 6, 18), weapons, { guard: false, health: 120 }); foes.push(...w); return w; };
        await m.killAll(waveAt(Hh.x + 60, Hh.z + 10, 7, ['smg', 'rifle', 'shotgun', 'pistol', 'smg', 'rifle', 'rpg']), 'Fight down <span class="y">Harbor Drive</span> towards the marina.', { counter: 'SAINTS' });
        await m.killAll(waveAt(mr.x - 40, mr.z, 8, ['rifle', 'smg', 'shotgun', 'rifle', 'rpg', 'smg', 'rifle', 'shotgun']), 'Clear the <span class="y">marina</span>.', { counter: 'SAINTS' });
        // Castell makes for the sea
        const pt = mr.pontoon || { x0: mr.x + 10, x1: mr.x + 80, z: mr.z };
        const cs = waterSpot(game, pt.x1 + 30, pt.z - 30, 3);
        const route = [[pt.x1 + 180, pt.z - 300], [pt.x1 + 260, pt.z - 700], [pt.x1 + 200, pt.z - 1100], [pt.x1 + 320, pt.z - 400]].map(([x, z]) => ({ x: Math.min(x, 1580), z }));
        const boss = saintBoat(m, 'cruiser', cs.x, cs.z, route, 0.8, true, nlook('castell'));
        boss.b.health = boss.b.maxHealth = 1400;
        m.blipEntity(boss.drv, 0xff3030, 'skull');
        m.hud.subtitle('That\'s him — the white cruiser! He\'s getting away!', 'Nina', 3);
        const my = waterSpot(game, pt.x1 - 20, pt.z + 9, 1.6);
        const boat = m.car('speedboat', my.x, my.z, Math.PI / 2, { color: 0x7a1f2b });
        m.blipEntity(boat, 0x4aa3ff, 'boat');
        m.objective('Take the <span class="b">speedboat</span> and stop <span class="r">Castell</span>.');
        m.failIf(() => !boss.drv.dead && Math.hypot(boss.b.pos.x - m.player.pos.x, boss.b.pos.z - m.player.pos.z) > 900, 'Castell got away.');
        await m.until(() => boss.drv.dead || boss.b.isWrecked || boss.b.exploded);
        if (!boss.b.exploded && !boss.b.isWrecked) boss.b.health = 0;
        boss.b.ai = null;
        await m.wait(2);
        await m.say('Nina', '...It\'s over. Come back in, Dre.', 3);
        const home = waterSpot(game, (pt.x0 + pt.x1) / 2, pt.z + 10, 1.4);
        await m.goTo(home.x, home.z, { vehicle: true, radius: 10, y: WATER_Y + 0.1, text: 'Head back to the <span class="y">marina</span>.' });
        m.gpsOff();
        await m.lines([
          ['Rico', 'Los Soles, Puerto Seco, and now San Aurelio. The whole coast, D.'],
          ['Nina', 'The Saints are finished. The port\'s open. What are you going to do with it all?'],
          ['Dre', 'Same thing Tino would have. Look after our own.'],
        ]);
      },
    },
  ];
}
