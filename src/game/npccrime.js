// NPC crime: the people of Los Soles break the law too. Pedestrians jaywalk across the middle of the block,
// some drivers speed and run red lights, bumps between cars turn into road rage or a hit-and-run, and muggers
// and car thieves work the streets (more of them at night and in the rougher districts). A crime the police
// see, or that somebody calls in, puts wanted stars on the culprit — over their head and on the radar — and
// the nearest patrol responds: a ticket for the small stuff; for the rest a pursuit, a tackle and a ride in
// the back of the cruiser. The player's own wanted level always comes first: while you're wanted, every unit
// is after you and the suspects get a head start.
import * as THREE from 'three';
import { LaneDriver } from './traffic.js';
import { XS, ZS, HALF_ROAD } from '../world/citymap.js';
import { rand, randInt, pick, clamp, dist2 } from '../core/utils.js';

// how often the staged crimes (reckless drivers, muggings, car thefts) happen
const LEVELS = { off: 0, normal: 1, high: 2.2 };
const CRIMES = {
  jaywalk: { name: 'Jaywalking', stars: 1, ticket: true },
  speeding: { name: 'Reckless driving', stars: 1, ticket: true },
  hitrun: { name: 'Hit and run', stars: 1 },
  hitped: { name: 'Hit and run', stars: 2 },
  assault: { name: 'Assault', stars: 1 },
  mugging: { name: 'Mugging', stars: 2 },
  gta: { name: 'Grand theft auto', stars: 2 },
  evading: { name: 'Evading police', stars: 2, add: 1 },
  resisting: { name: 'Resisting arrest', stars: 2, add: 1 },
  copAssault: { name: 'Attacking an officer', stars: 3 },
  copKill: { name: 'Officer down', stars: 4 },
};
const SAY = {
  ticket: ['Crosswalk\'s right there, pal.', 'That\'s a ticket.', 'Use the crossing next time.', 'Do you know how fast you were going?', 'License and registration.'],
  ticketed: ['Yeah, yeah...', 'For THAT? Seriously?', 'Fine. Whatever.', 'I was in a hurry, officer!'],
  stop: ['Police! Stop!', 'Freeze!', 'Stop right there!', 'On the ground!', 'Pull over!'],
  surrender: ['Okay, okay! Don\'t shoot!', 'I give up!', 'It wasn\'t me!', 'Alright, alright!'],
  flee: ['Not today!', 'Can\'t catch me!', 'Screw this!', 'I\'m outta here!'],
  cuffed: ['This is harassment!', 'I want my lawyer!', 'Ow! Watch the arm!', 'You got the wrong guy!'],
  mug: ['Wallet. Now.', 'Give me your money!', 'Don\'t make this hard.', 'Empty your pockets!'],
  mugged: ['Help! Police! He took my wallet!', 'Thief! Somebody stop him!', 'Help! I\'ve been robbed!'],
  rage: ['Are you blind?!', 'Look what you did to my car!', 'Learn to drive, idiot!', 'You wanna go?!', 'Get out of the car!'],
  sorry: ['Sorry! Sorry!', 'My bad!', 'Whoa, are you okay?!', 'I didn\'t see you!'],
};
const ROUGH = { hood: 1.8, corona: 1.6, docks: 1.4, westside: 1.2, midtown: 1, downtown: 0.9, beach: 0.8, hills: 0.4 };
const P = (c) => (c.vehicle ? c.vehicle.pos : c.ragdolling ? c.ragdoll.center : c.pos);

// ------------------------------------------------------------------ drivers
// A patrol car on a job: follows the roads to the suspect, pulls up near them, and runs a fleeing car down.
class SuspectDriver extends LaneDriver {
  constructor(game, veh, rec) {
    super(game, veh, false);
    this.rec = rec;
    this.fixedCruise = true;
    this.cruise = 20;
    this.ignoreLights = true;
    this.reverseT = 0;
    this.resnap();
  }
  _tp() { return P(this.rec.ped); }
  _chooseNext(cur) {
    const opts = this._options(cur);
    if (!opts.length) return null;
    const t = this._tp();
    let best = opts[0], bd = Infinity;
    for (const o of opts) { const far = this.net.nodes[o.dir === 0 ? o.e.b : o.e.a]; const d = Math.hypot(far.x - t.x, far.z - t.z) + Math.random() * 15; if (d < bd) { bd = d; best = o; } }
    return best;
  }
  update(dt) {
    const v = this.veh, rec = this.rec;
    if (!v.driver || v.isWrecked) return;
    const tp = this._tp();
    const d = Math.hypot(tp.x - v.pos.x, tp.z - v.pos.z);
    const sv = rec.ped.vehicle, chase = rec.phase === 'chase' && sv;
    const inp = v.input;
    this.dist = d;
    const stop = () => { inp.throttle = 0; inp.steer = 0; inp.brake = v.speedAbs > 0.4 ? 1 : 0; inp.handbrake = v.speedAbs <= 0.4; };
    // close enough to get out: stop (behind a pulled-over car, or by a suspect on foot)
    if (!chase && (d < (sv ? 11 : 16) || (rec.park && d < 45))) { stop(); return; }
    // a car pulled over nearby: drive straight up behind it (turning round if we came the other way)
    if (sv && rec.phase === 'pulled' && sv.speedAbs < 2 && d < 70) {
      const tx = sv.pos.x - Math.sin(sv.yaw) * 9, tz = sv.pos.z - Math.cos(sv.yaw) * 9;
      const dd = Math.hypot(tx - v.pos.x, tz - v.pos.z);
      if (dd < 3) { stop(); return; }
      const [lx, lz] = v.worldToLocal(tx, tz);
      const behind = lz < 0;
      if (this.reverseT > 0 || (behind && dd < 12)) {
        // just past it: back up
        this.reverseT = Math.max(0, this.reverseT - dt);
        inp.throttle = 0; inp.brake = v.speed > -3 ? 0.6 : 0; inp.handbrake = false; inp.steer = clamp(-Math.atan2(lx, -lz) / (v.def.steer * 0.7), -1, 1);
        return;
      }
      inp.steer = clamp(Math.atan2(lx, Math.max(0.5, lz)) / (v.def.steer * 0.7), -1, 1);
      const want = clamp(dd * 0.5, 2.5, 9), err = want - v.speed;
      inp.throttle = err > 0 ? clamp(err * 0.3, 0.15, 0.8) : 0;
      inp.brake = err < -1 ? clamp(-err * 0.25, 0.2, 1) : 0;
      inp.handbrake = false;
      if (Math.abs(v.speed) < 0.5 && want > 3) { this.stuck += dt; if (this.stuck > 2) { this.reverseT = 1.2; this.stuck = 0; } } else this.stuck = 0;
      return;
    }
    // a suspect driving: close in along the roads, then tail them (lights on: time to pull over) or run them down
    const tail = sv && !chase && rec.phase === 'respond' && sv.speedAbs > 2;
    if (!(chase || tail) || d > 55) { this.cruise = sv ? 34 : rec.phase === 'respond' ? 24 : 16; if (this._direct) { this._direct = false; this.resnap(); } super.update(dt); return; }
    this._direct = true;
    const lead = clamp(d / 30, 0, 1.2);
    const tx = tp.x + sv.vel.x * lead, tz = tp.z + sv.vel.z * lead;
    const [lx, lz] = v.worldToLocal(tx, tz);
    const col = this.game.collision;
    const probe = (ang) => { const a = v.yaw + ang; const h = col.raycast(v.pos.x, v.pos.y + 0.8, v.pos.z, Math.sin(a), 0, Math.cos(a), 14, { ignoreProps: true }); return h ? h.t : 14; };
    const fL = probe(0.35), fR = probe(-0.35), fC = probe(0);
    let steer = Math.atan2(lx, Math.max(0.5, lz)) / (v.def.steer * 0.7);
    if (fC < 9) steer += fL > fR ? 1.2 : -1.2; else if (fL < 6) steer -= 0.6; else if (fR < 6) steer += 0.6;
    inp.steer = clamp(steer, -1, 1);
    if (this.reverseT > 0) { this.reverseT -= dt; inp.throttle = 0; inp.brake = 1; inp.steer = -inp.steer; inp.handbrake = false; return; }
    let want = chase ? 36 : clamp(sv.speedAbs + (d - 12) * 0.8, 0, 38);
    if (Math.abs(Math.atan2(lx, lz)) > 1.4 && d < 30) want = Math.min(want, 9);
    const speed = v.speed;
    if (Math.abs(speed) < 1 && want > 5) { this.stuck += dt; if (this.stuck > 1.5) { this.reverseT = 1.2; this.stuck = 0; } } else this.stuck = 0;
    const err = want - speed;
    inp.throttle = err > 0 ? clamp(err * 0.3, 0.2, 1) : 0;
    inp.brake = err < -2 ? clamp(-err * 0.2, 0.2, 1) : 0;
    inp.handbrake = Math.abs(Math.atan2(lx, lz)) > 1.2 && speed > 10;
  }
}

