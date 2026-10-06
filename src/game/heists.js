// Armoured cash vans (GTA V's Gruppe Sechs random event): every few minutes a Stockade cash-in-transit van
// does its rounds somewhere near you, shown with a green $ on the radar. It's bullet resistant, and the two
// guards inside are armed. Shoot the back doors (or blow them) and the cash spills out the back; the
// guards bail out and fight, the van may try to make a run for it, and the alarm brings the police.
import { Traffic } from './traffic.js';
import { randomAppearance } from '../entities/humanoid.js';
import { RNG, rand, dist2 } from '../core/utils.js';

const money = (n) => '$' + Math.round(n).toLocaleString('en-US');

const FIRST = 75;              // seconds into a session before the first van
const EVERY = [150, 260];      // between vans
const DOOR_HITS = 4;           // bullets in the back doors to break the lock

export function guardLook(rng) {
  return randomAppearance(rng, { shirt: 0x6b7480, shirtType: 'long', jacketColor: 0x2b3340, pants: 0x1f2633, hairStyle: rng.chance(0.6) ? 'cap' : 'short', hat: 0x1f2633, shoes: 0x111111, shorts: false, bandana: null, glasses: rng.chance(0.3) });
}

export class Heists {
  constructor(game) {
    this.game = game;
    this.van = null;      // { v, guards, state: 'rounds' | 'alerted' | 'fleeing' | 'open', hits, cash: [], ... }
    this.timer = FIRST;
    this.enabled = true;
    this.robbed = 0;
    const ev = game.events;
    ev.on('vehicleShot', (v, shooter, point) => {
      const h = this.van;
      if (!h || v !== h.v || !shooter?.isPlayer) return;
      this._alert();
      if (point && h.state !== 'open') {
        // the back doors: the last metre of the van, high enough to miss the bumper
        const fx = Math.sin(v.yaw), fz = Math.cos(v.yaw);
        const along = (point.x - v.pos.x) * fx + (point.z - v.pos.z) * fz;
        if (along < -v.def.L / 2 + 0.9 && point.y > v.pos.y + v.def.clearance + 0.25) {
          h.hits++;
          game.audio?.playAt('metalhit', point, 0.6);
          if (h.hits >= DOOR_HITS) this._open();
        }
      }
    });
    ev.on('explosion', (pos, r) => {
      const h = this.van;
      if (!h || h.v.removed) return;
      if (Math.hypot(pos.x - h.v.pos.x, pos.z - h.v.pos.z) < r + 3.5) { this._alert(); this._open(); }
    });
    ev.on('carCrash', (A, B, impact) => {
      const h = this.van, pv = game.player.vehicle;
      if (h && pv && impact > 4 && ((A === h.v && B === pv) || (B === h.v && A === pv))) this._alert();
    });
    ev.on('kill', (killer, victim) => { if (killer?.isPlayer && this.van?.guards.includes(victim)) this._alert(); });
    ev.on('carjack', (by, victim, veh) => { if (by?.isPlayer && veh === this.van?.v) this._alert(); });
    const off = () => { if (this.van && this.van.state === 'open') this._finish(); };
    ev.on('playerDied', off);
    ev.on('busted', off);
  }

  blipList() {
    const h = this.van;
    if (!h || h.v.removed || h.v.isWrecked || h.state === 'open') return [];
    return [{ x: h.v.pos.x, z: h.v.pos.z, icon: 'money', color: 0x6fd36b }];
  }

