// Base character: skinned humanoid + procedural animator + ragdoll + simple kinematic physics.
import * as THREE from 'three';
import { createHumanoid, B } from './humanoid.js';
import { Animator } from './animator.js';
import { Ragdoll } from './ragdoll.js';
import { WEAPONS, createWeaponMesh, MUZZLE } from '../game/weapondefs.js';
import { WATER_Y } from '../world/citymap.js';
import { clamp, dampAngle, wrapAngle } from '../core/utils.js';

const HOLD = {
  knife: { p: [0, -0.07, 0.02], r: [Math.PI / 2, 0, 0] },
  bat: { p: [0, -0.07, 0.02], r: [Math.PI / 2 + 0.3, 0, 0] },
  pistol: { p: [0, -0.075, 0.02], r: [Math.PI / 2, 0, 0] },
  smg: { p: [0, -0.075, 0.02], r: [Math.PI / 2, 0, 0] },
  shotgun: { p: [0, -0.075, 0.02], r: [Math.PI / 2, 0, 0] },
  rifle: { p: [0, -0.075, 0.02], r: [Math.PI / 2, 0, 0] },
  rpg: { p: [0, -0.08, 0.02], r: [Math.PI / 2, 0, 0] },
  grenade: { p: [0, -0.08, 0.03], r: [0, 0, 0] },
};

const _v = new THREE.Vector3();
let nextId = 1;

export class Character {
  constructor(game, appearance, opts = {}) {
    this.id = nextId++;
    this.game = game;
    this.appearance = appearance;
    const h = createHumanoid(appearance);
    this.root = h.root; this.mesh = h.mesh; this.bones = h.bones;
    this.mesh.userData.character = this;
    this.anim = new Animator(h.bones, h.rest);
    this.ragdoll = null;
    this.pos = this.root.position;
    this.yaw = 0;
    this.vel = new THREE.Vector3();
    this.moveTarget = new THREE.Vector2();
    this.grounded = true;
    this.swimming = false;
    this.health = opts.health ?? 100;
    this.maxHealth = this.health;
    this.armor = opts.armor ?? 0;
    this.dead = false;
    this.ragdolling = false;
    this.downTime = 0;
    this.radius = 0.32;
    this.height = 1.8 * (appearance.height || 1);
    this.team = opts.team || 'civilian';
    this.weapons = { fist: { ammo: Infinity, clip: Infinity } };
    this.weapon = 'fist';
    this.weaponMesh = null;
    this.vehicle = null;
    this.seat = -1;
    this.isPlayer = false;
    this.aiming = false;
    this.aimPitch = 0;
    this.crouching = false;
    this.animState = { speed: 0, moveAngle: 0, grounded: true, vy: 0, aim: false, aimPitch: 0, weapon: 'none', crouch: false, sit: 0, steer: 0, swim: false, cower: false, handsUp: false, talking: false, lookYaw: 0, lookPitch: 0 };
    this.lastDamager = null;
    this.lastHitTime = -10;
    this.removed = false;
    this.visible = true;
    game.scene.add(this.root);
  }

  setPosition(x, y, z) {
    this.pos.set(x, y ?? this.game.map.groundHeight(x, z), z);
    this.vel.set(0, 0, 0);
  }
  setYaw(y) { this.yaw = y; this.root.rotation.y = y; }
  get forward() { return _v.set(Math.sin(this.yaw), 0, Math.cos(this.yaw)); }
  get headPos() { return new THREE.Vector3(this.pos.x, this.pos.y + this.height * 0.93, this.pos.z); }
  get chestPos() { return new THREE.Vector3(this.pos.x, this.pos.y + this.height * 0.72, this.pos.z); }

  giveWeapon(id, ammo = 0) {
    const def = WEAPONS[id];
    if (!def) return;
    if (!this.weapons[id]) this.weapons[id] = { ammo: 0, clip: 0 };
    const w = this.weapons[id];
    if (def.type === 'melee') { w.ammo = Infinity; w.clip = Infinity; return; }
    w.ammo += ammo;
    if (w.clip === 0) { const take = Math.min(def.clip, w.ammo); w.clip = take; w.ammo -= take; }
  }