// Getting away: fast, through red lights, always taking the road that leads away from the police.
class FleeDriver extends LaneDriver {
  constructor(game, veh, from) {
    super(game, veh, false);
    this.from = from;
    this.fixedCruise = true;
    this.cruise = rand(24, 30);
    this.ignoreLights = true;
    this.resnap();
  }
  _chooseNext(cur) {
    const opts = this._options(cur);
    if (!opts.length) return null;
    const f = this.from();
    let best = opts[0], bd = -Infinity;
    for (const o of opts) { const far = this.net.nodes[o.dir === 0 ? o.e.b : o.e.a]; const d = Math.hypot(far.x - f.x, far.z - f.z) + Math.random() * 60; if (d > bd) { bd = d; best = o; } }
    return best;
  }
}

// Pulled over: ease to the kerb and wait. (It reads as broken down to the traffic behind, which drives round.)
class PullOverDriver {
  constructor(veh) { this.veh = veh; this.t = 0; this.jam = 99; this.paths = []; }
  update(dt) {
    const v = this.veh, inp = v.input;
    this.t += dt;
    inp.throttle = 0; inp.steer = this.t < 1.2 && v.speedAbs > 2 ? 0.3 : 0;
    inp.brake = v.speedAbs > 0.4 ? 0.75 : 0; inp.handbrake = v.speedAbs <= 0.4;
  }
  resnap() {}
}

// ------------------------------------------------------------------ the system
export class NpcCrime {
  constructor(game) {
    this.game = game;
    this.cases = [];
    this.mugT = rand(45, 90);
    this.theftT = rand(70, 130);
    this.recklessT = 4;
    this.scanT = 0;
    this.later = [];          // [time, fn]: little delays in game time (a driver getting out after a bump...)
    game.events.on('carCrash', (A, B, impact) => this._onCrash(A, B, impact));
    game.events.on('pedHitByCar', (c, v, spd) => this._onPedHit(c, v, spd));
    game.events.on('melee', (att, vic) => this._onViolence(att, vic));
    game.events.on('kill', (killer, victim) => this._onViolence(killer, victim, true));
    game.events.on('wantedUp', () => { for (const rec of this.cases) this._release(rec); });
  }

  get level() { return LEVELS[this.game.settings.npcCrime] ?? 1; }
  get running() {
    const g = this.game;
    return this.level > 0 && !g.disableAmbient && !g.cutscene && g.gameplay?.state !== 'menu';
  }
  _policeFree() { const p = this.game.police; return !!p && p.enabled && p.level === 0 && !this.game.player.dead; }
  _pp() { const p = this.game.player; return p.vehicle ? p.vehicle.pos : p.pos; }
  _after(sec, fn) { this.later.push([this.game.time + sec, fn]); }
  // Think calls come unevenly (twice in a frame for people out of view), so timers run on the game clock:
  // the time since this character's last call, not the dt handed in.
  _dt(c) { const t = this.game.time, d = clamp(t - (c._crimeClock ?? t), 0, 0.25); c._crimeClock = t; return d; }
  _civ(p) {
    return p && !p.removed && !p.dead && !p.isPlayer && !p.remote && p.brain === 'civilian' && !p.persistent && !p.missionTag && !p.shopClerk &&
      !p.npcCase && !p.crimeTask && p.state !== 'follow' && !p.ragdolling;
  }
  _driver(v) {
    const d = v?.driver;
    return d && !v.removed && !v.remote && !v.policeUnit && !v.def.police && !v.persistent && !v.isWrecked && this._civ(d) && v.ai ? d : null;
  }

  // ------------------------------------------------------------------ witnesses
  // the nearest police unit that can see a spot: a patrol car (or its crew on foot)
  _policeSees(x, y, z, R = 40) {
    const g = this.game, pol = g.police;
    if (!pol) return null;
    let best = null, bd = R * R;
    const look = (ux, uy, uz, unit) => {
      const d2 = dist2(ux, uz, x, z);
      if (d2 >= bd) return;
      if (d2 > 144 && !g.collision.lineOfSight(ux, uy + 1.5, uz, x, y + 1.2, z)) return;
      bd = d2; best = unit;
    };
    for (const v of pol.cars) if (!v.removed && !v.isWrecked && v.driver?.brain === 'cop' && !v.driver.dead) look(v.pos.x, v.pos.y, v.pos.z, v);
    for (const c of pol.cops) if (!c.dead && !c.removed && !c.vehicle && c.homeCar && !c.homeCar.removed) look(c.pos.x, c.pos.y, c.pos.z, c.homeCar);
    return best;
  }

