// Keeps the Sol Line in service: a passenger train and a freight train shuttling between Dry Wells and
// Union Station. The line is single track except for the passing loop at Fern Creek, so a train may only
// run onto the single-track stretch either side of it (west: to Dry Wells, east: to Union Station) once
// the other train is off it — they cross at Fern Creek, the passenger train at the platform and the
// freight on the loop. Also: level crossings that close while a train is near (traffic waits at them),
// station and train blips, and helpers for boarding hints, missions and the teleport menu.
import { dist2 } from '../core/utils.js';

export class RailSystem {
  constructor(game) {
    this.game = game;
    this.rail = game.map.roadInfo?.rail || null;
    this.train = null;     // the passenger train (missions and the taxi / teleport code use this one)
    this.freight = null;
    this.tokens = { W: null, E: null };
    this.crossings = this.rail ? this.rail.crossings.filter((c) => c.kind === 'level').map((c) => ({ ...c, active: false })) : [];
    this.hintT = 0;
    this.held = null;      // a mission has the line: other trains wait in the loop
    this.blips = new Map();
    if (this.rail) for (const st of game.map.landmarks.stations || []) game.blips.add({ x: st.x, z: st.z, icon: 'train', color: 0x9fe3ff, small: true, noEdge: true });
  }

  get trains() { return [this.train, this.freight].filter((t) => t && !t.removed); }

  station(key) { return (this.game.map.landmarks.stations || []).find((s) => s.key === key); }

  spawnTrain() {
    const g = this.game;
    const union = this.station('union');
    const tmp = g.vehicles.spawn('train', 0, 0, 0, { persistent: true, s: 0, dwell: 8, dirS: -1, track: 0 });
    // centre the train on Union Station's platform
    if (union) { tmp.s = union.s + tmp.len / 2; tmp._place(); tmp._atStation = union; }
    tmp.persistent = true;
    this.train = tmp;
    return tmp;
  }

  spawnFreight() {
    const g = this.game;
    const loop = this.rail.loop;
    // as many wagons as the loop can hold
    const room = loop ? loop.m1 - loop.m0 - 17 - 14 : 100;
    const wagons = Math.max(3, Math.min(7, Math.floor(room / 15.7)));
    const tmp = g.vehicles.spawn('freight', 0, 0, 0, { persistent: true, s: 0, dwell: 20, dirS: 1, track: 1, wagons, seed: 3 });
    const dry = this.station('dry');
    if (dry) { tmp.s = Math.max(tmp.len + 3, dry.s + tmp.len / 2); tmp._place(); tmp._atStation = dry; }
    tmp.persistent = true;
    this.freight = tmp;
    return tmp;
  }

  // Park every other train in the Fern Creek loop and hold it there while `train` has the line (missions)
  clearLineFor(train) {
    this.held = train;
    const loop = this.rail?.loop;
    for (const t of this.trains) {
      if (t === train || !loop) continue;
      if (t.driver?.isPlayer) continue;
      t.track = 1;
      t.s = loop.m0 + 6 + t.len; t.v = 0; t.dirS = 1; t.dwell = 1e9; t._atStation = null; t._place();
    }
    this.tokens = { W: null, E: null };
  }
  releaseLine() {
    this.held = null;
    for (const t of this.trains) if (t.dwell > 1e6) t.dwell = 4;
  }

  update(dt) {
    if (!this.rail) return;
    const g = this.game;
    if (!this.train || this.train.removed) this.spawnTrain();
    if (!this.freight || this.freight.removed) this.spawnFreight();
    const trains = this.trains;
    this._dispatch(trains);
    // crossings close while a train is on them or closing in
    for (const c of this.crossings) {
      c.active = false;
      for (const tr of trains) {
        const head = tr.s, tailS = tr.s - tr.len;
        const on = c.s > tailS - 12 && c.s < head + 12;
        const approaching = tr.v > 0.5 ? c.s > head && c.s - head < 160 : tr.v < -0.5 ? c.s < tailS && tailS - c.s < 160 : false;
        if (on || approaching) { c.active = true; break; }
      }
    }
    // moving blips on the map for the trains
    for (const tr of trains) {
      let b = this.blips.get(tr);
      if (!b) { b = { x: 0, z: 0, icon: 'train', color: tr.freight ? 0xffa14a : 0x6fd3ff, small: true, noEdge: true }; this.blips.set(tr, b); g.blips.add(b); }
      const mid = tr.cars[Math.floor(tr.cars.length / 2)]?.pos || tr.pos;
      b.x = mid.x; b.z = mid.z;
    }
    for (const [tr, b] of this.blips) if (tr.removed) { g.blips.delete(b); this.blips.delete(tr); }
    // "press F to board" when standing on a platform next to a stopped passenger train
    this.hintT -= dt;
    const p = g.player, tr = this.train;
    if (tr && !p.vehicle && !p.dead && Math.abs(tr.v) < 0.5 && this.hintT <= 0) {
      const d = tr.nearestDoor(p.pos);
      if (d && Math.hypot(d.x - p.pos.x, d.z - p.pos.z) < 6) { this.hintT = 12; g.hud?.help('Press <b>F</b> to board the train. Ride it along the Sol Line, or climb into the cab at the front to drive it.', 5); }
    }
  }

