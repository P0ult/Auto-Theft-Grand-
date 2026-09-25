// Mission engine: async mission scripts driven by the frame loop, with cutscenes, objectives,
// entity management, fail conditions, rewards, contacts/blips and story progression.
import * as THREE from 'three';
import { LaneDriver, nearestLane } from './traffic.js';
import { clamp, dist2, rand, wrapAngle } from '../core/utils.js';

export class MissionFail extends Error { constructor(reason) { super(reason); this.reason = reason; } }

let _arrowGeo = null, _arrowMat = null;
const ARROW_GEO = () => {
  if (!_arrowGeo) { _arrowGeo = new THREE.ConeGeometry(0.2, 0.42, 4); _arrowGeo.rotateX(Math.PI); _arrowGeo.userData.shared = true; }
  return _arrowGeo;
};
const ARROW_MAT = () => _arrowMat || (_arrowMat = new THREE.MeshBasicMaterial({ color: new THREE.Color(2.4, 0.18, 0.12), fog: false }));
class MissionAbort extends Error {}

// --------------------------------------------------------------------------- AI drivers for missions
// Drives along the road network to a destination (A* route), or flees from the player.
export class RouteDriver extends LaneDriver {
  constructor(game, veh, dest, opts = {}) {
    super(game, veh, false);
    this.dest = dest;
    this.fixedCruise = true;
    this.cruise = opts.speed ?? 22;
    this.ignoreLights = opts.ignoreLights ?? true;
    this.flee = !!opts.flee;
    this.arrived = false;
    this.route = null;
    const net = this.net;
    // start in the lane that heads toward the destination (unless fleeing)
    const yaw = this.flee ? veh.yaw : Math.atan2(dest.x - veh.pos.x, dest.z - veh.pos.z);
    let st = nearestLane(net, veh.pos.x, veh.pos.z, yaw);
    if (!this.flee && st) {
      // pick the direction whose far end is closer to the destination along the road
      const e = st.e;
      if (e.lanesF > 0 && e.lanesB > 0) {
        const r = net.route(veh.pos.x, veh.pos.z, dest.x, dest.z);
        if (r && r.seq.length) {
          const firstNode = r.seq[0].node;
          const dir = firstNode === e.b ? 0 : 1;
          if (dir !== st.dir) st = { ...st, dir, lane: 0, s: e.len - st.s };
        }
      }
    }
    if (st && !this.flee && opts.snap !== false && veh.speedAbs < 2) {
      // turn the car around in place if it was parked facing the wrong way
      const pts = net.lanePath(st.e, st.dir, st.lane);
      let bi = 0, bd = Infinity;
      for (let i = 0; i < pts.length - 1; i++) { const d = (pts[i][0] - veh.pos.x) ** 2 + (pts[i][2] - veh.pos.z) ** 2; if (d < bd) { bd = d; bi = i; } }
      const a = pts[bi], b = pts[Math.min(pts.length - 1, bi + 1)];
      const lyaw = Math.atan2(b[0] - a[0], b[2] - a[2]);
      if (Math.abs(wrapAngle(lyaw - veh.yaw)) > Math.PI / 2) { veh.yaw = lyaw; veh.pos.x = a[0]; veh.pos.z = a[2]; }
    }
    if (st) this._start(st);
  }
  _chooseNext(cur) {
    const opts = this._options(cur);
    if (!opts.length) return null;
    const net = this.net;
    if (this.flee) {
      const p = this.game.player.vehicle ? this.game.player.vehicle.pos : this.game.player.pos;
      let best = opts[0], bd = -Infinity;
      for (const o of opts) { const far = net.nodes[o.dir === 0 ? o.e.b : o.e.a]; const d = Math.hypot(far.x - p.x, far.z - p.z) + Math.random() * 40; if (d > bd) { bd = d; best = o; } }
      return best;
    }
    // follow the A* route; recompute when we are off it
    for (let attempt = 0; attempt < 2; attempt++) {
      if (!this.route) this.route = net.route(cur.node.x, cur.node.z, this.dest.x, this.dest.z);
      const seq = this.route ? this.route.seq : [];
      const k = seq.findIndex((q) => q.node === cur.node.id);
      const nextEdge = k >= 0 && k + 1 < seq.length ? seq[k + 1].edge : k < 0 && seq.length && (net.edges[seq[0].edge].a === cur.node.id || net.edges[seq[0].edge].b === cur.node.id) ? seq[0].edge : null;
      if (nextEdge != null) { const o = opts.find((q) => q.e.id === nextEdge); if (o) return o; }
      if (k >= 0 && k === seq.length - 1) { const g = this.route.goal.e; const o = opts.find((q) => q.e === g); if (o) return o; }
      this.route = null;
    }
    let best = opts[0], bd = Infinity;
    for (const o of opts) { const far = net.nodes[o.dir === 0 ? o.e.b : o.e.a]; const d = Math.hypot(far.x - this.dest.x, far.z - this.dest.z) + Math.random() * 5; if (d < bd) { bd = d; best = o; } }
    return best;
  }
  update(dt) {
    const v = this.veh;
    if (!this.flee && Math.hypot(this.dest.x - v.pos.x, this.dest.z - v.pos.z) < 25) {
      this.arrived = true;
      v.input.throttle = 0; v.input.brake = 1; v.input.handbrake = v.speedAbs < 1;
      return;
    }
    super.update(dt);
  }
}