  update(dt) {
    const g = this.game, p = g.player;
    this.timer -= dt;
    if (!this.van) {
      if (this.timer <= 0 && this.enabled && !g.missions?.active && !p.dead && (g.police?.level ?? 0) === 0 && g.gameplay?.state === 'playing' && !g.net?.online) {
        this.timer = 20; // (retry if no road)
        if (this._spawn()) this.timer = rand(EVERY[0], EVERY[1]);
      }
      return;
    }
    const h = this.van, v = h.v;
    h.t += dt;
    // the doors swing open
    if (v.model?.rearDoors) for (const d of v.model.rearDoors) {
      d.open += ((h.state === 'open' ? 1 : 0) - d.open) * Math.min(1, dt * 4);
      d.pivot.rotation.y = -d.side * d.open * 1.85;
    }
    const pp = p.vehicle ? p.vehicle.pos : p.pos;
    const d = Math.sqrt(dist2(v.pos.x, v.pos.z, pp.x, pp.z));
    // the first sighting
    if (!h.told && d < 120 && h.state === 'rounds') {
      h.told = true;
      g.hud?.help('An <b style="color:#6fd36b">armoured van</b> is doing its rounds nearby. Shoot the back doors open to rob it.', 6);
    }
    // a run for it, until it's stopped: then the guards get out and fight
    if (h.state === 'fleeing') {
      if (v.ai) { v.ai.panic = 20; v.ai.ignoreLights = true; }
      h.still = v.speedAbs < 1.5 ? h.still + dt : 0;
      if (h.still > 1.6 || !v.driver || v.driver.dead || v.health < v.maxHealth * 0.35 || d < 8 && v.speedAbs < 4) this._guardsOut();
    }
    if (h.state === 'alerted' && h.t > h.outT) this._guardsOut();
    // the cash: count what's been picked up
    if (h.cash.length) {
      const left = h.cash.filter((c) => g.pickups.list.includes(c.pk));
      for (const c of h.cash) if (!c.got && !g.pickups.list.includes(c.pk) && c.pk.life > 0) { c.got = true; h.taken += c.amount; }
      if (!left.length && !h.done) {
        h.done = true;
        this.robbed++;
        if (h.taken > 0) { g.hud?.bigMessage('ARMORED VAN ROBBED', 'passed', 3.5, `+${money(h.taken)}`); g.audio?.play('passed', 0.8); }
      }
    }
    // gone: out of range, wrecked and left behind, or robbed and left behind
    const seen = g.peds._inView(v.pos.x, v.pos.z, 3);
    if (v.removed || (d > 360 && !seen) || ((v.isWrecked || h.done) && d > 140 && !seen)) this._finish();
  }

  _spawn() {
    const g = this.game, p = g.player, pp = p.vehicle ? p.vehicle.pos : p.pos;
    if (!g.traffic) return false;
    for (let k = 0; k < 8; k++) {
      const smp = Traffic.sampleLane(g, pp.x, pp.z, 130, 240, (e) => e.type !== 'freeway' && e.type !== 'ramp' && e.type !== 'rail');
      if (!smp || g.peds._inView(smp.x, smp.z, 4)) continue;
      if (g.vehicles.list.some((o) => dist2(o.pos.x, o.pos.z, smp.x, smp.z) < 12 * 12)) continue;
      const v = g.traffic.spawnCar(smp.start, smp.s0, { type: 'stockade' });
      v.persistent = true;
      v.heistVan = true;
      v.locked = true; // (you can still pull the driver out once the guards are out)
      // swap the civilians for guards
      for (let i = 0; i < v.occupants.length; i++) { const o = v.occupants[i]; if (o && !o.isPlayer) { v.occupants[i] = null; o.vehicle = null; g.peds.remove(o); } }
      const rng = new RNG((Math.random() * 1e9) | 0);
      const guards = [];
      for (let s = 0; s < 2; s++) {
        const q = g.peds.spawnPed(v.pos.x, v.pos.z, { appearance: guardLook(rng), brain: 'civilian', health: 130, armor: 50, persistent: true, y: v.pos.y });
        const w = s === 0 ? 'pistol' : rng.pick(['smg', 'shotgun', 'pistol']);
        q.giveWeapon(w, 400); q.equip(w);
        q.accuracy = 0.55; q.damageMul = 0.6;
        q.guard = true;
        v.putIn(q, s);
        guards.push(q);
      }
      this.van = { v, guards, state: 'rounds', hits: 0, cash: [], taken: 0, t: 0, outT: 0, still: 0, told: false, done: false };
      return true;
    }
    return false;
  }

