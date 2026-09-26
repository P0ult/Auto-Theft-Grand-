// Multiplayer: share the world with other players.
//
// Every player publishes one small state object about a dozen times a second: who they are (nickname,
// colour, look), where they are and what their body is doing, the vehicle they're driving (if any), and a
// short rolling log of recent moments (shots, hits on other players, explosions, chat, kills). Each client
// keeps simulating its own world (traffic, pedestrians, police) and draws the other players into it:
// avatars posed from their state, and the cars / aircraft they drive as kinematic stand-ins.
// Health is each player's own business: a hit you land on someone is sent to them as a moment, and their
// client applies it (unless they switched player damage off).
import * as THREE from 'three';
import { RoomTransport, WsTransport, cleanCode } from './transports.js';
import { RemoteAvatar, sanitizeAppearance } from './avatar.js';
import { VEHICLES } from '../entities/vehicledefs.js';
import { WEAPONS } from '../game/weapondefs.js';
import { clamp, lerp, wrapAngle } from '../core/utils.js';

export const PLAYER_COLORS = [0x4cc9f0, 0xf72585, 0x80ed99, 0xffd166, 0xff7b00, 0xb388ff, 0xff4d6d, 0x2ec4b6];
const SEND_HZ = 12;
const DELAY = 150;            // ms of interpolation delay
const EVENT_KEEP = 1600;      // ms a moment stays in the log (spans several sends)
const PROTO = 1;
const SKEY = 'atg_net_v1';

const r1 = (x) => Math.round(x * 10) / 10, r2 = (x) => Math.round(x * 100) / 100, r3 = (x) => Math.round(x * 1000) / 1000;
const num = (x, d = 0) => (Number.isFinite(x) ? x : d);
const vec3 = (a) => (Array.isArray(a) && a.length >= 3 && a.every(Number.isFinite) ? a : null);
const cleanText = (s, n) => String(s ?? '').replace(/[\u0000-\u001f\u007f-\u009f​-‏‪-‮⁠-⁯﻿]/g, '').trim().slice(0, n);
const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const hex = (c) => '#' + c.toString(16).padStart(6, '0');

function load() { try { return JSON.parse(localStorage.getItem(SKEY)) || {}; } catch { return {}; } }
function save(o) { try { localStorage.setItem(SKEY, JSON.stringify(o)); } catch { /* private window */ } }

const _v = new THREE.Vector3(), _q1 = new THREE.Quaternion(), _q2 = new THREE.Quaternion();

export class NetSystem {
  constructor(game) {
    this.game = game;
    this.transport = null;
    this.status = 'offline';      // offline | connecting | online | error
    this.error = '';
    this.code = '';
    this.linkUp = false;
    this.peers = new Map();
    this.avatarList = [];
    this.out = [];                // recent outgoing moments {t, e}
    this.seq = 0;
    this.sendT = 0;
    this.joinedAt = 0;
    this.pendingHits = new Map();
    this.shotT = 0;
    this.actN = 0; this._lastAct = null;
    this.chat = [];
    this.options = null;
    this._detect = null;
    const s = load();
    this.name = cleanText(s.name, 16) || `Player${100 + ((Math.random() * 900) | 0)}`;
    this.color = Number.isInteger(s.color) ? s.color % PLAYER_COLORS.length : (Math.random() * PLAYER_COLORS.length) | 0;
    this.pvp = s.pvp ?? true;
    this._dom();
  }

  get online() { return this.status === 'online' && !!this.transport; }
  get selfId() {
    const t = this.transport;
    if (!t) return null;
    if (t.kind === 'ws') return t.id;
    try { return t.r?.peers().find((p) => p.sameTab)?.peer || null; } catch { return null; }
  }
  saveSettings() { save({ name: this.name, color: this.color, pvp: this.pvp }); }

  // ------------------------------------------------------------------ connection
  detect() {
    if (!this._detect) {
      this._detect = Promise.all([RoomTransport.probe(), WsTransport.probe()]).then(([room, ws]) => {
        this.options = { room, ws };
        this.onChange?.();
        return this.options;
      });
    }
    return this._detect;
  }
  get transportLabel() { const o = this.options; return !o ? 'checking…' : o.room ? o.room.label : o.ws ? o.ws.label : 'unavailable'; }

