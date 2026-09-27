// Raiding the MV Pacific Star. The ship at Port Morena carries a crew of deckhands (hi-vis and hard hats),
// hired security (plate carriers and balaclavas) and the captain up on the bridge. Walk up the gangway and
// you're told to get off; draw a gun, shoot, hurt anyone or overstay and the whole ship turns on you, and the
// captain radios the coast guard. Up on the bridge the captain's safe holds the payroll; three containers are
// full of contraband. The crew and the loot come back a while after you've cleaned the ship out.
import * as THREE from 'three';
import { buildCargoShip, SHIP } from '../world/cargoship.js';
import { randomAppearance } from '../entities/humanoid.js';
import { RNG, rand, pick, dist2, clamp } from '../core/utils.js';

const RESTOCK = 900; // seconds before the ship has a crew and cargo again

// [local x, local z, deck (0-3 superstructure levels, 'fc' forecastle), role, facing, patrol z range]
const POSTS = [
  [-10.6, 47.2, 0, 'security', -Math.PI / 2], [-10.2, 51.5, 0, 'deck', Math.PI],
  [11.4, -30, 0, 'deck', 0, [-58, 44]], [-11.4, 10, 0, 'deck', Math.PI, [-58, 44]],
  [3, -39.5, 0, 'deck', Math.PI / 2], [-5, 23.5, 0, 'security', -Math.PI / 2], [6, 2.5, 0, 'deck', Math.PI / 2],
  [0, -80, 'fc', 'security', Math.PI], [4, -62, 0, 'deck', Math.PI],
  [-6, 57, 1, 'security', Math.PI], [5, 59, 2, 'security', Math.PI],
  [-5.5, 65.5, 3, 'security', Math.PI], [1.5, 70, 3, 'captain', Math.PI],
];

function crewAppearance(role, rng) {
  if (role === 'captain') return randomAppearance(rng, { female: false, beard: true, hairStyle: 'short', hair: 0x9a9a9a, shirtType: 'long', uniform: { shirt: 0xf4f4f0, pants: 0x1a2440, hat: 0xf4f4f0, band: 0xd4af37 }, noBadge: true, shoes: 0x0a0a0a, build: 1.12 });
  if (role === 'security') {
    const mask = rng.chance(0.55);
    return randomAppearance(rng, { female: rng.chance(0.15), shirtType: 'long', uniform: { shirt: 0x26282b, pants: 0x1f2124, hat: null }, noBadge: true, vest: { kind: 'tactical' }, mask: mask ? 0x151515 : null, hairStyle: mask ? 'buzz' : 'cap', hat: 0x151515, shoes: 0x0a0a0a, glasses: !mask && rng.chance(0.4) });
  }
  const over = rng.pick([0xe06c1b, 0x2c4a7a, 0x3d3f42]);
  return randomAppearance(rng, { female: rng.chance(0.2), shirtType: 'long', uniform: { shirt: over, pants: over, hat: null }, noBadge: true, vest: { kind: 'hiviz', color: rng.pick([0xd7ff1e, 0xff8a1e]) }, hardhat: rng.pick([0xf2d21b, 0xf4f4f4, 0xff7a1a]), hairStyle: 'buzz', shoes: 0x3a2a1a, beard: rng.chance(0.3) });
}

