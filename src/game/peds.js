// Pedestrians: spawning/despawning around the player, sidewalk wandering on the walk graph,
// reactions (flee, cower, hands up, fight back), gangs with territories, followers, bodies.
import * as THREE from 'three';
import { Character } from '../entities/character.js';
import { randomAppearance } from '../entities/humanoid.js';
import { RNG, rand, randInt, pick, clamp, dist2, wrapAngle, dampAngle } from '../core/utils.js';

export const GANGS = {
  kings: { name: 'Cedar Row Kings', color: 0xf2b705, district: 'hood', friendly: true, weapons: ['pistol', 'bat', 'fist'] },
  vipers: { name: 'Vipers', color: 0xc1121f, district: 'corona', friendly: false, weapons: ['pistol', 'smg', 'bat', 'knife'] },
  cuervos: { name: 'Los Cuervos', color: 0x1f7a8c, district: 'docks', friendly: false, weapons: ['pistol', 'smg', 'shotgun'] },
};

const LINES = {
  bump: ['Watch it!', 'Hey!', 'Excuse you!', 'You blind?', 'Move!'],
  flee: ['Help!', 'Oh my god!', 'Somebody call the cops!', 'Run!', 'He\'s crazy!', 'Aaaah!'],
  fight: ['You want some?', 'Big mistake!', 'Come on then!', 'You\'re dead!'],
  gang: ['Wrong hood, fool!', 'Vipers run this!', 'Get outta here!', 'You lost?'],
  handsUp: ['Take it! Take anything!', 'Don\'t shoot!', 'Please!'],
  jacked: ['My car!', 'Hey, that\'s my ride!', 'Thief!'],
};

export class Ped extends Character {
  constructor(game, appearance, opts = {}) {
    super(game, appearance, opts);
    this.brain = opts.brain || 'civilian';
    this.gang = opts.gang || null;
    this.state = opts.state || 'wander';
    this.stateTime = 0;
    this.walkSpeed = rand(1.15, 1.55);
    this.node = null; this.prevNode = null;
    this.targetPos = new THREE.Vector3();
    this.threat = null;
    this.threatPos = new THREE.Vector3();
    this.brave = Math.random();
    this.fireTimer = 0;
    this.meleeTimer = 0;
    this.thinkTimer = Math.random() * 0.3;
    this.lastSay = -10;
    this.follow = null;
    this.persistent = !!opts.persistent;
    this.missionTag = opts.missionTag || null;
    this.accuracy = opts.accuracy ?? 0.5;
    this.damageMul = opts.damageMul ?? 0.55;
    this.hostile = false;
    this.deathTime = 0;
    this.animLod = 0;
    this.onDamaged = (src, dmg, info) => this.game.peds.onPedDamaged(this, src, dmg, info);
    this.onCarjacked = (by) => { this.say(pick(LINES.jacked)); this.threat = by; this.setState(this.brave > 0.75 ? 'attack' : 'flee'); };
    this.onBumped = (veh) => { if (this.state === 'wander' && this.game.time - this.lastSay > 3) { this.say(pick(LINES.bump)); } };
  }

  say(text) {
    this.lastSay = this.game.time;
    this.game.hud?.speech(this, text);
  }

  setState(s) { if (this.state !== s) { this.state = s; this.stateTime = 0; } }

  // ------------------------------------------------------------------ movement helpers
  goTo(x, z, speed, dt, arriveDist = 0.4) {
    const dx = x - this.pos.x, dz = z - this.pos.z;
    const d = Math.hypot(dx, dz);
    if (d < arriveDist) { this.moveTarget.set(0, 0); return true; }
    const sp = Math.min(speed, d * 3);
    this.moveTarget.set(dx / d * sp, dz / d * sp);
    this.yaw = dampAngle(this.yaw, Math.atan2(dx, dz), 8, dt);
    return false;
  }
  stop() { this.moveTarget.set(0, 0); }

