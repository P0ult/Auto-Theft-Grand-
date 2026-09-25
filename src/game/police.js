// Police: wanted level & heat, witnesses, patrols, pursuit drivers, cops on foot, arrests,
// police helicopter with searchlight.
import * as THREE from 'three';
import { randomAppearance } from '../entities/humanoid.js';
import { LaneDriver, Traffic } from './traffic.js';
import { std } from '../render/materials.js';
import { RNG, rand, randInt, pick, clamp, dist2, wrapAngle } from '../core/utils.js';

const THRESH = [0, 1, 3, 7, 14, 24];
const COP_LINES = ['Freeze!', 'Police! Hands where I can see them!', 'Stop right there!', 'Suspect on foot!', 'Get down on the ground!', 'Drop it!'];

export function copAppearance(swat = false) {
  const rng = new RNG((Math.random() * 1e9) | 0);
  return randomAppearance(rng, {
    female: rng.chance(0.2), shirtType: 'long', hairStyle: 'buzz', glasses: rng.chance(0.3), shorts: false, bandana: null,
    uniform: swat ? { shirt: 0x1b1b1f, pants: 0x1b1b1f, hat: 0x111111 } : { shirt: 0x1f2f55, pants: 0x1a2440, hat: 0x141d33 },
    shoes: 0x0a0a0a,
  });
}

// Pursuit driver: routes along the road network toward the target, then drives directly & rams.
class PursuitDriver extends LaneDriver {
  constructor(game, veh, start, police) {
    super(game, veh, false);
    this.police = police;
    this.fixedCruise = true;
    this.cruise = 30;
    this.ignoreLights = true;
    this.direct = false;
    this.reverseT = 0;
    if (start) this._start(start); else this.resnap();
  }
  _chooseNext(cur) {
    const opts = this._options(cur);
    if (!opts.length) return null;
    const t = this.police ? this.police.targetPos() : this.game.player.pos;
    let best = opts[0], bd = Infinity;
    for (const o of opts) {
      const far = this.net.nodes[o.dir === 0 ? o.e.b : o.e.a];
      const d = Math.hypot(far.x - t.x, far.z - t.z) + Math.random() * 20;
      if (d < bd) { bd = d; best = o; }
    }
    return best;
  }
  update(dt) {
    const v = this.veh;
    if (!v.driver || v.isWrecked) return;
    const tp = this.police.targetPos();
    const d = Math.hypot(tp.x - v.pos.x, tp.z - v.pos.z);
    const pl = this.game.player;
    const see = d < 80;
    this.direct = see || !this.game.map.isOnRoad(v.pos.x, v.pos.z);
    if (!this.direct) {
      this.cruise = 26 + this.police.level * 2;
      if (this._wasDirect) { this._wasDirect = false; this.resnap(); }
      super.update(dt); return;
    }
    this._wasDirect = true;
    const inp = v.input;
    // direct chase: aim at predicted position
    const tv = pl.vehicle ? pl.vehicle.vel : pl.vel;
    const lead = clamp(d / 30, 0, 1.2);
    const tx = tp.x + tv.x * lead, tz = tp.z + tv.z * lead;
    let [lx, lz] = v.worldToLocal(tx, tz);
    // obstacle feelers
    const fx = Math.sin(v.yaw), fz = Math.cos(v.yaw);
    const col = this.game.collision;
    const probe = (ang) => {
      const a = v.yaw + ang;
      const h = col.raycast(v.pos.x, v.pos.y + 0.8, v.pos.z, Math.sin(a), 0, Math.cos(a), 14, { ignoreProps: true });
      return h ? h.t : 14;
    };
    const fL = probe(0.35), fR = probe(-0.35), fC = probe(0);
    let steer = Math.atan2(lx, Math.max(0.5, lz)) / (v.def.steer * 0.7);
    if (fC < 9) steer += fL > fR ? 1.2 : -1.2;
    else if (fL < 6) steer -= 0.6; else if (fR < 6) steer += 0.6;
    inp.steer = clamp(steer, -1, 1);
    const onFoot = !pl.vehicle;
    let want = onFoot ? (d > 20 ? 20 : clamp(d - 10, 0, 10)) : 34 + this.police.level * 2;
    if (Math.abs(Math.atan2(lx, lz)) > 1.4 && d < 30) want = Math.min(want, 9);
    const speed = v.speed;
    // reversing out of stuck spots
    if (this.reverseT > 0) {
      this.reverseT -= dt;
      inp.throttle = 0; inp.brake = 1; inp.steer = -inp.steer; inp.handbrake = false;
      return;
    }
    if (Math.abs(speed) < 1 && want > 5) { this.stuck += dt; if (this.stuck > 1.5) { this.reverseT = 1.2; this.stuck = 0; } } else this.stuck = 0;
    const err = want - speed;
    inp.throttle = err > 0 ? clamp(err * 0.3, 0.2, 1) : 0;
    inp.brake = err < -2 ? clamp(-err * 0.2, 0.2, 1) : 0;
    inp.handbrake = Math.abs(Math.atan2(lx, lz)) > 1.2 && speed > 10;
  }
}

