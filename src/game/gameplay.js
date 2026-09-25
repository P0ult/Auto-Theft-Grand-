// Glue gameplay rules: stats, WASTED / BUSTED + respawn, stunt & drift bonuses, damage feedback,
// title-screen flyover camera, first-time hints.
import * as THREE from 'three';
import { clamp, wrapAngle } from '../core/utils.js';

export class Gameplay {
  constructor(game) {
    this.game = game;
    this.state = 'menu';
    this.deathTimer = 0;
    this.drift = 0; this.driftTime = 0; this.driftCombo = 0;
    this.airTime = 0; this.airStart = null; this.airFlips = 0;
    this.lastPos = new THREE.Vector3();
    this.flyT = 0;
    this.hinted = {};
    const g = game;
    const ev = g.events;
    const p = g.player;
    p.onDeath = () => this.onPlayerDeath();
    p.onDamaged = (src, dmg) => { g.hud?.damage(dmg); g.rig.addShake(Math.min(0.5, dmg / 60)); };
    ev.on('kill', (killer, victim, weapon, part) => {
      if (killer !== g.player) return;
      g.stats.kills++;
      if (victim.brain === 'cop') g.stats.copKills++;
      if (part === 'head') { g.stats.headshots++; }
    });
    ev.on('carjack', (by) => { if (by === g.player) g.stats.carsStolen++; });
    ev.on('enteredVehicle', (c, v) => {
      if (c !== g.player) return;
      if (!v.ownedByPlayer) { v.ownedByPlayer = true; if (v.parked || v.traffic) g.stats.carsStolen++; }
      v.traffic = false; v.ai = null; v.parked = false;
      if (!this.hinted.drive) { this.hinted.drive = true; g.hud.help('<b>W</b> accelerate · <b>S</b> brake/reverse · <b>Space</b> handbrake · <b>N</b> radio · <b>V</b> camera · <b>F</b> exit', 7); }
    });
    ev.on('vehicleExploded', (v) => { if (v.lastDamager === g.player || g.player.vehicle === v) g.stats.carsDestroyed++; });
    ev.on('pedRunOver', (c, v) => { if (v.driver === g.player) g.stats.runOver++; });
    ev.on('wantedUp', (l) => { g.stats.maxWanted = Math.max(g.stats.maxWanted, l); g.audio?.play('wanted'); });
    ev.on('busted', () => this.onBusted());
  }

  // ------------------------------------------------------------------ death / arrest
  onPlayerDeath() {
    const g = this.game;
    if (this.state === 'dead') return;
    this.state = 'dead';
    this.deathTimer = 0;
    g.stats.wasted++;
    g.hud.showWasted('wasted');
    g.audio?.play('wasted');
    g.timeScale = 0.35;
    g.events.emit('playerDied');
  }
  onBusted() {
    const g = this.game;
    if (this.state !== 'playing') return;
    this.state = 'busted';
    this.deathTimer = 0;
    g.stats.busted++;
    const p = g.player;
    if (p.vehicle) { const v = p.vehicle; v.input.throttle = 0; v.input.brake = 1; g.vehicles.exit(p); }
    p.animState.handsUp = true;
    g.hud.showWasted('busted');
    g.audio?.play('wasted');
    g.timeScale = 0.6;
  }
  respawn(kind) {
    const g = this.game, p = g.player;
    const L = g.map.landmarks;
    const spot = kind === 'busted' ? L.police.respawn : L.hospital.respawn;
    g.hud.fade(0.8, () => {
      g.timeScale = 1;
      g.post.composite.uniforms.uDesat.value = 0;
      if (p.vehicle) { const v = p.vehicle; v.takeOut(p); }
      p.dead = false; p.ragdolling = false;
      p.health = p.maxHealth; p.armor = 0;
      const lost = kind === 'busted' ? 100 * Math.max(1, g.police.level) : 100;
      p.money = Math.max(0, p.money - lost);
      p.weapons = { fist: { ammo: Infinity, clip: Infinity } };
      p.equip('fist');
      p.anim.action = null;
      p.anim.beginBlend(0.01);
      p.animState.handsUp = false;
      p.setPosition(spot.x, undefined, spot.z);
      p.setYaw(spot.rot);
      p.root.visible = true;
      g.police.reset();
      g.env.setTime(g.env.hours + 3);
      g.rig.yaw = spot.rot + Math.PI;
      this.state = 'playing';
      g.hud.help(kind === 'busted' ? `The cops took your weapons and $${lost}.` : `The hospital bill came to $${lost}. Your weapons were... misplaced.`, 5);
      g.missions?.refreshContacts();
    });
  }