  think(dt) {
    const game = this.game;
    this.stateTime += dt;
    if (this.dead || this.ragdolling || this.vehicle) { this.stop(); this.aiming = false; return; }
    if (this.anim.action && !this.anim.action.done && this.anim.action.name === 'getup') { this.stop(); return; }
    if (this.brain === 'cop') { game.police?.copThink(this, dt); return; }
    if (this.brain === 'script') { this.scriptThink?.(dt); return; }
    const player = game.player;
    this.animState.cower = false;
    this.animState.handsUp = false;
    this.animState.talking = false;
    this.crouching = false;
    this.aiming = false;
    // gang hostility
    if (this.gang && !GANGS[this.gang].friendly && !player.dead && this.state !== 'attack' && this.state !== 'flee') {
      const d2 = dist2(this.pos.x, this.pos.z, player.pos.x, player.pos.z);
      if (d2 < 22 * 22 && (game.peds.gangAggro[this.gang] || d2 < 12 * 12)) {
        this.threat = player; this.setState('attack');
        if (game.time - this.lastSay > 5) this.say(pick(LINES.gang));
      }
    }
    // player aiming at me
    if (this.brain === 'civilian' && player.aiming && !player.vehicle && this.state !== 'flee' && this.state !== 'attack') {
      const d = Math.hypot(this.pos.x - player.pos.x, this.pos.z - player.pos.z);
      if (d < 14) {
        const dir = game.rig.lookDir(new THREE.Vector3());
        const tx = this.pos.x - game.camera.position.x, ty = this.pos.y + 1.2 - game.camera.position.y, tz = this.pos.z - game.camera.position.z;
        const tl = Math.hypot(tx, ty, tz);
        if ((dir.x * tx + dir.y * ty + dir.z * tz) / tl > 0.97) {
          if (this.state !== 'handsup') { this.setState('handsup'); this.say(pick(LINES.handsUp)); }
        }
      }
    }
    switch (this.state) {
      case 'wander': this._wander(dt); break;
      case 'idle':
        this.stop();
        this.animState.talking = this.talkPartner != null;
        if (this.talkPartner) this.faceTowards(this.talkPartner.pos.x, this.talkPartner.pos.z, dt, 3);
        if (this.stateTime > (this.idleTime || 6)) { this.talkPartner = null; this.setState('wander'); }
        break;
      case 'handsup':
        this.stop();
        this.animState.handsUp = true;
        this.faceTowards(player.pos.x, player.pos.z, dt, 6);
        if (!player.aiming || this.stateTime > 8) { this.threat = player; this.threatPos.copy(player.pos); this.setState('flee'); }
        break;
      case 'cower':
        this.stop();
        this.crouching = true;
        this.animState.cower = true;
        if (this.stateTime > (this.cowerTime || 4)) this.setState('flee');
        break;
      case 'flee': this._flee(dt); break;
      case 'attack': this._attack(dt); break;
      case 'follow': this._follow(dt); break;
      case 'guard': this._guard(dt); break;
      case 'goto':
        if (this.goTo(this.targetPos.x, this.targetPos.z, this.gotoSpeed || this.walkSpeed, dt, 0.6)) { this.setState(this.afterGoto || 'guard'); this.onArrive?.(); }
        break;
      default: this.stop();
    }
  }

  _wander(dt) {
    const map = this.game.map;
    if (!this.node) {
      // pick nearest walk node
      let best = null, bd = Infinity;
      const nodes = map.walkNodes;
      const b = map.blockAt(this.pos.x, this.pos.z);
      const cands = b ? b.nodeIds.map((i) => nodes[i]) : nodes;
      for (const n of cands) { const d = dist2(n.x, n.z, this.pos.x, this.pos.z); if (d < bd) { bd = d; best = n; } }
      this.node = best;
      this.nodeOffset = rand(-1.2, 1.2);
      if (!best) { this.stop(); return; }
    }
    const n = this.node;
    const tx = n.x + (this.offX || 0), tz = n.z + (this.offZ || 0);
    if (this.goTo(tx, tz, this.walkSpeed, dt, 0.7)) {
      const nodes = map.walkNodes;
      let opts = n.links.filter((l) => nodes[l] !== this.prevNode);
      if (!opts.length) opts = n.links;
      // cross roads less often
      let next = nodes[pick(opts)];
      if (n.cross && n.cross.includes(next.id) && Math.random() < 0.5) next = nodes[pick(opts)];
      this.prevNode = n;
      this.node = next;
      this.offX = rand(-1.1, 1.1); this.offZ = rand(-1.1, 1.1);
      if (Math.random() < 0.08) { this.setState('idle'); this.idleTime = rand(3, 9); this.stop(); }
    }
  }