  // shots fired, a ram, a guard down: the van's on alert
  _alert() {
    const g = this.game, h = this.van;
    if (!h || h.state !== 'rounds') return;
    const v = h.v;
    g.police?.crime(1.2, v.pos, true);
    if (v.speedAbs > 4 && v.driver && !v.driver.dead && Math.random() < 0.6) {
      h.state = 'fleeing';
      v.driver.say?.('Code red! Code red!');
    } else { h.state = 'alerted'; h.outT = h.t + 0.6; }
  }

  _guardsOut() {
    const g = this.game, h = this.van, p = g.player;
    h.state = h.state === 'open' ? 'open' : 'alerted';
    h.outT = Infinity;
    const v = h.v;
    if (v.ai) { v.ai = null; v.traffic = false; }
    v.locked = false;
    for (const q of h.guards) {
      if (q.dead || q.removed) continue;
      if (q.vehicle && !g.vehicles.isBusy(q)) g.vehicles.exit(q);
      q.threat = p;
      q.setState('attack');
    }
  }

  // the doors give: the cash bags fall out of the back
  _open() {
    const g = this.game, h = this.van;
    if (!h || h.state === 'open') return;
    const v = h.v;
    const wasRounds = h.state === 'rounds';
    h.state = 'open';
    g.audio?.playAt('metalhit', v.pos, 1);
    g.audio?.play('alarm', 0.5);
    g.police?.raise(2);
    if (wasRounds || v.ai) this._guardsOut();
    const fx = Math.sin(v.yaw), fz = Math.cos(v.yaw), rx = -fz, rz = fx;
    const total = Math.round(rand(3000, 7500) / 50) * 50, n = 4;
    for (let i = 0; i < n; i++) {
      const back = v.def.L / 2 + 0.9 + rand(0, 1.6), side = rand(-1.2, 1.2);
      const x = v.pos.x - fx * back + rx * side, z = v.pos.z - fz * back + rz * side;
      const amount = Math.round(total / n);
      const pk = g.pickups.spawn('money', x, z, { amount, life: 120, y: g.collision.floorHeight(x, z, v.pos.y + 1), blip: true });
      h.cash.push({ pk, amount, got: false });
    }
    g.hud?.help('The doors are open: <b style="color:#6fd36b">grab the cash</b> and lose the cops.', 5);
  }

  _finish() {
    const g = this.game, h = this.van;
    if (!h) return;
    const v = h.v;
    const pp = g.player.vehicle ? g.player.vehicle.pos : g.player.pos;
    for (const q of h.guards) if (!q.removed && (!q.vehicle || q.vehicle === v)) {
      const far = Math.hypot(q.pos.x - pp.x, q.pos.z - pp.z) > 60;
      if (far || q.dead) g.peds.remove(q); else { q.persistent = false; }
    }
    if (!v.removed && !v.driver?.isPlayer) {
      const seen = g.peds._inView(v.pos.x, v.pos.z, 3);
      if (!seen || Math.hypot(v.pos.x - pp.x, v.pos.z - pp.z) > 150) { for (const o of v.occupants) if (o && !o.isPlayer) g.peds.remove(o); v.occupants.fill(null); g.vehicles.remove(v); }
      else v.persistent = false;
    }
    this.van = null;
    this.timer = Math.max(this.timer, rand(EVERY[0], EVERY[1]));
  }

  reset() {
    if (this.van) {
      const g = this.game, h = this.van;
      for (const c of h.cash) { const i = g.pickups.list.indexOf(c.pk); if (i >= 0) { c.pk.remove(); g.pickups.list.splice(i, 1); } }
      for (const q of h.guards) if (!q.removed) g.peds.remove(q);
      if (!h.v.removed && !h.v.driver?.isPlayer) { for (const o of h.v.occupants) if (o && !o.isPlayer) g.peds.remove(o); h.v.occupants.fill(null); g.vehicles.remove(h.v); }
      this.van = null;
    }
    this.timer = FIRST;
  }
}