  equip(id) {
    if (!this.weapons[id]) return;
    this.weapon = id;
    if (this.weaponMesh) { this.weaponMesh.parent?.remove(this.weaponMesh); this.weaponMesh = null; }
    const m = createWeaponMesh(id);
    if (m) {
      const h = HOLD[id] || HOLD.pistol;
      m.position.set(...h.p);
      m.rotation.set(...h.r);
      this.bones[B.rHand].add(m);
      this.weaponMesh = m;
    }
  }

  muzzleWorld(out = new THREE.Vector3()) {
    const mz = MUZZLE[this.weapon];
    if (this.weaponMesh && mz) {
      this.root.updateMatrixWorld(true);
      return this.weaponMesh.localToWorld(out.set(mz[0], mz[1], mz[2]));
    }
    return out.copy(this.chestPos).addScaledVector(this.forward, 0.4);
  }

  get weaponDef() { return WEAPONS[this.weapon]; }
  get holdType() {
    const d = WEAPONS[this.weapon];
    return d ? d.hold : 'none';
  }

  // ------------------------------------------------------------------ physics & animation
  update(dt) {
    if (this.removed) return;
    const act = this.anim.action;
    if (act && !act.hitDone && act.hitTime >= 0 && act.t >= act.hitTime) { act.hitDone = true; act.onHit?.(); }
    if (this.vehicle) {
      this.animState.sit = this.seat === 0 ? 1 : 2;
      this.animState.speed = 0;
      this.animState.grounded = true;
      this.animState.swim = false;
      this.animState.aim = this.aiming && this.weaponDef.type === 'gun';
      this.animState.weapon = this.holdType;
      this.anim.update(dt, this.animState);
      return;
    }
    this.animState.sit = 0;
    if (this.ragdolling) {
      this.ragdoll.update(dt);
      this.ragdoll.apply();
      this.downTime += dt;
      if (!this.dead && (this.ragdoll.settled || this.downTime > 3.5) && this.downTime > 1.2) this.getUp();
      return;
    }
    this.physics(dt);
    this.root.rotation.y = this.yaw;
    const st = this.animState;
    const hs = Math.hypot(this.vel.x, this.vel.z);
    st.speed = hs;
    st.moveAngle = hs > 0.2 ? wrapAngle(Math.atan2(this.vel.x, this.vel.z) - this.yaw) : 0;
    st.grounded = this.grounded;
    st.vy = this.vel.y;
    st.swim = this.swimming;
    st.aim = this.aiming && (this.weaponDef.type === 'gun' || this.weaponDef.type === 'launcher');
    st.aimPitch = this.aimPitch;
    st.weapon = this.holdType;
    st.crouch = this.crouching && !this.swimming;
    if (this.weaponMesh) this.weaponMesh.visible = !this.swimming;
    this.anim.update(dt, st);
  }