  _flee(dt) {
    const game = this.game;
    const tp = this.threat && !this.threat.removed ? (this.threat.vehicle ? this.threat.vehicle.pos : this.threat.pos) : this.threatPos;
    let dx = this.pos.x - tp.x, dz = this.pos.z - tp.z;
    const d = Math.hypot(dx, dz) || 1;
    dx /= d; dz /= d;
    // bias toward sidewalks: blend with direction to a node away from threat
    const sp = 5.4 + this.brave * 1.2;
    // wall avoidance: probe ahead
    const probe = game.collision.resolveCircle(this.pos.x + dx * 1.5, this.pos.z + dz * 1.5, 0.4, this.pos.y + 0.3, 1.4);
    if (probe.hit) { const px = probe.x - (this.pos.x + dx * 1.5), pz = probe.z - (this.pos.z + dz * 1.5); dx += px * 2; dz += pz * 2; const l = Math.hypot(dx, dz) || 1; dx /= l; dz /= l; }
    this.moveTarget.set(dx * sp, dz * sp);
    this.yaw = dampAngle(this.yaw, Math.atan2(dx, dz), 10, dt);
    if (this.stateTime > 9 + this.brave * 4 && d > 40) { this.node = null; this.setState('wander'); this.threat = null; }
    if (this.stateTime > 25) { this.node = null; this.setState('wander'); this.threat = null; }
  }

  _attack(dt) {
    const game = this.game;
    const t = this.threat;
    if (!t || t.dead || t.removed) { this.threat = null; this.node = null; this.setState(this.gang ? 'guard' : 'wander'); return; }
    const tp = t.vehicle ? t.vehicle.pos : t.pos;
    const dx = tp.x - this.pos.x, dz = tp.z - this.pos.z;
    const d = Math.hypot(dx, dz);
    if (d > 60) { this.threat = null; this.setState(this.gang ? 'guard' : 'wander'); return; }
    const def = this.weaponDef;
    if (def.type === 'gun') {
      this.aiming = true;
      this.faceTowards(tp.x, tp.z, dt, 12);
      const aimY = (t.vehicle ? tp.y + 1.0 : tp.y + 1.3) - (this.pos.y + 1.45);
      this.aimPitch = Math.atan2(aimY, d);
      // keep some distance, strafe a bit
      const want = 9 + (this.id % 5);
      if (d > want + 4) this.goTo(tp.x, tp.z, 4.2, dt, 0);
      else if (d < want - 4) this.moveTarget.set(-dx / d * 2, -dz / d * 2);
      else { const s = Math.sin(game.time * 0.8 + this.id) * 1.8; this.moveTarget.set(-dz / d * s, dx / d * s); }
      this.yaw = Math.atan2(dx, dz);
      this.fireTimer -= dt;
      const w = this.weapons[this.weapon];
      if (this.fireTimer <= 0 && this.stateTime > 0.6) {
        const see = game.collision.lineOfSight(this.pos.x, this.pos.y + 1.5, this.pos.z, tp.x, tp.y + 1.3, tp.z);
        if (see) {
          if (w.clip <= 0) { w.clip = def.clip; this.fireTimer = 1.5; this.anim.play('reload'); return; }
          w.clip--;
          this.fireTimer = def.rate * (def.auto ? 1.2 : 2.5) + rand(0, 0.35) + (def.auto && Math.random() < 0.15 ? 0.8 : 0);
          const muzzle = this.muzzleWorld(new THREE.Vector3());
          const target = new THREE.Vector3(tp.x, tp.y + (t.vehicle ? 0.9 : 1.2), tp.z);
          const dir = target.sub(muzzle).normalize();
          const inacc = (1 - this.accuracy) * 0.12 + (Math.hypot(t.vel?.x || 0, t.vel?.z || 0) > 4 ? 0.05 : 0);
          dir.x += rand(-inacc, inacc); dir.y += rand(-inacc, inacc) * 0.6; dir.z += rand(-inacc, inacc); dir.normalize();
          this.anim.recoil = def.recoil;
          game.combat.fireWeapon(this, def, muzzle, dir, { fromMuzzle: true, spreadMul: 1.5 });
        } else if (d > 6) this.goTo(tp.x, tp.z, 4.5, dt, 0);
      }
    } else {
      // melee
      this.faceTowards(tp.x, tp.z, dt, 10);
      if (d > 1.3) this.goTo(tp.x, tp.z, 5.2, dt, 1.0);
      else {
        this.stop();
        this.meleeTimer -= dt;
        if (this.meleeTimer <= 0 && !this.anim.busy && !t.vehicle) {
          this.meleeTimer = rand(0.6, 1.1);
          const act = this.weapon === 'knife' ? 'stab' : this.weapon === 'bat' ? 'swing' : pick(['jab', 'cross', 'jab', 'kick']);
          const a = this.anim.play(act);
          a.onHit = () => game.combat.meleeHit(this, act);
        }
      }
      if (t.vehicle && d < 4 && t.vehicle.speedAbs < 2 && this.brave > 0.6 && !game.vehicles.isBusy(this) && t.isPlayer) {
        // try to drag the player out
        this.meleeTimer -= dt;
      }
    }
  }

