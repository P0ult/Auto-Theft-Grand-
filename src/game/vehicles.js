// Vehicle manager: spawning, parked-car streaming, vehicle-vehicle and vehicle-pedestrian collisions,
// and enter / exit / carjack sequences for any character.
import * as THREE from 'three';
import { Vehicle } from '../entities/vehicle.js';
import { vehicleClass } from '../entities/aircraft.js';
import { Train } from '../entities/train.js';
import { VEHICLES, TRAFFIC_POOL } from '../entities/vehicledefs.js';
import { RNG, clamp, dist2, hash2 } from '../core/utils.js';

const _a = new THREE.Vector3();

export class VehicleManager {
  constructor(game) {
    this.game = game;
    this.list = [];
    this.seqs = [];
    this.parked = new Map(); // spot index -> vehicle
    this.consumedSpots = new Set();
    this.streamTimer = 0;
    this.maxParked = 26;
  }

  spawn(id, x, z, yaw, opts = {}) {
    const Cls = VEHICLES[id].train ? Train : vehicleClass(VEHICLES[id]) || Vehicle;
    const v = new Cls(this.game, id, { x, z, yaw, ...opts });
    this.list.push(v);
    return v;
  }

  remove(v) {
    const i = this.list.indexOf(v);
    if (i >= 0) this.list.splice(i, 1);
    v.remove();
  }

  update(dt) {
    const list = this.list;
    // AI for vehicles not driven by the traffic or police systems (mission cars)
    for (const v of list) {
      if (v.ai && !v.traffic && !v.policeUnit && v.driver && !v.driver.isPlayer && !v.driver.dead && !v.isWrecked) v.ai.update(dt);
    }
    for (const v of list) v.update(dt);
    // vehicle vs vehicle
    for (let i = 0; i < list.length; i++) {
      const A = list[i];
      for (let j = i + 1; j < list.length; j++) {
        const Bv = list[j];
        const r = Math.max(A.hz, A.hx) + Math.max(Bv.hz, Bv.hx);
        if (dist2(A.pos.x, A.pos.z, Bv.pos.x, Bv.pos.z) > r * r) continue;
        if (Math.abs(A.pos.y - Bv.pos.y) > 2.2) continue;
        this._carCar(A, Bv);
      }
    }
    // vehicles vs characters
    const chars = this.game.allCharacters();
    for (const v of list) {
      if (v.removed) continue;
      const spd = v.speedAbs;
      const R = Math.hypot(v.hx, v.hz) + 0.5;
      for (const c of chars) {
        if (c.vehicle || c.removed) continue;
        if (Math.abs(c.pos.y - v.pos.y) > 1.8) continue;
        if (dist2(c.pos.x, c.pos.z, v.pos.x, v.pos.z) > R * R) continue;
        if (c.ragdolling) { this._runOver(v, c, spd); continue; }
        this._carPed(v, c);
      }
    }
    // sequences
    for (let i = this.seqs.length - 1; i >= 0; i--) {
      const s = this.seqs[i];
      if (this._runSeq(s, dt)) this.seqs.splice(i, 1);
    }
    // streaming parked cars
    this.streamTimer -= dt;
    if (this.streamTimer <= 0) { this.streamTimer = 0.5; this._streamParked(); }
    // cleanup wrecks far away
    const p = this.game.player.pos;
    for (let i = list.length - 1; i >= 0; i--) {
      const v = list[i];
      if (v.isWrecked) {
        v.wreckTime = (v.wreckTime || 0) + dt;
        if (v.wreckTime > 40 && dist2(v.pos.x, v.pos.z, p.x, p.z) > 80 * 80 && !v.persistent) this.remove(v);
      }
    }
  }