export class ShipRaid {
  constructor(game) {
    this.game = game;
    this.ship = buildCargoShip(game);
    this.S = this.ship.spec;
    this.crew = [];
    this.spawned = false;
    this.alerted = false;
    this.aboardT = 0;
    this.warned = false;
    this.radioT = -1;
    this.lootedAt = -1e9;
    this.markers = [];
    this.farT = 0;
    this.safeK = 0;
    this.blip = { x: SHIP.x, z: SHIP.z, icon: 'ship', color: 0xff9a1f, label: SHIP.name, small: true };
    game.blips?.add(this.blip);
    game.map.landmarks.cargoShip = { x: this.S.gangwayFoot.x, z: this.S.gangwayFoot.z, name: SHIP.name };
    const ev = game.events;
    const near = (p) => p && dist2(p.x, p.z, SHIP.x, SHIP.z) < 110 * 110;
    ev.on('gunshot', (s, pos) => { if (s?.isPlayer && this.spawned && (near(pos) || this.ship.aboard(game.player.pos))) this.alert('Shots fired on deck!'); });
    ev.on('explosion', (pos, r, src) => { if (src?.isPlayer && this.spawned && near(pos)) this.alert(); });
    ev.on('kill', (killer, victim) => { if (killer?.isPlayer && victim?.gang === 'crew') this.alert(); });
    ev.on('charDamaged', (ped, amt, src) => { if (src?.isPlayer && ped?.gang === 'crew') this.alert('A crewman\'s down!'); });
  }

  get restocked() { return this.game.time - this.lootedAt > RESTOCK; }

  alert(msg) {
    const g = this.game;
    if (!this.spawned) return;
    if (!this.alerted) {
      this.alerted = true;
      g.hud?.bigMessage('SHIP ALERTED', 'failed', 2.5, msg || 'The crew are armed');
      this.radioT = 9; // the captain calls it in
      g.audio?.play('wanted', 0.6);
    }
    g.peds.gangAggro.crew = true;
    for (const c of this.crew) if (!c.dead && c.state !== 'attack') { c.threat = g.player; c.setState('attack'); }
  }

  update(dt) {
    const g = this.game, p = g.player;
    this.ship.update(dt, g.env?.night ?? 0);
    const pp = p.vehicle ? p.vehicle.pos : p.pos;
    const d = Math.hypot(pp.x - SHIP.x, pp.z - SHIP.z);
    // crew aboard while you're around (and the ship's been restocked)
    if (d < 300 && !this.spawned && this.restocked) this._spawn();
    if (this.spawned && d > 420) { this.farT += dt; if (this.farT > 20) this._despawn(); } else this.farT = 0;
    if (!this.spawned) return;
    this.crew = this.crew.filter((c) => !c.removed);
    const aboard = this.ship.aboard(p.pos) && !p.dead;
    // trespass: a warning, then they come for you (at once if you've a gun out)
    if (aboard && !this.alerted) {
      if (!this.warned) {
        this.warned = true; this.aboardT = 0;
        g.hud?.bigMessage(SHIP.name.toUpperCase(), 'failed', 3, 'Crew only');
        g.hud?.help('You\'re aboard the <b>' + SHIP.name + '</b>. Security wants you off the ship. Draw a gun or go near the bridge and they\'ll open fire.', 7);
        const sec = this._nearestCrew(p.pos, 'security');
        if (sec) { sec.say('Hey! Off the ship. Now!'); sec.threat = null; }
      }
      this.aboardT += dt;
      const armed = p.aiming && p.weaponDef?.type !== 'melee';
      const bridge = p.pos.y > this.S.levels[1] - 0.5 && p.pos.z - SHIP.z > 50;
      if (armed || bridge || this.aboardT > 25) this.alert(armed ? 'He\'s armed!' : bridge ? 'Intruder at the bridge!' : null);
      // security turns to watch you
      for (const c of this.crew) if (!c.dead && c.role === 'security' && c.state === 'guard' && dist2(c.pos.x, c.pos.z, p.pos.x, p.pos.z) < 30 * 30) c.guardFace = Math.atan2(p.pos.x - c.pos.x, p.pos.z - c.pos.z);
    }
    if (!aboard && this.warned && !this.alerted && d > 60) this.warned = false;
    // the coast guard
    if (this.radioT > 0) {
      this.radioT -= dt;
      if (this.radioT <= 0) {
        const cap = this.crew.find((c) => c.role === 'captain');
        if (cap && !cap.dead) { g.police?.raise(2); g.hud?.dispatch?.(`${SHIP.name}: armed boarders, Port Morena`, 'Coast guard'); }
      }
    }
    // patrols along the side decks
    for (const c of this.crew) {
      if (c.dead || !c.patrol || c.state === 'attack' || c.state === 'flee') continue;
      if (c.state === 'guard') {
        c.patrolT = (c.patrolT ?? rand(2, 6)) - dt;
        if (c.patrolT <= 0) {
          c.patrolT = rand(6, 14);
          const [z0, z1] = c.patrol;
          const [x, z] = this.ship.toWorld(c.postX, rand(z0, z1));
          c.targetPos.set(x, 0, z); c.gotoSpeed = 1.3; c.afterGoto = 'guard'; c.setState('goto');
        }
      }
    }
    // cracking the safe (stand at it)
    if (this.safeMarker && !this.safeDone) {
      const sp = this.S.safe;
      const inside = dist2(p.pos.x, p.pos.z, sp.x, sp.z) < 1.6 * 1.6 && Math.abs(p.pos.y - sp.y) < 1.2 && !p.vehicle && !p.dead;
      if (inside) {
        if (this.safeK === 0) { g.hud?.help('Cracking the safe... stay put.', 3); this.alert('The safe!'); }
        this.safeK = Math.min(1, this.safeK + dt / 6);
        g.hud?.setBar('CRACKING SAFE', this.safeK, '#6cff6c');
        if (Math.random() < dt * 4) g.audio?.playAt('clink', sp, 0.3);
        if (this.safeK >= 1) this._openSafe();
      } else if (this.safeK > 0 && !g.missions?.active) g.hud?.setBar(null);
    }
  }

