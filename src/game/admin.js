// Free roam admin tools (single player only): cheats you can toggle, one-shot actions and a typed
// command console (` key). Also the vehicle spawner, which works online too.
import * as THREE from 'three';
import { VEHICLES } from '../entities/vehicledefs.js';
import { WEAPONS } from './weapondefs.js';
import { clamp } from '../core/utils.js';

// what the spawner offers, by group (trains need their rails, so they're left out)
export const SPAWN_GROUPS = [
  ['Cars', ['meridian', 'kestrel', 'brawler', 'zenith', 'bouncer', 'summit', 'taxi']],
  ['Work & emergency', ['hauler', 'parcel', 'boxer', 'police']],
  ['Military', ['ranger', 'barracks', 'mammoth']],
  ['Aircraft', ['skipper', 'skylark', 'hercules', 'warhawk', 'raptor']],
];

export const TOGGLES = [
  ['god', 'God mode', 'You take no damage'],
  ['vehGod', 'Bulletproof vehicle', 'Whatever you drive can\'t be damaged'],
  ['neverWanted', 'Never wanted', 'The police ignore you'],
  ['superJump', 'Super jump', 'Jump over buses (no fall damage)'],
  ['superRun', 'Super speed', 'Run twice as fast'],
  ['infSprint', 'Endless sprint', 'Never run out of breath'],
  ['lowGravity', 'Moon gravity', 'Everything falls slowly'],
  ['explosive', 'Explosive bullets', 'Every bullet you fire explodes'],
  ['oneHit', 'One-hit kills', 'Your weapons are ten times stronger'],
  ['freezeTime', 'Freeze time', 'The clock stops'],
  ['slowmo', 'Slow motion', 'Half-speed world'],
  ['riot', 'Riot', 'Everyone in the streets starts fighting'],
];

const HELP = [
  'god · car <name> · repair · flip · heal · weapons · wanted <0-5> · never',
  'time <0-24> · weather clear|cloudy|rain|storm|fog · freeze · slowmo',
  'jump · run · sprint · gravity · boom · riot · traffic off|normal|heavy · peds off|normal|heavy',
  'clear · explode · skydive · bodyguard · enemies · tp <place> · colour <n>',
];

const NONE = Object.freeze({});

export class Admin {
  constructor(game) {
    this.game = game;
    this.cheats = {};
    this.summoned = [];
    this.traffic = 'normal';
    this.peds = 'normal';
    this.color = null; // spawner paint: null = the model's own colours
    this.riotT = 0;
    game.cheats = this.cheats;
    game.cheatsOn = NONE;   // what the rest of the game reads: the cheats, or nothing when not allowed
  }

  // admin tools: free roam, not online (the vehicle spawner is allowed online)
  get allowed() { return !!this.game.freeroam?.active && !this.game.net?.online; }
  get canSpawn() { return !!this.game.freeroam?.active; }

  // ------------------------------------------------------------------ per frame
  update(dt) {
    const g = this.game, c = this.cheats, p = g.player;
    if (!this.allowed) {
      g.cheatsOn = NONE;
      if (this._applied) this._reset();
      return;
    }
    g.cheatsOn = c;
    this._applied = true;
    p.invincible = !!c.god;
    if (c.god && p.health < p.maxHealth && !p.dead) p.health = p.maxHealth;
    if (c.neverWanted && g.police && (g.police.level > 0 || g.police.heat > 0)) g.police.clear();
    if (c.infSprint) p.stamina = 1;
    g.gravity = c.lowGravity ? 0.3 : 1;
    // (only touch the clocks when the switch changes: other things slow time too, e.g. being wasted)
    if (!!c.slowmo !== !!this._slow) { this._slow = !!c.slowmo; g.timeScale = c.slowmo ? 0.45 : 1; }
    if (g.env && !!c.freezeTime !== !!this._frozen) {
      this._frozen = !!c.freezeTime;
      if (c.freezeTime) { this._envScale = g.env.timeScale; g.env.timeScale = 0; } else g.env.timeScale = this._envScale || 0.25;
    }
    const v = p.vehicle;
    if (v && c.vehGod && !v.exploded) { v.health = v.maxHealth; v.onFire = false; v.burnTime = 0; }
    this._density();
    if (c.riot) this._riot(dt);
  }

  _reset() {
    const g = this.game;
    this._applied = false;
    g.player.invincible = false;
    g.gravity = 1;
    if (this._slow) { g.timeScale = 1; this._slow = false; }
    if (this._frozen && g.env) { g.env.timeScale = this._envScale || 0.25; this._frozen = false; }
    if (g.traffic && this._baseCars != null) g.traffic.maxCars = this._baseCars;
    if (g.peds && this._basePeds != null) g.peds.maxPeds = this._basePeds;
  }