// Follows a list of waypoints directly (for races), with obstacle feelers.
export class RaceDriver {
  constructor(game, veh, points, skill = 0.8) {
    this.game = game; this.veh = veh; this.points = points; this.idx = 0; this.skill = skill; this.lap = 0; this.done = false; this.stuck = 0; this.rev = 0;
  }
  update(dt) {
    const v = this.veh;
    if (!v.driver || v.isWrecked || this.done) { v.input.throttle = 0; v.input.brake = 1; return; }
    const tp = this.points[this.idx];
    const d = Math.hypot(tp.x - v.pos.x, tp.z - v.pos.z);
    if (d < 14) { this.idx++; if (this.idx >= this.points.length) { this.done = true; return; } }
    const next = this.points[Math.min(this.idx, this.points.length - 1)];
    const after = this.points[Math.min(this.idx + 1, this.points.length - 1)];
    const k = clamp(1 - d / 40, 0, 0.6);
    const tx = next.x + (after.x - next.x) * k, tz = next.z + (after.z - next.z) * k;
    const [lx, lz] = v.worldToLocal(tx, tz);
    const ang = Math.atan2(lx, Math.max(0.5, lz));
    let steer = ang / (v.def.steer * 0.7);
    const col = this.game.collision;
    const probe = (a) => { const h = col.raycast(v.pos.x, v.pos.y + 0.8, v.pos.z, Math.sin(v.yaw + a), 0, Math.cos(v.yaw + a), 16, { ignoreProps: true }); return h ? h.t : 16; };
    const fl = probe(0.3), fr = probe(-0.3);
    if (fl < 7) steer -= 0.7; if (fr < 7) steer += 0.7;
    // cars ahead
    for (const o of this.game.vehicles.list) {
      if (o === v) continue;
      const [ox, oz] = v.worldToLocal(o.pos.x, o.pos.z);
      if (oz > 0 && oz < 14 && Math.abs(ox) < 2.4) steer += ox > 0 ? -0.6 : 0.6;
    }
    v.input.steer = clamp(steer, -1, 1);
    const turn = Math.abs(ang);
    const want = (turn > 0.9 ? 12 : turn > 0.5 ? 20 : v.def.top * 0.95) * (0.8 + this.skill * 0.25);
    const sp = v.speed;
    if (this.rev > 0) { this.rev -= dt; v.input.throttle = 0; v.input.brake = 1; v.input.steer = -v.input.steer; return; }
    if (Math.abs(sp) < 1.2) { this.stuck += dt; if (this.stuck > 1.6) { this.stuck = 0; this.rev = 1.0; } } else this.stuck = 0;
    v.input.throttle = sp < want ? 1 : 0;
    v.input.brake = sp > want + 4 ? 0.6 : 0;
    v.input.handbrake = turn > 1.0 && sp > 14;
  }
  progress() {
    const v = this.veh;
    const tp = this.points[Math.min(this.idx, this.points.length - 1)];
    return this.idx * 1000 - Math.hypot(tp.x - v.pos.x, tp.z - v.pos.z);
  }
}