  physics(dt) {
    const map = this.game.map;
    const accel = this.swimming ? 6 : this.grounded ? 24 : 3;
    const tx = this.moveTarget.x, tz = this.moveTarget.y;
    const dvx = tx - this.vel.x, dvz = tz - this.vel.z;
    const dl = Math.hypot(dvx, dvz);
    const maxDv = accel * dt;
    if (dl > maxDv) { this.vel.x += dvx / dl * maxDv; this.vel.z += dvz / dl * maxDv; }
    else { this.vel.x = tx; this.vel.z = tz; }
    if (!this.grounded && !this.swimming) this.vel.y -= 19 * dt;
    const oldY = this.pos.y;
    this.pos.x += this.vel.x * dt;
    this.pos.z += this.vel.z * dt;
    this.pos.y += this.vel.y * dt;
    // static collision
    const res = this.game.collision.resolveCircle(this.pos.x, this.pos.z, this.radius, this.pos.y + 0.3, 1.5);
    if (res.hit) {
      this.pos.x = res.x; this.pos.z = res.z;
      this.onWallHit?.(res.hit);
    }
    // ground
    const gh = map.groundHeight(this.pos.x, this.pos.z);
    const depth = WATER_Y - gh;
    if (depth > 1.35 && this.pos.y < WATER_Y - 0.85) {
      if (!this.swimming) this.onEnterWater?.();
      this.swimming = true;
      this.grounded = false;
      this.pos.y = WATER_Y - 1.05;
      this.vel.y = 0;
      return;
    }
    if (this.swimming && depth < 1.2) { this.swimming = false; this.pos.y = gh; }
    if (this.swimming) return;
    if (this.pos.y <= gh + 0.001 || (this.grounded && this.pos.y - gh < 0.45 && this.vel.y <= 0.01)) {
      if (!this.grounded && this.vel.y < -13) this.onHardLanding?.(-this.vel.y);
      if (gh - oldY > 0.6 && this.grounded) {
        // too high a step: treat as wall (terrain cliffs)
        this.pos.x -= this.vel.x * dt; this.pos.z -= this.vel.z * dt;
        this.vel.x *= 0.2; this.vel.z *= 0.2;
      } else {
        this.pos.y = gh;
      }
      this.vel.y = Math.max(0, this.vel.y);
      this.grounded = true;
    } else {
      this.grounded = false;
    }
  }

  jump(v = 6.2) {
    if (!this.grounded || this.swimming || this.vehicle || this.ragdolling) return false;
    this.vel.y = v;
    this.grounded = false;
    this.pos.y += 0.05;
    return true;
  }

  faceTowards(x, z, dt, rate = 10) {
    const target = Math.atan2(x - this.pos.x, z - this.pos.z);
    this.yaw = dt > 0 ? dampAngle(this.yaw, target, rate, dt) : target;
  }

  // ------------------------------------------------------------------ damage
  takeDamage(amount, info = {}) {
    if (this.dead) return false;
    if (this.invincible) return false;
    let dmg = amount;
    if (info.part === 'head') dmg *= info.headMul ?? 4;
    if (info.part === 'limb') dmg *= 0.7;
    if (this.armor > 0 && info.type !== 'fall' && info.type !== 'drown') {
      const absorb = Math.min(this.armor, dmg * 0.8);
      this.armor -= absorb;
      dmg -= absorb;
    }
    this.health -= dmg;
    this.lastDamager = info.source || null;
    this.lastHitTime = this.game.time;
    if (info.source && this.onDamaged) this.onDamaged(info.source, dmg, info);
    if (this.health <= 0) {
      this.health = 0;
      this.die(info);
      return true;
    }
    if (!this.ragdolling && info.type !== 'fire') {
      if (info.knockdown) this.knockDown(info.impulse || new THREE.Vector3());
      else this.anim.play('flinch');
    }
    return false;
  }

  die(info = {}) {
    if (this.dead) return;
    this.dead = true;
    this.health = 0;
    if (this.vehicle) this.vehicle.ejectOccupant?.(this, true);
    const imp = info.impulse || new THREE.Vector3();
    this.startRagdoll(imp, info.hitPoint);
    if (this.weaponMesh) this.weaponMesh.visible = false;
    this.onDeath?.(info);
    this.game.events?.emit('death', this, info);
  }

  startRagdoll(impulse, hitPoint) {
    if (!this.ragdoll) this.ragdoll = new Ragdoll(this, { map: this.game.map, collision: this.game.collision });
    this.root.rotation.y = this.yaw;
    this.root.updateMatrixWorld(true);
    let pi = -1;
    if (hitPoint) {
      this.ragdoll.start(this.vel, null);
      pi = this.ragdoll.nearestParticle(hitPoint.x, hitPoint.y, hitPoint.z);
      this.ragdoll.push(pi, impulse.x, impulse.y, impulse.z);
    } else {
      this.ragdoll.start(this.vel, impulse);
    }
    this.ragdolling = true;
    this.downTime = 0;
    this.anim.action = null;
  }