  _nearestCrew(pos, role) {
    let best = null, bd = Infinity;
    for (const c of this.crew) { if (c.dead || (role && c.role !== role)) continue; const d = dist2(c.pos.x, c.pos.z, pos.x, pos.z); if (d < bd) { bd = d; best = c; } }
    return best;
  }

  _spawn() {
    const g = this.game, S = this.S;
    if (!g.peds?.spawnPed) return;
    this.spawned = true; this.alerted = false; this.warned = false; this.safeK = 0; this.safeDone = false; this.lootLeft = 0;
    g.peds.gangAggro.crew = false;
    const rng = new RNG((Math.random() * 1e9) | 0);
    for (const [lx, lz, lvl, role, face, patrol] of POSTS) {
      const [x, z] = this.ship.toWorld(lx, lz);
      const y = lvl === 'fc' ? S.deckY + 2.4 : S.levels[lvl];
      const weapon = role === 'captain' ? 'pistol' : role === 'security' ? rng.pick(['smg', 'shotgun', 'rifle', 'smg']) : rng.pick(['pistol', 'bat', 'knife', 'pistol']);
      const ped = g.peds.spawnPed(x, z, { appearance: crewAppearance(role, rng), brain: 'gang', gang: 'crew', state: 'guard', weapon, health: role === 'captain' ? 160 : role === 'security' ? 130 : 100, armor: role === 'security' ? 40 : 0, persistent: true });
      if (!ped) continue;
      ped.setPosition(x, y, z);
      ped.guardFace = face; ped.yaw = face;
      ped.role = role;
      ped.accuracy = role === 'security' ? 0.5 : 0.35;
      ped.damageMul = role === 'security' ? 0.6 : 0.45;
      ped.moneyDrop = role === 'captain' ? 600 : rand(40, 180);
      if (patrol) { ped.patrol = patrol; ped.postX = lx; }
      this.crew.push(ped);
    }
    // the safe on the bridge, and three containers of contraband off the cross walkways
    this.safeMarker = g.pickups.addMarker(S.safe.x, S.safe.z - 0.6, { y: S.safe.y, color: 0x6cff6c, radius: 0.9, height: 1.2, icon: 'money', label: 'Captain\'s safe', footOnly: true, blip: false });
    this.markers.push(this.safeMarker);
    const gaps = [];
    for (let k = 0; k < S.bays.length - 1; k++) gaps.push((S.bays[k] + S.bays[k + 1]) / 2);
    const picks = [];
    const pool = S.stacks.filter((s) => Math.abs(s.row) === 1 && Math.abs(s.lx) < 9);
    for (let n = 0; n < 3 && pool.length; n++) { const i = rng.int(0, pool.length - 1); picks.push(pool.splice(i, 1)[0]); }
    for (const st of picks) {
      const face = st.row > 0 ? 1 : -1;
      const gz = st.lz + face * 3.6; // the door end, facing a cross walkway
      const [x, z] = this.ship.toWorld(st.lx, gz);
      const m = g.pickups.addMarker(x, z, { y: S.deckY, color: 0xffb000, radius: 1.0, height: 1.4, icon: 'weapon', label: 'Contraband', footOnly: true, blip: false, onEnter: () => this._openContainer(m, x, z) });
      this.markers.push(m);
      this.lootLeft++;
    }
  }