  // --------------------------------------------------------------- collisions
  _corners(v) {
    const s = Math.sin(v.yaw), c = Math.cos(v.yaw);
    const out = [];
    for (const [sx, sz] of [[1, 1], [1, -1], [-1, -1], [-1, 1]]) {
      const lx = v.hx * sx, lz = v.hz * sz;
      out.push([v.pos.x + lx * c + lz * s, v.pos.z - lx * s + lz * c]);
    }
    return out;
  }

  _carCar(A, Bc) {
    const ca = this._corners(A), cb = this._corners(Bc);
    const axes = [];
    for (const v of [A, Bc]) { const s = Math.sin(v.yaw), c = Math.cos(v.yaw); axes.push([s, c], [c, -s]); }
    let minOv = Infinity, nx = 0, nz = 0;
    for (const [ax, az] of axes) {
      let aMin = Infinity, aMax = -Infinity, bMin = Infinity, bMax = -Infinity;
      for (const p of ca) { const d = p[0] * ax + p[1] * az; aMin = Math.min(aMin, d); aMax = Math.max(aMax, d); }
      for (const p of cb) { const d = p[0] * ax + p[1] * az; bMin = Math.min(bMin, d); bMax = Math.max(bMax, d); }
      const ov = Math.min(aMax, bMax) - Math.max(aMin, bMin);
      if (ov <= 0) return;
      if (ov < minOv) {
        minOv = ov;
        const dir = ((Bc.pos.x - A.pos.x) * ax + (Bc.pos.z - A.pos.z) * az) > 0 ? 1 : -1;
        nx = ax * dir; nz = az * dir; // from A to B
      }
    }
    // contact point: average of corners inside the other box (fallback midpoint)
    let px = 0, pz = 0, n = 0;
    const inside = (v, x, z) => { const [lx, lz] = v.worldToLocal(x, z); return Math.abs(lx) <= v.hx + 0.02 && Math.abs(lz) <= v.hz + 0.02; };
    for (const p of ca) if (inside(Bc, p[0], p[1])) { px += p[0]; pz += p[1]; n++; }
    for (const p of cb) if (inside(A, p[0], p[1])) { px += p[0]; pz += p[1]; n++; }
    if (n) { px /= n; pz /= n; } else { px = (A.pos.x + Bc.pos.x) / 2; pz = (A.pos.z + Bc.pos.z) / 2; }
    // separate proportional to inverse mass
    const imA = 1 / A.mass, imB = 1 / Bc.mass;
    const tot = imA + imB;
    A.pos.x -= nx * minOv * imA / tot; A.pos.z -= nz * minOv * imA / tot;
    Bc.pos.x += nx * minOv * imB / tot; Bc.pos.z += nz * minOv * imB / tot;
    // impulse
    const rax = px - A.pos.x, raz = pz - A.pos.z, rbx = px - Bc.pos.x, rbz = pz - Bc.pos.z;
    const vax = A.vel.x + A.r * raz, vaz = A.vel.z - A.r * rax;
    const vbx = Bc.vel.x + Bc.r * rbz, vbz = Bc.vel.z - Bc.r * rbx;
    const rvn = (vbx - vax) * nx + (vbz - vaz) * nz;
    // tanks flatten whatever they drive into
    const tank = A.def.tank ? A : Bc.def.tank ? Bc : null;
    if (tank) {
      const other = tank === A ? Bc : A;
      if (!other.def.tank && !other.def.kind && tank.speedAbs > 1.2 && this.game.time - (other._crushT || 0) > 0.35) {
        other._crushT = this.game.time;
        other.damage(240, tank.driver);
        other.dent(px, other.pos.y + 1.1, pz, 30);
        other.bodyYV -= 2.5;
        this.game.audio?.playAt('crash', _a.set(px, other.pos.y + 0.8, pz), 0.8);
        this.game.effects?.sparks?.(new THREE.Vector3(px, other.pos.y + 0.8, pz), 12);
      }
    }
    if (rvn >= 0) return;
    const raN = raz * nx - rax * nz, rbN = rbz * nx - rbx * nz;
    const e = 0.25;
    const j = -(1 + e) * rvn / (imA + imB + raN * raN / A.I + rbN * rbN / Bc.I);
    A.vel.x -= j * nx * imA; A.vel.z -= j * nz * imA; A.r -= raN * j / A.I;
    Bc.vel.x += j * nx * imB; Bc.vel.z += j * nz * imB; Bc.r += rbN * j / Bc.I;
    const impact = -rvn;
    if (impact > 2.5) {
      const dmg = (impact - 2.5) * 12;
      A.damage(dmg * (Bc.mass / (A.mass + Bc.mass)) * 2, Bc.driver);
      Bc.damage(dmg * (A.mass / (A.mass + Bc.mass)) * 2, A.driver);
      A.dent(px, A.pos.y + 0.6, pz, impact); Bc.dent(px, Bc.pos.y + 0.6, pz, impact);
      if (this.game.time - (A.lastHit || 0) > 0.3) {
        A.lastHit = this.game.time;
        this.game.audio?.playAt('crash', _a.set(px, A.pos.y + 0.5, pz), clamp(impact / 16, 0.25, 1));
        this.game.effects?.sparks?.(new THREE.Vector3(px, A.pos.y + 0.5, pz), impact);
        const pl = this.game.player;
        if (pl.vehicle === A || pl.vehicle === Bc) this.game.rig.addShake(Math.min(0.9, impact / 20));
      }
      this.game.events.emit('carCrash', A, Bc, impact);
    }
  }