// --------------------------------------------------------------------------- mission context
class Ctx {
  constructor(engine, def) {
    this.engine = engine;
    this.game = engine.game;
    this.def = def;
    this.title = def.title;
    this.waiters = [];
    this.fails = [];
    this.peds = [];
    this.cars = [];
    this.markers = [];
    this.blips = [];
    this.tickers = [];
    this.t = 0;
    this.aborted = false;
    this.noSpray = def.noSpray ?? true;
  }
  get player() { return this.game.player; }
  get hud() { return this.game.hud; }
  get L() { return this.game.map.landmarks; }

  _check(dt) {
    this.t += dt;
    for (const fn of this.tickers) fn(dt);
    for (const f of this.fails) {
      let r = null;
      try { r = f.fn(); } catch { r = null; }
      if (r) { this._rejectAll(new MissionFail(typeof r === 'string' ? r : f.reason)); return; }
    }
    for (let i = this.waiters.length - 1; i >= 0; i--) {
      const w = this.waiters[i];
      let done = false;
      try { done = w.check(dt); } catch (e) { this.waiters.splice(i, 1); w.reject(e); continue; }
      if (done) { this.waiters.splice(i, 1); w.resolve(done); }
    }
  }
  _rejectAll(err) {
    const ws = this.waiters.splice(0);
    for (const w of ws) w.reject(err);
    this.pendingError = err;
  }
  abort() { this.aborted = true; this._rejectAll(new MissionAbort('aborted')); }

  until(check, opts = {}) {
    if (this.pendingError) return Promise.reject(this.pendingError);
    return new Promise((resolve, reject) => {
      const start = this.t;
      this.waiters.push({
        check: (dt) => {
          if (opts.timeout && this.t - start > opts.timeout) { if (opts.onTimeout === 'resolve') return 'timeout'; throw new MissionFail(opts.timeoutReason || 'You ran out of time.'); }
          return check(dt);
        }, resolve, reject,
      });
    });
  }
  wait(sec) { const end = this.t + sec; return this.until(() => this.t >= end); }
  tick(fn) { this.tickers.push(fn); return () => { const i = this.tickers.indexOf(fn); if (i >= 0) this.tickers.splice(i, 1); }; }
  failIf(fn, reason) { const f = { fn, reason }; this.fails.push(f); return () => { const i = this.fails.indexOf(f); if (i >= 0) this.fails.splice(i, 1); }; }

  // ---------------------------------------------------------------- UI
  help(text, dur = 6) { this.hud.help(text, dur); }
  objective(text) { this.hud.objective(text); }
  async say(speaker, text, dur) {
    const d = dur ?? clamp(1.6 + text.length * 0.052, 2.2, 7);
    this.hud.subtitle(text, speaker, d + 0.2);
    const spk = this.speakers?.[speaker];
    if (spk && !spk.dead) spk.animState.talking = true;
    const end = this.t + d;
    await this.until(() => this.t >= end || (this.inCutscene && this.game.input.hit('skip')));
    if (spk && !spk.dead) spk.animState.talking = false;
  }
  async lines(list) { for (const [who, text, dur] of list) await this.say(who, text, dur); }
  timer(sec, reason = 'You ran out of time.') {
    const end = this.t + sec;
    this.timerEnd = end;
    const stop = this.tick(() => this.hud.setTimer(end - this.t));
    const unfail = this.failIf(() => this.t > end, reason);
    return () => { stop(); unfail(); this.hud.setTimer(null); };
  }

