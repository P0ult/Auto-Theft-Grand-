// The army joins the chase at five stars (police.js runs one to five): Barracks troop trucks and Ranger
// jeeps that run you down and unload soldiers, a Mammoth tank that hunts you down and shells you, and a
// Warhawk gunship flown by an autopilot that strafes you with its chin gun and rocket pods. They pull out
// when the stars drop below five, and everything resets when you're wasted or busted.
import * as THREE from 'three';
import { PursuitDriver } from './police.js';
import { LaneDriver } from './traffic.js';
import { randomAppearance } from '../entities/humanoid.js';
import { RNG, rand, pick, clamp, dist2, wrapAngle } from '../core/utils.js';

const RADIO = [
  'All units: the National Guard has been authorised. Military assets inbound.',
  'Command to all callsigns: suspect is armed and extremely dangerous. Weapons free.',
  'Armour rolling out of Fort Carver. Clear the streets.',
  'Gunship on station. Painting the target.',
];

// the Fort Carver uniform (military.js uses the same look for the garrison)
export function soldierLook(rng = new RNG((Math.random() * 1e9) | 0)) {
  return randomAppearance(rng, {
    female: rng.chance(0.15), shirt: 0x5b6b3a, shirtType: 'long', pants: 0x4d5a33, jacketColor: 0x5b6b3a,
    hairStyle: 'cap', hat: 0x46542c, shoes: 0x2a2620, glasses: false, bandana: null, beard: false, shorts: false,
  });
}

export class Army {
  constructor(game) {
    this.game = game;
    this.active = false;
    this.units = [];    // { veh, kind: 'truck' | 'jeep' | 'tank' | 'heli', ... }
    this.troops = [];   // soldiers on foot
    this.time = 0;      // seconds at five stars
    this.spawnT = 0;
    this.radioT = 0;
    this.enabled = true;
  }

  get targetPos() { return this.game.police.targetPos(); }

  update(dt) {
    const g = this.game, pol = g.police, p = g.player;
    if (!pol || g.gameplay?.state === 'menu') return;
    const want = this.enabled && pol.level >= 5 && !p.dead && !g.missions?.noArmy;
    if (want && !this.active) this._activate();
    else if (!want && this.active) this._standDown();
    this.troops = this.troops.filter((s) => !s.removed);
    this.units = this.units.filter((u) => !u.veh.removed);
    if (this.active) {
      this.time += dt;
      this._spawn(dt);
      this.radioT -= dt;
      if (this.radioT <= 0) { this.radioT = rand(25, 40); g.hud?.dispatch?.(pick(RADIO)); }
    }
    for (const u of this.units) {
      const v = u.veh;
      if (!v.driver || v.driver.dead || v.isWrecked) continue;
      if (u.kind === 'heli') this._fly(u, dt);
      else if (u.kind === 'tank') this._tank(u, dt);
      else this._convoy(u, dt);
    }
    this._despawn();
  }

  _activate() {
    const g = this.game;
    this.active = true;
    this.time = 0;
    this.spawnT = 2;
    this.radioT = 6;
    g.peds.gangAggro.army = true;
    g.hud?.bigMessage('THE ARMY IS COMING', 'failed', 3.5, 'Five stars · Fort Carver has been mobilised');
    g.hud?.dispatch?.(RADIO[0]);
    g.audio?.play('alarm', 0.8);
    g.events.emit('armyDeployed');
  }

  // below five stars: the units head home (they're tidied up once out of sight)
  _standDown() {
    const g = this.game;
    this.active = false;
    g.peds.gangAggro.army = !!g.military?.alerted;
    for (const s of this.troops) if (!s.dead && s.state === 'attack' && !g.peds.gangAggro.army) { s.threat = null; s.setState('guard'); }
    for (const u of this.units) {
      u.leaving = true;
      const v = u.veh;
      if (u.kind !== 'heli' && v.driver && !v.driver.isPlayer && !v.isWrecked) { v.ai = new LaneDriver(g, v, null); v.aimAt = null; }
    }
  }

  reset() {
    const g = this.game;
    for (const u of this.units) {
      for (const o of u.veh.occupants) if (o && !o.isPlayer) g.peds.remove(o);
      u.veh.occupants.fill(null);
      if (!u.veh.occupants.some((o) => o?.isPlayer)) g.vehicles.remove(u.veh);
    }
    for (const s of this.troops) g.peds.remove(s);
    this.units = []; this.troops = [];
    if (this.active) { this.active = false; g.peds.gangAggro.army = !!g.military?.alerted; }
  }

