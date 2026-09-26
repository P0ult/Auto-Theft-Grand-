// Shared NPCs: every client keeps simulating the pedestrians and traffic it spawned, and publishes the
// ones near other players on a second channel (the "world" presence) about six times a second. Other
// clients draw them as stand-ins: pedestrians posed from that state, cars as kinematic proxies, both
// passing any damage done to them back to the owner, who applies it. Stealing a stand-in car hands it
// over. Spawning counts everyone's NPCs so crowds don't double where players meet, and an owner keeps
// NPCs alive while any player is near them. The trains follow whoever has been online longest (or the
// player driving one).
import * as THREE from 'three';
import { Character } from '../entities/character.js';
import { VEHICLES } from '../entities/vehicledefs.js';
import { WEAPONS } from '../game/weapondefs.js';
import { clamp, lerp, wrapAngle } from '../core/utils.js';

const SEND_HZ = 6;
const SHARE_R = 260;          // NPCs this close to another player are shared with them
const SHOW_R = 360;           // draw others' NPCs this close to us
const MAX_CARS = 18, MAX_PEDS = 24;
const DELAY = 260;            // ms of interpolation delay (sends are 170 ms apart)
const VTYPES = Object.keys(VEHICLES);
const WTYPES = Object.keys(WEAPONS);
const HAIR = ['short', 'bald', 'afro', 'cap', 'buzz', 'long', 'ponytail', 'bun'];
const SHIRT = ['tee', 'long', 'tank', 'jacket'];
const num = (x, d = 0) => (Number.isFinite(x) ? x : d);
const col = (x) => (Number.isInteger(x) && x >= 0 && x <= 0xffffff ? x : 0x888888);

// ------------------------------------------------------------------ appearance <-> compact array
export function packLook(a) {
  const u = a.uniform;
  return [a.female ? 1 : 0, a.skin, a.hair, HAIR.indexOf(a.hairStyle), a.shirt, SHIRT.indexOf(a.shirtType), a.pants, a.shorts ? 1 : 0, a.shoes,
    a.hat ?? -1, Math.round((a.build || 1) * 100), Math.round((a.height || 1) * 100), a.glasses ? 1 : 0, a.beard ? 1 : 0, a.jacketColor ?? 0, a.bandana ?? -1,
    u ? u.shirt : -1, u ? u.pants : -1, u ? (u.hat ?? -1) : -1];
}
export function unpackLook(q) {
  if (!Array.isArray(q) || q.length < 16) return null;
  const opt = (x) => (x === -1 ? null : col(x));
  const a = {
    female: q[0] === 1, skin: col(q[1]), hair: col(q[2]), hairStyle: HAIR[q[3]] || 'short', shirt: col(q[4]), shirtType: SHIRT[q[5]] || 'tee',
    pants: col(q[6]), shorts: q[7] === 1, shoes: col(q[8]), hat: opt(q[9]), build: clamp(num(q[10], 100) / 100, 0.85, 1.25), height: clamp(num(q[11], 100) / 100, 0.9, 1.1),
    glasses: q[12] === 1, beard: q[13] === 1, jacketColor: col(q[14]), bandana: opt(q[15]), uniform: null,
  };
  if (q.length >= 19 && q[16] !== -1) a.uniform = { shirt: col(q[16]), pants: col(q[17]), hat: opt(q[18]) };
  return a;
}

// ------------------------------------------------------------------ a stand-in pedestrian
class NpcProxy extends Character {
  constructor(game, sync, owner, id, look) {
    super(game, look, { team: 'remote' });
    this.sync = sync; this.owner = owner; this.netId = id;
    this.remote = true; this.npcProxy = true;
    this.invincible = false;
  }
  update(dt) {
    if (this.removed) return;
    if (this.vehicle) {
      const st = this.animState;
      st.sit = this.seat === 0 ? 1 : 2; st.speed = 0; st.grounded = true; st.swim = false; st.weapon = 'none';
      this.anim.update(dt, st);
      return;
    }
    if (this.ragdolling) { this.ragdoll.update(dt); this.ragdoll.apply(); return; }
    this.animState.sit = 0;
    this.root.rotation.y = this.yaw;
    this.anim.update(dt, this.animState);
    this._orientWeapon?.();
  }
  takeDamage(amount, info = {}) {
    if (this.dead) return false;
    let dmg = amount;
    if (info.part === 'head') dmg *= info.headMul ?? 4;
    if (info.part === 'limb') dmg *= 0.7;
    this.sync.sendPedHit(this, dmg, info);
    if (!this.ragdolling && !this.vehicle && info.type !== 'fire') this.anim.play('flinch');
    return false;
  }
  knockDown() { /* the owner decides; its state tells us */ }
  setDown(down, dead) {
    if (down && !this.ragdolling && !this.vehicle) this.startRagdoll(new THREE.Vector3(), null);
    else if (!down && this.ragdolling) { this.ragdolling = false; this.anim.beginBlend(0.25); }
    this.dead = dead;
  }
}