  // ---------------------------------------------------------------- cutscenes
  async cutscene(fn) {
    const game = this.game;
    this.inCutscene = true;
    game.cutscene = true;
    this.hud.letterbox(true);
    game.police.enabled = false;
    const p = game.player;
    p.moveTarget.set(0, 0);
    p.aiming = false;
    try { await fn(); } finally {
      this.inCutscene = false;
      game.cutscene = false;
      this.hud.letterbox(false);
      game.rig.clearCinematic();
      game.police.enabled = true;
      for (const s of Object.values(this.speakers || {})) if (s && !s.dead) s.animState.talking = false;
    }
  }
  shot(from, to, fov = 50, snap = true) {
    const f = Array.isArray(from) ? new THREE.Vector3(...from) : from.clone();
    const t = Array.isArray(to) ? new THREE.Vector3(...to) : to.clone();
    this.game.rig.setCinematic(f, t, fov);
    if (snap) { this.game.camera.position.copy(f); this.game.camera.lookAt(t); this.game.rig.fov = fov; }
  }
  // camera framing two characters talking
  twoShot(a, b, side = 1, dist = 3.2, height = 1.55) {
    const pa = a.pos, pb = b.pos;
    const mid = new THREE.Vector3((pa.x + pb.x) / 2, (pa.y + pb.y) / 2 + height, (pa.z + pb.z) / 2);
    const dx = pb.x - pa.x, dz = pb.z - pa.z; const l = Math.hypot(dx, dz) || 1;
    const nx = -dz / l * side, nz = dx / l * side;
    this.shot(mid.clone().add(new THREE.Vector3(nx * dist, 0.15, nz * dist)), mid, 45);
  }
  overShoulder(from, to, dist = 1.4) {
    const d = new THREE.Vector3(to.pos.x - from.pos.x, 0, to.pos.z - from.pos.z).normalize();
    const side = new THREE.Vector3(-d.z, 0, d.x);
    const cam = new THREE.Vector3(from.pos.x, from.pos.y + 1.7, from.pos.z).addScaledVector(d, -dist).addScaledVector(side, 0.55);
    this.shot(cam, new THREE.Vector3(to.pos.x, to.pos.y + 1.55, to.pos.z), 40);
  }
  face(a, b) { a.faceTowards(b.pos.x, b.pos.z, 0); a.root.rotation.y = a.yaw; }
  speakersSet(map) { this.speakers = map; }

