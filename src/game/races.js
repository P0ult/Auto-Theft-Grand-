// Street races: a chequered flag on the map marks each start. Roll up in a car, pay the entry and race
// three locals through the checkpoints. First place takes the pot, second gets double the entry back,
// third gets the entry back. Best times are kept in the stats.
import { RaceDriver } from './missions.js';
import { XS, ZS } from '../world/citymap.js';
import { NCITY, ncRingR, TOWNS } from '../world/worldgen.js';
import { clamp, rand, pick, RNG } from '../core/utils.js';
import { randomAppearance } from '../entities/humanoid.js';

const el = (tag, cls, parent, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html != null) e.innerHTML = html; if (parent) parent.appendChild(e); return e; };

// waypoints the route is threaded through (the road network fills in between)
function raceDefs(game) {
  const L = game.map.landmarks;
  const ring = [];
  for (let k = 0; k <= 12; k++) { const th = -0.3 + k / 12 * Math.PI * 2; const r = ncRingR(2, th); ring.push([NCITY.x + r * Math.cos(th), NCITY.z + r * Math.sin(th)]); }
  return [
    { id: 'solsprint', name: 'Sol Sprint', entry: 250, prize: 2000, cars: ['kestrel', 'brawler', 'zenith'], pts: [[XS[12], ZS[4]], [XS[12], ZS[8]], [XS[8], ZS[8]], [XS[8], ZS[4]], [XS[11], ZS[4]]] },
    { id: 'bayshore', name: 'Bayshore Run', entry: 500, prize: 4500, cars: ['zenith', 'kestrel', 'brawler'], pts: [[XS[17], ZS[3]], [960, -560], [TOWNS.hale.x, TOWNS.hale.z], [960, -1500], [860, -1860]] },
    { id: 'aurring', name: 'Aurelio Ring', entry: 400, prize: 3500, cars: ['kestrel', 'pico', 'brawler'], pts: ring },
    { id: 'cedarclimb', name: 'Cedar Valley Climb', entry: 600, prize: 5000, cars: ['summit', 'zenith', 'kestrel'], pts: [[NCITY.x - 280, NCITY.z + 300], [60, -3620], [-10, -3420], [TOWNS.ridge.x, TOWNS.ridge.z], [160, -2830]] },
  ];
}

export class RaceSystem {
  constructor(game) {
    this.game = game;
    this.races = [];
    this.active = null;
    this.rng = new RNG(4711);
    for (const def of raceDefs(game)) this._plan(def);
  }

  // thread the waypoints through the road network into a dense line for the AI and sparse checkpoints
  _plan(def) {
    const game = this.game, net = game.map.roads;
    const line = [];
    for (let i = 0; i < def.pts.length - 1; i++) {
      const [ax, az] = def.pts[i], [bx, bz] = def.pts[i + 1];
      const r = net.route(ax, az, bx, bz);
      const poly = net.routePolyline(r, ax, az, bx, bz);
      for (const p of poly.slice(i ? 1 : 0)) line.push({ x: p[0], z: p[1] });
    }
    // snap onto the road and resample: AI points every ~18 m, checkpoints every ~140 m
    const snapped = [];
    for (const p of line) { const c = net.closest(p.x, p.z, (e) => !e.removed && e.type !== 'rail' && e.type !== 'dirt', 40); if (c) { const t = [0, 0, 0, 0, 0]; net.at(c.e, c.s, t); snapped.push({ x: t[0], z: t[2], y: t[1] }); } }
    const dense = [snapped[0]];
    for (const p of snapped) { const q = dense[dense.length - 1]; const d = Math.hypot(p.x - q.x, p.z - q.z); if (d > 18) { const n = Math.floor(d / 18); for (let k = 1; k <= n; k++) dense.push({ x: q.x + (p.x - q.x) * k / n, z: q.z + (p.z - q.z) * k / n, y: p.y }); } }
    if (dense.length < 6) return;
    const cps = [];
    let acc = 0;
    for (let i = 1; i < dense.length; i++) { acc += Math.hypot(dense[i].x - dense[i - 1].x, dense[i].z - dense[i - 1].z); if (acc > 140 || i === dense.length - 1) { cps.push(dense[i]); acc = 0; } }
    const s = dense[0], n2 = dense[3];
    let len = 0;
    for (let i = 1; i < dense.length; i++) len += Math.hypot(dense[i].x - dense[i - 1].x, dense[i].z - dense[i - 1].z);
    // grid lanes: both lanes of our direction on a wide street, single file on a two-lane road
    const se = net.closest(s.x, s.z, (e) => !e.removed && e.type !== 'rail');
    const wide = se && (se.e.lanesF > 1 || se.e.lanesB > 1);
    const race = { ...def, dense, cps, len, lanes: wide ? [1.9, 5.1] : [1.8, 1.8], start: { x: s.x, z: s.z, yaw: Math.atan2(n2.x - s.x, n2.z - s.z) } };
    race.marker = game.pickups.addMarker(s.x, s.z, { color: 0xffffff, radius: 4, height: 2.2, icon: 'flag', label: def.name, vehicleOnly: true, arrow: false, onEnter: () => this.offer(race) });
    this.races.push(race);
  }