  _carPed(v, c) {
    const [lx, lz] = v.worldToLocal(c.pos.x, c.pos.z);
    const r = c.radius;
    const qx = clamp(lx, -v.hx, v.hx), qz = clamp(lz, -v.hz, v.hz);
    const dx = lx - qx, dz = lz - qz;
    const d2 = dx * dx + dz * dz;
    if (d2 > r * r) return;
    const s = Math.sin(v.yaw), co = Math.cos(v.yaw);
    let nlx = dx, nlz = dz;
    let d = Math.sqrt(d2);
    if (d < 1e-4) { // center inside: push along smallest exit
      const ex = v.hx - Math.abs(lx), ez = v.hz - Math.abs(lz);
      if (ex < ez) { nlx = Math.sign(lx) || 1; nlz = 0; } else { nlx = 0; nlz = Math.sign(lz) || 1; }
      d = 0;
    } else { nlx /= d; nlz /= d; }
    const nx = nlx * co + nlz * s, nz = -nlx * s + nlz * co;
    // car point velocity
    const px = c.pos.x - v.pos.x, pz = c.pos.z - v.pos.z;
    const vpx = v.vel.x + v.r * pz, vpz = v.vel.z - v.r * px;
    // Impact speed is how fast the *car* closes on the person. Someone running into a parked car
    // must not count as being hit by it (only a person moving away softens the blow).
    const carN = vpx * nx + vpz * nz;
    const pedN = c.vel.x * nx + c.vel.z * nz;
    const rel = carN - Math.max(0, pedN);
    if (rel > 3.2 && carN > 3.2 && !c.invincible) {
      const spd = Math.hypot(vpx, vpz);
      const dmg = Math.pow(rel - 2.5, 2) * 2.4 + 6;
      const imp = new THREE.Vector3(vpx * 0.85 + nx * 2, 2.2 + spd * 0.22, vpz * 0.85 + nz * 2);
      c.vel.set(0, 0, 0);
      this.game.events.emit('pedHitByCar', c, v, rel);
      if (c.isPlayer && c.health - dmg <= 0 && dmg < 60) c.takeDamage(dmg, { type: 'vehicle', source: v.driver, impulse: imp, knockdown: true });
      else c.takeDamage(dmg, { type: 'vehicle', source: v.driver, impulse: imp, knockdown: true });
      if (c.dead && !c.ragdolling) c.startRagdoll(imp);
      this.game.audio?.playAt('bodyhit', c.pos, clamp(rel / 12, 0.3, 1));
      this.game.effects?.blood?.(c.chestPos, new THREE.Vector3(nx, 0.5, nz), Math.min(20, rel * 2));
      v.vel.multiplyScalar(1 - clamp(80 / v.mass, 0.01, 0.08));
      v.bodyYV += 0.4;
    } else {
      // gentle push
      const push = r - d + 0.02;
      c.pos.x += nx * push; c.pos.z += nz * push;
      // cancel the part of the person's velocity that points into the car
      if (pedN < 0) { c.vel.x -= nx * pedN; c.vel.z -= nz * pedN; }
      if (carN > 0.6 && !c.isPlayer) c.onBumped?.(v);
    }
  }