  // ---------------------------------------------------------------- entities
  ped(x, z, opts = {}) {
    const p = this.game.peds.spawnPed(x, z, { persistent: true, ...opts });
    if (opts.brain === undefined && !opts.gang) { p.brain = 'script'; p.state = 'idle'; }
    if (opts.yaw != null) p.setYaw(opts.yaw);
    this.peds.push(p);
    if (opts.invincible) p.invincible = true;
    return p;
  }
  enemy(x, z, opts = {}) {
    const p = this.game.peds.spawnPed(x, z, { persistent: true, brain: 'gang', gang: opts.gang || 'vipers', state: opts.guard ? 'guard' : 'attack', weapon: opts.weapon || 'pistol', health: opts.health ?? 100, armor: opts.armor ?? 0, appearance: opts.appearance });
    p.threat = this.player;
    p.accuracy = opts.accuracy ?? 0.4;
    p.damageMul = opts.damageMul ?? 0.5;
    if (opts.guard) { p.guardFace = opts.face ?? Math.random() * 6.28; }
    p.missionEnemy = true;
    this.peds.push(p);
    if (opts.blip !== false) this.blipEntity(p, 0xff3030, 'dot', true);
    if (opts.arrow !== false) this.targetArrow(p);
    return p;
  }
  // bobbing red arrow over a target's head (San Andreas style), removed on death / cleanup
  targetArrow(p) {
    const arrow = new THREE.Mesh(ARROW_GEO(), ARROW_MAT());
    arrow.renderOrder = 3;
    p.root.add(arrow);
    p.targetArrow = arrow;
    const ph = Math.random() * 6;
    this.tick(() => {
      if (p.removed) return;
      const s = 1 / (p.root.scale.y || 1);
      arrow.visible = !p.dead && !p.ragdolling && !p.vehicle;
      arrow.scale.setScalar(s);
      arrow.position.y = (2.3 + Math.sin(this.game.time * 4 + ph) * 0.08) * s;
      arrow.rotation.y = this.game.time * 2 + ph;
    });
    return arrow;
  }
  car(type, x, z, yaw = 0, opts = {}) {
    const v = this.game.vehicles.spawn(type, x, z, yaw, { persistent: true, ...opts });
    this.cars.push(v);
    return v;
  }
  driver(car, opts = {}) {
    const p = this.ped(car.pos.x, car.pos.z, { brain: 'script', ...opts });
    car.putIn(p, opts.seat ?? 0);
    return p;
  }
  follower(ped, slot = 0) { ped.brain = 'civilian'; ped.state = 'follow'; ped.follow = this.player; ped.followSlot = slot; ped.persistent = true; return ped; }
  marker(x, z, opts = {}) { const m = this.game.pickups.addMarker(x, z, opts); this.markers.push(m); return m; }
  removeMarker(m) { this.game.pickups.removeMarker(m); }
  blipEntity(ent, color = 0x4a90ff, icon = 'dot', small = false) {
    const b = { x: 0, z: 0, color, icon, small, entity: ent };
    this.game.blips.add(b);
    this.blips.push(b);
    const upd = () => {
      if (ent.removed || ent.dead || ent.isWrecked) { this.game.blips.delete(b); return; }
      const p = ent.vehicle ? ent.vehicle.pos : ent.ragdolling ? ent.ragdoll.center : ent.pos;
      b.x = p.x; b.z = p.z;
    };
    upd();
    this.tick(upd);
    return b;
  }
  unblip(b) { this.game.blips.delete(b); }
  gps(x, z) { this.hud.routeTo({ x, z }); }
  gpsOff() { this.hud.routeTo(null); }

