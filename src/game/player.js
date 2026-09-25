// The player character (Andre "Dre" Castillo): input-driven controller on foot and in vehicles.
import * as THREE from 'three';
import { Character } from '../entities/character.js';
import { randomAppearance } from '../entities/humanoid.js';
import { WEAPONS, WEAPON_ORDER } from './weapondefs.js';
import { clamp, dampAngle, wrapAngle } from '../core/utils.js';

export const PLAYER_LOOK = {
  female: false, skin: 0x8a5536, hair: 0x111111, hairStyle: 'buzz', shirt: 0xf2f2f2, shirtType: 'tank', pants: 0x2b3a55, shorts: false,
  shoes: 0xeeeeee, hat: null, build: 1.08, height: 1.0, glasses: false, beard: false, jacketColor: 0x1b5e20, bandana: null, uniform: null,
};

export class Player extends Character {
  constructor(game) {
    super(game, randomAppearance(undefined, PLAYER_LOOK), { team: 'player', health: 100 });
    this.isPlayer = true;
    this.money = 250;
    this.sprinting = false;
    this.stamina = 1;
    this.aimHold = 0;
    this.fireCooldown = 0;
    this.comboIndex = 0;
    this.comboTimer = 0;
    this.reloading = 0;
    this.enterRequest = false;
    this.maxArmor = 100;
    this.onHardLanding = (v) => this.takeDamage((v - 13) * 6, { type: 'fall' });
  }

  control(dt, input, rig) {
    if (this.dead || this.ragdolling) { this.moveTarget.set(0, 0); this.aiming = false; return; }
    if (this.vehicle) return;
    const g = this.game;
    const mx = input.moveX(), my = input.moveY();
    const camYaw = rig.forwardYaw;
    // movement direction relative to camera
    // forward = (sin, cos), right = (-cos, sin) for a heading angle
    let dx = Math.sin(camYaw) * my - Math.cos(camYaw) * mx;
    let dz = Math.cos(camYaw) * my + Math.sin(camYaw) * mx;
    const len = Math.hypot(dx, dz);
    if (len > 1) { dx /= len; dz /= len; }
    const def = this.weaponDef;
    const wantAim = input.aimDown() && def.type !== 'melee' && def.type !== 'thrown';
    this.aimHold = Math.max(0, this.aimHold - dt);
    this.aiming = (wantAim || this.aimHold > 0) && !this.swimming;
    this.crouching = input.down('crouch') && !this.swimming;
    const wantSprint = input.down('sprint') && len > 0.1 && !this.aiming && !this.crouching;
    if (wantSprint && this.stamina > 0.05) { this.sprinting = true; this.stamina = Math.max(0, this.stamina - dt * 0.08); }
    else { this.sprinting = false; this.stamina = Math.min(1, this.stamina + dt * 0.15); }
    let speed = this.sprinting ? 7.2 : 4.3;
    if (this.aiming) speed = 2.6;
    if (this.crouching) speed = 1.8;
    if (this.swimming) speed = this.sprinting ? 3.8 : 2.4;
    if (this.anim.busy && this.anim.action && ['jab', 'cross', 'kick', 'stab', 'swing', 'getup'].includes(this.anim.action.name)) speed *= 0.25;
    this.moveTarget.set(dx * speed, dz * speed);

    // facing
    if (this.aiming || (this.anim.busy && def.type === 'melee')) {
      this.yaw = dampAngle(this.yaw, camYaw, this.aiming ? 25 : 12, dt);
      // pitch from camera
      const dir = rig.lookDir(new THREE.Vector3());
      this.aimPitch = Math.asin(clamp(dir.y, -1, 1));
    } else if (len > 0.1) {
      this.yaw = dampAngle(this.yaw, Math.atan2(dx, dz), this.sprinting ? 8 : 11, dt);
    }

    if (input.hit('jump') && !this.aiming) this.jump();

    // weapon switching
    if (input.hit('nextWeapon') || input.mouse.wheel > 0) this.cycleWeapon(1);
    if (input.hit('prevWeapon') || input.mouse.wheel < 0) this.cycleWeapon(-1);
    for (let k = 0; k <= 8; k++) if (input.keyHit('Digit' + (k + 1))) { const id = WEAPON_ORDER[k]; if (this.weapons[id]) this.switchTo(id); }

    // attacks
    this.fireCooldown -= dt;
    this.comboTimer -= dt;
    if (this.reloading > 0) {
      this.reloading -= dt;
      if (this.reloading <= 0) this.finishReload();
    }
    if (input.hit('reload')) this.startReload();
    if (def.type === 'melee') {
      if (input.attackPressed() && !this.swimming) this.melee();
    } else if (def.type === 'thrown') {
      if (input.attackPressed() && this.fireCooldown <= 0) this.throwGrenade(rig);
    } else if (!this.swimming) {
      const wantFire = def.auto ? input.mouse.left : input.mouse.leftPressed;
      if (wantFire) {
        this.aimHold = 0.6;
        if (!this.aiming) this.yaw = camYaw;
        this.aiming = true;
        if (this.fireCooldown <= 0 && this.reloading <= 0) this.fire(rig);
      }
    }
  }