  // ------------------------------------------------------------------ crimes
  // An NPC broke the law. Seen by the police it counts at once; otherwise somebody may call it in.
  commit(ped, key, opts = {}) {
    if (!this.running || !ped || ped.dead || ped.removed || ped.isPlayer || ped.remote) return null;
    const C = CRIMES[key], g = this.game;
    let rec = ped.npcCase;
    if (!rec) {
      if (this.cases.length >= (this.level > 1 ? 7 : 4) && !opts.force) return null;
      rec = { ped, crime: key, stars: 0, known: false, t: g.time, seenT: g.time, pos: new THREE.Vector3(), scene: new THREE.Vector3(), phase: 'open', unit: null, car: null, callAt: null, dispatchT: 0 };
      rec.scene.copy(P(ped));
      ped.npcCase = rec;
      this.cases.push(rec);
    }
    if (C.add) rec.stars = Math.min(5, rec.stars + C.add);
    if (C.stars > (CRIMES[rec.crime]?.stars ?? 0) || C.add) rec.crime = key;
    rec.stars = Math.max(rec.stars, C.stars);
    if (opts.car) rec.car = opts.car;
    rec.pos.copy(P(ped));
    const pos = P(ped);
    const unit = opts.witness !== undefined ? opts.witness : this._policeSees(pos.x, pos.y, pos.z, opts.seeR ?? 42);
    if (unit || rec.known) this._know(rec, unit);
    else if (opts.call && Math.random() < opts.call) rec.callAt = Math.min(rec.callAt ?? Infinity, g.time + rand(opts.delay?.[0] ?? 4, opts.delay?.[1] ?? 9));
    else if (!rec.callAt) rec.expire = g.time + 25; // nobody saw: it only counts if a patrol spots them soon
    return rec;
  }

  _know(rec, unit = null) {
    const g = this.game, first = !rec.known;
    rec.known = true; rec.callAt = null; rec.expire = null;
    rec.seenT = g.time; rec.pos.copy(P(rec.ped));
    rec.ped.npcWanted = rec.stars;
    // (kept around while wanted: a chase can run further than pedestrians normally live from the player)
    if (first) { rec.pedPersistent = !!rec.ped.persistent; rec.ped.persistent = true; }
    if (rec.car && !rec.car.removed) { rec.carPersistent ??= !!rec.car.persistent; rec.car.persistent = true; }
    if (first || rec.stars !== rec.shownStars) {
      rec.shownStars = rec.stars;
      const pp = this._pp();
      if (dist2(rec.pos.x, rec.pos.z, pp.x, pp.z) < 260 * 260) g.hud?.dispatch?.(`${'★'.repeat(rec.stars)} ${CRIMES[rec.crime].name}`, g.map.zoneName(rec.pos.x, rec.pos.z));
    }
    if (unit && !rec.unit && this._policeFree() && !unit.npcJob) this._assign(rec, unit);
  }

  // ------------------------------------------------------------------ police response
  _dispatch(rec) {
    const g = this.game, pol = g.police;
    if (!this._policeFree() || rec.unit) return;
    const sp = P(rec.ped);
    let best = null, bd = 300 * 300;
    for (const v of pol.cars) {
      if (v.removed || v.isWrecked || v.npcJob || !v.driver || v.driver.brain !== 'cop' || v.driver.dead) continue;
      const d2 = dist2(v.pos.x, v.pos.z, sp.x, sp.z);
      if (d2 < bd) { bd = d2; best = v; }
    }
    const pp = this._pp();
    if (!best && dist2(sp.x, sp.z, pp.x, pp.z) < 220 * 220 && g.time - (this.spawnedT ?? -99) > 5) {
      best = pol.spawnCar(false, sp);
      this.spawnedT = g.time;
    }
    if (best) this._assign(rec, best);
  }

  _assign(rec, car) {
    car.npcJob = rec;
    rec.unit = { car, cops: car.occupants.filter((o) => o && o.brain === 'cop') };
    for (const c of rec.unit.cops) c.homeCar = car;
    car.ai = new SuspectDriver(this.game, car, rec);
    car.sirenOn = true;
    if (rec.phase === 'open') rec.phase = 'respond';
  }

  _release(rec) {
    const u = rec.unit;
    rec.unit = null; rec.park = false;
    if (rec.phase !== 'closed' && rec.phase !== 'chase') rec.phase = 'open';
    if (!u) return;
    for (const c of u.cops) {
      if (c.npcTask === rec) c.npcTask = null;
      if (c._npcAim) { [c.accuracy, c.damageMul] = c._npcAim; c._npcAim = null; }
      if (c.threat === rec.ped) c.threat = null;
    }
    const car = u.car;
    if (car.npcJob === rec) car.npcJob = null;
    if (!car.removed && car.ai instanceof SuspectDriver) { car.ai = new LaneDriver(this.game, car, null); car.sirenOn = false; }
  }

  _close(rec, why) {
    if (rec.phase === 'closed') return;
    const g = this.game, ped = rec.ped;
    this._release(rec);
    rec.phase = 'closed'; rec.why = why;
    this.cases.splice(this.cases.indexOf(rec), 1);
    if (ped.npcCase === rec) { ped.npcCase = null; ped.npcWanted = 0; ped.fleeSpeed = undefined; }
    if (rec.pedPersistent === false && !ped.removed) ped.persistent = false;
    if (ped.crimeTask?.kind !== 'escort') ped.crimeTask = null;
    const car = rec.car;
    if (car && !car.removed) {
      car.persistent = rec.carPersistent ?? false;
      // a getaway car still under its driver goes back to normal traffic (which also tidies it away later)
      if (car.driver === ped && !ped.dead && (car.ai instanceof FleeDriver || car.ai instanceof PullOverDriver)) car.ai = new LaneDriver(g, car, null);
      if (!car.policeUnit && !g.traffic.cars.includes(car) && !car.ownedByPlayer && g.player.vehicle !== car) { car.traffic = true; g.traffic.cars.push(car); }
    }
    if (rec.known && why !== 'gone') {
      const pp = this._pp(), sp = rec.pos;
      if (dist2(sp.x, sp.z, pp.x, pp.z) < 200 * 200) g.hud?.dispatch?.({ arrested: 'Suspect in custody', ticketed: 'Ticket issued', escaped: 'Suspect got away', dead: 'Suspect down' }[why] || null);
    }
  }