  async connect(code = '') {
    await this.disconnect(true);
    this.status = 'connecting'; this.error = ''; this.onChange?.();
    const opts = await this.detect();
    const t = opts.room || opts.ws;
    if (!t) {
      this.status = 'error';
      this.error = 'Multiplayer works in the published claude.ai version of the game, or when you run it yourself with `node server.mjs`.';
      this.onChange?.();
      return false;
    }
    t.onPeer = (id, st, guest) => this._onPeer(id, st, guest);
    t.onLeft = (id) => this._removePeer(id, true);
    t.onStatus = (ok) => { this.linkUp = ok; this.onChange?.(); };
    try { await t.join(code); } catch (e) {
      this.status = 'error'; this.error = e?.message || 'Could not join that room.'; this.onChange?.();
      return false;
    }
    this.transport = t;
    this.code = cleanCode(code);
    this.status = 'online';
    this.linkUp = true;
    this.joinedAt = Date.now();
    this.sendT = 0;
    this._sys(`You joined ${this.code ? `room <b>${esc(this.code.toUpperCase())}</b>` : 'the <b>public world</b>'}.`);
    this.onChange?.();
    this._send();
    return true;
  }

  async disconnect(quiet = false) {
    const t = this.transport;
    this.transport = null;
    for (const id of [...this.peers.keys()]) this._removePeer(id, false);
    if (t) { try { await t.leave(); } catch { /* ignore */ } }
    if (this.status === 'online' && !quiet) this._sys('You left multiplayer.');
    this.status = 'offline';
    this.game.env.weatherLocked = false;
    this.onChange?.();
  }

  // ------------------------------------------------------------------ outgoing
  _emit(kind, ...data) {
    this.out.push({ t: performance.now(), e: [++this.seq, kind, ...data] });
    if (this.out.length > 24) this.out.shift();
  }

  onShot(shooter, def, from, to) {
    if (!this.online || shooter !== this.game.player) return;
    const now = performance.now();
    if (now - this.shotT < 70) return;
    this.shotT = now;
    this._emit('sh', def.id, r2(from.x), r2(from.y), r2(from.z), r1(to.x), r1(to.y), r1(to.z));
  }

  onExplosion(pos, radius, source) {
    if (!this.online) return;
    const p = this.game.player;
    if (source !== p && source !== p.vehicle) return;
    this._emit('ex', r1(pos.x), r1(pos.y), r1(pos.z), r1(radius));
  }

  sendHit(peerId, dmg, info) {
    if (!this.online) return;
    const P = this.peers.get(peerId);
    if (!P || !P.st.pvp) return;
    const h = this.pendingHits.get(peerId) || { dmg: 0, head: 0, kd: 0, imp: null, w: '' };
    h.dmg += dmg;
    if (info.part === 'head') h.head = 1;
    if (info.knockdown) { h.kd = 1; if (info.impulse) h.imp = [r1(info.impulse.x), r1(info.impulse.y), r1(info.impulse.z)]; }
    h.w = info.weapon || info.type || '';
    this.pendingHits.set(peerId, h);
  }

  sendVehicleHit(v, dmg, source) {
    if (!this.online || !v.remoteOwner) return;
    const p = this.game.player;
    if (source !== p && source !== p.vehicle && source?.driver !== p) return;
    const P = this.peers.get(v.remoteOwner);
    if (!P || !P.st.pvp) return;
    this._emit('vh', v.remoteOwner, r1(dmg));
  }

  say(text) {
    const t = cleanText(text, 140);
    if (!t) return;
    if (this.online) this._emit('ch', t);
    this._pushChat({ name: this.name, color: PLAYER_COLORS[this.color], text: esc(t), me: true });
  }