  _runOver(v, c, spd) {
    if (spd < 3 || !c.ragdoll) return;
    const [lx, lz] = v.worldToLocal(c.ragdoll.pos[0], c.ragdoll.pos[2]);
    if (Math.abs(lx) < v.hx && Math.abs(lz) < v.hz) {
      if (!c._runOverT || this.game.time - c._runOverT > 0.5) {
        c._runOverT = this.game.time;
        v.bodyYV += 1.2;
        v.bodyRollV += (lx > 0 ? 1 : -1) * 0.8;
        if (!c.dead) c.takeDamage(spd * 4, { type: 'vehicle', source: v.driver });
        c.ragdoll.push(0, v.vel.x * 0.5, 1, v.vel.z * 0.5);
        this.game.audio?.playAt('bodyhit', c.pos, 0.5);
        this.game.events.emit('pedRunOver', c, v);
      }
    }
  }

  // --------------------------------------------------------------- enter / exit
  nearestEnterable(pos, maxDist = 4.5) {
    let best = null, bd = maxDist * maxDist;
    for (const v of this.list) {
      if (v.isWrecked || v.removed || v.locked) continue;
      const dp = v.nearestDoor ? v.nearestDoor(pos) : v.doorWorld(_a);
      const d = dist2(pos.x, pos.z, dp.x, dp.z);
      const dc = v.nearestDoor ? Infinity : dist2(pos.x, pos.z, v.pos.x, v.pos.z);
      const dd = Math.min(d, dc * 0.8);
      if (dd < bd && Math.abs(v.pos.y - pos.y) < 2) { bd = dd; best = v; }
    }
    return best;
  }

  isBusy(char) { return this.seqs.some((s) => s.char === char); }

  enter(char, veh, seat = 0, opts = {}) {
    if (this.isBusy(char) || veh.isWrecked) return false;
    if (veh.locked && !opts.force) { this.game.audio?.play('locked'); return false; }
    this.seqs.push({ type: 'enter', char, veh, seat, phase: 'approach', t: 0, opts });
    return true;
  }

  exit(char, opts = {}) {
    const veh = char.vehicle;
    if (!veh || this.isBusy(char)) return false;
    if (veh.def.aircraft && (!veh.grounded || veh.speedAbs > 9)) {
      // bail out: step off the side into the air (the player's chute opens once clear)
      const alt = veh.altitude;
      const side = veh.localPoint(veh.hx + 1.6, veh.cgY, 0);
      veh.takeOut(char, side);
      char.pos.y = side.y - 1;
      char.vel.copy(veh.vel).multiplyScalar(0.8);
      char.grounded = false;
      if (char.isPlayer) char.bailOut?.(alt);
      this.game.events.emit('exitedVehicle', char, veh);
      return true;
    }
    if (veh.speedAbs > 9 && char.isPlayer) {
      // bail out of a moving car
      veh.takeOut(char, veh.localToWorld(veh.hx + 1.0, 0, 0));
      char.vel.set(veh.vel.x * 0.7, 2, veh.vel.z * 0.7);
      char.knockDown(new THREE.Vector3(veh.vel.x * 0.1, 1.5, veh.vel.z * 0.1));
      char.takeDamage(Math.min(30, veh.speedAbs * 0.8), { type: 'fall' });
      return true;
    }
    this.seqs.push({ type: 'exit', char, veh, phase: 'open', t: 0, opts });
    return true;
  }