export class Police {
  constructor(game) {
    this.game = game;
    this.heat = 0;
    this.level = 0;
    this.lastSeen = 0;
    this.seen = false;
    this.lastKnown = new THREE.Vector3();
    this.cars = [];
    this.cops = [];
    this.spawnTimer = 0;
    this.arrestTimer = 0;
    this.heli = null;
    this.patrolTimer = 10;
    this.flash = false;
    this.lineTimer = 0;
    this.enabled = true;
    const ev = game.events;
    ev.on('kill', (killer, victim, weapon) => {
      if (!killer?.isPlayer) return;
      if (victim.brain === 'cop') this.crime(3, victim.pos, true);
      else if (victim.gang) this.crime(0.8, victim.pos, false);
      else this.crime(1.4, victim.pos, false);
    });
    ev.on('gunshot', (shooter, pos) => { if (shooter?.isPlayer) this.crime(0.18, pos, false, true); });
    ev.on('melee', (att, vic) => { if (att?.isPlayer) this.crime(vic.brain === 'cop' ? 2 : 0.35, vic.pos, vic.brain === 'cop'); });
    ev.on('pedHitByCar', (c, v, spd) => { if (v.driver?.isPlayer) this.crime(c.brain === 'cop' ? 2 : 0.5, c.pos, c.brain === 'cop'); });
    ev.on('carjack', (by, victim, veh) => { if (by.isPlayer) this.crime(veh.def.police ? 2 : 0.6, veh.pos, false); });
    ev.on('explosion', (pos, r, src) => { if (src?.isPlayer) this.crime(1.2, pos, false); });
    ev.on('vehicleShot', (v, s) => { if (s?.isPlayer && v.def.police) this.crime(1.5, v.pos, true); });
    ev.on('enteredVehicle', (c, v) => { if (c.isPlayer && v.def.police && this.level < 1) this.crime(1, v.pos, true); });
    ev.on('carCrash', (A, B, impact) => { const pv = game.player.vehicle; if (pv && (A === pv || B === pv) && (A.def.police || B.def.police) && impact > 5) this.crime(0.8, pv.pos, true); });
  }

  targetPos() {
    const p = this.game.player;
    if (this.seen || this.level === 0) return p.vehicle ? p.vehicle.pos : p.pos;
    return this.lastKnown;
  }

  // witnessed: forces at least one star
  crime(amount, pos, severe, noise = false) {
    if (!this.enabled || this.game.player.dead) return;
    const nearCop = this.cops.some((c) => !c.dead && dist2(c.pos.x, c.pos.z, pos.x, pos.z) < (noise ? 45 : 35) ** 2);
    const witnessK = nearCop ? 1 : noise ? 0.35 : 0.6;
    this.heat += amount * witnessK * (severe ? 1.5 : 1);
    if ((nearCop || severe) && this.level === 0 && amount >= 0.3) this.heat = Math.max(this.heat, THRESH[1]);
    if (!noise || nearCop) { this.lastSeen = this.game.time; this.lastKnown.copy(this.game.player.pos); }
    this._updateLevel();
  }