  // ------------------------------------------------------------------ per frame
  update(dt) {
    const g = this.game;
    if (!this.running) { for (const rec of [...this.cases]) this._close(rec, 'gone'); this.later.length = 0; return; }
    for (let i = this.later.length - 1; i >= 0; i--) if (g.time >= this.later[i][0]) { const fn = this.later[i][1]; this.later.splice(i, 1); fn(); }
    // people with something on who are now in a car (their own think stops once they're in a seat)
    for (const p of g.peds.list) {
      const t = p.crimeTask;
      if (!t || !p.vehicle) continue;
      if (t.kind === 'steal' && p.vehicle === t.car) this._stolen(p, t);
      else if (t.kind === 'rejoin' || t.kind === 'argue') { p.crimeTask = null; if (p.vehicle.driver === p) p.vehicle.ai?.resnap?.(); }
    }
    this._stage(dt);
    this.scanT -= dt;
    if (this.scanT <= 0) { this.scanT = 0.4; this._scan(); }
    for (const rec of [...this.cases]) this._updateCase(rec, dt);
  }

  // ongoing offences a patrol can catch in the act: jaywalking, speeding
  _scan() {
    const g = this.game;
    const pp = this._pp();
    for (const p of g.peds.list) {
      const t = p.crimeTask;
      if (!t || t.kind !== 'jaywalk' || p.dead || p.npcCase) continue;
      if (!g.map.isOnRoad(p.pos.x, p.pos.z)) continue;
      const unit = this._policeSees(p.pos.x, p.pos.y, p.pos.z, 36);
      if (unit) this.commit(p, 'jaywalk', { witness: unit });
      // drivers lean on the horn
      for (const v of g.traffic.cars) {
        if (v.removed || !v.driver || v.speedAbs < 2 || (v.hornT ?? 0) > g.time) continue;
        const [lx, lz] = v.worldToLocal(p.pos.x, p.pos.z);
        if (lz > 0 && lz < 16 && Math.abs(lx) < 3) { v.hornT = g.time + 6; g.audio?.playAt('horn', v.pos, 0.8); }
      }
    }
    for (const v of g.traffic.cars) {
      const ai = v.ai;
      if (!ai?.reckless || v.removed || v.isWrecked || !v.driver || v.driver.npcCase || v.driver.dead) continue;
      if (dist2(v.pos.x, v.pos.z, pp.x, pp.z) > 300 * 300) continue;
      const lane = ai.paths?.[0];
      const limit = (lane?.kind === 'lane' ? lane.speed : lane?.to?.speed) ?? 14;
      if (v.speedAbs < Math.max(13, limit * 1.15)) continue;
      const unit = this._policeSees(v.pos.x, v.pos.y, v.pos.z, 48);
      if (unit) this.commit(v.driver, 'speeding', { witness: unit, car: v });
    }
  }

  _updateCase(rec, dt) {
    const g = this.game, ped = rec.ped;
    if (ped.removed) { this._close(rec, 'gone'); return; }
    if (ped.dead) { this._close(rec, 'dead'); return; }
    const sp = P(ped), pp = this._pp();
    if (dist2(sp.x, sp.z, pp.x, pp.z) > 330 * 330) { this._close(rec, 'gone'); return; }
    if (!rec.known) {
      if (rec.callAt != null && g.time >= rec.callAt) this._know(rec);
      else if (rec.expire != null && g.time > rec.expire) { this._close(rec, 'gone'); return; }
      else if (Math.random() < dt * 2) { const u = this._policeSees(sp.x, sp.y, sp.z, 30); if (u) this._know(rec, u); }
      if (!rec.known) return;
    }
    ped.npcWanted = rec.stars;
    // in sight of any unit?
    if (Math.random() < dt * 3) {
      const u = this._policeSees(sp.x, sp.y, sp.z, 50);
      if (u || (rec.unit && rec.unit.cops.some((c) => !c.dead && !c.vehicle && dist2(c.pos.x, c.pos.z, sp.x, sp.z) < 30 * 30))) { rec.seenT = g.time; rec.pos.copy(sp); }
    }
    const lost = g.time - rec.seenT;
    if (lost > 22 + rec.stars * 6) { this._close(rec, 'escaped'); return; }
    if (!this._policeFree()) { if (rec.unit) this._release(rec); return; }
    // the unit: still a working police car with its crew?
    const u = rec.unit;
    if (u) {
      u.cops = u.cops.filter((c) => !c.removed && !c.dead);
      if (u.car.removed || u.car.isWrecked || !u.cops.length || (u.car.driver && u.car.driver.brain !== 'cop')) this._release(rec);
    }
    if (!rec.unit) { rec.dispatchT -= dt; if (rec.dispatchT <= 0) { rec.dispatchT = 2; this._dispatch(rec); } return; }
    this._direct(rec, dt);
  }