  // Signals. W = the single track west of the loop (to Dry Wells), E = east of it (to Union Station).
  // A train holds a section while any part of it is on it; an autopilot train needs the section before
  // it may leave the loop onto it, and never closes within 25 m of another train on the same track.
  // (The loop is longer than either train, so a train is always wholly inside it before its signal.)
  _dispatch(trains) {
    const loop = this.rail.loop;
    for (const t of trains) { t.limitLo = -Infinity; t.limitHi = Infinity; t.hold = false; }
    if (!loop) return;
    const M0 = loop.m0, M1 = loop.m1;
    const inW = (t) => t.s - t.len < M0, inE = (t) => t.s > M1;
    const onSec = { W: inW, E: inE };
    for (const k of ['W', 'E']) {
      const h = this.tokens[k];
      if (h && (h.removed || !onSec[k](h))) this.tokens[k] = null;
    }
    for (const t of trains) for (const k of ['W', 'E']) if (onSec[k](t) && !this.tokens[k]) this.tokens[k] = t;
    const grant = (k, t) => {
      const h = this.tokens[k];
      if (h === t) return true;
      if (h) return false;
      if (trains.some((o) => o !== t && onSec[k](o))) return false;
      this.tokens[k] = t;
      return true;
    };
    for (const t of trains) {
      if (t.driver?.isPlayer) continue;
      if (this.held && t !== this.held) { t.hold = true; continue; }
      // a train asks for the next section only once it's wholly inside the loop (asking from the far
      // single-track section would deadlock: each train waiting for the section the other stands on)
      if (t.dirS > 0 && !inE(t) && !(!inW(t) && grant('E', t))) { t.limitHi = M1 - 4; t.hold = !inW(t); }
      if (t.dirS < 0 && !inW(t) && !(!inE(t) && grant('W', t))) { t.limitLo = M0 + 4; t.hold = !inE(t); }
      // spacing: another train ahead on the same track (on the single-track sections everyone shares it)
      for (const o of trains) {
        if (o === t) continue;
        const shared = (s) => s < M0 || s > M1 || o.track === t.track;
        if (t.dirS > 0) { const near = o.s - o.len; if (near > t.s - 1 && shared(near)) t.limitHi = Math.min(t.limitHi, near - 25); }
        else { const near = o.s; if (near < t.s - t.len + 1 && shared(near)) t.limitLo = Math.max(t.limitLo, near + 25); }
      }
    }
    // hard stop: trains never pass through each other (a player at the controls can still bump one)
    for (const a of trains) for (const b of trains) {
      if (a === b || a.s > b.s) continue; // a is behind b along +s
      const gap = (b.s - b.len) - a.s;
      const shared = (s) => s < M0 || s > M1 || a.track === b.track;
      if (gap < 0.5 && shared(a.s)) {
        const push = 0.5 - gap;
        const mover = Math.abs(a.v) >= Math.abs(b.v) ? a : b;
        if (Math.max(Math.abs(a.v), Math.abs(b.v)) > 4) this.game.rig?.addShake(0.5);
        if (mover === a) a.s -= push; else b.s += push;
        a.v = Math.min(a.v, 0); b.v = Math.max(b.v, 0);
        a._place(); b._place();
      }
    }
  }

  // Distance along a heading to an active level crossing (for AI drivers), or Infinity
  crossingAhead(x, z, fx, fz, maxD = 40) {
    let best = Infinity;
    for (const c of this.crossings) {
      if (!c.active) continue;
      const dx = c.x - x, dz = c.z - z;
      const along = dx * fx + dz * fz;
      if (along < 0 || along > maxD) continue;
      const lat = Math.abs(dx * fz - dz * fx);
      if (lat > 9) continue;
      best = Math.min(best, along - 7);
    }
    return best;
  }

  nearestStation(x, z) {
    let best = null, bd = Infinity;
    for (const st of this.game.map.landmarks.stations || []) { const d = dist2(st.x, st.z, x, z); if (d < bd) { bd = d; best = st; } }
    return best;
  }
}