  setLevel(l) {
    this.level = clamp(l, 0, 5);
    this.heat = THRESH[this.level];
    if (this.level > 0) { this.lastSeen = this.game.time; this.lastKnown.copy(this.game.player.pos); }
  }

  clear() {
    this.level = 0; this.heat = 0;
    for (const c of this.cars) if (c.ai instanceof PursuitDriver) c.sirenOn = false;
    this.game.events.emit('wantedCleared');
  }

  _updateLevel() {
    let l = 0;
    for (let i = 1; i <= 5; i++) if (this.heat >= THRESH[i]) l = i;
    if (this.game.missions?.maxWanted != null) l = Math.min(l, this.game.missions.maxWanted);
    if (l > this.level) { this.level = l; this.game.events.emit('wantedUp', l); }
  }

  spawnCop(x, z, opts = {}) {
    const swat = this.level >= 4 && Math.random() < 0.6;
    const cop = this.game.peds.spawnPed(x, z, { appearance: copAppearance(swat), brain: 'cop', team: 'police', health: swat ? 150 : 100, armor: swat ? 50 : 0, persistent: true, ...opts });
    const w = this.level >= 4 ? pick(['rifle', 'smg', 'shotgun']) : this.level >= 3 ? pick(['pistol', 'shotgun', 'smg']) : 'pistol';
    cop.giveWeapon(w, 999); cop.equip(w);
    cop.accuracy = 0.35 + this.level * 0.08;
    cop.damageMul = 0.45 + this.level * 0.05;
    this.cops.push(cop);
    return cop;
  }

  spawnCar(pursuit = true) {
    const game = this.game;
    const p = this.targetPos();
    for (let tries = 0; tries < 14; tries++) {
      const smp = Traffic.sampleLane(game, p.x, p.z, 75, 230);
      if (!smp) continue;
      const { x, z } = smp;
      const d2 = dist2(x, z, p.x, p.z);
      if (d2 < 70 * 70 || d2 > 240 * 240) continue;
      if (game.peds._inView(x, z) && d2 < 120 * 120 && tries < 10) continue;
      let blocked = false;
      for (const o of game.vehicles.list) if (dist2(o.pos.x, o.pos.z, x, z) < 100 && Math.abs(o.pos.y - smp.y) < 4) { blocked = true; break; }
      if (blocked) continue;
      const net = game.map.roads;
      const pts = net.lanePath(smp.start.e, smp.start.dir, smp.start.lane);
      const k = Math.min(pts.length - 2, Math.max(0, Math.floor(smp.s0 / 5)));
      const yaw = Math.atan2(pts[k + 1][0] - pts[k][0], pts[k + 1][2] - pts[k][2]);
      const v = game.vehicles.spawn('police', x, z, yaw, { persistent: true });
      const n = pursuit && this.level >= 2 ? 2 : 1;
      for (let q = 0; q < n; q++) { const cop = this.spawnCop(x, z); v.putIn(cop, q); cop.homeCar = v; }
      v.ai = pursuit ? new PursuitDriver(game, v, smp.start, this) : new LaneDriver(game, v, smp.start);
      v.sirenOn = pursuit;
      v.policeUnit = true;
      v.vel.set(Math.sin(yaw) * 12, 0, Math.cos(yaw) * 12);
      this.cars.push(v);
      return v;
    }
    return null;
  }