  _density() {
    const g = this.game;
    if (g.traffic) { this._baseCars ??= g.traffic.maxCars; g.traffic.maxCars = Math.round(this._baseCars * { off: 0, normal: 1, heavy: 2 }[this.traffic]); }
    if (g.peds) { this._basePeds ??= g.peds.maxPeds; g.peds.maxPeds = Math.round(this._basePeds * { off: 0, normal: 1, heavy: 1.8 }[this.peds]); }
    if (this.traffic === 'off' && g.traffic) for (const v of [...g.traffic.cars]) if (!v.removed && g.player.vehicle !== v && !v.persistent) g.traffic._despawn(v);
    if (this.peds === 'off' && g.peds) for (const q of [...g.peds.list]) if (!q.persistent && !q.vehicle && q.brain === 'civilian') g.peds.remove(q);
  }

  // riot: civilians near the player pick fights with each other (and anyone else)
  _riot(dt) {
    const g = this.game;
    this.riotT -= dt;
    if (this.riotT > 0 || !g.peds) return;
    this.riotT = 1.5;
    const p = g.player;
    const near = g.peds.list.filter((q) => !q.dead && !q.vehicle && q.brain === 'civilian' && Math.hypot(q.pos.x - p.pos.x, q.pos.z - p.pos.z) < 70);
    for (const q of near) {
      if (q.state === 'attack' && q.threat && !q.threat.dead) continue;
      if (Math.random() < 0.5) continue;
      const others = near.filter((o) => o !== q);
      const t = Math.random() < 0.15 || !others.length ? p : others[Math.floor(Math.random() * others.length)];
      if (Math.random() < 0.35 && !q.weapons.pistol) { q.giveWeapon(Math.random() < 0.5 ? 'pistol' : 'bat', 60); q.equip(q.weapons.pistol ? 'pistol' : 'bat'); }
      q.threat = t; q.setState('attack');
    }
  }

  toggle(k, on = !this.cheats[k]) {
    if (!this.allowed) return 'Admin tools are for single-player free roam.';
    this.cheats[k] = on;
    const t = TOGGLES.find((x) => x[0] === k);
    return `${t ? t[1] : k}: ${on ? 'on' : 'off'}`;
  }

  // ------------------------------------------------------------------ actions
  heal() { const p = this.game.player; p.health = p.maxHealth; p.armor = 100; return 'Health and armour full.'; }

  weapons() {
    const p = this.game.player;
    for (const [id, d] of Object.entries(WEAPONS)) { if (d.vehicleOnly || d.hidden) continue; p.giveWeapon(id, (d.clip || 1) * 8); }
    return 'All weapons.';
  }

  wanted(n) {
    const pol = this.game.police;
    if (!pol) return 'No police here.';
    n = clamp(Math.round(n) || 0, 0, 5);
    if (n > 0) this.cheats.neverWanted = false;
    if (n === 0) pol.clear(); else pol.setLevel(n);
    return n ? `Wanted level ${n}.` : 'Wanted level cleared.';
  }

  setTime(h) { const env = this.game.env; env.setTime?.(((h % 24) + 24) % 24); return `Time set to ${env.timeString}.`; }
  setWeather(w) {
    const ok = ['clear', 'cloudy', 'rain', 'storm', 'fog'];
    if (!ok.includes(w)) return `Weather: ${ok.join(', ')}.`;
    this.game.env.setWeather(w);
    this.game.env.weatherLocked = true;
    this.game.env.weatherTimer = 1e9;
    return `Weather: ${w}.`;
  }

  repair() {
    const v = this.game.player.vehicle;
    if (!v) return 'Get in a vehicle first.';
    if (v.exploded) return 'That one\'s beyond repair — spawn a new one.';
    v.health = v.maxHealth; v.onFire = false; v.burnTime = 0;
    v.undent?.();
    return `${v.def.name} repaired.`;
  }

  flip() {
    const v = this.game.player.vehicle;
    if (!v) return 'Get in a vehicle first.';
    v.bodyRoll = 0; v.bodyPitch = 0; v.groundPitch = 0; v.groundRoll = 0; v.r = 0;
    if (v.quat) { v.quat.setFromAxisAngle(new THREE.Vector3(0, 1, 0), v.yaw); v.angVel?.set(0, 0, 0); }
    v.pos.y = this.game.collision.floorHeight(v.pos.x, v.pos.z, v.pos.y + 2) + 0.2;
    v.vel.set(0, 0, 0);
    return 'Back on your wheels.';
  }

