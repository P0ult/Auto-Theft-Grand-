// Vigilante: sitting in the driver's seat of a police car, press J (the taxi-job button) to go on patrol.
// Dispatch sends you after a car full of suspects; they run, and from level 3 they shoot back. Take them
// all down before the clock runs out for a cash reward that grows with each level; the next call comes in
// straight after. Killing suspects doesn't count against you, and neither does the shooting.
import * as THREE from 'three';
import { Traffic } from './traffic.js';
import { FleeDriver } from './npccrime.js';
import { randomAppearance } from '../entities/humanoid.js';
import { rand, pick, clamp, RNG } from '../core/utils.js';

const CARS = ['kestrel', 'brawler', 'meridian', 'summit', 'bouncer', 'pico'];
const CRIMES = ['armed robbery', 'a hit and run', 'carjacking', 'a jewellery heist', 'assault on an officer', 'a drive-by', 'a bank job'];

export class Vigilante {
  constructor(game) {
    this.game = game;
    this.active = false;
    this.level = 0;
    this.rng = new RNG(99);
    this.hinted = false;
  }
  get _ctl() { const g = this.game; return g.input.enabled && !g.cutscene && !g.player.dead && g.gameplay?.state === 'playing' && !g.hud?.menuOpen; }

  update(dt) {
    const g = this.game, p = g.player, v = p.vehicle;
    const inCop = v && v.def.police && !v.isBoat && p.seat === 0 && v.driver === p;
    if (inCop && this._ctl && !g.missions?.active && !g.races?.active) {
      if (g.input.hit('taxiJob')) { if (this.active) this.stop('Off duty.'); else this.start(); }
      else if (!this.active && !this.hinted) { this.hinted = true; g.hud?.help('You\'re in a police car. Press <b>J</b> to go on <b>vigilante</b> patrol.', 6); }
    }
    if (!this.active) return;
    if (g.missions?.active) { this.stop(null); return; }
    if (this.between > 0) { this.between -= dt; if (this.between <= 0) this._dispatch(); return; }
    this.clock -= dt;
    g.hud?.setTimer(Math.max(0, this.clock));
    const left = this.suspects.filter((s) => !s.dead && !s.removed);
    g.hud?.setCounter('SUSPECTS', `${this.total - left.length}/${this.total}`);
    // out of the car too long, or out of time
    if (!p.vehicle || !p.vehicle.def.police) { this.outT = (this.outT || 0) + dt; if (this.outT > 60) { this.stop('You abandoned your patrol car.'); return; } } else this.outT = 0;
    if (this.clock <= 0) { this.stop('The suspects got away.', true); return; }
    // shooters in the car (from level 3)
    for (const s of left) {
      if (!s.vehicle || s.seat === 0 || this.level < 3) continue;
      s.fireT = (s.fireT ?? rand(1, 2)) - dt;
      const pp = p.vehicle ? p.vehicle.pos : p.pos;
      const d = Math.hypot(pp.x - s.vehicle.pos.x, pp.z - s.vehicle.pos.z);
      if (d < 40 && s.fireT <= 0) {
        s.fireT = rand(0.25, 0.7);
        const from = s.vehicle.pos.clone().add(new THREE.Vector3(0, 1.3, 0));
        const to = new THREE.Vector3(pp.x + rand(-1.8, 1.8), pp.y + 1, pp.z + rand(-1.8, 1.8));
        const dir = to.sub(from).normalize();
        const hit = g.combat.raycast(from.x, from.y, from.z, dir.x, dir.y, dir.z, 60, s);
        g.effects.tracers.add(from, hit ? hit.point : from.clone().addScaledVector(dir, 60));
        g.effects.muzzleFlash(from, dir);
        if (hit) g.combat.applyHit(hit, { id: 'pistol', damage: 8 }, s, dir);
        g.audio?.playAt('pistol', from, 0.8, { gun: true });
      }
    }
    // on foot (bailed out of a wreck): they fight
    for (const s of left) if (!s.vehicle && s.state !== 'attack') { s.threat = p; s.setState('attack'); }
    // keep the blips on them
    for (const s of this.suspects) { const b = s._vblip; if (!b) continue; if (s.dead || s.removed) { g.blips.delete(b); s._vblip = null; continue; } const q = s.vehicle ? s.vehicle.pos : s.pos; b.x = q.x; b.z = q.z; }
    if (!left.length) this._cleared();
  }

