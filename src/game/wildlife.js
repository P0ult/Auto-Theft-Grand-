// Wildlife: animals around the player, by district — pigeons on city pavements and plazas, gulls on the
// beach and the docks, cats in the back streets, stray dogs in the rougher neighbourhoods, deer, rabbits and
// crows in the country and the forest, coyotes in the desert, cows on the farmland — and people out walking
// their dogs. Animals wander, graze and peck, and bolt (or take off) when you come too close, fire a gun or
// drive at them. Cars, bullets, blasts and fists kill them.
import * as THREE from 'three';
import { Animal, SPECIES } from '../entities/animals.js';
import { rand, randInt, pick, clamp, dist2 } from '../core/utils.js';

// district -> [breed, weight, [min, max] group size]
const MIX = {
  downtown: [['pigeon', 6, [3, 7]]],
  midtown: [['pigeon', 5, [3, 6]], ['tabby', 1, [1, 1]], ['blackcat', 1, [1, 1]]],
  westside: [['pigeon', 2, [2, 5]], ['tabby', 2, [1, 1]], ['siamese', 1, [1, 1]]],
  hood: [['pigeon', 2, [2, 5]], ['tabby', 2, [1, 1]], ['blackcat', 2, [1, 1]], ['stray', 1, [1, 2]]],
  corona: [['pigeon', 2, [2, 5]], ['blackcat', 2, [1, 1]], ['stray', 1, [1, 2]]],
  beach: [['seagull', 6, [2, 6]]],
  docks: [['seagull', 4, [2, 5]], ['pigeon', 2, [2, 4]], ['stray', 1, [1, 1]]],
  hills: [['deer', 2, [1, 3]], ['rabbit', 3, [1, 2]], ['crow', 1, [2, 4]], ['tabby', 1, [1, 1]]],
  country: [['rabbit', 3, [1, 2]], ['deer', 2, [1, 3]], ['crow', 2, [3, 6]], ['cow', 2, [3, 6]]],
  forest: [['deer', 4, [1, 4]], ['rabbit', 2, [1, 2]], ['crow', 2, [2, 5]]],
  desert: [['coyote', 3, [1, 3]], ['rabbit', 2, [1, 2]], ['crow', 1, [1, 3]]],
  base: [['crow', 1, [2, 4]]],
};
const STRAYS = ['lab', 'rottweiler', 'shepherd', 'husky'];
const CITY = new Set(['downtown', 'midtown', 'westside', 'hood', 'corona', 'docks', 'beach']);

export class Wildlife {
  constructor(game) {
    this.game = game;
    this.list = [];
    this.spawnT = 0;
    this.max = { low: 11, medium: 15, high: 19, ultra: 23 }[game.settings?.quality] ?? 15;
    game.events.on('gunshot', (s, pos) => this._scare(pos, 55, s));
    game.events.on('explosion', (pos) => this._scare(pos, 90, null));
  }

  get count() { let n = 0; for (const a of this.list) if (!a.dead && !a.owner) n += a.sp.bird ? 0.6 : 1; return n; }

  update(dt) {
    const g = this.game, pl = g.player;
    const pp = pl.vehicle ? pl.vehicle.pos : pl.pos;
    this.spawnT -= dt;
    if (this.spawnT <= 0 && !g.disableAmbient && g.gameplay?.state !== 'menu') {
      this.spawnT = 0.6;
      if (this.count < this.max) this._spawnGroup(pp);
    }
    for (let i = this.list.length - 1; i >= 0; i--) {
      const a = this.list[i];
      if (a.removed) { this.list.splice(i, 1); continue; }
      const d2 = dist2(a.pos.x, a.pos.z, pp.x, pp.z);
      const far = a.owner ? (a.owner.removed || (a.owner !== pl && d2 > 170 * 170)) : d2 > 170 * 170 || (a.dead && a.deathT > 40 && d2 > 50 * 50);
      if (far && !a.pet) { a.remove(); this.list.splice(i, 1); continue; }
      if (!a.pet) this._think(a, dt, pp);
      a.update(dt);
      a.root.visible = d2 < 230 * 230;
    }
    this._roadkill();
  }