export class NpcSync {
  constructor(net) {
    this.net = net;
    this.game = net.game;
    this.sendT = 0;
    this.seq = 0;
    this.nextId = 1;
    this.byId = new Map();     // our shared entities: netId -> ped / vehicle
    this.pedList = [];         // everyone else's stand-in pedestrians (for allCharacters)
    this.sentStatic = new Map(); // netId -> time we last sent its static info
    this.rot = 0;
  }

  // ------------------------------------------------------------------ helpers used by the game
  // Other players near a point (for spawning budgets and keeping NPCs alive)
  peersNear(x, z, R) {
    let n = 0;
    for (const P of this.net.peers.values()) { const p = P.avatar.vehicle ? P.avatar.vehicle.pos : P.avatar.pos; if ((p.x - x) ** 2 + (p.z - z) ** 2 < R * R) n++; }
    return n;
  }
  // stand-ins near a point (spawning counts them)
  proxyCarsNear(x, z, R) {
    let n = 0;
    for (const P of this.net.peers.values()) if (P.npc) for (const v of P.npc.cars.values()) if ((v.pos.x - x) ** 2 + (v.pos.z - z) ** 2 < R * R) n++;
    return n;
  }
  proxyPedsNear(x, z, R) {
    let n = 0;
    for (const q of this.pedList) if (!q.vehicle && (q.pos.x - x) ** 2 + (q.pos.z - z) ** 2 < R * R) n++;
    return n;
  }

  _id(o) { if (!o.netId) { o.netId = this.nextId++; } return o.netId; }