  _follow(dt) {
    const game = this.game;
    const L = this.follow;
    if (!L || L.removed) { this.setState('wander'); return; }
    if (L.dead) { this.setState('flee'); return; }
    // get in the leader's car
    if (L.vehicle && !this.vehicle && !game.vehicles.isBusy(this)) {
      const v = L.vehicle;
      const free = [1, 2, 3].find((s) => !v.occupants[s]);
      if (free && dist2(this.pos.x, this.pos.z, v.pos.x, v.pos.z) < 30 * 30) { game.vehicles.enter(this, v, free, { force: true }); return; }
    }
    if (!L.vehicle && this.vehicle) { game.vehicles.exit(this); return; }
    // fight what the leader fights
    if (this.threat && !this.threat.dead && this.weapons[this.weapon] && this.weaponDef.type === 'gun') { this._attack(dt); this.setState('follow'); return; }
    const d = Math.hypot(L.pos.x - this.pos.x, L.pos.z - this.pos.z);
    const side = (this.followSlot || 0) - 1;
    const tx = L.pos.x - Math.sin(L.yaw) * 1.6 + Math.cos(L.yaw) * side * 1.2, tz = L.pos.z - Math.cos(L.yaw) * 1.6 - Math.sin(L.yaw) * side * 1.2;
    const speed = d > 8 ? 6.5 : d > 3 ? 4.3 : 1.5;
    this.goTo(tx, tz, speed, dt, 0.8);
    if (d > 150) { this.setPosition(L.pos.x - Math.sin(L.yaw) * 2, undefined, L.pos.z - Math.cos(L.yaw) * 2); }
  }

  _guard(dt) {
    this.stop();
    this.animState.talking = Math.sin(this.id * 7 + this.game.time * 0.2) > 0.3;
    if (this.guardFace != null) this.yaw = dampAngle(this.yaw, this.guardFace, 3, dt);
  }

  update(dt) {
    this.think(dt);
    super.update(dt);
  }
}

// ======================================================================================
export class PedManager {
  constructor(game) {
    this.game = game;
    this.list = [];
    this.maxPeds = game.quality?.peds ?? 36;
    this.spawnTimer = 0;
    this.gangAggro = { vipers: true, cuervos: false, kings: false };
    this.frustum = new THREE.Frustum();
    this._m = new THREE.Matrix4();
    this.bodies = 0;
    game.events.on('gunshot', (shooter, pos) => this.onNoise(shooter, pos, 45, true));
    game.events.on('explosion', (pos) => this.onNoise(null, pos, 60, true));
    game.events.on('death', (c, info) => this.onDeath(c, info));
    game.events.on('pedHitByCar', (c, v) => { if (v.driver?.isPlayer) this.onNoise(v.driver, c.pos, 25, false); });
    game.events.on('melee', (att, vic) => { if (att.isPlayer) this.onNoise(att, vic.pos, 18, false); });
  }