  offer(race) {
    const game = this.game, p = game.player, v = p.vehicle;
    if (this.active || game.missions?.active || !v || v.driver !== p || v.isBoat || v.def.aircraft || v.def.train) return;
    if (v.def.pedal || v.def.board) { game.hud?.help('You\'ll need something with an engine.', 3); return; }
    v.input.brake = 1; v.vel.multiplyScalar(0.3);
    const hud = game.hud;
    game.paused = true; game.input.exitLock(); hud.menuOpen = 'shop';
    const o = hud.overlay;
    o.innerHTML = ''; o.className = 'hud-overlay show';
    const panel = el('div', 'menu-panel shop', o);
    const best = game.stats.raceBest?.[race.id];
    const km = (race.len / 1000).toFixed(1);
    el('h2', '', panel, `${race.name.toUpperCase()} <small>Street race · ${km} km · ${race.cps.length} checkpoints</small>`);
    el('div', 'shop-money', panel, `Entry <b>$${race.entry}</b> · First place wins <b>$${race.prize.toLocaleString()}</b>${best ? ` · Your best ${fmt(best)}` : ''}`);
    const row = el('div', 'shop-item', panel);
    el('div', 'shop-name', row, `Race in your ${v.def.name}<small>Three locals are waiting. Traffic won't stop for you.</small>`);
    const go = el('button', 'btn primary', row, `Race — $${race.entry}`);
    go.onclick = () => {
      if (p.money < race.entry && !game.freeroam?.active) { game.audio?.play('locked'); go.textContent = 'Not enough cash'; return; }
      if (!game.freeroam?.active) p.money -= race.entry;
      hud.closeOverlay();
      this.start(race);
    };
    const no = el('button', 'btn', panel, 'Not now (Esc)');
    no.onclick = () => hud.closeOverlay();
  }

  start(race) {
    const game = this.game, p = game.player, v = p.vehicle;
    const s = race.start, fx = Math.sin(s.yaw), fz = Math.cos(s.yaw);
    // (right of travel is (-fz, fx))
    const slot = (i) => { const back = 4 + (race.lanes[0] === race.lanes[1] ? i * 9 : (i >> 1) * 9), lat = race.lanes[i % 2]; return { x: s.x - fx * back - fz * lat, z: s.z - fz * back + fx * lat }; };
    const place = (veh, i) => { const q = slot(i); veh.pos.set(q.x, game.map.groundHeight(q.x, q.z) + 0.05, q.z); veh.yaw = s.yaw; veh.vel.set(0, 0, 0); veh.r = 0; veh.input.throttle = 0; veh.input.brake = 1; };
    game.hud.fade?.(0.4, () => {});
    place(v, 0);
    game.rig.yaw = s.yaw + Math.PI;
    // clear the grid of traffic
    for (const o of game.vehicles.list) if (o !== v && o.ai && !o.persistent && Math.hypot(o.pos.x - s.x, o.pos.z - s.z) < 40) game.traffic?._despawn?.(o);
    const rivals = [];
    for (let i = 1; i <= 3; i++) {
      const type = race.cars[(i - 1) % race.cars.length];
      const q = slot(i);
      const car = game.vehicles.spawn(type, q.x, q.z, s.yaw, { persistent: true });
      place(car, i);
      const d = game.peds.spawnPed(q.x, q.z, { persistent: true, appearance: randomAppearance(this.rng, { female: this.rng.chance(0.3), shirtType: pick(['jacket', 'tee', 'long']), glasses: this.rng.chance(0.5) }) });
      car.putIn(d, 0);
      const ai = new RaceDriver(game, car, race.dense, 0.7 + i * 0.06 + rand(-0.04, 0.04));
      car.ai = null;
      const blip = { x: q.x, z: q.z, color: 0xff9f1c, icon: 'dot', small: true };
      game.blips.add(blip);
      rivals.push({ car, driver: d, ai, blip, done: false, t: 0 });
    }
    game.disableAmbient = true;
    this.active = { race, car: v, rivals, idx: 0, t: -3.5, count: 3, marker: null, out: 0, finished: [] };
    game.hud.help(`<b>${race.name}</b> — first to the line takes $${race.prize.toLocaleString()}.`, 3);
  }