  clearArea() {
    const g = this.game, p = g.player, R = 90;
    let n = 0;
    for (const v of [...g.vehicles.list]) {
      if (v === p.vehicle || v.def.train || v.remote || v.persistent) continue;
      if (Math.hypot(v.pos.x - p.pos.x, v.pos.z - p.pos.z) > R) continue;
      if (v.occupants.some((o) => o?.isPlayer)) continue;
      if (g.traffic) g.traffic._despawn(v); else g.vehicles.remove(v);
      n++;
    }
    for (const q of [...g.peds.list]) { if (q.persistent || q.vehicle) continue; if (Math.hypot(q.pos.x - p.pos.x, q.pos.z - p.pos.z) > R) continue; g.peds.remove(q); n++; }
    g.police?.clear();
    return `Cleared ${n} people and vehicles.`;
  }

  explodeNearby() {
    const g = this.game, p = g.player;
    let n = 0;
    for (const v of g.vehicles.list) {
      if (v === p.vehicle || v.def.train || v.remote || v.exploded) continue;
      if (Math.hypot(v.pos.x - p.pos.x, v.pos.z - p.pos.z) > 45) continue;
      v.lastDamager = p; v.explode(); n++;
    }
    return n ? `Boom (${n}).` : 'Nothing close enough to blow up.';
  }

  skydive() {
    const g = this.game, p = g.player;
    if (p.vehicle) { const v = p.vehicle; v.takeOut(p); }
    p.setPosition(p.pos.x, g.map.groundHeight(p.pos.x, p.pos.z) + 420, p.pos.z);
    p.vel.set(0, 0, 0);
    p.bailOut?.(400);
    return 'Geronimo! Space opens the parachute.';
  }

  bodyguard() {
    const g = this.game, p = g.player;
    const a = p.yaw + Math.PI;
    const q = g.peds.spawnPed(p.pos.x + Math.sin(a) * 2, p.pos.z + Math.cos(a) * 2, { brain: 'civilian', team: 'player', health: 250, persistent: true });
    q.giveWeapon('rifle', 900); q.equip('rifle');
    q.follow = p; q.followSlot = (this._guards = (this._guards || 0) + 1) % 3; q.setState('follow');
    return 'A bodyguard has your back.';
  }

  enemies() {
    const g = this.game, p = g.player;
    for (let k = 0; k < 5; k++) {
      const a = Math.random() * Math.PI * 2, d = 25 + Math.random() * 10;
      const x = p.pos.x + Math.cos(a) * d, z = p.pos.z + Math.sin(a) * d;
      const q = g.peds.spawnPed(x, z, { brain: 'gang', gang: 'vipers', health: 110 });
      q.giveWeapon(k % 2 ? 'smg' : 'pistol', 400); q.equip(k % 2 ? 'smg' : 'pistol');
      q.threat = p; q.setState('attack');
    }
    return 'Here they come.';
  }

  // ------------------------------------------------------------------ vehicle spawner
  summon(id, opts = {}) {
    const g = this.game, p = g.player, d = VEHICLES[id];
    if (!d || d.train) return 'No such vehicle.';
    if (!this.canSpawn) return 'Vehicle spawning is a free roam feature.';
    if (g.missions?.active) return 'Not during a mission.';
    const old = p.vehicle;
    const into = opts.into !== false;
    if (old && into && !old.def.train) {
      old.takeOut(p);
      if (this.summoned.includes(old)) { this.summoned.splice(this.summoned.indexOf(old), 1); g.vehicles.remove(old); }
    } else if (old) old.takeOut(p);
    const air = !!d.aircraft;
    const fx = Math.sin(p.yaw), fz = Math.cos(p.yaw);
    const ahead = into ? 0 : Math.max(d.L, 5) / 2 + 3;
    let x = p.pos.x + fx * ahead, z = p.pos.z + fz * ahead;
    const gy = g.map.groundHeight(x, z);
    const opt = { persistent: true };
    if (this.color != null) opt.color = this.color;
    const inAir = air && (opts.air ?? (d.kind !== 'heli' || this._cramped(x, z, d)));
    let y = g.collision.floorHeight(x, z, p.pos.y + 1.5);
    if (inAir) y = Math.max(gy, 0) + (d.kind === 'heli' ? 70 : 260);
    const v = g.vehicles.spawn(id, x, z, p.yaw, opt);
    v.persistent = true;
    v.pos.y = y;
    if (inAir && v.quat) {
      v.grounded = false; v.gearDown = false; v.gearK = 0;
      if (d.kind === 'heli') { v.spool = 1; v.vel.set(0, 0, 0); }
      else { v.power = 0.85; v.spool = 0.85; const sp = Math.max(d.vRotate ?? 40, 45) * 1.35; v.vel.set(fx * sp, 0, fz * sp); }
    }
    v._placeGroup?.();
    if (into) g.vehicles.seatNow(p, v, 0);
    this.summoned.push(v);
    // keep a handful: the oldest empty one goes
    while (this.summoned.length > 5) {
      const i = this.summoned.findIndex((o) => o !== p.vehicle && !o.occupants.some((c) => c?.isPlayer));
      if (i < 0) break;
      const [o] = this.summoned.splice(i, 1);
      if (!o.removed) { o.persistent = false; if (!o.driver) g.vehicles.remove(o); }
    }
    return `${d.name}${inAir ? ' (airborne)' : ''}.`;
  }