  // ------------------------------------------------------------------ outgoing
  build() {
    const g = this.game, peers = [...this.net.peers.values()];
    if (!peers.length) return null;
    const ppos = peers.map((P) => (P.avatar.vehicle ? P.avatar.vehicle.pos : P.avatar.pos));
    const near = (p) => { let best = Infinity; for (const q of ppos) { const d = (q.x - p.x) ** 2 + (q.z - p.z) ** 2; if (d < best) best = d; } return Math.sqrt(best); };
    const me = g.player;
    const cars = [], peds = [];
    for (const v of g.vehicles.list) {
      if (v.removed || v.remote || v.def.train || v === me.vehicle || v.occupants.some((o) => o?.isPlayer || o?.remote)) continue;
      const d = near(v.pos);
      if (d < SHARE_R) cars.push([d, v]);
    }
    for (const q of g.peds.list) {
      if (q.removed || q.remote || q.shopClerk || (q.vehicle && q.vehicle.remote)) continue; // (every client has its own shop clerks)
      if (q.vehicle && q.seat !== 0) continue;
      const pos = q.ragdolling ? q.ragdoll.center : q.pos;
      const d = q.vehicle ? near(q.vehicle.pos) : near(pos);
      if (d < SHARE_R) peds.push([d, q]);
    }
    cars.sort((a, b) => a[0] - b[0]); peds.sort((a, b) => a[0] - b[0]);
    const C = [], Pd = [], S = [];
    const now = performance.now();
    const statics = [];
    const shown = new Set();
    for (const [, v] of cars.slice(0, MAX_CARS)) {
      const id = this._id(v); shown.add(id); this.byId.set(id, v);
      const fwd = v.vel.x * Math.sin(v.yaw) + v.vel.z * Math.cos(v.yaw);
      const drv = v.driver && !v.driver.isPlayer ? this._id(v.driver) : 0;
      if (drv) { this.byId.set(drv, v.driver); shown.add(drv); }
      const fl = (v.input.brake > 0.1 || v.input.handbrake ? 1 : 0) | (v.sirenOn ? 2 : 0) | (v.exploded ? 4 : 0) | (v.health <= 0 ? 8 : 0);
      C.push(id, Math.round(v.pos.x * 10), Math.round(v.pos.y * 10), Math.round(v.pos.z * 10), Math.round(v.yaw * 100), Math.round(fwd * 10), Math.round((v.steerAngle || 0) * 100), fl, drv);
      statics.push([id, 'c', v, drv ? v.driver : null]);
    }
    for (const [, q] of peds.slice(0, MAX_PEDS)) {
      if (q.vehicle) continue; // drivers ride along with their car's record
      const id = this._id(q); shown.add(id); this.byId.set(id, q);
      const pos = q.ragdolling ? q.ragdoll.center : q.pos;
      const a = q.animState;
      const fl = (q.aiming ? 2 : 0) | (a.crouch || q.crouching ? 4 : 0) | (a.handsUp ? 8 : 0) | (a.cower ? 16 : 0) | (q.dead ? 32 : 0) | (q.ragdolling ? 64 : 0) | (q.swimming ? 128 : 0) | (a.talking ? 256 : 0);
      Pd.push(id, Math.round(pos.x * 10), Math.round(pos.y * 10), Math.round(pos.z * 10), Math.round(q.yaw * 100), Math.round((a.speed || 0) * 10), fl, WTYPES.indexOf(q.weapon));
      statics.push([id, 'p', q]);
    }
    // static info (type / colour / look): new ones first, then a rotating few
    statics.sort((a, b) => (this.sentStatic.get(a[0]) || 0) - (this.sentStatic.get(b[0]) || 0));
    let budget = 900;
    for (const [id, kind, o, drv] of statics) {
      const last = this.sentStatic.get(id) || 0;
      if (last && now - last < 2500) continue;
      const rec = kind === 'c' ? [id, 0, VTYPES.indexOf(o.type), o.color, drv ? packLook(drv.appearance) : 0] : [id, 1, packLook(o.appearance)];
      const len = JSON.stringify(rec).length;
      if (len > budget) break;
      budget -= len;
      S.push(rec);
      this.sentStatic.set(id, now);
    }
    // forget what's no longer shared
    for (const [id, o] of this.byId) if (!shown.has(id) || o.removed) { this.byId.delete(id); this.sentStatic.delete(id); }
    const out = { v: 1, q: ++this.seq, c: C, p: Pd, s: S };
    const tr = this._trainState();
    if (tr) out.tr = tr;
    return out;
  }

  send(dt) {
    this.sendT -= dt;
    if (this.sendT > 0) return;
    this.sendT = 1 / SEND_HZ;
    const t = this.net.transport;
    if (!t?.sendWorld) return;
    const n = this.build();
    if (!n) { if (this._sentAny) { this._sentAny = false; try { t.sendWorld(null); } catch { /* */ } } return; }
    // keep under the 4 KiB presence limit
    let s = JSON.stringify(n);
    while (s.length > 3900 && (n.p.length || n.c.length)) { if (n.p.length) n.p.length = Math.max(0, n.p.length - 8 * 4); else n.c.length = Math.max(0, n.c.length - 9 * 3); s = JSON.stringify(n); }
    this._sentAny = true;
    try { t.sendWorld(n); } catch { /* dropped */ }
  }

  // ------------------------------------------------------------------ incoming
  onWorld(P, n) {
    if (!P) return;
    if (!P.npc) P.npc = { buf: [], cars: new Map(), peds: new Map(), statics: new Map(), seen: new Map() };
    if (!n || n.v !== 1) { this.clearPeer(P); return; }
    const now = performance.now();
    const snap = { t: now, cars: new Map(), peds: new Map(), tr: Array.isArray(n.tr) ? n.tr : null };
    const C = Array.isArray(n.c) ? n.c : [], Pd = Array.isArray(n.p) ? n.p : [];
    for (let i = 0; i + 8 < C.length; i += 9) snap.cars.set(C[i], C.slice(i, i + 9));
    for (let i = 0; i + 7 < Pd.length; i += 8) snap.peds.set(Pd[i], Pd.slice(i, i + 8));
    for (const r of Array.isArray(n.s) ? n.s : []) if (Array.isArray(r) && Number.isInteger(r[0])) P.npc.statics.set(r[0], r);
    // forget the looks of NPCs that are long gone
    if (P.npc.statics.size > 300) for (const id of P.npc.statics.keys()) if (!snap.cars.has(id) && !snap.peds.has(id) && !P.npc.cars.has(id) && !P.npc.peds.has(id)) P.npc.statics.delete(id);
    P.npc.buf.push(snap);
    if (P.npc.buf.length > 8) P.npc.buf.shift();
    P.npc.train = snap.tr;
  }