  _buildState() {
    const g = this.game, p = g.player, v = p.vehicle;
    const now = performance.now();
    this.out = this.out.filter((o) => now - o.t < EVENT_KEEP);
    for (const [id, h] of this.pendingHits) this._emit('hit', id, r1(h.dmg), h.head, h.kd, h.imp, h.w);
    this.pendingHits.clear();
    const act = p.anim.action && !p.anim.action.done ? p.anim.action : null;
    if (act !== this._lastAct) { this._lastAct = act; if (act) this.actN++; }
    const pos = p.ragdolling ? p.ragdoll.center : p.pos;
    const a = p.animState;
    const st = {
      v: PROTO, n: this.name, c: this.color, j: this.joinedAt, pvp: this.pvp ? 1 : 0, ap: p.appearance,
      m: g.freeRoam ? 'f' : 's', pz: g.paused ? 1 : 0,
      p: [r2(pos.x), r2(pos.y), r2(pos.z)], y: r3(p.yaw), vl: [r1(p.vel.x), r1(p.vel.y), r1(p.vel.z)],
      a: [r2(a.speed), r2(a.moveAngle), a.grounded ? 1 : 0, p.aiming ? 1 : 0, r2(p.aimPitch || 0), a.crouch ? 1 : 0, a.swim ? 1 : 0, a.handsUp ? 1 : 0, p.chute ? 1 : 0, r1(a.vy || 0)],
      w: p.weapon, ac: act ? [act.name, this.actN] : null,
      hp: Math.round(p.health), ar: Math.round(p.armor), d: p.dead ? 1 : 0, rg: p.ragdolling ? 1 : 0, wl: g.police?.level | 0,
      vh: v ? this._vehState(v, p.seat) : null,
      tod: r2(g.env.hours), wx: g.env.weather,
      ev: this.out.map((o) => o.e),
    };
    return st;
  }

  _vehState(v, seat) {
    if (v.remoteOwner) return { ref: v.remoteOwner, s: seat };
    const o = { t: v.type, c: v.color, s: seat, p: [r2(v.pos.x), r2(v.pos.y), r2(v.pos.z)], y: r3(v.yaw), vl: [r1(v.vel.x), r1(v.vel.y || 0), r1(v.vel.z)], hp: Math.round(v.health), x: v.exploded ? 1 : 0 };
    if (v.def.train) o.tr = 1;
    if (v.quat) { o.q = [r3(v.quat.x), r3(v.quat.y), r3(v.quat.z), r3(v.quat.w)]; o.sp = r2(v.def.kind === 'heli' ? v.spool : v.power); o.gr = v.grounded ? 1 : 0; }
    else { o.pr = [r3(v.groundPitch || 0), r3(v.groundRoll || 0)]; o.st = r2(v.steerAngle || 0); o.br = v.input.brake > 0.1 || v.input.handbrake ? 1 : 0; o.sr = v.sirenOn ? 1 : 0; }
    if (v.def.tank) { o.tu = r2(v.turretYaw); o.gp = r2(v.gunPitch); }
    return o;
  }

  _send() {
    if (!this.online) return;
    const st = this._buildState();
    // stay well inside the room's 4 KiB presence limit: shed the oldest moments first
    while (st.ev.length && JSON.stringify(st).length > 3600) st.ev.shift();
    try { this.transport.send(st); } catch { /* dropped */ }
  }

  // ------------------------------------------------------------------ incoming
  _onPeer(id, st, guest) {
    if (!st || st.v !== PROTO || !vec3(st.p)) return;
    let P = this.peers.get(id);
    const now = performance.now();
    const apKey = JSON.stringify(st.ap || {}) + '|' + st.c;
    const rebuilt = !!P && P.apKey !== apKey; // new look / colour: rebuild the avatar quietly
    if (rebuilt) { this._removePeer(id, false); P = null; }
    const fresh = !P;
    if (!P) P = this._addPeer(id, st, guest, apKey);
    P.name = cleanText(st.n, 16) || 'Player';
    P.color = PLAYER_COLORS[(Number.isInteger(st.c) ? st.c : 0) % PLAYER_COLORS.length];
    P.st = st;
    P.last = now;
    P.buf.push({ t: now, st });
    if (P.buf.length > 10) P.buf.shift();
    if (fresh) {
      // don't replay moments from before we met
      for (const e of Array.isArray(st.ev) ? st.ev : []) if (Array.isArray(e) && Number.isInteger(e[0])) P.lastEv = Math.max(P.lastEv, e[0]);
      if (!rebuilt) this._sys(`<b style="color:${hex(P.color)}">${esc(P.name)}</b> joined.`);
    } else this._moments(P, st.ev);
    if (fresh) this.onChange?.();
  }