  // too many buildings round about for a helicopter to spin up?
  _cramped(x, z, d) {
    const r = Math.max(d.L, d.W) / 2 + 2;
    const list = this.game.collision.query(x - r, z - r, x + r, z + r, []);
    return list.some((o) => (o.kind === 'box' || o.kind === 'obox') && (o.maxY ?? 0) > 3);
  }

  // ------------------------------------------------------------------ console
  run(line) {
    const g = this.game;
    const [cmd0, ...args] = String(line || '').trim().split(/\s+/);
    const cmd = (cmd0 || '').toLowerCase();
    const a = args.join(' ').toLowerCase();
    if (!cmd) return '';
    if (cmd === 'help' || cmd === '?') return HELP.join('<br>');
    if (cmd === 'car' || cmd === 'spawn' || cmd === 'veh') {
      const want = a.replace(/\s+/g, '');
      const id = Object.keys(VEHICLES).find((k) => k === want || VEHICLES[k].name.toLowerCase().replace(/\s+/g, '') === want) || Object.keys(VEHICLES).find((k) => k.startsWith(want) || VEHICLES[k].name.toLowerCase().startsWith(a));
      if (!id) return `Vehicles: ${SPAWN_GROUPS.flatMap((x) => x[1]).join(', ')}`;
      return this.summon(id);
    }
    if (cmd === 'tp' || cmd === 'goto') {
      const list = g.freeroam.destinations();
      const d = !a || a === 'waypoint' ? list.find((x) => x.name === 'Map waypoint') : list.find((x) => x.name.toLowerCase().includes(a));
      if (!d) return a ? `No place called "${a}".` : 'Set a waypoint on the map first.';
      g.freeroam.teleport(d);
      return `Teleporting to ${d.name}.`;
    }
    if (cmd === 'colour' || cmd === 'color' || cmd === 'paint') {
      if (!a || a === 'default') { this.color = null; return 'Spawned vehicles use their own colours.'; }
      const n = parseInt(a.replace('#', ''), 16);
      if (!Number.isFinite(n)) return 'colour <hex>, e.g. colour ff0000';
      this.color = n;
      const v = g.player.vehicle;
      if (v?.model?.bodyMat) v.model.bodyMat.color?.setHex(n);
      return `Paint: #${n.toString(16).padStart(6, '0')}.`;
    }
    if (!this.allowed) return 'Admin commands work in single-player free roam (the vehicle spawner works online too).';
    const T = { god: 'god', jump: 'superJump', run: 'superRun', speed: 'superRun', sprint: 'infSprint', gravity: 'lowGravity', moon: 'lowGravity', boom: 'explosive', explosive: 'explosive', onehit: 'oneHit', freeze: 'freezeTime', slowmo: 'slowmo', never: 'neverWanted', riot: 'riot', bulletproof: 'vehGod' };
    if (T[cmd]) return this.toggle(T[cmd], a === 'on' ? true : a === 'off' ? false : undefined);
    switch (cmd) {
      case 'heal': case 'health': case 'armour': case 'armor': return this.heal();
      case 'weapons': case 'guns': return this.weapons();
      case 'wanted': return this.wanted(parseFloat(a));
      case 'time': return this.setTime(parseFloat(a) || 12);
      case 'weather': return this.setWeather(a);
      case 'repair': case 'fix': return this.repair();
      case 'flip': return this.flip();
      case 'clear': return this.clearArea();
      case 'explode': case 'nuke': return this.explodeNearby();
      case 'skydive': return this.skydive();
      case 'bodyguard': return this.bodyguard();
      case 'enemies': case 'attack': return this.enemies();
      case 'traffic': if (!['off', 'normal', 'heavy'].includes(a)) return 'traffic off | normal | heavy'; this.traffic = a; return `Traffic: ${a}.`;
      case 'peds': case 'people': if (!['off', 'normal', 'heavy'].includes(a)) return 'peds off | normal | heavy'; this.peds = a; return `Pedestrians: ${a}.`;
      default: return `Unknown command "${cmd}". Type <b>help</b>.`;
    }
  }
}