  clearPeer(P) {
    if (!P.npc) return;
    for (const v of P.npc.cars.values()) this._dropCar(v);
    for (const q of P.npc.peds.values()) this._dropPed(q);
    P.npc.cars.clear(); P.npc.peds.clear(); P.npc.buf = [];
    this.pedList = this.pedList.filter((q) => !q.removed);
  }

  _dropPed(q) {
    if (q.vehicle) q.vehicle.takeOut(q);
    q.remove();
    q.removed = true;
  }
  _dropCar(v) {
    for (const o of v.occupants) if (o && o.npcProxy) v.takeOut(o);
    if (!v.removed) this.game.vehicles.remove(v);
  }

  // ------------------------------------------------------------------ per frame
  update(dt) {
    const g = this.game, now = performance.now();
    const me = g.player.vehicle ? g.player.vehicle.pos : g.player.pos;
    for (const P of this.net.peers.values()) {
      const N = P.npc;
      if (!N || !N.buf.length) continue;
      const T = now - DELAY;
      let i = N.buf.length - 1;
      while (i > 0 && N.buf[i - 1].t > T) i--;
      const B = N.buf[i], A = i > 0 ? N.buf[i - 1] : B;
      const k = A === B ? 0 : clamp((T - A.t) / Math.max(1, B.t - A.t), 0, 1);
      const ext = A === B ? clamp((T - B.t) / 1000, 0, 0.3) : 0;
      // cars
      for (const [id, rb] of B.cars) {
        const ra = A.cars.get(id) || rb;
        const x = lerp(ra[1], rb[1], k) / 10, y = lerp(ra[2], rb[2], k) / 10, z = lerp(ra[3], rb[3], k) / 10;
        if ((x - me.x) ** 2 + (z - me.z) ** 2 > SHOW_R * SHOW_R) { const v = N.cars.get(id); if (v) { this._dropCar(v); N.cars.delete(id); } continue; }
        let v = N.cars.get(id);
        if (!v || v.removed) {
          const st = N.statics.get(id);
          if (!st || st[1] !== 0 || !VTYPES[st[2]]) continue;
          v = g.vehicles.spawn(VTYPES[st[2]], x, z, rb[4] / 100, { color: col(st[3]), y, persistent: true });
          v.remote = true; v.npcRemote = true; v.remoteOwner = P.id; v.netId = id; v.locked = true; v.persistent = true; v.ai = null;
          v._mass0 = v.mass; v.mass = 1e6;
          N.cars.set(id, v);
        }
        const yaw = ra[4] / 100 + wrapAngle(rb[4] / 100 - ra[4] / 100) * k;
        const fwd = rb[5] / 10;
        v.pos.set(x + Math.sin(yaw) * fwd * ext, y, z + Math.cos(yaw) * fwd * ext);
        v.yaw = yaw;
        v.vel.set(Math.sin(yaw) * fwd, 0, Math.cos(yaw) * fwd);
        v.steerAngle = clamp(rb[6] / 100, -1, 1);
        v.input.brake = rb[7] & 1 ? 1 : 0; v.sirenOn = !!(rb[7] & 2);
        if (rb[7] & 4 && !v.exploded) this.net._proxyExplode(v);
        if (rb[7] & 8 && !v.exploded) { v.health = 0; v.onFire = true; }
        v.wheelRot += fwd * dt / (v.def.wheelR || 0.35);
        v.airborne = false;
        try { v._updateVisual(dt); } catch { /* visual only */ }
        // the driver
        const drvId = rb[8];
        const cur = v.occupants[0];
        if (drvId && (!cur || (cur.npcProxy && cur.netId !== drvId))) {
          if (cur) { v.takeOut(cur); this._dropPed(cur); }
          const st = N.statics.get(id);
          const look = st && Array.isArray(st[4]) ? unpackLook(st[4]) : null;
          if (look) { const q = new NpcProxy(g, this, P.id, drvId, look); q.setPosition(x, y, z); v.putIn(q, 0); this.pedList.push(q); }
        } else if (!drvId && cur && cur.npcProxy) { v.takeOut(cur); this._dropPed(cur); }
        if (v.occupants[0]?.npcProxy) v.occupants[0].update(dt);
      }
      for (const [id, v] of N.cars) if (!B.cars.has(id)) { v._gone = (v._gone || 0) + dt; if (v._gone > 1.2) { this._dropCar(v); N.cars.delete(id); } } else v._gone = 0;
      // pedestrians
      for (const [id, rb] of B.peds) {
        const ra = A.peds.get(id) || rb;
        const x = lerp(ra[1], rb[1], k) / 10, y = lerp(ra[2], rb[2], k) / 10, z = lerp(ra[3], rb[3], k) / 10;
        if ((x - me.x) ** 2 + (z - me.z) ** 2 > SHOW_R * SHOW_R) { const q = N.peds.get(id); if (q) { this._dropPed(q); N.peds.delete(id); } continue; }
        let q = N.peds.get(id);
        if (!q || q.removed) {
          const st = N.statics.get(id);
          const look = st && st[1] === 1 ? unpackLook(st[2]) : null;
          if (!look) continue;
          q = new NpcProxy(g, this, P.id, id, look);
          q.setPosition(x, y, z);
          N.peds.set(id, q);
          this.pedList.push(q);
        }
        const fl = rb[6];
        q.setDown(!!(fl & 96), !!(fl & 32));
        if (!q.ragdolling) { q.pos.set(x, y, z); q.yaw = ra[4] / 100 + wrapAngle(rb[4] / 100 - ra[4] / 100) * k; }
        const s = q.animState;
        s.speed = clamp(rb[5] / 10, 0, 12); s.moveAngle = 0; s.grounded = true;
        s.aim = !!(fl & 2); s.crouch = !!(fl & 4); s.handsUp = !!(fl & 8); s.cower = !!(fl & 16); s.swim = !!(fl & 128); s.talking = !!(fl & 256);
        q.aiming = s.aim; q.crouching = s.crouch; q.swimming = s.swim;
        const w = WTYPES[rb[7]];
        if (w && w !== q.weapon) { if (!q.weapons[w]) q.giveWeapon(w, 1); q.equip(w); }
        s.weapon = q.holdType;
        q.update(dt);
      }
      for (const [id, q] of N.peds) if (!B.peds.has(id)) { q._gone = (q._gone || 0) + dt; if (q._gone > 1.2) { this._dropPed(q); N.peds.delete(id); } } else q._gone = 0;
    }
    this.pedList = this.pedList.filter((q) => !q.removed);
    this._applyTrains(dt);
  }