  _addPeer(id, st, guest, apKey) {
    const g = this.game;
    // everyone wears their player colour, so you can tell people apart at a glance
    const look = sanitizeAppearance(st.ap);
    look.shirt = PLAYER_COLORS[(Number.isInteger(st.c) ? st.c : 0) % PLAYER_COLORS.length];
    if (look.shirtType === 'jacket') look.jacketColor = look.shirt;
    const av = new RemoteAvatar(g, this, id, look);
    av.setPosition(st.p[0], st.p[1], st.p[2]);
    const P = { id, st, guest: !!guest, apKey, avatar: av, veh: null, oldVeh: null, buf: [], lastEv: 0, last: performance.now(), actN: 0, name: '', color: PLAYER_COLORS[0], tag: null, blip: null };
    P.blip = { x: st.p[0], z: st.p[2], icon: 'person', color: PLAYER_COLORS[(st.c | 0) % PLAYER_COLORS.length], label: 'Player' };
    g.blips.add(P.blip);
    P.tag = document.createElement('div');
    P.tag.className = 'net-tag';
    this.tagLayer.appendChild(P.tag);
    this.peers.set(id, P);
    this.avatarList = [...this.peers.values()].map((q) => q.avatar);
    return P;
  }

  _removePeer(id, announce) {
    const P = this.peers.get(id);
    if (!P) return;
    const g = this.game;
    this.peers.delete(id);
    this.avatarList = [...this.peers.values()].map((q) => q.avatar);
    const av = P.avatar;
    if (av.vehicle) { const v = av.vehicle; v.takeOut(av); }
    av.remove();
    if (P.veh) this._releaseProxy(P.veh);
    g.blips.delete(P.blip);
    P.tag?.remove();
    if (announce) { this._sys(`<b style="color:${hex(P.color)}">${esc(P.name)}</b> left.`); this.onChange?.(); }
  }

  _moments(P, ev) {
    if (!Array.isArray(ev)) return;
    const g = this.game, me = this.selfId;
    for (const e of ev) {
      if (!Array.isArray(e) || !Number.isInteger(e[0]) || e[0] <= P.lastEv) continue;
      P.lastEv = e[0];
      const k = e[1];
      if (k === 'sh') {
        const def = WEAPONS[e[2]];
        const f = vec3(e.slice(3, 6)), t = vec3(e.slice(6, 9));
        if (!def || !f || !t) continue;
        const from = new THREE.Vector3(...f), to = new THREE.Vector3(...t);
        if (from.distanceTo(to) > 400) continue;
        g.effects.tracers.add(from, to);
        g.effects.muzzleFlash(from, to.clone().sub(from).normalize(), def.id === 'shotgun');
        g.audio?.playAt(def.sound, from, 0.85, { gun: true });
        g.events.emit('gunshot', P.avatar, from, def);
      } else if (k === 'ex') {
        const p = vec3(e.slice(2, 5));
        if (!p) continue;
        const pos = new THREE.Vector3(...p);
        g.effects.explosion(pos, clamp(num(e[5], 5), 1, 20) * 0.75);
        g.audio?.playAt('explosion', pos, 1);
        const pd = g.player.vehicle ? g.player.vehicle.pos : g.player.pos;
        g.rig.addShake(clamp(1.2 - pos.distanceTo(pd) / 60, 0, 1.2));
      } else if (k === 'hit') {
        if (!me || e[2] !== me) continue;
        this._takeHit(P, clamp(num(e[3]), 0, 400), !!e[4], !!e[5], vec3(e[6]), String(e[7] || ''));
      } else if (k === 'vh') {
        if (!me || e[2] !== me || !this.pvp) continue;
        const v = g.player.vehicle;
        if (v && g.player.seat === 0 && !v.remoteOwner) v.damage(clamp(num(e[3]), 0, 2000), P.avatar);
      } else if (k === 'ch') {
        const text = cleanText(e[2], 140);
        if (text) this._pushChat({ name: P.name, color: P.color, text: esc(text) });
      } else if (k === 'k') {
        const killer = e[2] === me ? { name: this.name, color: PLAYER_COLORS[this.color] } : this.peers.get(e[2]);
        const w = WEAPONS[e[3]]?.name || 'explosion';
        if (killer) this._sys(`<b style="color:${hex(killer.color)}">${esc(killer.name)}</b> <span class="kf">${esc(w)}</span> <b style="color:${hex(P.color)}">${esc(P.name)}</b>`);
      }
    }
  }