  // ---------------------------------------------------------------- objectives
  async goTo(x, z, opts = {}) {
    const m = this.marker(x, z, { radius: opts.radius ?? (opts.vehicle ? 4 : 1.6), color: opts.color ?? 0xffd23f, label: opts.label, vehicleOnly: !!opts.vehicle, footOnly: !!opts.onFoot, icon: 'dot', height: opts.height });
    if (opts.text) this.objective(opts.text);
    this.gps(x, z);
    let entered = false;
    m.onEnter = () => { entered = true; };
    await this.until(() => {
      if (!entered) return false;
      const pl = this.player;
      if (opts.inCar && pl.vehicle !== opts.inCar) { entered = false; this.help(opts.inCarMsg || 'You need the right vehicle.'); return false; }
      if (opts.slow && pl.vehicle && pl.vehicle.speedAbs > 6) return false;
      if (opts.condition && !opts.condition()) { entered = false; return false; }
      return true;
    });
    this.removeMarker(m);
    this.gpsOff();
    this.game.audio?.play('checkpoint');
    if (opts.stop && this.player.vehicle) { this.player.vehicle.input.brake = 1; }
  }
  getIn(car, text) {
    if (text) this.objective(text);
    const b = this.blipEntity(car, 0x4aa3ff, 'car');
    this.gps(car.pos.x, car.pos.z);
    return this.until(() => this.player.vehicle === car && !this.game.vehicles.isBusy(this.player)).then(() => { this.unblip(b); this.gpsOff(); });
  }
  killAll(list, text, opts = {}) {
    if (text) this.objective(text);
    const total = list.length;
    return this.until(() => {
      const left = list.filter((p) => !p.dead && !p.removed).length;
      if (opts.counter) this.hud.setCounter(opts.counter, `${total - left}/${total}`);
      return left === 0;
    }).then(() => { if (opts.counter) this.hud.setCounter(null); });
  }
  async loseWanted(text = 'Lose the cops.') {
    if (this.game.police.level === 0) return;
    this.objective(text);
    await this.until(() => this.game.police.level === 0);
  }
  wanted(level) { this.game.police.setLevel(Math.max(this.game.police.level, level)); }
  keepAlive(ent, reason) { return this.failIf(() => ent.dead || ent.isWrecked, reason); }
  cash(amount) { this.player.money += amount; this.hud.moneyFlash(amount); }
  distTo(ent) { const p = this.player.vehicle ? this.player.vehicle.pos : this.player.pos; const e = ent.vehicle ? ent.vehicle.pos : ent.pos || ent; return Math.hypot(p.x - e.x, p.z - e.z); }
  // make a ped shoot at the player (or another target) from inside a vehicle
  driveBy(ped, target = null, range = 35) {
    let t = rand(0.5, 1.5);
    return this.tick((dt) => {
      if (ped.dead || !ped.vehicle) return;
      const tg = target || this.player;
      const tp = tg.vehicle ? tg.vehicle.pos : tg.pos;
      const d = Math.hypot(tp.x - ped.vehicle.pos.x, tp.z - ped.vehicle.pos.z);
      ped.aiming = d < range;
      t -= dt;
      if (d < range && t <= 0) {
        t = rand(0.35, 1.0);
        const from = ped.vehicle.pos.clone().add(new THREE.Vector3(0, 1.3, 0));
        const to = new THREE.Vector3(tp.x + rand(-1.5, 1.5), tp.y + 1, tp.z + rand(-1.5, 1.5));
        const dir = to.sub(from).normalize();
        const hit = this.game.combat.raycast(from.x, from.y, from.z, dir.x, dir.y, dir.z, 60, ped);
        this.game.effects.tracers.add(from, hit ? hit.point : from.clone().addScaledVector(dir, 60));
        this.game.effects.muzzleFlash(from, dir);
        if (hit) this.game.combat.applyHit(hit, { id: 'smg', damage: 9 }, ped, dir);
        this.game.audio?.playAt('smg', from, 0.9, { gun: true });
      }
    });
  }

  cleanup(passed) {
    const game = this.game;
    this.hud.setTimer(null); this.hud.setCounter(null); this.hud.setBar(null); this.hud.clearObjective(); this.gpsOff();
    for (const m of this.markers) game.pickups.removeMarker(m);
    for (const b of this.blips) game.blips.delete(b);
    for (const p of this.peds) {
      if (p.targetArrow) { p.targetArrow.parent?.remove(p.targetArrow); p.targetArrow = null; }
      if (p.removed) continue;
      if (p.keep) continue;
      p.persistent = false; p.invincible = false;
      if (p.brain === 'script') { p.brain = 'civilian'; p.state = 'wander'; p.node = null; }
      if (p.state === 'follow') { p.state = 'wander'; p.follow = null; }
    }
    for (const v of this.cars) { if (!v.removed && !v.keep) { v.persistent = false; v.locked = false; if (v.ai && !v.traffic) v.ai = null; } }
    game.cutscene = false;
    game.hud.letterbox(false);
    game.rig.clearCinematic();
    game.police.enabled = true;
    game.missions.maxWanted = null;
    game.missions.noBust = false;
  }
}

// --------------------------------------------------------------------------- engine
export class Missions {
  constructor(game, story) {
    this.game = game;
    this.story = story;
    story.init?.(game);
    this.completed = new Set();
    this.active = null;
    this.contactMarkers = [];
    this.log = [];
    this.total = story.missions.length;
    this.maxWanted = null;
    this.noBust = false;
    this.gangDensity = { kings: 0.35, vipers: 0.4, cuervos: 0.3 };
    this.refreshTimer = 0;
    game.events.on('playerDied', () => this.failActive('You died.'));
    game.events.on('busted', () => { if (this.active && !this.noBust) this.failActive('You got busted.'); });
  }

  get completedCount() { return this.completed.size; }

  available() {
    return this.story.missions.filter((m) => !this.completed.has(m.id) && (m.requires || []).every((r) => this.completed.has(r)));
  }

