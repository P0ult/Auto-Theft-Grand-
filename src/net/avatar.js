// Another player, as seen here: a character posed from the network state (position, heading, animation,
// weapon, seat) instead of simulated. Damage the local player deals to it is sent to its owner (who keeps
// their own health); everything else that would hurt it here (local traffic, local cops) is ignored.
import * as THREE from 'three';
import { Character } from '../entities/character.js';
import { WEAPONS } from '../game/weapondefs.js';
import { randomAppearance } from '../entities/humanoid.js';
import { clamp } from '../core/utils.js';

const STYLES = { hairStyle: ['short', 'bald', 'afro', 'cap', 'buzz', 'long', 'ponytail', 'bun'], shirtType: ['tee', 'long', 'tank', 'jacket'] };
const COLORS = ['skin', 'hair', 'shirt', 'pants', 'shoes', 'hat', 'jacketColor', 'bandana'];

// Only well-formed appearance fields from the network (it's someone else's data)
export function sanitizeAppearance(ap) {
  const base = randomAppearance();
  if (!ap || typeof ap !== 'object') return base;
  const out = { ...base };
  for (const k of COLORS) { const v = ap[k]; if (v === null && (k === 'hat' || k === 'bandana')) out[k] = null; else if (Number.isInteger(v) && v >= 0 && v <= 0xffffff) out[k] = v; }
  for (const [k, list] of Object.entries(STYLES)) if (list.includes(ap[k])) out[k] = ap[k];
  for (const k of ['female', 'shorts', 'glasses', 'beard']) if (typeof ap[k] === 'boolean') out[k] = ap[k];
  if (Number.isFinite(ap.build)) out.build = clamp(ap.build, 0.85, 1.25);
  if (Number.isFinite(ap.height)) out.height = clamp(ap.height, 0.9, 1.1);
  out.uniform = null;
  return out;
}

export class RemoteAvatar extends Character {
  constructor(game, net, peerId, appearance) {
    super(game, appearance, { team: 'remote' });
    this.net = net;
    this.peerId = peerId;
    this.remote = true;
    this.invincible = false;
    this.lastAct = 0;
    this.shownHp = 100;
  }

  setWeapon(id) {
    if (!WEAPONS[id] || id === this.weapon) return;
    if (!this.weapons[id]) this.giveWeapon(id, 1);
    this.equip(id);
  }

  // Posed by NetSystem each frame; just animate here.
  update(dt) {
    if (this.removed) return;
    if (this.vehicle) {
      const st = this.animState;
      st.sit = this.seat === 0 ? 1 : 2; st.speed = 0; st.grounded = true; st.swim = false;
      st.weapon = this.holdType;
      this.anim.update(dt, st);
      return;
    }
    if (this.ragdolling) { this.ragdoll.update(dt); this.ragdoll.apply(); return; }
    this.animState.sit = 0;
    this.root.rotation.y = this.yaw;
    if (this.weaponMesh) this.weaponMesh.visible = !this.swimming;
    this.anim.update(dt, this.animState);
    this._orientWeapon();
  }

  // Hits: only what the local player does to them counts, and it's their client that applies it.
  takeDamage(amount, info = {}) {
    if (this.dead) return false;
    const g = this.game, p = g.player;
    const src = info.source;
    const mine = src === p || (src && src === p.vehicle) || (src?.driver === p) || info.byPlayer;
    if (!mine) return false;
    let dmg = amount;
    if (info.part === 'head') dmg *= info.headMul ?? 4;
    if (info.part === 'limb') dmg *= 0.7;
    this.net.sendHit(this.peerId, dmg, info);
    this.lastHitTime = g.time;
    if (!this.ragdolling && !this.vehicle && info.type !== 'fire') this.anim.play('flinch');
    return false;
  }

  knockDown() { /* their own client decides; the state tells us when they're down */ }

  // Their client says they're down / dead / back up
  setDown(down, dead) {
    if (down && !this.ragdolling) {
      if (this.vehicle) return;
      this.startRagdoll(new THREE.Vector3(), null);
    } else if (!down && this.ragdolling) {
      this.ragdolling = false;
      this.anim.beginBlend(0.25);
    }
    this.dead = dead;
  }
}
