// Pets: adopt a dog or a cat at Pet Palace. Your pet follows you (at heel, running to catch up, sitting when
// you stop), hops into the passenger seat when you drive off, and — dogs — goes for anyone who hurts you, or
// whoever you're aiming at when you whistle (K, or click the right stick). A whistle with nobody targeted
// tells it to stay / come. Other players see your pet too.
import * as THREE from 'three';
import { Animal, BREEDS, PET_BREEDS } from '../entities/animals.js';
import { followOwner } from './wildlife.js';
import { pick, clamp } from '../core/utils.js';

const NAMES = {
  dog: ['Rex', 'Chop', 'Buddy', 'Luna', 'Max', 'Bella', 'Rocky', 'Daisy', 'Duke', 'Coco', 'Bruno', 'Nala', 'Tank', 'Biscuit'],
  cat: ['Mittens', 'Salem', 'Whiskers', 'Luna', 'Oliver', 'Cleo', 'Tiger', 'Smokey', 'Pepper'],
};
const _v = new THREE.Vector3(), _d = new THREE.Vector3();

export class PetSystem {
  constructor(game) {
    this.game = game;
    this.pet = null;          // our pet (an Animal)
    this.info = null;         // { breed, name } (kept after it dies, so the HUD / save know)
    this.stay = false;
    this.target = null;       // who the dog is going for
    this.remote = new Map();  // peer id -> stand-in for another player's pet
    game.events.on('enteredVehicle', (c, v) => { if (c === game.player) this._board(v); });
    game.events.on('exitedVehicle', (c, v) => { if (c === game.player) this._alight(v); });
  }

  animals() {
    const out = [];
    if (this.pet && !this.pet.removed) out.push(this.pet);
    for (const a of this.remote.values()) out.push(a);
    return out;
  }

  adopt(breed, name) {
    const g = this.game, p = g.player;
    if (!BREEDS[breed]) return null;
    this.release();
    const kind = BREEDS[breed].species;
    const nm = name || pick(NAMES[kind] || NAMES.dog);
    const f = new THREE.Vector3(Math.sin(p.yaw), 0, Math.cos(p.yaw));
    const x = p.pos.x + f.x * 1.3, z = p.pos.z + f.z * 1.3;
    const a = new Animal(g, breed, x, z, { y: g.collision.floorHeight(x, z, p.pos.y + 1), yaw: p.yaw + Math.PI });
    a.pet = true; a.owner = p; a.petName = nm;
    a.health = a.maxHealth = a.maxHealth * 3;
    a.onHurt = () => { if (a.kind === 'dog') g.audio?.playAt('yelp', a.pos, 0.8); else g.audio?.playAt('meow', a.pos, 0.8); };
    this.pet = a; this.info = { breed, name: nm }; this.stay = false; this.target = null; this._mourned = false;
    g.audio?.playAt(kind === 'dog' ? 'bark' : 'meow', a.pos, 0.9);
    g.events.emit('petAdopted', a);
    return a;
  }

  release() {
    if (this.pet) { if (this.pet.inVehicle) this.pet.inVehicle.petSeat = null; this.pet.remove(); }
    this.pet = null; this.info = null; this.target = null;
  }

  // the whistle: set the dog on whoever you're aiming at, else stay / come
  command() {
    const g = this.game, p = g.player, a = this.pet;
    if (!a || a.dead) { if (!a) g.hud?.help('You don\'t have a pet. Pet Palace (paw icon on the map) has dogs and cats looking for a home.', 5); return; }
    g.audio?.play('whistle');
    if (p.aiming && a.kind === 'dog' && !p.vehicle) {
      const o = g.rig.cam.position, dir = g.rig.lookDir(_d);
      const hit = g.combat.raycast(o.x, o.y, o.z, dir.x, dir.y, dir.z, 60, p);
      if (hit && hit.kind === 'char' && !hit.obj.dead && !hit.obj.isPlayer) {
        this.target = hit.obj; this.stay = false;
        g.hud?.help(`<b>${a.petName}</b>: get 'em!`, 2);
        return;
      }
    }
    this.target = null;
    this.stay = !this.stay;
    g.hud?.help(this.stay ? `<b>${a.petName}</b>: stay.` : `<b>${a.petName}</b>: come!`, 2);
  }

  update(dt) {
    const g = this.game, p = g.player, a = this.pet;
    if (g.input.hit('pet') && g.gameplay?.state === 'playing' && !g.cutscene) this.command();
    if (a) {
      if (a.removed) { this.pet = null; }
      else {
        if (a.dead) {
          if (!this._mourned) { this._mourned = true; this._deadT = 0; g.hud?.help(`<b>${a.petName}</b> didn't make it. Pet Palace has more looking for a home.`, 6); if (a.inVehicle) { const v = a.inVehicle; v.petSeat = null; a.getOut(v.pos.x + 2, v.pos.z); a.die(); } }
          this._deadT += dt;
          if (this._deadT > 25) { a.remove(); this.pet = null; this.info = null; }
        } else this._think(a, dt);
        if (this.pet) a.update(dt);
      }
    }
    this._updateRemote(dt);
  }

