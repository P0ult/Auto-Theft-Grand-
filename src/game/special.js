// The special ability (GTA V): a meter under the minimap that you fill by driving fast, drifting, landing
// headshots and taking people down. Press Caps Lock (or click both sticks) and the world slows right down for a
// few seconds: bullet time on foot, and on the road a slowed world with extra grip to thread the traffic.
// The screen loses some colour while it's on. The meter drains while it's active; press again to stop early.
import { GP } from '../core/input.js';
import { clamp } from '../core/utils.js';

const DRAIN = 0.11;   // meter per real second while active (about nine seconds from full)
const FOOT = 0.38, DRIVE = 0.5;

export class Special {
  constructor(game) {
    this.game = game;
    this.meter = 1;
    this.active = false;
    this.k = 0;          // the screen effect, eased in and out
    this.lastBlocked = 0;
    game.events.on('kill', (killer, victim, weapon, part) => {
      if (!killer?.isPlayer || this.active) return;
      this.add(part === 'head' ? 0.1 : 0.04);
    });
    game.events.on('playerDied', () => this.stop());
    game.events.on('busted', () => this.stop());
  }

  add(x) { this.meter = clamp(this.meter + x, 0, 1); }

  get pressed() {
    const input = this.game.input;
    return input.keyHit('CapsLock') || (input.gpDown(GP.LS) && input.gpDown(GP.RS) && (input.gpHit(GP.LS) || input.gpHit(GP.RS)));
  }

  start() {
    const g = this.game;
    if (this.active) return;
    if (this.meter < 0.12) { if (g.time - this.lastBlocked > 1) { this.lastBlocked = g.time; g.audio?.play('dryfire'); } return; }
    this.active = true;
    g.audio?.play('swoosh', 1);
    g.events.emit('specialOn');
  }

  stop() {
    if (!this.active) return;
    this.active = false;
    delete this.game.slowmo.special;
    this.game.audio?.play('swoosh', 0.6);
  }

  // (uses the real frame time: the meter mustn't drain slower because the world is slowed)
  update(dt) {
    const g = this.game, p = g.player, input = g.input;
    const real = input.frameDt || dt;
    const playing = g.gameplay?.state === 'playing' && !g.cutscene && !p.dead;
    if (playing && input.enabled && !g.hud?.menuOpen && !g.phone?.open && this.pressed) { if (this.active) this.stop(); else this.start(); }
    if (!playing) this.stop();
    if (this.active) {
      this.meter -= DRAIN * real;
      if (this.meter <= 0) { this.meter = 0; this.stop(); }
      else g.slowmo.special = p.vehicle ? DRIVE : FOOT;
    } else if (playing) {
      // filling up: a trickle all the time, faster at speed and while drifting
      let gain = 0.004;
      const v = p.vehicle;
      if (v && !v.def.kind) {
        const spd = v.speedAbs;
        if (spd > 22) gain += 0.012 * Math.min(1, (spd - 22) / 20);
        if (g.gameplay?.drift > 10) gain += 0.03;
        if (v.airborne) gain += 0.02;
      }
      this.add(gain * real);
    }
    // the look: drained colour and a little extra fringing
    this.k += ((this.active ? 1 : 0) - this.k) * Math.min(1, real * 6);
    const u = g.post?.composite?.uniforms;
    if (u && !p.dead) {
      u.uDesat.value = 0.42 * this.k;
      u.uChroma.value = 0.0022 + 0.004 * this.k;
    }
  }

  // extra tyre grip for the player's car while it's on (vehicle.js)
  get grip() { return this.active && this.game.player.vehicle ? 1.25 : 1; }
}