  // what happens between a unit and its suspect
  _direct(rec, dt) {
    const g = this.game, ped = rec.ped, car = rec.unit.car, cops = rec.unit.cops;
    const sp = P(ped);
    const dCar = Math.hypot(car.pos.x - sp.x, car.pos.z - sp.z);
    const sv = ped.vehicle;
    const C = CRIMES[rec.crime];
    // first contact (the cruiser pulls up, or its crew gets close on foot): comply, run, or (armed and desperate) fight
    let dCop = Infinity;
    for (const c of cops) if (!c.vehicle) dCop = Math.min(dCop, Math.hypot(c.pos.x - sp.x, c.pos.z - sp.z));
    if (rec.phase === 'respond' && (dCar < (sv ? 30 : 34) || dCop < 22)) {
      rec.phase = 'confront';
      // (only someone holding a gun takes on armed police)
      const armed = ped.weaponDef?.type === 'gun';
      const r = Math.random();
      rec.reaction = rec.force || (C.ticket ? (r < 0.78 ? 'comply' : 'flee') : armed && rec.stars >= 2 && r < 0.06 ? 'fight' : r < 0.42 ? 'comply' : 'flee');
      if (sv) {
        if (!rec.car) { rec.car = sv; rec.carPersistent ??= !!sv.persistent; sv.persistent = true; }
        if (rec.reaction === 'comply') { sv.ai = new PullOverDriver(sv); rec.phase = 'pulled'; }
        else this._carChase(rec, sv);
      } else if (rec.reaction === 'flee') { rec.phase = 'chase'; this.commit(ped, 'resisting'); ped.say(pick(SAY.flee)); }
      else if (rec.reaction === 'fight') { rec.phase = 'chase'; this.commit(ped, 'copAssault'); }
      else { ped.say(pick(SAY.surrender)); }
    }
    // a car chase ends when the getaway car can't go on: out they get, on foot
    if (rec.phase === 'chase' && sv) {
      // stopped, or pinned against the cruiser
      rec.stopT = sv.speedAbs < 1.2 || (sv.speedAbs < 4 && dCar < 8) ? (rec.stopT || 0) + dt : Math.max(0, (rec.stopT || 0) - dt);
      if ((sv.isWrecked || sv.health < sv.maxHealth * 0.45 || (rec.stopT > 2.5 && dCar < 30)) && !g.vehicles.isBusy(ped)) {
        g.vehicles.exit(ped);
        rec.reaction = Math.random() < 0.55 ? 'flee' : 'comply';
        if (rec.reaction === 'comply') { rec.phase = 'confront'; ped.say(pick(SAY.surrender)); } else ped.say(pick(SAY.flee));
      }
    }
    // suspect on foot and close: park up and let the crew out
    rec.park = !sv && dCar < 45;
    // crew out when the suspect is close on foot, or pulled over and stopped; back in for a car chase
    const out = rec.phase !== 'escort' && ((!sv && dCar < 40) || (rec.phase === 'pulled' && sv && sv.speedAbs < 1 && dCar < 20));
    if (out && car.speedAbs < 2) for (const c of cops) if (c.vehicle === car && !g.vehicles.isBusy(c)) { g.vehicles.exit(c); c.npcTask = rec; }
    if ((rec.phase === 'chase' && sv) || rec.phase === 'escort') {
      for (const c of cops) if (!c.vehicle && !g.vehicles.isBusy(c)) { const seat = [0, 1].find((s) => !car.occupants[s]); if (seat != null) g.vehicles.enter(c, car, seat, { force: true }); }
    }
    for (const c of cops) if (!c.vehicle) c.npcTask = rec;
    // booked: suspect in the back, crew in the front: drive off
    if (rec.phase === 'escort') {
      rec.escortT = (rec.escortT || 0) + dt;
      if (!ped.vehicle && !g.vehicles.isBusy(ped) && rec.escortT > 12) { const seat = [2, 3, 1].find((s) => s < (car.model?.seats?.length || 2) && !car.occupants[s]); if (seat != null) g.vehicles.seatNow(ped, car, seat); }
      if (ped.vehicle === car && cops.every((c) => c.vehicle === car)) { ped.crimeTask = { kind: 'escort' }; this._close(rec, 'arrested'); }
    }
  }

  _carChase(rec, sv) {
    rec.phase = 'chase';
    this.commit(rec.ped, 'evading');
    const car = rec.unit?.car;
    sv.ai = new FleeDriver(this.game, sv, () => (car && !car.removed ? car.pos : rec.scene));
    rec.car = sv;
    rec.carPersistent ??= !!sv.persistent;
    sv.persistent = true;
  }

  // ------------------------------------------------------------------ cops on a job (called from Police.copThink)
  copThink(cop, dt) {
    dt = this._dt(cop);
    const g = this.game, rec = cop.npcTask;
    if (!rec || rec.phase === 'closed' || !rec.unit || rec.unit.car !== cop.homeCar) { cop.npcTask = null; return false; }
    if (rec.phase === 'escort' || (rec.phase === 'chase' && rec.ped.vehicle)) return false; // back to the car
    const ped = rec.ped, sv = ped.vehicle;
    if (sv) {
      // walk up to the driver's window
      if (rec.phase !== 'pulled') return false;
      const door = sv.doorWorld();
      const d = Math.hypot(door.x - cop.pos.x, door.z - cop.pos.z);
      if (d > 1.6) { cop.goTo(door.x, door.z, 2.6, dt, 1.2); return true; }
      cop.stop(); cop.faceTowards(sv.pos.x, sv.pos.z, dt, 6); cop.animState.talking = true;
      rec.talkT = (rec.talkT || 0) + dt;
      if (!rec.said) { rec.said = true; cop.say(pick(SAY.ticket)); }
      if (rec.talkT > 4.5) {
        if (CRIMES[rec.crime].ticket) { ped.say(pick(SAY.ticketed)); if (sv.driver === ped) sv.ai = new LaneDriver(g, sv, null); this._close(rec, 'ticketed'); }
        else if (!g.vehicles.isBusy(ped)) { g.vehicles.exit(ped); rec.phase = 'confront'; rec.reaction = 'comply'; rec.talkT = 0; }
      }
      return true;
    }
    const sp = P(ped);
    const dx = sp.x - cop.pos.x, dz = sp.z - cop.pos.z, d = Math.hypot(dx, dz);
    if (rec.reaction === 'fight' && cop.weaponDef.type === 'gun' && d < 45) {
      // (the police hold back against the player when you're not wanted; not against an armed suspect)
      cop._npcAim ??= [cop.accuracy, cop.damageMul];
      cop.accuracy = Math.max(cop.accuracy, 0.6); cop.damageMul = Math.max(cop.damageMul, 1);
      cop.threat = ped; cop._attack(dt);
      return true;
    }
    cop.aiming = false;
    if ((cop.lineT ?? 0) < g.time && d < 26 && rec.phase === 'chase') { cop.lineT = g.time + rand(5, 9); cop.say(pick(SAY.stop)); }
    const running = ped.state === 'flee' || rec.reaction === 'flee';
    if (d > (running ? 1.4 : 1.5) || ped.ragdolling) {
      // (a runner gets chased flat out right up to the tackle; someone waiting gets walked up to)
      if (d > 5 || (running && !ped.ragdolling)) this._run(cop, sp.x, sp.z, running ? 7.3 : 6, dt); else cop.goTo(sp.x, sp.z, 2.8, dt, 1.3);
      return true;
    }
    cop.stop(); cop.faceTowards(sp.x, sp.z, dt, 10);
    if (running) {
      // tackle
      ped.knockDown(new THREE.Vector3(dx / (d || 1) * 3.5, 1.2, dz / (d || 1) * 3.5));
      rec.reaction = 'comply'; rec.phase = 'arrest'; rec.cuffT = -1.5;
      ped.setState('wander');
      return true;
    }
    if (CRIMES[rec.crime].ticket) {
      cop.animState.talking = true;
      rec.talkT = (rec.talkT || 0) + dt;
      if (!rec.said) { rec.said = true; cop.say(pick(SAY.ticket)); }
      if (rec.talkT > 4) { ped.say(pick(SAY.ticketed)); this._close(rec, 'ticketed'); }
      return true;
    }
    rec.phase = 'arrest';
    rec.cuffT = (rec.cuffT || 0) + dt;
    if (rec.cuffT > 2.5 && !ped.ragdolling) {
      rec.phase = 'escort';
      ped.say(pick(SAY.cuffed));
      const car = rec.unit.car, seat = [2, 3, 1].find((s) => s < (car.model?.seats?.length || 2) && !car.occupants[s]);
      if (seat != null) g.vehicles.enter(ped, car, seat, { force: true });
      else { ped.crimeTask = { kind: 'escort' }; this._close(rec, 'arrested'); ped.remove?.(); }
    }
    return true;
  }