  _think(a, dt) {
    const g = this.game, p = g.player;
    if (a.inVehicle) {
      if (a.inVehicle.exploded) { a.inVehicle.petSeat = null; const v = a.inVehicle; a.getOut(v.pos.x + 2.5, v.pos.z); a.die(); }
      return;
    }
    // someone hurt us: the dog goes for them
    if (a.kind === 'dog' && p.lastDamager && p.lastDamager !== a && g.time - (p.lastHitTime ?? -9) < 0.6) {
      const src = p.lastDamager;
      if (src.pos && !src.isPlayer && !src.dead && !src.def && (src.remote ? !!src.npcProxy : true)) this.target = src;
    }
    const t = this.target;
    if (t) {
      const tp = t.ragdolling && t.ragdoll ? t.ragdoll.center : t.pos;
      const d = Math.hypot(tp.x - a.pos.x, tp.z - a.pos.z);
      if (t.dead || t.removed || t.vehicle || d > 70) { this.target = null; }
      else {
        a.state = 'follow'; a.happy = false;
        if (d > 1.1) a.goTo(tp.x, tp.z, a.sp.run);
        else {
          a.stop();
          a.yaw = Math.atan2(tp.x - a.pos.x, tp.z - a.pos.z);
          a.biteT = (a.biteT || 0) - dt;
          if (a.biteT <= 0) {
            a.biteT = 0.85;
            const imp = new THREE.Vector3(Math.sin(a.yaw) * 2, 1.2, Math.cos(a.yaw) * 2);
            t.takeDamage?.(a.scale < 0.7 ? 5 : 13, { source: a, type: 'melee', part: 'limb', impulse: imp, knockdown: Math.random() < 0.3 && !t.isPlayer, hitPoint: tp });
            g.audio?.playAt('bark', a.pos, 0.8);
          }
        }
        return;
      }
    }
    if (this.stay) { a.stop(); a.state = 'sit'; return; }
    // riding with you on a bike or in a plane isn't a thing: run alongside, catch up out of sight
    followOwner(a, p, dt, { sit: true, teleport: p.vehicle ? 45 : 70 });
    if (Math.random() < dt * 0.03 && a.kind === 'dog' && a.speed < 0.2) g.audio?.playAt('bark', a.pos, 0.6);
  }

  // you got in a car: the pet hops in the passenger side
  _board(v) {
    const a = this.pet;
    if (!a || a.dead || a.inVehicle || this.stay || v.def.bike || v.def.kind) return;
    if (Math.hypot(a.pos.x - v.pos.x, a.pos.z - v.pos.z) > 25) return;
    const n = v.model?.seats?.length || 2;
    const seat = [1, 2, 3].find((k) => k < n && !v.occupants[k]);
    if (seat == null) return;
    v.petSeat = seat;
    a.sitIn(v, seat);
  }
  _alight(v) {
    const a = this.pet;
    if (!a || a.inVehicle !== v) return;
    v.petSeat = null;
    const out = v.localToWorld(-(v.hx + 0.9), 0, 0.2, _v);
    a.getOut(out.x, out.z);
  }

  // ---------------------------------------------------------------- multiplayer
  // our pet for the player state: [breed, x, y, z, yaw, speed, flags]
  netState() {
    const a = this.pet;
    if (!a || a.removed || a.dead) return null;
    const r = (x) => Math.round(x * 100) / 100;
    return [PET_BREEDS.indexOf(this.info.breed), r(a.inVehicle ? a.inVehicle.pos.x : a.pos.x), r(a.pos.y), r(a.inVehicle ? a.inVehicle.pos.z : a.pos.z), r(a.yaw), r(a.speed), (a.state === 'sit' ? 1 : 0) | (a.inVehicle ? 2 : 0)];
  }
  // another player's pet, posed from their state
  poseRemote(id, st, dt) {
    const breed = Array.isArray(st) ? PET_BREEDS[st[0]] : null;
    let a = this.remote.get(id);
    if (!breed || !st.slice(1, 6).every(Number.isFinite)) { if (a) { a.remove(); this.remote.delete(id); } return; }
    if (!a || a.removed || a.breed !== breed) {
      if (a) a.remove();
      a = new Animal(this.game, breed, st[1], st[3], { y: st[2] });
      a.remotePet = true;
      this.remote.set(id, a);
    }
    const k = clamp(dt * 8, 0, 1);
    a.pos.x += (st[1] - a.pos.x) * k; a.pos.y += (st[2] - a.pos.y) * k; a.pos.z += (st[3] - a.pos.z) * k;
    a.yaw = st[4]; a.speed = st[5]; a.state = st[6] & 1 ? 'sit' : 'follow';
    a.root.visible = !(st[6] & 2);
    a.root.rotation.set(0, a.yaw, 0);
    a.t += dt;
    a._animate(dt);
  }
  dropRemote(id) { const a = this.remote.get(id); if (a) { a.remove(); this.remote.delete(id); } }
  _updateRemote() {
    const net = this.game.net;
    for (const id of [...this.remote.keys()]) if (!net?.peers?.has(id)) this.dropRemote(id);
  }
}

export { PET_BREEDS };