  // ---------------------------------------------------------------- spawning
  _spawnGroup(pp) {
    const g = this.game, map = g.map;
    for (let tries = 0; tries < 6; tries++) {
      const ang = Math.random() * Math.PI * 2, r = rand(45, 115);
      const x = pp.x + Math.cos(ang) * r, z = pp.z + Math.sin(ang) * r;
      const district = map.districtAt(x, z);
      const mix = MIX[district];
      if (!mix) continue;
      let tot = 0; for (const m of mix) tot += m[1];
      let pickR = Math.random() * tot, row = mix[0];
      for (const m of mix) { pickR -= m[1]; if (pickR <= 0) { row = m; break; } }
      const [breed0, , [n0, n1]] = row;
      const n = randInt(n0, n1);
      const spot = this._spot(x, z, district, breed0);
      if (!spot) continue;
      if (g.peds?._inView(spot.x, spot.z) && r < 80) continue;
      for (let k = 0; k < n; k++) {
        const breed = breed0 === 'stray' ? pick(STRAYS) : breed0 === 'cow' && Math.random() < 0.35 ? 'brown_cow' : breed0;
        const sx = spot.x + rand(-2.5, 2.5) * (k ? 1 : 0), sz = spot.z + rand(-2.5, 2.5) * (k ? 1 : 0);
        const y = g.collision.floorHeight(sx, sz, spot.y + 1.5);
        if (!Number.isFinite(y) || map.waterDepth(sx, sz) > 0.2) continue;
        const a = new Animal(g, breed, sx, sz, { y });
        a.home = { x: spot.x, z: spot.z };
        a.state = a.sp.bird ? 'peck' : Math.random() < 0.5 ? 'graze' : 'wander';
        if (breed0 === 'stray') a.stray = true;
        this.list.push(a);
      }
      return;
    }
  }

  // somewhere an animal of this kind can stand
  _spot(x, z, district, breed) {
    const g = this.game, map = g.map;
    if (CITY.has(district) && !(breed === 'seagull' && district === 'beach')) {
      // pavements, plazas, parks: the pedestrian walk graph
      const b = map.walkAreaAt?.(x, z);
      if (!b || !b.nodeIds?.length) return null;
      const n = map.walkNodes[pick(b.nodeIds)];
      const px = n.x + rand(-1.5, 1.5), pz = n.z + rand(-1.5, 1.5);
      if (map.isOnRoad(px, pz)) return null;
      return { x: px, z: pz, y: g.collision.floorHeight(px, pz, 50) };
    }
    if (map.isOnRoad(x, z) || map.waterDepth(x, z) > 0) return null;
    const y = g.collision.floorHeight(x, z, 400);
    if (!Number.isFinite(y)) return null;
    if (g.collision.resolveCircle(x, z, 1.5, y + 0.2, 1.5).hit) return null;
    // not up a cliff
    const s = Math.abs(map.groundHeight(x + 2, z) - map.groundHeight(x - 2, z)) + Math.abs(map.groundHeight(x, z + 2) - map.groundHeight(x, z - 2));
    if (s > 2.4) return null;
    return { x, z, y };
  }

  // people out walking their dog (called by the pedestrian spawner)
  addWalkedDog(ped) {
    if (this.list.filter((a) => a.owner && !a.pet).length > 5) return null;
    const a = new Animal(this.game, pick(['lab', 'lab', 'poodle', 'pug', 'husky', 'shepherd']), ped.pos.x + 1, ped.pos.z - 1, { y: ped.pos.y });
    a.owner = ped; a.state = 'follow';
    ped.walkedDog = a;
    this.list.push(a);
    return a;
  }