  _marker(a) {
    const game = this.game;
    if (a.marker) game.pickups.removeMarker(a.marker);
    const cp = a.race.cps[a.idx];
    if (!cp) return;
    const last = a.idx === a.race.cps.length - 1;
    a.marker = game.pickups.addMarker(cp.x, cp.z, { radius: 9, height: 6, color: last ? 0xffffff : 0xffd23f, icon: last ? 'flag' : 'dot' });
    game.hud.routeTo({ x: cp.x, z: cp.z });
  }

  update(dt) {
    const a = this.active;
    if (!a) return;
    const game = this.game, p = game.player, hud = game.hud;
    if (game.paused) return;
    a.t += dt;
    // countdown: everyone held on the brakes
    if (a.t < 0) {
      const n = Math.ceil(-a.t - 0.5);
      if (n < a.count && n >= 1) { a.count = n; hud.bigMessage(String(n), 'hint', 0.9); game.audio?.play('ui'); }
      for (const r of a.rivals) { r.car.input.throttle = 0; r.car.input.brake = 1; }
      if (a.car) { a.car.input.brake = Math.max(a.car.input.brake, 1); a.car.vel.multiplyScalar(0.9); }
      return;
    }
    if (!a.go) { a.go = true; hud.bigMessage('GO!', 'passed', 1.2); game.audio?.play('checkpoint'); this._marker(a); }
    for (const r of a.rivals) {
      if (!r.done) { r.ai.update(dt); if (r.ai.done) { r.done = true; r.t = a.t; a.finished.push(r); } }
      r.blip.x = r.car.pos.x; r.blip.z = r.car.pos.z;
    }
    // player checkpoints
    const pp = p.vehicle ? p.vehicle.pos : p.pos;
    const cp = a.race.cps[a.idx];
    if (cp && p.vehicle && Math.hypot(pp.x - cp.x, pp.z - cp.z) < 11) {
      a.idx++;
      game.audio?.play('checkpoint');
      if (a.idx >= a.race.cps.length) { this._finish(); return; }
      this._marker(a);
    }
    // standings: progress along the dense line
    const near = (pos, from) => { let bi = from, bd = Infinity; for (let i = Math.max(0, from - 5); i < Math.min(a.race.dense.length, from + 40); i++) { const q = a.race.dense[i]; const d = (q.x - pos.x) ** 2 + (q.z - pos.z) ** 2; if (d < bd) { bd = d; bi = i; } } return bi; };
    a.pi = near(pp, a.pi || 0);
    const ahead = a.rivals.filter((r) => r.done || r.ai.idx > a.pi + 1).length;
    hud.setCounter('POSITION', `${1 + ahead}/4`);
    hud.setBar('CHECKPOINTS', a.idx / a.race.cps.length, '#ffd23f');
    // out of the car, wrecked or dead: the race is off
    if (!p.vehicle || p.vehicle.isWrecked) a.out += dt; else a.out = 0;
    if (a.out > 8 || p.dead || (a.car && a.car.isWrecked)) this._end('RACE ABANDONED', 'failed', 'You left the race');
  }

  _finish() {
    const a = this.active, game = this.game, p = game.player;
    const place = 1 + a.finished.length;
    const time = a.t;
    game.stats.raceBest = game.stats.raceBest || {};
    const prev = game.stats.raceBest[a.race.id];
    if (place === 1 && (!prev || time < prev)) game.stats.raceBest[a.race.id] = time;
    const pay = place === 1 ? a.race.prize : place === 2 ? a.race.entry * 2 : place === 3 ? a.race.entry : 0;
    if (pay) { p.money += pay; game.hud.moneyFlash?.(pay); game.audio?.play('cash'); }
    const title = place === 1 ? 'RACE WON!' : `${['', '1ST', '2ND', '3RD', '4TH'][place]} PLACE`;
    this._end(title, place === 1 ? 'passed' : 'hint', `${fmt(time)}${pay ? ` · $${pay.toLocaleString()}` : ''}${place === 1 && (!prev || time < prev) ? ' · New best!' : ''}`);
    if (place === 1) { game.stats.racesWon = (game.stats.racesWon || 0) + 1; game.events.emit('raceWon', a.race.id); }
  }

  _end(title, kind, sub) {
    const a = this.active, game = this.game;
    if (!a) return;
    this.active = null;
    game.hud.bigMessage(title, kind, 4, sub);
    if (a.marker) game.pickups.removeMarker(a.marker);
    game.hud.routeTo(null); game.hud.setCounter(null); game.hud.setBar(null);
    game.disableAmbient = false;
    for (const r of a.rivals) {
      game.blips.delete(r.blip);
      // the rivals cruise off and become ordinary traffic that despawns in time
      r.car.persistent = false; r.driver.persistent = false;
      if (!r.car.isWrecked && r.car.driver) { r.car.ai = null; r.car.input.throttle = 0; r.car.input.brake = 1; }
    }
  }
}

function fmt(t) { const m = Math.floor(t / 60), s = t - m * 60; return `${m}:${s.toFixed(2).padStart(5, '0')}`; }