  _doorTarget(veh, seat, char = null) {
    if (veh.doorFor) return veh.doorFor(seat, char);
    const d = veh.model.doorPos;
    const x = seat % 2 === 0 ? d.x : -d.x;
    const z = seat < 2 ? d.z : d.z - 0.9;
    return veh.localToWorld(x, 0, z, new THREE.Vector3());
  }

  _runSeq(s, dt) {
    const { char, veh } = s;
    s.t += dt;
    if (char.dead || char.removed || char.ragdolling) { if (veh.model.door) veh.model.door.open = 0; return true; }
    const door = veh.model.door;
    if (s.type === 'enter') {
      if (veh.isWrecked) return true;
      if (s.phase === 'approach') {
        const tp = this._doorTarget(veh, s.seat, char);
        const dx = tp.x - char.pos.x, dz = tp.z - char.pos.z;
        const d = Math.hypot(dx, dz);
        if (d < 0.35 || s.t > 2.5) {
          char.moveTarget.set(0, 0); char.vel.set(0, 0, 0);
          if (s.t > 2.5 && d > 1.5) { char.pos.x = tp.x; char.pos.z = tp.z; }
          s.phase = 'open'; s.t = 0;
          if (veh.speedAbs > 4 && !s.opts.force) return true;
        } else {
          const sp = Math.min(char.isPlayer ? 4.5 : 3.5, d * 6);
          char.moveTarget.set(dx / d * sp, dz / d * sp);
          char.faceTowards(tp.x, tp.z, dt, 14);
        }
        return false;
      }
      // face the car's side
      char.moveTarget.set(0, 0);
      const sideYaw = veh.yaw + (s.seat % 2 === 0 ? -Math.PI / 2 : Math.PI / 2);
      char.yaw = sideYaw;
      if (s.phase === 'open') {
        if (s.seat === 0) door.open = Math.min(1, s.t / 0.3);
        if (s.t >= 0.3) {
          const occupant = veh.occupants[s.seat];
          if (occupant && occupant !== char) { s.phase = 'jack'; s.t = 0; char.anim.play('pull'); this.game.audio?.play('doorOpen'); }
          else { s.phase = 'enter'; s.t = 0; this._beginSit(s); }
        }
        return false;
      }
      if (s.phase === 'jack') {
        const occupant = veh.occupants[s.seat];
        if (s.t > 0.35 && occupant && occupant !== char) {
          veh.takeOut(occupant, this._doorTarget(veh, s.seat).addScaledVector(new THREE.Vector3(Math.sin(sideYaw + Math.PI), 0, Math.cos(sideYaw + Math.PI)), -0.6));
          const away = new THREE.Vector3(-Math.sin(sideYaw) * 3.5, 1.5, -Math.cos(sideYaw) * 3.5);
          occupant.knockDown(away);
          occupant.onCarjacked?.(char);
          this.game.events.emit('carjack', char, occupant, veh);
        }
        if (s.t > 0.8) { s.phase = 'enter'; s.t = 0; this._beginSit(s); }
        return false;
      }
      if (s.phase === 'enter') {
        const k = Math.min(1, s.t / 0.45);
        const from = s.fromLocal, to = s.toLocal;
        char.root.position.set(from.x + (to.x - from.x) * k, from.y + (to.y - from.y) * k, from.z + (to.z - from.z) * k);
        if (k >= 1) { s.phase = 'close'; s.t = 0; }
        return false;
      }
      if (s.phase === 'close') {
        if (s.seat === 0) door.open = Math.max(0, 1 - s.t / 0.3);
        if (s.t >= 0.3) { door.open = 0; this.game.audio?.playAt('doorClose', veh.pos, 0.7); char.onEnteredVehicle?.(veh); this.game.events.emit('enteredVehicle', char, veh); return true; }
        return false;
      }
    } else if (s.type === 'exit') {
      if (s.phase === 'open') {
        if (char.seat === 0) door.open = Math.min(1, s.t / 0.3);
        veh.input.throttle = 0; veh.input.brake = 1;
        if (s.t >= 0.3) {
          const seat = char.seat;
          const tp = this._doorTarget(veh, seat);
          veh.takeOut(char, tp);
          char.yaw = veh.yaw + (seat % 2 === 0 ? Math.PI / 2 : -Math.PI / 2);
          veh.input.brake = 0; veh.input.handbrake = true;
          s.phase = 'close'; s.t = 0; s.seat = seat;
          this.game.events.emit('exitedVehicle', char, veh);
        }
        return false;
      }
      if (s.phase === 'close') {
        if (s.seat === 0) door.open = Math.max(0, 1 - s.t / 0.3);
        if (s.t >= 0.3) { door.open = 0; this.game.audio?.playAt('doorClose', veh.pos, 0.6); return true; }
        return false;
      }
    }
    return true;
  }