  // ---------------------------------------------------------------- behaviour
  _think(a, dt, pp) {
    const g = this.game, pl = g.player, sp = a.sp;
    if (a.dead) return;
    // walked dogs trot along with their owner; strays wander
    if (a.owner) {
      if (a.owner.dead) { a.owner = null; a.stray = true; a.state = 'flee'; a.threat = { x: a.pos.x + rand(-1, 1), z: a.pos.z + rand(-1, 1) }; a.stateT = 0; a.fleeT = 6; this._bark(a); return; }
      followOwner(a, a.owner, dt);
      return;
    }
    // what's scary: the player close by (more so at speed in a car), anyone with a gun out
    const threatD = Math.hypot(a.pos.x - pp.x, a.pos.z - pp.z);
    const pv = pl.vehicle;
    const fear = sp.fear * (pv ? (pv.speedAbs > 5 ? 1.7 : 1.1) : pl.aiming ? 1.6 : pl.sprinting ? 1.3 : 1);
    if (a.state !== 'flee' && a.state !== 'fly' && threatD < fear && !(a.stray && threatD > 4)) this._startFlee(a, pp.x, pp.z);
    switch (a.state) {
      case 'flee': {
        const t = a.threat;
        let dx = a.pos.x - t.x, dz = a.pos.z - t.z;
        const d = Math.hypot(dx, dz) || 1;
        dx /= d; dz /= d;
        if (a.bumpT > 0.4) { const s = a.id % 2 ? 1 : -1; const nx = -dz * s, nz = dx * s; dx = nx; dz = nz; }
        a.goTo(a.pos.x + dx * 10, a.pos.z + dz * 10, sp.run);
        if (a.stateT > (a.fleeT || 5) && d > (sp.fear || 8) * 1.5) { a.state = 'wander'; a.stateT = 0; a.stop(); }
        break;
      }
      case 'fly': {
        // up and away, then land somewhere else (or just leave)
        if (!a.flyTo) { const ang = Math.atan2(a.pos.x - a.threat.x, a.pos.z - a.threat.z) + rand(-0.6, 0.6); a.flyTo = { x: a.pos.x + Math.sin(ang) * rand(35, 70), z: a.pos.z + Math.cos(ang) * rand(35, 70) }; a.alt = a.pos.y + rand(7, 14); }
        a.goTo(a.flyTo.x, a.flyTo.z, sp.fly);
        if (Math.hypot(a.pos.x - a.flyTo.x, a.pos.z - a.flyTo.z) < 2) {
          const y = g.collision.floorHeight(a.pos.x, a.pos.z, a.pos.y + 1);
          a.alt = y;
          if (a.pos.y - y < 0.4) { a.flying = false; a.flyTo = null; a.state = 'peck'; a.stateT = 0; a.stop(); }
        }
        break;
      }
      case 'peck': // birds potter about
        if (!a.want || a.stateT > 3) { a.stateT = 0; a.goTo(a.pos.x + rand(-2, 2), a.pos.z + rand(-2, 2), sp.walk * (Math.random() < 0.5 ? 1 : 0)); }
        break;
      case 'graze':
        a.stop();
        if (a.stateT > rand(4, 9)) { a.state = 'wander'; a.stateT = 0; }
        if (a.kind === 'cow' && Math.random() < dt * 0.02) g.audio?.playAt('moo', a.pos, 0.7);
        break;
      case 'wander':
      default: {
        if (!a.want || a.stateT > 7 || a.bumpT > 1) {
          a.stateT = 0;
          const h = a.home || a.pos;
          const R = a.kind === 'cow' ? 10 : a.kind === 'cat' || a.stray ? 14 : 22;
          a.goTo(h.x + rand(-R, R), h.z + rand(-R, R), sp.walk);
          if (!sp.bird && Math.random() < 0.4) { a.state = 'graze'; a.stop(); }
        }
        if (a.stray && threatD < 6 && Math.random() < dt * 0.4) this._bark(a);
        break;
      }
    }
  }

  _startFlee(a, x, z) {
    a.threat = { x, z };
    a.stateT = 0;
    a.fleeT = rand(4, 8);
    if (a.sp.bird) { a.state = 'fly'; a.flying = true; a.flyTo = null; if (Math.random() < 0.5) this.game.audio?.playAt('flap', a.pos, 0.6); }
    else { a.state = 'flee'; if (a.kind === 'cat' && Math.random() < 0.5) this.game.audio?.playAt('meow', a.pos, 0.8); }
  }