  cycleWeapon(dir) {
    const owned = WEAPON_ORDER.filter((id) => this.weapons[id] && (WEAPONS[id].type === 'melee' || this.weapons[id].clip + this.weapons[id].ammo > 0));
    if (!owned.length) return;
    let i = owned.indexOf(this.weapon);
    i = (i + dir + owned.length) % owned.length;
    this.switchTo(owned[i]);
  }
  switchTo(id) {
    if (id === this.weapon) return;
    this.reloading = 0;
    this.equip(id);
    this.game.audio?.play('switch');
  }

  startReload() {
    const def = this.weaponDef;
    const w = this.weapons[this.weapon];
    if (!def.clip || def.type === 'melee' || this.reloading > 0) return;
    if (w.clip >= def.clip || w.ammo <= 0) return;
    this.reloading = def.id === 'shotgun' ? 1.4 : def.id === 'rpg' ? 1.6 : 1.1;
    this.anim.play('reload', 1.1 / this.reloading);
    this.game.audio?.play('reload');
  }
  finishReload() {
    const def = this.weaponDef;
    const w = this.weapons[this.weapon];
    const need = def.clip - w.clip;
    const take = Math.min(need, w.ammo);
    w.clip += take; w.ammo -= take;
  }

  melee() {
    if (this.anim.busy) {
      // queue combo if near end
      if (this.anim.action.t / this.anim.action.dur < 0.6) return;
    }
    const w = this.weapon;
    let act;
    if (w === 'knife') act = 'stab';
    else if (w === 'bat') act = 'swing';
    else {
      if (this.comboTimer <= 0) this.comboIndex = 0;
      act = ['jab', 'cross', 'kick'][this.comboIndex % 3];
      this.comboIndex++;
      this.comboTimer = 0.9;
    }
    const a = this.anim.play(act);
    a.onHit = () => this.game.combat.meleeHit(this, act);
    this.game.audio?.play('swoosh');
  }

  fire(rig) {
    const def = this.weaponDef;
    const w = this.weapons[this.weapon];
    if (w.clip <= 0) {
      if (w.ammo > 0) this.startReload();
      else { this.game.audio?.play('dryfire'); this.fireCooldown = 0.3; }
      return;
    }
    w.clip--;
    this.fireCooldown = def.rate;
    this.anim.recoil = def.recoil;
    rig.addShake(def.shake || 0.2);
    // aim ray from camera through screen center
    const origin = rig.cam.position.clone();
    const dir = rig.lookDir(new THREE.Vector3());
    this.game.combat.fireWeapon(this, def, origin, dir);
    if (w.clip <= 0 && w.ammo > 0) setTimeout(() => this.startReload(), 250);
  }

  throwGrenade(rig) {
    const w = this.weapons.grenade;
    if (!w || w.clip + w.ammo <= 0) return;
    if (w.clip > 0) w.clip--; else w.ammo--;
    this.fireCooldown = 1.0;
    this.yaw = rig.forwardYaw;
    const a = this.anim.play('throw');
    const dir = rig.lookDir(new THREE.Vector3());
    a.onHit = () => this.game.combat.throwGrenade(this, dir);
    if (w.clip + w.ammo <= 0) setTimeout(() => { delete this.weapons.grenade; this.switchTo('fist'); }, 800);
  }

}