  // ------------------------------------------------------------------ NPCs with something on (called from Ped.think)
  pedThink(ped, dt) {
    dt = this._dt(ped);
    const g = this.game, t = ped.crimeTask;
    if (t) switch (t.kind) {
      case 'jaywalk': {
        t.t = (t.t || 0) + dt;
        // caught in the act: carry on across until the police get here, then the case decides
        if (ped.npcCase && ped.npcCase.phase !== 'open' && ped.npcCase.phase !== 'respond') { ped.crimeTask = null; break; }
        if (t.t < 0.6) { ped.stop(); ped.faceTowards(t.x, t.z, dt, 6); return true; }
        if (ped.goTo(t.x, t.z, t.sp, dt, 0.8) || t.t > 25) {
          const nodes = g.map.walkNodes;
          let best = null, bd = Infinity;
          for (const i of t.b.nodeIds) { const n = nodes[i]; const d = dist2(n.x, n.z, ped.pos.x, ped.pos.z); if (d < bd) { bd = d; best = n; } }
          ped.node = best; ped.prevNode = null; ped.crimeTask = null; ped.setState('wander');
          return false;
        }
        return true;
      }
      case 'mug': return this._mugThink(ped, t, dt);
      case 'victim': {
        t.t += dt;
        const m = t.by;
        if (m.dead || m.removed || t.t > 6 || ped.ragdolling) { ped.crimeTask = null; ped.threat = m; ped.threatPos.copy(m.pos); ped.setState('flee'); return false; }
        ped.stop(); ped.animState.handsUp = true; ped.faceTowards(m.pos.x, m.pos.z, dt, 6);
        return true;
      }
      case 'steal': {
        t.t += dt;
        const car = t.car;
        if (car.removed || car.isWrecked || t.t > 14 || (car.driver && car.driver.isPlayer)) { ped.crimeTask = null; return false; }
        if (!g.vehicles.isBusy(ped) && t.t > 0.2 && !t.tried) { t.tried = true; if (!g.vehicles.enter(ped, car, 0)) ped.crimeTask = null; }
        return true;
      }
      case 'argue': return this._argueThink(ped, t, dt);
      case 'rejoin':
        if (!g.vehicles.isBusy(ped)) { ped.crimeTask = null; return false; }
        return true;
      case 'escort': return true;
    }
    const rec = ped.npcCase;
    if (!rec || !rec.known || !rec.unit || ped.vehicle) return false;
    const cop = this._nearestCop(rec, ped);
    // waiting on a ticket / hands up for the cuffs / being walked to the car
    if (rec.phase === 'escort') return true;
    if (rec.reaction === 'comply' && (rec.phase === 'confront' || rec.phase === 'arrest')) {
      ped.stop();
      if (cop) ped.faceTowards(cop.pos.x, cop.pos.z, dt, 5);
      if (CRIMES[rec.crime].ticket) ped.animState.talking = true; else ped.animState.handsUp = true;
      return true;
    }
    if (rec.reaction === 'flee' && rec.phase === 'chase' && cop) {
      if (ped.state !== 'flee') ped.setState('flee');
      ped.threat = cop; ped.stateTime = Math.min(ped.stateTime, 5);
      // flat out at first, then tiring
      rec.fleeAt ??= g.time;
      ped.fleeSpeed = clamp(6.3 - (g.time - rec.fleeAt) * 0.07, 5, 6.3);
      return false;
    }
    if (rec.reaction === 'fight' && cop) {
      if (ped.weaponDef?.type !== 'gun' && ped.weapons.pistol) ped.equip('pistol');
      ped.threat = cop;
      if (ped.state !== 'attack') ped.setState('attack');
      return false;
    }
    return false;
  }

  // run at a point, sliding round walls on the way (a straight line pinned a cop to a building mid-chase)
  _run(c, x, z, sp, dt) {
    let dx = x - c.pos.x, dz = z - c.pos.z;
    const l = Math.hypot(dx, dz) || 1;
    dx /= l; dz /= l;
    const pr = this.game.collision.resolveCircle(c.pos.x + dx * 1.6, c.pos.z + dz * 1.6, 0.4, c.pos.y + 0.3, 1.4);
    if (pr.hit) { dx += (pr.x - (c.pos.x + dx * 1.6)) * 2; dz += (pr.z - (c.pos.z + dz * 1.6)) * 2; const m = Math.hypot(dx, dz) || 1; dx /= m; dz /= m; }
    c.moveTarget.set(dx * sp, dz * sp);
    c.yaw = Math.atan2(dx, dz);
  }

  _nearestCop(rec, ped) {
    let best = null, bd = Infinity;
    for (const c of rec.unit?.cops || []) { if (c.dead) continue; const d = dist2(c.pos.x, c.pos.z, ped.pos.x, ped.pos.z); if (d < bd) { bd = d; best = c; } }
    return best;
  }

  // ------------------------------------------------------------------ jaywalking (asked by Ped._wander now and then)
  // straight across the nearest street, well away from the corner: that's what the crossings are for
  jaywalk(ped, force = false) {
    if (!this.running || ped.brain !== 'civilian' || ped.persistent || ped.npcCase || ped.crimeTask) return false;
    if (ped.jaywalker === undefined) ped.jaywalker = Math.random() < 0.2 * Math.min(2, this.level);
    if (!force && !ped.jaywalker) return false;
    const map = this.game.map, x = ped.pos.x, z = ped.pos.z;
    if (!map.blockAt(x, z)) return false;
    const i = map.nearestX(x), j = map.nearestZ(z);
    const dx = Math.abs(x - XS[i]), dz = Math.abs(z - ZS[j]);
    let tx, tz;
    if (dx < dz) {
      if (dx > HALF_ROAD + 6 || dz < 24 || !map.isOnCityStreet(XS[i], z)) return false;
      tx = 2 * XS[i] - x + rand(-0.8, 0.8); tz = z + rand(-6, 6);
    } else {
      if (dz > HALF_ROAD + 6 || dx < 24 || !map.isOnCityStreet(x, ZS[j])) return false;
      tz = 2 * ZS[j] - z + rand(-0.8, 0.8); tx = x + rand(-6, 6);
    }
    const b = map.walkAreaAt(tx, tz);
    if (!b?.nodeIds?.length || map.isOnRoad(tx, tz)) return false;
    ped.crimeTask = { kind: 'jaywalk', x: tx, z: tz, b, sp: Math.random() < 0.35 ? 3.3 : 1.8, t: 0 };
    return true;
  }