  _takeHit(P, dmg, head, kd, imp, weapon) {
    const g = this.game, p = g.player;
    if (!this.pvp || p.dead || !dmg) return;
    const wasDead = p.dead;
    const info = { type: WEAPONS[weapon] ? (WEAPONS[weapon].type === 'melee' ? 'melee' : 'bullet') : weapon === 'vehicle' ? 'vehicle' : 'explosion', source: P.avatar, weapon, headMul: 1 };
    if (kd && !p.vehicle) { info.knockdown = true; info.impulse = imp ? new THREE.Vector3(...imp) : new THREE.Vector3(0, 3, 0); }
    p.takeDamage(dmg, info);
    const hp = p.chestPos;
    if (info.type === 'bullet') g.effects.blood(hp, new THREE.Vector3(Math.sin(p.yaw), 0.2, Math.cos(p.yaw)), head ? 12 : 6);
    if (p.dead && !wasDead) this._emit('k', P.id, weapon);
  }

  // ------------------------------------------------------------------ frame
  tick(dt) {
    const g = this.game;
    if (!this.online) return;
    // send
    this.sendT -= dt;
    const rate = g.paused ? 3 : SEND_HZ;
    if (this.sendT <= 0) { this.sendT = 1 / rate; this._send(); }
    // draw everyone else
    const now = performance.now();
    for (const P of this.peers.values()) this._pose(P, now, dt);
    this._syncClock(dt);
    this._tags();
  }

  _sample(P, now) {
    const T = now - DELAY, b = P.buf;
    let i = b.length - 1;
    while (i > 0 && b[i - 1].t > T) i--;
    const B = b[i], A = i > 0 ? b[i - 1] : null;
    if (!A || B.t <= T) {
      // past the newest state: extrapolate a little
      const ext = clamp((T - B.t) / 1000, 0, 0.25);
      return { a: B.st, b: B.st, k: 0, ext };
    }
    const k = clamp((T - A.t) / Math.max(1, B.t - A.t), 0, 1);
    return { a: A.st, b: B.st, k, ext: 0 };
  }

  _pose(P, now, dt) {
    const g = this.game, av = P.avatar;
    const { a, b, k, ext } = this._sample(P, now);
    const st = P.st;
    const jump = Math.hypot(a.p[0] - b.p[0], a.p[2] - b.p[2]) > 40;
    const kk = jump ? 1 : k;
    const vl = vec3(b.vl) || [0, 0, 0];
    const x = lerp(a.p[0], b.p[0], kk) + vl[0] * ext, y = lerp(a.p[1], b.p[1], kk) + vl[1] * ext * 0.5, z = lerp(a.p[2], b.p[2], kk) + vl[2] * ext;
    const yaw = num(a.y) + wrapAngle(num(b.y) - num(a.y)) * kk;
    P.blip.x = x; P.blip.z = z; P.blip.color = P.color;
    // vehicle
    const vs = st.vh && typeof st.vh === 'object' ? st.vh : null;
    if (vs && !vs.tr) this._poseVehicle(P, vs, a.vh, b.vh, kk, ext, dt);
    else if (av.vehicle) { const v = av.vehicle; v.takeOut(av); }
    if (!vs && P.veh) { this._releaseProxy(P.veh); P.oldVeh = P.veh; P.veh = null; }
    if (!av.vehicle) {
      // on foot (or riding the train: shown where they are)
      const down = !!(st.rg || st.d);
      av.setDown(down, !!st.d);
      if (!av.ragdolling) { av.pos.set(x, y, z); av.yaw = yaw; }
      const A = Array.isArray(st.a) ? st.a : [];
      const s = av.animState;
      s.speed = clamp(num(A[0]), 0, 12); s.moveAngle = num(A[1]); s.grounded = !!A[2]; s.aim = !!A[3]; s.aimPitch = clamp(num(A[4]), -1.5, 1.5);
      s.crouch = !!A[5]; s.swim = !!A[6]; s.handsUp = !!A[7]; s.vy = num(A[9]);
      av.aiming = !!A[3]; av.aimPitch = s.aimPitch; av.swimming = !!A[6]; av.crouching = !!A[5];
      av.vel.set(vl[0], vl[1], vl[2]);
    } else av.setDown(false, !!st.d);
    av.setWeapon(typeof st.w === 'string' ? st.w : 'fist');
    av.animState.weapon = av.holdType;
    if (Array.isArray(st.ac) && st.ac[1] !== P.actN) { P.actN = st.ac[1]; if (typeof st.ac[0] === 'string' && !av.ragdolling) av.anim.play(st.ac[0]); }
    av.health = clamp(num(st.hp, 100), 0, 200);
    av.update(dt);
  }