  refreshContacts() {
    for (const m of this.contactMarkers) this.game.pickups.removeMarker(m);
    this.contactMarkers = [];
    if (this.active) return;
    const shown = new Set();
    for (const def of this.available()) {
      if (def.auto && !def.start) continue;
      const pos = def.start(this.game.map.landmarks);
      const key = `${Math.round(pos.x)},${Math.round(pos.z)}`;
      if (shown.has(key)) continue;
      shown.add(key);
      const mk = this.game.pickups.addMarker(pos.x, pos.z, { color: 0xffd23f, radius: 1.3, label: def.title, icon: 'dot', footOnly: !def.startInCar, onEnter: () => this.start(def) });
      mk.blip.letter = def.contact;
      mk.blip.color = 0xffd23f;
      this.contactMarkers.push(mk);
    }
  }

  start(def) {
    if (this.active) return;
    const game = this.game;
    if (def.auto) game.police.clear();
    else if (game.police.level > 0 && !def.allowWanted) { game.hud.help('Lose your wanted level before starting a mission.'); return; }
    for (const m of this.contactMarkers) game.pickups.removeMarker(m);
    this.contactMarkers = [];
    const ctx = new Ctx(this, def);
    this.active = ctx;
    game.hud.bigMessage(def.title, 'title', 3.5);
    game.events.emit('missionStart', def.id);
    const run = async () => {
      try {
        await def.run(ctx, game);
        this._pass(ctx);
      } catch (e) {
        if (e instanceof MissionAbort) { ctx.cleanup(false); this.active = null; this.refreshContacts(); return; }
        if (e instanceof MissionFail) this._fail(ctx, e.reason);
        else { console.error(e); this._fail(ctx, 'Something went wrong.'); }
      }
    };
    run();
  }

  _pass(ctx) {
    const game = this.game;
    const def = ctx.def;
    ctx.cleanup(true);
    this.completed.add(def.id);
    this.active = null;
    const reward = def.reward ?? 0;
    if (reward) game.player.money += reward;
    game.stats.missions++;
    game.hud.bigMessage('MISSION PASSED!', 'passed', 5, reward ? `$${reward.toLocaleString()}` : 'RESPECT +');
    game.audio?.play('passed');
    if (def.log) this.log.push(def.log);
    game.events.emit('missionPassed', def.id);
    const next = this.story.missions.find((m) => m.auto && !this.completed.has(m.id) && (m.requires || []).every((r) => this.completed.has(r)));
    setTimeout(() => {
      if (def.after) def.after(game);
      if (next) this.start(next);
      else this.refreshContacts();
      game.save?.save(true);
    }, 5500);
    if (def.chapterEnd) setTimeout(() => game.hud.bigMessage(def.chapterEnd[0], 'chapter', 5, def.chapterEnd[1]), 6000);
  }

  _fail(ctx, reason) {
    const game = this.game;
    ctx.cleanup(false);
    this.active = null;
    // while the wasted / busted screen is up, hold the message until the player has respawned
    const show = () => { game.hud.bigMessage('MISSION FAILED!', 'failed', 4.5, reason || ''); game.audio?.play('failed'); };
    const st = game.gameplay?.state;
    if (st === 'dead' || st === 'busted' || st === 'respawning') game.gameplay.afterRespawn = show;
    else show();
    game.events.emit('missionFailed', ctx.def.id);
    setTimeout(() => this.refreshContacts(), 4000);
  }

  failActive(reason) { if (this.active) this.active._rejectAll(new MissionFail(reason)); }
  abortActive() { if (this.active) this.active.abort(); }

  canEnterVehicle(v) { return !(this.active && this.active.lockedCars && this.active.lockedCars.includes(v)); }

  update(dt) {
    if (this.active) this.active._check(dt);
  }

  serialize() { return { completed: [...this.completed], log: this.log }; }
  load(data) { this.completed = new Set(data?.completed || []); this.log = data?.log || []; }
}

export { Ctx };