  // ------------------------------------------------------------------ staged crimes
  _stage(dt) {
    const g = this.game;
    if (g.missions?.active) return; // (a mission's set pieces come first)
    const lvl = this.level, pp = this._pp();
    const night = g.env.night > 0.5 ? 1.7 : 1;
    const rough = ROUGH[g.map.districtAt(pp.x, pp.z)] ?? 0.7;
    const k = lvl * night * rough;
    // a few drivers in a hurry
    this.recklessT -= dt;
    if (this.recklessT <= 0) {
      this.recklessT = 6;
      const want = Math.round((lvl > 1 ? 3 : 1) * Math.min(1.5, night));
      let n = 0;
      const cands = [];
      for (const v of g.traffic.cars) {
        if (v.removed || !(dist2(v.pos.x, v.pos.z, pp.x, pp.z) < 250 * 250)) continue;
        if (v.ai?.reckless) n++;
        else if (this._driver(v) && v.ai.constructor === LaneDriver && v.type !== 'taxi' && !v.def.bike && dist2(v.pos.x, v.pos.z, pp.x, pp.z) > 50 * 50) cands.push(v);
      }
      if (n < want && cands.length) this.makeReckless(pick(cands));
    }
    this.mugT -= dt * k;
    if (this.mugT <= 0) { this.mugT = rand(60, 120); this.stage('mug'); }
    this.theftT -= dt * k;
    if (this.theftT <= 0) { this.theftT = rand(80, 150); this.stage('steal'); }
  }

  makeReckless(v) {
    const ai = v.ai;
    ai.reckless = true;
    ai.cruiseFactor = rand(1.35, 1.65);
    ai.ignoreLights = Math.random() < 0.65;
  }

  // set up a crime near the player (the director, and the admin 'crime now' command)
  stage(kind) {
    const g = this.game, pp = this._pp();
    const peds = g.peds.list.filter((p) => this._civ(p) && !p.vehicle && !p.walkedDog && (p.state === 'wander' || p.state === 'idle'));
    const within = (p, r0, r1) => { const d2 = dist2(p.pos.x, p.pos.z, pp.x, pp.z); return d2 > r0 * r0 && d2 < r1 * r1; };
    if (kind === 'mug') {
      for (const m of peds.filter((p) => within(p, 18, 75)).sort(() => Math.random() - 0.5)) {
        const v = peds.find((p) => p !== m && dist2(p.pos.x, p.pos.z, m.pos.x, m.pos.z) < 30 * 30 && dist2(p.pos.x, p.pos.z, m.pos.x, m.pos.z) > 3 * 3);
        if (!v) continue;
        const w = Math.random() < 0.55 ? 'knife' : 'pistol';
        m.giveWeapon(w, 24);
        m.crimeTask = { kind: 'mug', victim: v, w, t: 0, stage: 'approach' };
        m.talkPartner = null; m.setState('wander');
        return true;
      }
      return false;
    }
    if (kind === 'steal') {
      for (const th of peds.filter((p) => within(p, 20, 90)).sort(() => Math.random() - 0.5)) {
        let best = null, bd = 32 * 32;
        for (const v of g.vehicles.list) {
          if (v.removed || v.isWrecked || v.remote || v.persistent || v.def.police || v.def.aircraft || v.def.train || v.def.boat || v.policeUnit || v.ownedByPlayer || v.locked) continue;
          if (v.driver && (v.driver.isPlayer || v.driver.brain !== 'civilian' || v.speedAbs > 0.8)) continue;
          if (v.occupants.some((o) => o && (o.isPlayer || o.remote))) continue;
          const d2 = dist2(v.pos.x, v.pos.z, th.pos.x, th.pos.z);
          if (d2 < bd) { bd = d2; best = v; }
        }
        if (!best) continue;
        th.crimeTask = { kind: 'steal', car: best, t: 0, occupied: !!best.driver, from: th.pos.clone() };
        th.talkPartner = null; th.setState('wander');
        return true;
      }
      return false;
    }
    if (kind === 'speed') {
      const cands = g.traffic.cars.filter((v) => this._driver(v) && v.ai.constructor === LaneDriver && !v.ai.reckless && within(v, 30, 200));
      if (!cands.length) return false;
      this.makeReckless(pick(cands));
      return true;
    }
    if (kind === 'jaywalk') {
      for (const p of peds.filter((q) => within(q, 8, 60))) if (this.jaywalk(p, true)) return true;
      return false;
    }
    return false;
  }

  // the thief is behind the wheel: off they go
  _stolen(ped, t) {
    const g = this.game, car = t.car;
    ped.crimeTask = null;
    car.ai = new FleeDriver(g, car, () => t.from);
    car.parked = false;
    if (!g.traffic.cars.includes(car)) { car.traffic = true; g.traffic.cars.push(car); }
    // (a jacked driver already shouts about it; a parked car's owner is nowhere to be seen)
    this.commit(ped, 'gta', { car, call: t.occupied ? 0.95 : 0.55, delay: [3, 7] });
  }

  _mugThink(m, t, dt) {
    const g = this.game, v = t.victim;
    t.t += dt;
    if (m.npcCase || v.dead || v.removed || v.vehicle || t.t > 40) {
      m.crimeTask = null;
      if (v.crimeTask?.kind === 'victim') v.crimeTask = null;
      return false;
    }
    const dx = v.pos.x - m.pos.x, dz = v.pos.z - m.pos.z, d = Math.hypot(dx, dz);
    if (t.stage === 'approach') {
      if (d > 2.2) { m.goTo(v.pos.x, v.pos.z, d > 12 ? 2.6 : 3.6, dt, 1.8); return true; }
      t.stage = 'rob'; t.rt = 0;
      m.equip(t.w);
      m.say(pick(SAY.mug));
      v.crimeTask = { kind: 'victim', by: m, t: 0 };
      if (v.state === 'idle') v.talkPartner = null;
      v.setState('wander');
    }
    if (t.stage === 'rob') {
      t.rt += dt;
      m.stop(); m.faceTowards(v.pos.x, v.pos.z, dt, 10);
      if (m.weaponDef.type === 'gun') { m.aiming = true; m.aimPitch = 0; }
      if (t.rt > 2.6) {
        m.loot = randInt(40, 260);
        m.aiming = false;
        m.crimeTask = null;
        m.threat = v; m.threatPos.copy(v.pos); m.setState('flee');
        if (v.crimeTask?.kind === 'victim') v.crimeTask = null;
        v.threat = m; v.threatPos.copy(m.pos); v.setState('flee');
        v.say(pick(SAY.mugged));
        this.commit(m, 'mugging', { call: 1, delay: [2, 5] });
        return false;
      }
    }
    return true;
  }