  // ------------------------------------------------------------------ damage & theft
  sendPedHit(q, dmg, info) {
    const net = this.net;
    const imp = info.impulse ? [Math.round(info.impulse.x * 10) / 10, Math.round(info.impulse.y * 10) / 10, Math.round(info.impulse.z * 10) / 10] : null;
    net._emit('nh', q.owner, q.netId, Math.round(dmg * 10) / 10, info.part === 'head' ? 1 : 0, info.knockdown ? 1 : 0, imp, info.weapon || info.type || '');
  }
  sendCarHit(v, dmg) { this.net._emit('nv', v.remoteOwner, v.netId, Math.round(dmg)); }

  // we took over another client's car (the player got in): it stops being theirs
  takeCar(v) {
    const P = this.net.peers.get(v.remoteOwner);
    this.net._emit('tk', v.remoteOwner, v.netId);
    if (P?.npc) P.npc.cars.delete(v.netId);
    const drv = v.occupants[0];
    // the stand-in driver becomes one of our own pedestrians, thrown out of the car
    if (drv && drv.npcProxy) {
      v.takeOut(drv);
      const look = drv.appearance;
      this._dropPed(drv);
      const ped = this.game.peds.spawnPed(v.pos.x + 1.5, v.pos.z, { appearance: look });
      ped.threat = this.game.player; ped.setState?.('flee');
    }
    v.remote = false; v.npcRemote = false; v.remoteOwner = null; v.netId = 0; v.locked = false; v.persistent = false;
    if (v._mass0) v.mass = v._mass0;
  }