  _poseVehicle(P, vs, va, vb, k, ext, dt) {
    const g = this.game, av = P.avatar;
    if (vs.ref) {
      // riding in someone's vehicle: ours, or another player's stand-in
      const me = this.selfId;
      const host = vs.ref === me ? g.player.vehicle : this.peers.get(vs.ref)?.veh;
      const seat = clamp(num(vs.s, 1) | 0, 1, 3);
      if (!host || seat >= (host.model?.seats?.length || 2)) { if (av.vehicle) av.vehicle.takeOut(av); return; }
      if (av.vehicle !== host || av.seat !== seat) {
        if (av.vehicle) av.vehicle.takeOut(av);
        if (!host.occupants[seat]) host.putIn(av, seat);
      }
      return;
    }
    if (!VEHICLES[vs.t] || !vec3(vs.p)) return;
    let v = P.veh;
    if (!v || v.removed || v.type !== vs.t || (v.exploded && !vs.x)) {
      if (v) this._releaseProxy(v);
      // they got back into the car they left here? use it again
      const o = P.oldVeh;
      if (o && !o.removed && o.type === vs.t && !o.driver && Math.hypot(o.pos.x - vs.p[0], o.pos.z - vs.p[2]) < 10) v = o;
      else v = this.game.vehicles.spawn(vs.t, vs.p[0], vs.p[2], num(vs.y), { color: Number.isInteger(vs.c) ? vs.c : undefined, y: vs.p[1], persistent: true });
      v.remote = true;
      v.remoteOwner = P.id;
      v.persistent = true;
      v.locked = true;
      v._mass0 = v._mass0 ?? v.mass;
      v.mass = 1e6;   // immovable for local collisions: its owner drives it
      v.ai = null;
      P.veh = v;
    }
    const A = va && va.p ? va : vs, B = vb && vb.p ? vb : vs;
    const jump = Math.hypot(A.p[0] - B.p[0], A.p[2] - B.p[2]) > 60;
    const kk = jump ? 1 : k;
    const vl = vec3(B.vl) || [0, 0, 0];
    v.pos.set(lerp(A.p[0], B.p[0], kk) + vl[0] * ext, lerp(A.p[1], B.p[1], kk) + vl[1] * ext, lerp(A.p[2], B.p[2], kk) + vl[2] * ext);
    v.yaw = num(A.y) + wrapAngle(num(B.y) - num(A.y)) * kk;
    v.vel.set(vl[0], vl[1], vl[2]);
    v.health = clamp(num(vs.hp, 1000), 0, 5000);
    const seat = clamp(num(vs.s) | 0, 0, 3);
    if (av.vehicle !== v || av.seat !== seat) {
      if (av.vehicle) av.vehicle.takeOut(av);
      if (v.occupants[seat] && v.occupants[seat] !== av) { const o = v.occupants[seat]; if (!o.isPlayer) v.takeOut(o); }
      if (!v.occupants[seat]) v.putIn(av, seat);
    }
    v.netT = { A, B, k: kk, sp: num(vs.sp), gr: vs.gr, pr: Array.isArray(B.pr) ? B.pr : [0, 0], st: num(vs.st), br: vs.br, sr: vs.sr, tu: num(vs.tu), gp: num(vs.gp) };
    if (vs.x && !v.exploded) this._proxyExplode(v);
    this._netStep(v, dt);
  }