  // ---------------------------------------------------------------- cop brain (called by Ped.think)
  copThink(cop, dt) {
    const game = this.game;
    const pl = game.player;
    cop.animState.cower = false; cop.animState.handsUp = false;
    cop.aiming = false;
    if (this.level === 0 || pl.dead) {
      // no wanted: return to car or patrol
      if (cop.homeCar && !cop.homeCar.isWrecked && !cop.vehicle && !game.vehicles.isBusy(cop)) {
        const seat = cop.homeCar.occupants.findIndex((o) => !o);
        if (seat >= 0 && seat < 2) game.vehicles.enter(cop, cop.homeCar, seat, { force: true });
        else cop.stop();
      } else if (!cop.vehicle) { cop.brain = 'civilian'; cop.state = 'wander'; cop.persistent = false; cop.node = null; }
      return;
    }
    const tp = pl.vehicle ? pl.vehicle.pos : pl.pos;
    const d = Math.hypot(tp.x - cop.pos.x, tp.z - cop.pos.z);
    // player drove off: get back in the car
    if (pl.vehicle && d > 35 && cop.homeCar && !cop.homeCar.isWrecked && !game.vehicles.isBusy(cop)) {
      const hd = Math.hypot(cop.homeCar.pos.x - cop.pos.x, cop.homeCar.pos.z - cop.pos.z);
      if (hd < 40) { const seat = cop.homeCar.occupants.findIndex((o) => !o); if (seat >= 0) { game.vehicles.enter(cop, cop.homeCar, seat, { force: true }); return; } }
    }
    const lethal = this.level >= 2 || cop.threat === pl;
    const hasLOS = d < 60 && game.collision.lineOfSight(cop.pos.x, cop.pos.y + 1.6, cop.pos.z, tp.x, tp.y + 1.2, tp.z);
    if (hasLOS) { this.seen = true; this.lastSeen = game.time; this.lastKnown.copy(tp); }
    if (cop.lineT === undefined) cop.lineT = rand(0, 3);
    cop.lineT -= dt;
    if (cop.lineT <= 0 && hasLOS && d < 30) { cop.lineT = rand(6, 12); cop.say(pick(COP_LINES)); }
    if (lethal && hasLOS && d < 45 && cop.weaponDef.type === 'gun') {
      cop.threat = pl;
      cop._attack(dt);
      return;
    }
    // chase to arrest
    const target = hasLOS ? tp : this.lastKnown;
    if (d > 1.1) {
      cop.goTo(target.x, target.z, d > 6 ? 5.8 : 3, dt, 1.0);
    } else {
      cop.stop();
      cop.faceTowards(tp.x, tp.z, dt, 10);
    }
  }