  spawnPed(x, z, opts = {}) {
    const rng = new RNG((Math.random() * 1e9) | 0);
    let app;
    if (opts.appearance) app = opts.appearance;
    else if (opts.gang) {
      const g = GANGS[opts.gang];
      app = randomAppearance(rng, { female: rng.chance(0.15), shirt: g.color, shirtType: rng.pick(['tee', 'tank', 'jacket', 'long']), jacketColor: 0x1a1a1a, bandana: rng.chance(0.5) ? g.color : null, hairStyle: rng.pick(['cap', 'buzz', 'short', 'bald']), hat: g.color });
    } else app = this._districtLook(rng, this.game.map.districtAt(x, z));
    const p = new Ped(this.game, app, opts);
    p.setPosition(x, opts.y, z);
    p.setYaw(opts.yaw ?? Math.random() * 6.28);
    if (opts.weapon) { p.giveWeapon(opts.weapon, 999); p.equip(opts.weapon); }
    this.list.push(p);
    return p;
  }

  _districtLook(rng, district) {
    const o = {};
    switch (district) {
      case 'beach': o.shorts = rng.chance(0.7); o.shirtType = rng.pick(['tank', 'tee', 'tee']); o.glasses = rng.chance(0.4); break;
      case 'downtown': if (rng.chance(0.5)) { o.shirtType = 'jacket'; o.jacketColor = rng.pick([0x222222, 0x2b2d42, 0x3d405b, 0x555555]); o.shirt = 0xf2f2f2; o.pants = o.jacketColor; o.shoes = 0x111111; } break;
      case 'hills': o.shirtType = rng.pick(['long', 'jacket', 'tee']); o.glasses = rng.chance(0.5); o.shirt = rng.pick([0xffffff, 0xe9d8a6, 0xa8dadc, 0xffc8dd]); break;
      case 'docks': o.shirtType = rng.pick(['long', 'jacket']); o.jacketColor = rng.pick([0xff8800, 0x2a4d69]); o.hairStyle = 'cap'; o.hat = rng.pick([0xffcc00, 0x333333]); o.female = rng.chance(0.1); break;
      default: break;
    }
    return randomAppearance(rng, o);
  }

  // Random civilian near but out of sight of the player
  _spawnAmbient() {
    const game = this.game;
    const map = game.map;
    const p = game.player.vehicle ? game.player.vehicle.pos : game.player.pos;
    const nodes = map.walkNodes;
    const cam = game.camera;
    for (let tries = 0; tries < 12; tries++) {
      const ang = Math.random() * Math.PI * 2;
      const r = rand(55, 95);
      const x = p.x + Math.cos(ang) * r, z = p.z + Math.sin(ang) * r;
      const b = map.blockAt(x, z);
      if (!b) continue;
      // in view? prefer out of view or far
      const inView = this._inView(x, z, 1);
      if (inView && r < 80) continue;
      const n = nodes[pick(b.nodeIds)];
      // spawn on the sidewalk between two nodes
      const n2 = nodes[pick(n.links)];
      const t = Math.random();
      const sx = n.x + (n2.x - n.x) * t + rand(-1, 1), sz = n.z + (n2.z - n.z) * t + rand(-1, 1);
      if (map.isOnRoad(sx, sz) && !(n.cross && n.cross.includes(n2.id))) continue;
      const district = b.district;
      // gangs in their turf
      for (const gid in GANGS) {
        const g = GANGS[gid];
        if (g.district === district && Math.random() < (game.missions?.gangDensity?.[gid] ?? 0.3)) {
          const count = randInt(2, 4);
          const face = Math.random() * 6.28;
          for (let k = 0; k < count; k++) {
            const ped = this.spawnPed(sx + Math.cos(k * 2.1) * 1.4, sz + Math.sin(k * 2.1) * 1.4, { brain: 'gang', gang: gid, state: 'guard', weapon: pick(g.weapons), health: 110 });
            ped.guardFace = Math.atan2(sx - ped.pos.x, sz - ped.pos.z);
            ped.accuracy = 0.45;
          }
          return;
        }
      }
      const ped = this.spawnPed(sx, sz, {});
      ped.node = n2; ped.prevNode = n;
      // occasional conversation pair
      if (Math.random() < 0.15 && this.list.length < this.maxPeds - 1) {
        const other = this.spawnPed(sx + 1.2, sz + 0.3, {});
        ped.setState('idle'); other.setState('idle');
        ped.idleTime = other.idleTime = rand(6, 16);
        ped.talkPartner = other; other.talkPartner = ped;
      }
      return;
    }
  }