  _bark(a) {
    if (this.game.time - (a.barkT || -9) < 1.5) return;
    a.barkT = this.game.time;
    this.game.audio?.playAt('bark', a.pos, a.scale > 0.9 ? 1 : 0.7);
  }

  // gunfire and blasts send everything running (dogs out with their owner stay with them)
  _scare(pos, R, src) {
    for (const a of this.list) {
      if (a.dead || a.pet || a.owner || a.inVehicle) continue;
      if (dist2(a.pos.x, a.pos.z, pos.x, pos.z) < R * R) this._startFlee(a, pos.x, pos.z);
    }
  }

  // cars that hit animals kill them
  _roadkill() {
    const g = this.game;
    for (const v of g.vehicles.list) {
      if (v.removed || v.speedAbs < 3.5 || v.def.kind === 'train' || v.def.aircraft) continue;
      for (const a of this.all()) {
        if (a.dead || a.inVehicle || a.flying) continue;
        const dx = a.pos.x - v.pos.x, dz = a.pos.z - v.pos.z;
        if (dx * dx + dz * dz > 49 || Math.abs(a.pos.y - v.pos.y) > 1.8) continue;
        const [lx, lz] = v.worldToLocal(a.pos.x, a.pos.z);
        if (Math.abs(lx) < v.hx + a.radius && Math.abs(lz) < v.hz + a.radius) {
          a.takeDamage(v.speedAbs * 12, { source: v.driver || null, type: 'vehicle' });
          if (!a.dead) { a.pos.x += (dx / (Math.hypot(dx, dz) || 1)) * 1.2; a.pos.z += (dz / (Math.hypot(dx, dz) || 1)) * 1.2; }
          g.audio?.playAt('bodyhit', a.pos, 0.5);
        }
      }
    }
  }

  // every animal in the world (wildlife, walked dogs, pets — ours and other players')
  all() {
    const g = this.game;
    const out = this.list;
    const extra = g.pets?.animals();
    return extra && extra.length ? out.concat(extra) : out;
  }

  clear() { for (const a of this.list) if (!a.pet) a.remove(); this.list = this.list.filter((a) => a.pet); }
}

// keep up with someone: trot at heel, run to catch up, sit when they stop (pets)
export function followOwner(a, owner, dt, opts = {}) {
  const sp = a.sp;
  const op = owner.vehicle ? owner.vehicle.pos : owner.ragdolling && owner.ragdoll ? owner.ragdoll.center : owner.pos;
  const oy = owner.yaw || 0;
  const side = a.followSide || (a.followSide = a.id % 2 ? 1 : -1);
  const tx = op.x - Math.sin(oy) * 1.5 + Math.cos(oy) * 0.9 * side, tz = op.z - Math.cos(oy) * 1.5 - Math.sin(oy) * 0.9 * side;
  const d = Math.hypot(tx - a.pos.x, tz - a.pos.z);
  const ownerSpeed = owner.vehicle ? owner.vehicle.speedAbs : Math.hypot(owner.vel?.x || 0, owner.vel?.z || 0);
  if (d > (opts.teleport || 70)) {
    // hopelessly far behind: catch up out of sight
    const y = a.game.collision.floorHeight(tx, tz, op.y + 1.5);
    if (Number.isFinite(y)) { a.pos.set(tx, y, tz); a.speed = 0; }
    return;
  }
  if (d > 1.2) {
    const spd = d > 6 || ownerSpeed > 3.5 ? Math.min(sp.run, Math.max(ownerSpeed * 1.15, sp.walk * 2.2)) : Math.max(sp.walk, ownerSpeed);
    a.goTo(tx, tz, spd);
    a.state = 'follow'; a.idleT = 0;
  } else {
    a.stop();
    a.idleT = (a.idleT || 0) + dt;
    if (a.idleT > 2.5 && opts.sit) a.state = 'sit'; else if (a.state !== 'sit') a.state = 'follow';
  }
  a.happy = true;
}

export { SPECIES };