  // ---------------------------------------------------------------- update
  update(dt) {
    const game = this.game;
    const pl = game.player;
    this.cops = this.cops.filter((c) => !c.removed);
    this.cars = this.cars.filter((c) => !c.removed);
    // visibility check & evasion
    if (this.level > 0) {
      const tp = pl.vehicle ? pl.vehicle.pos : pl.pos;
      let seenNow = false;
      for (const c of this.cops) {
        if (c.dead) continue;
        const cp = c.vehicle ? c.vehicle.pos : c.pos;
        const d2 = dist2(cp.x, cp.z, tp.x, tp.z);
        if (d2 < 50 * 50 && (d2 < 12 * 12 || game.collision.lineOfSight(cp.x, cp.y + 1.5, cp.z, tp.x, tp.y + 1.2, tp.z))) { seenNow = true; break; }
      }
      if (this.heli && !this.heli.down && dist2(this.heli.pos.x, this.heli.pos.z, tp.x, tp.z) < 70 * 70) seenNow = true;
      this.seen = seenNow;
      if (seenNow) { this.lastSeen = game.time; this.lastKnown.copy(tp); }
      const evadeTime = 10 + this.level * 5;
      const unseen = game.time - this.lastSeen;
      this.flash = unseen > 2;
      if (unseen > evadeTime) { this.clear(); }
      this.heat = Math.max(THRESH[this.level], this.heat - dt * 0.01);
    } else { this.flash = false; this.heat = Math.max(0, this.heat - dt * 0.05); }

    // spawning response units
    const wantCars = [0, 1, 2, 3, 4, 5][this.level];
    const pursuitCars = this.cars.filter((c) => c.ai instanceof PursuitDriver && !c.isWrecked && c.driver && !c.driver.dead);
    this.spawnTimer -= dt;
    if (this.level > 0 && pursuitCars.length < wantCars && this.spawnTimer <= 0) { this.spawnTimer = 4; this.spawnCar(true); }
    // foot cops at low levels nearby if none
    if (this.level >= 1 && this.cops.filter((c) => !c.dead && !c.vehicle).length < 1 && !pl.vehicle && this.spawnTimer <= 0 && Math.random() < 0.02) {
      const a = Math.random() * 6.28;
      const x = pl.pos.x + Math.cos(a) * 45, z = pl.pos.z + Math.sin(a) * 45;
      if (game.map.blockAt(x, z) && !game.peds._inView(x, z)) this.spawnCop(x, z);
    }
    // patrol cars when not wanted
    this.patrolTimer -= dt;
    if (this.level === 0 && this.patrolTimer <= 0) {
      this.patrolTimer = 20;
      if (this.cars.filter((c) => !c.isWrecked).length < 2 && Math.random() < 0.6) this.spawnCar(false);
    }
    // update drivers & convert patrols into pursuers when wanted
    for (const v of this.cars) {
      if (!v.driver || v.driver.dead || v.driver.isPlayer) { v.sirenOn = false; continue; }
      if (this.level > 0 && !(v.ai instanceof PursuitDriver)) {
        v.ai = new PursuitDriver(game, v, null, this);
        v.sirenOn = true;
      }
      if (this.level === 0 && v.ai instanceof PursuitDriver) { v.ai = new LaneDriver(game, v, null); v.sirenOn = false; }
      v.ai?.update(dt);
      // cops bail out when the player is on foot nearby, or the car is stuck close by
      const tp = pl.vehicle ? pl.vehicle.pos : pl.pos;
      const d = Math.hypot(tp.x - v.pos.x, tp.z - v.pos.z);
      if (this.level > 0 && ((!pl.vehicle && d < 22) || (pl.vehicle && d < 12 && pl.vehicle.speedAbs < 3)) && v.speedAbs < 4) {
        for (const o of [...v.occupants]) if (o && !game.vehicles.isBusy(o)) game.vehicles.exit(o);
      }
    }
    // despawn far units
    const pp = pl.vehicle ? pl.vehicle.pos : pl.pos;
    for (const v of this.cars) {
      const d2 = dist2(v.pos.x, v.pos.z, pp.x, pp.z);
      if (d2 > 260 * 260 || (v.isWrecked && d2 > 120 * 120)) {
        for (const o of v.occupants) if (o && !o.isPlayer) game.peds.remove(o);
        v.occupants.fill(null);
        game.vehicles.remove(v);
      }
    }
    for (const c of this.cops) {
      if (c.vehicle) continue;
      const d2 = dist2(c.pos.x, c.pos.z, pp.x, pp.z);
      if (d2 > 200 * 200 || (c.dead && d2 > 60 * 60)) game.peds.remove(c);
    }
    // arrest
    if (this.level > 0 && !pl.dead && !game.missions?.noBust) {
      let close = false;
      for (const c of this.cops) {
        if (c.dead || c.vehicle || c.ragdolling) continue;
        const d = Math.hypot(c.pos.x - pl.pos.x, c.pos.z - pl.pos.z);
        if (!pl.vehicle && d < 1.5 && Math.hypot(pl.vel.x, pl.vel.z) < 3.5) close = true;
        if (pl.vehicle && pl.vehicle.speedAbs < 1.5) { const dp = pl.vehicle.doorWorld(); if (Math.hypot(c.pos.x - dp.x, c.pos.z - dp.z) < 1.6) close = true; }
      }
      this.arrestTimer = close ? this.arrestTimer + dt : Math.max(0, this.arrestTimer - dt * 2);
      if (this.arrestTimer > (pl.vehicle ? 2.2 : 1.4)) { this.arrestTimer = 0; game.events.emit('busted'); }
    }
    // helicopter
    if (this.level >= 3 && !this.heli && !pl.dead) this.heli = new Helicopter(game, this);
    if (this.heli) {
      this.heli.update(dt);
      if (this.heli.done) this.heli = null;
    }
  }