  // ------------------------------------------------------------------ spawning
  soldier(x, z, opts = {}) {
    const g = this.game;
    const s = g.peds.spawnPed(x, z, { appearance: soldierLook(), brain: 'gang', gang: 'army', state: 'guard', weapon: opts.weapon || pick(['rifle', 'rifle', 'smg']), health: 140, armor: 40, persistent: true, ...opts });
    s.accuracy = 0.5;
    s.damageMul = 0.55;
    s.soldier = true;
    s.response = true; // (not the Fort Carver garrison: killing one doesn't lock the base down)
    return s;
  }

  _spawn(dt) {
    const g = this.game, pol = g.police;
    this.spawnT -= dt;
    if (this.spawnT > 0) return;
    this.spawnT = 3;
    const alive = (k) => this.units.filter((u) => u.kind === k && !u.leaving && !u.veh.isWrecked && u.veh.driver && !u.veh.driver.dead).length;
    const crew = (v) => this.soldier(v.pos.x, v.pos.z);
    const ground = (type, seats, kind) => {
      const v = pol.spawnCar(true, null, { type, seats, crew, list: [], siren: false, radius: [110, 230] });
      if (!v) return null;
      v.armyUnit = true;
      v.stable = true;
      const u = { veh: v, kind, unloaded: false, fireT: 3 };
      this.units.push(u);
      return u;
    };
    // troop trucks and jeeps (on the roads: they need a lane to spawn on)
    if (alive('truck') + alive('jeep') < 2) { if (alive('truck') <= alive('jeep')) ground('barracks', 2, 'truck'); else ground('ranger', 3, 'jeep'); return; }
    // the gunship after a few seconds
    if (this.time > 8 && alive('heli') < 1 && !this.units.some((u) => u.kind === 'heli' && !u.veh.isWrecked)) { this._spawnHeli(); return; }
    // and then the armour
    if (this.time > 18 && alive('tank') < 1 && !this.units.some((u) => u.kind === 'tank' && !u.veh.isWrecked)) {
      ground('mammoth', 1, 'tank');
    }
  }

  _spawnHeli() {
    const g = this.game;
    const tp = this.targetPos;
    const a = Math.random() * Math.PI * 2;
    const x = tp.x + Math.cos(a) * 240, z = tp.z + Math.sin(a) * 240;
    const y = Math.max(g.map.groundHeight(x, z), 0) + 75;
    const yaw = Math.atan2(tp.x - x, tp.z - z);
    const v = g.vehicles.spawn('warhawk', x, z, yaw, { y, persistent: true });
    const pilot = this.soldier(x, z, { weapon: 'pistol' });
    v.putIn(pilot, 0); pilot.homeCar = v;
    v.spool = 1; v.grounded = false;
    v.vel.set(Math.sin(yaw) * 24, 0, Math.cos(yaw) * 24); // (arrives at speed)
    v.armyUnit = true;
    this.units.push({ veh: v, kind: 'heli', burst: 0, burstT: 4, rocketT: 6, orbit: Math.random() * 6.28, side: Math.random() < 0.5 ? 1 : -1 });
  }

  // ------------------------------------------------------------------ troop trucks & jeeps
  _convoy(u, dt) {
    const g = this.game, v = u.veh, pl = g.player;
    v.ai?.update(dt);
    if (u.leaving) return;
    const tp = this.targetPos;
    const d = Math.hypot(tp.x - v.pos.x, tp.z - v.pos.z);
    // close in and slowed down (or stuck): the troops pile out of the back
    if (!u.unloaded && (d < (pl.vehicle ? 26 : 38)) && v.speedAbs < 6) {
      u.unloaded = true;
      const n = u.kind === 'truck' ? 4 : 0;
      const s = Math.sin(v.yaw), c = Math.cos(v.yaw);
      for (let i = 0; i < n; i++) {
        const back = v.def.L / 2 + 1 + (i >> 1) * 1.1, side = (i & 1 ? 1 : -1) * 0.8;
        const x = v.pos.x - s * back + c * side, z = v.pos.z - c * back - s * side;
        const sol = this.soldier(x, z, { yaw: v.yaw + Math.PI, y: g.collision.floorHeight(x, z, v.pos.y + 0.5) });
        sol.threat = pl; sol.setState('attack');
        this.troops.push(sol);
      }
      // the passengers too; the driver keeps the truck after you
      for (const o of [...v.occupants]) {
        if (!o || o === v.driver || o.isPlayer || g.vehicles.isBusy(o)) continue;
        g.vehicles.exit(o);
        o.threat = pl; o.setState('attack');
        this.troops.push(o);
      }
      g.hud?.dispatch?.(u.kind === 'truck' ? 'Troops deploying!' : 'Ranger crew dismounting.');
    }
    // ready to go again once you've driven off
    if (u.unloaded && d > 120) u.unloaded = u.kind === 'truck';
  }