  // Kinematic update for a stand-in vehicle (VehicleManager skips remote vehicles' physics)
  _netStep(v, dt) {
    const T = v.netT;
    if (!T) return;
    if (v.quat) {
      const qa = Array.isArray(T.A.q) && T.A.q.length === 4 ? T.A.q : null, qb = Array.isArray(T.B.q) && T.B.q.length === 4 ? T.B.q : qa;
      if (qb) { _q1.set(...(qa || qb)).normalize(); _q2.set(...qb).normalize(); v.quat.copy(_q1).slerp(_q2, T.k); }
      if (v.def.kind === 'heli') v.spool = clamp(T.sp, 0, 1); else { v.power = clamp(T.sp, 0, 1); v.spool = Math.max(v.spool || 0, v.power); }
      v.grounded = !!T.gr;
      try { v._placeGroup?.(); v._updateVisual?.(dt); } catch { /* visual only */ }
    } else {
      v.groundPitch = clamp(num(T.pr[0]), -0.8, 0.8); v.groundRoll = clamp(num(T.pr[1]), -0.8, 0.8);
      v.steerAngle = clamp(T.st, -1, 1);
      v.input.brake = T.br ? 1 : 0; v.input.handbrake = false; v.input.throttle = 0.3;
      v.sirenOn = !!T.sr;
      const fwd = v.vel.x * Math.sin(v.yaw) + v.vel.z * Math.cos(v.yaw);
      v.wheelRot += fwd * dt / (v.def.wheelR || 0.35);
      if (v.def.tank) { v.turretYaw = T.tu; v.gunPitch = T.gp; }
      v.airborne = false;
      try { v._updateVisual(dt); } catch { /* visual only */ }
    }
  }

  _proxyExplode(v) {
    const c = this.game.combat;
    const old = c.visualOnly;
    c.visualOnly = true;
    v.lastDamager = null;
    try { v.explode(); } finally { c.visualOnly = old; }
  }

  // A stand-in its owner has left: it becomes an ordinary parked car in our world
  _releaseProxy(v) {
    for (let i = 0; i < v.occupants.length; i++) { const o = v.occupants[i]; if (o && o.remote) v.takeOut(o); }
    v.remote = false; v.remoteOwner = null; v.locked = false; v.persistent = false; v.netT = null;
    if (v._mass0) v.mass = v._mass0;
    v.vel.set(0, 0, 0);
    if (v.quat) { v.grounded = true; v.power = 0; }
  }

  // Follow the host's clock and weather (the player who has been here longest) in free roam
  _syncClock(dt) {
    const g = this.game;
    this.clockT = (this.clockT || 0) - dt;
    if (this.clockT > 0) return;
    this.clockT = 2;
    let host = null, hj = this.joinedAt, hid = this.selfId || '';
    for (const P of this.peers.values()) { const j = num(P.st.j, Infinity); if (j < hj || (j === hj && P.id < hid)) { hj = j; hid = P.id; host = P; } }
    const follow = !!host && !g.missions?.active && g.freeRoam;
    g.env.weatherLocked = follow;
    if (!follow) return;
    const tod = num(host.st.tod, g.env.hours);
    const diff = Math.abs(((tod - g.env.hours + 36) % 24) - 12);
    if (12 - diff > 0.12) g.env.setTime(tod);
    const wx = host.st.wx;
    if (['clear', 'cloudy', 'rain', 'storm', 'fog'].includes(wx) && wx !== g.env.weather) g.env.setWeather(wx);
  }

  // ------------------------------------------------------------------ on-screen: name tags, chat, badge
  _dom() {
    this.tagLayer = document.createElement('div');
    this.tagLayer.className = 'net-tags';
    document.body.appendChild(this.tagLayer);
    this.feed = document.createElement('div');
    this.feed.className = 'net-feed';
    document.body.appendChild(this.feed);
    this.badge = document.createElement('div');
    this.badge.className = 'net-badge';
    document.body.appendChild(this.badge);
    this.chatBox = document.createElement('div');
    this.chatBox.className = 'net-chatbox';
    this.chatBox.innerHTML = '<span>Say:</span><input maxlength="140" placeholder="Press Enter to send, Esc to cancel">';
    document.body.appendChild(this.chatBox);
    const inp = this.chatBox.querySelector('input');
    inp.addEventListener('keydown', (e) => {
      e.stopPropagation();
      if (e.key === 'Enter') { this.say(inp.value); this.closeChat(); }
      else if (e.key === 'Escape') this.closeChat();
    });
    inp.addEventListener('blur', () => this.closeChat());
    this.chatInput = inp;
  }

  openChat() {
    if (this.chatOpen) return;
    this.chatOpen = true;
    this.chatBox.classList.add('show');
    this._inputWas = this.game.input.enabled;
    this.game.input.enabled = false;
    this.game.input.keys.clear();
    setTimeout(() => this.chatInput.focus(), 0);
  }
  closeChat() {
    if (!this.chatOpen) return;
    this.chatOpen = false;
    this.chatBox.classList.remove('show');
    this.chatInput.value = '';
    this.chatInput.blur();
    this.game.input.enabled = this._inputWas ?? true;
  }

