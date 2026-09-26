// Keeps the Sol Line in service: one train shuttling between Dry Wells and Union Station, level
// crossings that close while it's near (traffic waits at them), station blips, and helpers for
// boarding hints, missions and the teleport menu.
import { dist2 } from '../core/utils.js';

export class RailSystem {
  constructor(game) {
    this.game = game;
    this.rail = game.map.roadInfo?.rail || null;
    this.train = null;
    this.crossings = this.rail ? this.rail.crossings.filter((c) => c.kind === 'level').map((c) => ({ ...c, active: false })) : [];
    this.hintT = 0;
    if (this.rail) for (const st of game.map.landmarks.stations || []) game.blips.add({ x: st.x, z: st.z, icon: 'train', color: 0x9fe3ff, small: true, noEdge: true });
  }

  spawnTrain() {
    const g = this.game;
    const union = (g.map.landmarks.stations || []).find((s) => s.key === 'union');
    const tmp = g.vehicles.spawn('train', 0, 0, 0, { persistent: true, s: 0, dwell: 8, dirS: -1 });
    // centre the train on Union Station's platform
    if (union) { tmp.s = union.s + tmp.len / 2; tmp._place(); tmp._atStation = union; }
    tmp.persistent = true;
    this.train = tmp;
    return tmp;
  }

  update(dt) {
    if (!this.rail) return;
    const g = this.game;
    if (!this.train || this.train.removed) this.spawnTrain();
    const tr = this.train;
    // crossings close while the train is on them or closing in
    for (const c of this.crossings) {
      const head = tr.s, tailS = tr.s - tr.len;
      const on = c.s > tailS - 12 && c.s < head + 12;
      const approaching = tr.v > 0.5 ? c.s > head && c.s - head < 160 : tr.v < -0.5 ? c.s < tailS && tailS - c.s < 160 : false;
      c.active = on || approaching;
    }
    // "press F to board" when standing on a platform next to a stopped train
    this.hintT -= dt;
    const p = g.player;
    if (!p.vehicle && !p.dead && Math.abs(tr.v) < 0.5 && this.hintT <= 0) {
      const d = tr.nearestDoor(p.pos);
      if (d && Math.hypot(d.x - p.pos.x, d.z - p.pos.z) < 6) { this.hintT = 12; g.hud?.help('Press <b>F</b> to board the train. Ride it along the Sol Line, or climb into the cab at the front to drive it.', 5); }
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