  // ------------------------------------------------------------------ the tank
  _tank(u, dt) {
    const g = this.game, v = u.veh, pl = g.player;
    if (u.leaving) { v.ai?.update(dt); return; }
    const tp = this.targetPos;
    const d = Math.hypot(tp.x - v.pos.x, tp.z - v.pos.z);
    const see = d < 180 && g.collision.lineOfSight(v.pos.x, v.pos.y + 2.4, v.pos.z, tp.x, tp.y + 1.2, tp.z);
    // drive towards you; hold back and shell once in range and in sight
    v.ai?.update(dt);
    if (see && d < 70) { v.input.throttle = 0; v.input.brake = v.speed > 0.5 ? 1 : 0; }
    // the turret: lead a moving target a little, and miss by a few metres (it's a tank, not a sniper)
    if (u.miss === undefined || (u.missT -= dt) <= 0) { u.miss = new THREE.Vector3(rand(-4, 4), 0, rand(-4, 4)); u.missT = rand(2, 4); }
    const tv = pl.vehicle ? pl.vehicle.vel : pl.vel;
    const lead = clamp(d / 190, 0, 1.2);
    v.aimAt = (v.aimAt || new THREE.Vector3()).set(tp.x + tv.x * lead + u.miss.x, tp.y + 0.8, tp.z + tv.z * lead + u.miss.z);
    u.fireT -= dt;
    if (see && u.fireT <= 0 && v.reload <= 0 && d > 12 && !this._friendlyNear(v.aimAt, 11, v)) {
      const want = wrapAngle(Math.atan2(v.aimAt.x - v.pos.x, v.aimAt.z - v.pos.z) - v.yaw);
      if (Math.abs(wrapAngle(want - v.turretYaw)) < 0.07) { v.fireCannon(); u.fireT = rand(3.5, 5.5); }
    }
  }