  _beginSit(s) {
    const { char, veh, seat } = s;
    const door = this._doorTarget(veh, seat);
    veh.putIn(char, seat);
    const [lx, lz] = veh.worldToLocal(door.x, door.z);
    const to = char.root.position.clone();
    s.toLocal = to;
    s.fromLocal = new THREE.Vector3(lx, 0, lz);
    char.root.position.copy(s.fromLocal);
    char.yaw = veh.yaw;
  }

  // --------------------------------------------------------------- parked cars
  _streamParked() {
    const game = this.game;
    const spots = game.map.parkingSpots;
    const p = game.player.vehicle ? game.player.vehicle.pos : game.player.pos;
    // despawn far
    for (const [idx, v] of this.parked) {
      if (v.removed) { this.parked.delete(idx); continue; }
      const d2 = dist2(v.pos.x, v.pos.z, p.x, p.z);
      const touched = !v.parked || v.driver || v.health < 1000 || v.persistent;
      if (touched) { this.parked.delete(idx); this.consumedSpots.add(idx); v.persistent = v.persistent; continue; }
      if (d2 > 170 * 170) { this.remove(v); this.parked.delete(idx); }
    }
    // free consumed spots that are far away
    if (this.consumedSpots.size > 200) this.consumedSpots.clear();
    if (this.parked.size >= this.maxParked) return;
    let added = 0;
    for (let i = 0; i < spots.length && added < 3; i++) {
      const s = spots[i];
      const d2 = dist2(s.x, s.z, p.x, p.z);
      if (d2 > 110 * 110 || d2 < 30 * 30) continue;
      if (this.parked.has(i) || this.consumedSpots.has(i)) continue;
      if (hash2(i, 77) > 0.5) continue;
      // avoid spawning in view very close
      let blocked = false;
      for (const v of this.list) if (dist2(v.pos.x, v.pos.z, s.x, s.z) < 36) { blocked = true; break; }
      if (blocked) continue;
      const rng = new RNG(i * 31 + 7);
      let type;
      if (s.police) type = 'police';
      else if (s.fancy) type = rng.pick(['zenith', 'kestrel', 'summit']);
      else if (s.district === 'hood') type = rng.weighted([['bouncer', 3], ['meridian', 4], ['brawler', 2], ['hauler', 2], ['summit', 1]]);
      else if (s.district === 'docks') type = rng.weighted([['boxer', 2], ['parcel', 3], ['hauler', 3]]);
      else type = rng.weighted(TRAFFIC_POOL.filter(([id]) => id !== 'boxer'));
      const v = this.spawn(type, s.x, s.z, s.rot, { parked: true });
      if (s.police) v.locked = false;
      this.parked.set(i, v);
      added++;
      if (this.parked.size >= this.maxParked) break;
    }
  }
}

export { VEHICLES };