  _inView(x, z, margin = 0) {
    const cam = this.game.camera;
    this._m.multiplyMatrices(cam.projectionMatrix, cam.matrixWorldInverse);
    this.frustum.setFromProjectionMatrix(this._m);
    return this.frustum.containsPoint(new THREE.Vector3(x, 1, z));
  }

  density() {
    const game = this.game;
    const h = game.env.hours;
    const night = h < 6 || h > 22 ? 0.45 : h < 8 || h > 20 ? 0.75 : 1;
    const rain = 1 - game.env.rain * 0.5;
    const d = game.map.districtAt(game.player.pos.x, game.player.pos.z);
    const dm = { downtown: 1.2, beach: 1.15, midtown: 1.1, hood: 0.9, corona: 0.9, westside: 0.9, docks: 0.55, hills: 0.4 }[d] || 1;
    return clamp(night * rain * dm, 0.2, 1.3);
  }

  onNoise(src, pos, radius, gunfire) {
    for (const p of this.list) {
      if (p.dead || p.ragdolling || p.vehicle || p === src) continue;
      if (p.brain === 'cop' || p.brain === 'script' || p.state === 'follow') continue;
      const d2 = dist2(p.pos.x, p.pos.z, pos.x, pos.z);
      if (d2 > radius * radius) continue;
      if (p.gang && src && src.isPlayer) {
        const g = GANGS[p.gang];
        if (!g.friendly || d2 < 15 * 15) { p.threat = src; p.setState(g.friendly && gunfire ? 'flee' : 'attack'); if (!g.friendly) this.gangAggro[p.gang] = true; continue; }
      }
      if (p.state === 'attack') continue;
      p.threat = src; p.threatPos.copy(pos);
      if (gunfire && d2 < 12 * 12 && Math.random() < 0.35) { p.setState('cower'); p.cowerTime = rand(2, 5); }
      else { p.setState('flee'); if (Math.random() < 0.3 && this.game.time - p.lastSay > 4) p.say(pick(LINES.flee)); }
    }
  }

  onPedDamaged(ped, src, dmg, info) {
    if (ped.dead) return;
    if (!src || src === ped) return;
    if (ped.brain === 'cop' || ped.brain === 'script') { ped.threat = src; return; }
    if (ped.state === 'follow') { ped.threat = src; return; }
    if (ped.gang) {
      ped.threat = src; ped.setState('attack');
      // gang backup
      for (const p of this.list) if (p.gang === ped.gang && !p.dead && dist2(p.pos.x, p.pos.z, ped.pos.x, ped.pos.z) < 30 * 30) { p.threat = src; p.setState('attack'); }
      if (src.isPlayer) this.gangAggro[ped.gang] = true;
      return;
    }
    if (ped.brave > 0.78 && info.type === 'melee') { ped.threat = src; ped.setState('attack'); if (ped.game.time - ped.lastSay > 3) ped.say(pick(LINES.fight)); }
    else { ped.threat = src; ped.threatPos.copy(src.pos); ped.setState('flee'); if (Math.random() < 0.5) ped.say(pick(LINES.flee)); }
  }

  onDeath(c, info) {
    if (c.isPlayer) return;
    c.deathTime = this.game.time;
    // witnesses flee
    this.onNoise(info?.source || null, c.pos, 25, false);
    // cash drop
    if (Math.random() < (c.gang ? 0.7 : 0.35)) this.game.pickups?.dropMoney(c.pos, c.gang ? randInt(20, 120) : randInt(5, 60));
    if (c.gang && c.weapon !== 'fist' && Math.random() < 0.5) this.game.pickups?.dropWeapon(c.pos, c.weapon, c.weapon === 'bat' || c.weapon === 'knife' ? 1 : randInt(8, 30));
    if (c.brain === 'cop' && Math.random() < 0.8) this.game.pickups?.dropWeapon(c.pos, c.weapon === 'fist' ? 'pistol' : c.weapon, randInt(10, 30));
  }