  _openContainer(m, x, z) {
    const g = this.game, y = this.S.deckY;
    g.pickups.removeMarker(m);
    this.markers = this.markers.filter((k) => k !== m);
    g.audio?.playAt('metalhit', m.pos, 0.9);
    const cash = Math.round(rand(1500, 3200));
    g.pickups.spawn('money', x + rand(-0.6, 0.6), z + rand(-0.6, 0.6), { amount: cash, y, life: 120 });
    const w = pick(['rifle', 'shotgun', 'smg', 'grenade', 'rpg']);
    g.pickups.spawn('weapon', x + rand(-0.8, 0.8), z + rand(-0.8, 0.8), { weapon: w, ammo: w === 'rpg' ? 4 : w === 'grenade' ? 6 : 90, y, life: 120 });
    if (Math.random() < 0.5) g.pickups.spawn('armor', x + rand(-0.8, 0.8), z + rand(-0.8, 0.8), { y, life: 120 });
    g.hud?.help(`Contraband: <b>$${cash.toLocaleString()}</b> and a ${w}.`, 3);
    this.alert();
    this.lootLeft--;
    this._checkDone();
  }

  _openSafe() {
    const g = this.game, sp = this.S.safe;
    this.safeDone = true;
    g.pickups.removeMarker(this.safeMarker);
    this.markers = this.markers.filter((k) => k !== this.safeMarker);
    this.safeMarker = null;
    g.hud?.setBar(null);
    const total = Math.round(rand(12000, 18000) / 100) * 100;
    this.lastSafe = total;
    for (let i = 0; i < 4; i++) g.pickups.spawn('money', sp.x + rand(-1, 1), sp.z - rand(0.8, 1.8), { amount: total / 4, y: sp.y, life: 180 });
    g.hud?.bigMessage('SAFE CRACKED', 'passed', 3, `The payroll: $${total.toLocaleString()}`);
    g.audio?.play('cash', 0.6);
    g.events.emit('shipSafe', total);
    this._checkDone();
  }

  _checkDone() {
    if (!this.safeDone || this.lootLeft > 0) return;
    const g = this.game;
    this.lootedAt = g.time;
    g.stats && (g.stats.shipRaids = (g.stats.shipRaids || 0) + 1);
    g.hud?.bigMessage('SHIP RAIDED', 'passed', 3.5, 'Now get off the ship');
    g.events.emit('shipRaided');
  }

  _despawn() {
    const g = this.game;
    for (const c of this.crew) if (!c.removed) g.peds.remove(c);
    this.crew = [];
    for (const m of this.markers) g.pickups.removeMarker(m);
    this.markers = []; this.safeMarker = null;
    this.spawned = false; this.alerted = false; this.warned = false;
    g.peds.gangAggro.crew = false;
  }
}