  // ------------------------------------------------------------------ the gunship's autopilot
  _fly(u, dt) {
    const g = this.game, v = u.veh, c = v.ctl, pl = g.player;
    v.spool = Math.max(v.spool, 0.95);
    const tp = this.targetPos;
    let gx, gz, alt;
    if (u.leaving) {
      // climb away towards Fort Carver
      gx = v.pos.x + Math.sin(v.yaw) * 300; gz = v.pos.z + Math.cos(v.yaw) * 300; alt = 140;
      u.leaveT = (u.leaveT || 0) + dt;
      if (u.leaveT > 25) { this._remove(u); return; }
    } else {
      // circle the target at 60 m, off to one side so the chin gun has a clear line
      u.orbit += dt * 0.22 * u.side;
      gx = tp.x + Math.cos(u.orbit) * 60; gz = tp.z + Math.sin(u.orbit) * 60;
      alt = 38;
    }
    // altitude above whatever is higher: the ground here, or where we're going; climb over buildings ahead
    const here = g.map.groundHeight(v.pos.x, v.pos.z), there = g.map.groundHeight(gx, gz);
    let targetY = Math.max(here, there, 0) + alt;
    const fx = Math.sin(v.yaw), fz = Math.cos(v.yaw);
    const ahead = g.collision.raycast(v.pos.x, v.pos.y + 1, v.pos.z, fx, 0, fz, 50, { ignoreProps: true });
    if (ahead) targetY = Math.max(targetY, v.pos.y + 25);
    c.coll = clamp((targetY - v.pos.y) * 0.12 - v.vel.y * 0.35, -1, 1);
    // face the target once close, otherwise the direction of travel
    const dx = gx - v.pos.x, dz = gz - v.pos.z, dist = Math.hypot(dx, dz);
    const faceX = !u.leaving && Math.hypot(tp.x - v.pos.x, tp.z - v.pos.z) < 160 ? tp.x - v.pos.x : dx;
    const faceZ = !u.leaving && Math.hypot(tp.x - v.pos.x, tp.z - v.pos.z) < 160 ? tp.z - v.pos.z : dz;
    const dyaw = wrapAngle(Math.atan2(faceX, faceZ) - v.yaw);
    c.yaw = clamp(-dyaw * 1.6, -1, 1);
    // move towards the goal point in the heli's own frame (pitch forward / roll sideways)
    const sy = Math.sin(v.yaw), cy = Math.cos(v.yaw);
    const fwd = dx * sy + dz * cy, left = dx * cy - dz * sy;
    const vF = v.vel.x * sy + v.vel.z * cy, vL = v.vel.x * cy - v.vel.z * sy;
    const want = Math.min(dist * 0.5, u.leaving ? 45 : 26);
    const wantF = dist > 1 ? fwd / dist * want : 0, wantL = dist > 1 ? left / dist * want : 0;
    c.pitch = clamp((wantF - vF) * 0.08, -1, 1);
    c.roll = clamp(-(wantL - vL) * 0.08, -1, 1);
    if (u.leaving) return;
    // weapons
    const d = Math.hypot(tp.x - v.pos.x, tp.z - v.pos.z);
    const see = d < 220 && g.collision.lineOfSight(v.pos.x, v.pos.y - 0.5, v.pos.z, tp.x, tp.y + 1.2, tp.z);
    const tv = pl.vehicle ? pl.vehicle.vel : pl.vel;
    v.aimAt = (v.aimAt || new THREE.Vector3()).set(tp.x + tv.x * 0.25 + rand(-1.5, 1.5), tp.y + 1, tp.z + tv.z * 0.25 + rand(-1.5, 1.5));
    u.burstT -= dt; u.rocketT -= dt;
    if (u.burst > 0) {
      u.burst -= dt;
      if (v.gunT <= 0 && see) { v.fireMinigun(); v.gunT = 0.09; }
    } else if (u.burstT <= 0 && see && d < 170) { u.burst = 1.4; u.burstT = rand(3, 5); }
    if (u.rocketT <= 0 && see && d > 30 && d < 200 && !this._friendlyNear(v.aimAt, 9, v)) { v.fireRocket(); u.rocketT = rand(5, 8); }
  }

  // own troops or vehicles near where a shell or rocket would land (they hold fire rather than hit them)
  _friendlyNear(p, r, self) {
    const r2 = r * r;
    for (const s of this.troops) if (!s.dead && !s.removed && (s.pos.x - p.x) ** 2 + (s.pos.z - p.z) ** 2 < r2) return true;
    for (const o of this.units) { const w = o.veh; if (w !== self && !w.removed && !w.isWrecked && !o.leaving && (w.pos.x - p.x) ** 2 + (w.pos.z - p.z) ** 2 < r2) return true; }
    return false;
  }

  // ------------------------------------------------------------------ tidy-up
  _remove(u) {
    const g = this.game;
    if (u.veh.occupants.some((o) => o?.isPlayer)) return;
    for (const o of u.veh.occupants) if (o) g.peds.remove(o);
    u.veh.occupants.fill(null);
    g.vehicles.remove(u.veh);
  }

  _despawn() {
    const g = this.game, pl = g.player;
    const pp = pl.vehicle ? pl.vehicle.pos : pl.pos;
    for (const u of this.units) {
      const v = u.veh;
      if (v.occupants.some((o) => o?.isPlayer)) { u.leaving = true; continue; } // stolen: it's yours now
      const d2 = dist2(v.pos.x, v.pos.z, pp.x, pp.z);
      const far = u.kind === 'heli' ? 420 : 280;
      if (d2 > far * far || (v.isWrecked && d2 > 140 * 140) || (u.leaving && d2 > 200 * 200 && !g.peds._inView(v.pos.x, v.pos.z))) this._remove(u);
    }
    for (const s of this.troops) {
      const d2 = dist2(s.pos.x, s.pos.z, pp.x, pp.z);
      if (d2 > 220 * 220 || (s.dead && d2 > 70 * 70) || (!this.active && !s.dead && d2 > 90 * 90 && !g.peds._inView(s.pos.x, s.pos.z))) g.peds.remove(s);
    }
  }
}