  remove(p) {
    const i = this.list.indexOf(p);
    if (i >= 0) this.list.splice(i, 1);
    if (p.vehicle) { const v = p.vehicle; const s = v.occupants.indexOf(p); if (s >= 0) v.occupants[s] = null; p.vehicle = null; }
    p.remove();
  }

  update(dt) {
    const game = this.game;
    const pl = game.player;
    const pp = pl.vehicle ? pl.vehicle.pos : pl.pos;
    // spawn
    this.spawnTimer -= dt;
    const ambientCount = this.list.filter((p) => !p.persistent && p.brain !== 'cop' && !p.vehicle).length;
    const target = Math.floor(this.maxPeds * this.density());
    if (this.spawnTimer <= 0 && ambientCount < target && !game.disableAmbient) {
      this.spawnTimer = 0.25;
      this._spawnAmbient();
    }
    // frustum for visibility culling
    const cam = game.camera;
    this._m.multiplyMatrices(cam.projectionMatrix, cam.matrixWorldInverse);
    this.frustum.setFromProjectionMatrix(this._m);
    const sphere = new THREE.Sphere(new THREE.Vector3(), 1.4);
    // update & despawn
    for (let i = this.list.length - 1; i >= 0; i--) {
      const p = this.list[i];
      if (p.removed) { this.list.splice(i, 1); continue; }
      const cx = p.ragdolling ? p.ragdoll.pos[0] : p.pos.x, cz = p.ragdolling ? p.ragdoll.pos[2] : p.pos.z;
      const d2 = dist2(cx, cz, pp.x, pp.z);
      if (!p.persistent && !p.vehicle) {
        if (d2 > 150 * 150 || (p.dead && game.time - p.deathTime > 60 && d2 > 40 * 40)) { this.remove(p); continue; }
      }
      if (p.vehicle) { p.root.visible = true; p.update(dt); continue; }
      sphere.center.set(cx, (p.ragdolling ? p.ragdoll.pos[1] : p.pos.y) + 0.9, cz);
      const vis = this.frustum.intersectsSphere(sphere);
      p.root.visible = vis;
      // animation LOD: far or invisible peds update less often
      p.animLod = (p.animLod + 1) % (d2 > 70 * 70 || !vis ? 3 : 1);
      if (p.animLod === 0) p.update(dt * (d2 > 70 * 70 || !vis ? 3 : 1));
      else p.think(dt);
    }
    // separation
    const L = this.list;
    for (let i = 0; i < L.length; i++) {
      const a = L[i];
      if (a.vehicle || a.ragdolling) continue;
      for (let j = i + 1; j < L.length; j++) {
        const b = L[j];
        if (b.vehicle || b.ragdolling) continue;
        const dx = b.pos.x - a.pos.x, dz = b.pos.z - a.pos.z;
        const d2 = dx * dx + dz * dz;
        if (d2 < 0.36 && d2 > 1e-6) { const d = Math.sqrt(d2), push = (0.6 - d) * 0.5; a.pos.x -= dx / d * push; a.pos.z -= dz / d * push; b.pos.x += dx / d * push; b.pos.z += dz / d * push; }
      }
      // vs player
      if (!pl.vehicle && !pl.ragdolling) {
        const dx = pl.pos.x - a.pos.x, dz = pl.pos.z - a.pos.z;
        const d2 = dx * dx + dz * dz;
        if (d2 < 0.4 && d2 > 1e-6) {
          const d = Math.sqrt(d2), push = 0.63 - d; a.pos.x -= dx / d * push * 0.7; a.pos.z -= dz / d * push * 0.7; pl.pos.x += dx / d * push * 0.3; pl.pos.z += dz / d * push * 0.3;
          if (Math.hypot(pl.vel.x, pl.vel.z) > 5 && a.state === 'wander' && Math.random() < 0.05 && !a.dead) { a.knockDown(new THREE.Vector3(pl.vel.x * 0.4, 1, pl.vel.z * 0.4)); a.say(pick(LINES.bump)); }
        }
      }
    }
  }
}

export { LINES };