  knockDown(impulse) {
    if (this.dead) return;
    if (this.vehicle) return;
    this.startRagdoll(impulse);
    this.onKnockedDown?.();
  }

  getUp() {
    const rd = this.ragdoll;
    const p = rd.pos;
    const hx = p[0], hz = p[2];
    // head direction relative to hips
    const dx = p[3 * 3] - hx, dz = p[3 * 3 + 2] - hz;
    const faceUp = true;
    const yaw = Math.atan2(-dx, -dz);
    this.ragdolling = false;
    this.pos.set(hx, this.game.map.groundHeight(hx, hz), hz);
    this.yaw = yaw; this.root.rotation.y = yaw;
    this.vel.set(0, 0, 0);
    this.anim.beginBlend(0.3);
    if (faceUp) this.anim.play('getup');
    this.onGotUp?.();
  }

  get isDown() { return this.ragdolling || this.dead; }

  // Hit test against a ray: returns {t, part} or null. Uses capsules for body & sphere for head.
  rayHit(ox, oy, oz, dx, dy, dz, maxT) {
    if (this.removed) return null;
    if (this.ragdolling) {
      // test ragdoll particles as spheres
      const rp = this.ragdoll.pos;
      let best = null;
      for (let i = 0; i < 18; i++) {
        const cx = rp[i * 3], cy = rp[i * 3 + 1], cz = rp[i * 3 + 2];
        const r = i === 3 ? 0.14 : i < 2 ? 0.2 : 0.12;
        const t = sphereT(ox, oy, oz, dx, dy, dz, cx, cy, cz, r);
        if (t >= 0 && t < maxT && (!best || t < best.t)) best = { t, part: i === 3 ? 'head' : i < 3 ? 'torso' : 'limb', particle: i };
      }
      return best;
    }
    const h = this.height;
    const base = this.pos.y + (this.vehicle ? 0.35 : 0);
    // head sphere
    const hp = this.vehicle ? this.bones[B.head].getWorldPosition(_v) : _v.set(this.pos.x, base + h * 0.93, this.pos.z);
    const th = sphereT(ox, oy, oz, dx, dy, dz, hp.x, hp.y, hp.z, 0.14);
    // body: vertical capsule approximated by cylinder
    let tb = -1;
    if (!this.vehicle) {
      const r = this.crouching ? 0.36 : 0.3;
      const top = base + (this.crouching ? h * 0.62 : h * 0.84), bot = base;
      tb = cylT(ox, oy, oz, dx, dy, dz, this.pos.x, this.pos.z, r, bot, top);
    }
    let best = null;
    if (th >= 0 && th < maxT) best = { t: th, part: 'head' };
    if (tb >= 0 && tb < maxT && (!best || tb < best.t - 0.05)) {
      const hy = oy + dy * tb - base;
      best = { t: tb, part: hy < h * 0.5 ? 'limb' : 'torso' };
    }
    return best;
  }

  remove() {
    if (this.removed) return;
    this.removed = true;
    this.root.parent?.remove(this.root);
  }
}

function sphereT(ox, oy, oz, dx, dy, dz, cx, cy, cz, r) {
  const lx = ox - cx, ly = oy - cy, lz = oz - cz;
  const b = lx * dx + ly * dy + lz * dz;
  const c = lx * lx + ly * ly + lz * lz - r * r;
  const h = b * b - c;
  if (h < 0) return -1;
  const t = -b - Math.sqrt(h);
  return t >= 0 ? t : -1;
}
function cylT(ox, oy, oz, dx, dy, dz, cx, cz, r, y0, y1) {
  const lx = ox - cx, lz = oz - cz;
  const a = dx * dx + dz * dz;
  if (a < 1e-9) return -1;
  const b = 2 * (lx * dx + lz * dz);
  const c = lx * lx + lz * lz - r * r;
  const disc = b * b - 4 * a * c;
  if (disc < 0) return -1;
  const t = (-b - Math.sqrt(disc)) / (2 * a);
  if (t < 0) return -1;
  const y = oy + dy * t;
  if (y < y0 || y > y1) return -1;
  return t;
}