  // a moment addressed to us: hits on our NPCs, cars taken
  onMoment(P, e) {
    const k = e[1], me = this.net.selfId;
    if (!me || e[2] !== me) return;
    const o = this.byId.get(e[3]);
    if (!o || o.removed) return;
    if (k === 'nh' && o.takeDamage) {
      const imp = Array.isArray(e[7]) && e[7].length === 3 && e[7].every(Number.isFinite) ? new THREE.Vector3(...e[7]) : null;
      const info = { source: P.avatar, part: e[5] ? 'head' : 'torso', headMul: 1, weapon: String(e[8] || ''), type: WEAPONS[e[8]] ? (WEAPONS[e[8]].type === 'melee' ? 'melee' : 'bullet') : String(e[8] || 'bullet') };
      if (e[6]) { info.knockdown = true; info.impulse = imp || new THREE.Vector3(0, 3, 0); }
      const wasDead = o.dead;
      o.takeDamage(clamp(num(e[4]), 0, 500), info);
      if (o.dead && !wasDead && !o.ragdolling) o.startRagdoll?.(info.impulse || new THREE.Vector3(0, 1, 0));
      if (info.type === 'bullet') this.game.effects?.blood?.(o.chestPos, new THREE.Vector3(0, 0.3, 0), e[5] ? 12 : 6);
    } else if (k === 'nv' && o.damage) o.damage(clamp(num(e[4]), 0, 3000), P.avatar);
    else if (k === 'tk' && o.def) {
      // they drove off in it: it's theirs now
      for (const c of o.occupants) if (c && !c.isPlayer) { o.takeOut(c); this.game.peds.remove(c); }
      this.byId.delete(e[3]);
      this.game.vehicles.remove(o);
    }
  }

  // ------------------------------------------------------------------ trains
  // The host (online longest) runs the timetable; a player at a train's controls drives that one.
  _isHost() {
    const net = this.net;
    let hj = net.joinedAt, hid = net.selfId || '';
    for (const P of net.peers.values()) { const j = num(P.st.j, Infinity); if (j < hj || (j === hj && P.id < hid)) return false; }
    return true;
  }
  _trainState() {
    const R = this.game.rail;
    if (!R || !this.game.freeRoam) return null;
    const host = this._isHost();
    const out = [];
    [R.train, R.freight].forEach((t, i) => {
      if (!t || t.removed) return;
      const driving = t.driver?.isPlayer;
      if (host || driving) out.push([i, Math.round(t.s * 10), Math.round(t.v * 10), t.dirS, driving ? 1 : 0, Math.round(Math.max(0, Math.min(t.dwell, 999)) * 10)]);
    });
    return out.length ? out : null;
  }
  _applyTrains(dt) {
    const g = this.game, R = g.rail;
    if (!R || !g.freeRoam) return;
    const trains = [R.train, R.freight];
    const want = [null, null];
    for (const P of this.net.peers.values()) {
      const tr = P.npc?.train;
      if (!Array.isArray(tr)) continue;
      for (const r of tr) {
        if (!Array.isArray(r) || (r[0] !== 0 && r[0] !== 1)) continue;
        const prio = r[4] ? 2 : (this._peerIsHost(P) ? 1 : 0);
        if (!prio) continue;
        if (!want[r[0]] || prio > want[r[0]].prio) want[r[0]] = { prio, r };
      }
    }
    const host = this._isHost();
    trains.forEach((t, i) => {
      if (!t || t.removed) return;
      const w = want[i];
      if (!w || t.driver?.isPlayer || (host && w.prio < 2)) { t.netTarget = null; return; }
      t.netTarget = { s: w.r[1] / 10, v: w.r[2] / 10, dirS: w.r[3] === 1 ? 1 : -1, dwell: num(w.r[5]) / 10 };
    });
  }
  _peerIsHost(P) {
    const net = this.net;
    const j = num(P.st.j, Infinity);
    if (j > net.joinedAt || (j === net.joinedAt && P.id > (net.selfId || ''))) return false;
    for (const Q of net.peers.values()) { if (Q === P) continue; const jq = num(Q.st.j, Infinity); if (jq < j || (jq === j && Q.id < P.id)) return false; }
    return true;
  }
}