  start() {
    const g = this.game;
    this.active = true;
    this.level = 0;
    this.earned = 0;
    this.between = 0;
    g.hud?.bigMessage('VIGILANTE', 'title', 2.5, 'Take down the suspects');
    this._dispatch();
  }

  _dispatch() {
    const g = this.game, p = g.player;
    this.level++;
    const pp = p.vehicle ? p.vehicle.pos : p.pos;
    const smp = Traffic.sampleLane(g, pp.x, pp.z, 220, 420, (e) => e.type !== 'freeway' && e.type !== 'ramp' && e.type !== 'dirt') || Traffic.sampleLane(g, pp.x, pp.z, 120, 300);
    if (!smp) { this.stop('No calls right now.'); return; }
    const pts = g.map.roads.lanePath(smp.start.e, smp.start.dir, smp.start.lane);
    const a = pts[Math.min(pts.length - 1, 1)], b = pts[Math.min(pts.length - 1, 2)] || a;
    const car = g.vehicles.spawn(pick(CARS), smp.x, smp.z, Math.atan2(b[0] - a[0], b[2] - a[2]), { y: smp.y, persistent: true });
    car.health = car.maxHealth = 700 + this.level * 80;
    const n = clamp(1 + Math.floor(this.level / 2), 1, 4);
    this.suspects = [];
    for (let k = 0; k < n; k++) {
      const s = g.peds.spawnPed(smp.x, smp.z, { persistent: true, brain: 'gang', gang: 'vipers', weapon: pick(['pistol', 'smg', 'pistol', 'shotgun']), appearance: randomAppearance(this.rng, { female: this.rng.chance(0.15), shirtType: pick(['tank', 'tee', 'jacket']), bandana: this.rng.chance(0.5) ? 0x111111 : null, hairStyle: pick(['cap', 'buzz', 'short']), glasses: this.rng.chance(0.4) }) });
      s.criminal = true;
      s.accuracy = 0.3 + this.level * 0.03;
      car.putIn(s, k);
      s._vblip = { x: smp.x, z: smp.z, color: 0xff3030, icon: 'dot', small: k > 0 };
      g.blips.add(s._vblip);
      this.suspects.push(s);
    }
    car.ai = new FleeDriver(g, car, () => (p.vehicle ? p.vehicle.pos : p.pos));
    this.car = car;
    this.total = n;
    this.clock = 100 + this.level * 12;
    g.hud?.help(`Dispatch: level ${this.level}. ${n} suspect${n > 1 ? 's' : ''} wanted for ${pick(CRIMES)}. Take them down.`, 5);
    g.hud?.routeTo?.(null);
  }

  _cleared() {
    const g = this.game, p = g.player;
    const pay = this.level * 500 + Math.round(Math.max(0, this.clock) * 5);
    p.money += pay;
    this.earned += pay;
    g.hud?.moneyFlash?.(pay);
    g.audio?.play('cash');
    g.hud?.bigMessage('SUSPECTS DOWN', 'passed', 2.5, `$${pay.toLocaleString()}`);
    g.stats.vigilanteBest = Math.max(g.stats.vigilanteBest || 0, this.level);
    if (this.car) { this.car.persistent = false; this.car = null; }
    this.suspects = [];
    this.between = 3.5; // the next call comes in shortly
    g.hud?.setTimer(null);
  }

  stop(msg, failed = false) {
    const g = this.game;
    if (!this.active) return;
    this.active = false;
    for (const s of this.suspects || []) { if (s._vblip) g.blips.delete(s._vblip); s._vblip = null; if (!s.dead) { s.persistent = false; } }
    if (this.car) { this.car.persistent = false; this.car = null; }
    this.suspects = [];
    g.hud?.setTimer(null); g.hud?.setCounter(null);
    if (msg) g.hud?.bigMessage(failed ? 'VIGILANTE OVER' : 'OFF DUTY', failed ? 'failed' : 'hint', 3, `${msg} Reached level ${this.level} · earned $${(this.earned || 0).toLocaleString()}`);
  }
}