  reset() {
    this.clear();
    for (const v of this.cars) { for (const o of v.occupants) if (o && !o.isPlayer) this.game.peds.remove(o); v.occupants.fill(null); this.game.vehicles.remove(v); }
    for (const c of this.cops) this.game.peds.remove(c);
    this.cars = []; this.cops = [];
    if (this.heli) { this.heli.remove(); this.heli = null; }
  }
}

// ---------------------------------------------------------------------------------- helicopter
class Helicopter {
  constructor(game, police) {
    this.game = game;
    this.police = police;
    this.health = 700;
    this.down = false;
    this.done = false;
    const g = new THREE.Group();
    const body = std({ color: 0x1b2a4a, roughness: 0.4, metalness: 0.5 }, { key: 'heli' });
    const white = std({ color: 0xeeeeee, roughness: 0.4, metalness: 0.3 }, { key: 'heli' });
    const glass = std({ color: 0x111820, roughness: 0.05, metalness: 0.6 }, { key: 'heli' });
    const m = (geo, mat, x, y, z, rx = 0, ry = 0, rz = 0) => { const mesh = new THREE.Mesh(geo, mat); mesh.position.set(x, y, z); mesh.rotation.set(rx, ry, rz); mesh.castShadow = true; g.add(mesh); return mesh; };
    m(new THREE.SphereGeometry(1.4, 16, 12), body, 0, 0, 0.3).scale.set(1, 0.95, 1.6);
    m(new THREE.SphereGeometry(1.1, 12, 10), glass, 0, 0.15, 1.7).scale.set(1, 0.85, 0.9);
    m(new THREE.CylinderGeometry(0.25, 0.4, 5.5, 8), white, 0, 0.3, -3.6, Math.PI / 2);
    m(new THREE.BoxGeometry(0.1, 1.2, 0.8), body, 0, 0.8, -6.2);
    for (const s of [-1, 1]) m(new THREE.BoxGeometry(0.1, 0.1, 3), white, s * 0.9, -1.35, 0.3);
    this.rotor = new THREE.Group();
    const blade = new THREE.BoxGeometry(11, 0.05, 0.35);
    const b1 = new THREE.Mesh(blade, body), b2 = new THREE.Mesh(blade, body);
    b2.rotation.y = Math.PI / 2;
    this.rotor.add(b1, b2);
    this.rotor.position.set(0, 1.45, 0.3);
    g.add(this.rotor);
    this.tailRotor = new THREE.Mesh(new THREE.BoxGeometry(0.05, 1.6, 0.18), body);
    this.tailRotor.position.set(0.2, 0.8, -6.3);
    g.add(this.tailRotor);
    // searchlight cone
    const coneMat = new THREE.MeshBasicMaterial({ color: 0xfff4d0, transparent: true, opacity: 0.06, depthWrite: false, blending: THREE.AdditiveBlending, side: THREE.DoubleSide });
    this.cone = new THREE.Mesh(new THREE.ConeGeometry(4, 30, 20, 1, true), coneMat);
    this.cone.geometry.translate(0, -15, 0);
    g.add(this.cone);
    this.spot = new THREE.SpotLight(0xfff4d8, 0, 90, 0.2, 0.4, 1);
    g.add(this.spot);
    g.add(this.spot.target);
    this.group = g;
    const pp = game.player.pos;
    this.pos = g.position;
    const a = Math.random() * 6.28;
    this.pos.set(pp.x + Math.cos(a) * 180, pp.y + 60, pp.z + Math.sin(a) * 180);
    this.vel = new THREE.Vector3();
    this.yaw = 0;
    this.fireT = 2;
    game.scene.add(g);
    this.sound = game.audio?.loop('heli', this.pos);
  }