  // ------------------------------------------------------------------ per-frame
  update(dt) {
    const g = this.game, p = g.player;
    if (this.state === 'menu') { this._flyover(dt); return; }
    g.stats.playTime += dt;
    if (this.state === 'dead' || this.state === 'busted') {
      this.deathTimer += dt / Math.max(0.2, g.timeScale);
      g.post.composite.uniforms.uDesat.value = clamp(this.deathTimer / 1.5, 0, 0.9);
      if (this.state === 'busted') { p.moveTarget.set(0, 0); p.animState.handsUp = true; }
      if (this.deathTimer > 4.5) { const k = this.state; this.state = 'respawning'; this.respawn(k === 'busted' ? 'busted' : 'wasted'); }
      return;
    }
    // distance stats
    const pos = p.vehicle ? p.vehicle.pos : p.pos;
    const d = Math.hypot(pos.x - this.lastPos.x, pos.z - this.lastPos.z);
    if (d < 50) { if (p.vehicle) g.stats.driven += d; else g.stats.walked += d; }
    this.lastPos.copy(pos);
    // drift scoring & stunts
    const v = p.vehicle;
    if (v && p.seat === 0 && !v.isWrecked) {
      const spd = v.speedAbs;
      const velYaw = Math.atan2(v.vel.x, v.vel.z);
      const slip = Math.abs(wrapAngle(velYaw - v.yaw));
      const drifting = !v.airborne && spd > 9 && slip > 0.3 && slip < 2.2;
      if (drifting) { this.drift += spd * dt; this.driftTime = 0; }
      else if (this.drift > 0) {
        this.driftTime += dt;
        if (this.driftTime > 0.6) {
          if (this.drift > 25) {
            const cash = Math.floor(this.drift);
            p.money += cash;
            g.stats.bestDrift = Math.max(g.stats.bestDrift, this.drift);
            g.hud.bigMessage(`DRIFT ${Math.floor(this.drift)} M`, 'hint', 1.6, `+$${cash}`);
          }
          this.drift = 0;
        }
      }
      if (this.drift > 15) g.hud.setBar('DRIFT', clamp(this.drift / 200, 0, 1), '#ffd23f'); else if (!g.missions?.active) g.hud.setBar(null);
      if (v.airborne) { this.airTime += dt; if (!this.airStart) this.airStart = v.pos.clone(); }
      else if (this.airTime > 0) {
        if (this.airTime > 1.2 && this.airStart) {
          const dist = Math.hypot(v.pos.x - this.airStart.x, v.pos.z - this.airStart.z);
          const cash = Math.floor(dist * 5 + this.airTime * 40);
          p.money += cash;
          g.hud.bigMessage('INSANE STUNT BONUS', 'passed', 3, `Distance ${dist.toFixed(0)}m · Air ${this.airTime.toFixed(1)}s · $${cash}`);
          g.audio?.play('passed', 0.5);
        }
        this.airTime = 0; this.airStart = null;
      }
      // fire / smoke on damaged cars
    }
    for (const veh of g.vehicles.list) {
      if (veh.isWrecked && !veh.exploded) continue;
      if (veh.health < 400 && !veh.exploded && Math.random() < dt * (veh.health < 150 ? 20 : 8)) {
        const f = veh.fwd;
        const hp = veh.pos.clone().addScaledVector(f, veh.def.L * 0.35).setY(veh.pos.y + veh.def.H * 0.7);
        g.effects.engineSmoke(hp, veh.health < 150 ? 1 : 0);
        if (veh.onFire && Math.random() < 0.8) g.effects.fire(hp, 0.9);
      }
      if (veh.exploded && veh.wreckTime < 20 && Math.random() < dt * 12) g.effects.fire(veh.pos.clone().setY(veh.pos.y + 0.8), 1.3);
      // tire smoke & skid marks
      if (veh.skid && dist2v(veh.pos, g.camera.position) < 150 * 150) {
        const s = Math.sin(veh.yaw), c = Math.cos(veh.yaw);
        const d2 = veh.def;
        for (const w of veh.model.wheels) {
          if (w.front && !(veh.input.brake > 0.5 && veh.speed > 12)) continue;
          const wx = veh.pos.x + w.x * c + w.z * s, wz = veh.pos.z - w.x * s + w.z * c;
          const wy = veh.pos.y;
          g.effects.skids.add(`${veh.vid}:${w.x}:${w.z}`, wx, wy, wz, 0.24, clamp(veh.slipRear / 6 + 0.4, 0, 1));
          if (Math.random() < 0.5) g.effects.tireSmoke(new THREE.Vector3(wx, wy, wz), clamp(veh.slipRear / 10 + veh.wheelspin, 0.4, 1.2));
        }
      } else if (veh._skidding) {
        for (const w of veh.model.wheels) g.effects.skids.break(`${veh.vid}:${w.x}:${w.z}`);
      }
      veh._skidding = veh.skid;
      // dust off-road
      if (!veh.skid && veh.speedAbs > 8 && veh._surface < 1 && Math.random() < dt * 10) g.effects.dust(veh.pos.clone().addScaledVector(veh.fwd, -veh.def.L / 2), 1);
    }
    // player wet/drown
    if (p.swimming && !p.vehicle) { p.swimTime = (p.swimTime || 0) + dt; } else p.swimTime = 0;
    if (p.vehicle && p.vehicle.sunk && !g.vehicles.isBusy(p)) { const v2 = p.vehicle; v2.takeOut(p, v2.pos.clone().add(new THREE.Vector3(0, 0, 0))); p.pos.y = v2.pos.y + 1; }
    if (p.health <= 0 && !p.dead) p.die();
  }

  _flyover(dt) {
    const g = this.game;
    this.flyT += dt;
    const t = this.flyT * 0.03;
    const L = g.map.landmarks;
    const cx = 120, cz = -120;
    const r = 520;
    const pos = new THREE.Vector3(cx + Math.cos(t) * r, 150 + Math.sin(t * 0.7) * 40, cz + Math.sin(t) * r);
    const look = new THREE.Vector3(cx + Math.cos(t + 1.3) * 120, 30, cz + Math.sin(t + 1.3) * 120);
    g.rig.setCinematic(pos, look, 55);
    if (this.flyT < 0.1) { g.camera.position.copy(pos); g.camera.lookAt(look); }
    g.player.pos.set(pos.x, g.map.groundHeight(pos.x, pos.z), pos.z);
    g.player.root.visible = false;
  }
}

function dist2v(a, b) { return (a.x - b.x) ** 2 + (a.z - b.z) ** 2; }