  _sys(html) { this._pushChat({ sys: true, text: html }); }
  _pushChat(m) {
    m.t = performance.now();
    this.chat.push(m);
    if (this.chat.length > 60) this.chat.shift();
    const el = document.createElement('div');
    el.className = 'net-line' + (m.sys ? ' sys' : '');
    el.innerHTML = m.sys ? m.text : `<b style="color:${hex(m.color)}">${esc(m.name)}</b> ${m.text}`;
    this.feed.appendChild(el);
    while (this.feed.children.length > 7) this.feed.firstChild.remove();
    setTimeout(() => el.classList.add('fade'), 11000);
    setTimeout(() => el.remove(), 12500);
    this.onChat?.();
  }

  _tags() {
    const g = this.game, cam = g.camera;
    const W = window.innerWidth, H = window.innerHeight;
    const cp = cam.position;
    for (const P of this.peers.values()) {
      const av = P.avatar, tag = P.tag;
      const base = av.vehicle ? av.vehicle.pos : av.ragdolling ? av.ragdoll.center : av.pos;
      _v.set(base.x, base.y + (av.vehicle ? (av.vehicle.def.H || 1.6) + 0.9 : 2.25), base.z);
      const d = _v.distanceTo(cp);
      _v.project(cam);
      const show = _v.z < 1 && _v.z > -1 && d < 320 && !g.hud?.menuOpen && g.gameplay?.state === 'playing';
      if (!show) { if (tag.style.display !== 'none') tag.style.display = 'none'; continue; }
      const hp = clamp(num(P.st.hp, 100) / 100, 0, 1);
      const key = `${P.name}|${P.color}|${Math.round(hp * 20)}|${P.st.pz ? 1 : 0}|${P.st.pvp ? 1 : 0}|${P.st.d ? 1 : 0}`;
      if (tag._k !== key) {
        tag._k = key;
        tag.innerHTML = `<b style="color:${hex(P.color)}">${esc(P.name)}</b>${P.st.pz ? ' <i>paused</i>' : ''}${P.st.pvp ? '' : ' <i>passive</i>'}${P.st.d ? ' <i>wasted</i>' : ''}<span class="hp"><i style="width:${Math.round(hp * 100)}%"></i></span>`;
      }
      tag.style.display = 'block';
      const s = clamp(1.25 - d / 260, 0.55, 1.1);
      tag.style.transform = `translate(${((_v.x + 1) / 2 * W).toFixed(1)}px, ${((1 - _v.y) / 2 * H).toFixed(1)}px) translate(-50%, -100%) scale(${s.toFixed(2)})`;
    }
    const n = this.peers.size;
    const txt = this.online ? `<i class="dot${this.linkUp ? '' : ' off'}"></i>ONLINE · ${this.code ? 'ROOM ' + esc(this.code.toUpperCase()) : 'PUBLIC WORLD'} · ${n + 1} player${n ? 's' : ''} · <b>/</b> chat` : '';
    if (this.badge._t !== txt) { this.badge._t = txt; this.badge.innerHTML = txt; this.badge.style.display = txt ? 'block' : 'none'; }
  }

  // ------------------------------------------------------------------ helpers for the Online tab
  roster() {
    const g = this.game, me = g.player.vehicle ? g.player.vehicle.pos : g.player.pos;
    return [...this.peers.values()].map((P) => {
      const av = P.avatar, pos = av.vehicle ? av.vehicle.pos : av.pos;
      return {
        id: P.id, name: P.name, color: P.color, guest: P.guest,
        dist: Math.hypot(pos.x - me.x, pos.z - me.z), where: g.map.zoneName?.(pos.x, pos.z) || '',
        hp: num(P.st.hp, 100), dead: !!P.st.d, pvp: !!P.st.pvp, paused: !!P.st.pz, story: P.st.m === 's',
        vehicle: P.st.vh && !P.st.vh.ref && VEHICLES[P.st.vh.t] ? VEHICLES[P.st.vh.t].name : P.st.vh?.ref ? 'passenger' : '',
        x: pos.x, z: pos.z,
      };
    }).sort((a, b) => a.dist - b.dist);
  }
}