  update(dt) {
    const game = this.game;
    const police = this.police;
    this.rotor.rotation.y += dt * 30;
    this.tailRotor.rotation.x += dt * 40;
    if (this.down) {
      this.vel.y -= 9.8 * dt;
      this.pos.addScaledVector(this.vel, dt);
      this.group.rotation.y += dt * 3;
      if (Math.random() < 0.6) game.effects.fire(this.pos, 1.2);
      if (this.pos.y <= game.map.groundHeight(this.pos.x, this.pos.z) + 1) {
        game.combat.explosion(this.pos.clone(), 10, 250, game.player);
        this.remove();
      }
      this.sound?.setPos?.(this.pos);
      return;
    }
    const tp = police.level > 0 ? police.targetPos() : null;
    let target;
    if (!tp || police.level < 3) {
      // leave
      target = this.pos.clone().add(new THREE.Vector3(0, 0, -200));
      target.y = 120;
      if (this.leaveT === undefined) this.leaveT = 0;
      this.leaveT += dt;
      if (this.leaveT > 12) { this.remove(); return; }
    } else {
      const t = game.time * 0.25;
      target = new THREE.Vector3(tp.x + Math.cos(t) * 28, game.map.groundHeight(tp.x, tp.z) + 32, tp.z + Math.sin(t) * 28);
    }
    const dx = target.x - this.pos.x, dy = target.y - this.pos.y, dz = target.z - this.pos.z;
    this.vel.x += (dx * 0.6 - this.vel.x) * dt * 0.8;
    this.vel.y += (dy * 0.8 - this.vel.y) * dt * 1.2;
    this.vel.z += (dz * 0.6 - this.vel.z) * dt * 0.8;
    const sp = Math.hypot(this.vel.x, this.vel.z);
    if (sp > 30) { this.vel.x *= 30 / sp; this.vel.z *= 30 / sp; }
    this.pos.addScaledVector(this.vel, dt);
    // face the player
    if (tp) this.yaw += wrapAngle(Math.atan2(tp.x - this.pos.x, tp.z - this.pos.z) - this.yaw) * Math.min(1, dt * 1.5);
    this.group.rotation.set(clamp(this.vel.z * 0.01, -0.3, 0.3) * 0 + clamp(sp * 0.012, 0, 0.3), this.yaw, 0, 'YXZ');
    // searchlight at night
    const night = game.env.night;
    if (tp) {
      const local = this.group.worldToLocal(new THREE.Vector3(tp.x, tp.y, tp.z));
      this.cone.lookAt(tp.x, tp.y, tp.z);
      this.cone.rotateX(-Math.PI / 2);
      const L = local.length();
      this.cone.scale.set(1, L / 30, 1);
      this.cone.material.opacity = 0.05 * night;
      this.cone.visible = night > 0.2;
      this.spot.intensity = night * 400;
      this.spot.target.position.copy(local);
    }
    // sniper at 4+ stars
    this.fireT -= dt;
    if (tp && police.level >= 4 && this.fireT <= 0) {
      this.fireT = 0.9;
      const from = this.pos.clone().add(new THREE.Vector3(0, -1.2, 0));
      const to = new THREE.Vector3(tp.x + rand(-2.5, 2.5), tp.y + 1, tp.z + rand(-2.5, 2.5));
      const dir = to.sub(from).normalize();
      const hit = game.combat.raycast(from.x, from.y, from.z, dir.x, dir.y, dir.z, 120, null);
      game.effects.tracers.add(from, hit ? hit.point : from.clone().addScaledVector(dir, 120));
      if (hit) game.combat.applyHit(hit, { id: 'smg', damage: 10 }, { isPlayer: false, damageMul: 0.9 }, dir);
      game.audio?.playAt('smg', from, 0.7);
    }
    this.sound?.setPos?.(this.pos);
  }

  hit(dmg) {
    this.health -= dmg;
    if (this.health <= 0 && !this.down) { this.down = true; this.vel.y = 0; this.game.events.emit('heliDown'); }
  }

  remove() {
    this.done = true;
    this.group.parent?.remove(this.group);
    this.sound?.stop?.();
  }
}

export { Helicopter };