  _argueThink(ped, t, dt) {
    const g = this.game;
    t.t += dt;
    if (g.vehicles.isBusy(ped)) return true;
    const own = t.car, other = t.other;
    const done = t.t > t.dur || !other || other.removed || (t.foe && (t.foe.dead || t.foe.ragdolling || t.foe.removed));
    if (done || ped.npcCase) {
      // back in the car and on your way (or leave it, if it's wrecked)
      if (ped.state === 'attack') ped.setState('wander');
      ped.threat = null;
      if (!ped.npcCase && own && !own.removed && !own.isWrecked && !own.driver && dist2(own.pos.x, own.pos.z, ped.pos.x, ped.pos.z) < 30 * 30) {
        if (g.vehicles.enter(ped, own, 0)) ped.crimeTask = { kind: 'rejoin', car: own, t: 0 };
        else ped.crimeTask = null;
        return true;
      }
      ped.crimeTask = null;
      return false;
    }
    if (t.foe) {
      // fists out
      ped.threat = t.foe;
      if (ped.state !== 'attack') ped.setState('attack');
      return false;
    }
    const door = other.doorWorld ? other.doorWorld() : other.pos;
    const d = Math.hypot(door.x - ped.pos.x, door.z - ped.pos.z);
    if (d > 1.8) { ped.goTo(door.x, door.z, 2.4, dt, 1.4); return true; }
    ped.stop(); ped.faceTowards(other.pos.x, other.pos.z, dt, 6); ped.animState.talking = true;
    if ((t.sayT ?? 0) <= t.t) { t.sayT = t.t + 2.6; ped.say(pick(SAY.rage)); const od = other.driver; if (od && !od.isPlayer && Math.random() < 0.5) this._after(0.9, () => { if (!od.removed) od.say(pick(SAY.sorry)); }); }
    if (t.fight && t.t > 4 && !t.foe) {
      const od = other.driver;
      if (od && !od.isPlayer && od.brain === 'civilian' && !g.vehicles.isBusy(od)) {
        g.vehicles.exit(od);
        od.crimeTask = { kind: 'argue', car: other, other: own, t: 0, dur: t.dur - t.t + 2, foe: ped };
        t.foe = od;
      } else t.fight = false;
    }
    return true;
  }

  // ------------------------------------------------------------------ events
  _onCrash(A, B, impact) {
    const g = this.game;
    if (!this.running || impact < 4.5) return;
    const da = this._driver(A), db = this._driver(B);
    if (!da && !db) return;
    if ((A.crashT ?? -99) > g.time - 12 || (B.crashT ?? -99) > g.time - 12) return;
    A.crashT = B.crashT = g.time;
    // who ran into whom: the one heading at the other harder (a reckless driver or a getaway car, always)
    const into = (X, Y) => { const dx = Y.pos.x - X.pos.x, dz = Y.pos.z - X.pos.z, l = Math.hypot(dx, dz) || 1; return (X.vel.x * dx + X.vel.z * dz) / l; };
    let bad = into(A, B) >= into(B, A) ? A : B;
    if (A.ai?.reckless || A.driver?.npcCase) bad = A; else if (B.ai?.reckless || B.driver?.npcCase) bad = B;
    const good = bad === A ? B : A;
    const bd = this._driver(bad), gd = this._driver(good);
    const pp = this._pp();
    if (dist2(bad.pos.x, bad.pos.z, pp.x, pp.z) > 200 * 200) return;
    const r = Math.random();
    if (bd && (r < (bad.ai?.reckless ? 0.7 : 0.35))) {
      // hit and run
      bad.ai = new FleeDriver(g, bad, () => good.pos);
      this.commit(bd, 'hitrun', { car: bad, call: 0.6, delay: [5, 10] });
      if (gd) this._after(0.6, () => { if (!gd.removed && !gd.dead) gd.say(pick(SAY.rage)); });
      return;
    }
    if (gd && r < 0.8 && good.speedAbs < 6 && !good.def.bike) {
      // road rage: the other driver gets out and has words (and sometimes more)
      this._after(0.9, () => {
        if (gd.removed || gd.dead || gd.vehicle !== good || g.vehicles.isBusy(gd) || gd.crimeTask || gd.npcCase) return;
        g.vehicles.exit(gd);
        gd.crimeTask = { kind: 'argue', car: good, other: bad, t: 0, dur: rand(8, 13), fight: Math.random() < 0.4 };
      });
    }
  }

  _onPedHit(c, v, spd) {
    const g = this.game;
    const d = this._driver(v);
    if (!this.running || !d || spd < 5 || c.remote || (v.pedHitT ?? -99) > g.time - 8) return;
    v.pedHitT = g.time;
    if (Math.random() < 0.6 || v.ai?.reckless) {
      v.ai = new FleeDriver(g, v, () => c.pos);
      this.commit(d, 'hitped', { car: v, call: 0.85, delay: [3, 6] });
    } else this._after(0.5, () => { if (!d.removed && !d.dead) d.say(pick(SAY.sorry)); });
  }

  _onViolence(att, vic, kill = false) {
    if (!att || att.isPlayer || att.remote || att.brain !== 'civilian' || att.dead) return;
    if (vic?.brain === 'cop') { this.commit(att, kill ? 'copKill' : 'copAssault', { call: 1, delay: [1, 2] }); return; }
    if (vic && !vic.isPlayer && (att.crimeTask?.kind === 'argue' || att.npcCase || att.state === 'attack')) this.commit(att, 'assault', { call: 0.5, delay: [4, 8] });
  }

  // ------------------------------------------------------------------ for the HUD / radar
  // [{c, n, hot}] everyone with stars showing near the camera (our suspects and other players')
  tagged() {
    const out = [];
    for (const rec of this.cases) if (rec.known && !rec.ped.removed && rec.phase !== 'closed') out.push({ c: rec.ped, n: rec.stars, hot: !!rec.unit });
    const npc = this.game.net?.npc;
    if (npc) for (const q of npc.pedList) if (q.npcWanted > 0 && !q.removed) out.push({ c: q, n: q.npcWanted, hot: true });
    return out;
  }
}

export { SuspectDriver, FleeDriver, CRIMES };
