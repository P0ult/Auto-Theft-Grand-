// The player character (Andre "Dre" Castillo): input-driven controller on foot and in vehicles.
import * as THREE from 'three';
import { Character } from '../entities/character.js';
import { randomAppearance } from '../entities/humanoid.js';
import { WEAPONS, WEAPON_ORDER } from './weapondefs.js';
import { clamp, damp, dampAngle, wrapAngle } from '../core/utils.js';
import { std } from '../render/materials.js';

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
    this.onHardLanding = (v) => { if (!this.game.cheatsOn?.superJump) this.takeDamage((v - 13) * 6, { type: 'fall' }); };
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
    if (this.chute) speed = 9;
    if (this.aiming) speed = 2.6;
    if (this.crouching) speed = 1.8;
    if (this.swimming) speed = this.sprinting ? 3.8 : 2.4;
    if (this.game.cheatsOn?.superRun && !this.aiming && !this.swimming) speed *= 2;
    if (this.anim.busy && this.anim.action && ['jab', 'cross', 'kick', 'stab', 'swing', 'getup'].includes(this.anim.action.name)) speed *= 0.25;
    this.moveTarget.set(dx * speed, dz * speed);
    // under canopy: glide forward when hands-off, Space opens the chute in free fall
    if (this.chute && len < 0.1) this.moveTarget.set(Math.sin(this.yaw) * 6, Math.cos(this.yaw) * 6);
    if (this.skydive && !this.chute && input.hit('jump')) this.openChute();

    // facing
    if (this.aiming || (this.anim.busy && def.type === 'melee')) {
      this.yaw = dampAngle(this.yaw, camYaw, this.aiming ? 25 : 12, dt);
      // pitch from camera
      const dir = rig.lookDir(new THREE.Vector3());
      this.aimPitch = Math.asin(clamp(dir.y, -1, 1));
      this.aimDir = (this.aimDir || new THREE.Vector3()).copy(dir);
    } else if (len > 0.1) {
      this.yaw = dampAngle(this.yaw, Math.atan2(dx, dz), this.sprinting ? 8 : 11, dt);
    }

    if (input.hit('jump') && !this.aiming) this.jump(this.game.cheatsOn?.superJump ? 17 : undefined);

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

  // ------------------------------------------------------------------ bailing out & parachute
  bailOut(alt) {
    this.skydive = { t: 0, auto: alt > 28 };
    if (alt > 28) this.game.hud?.help('Press <b>Space</b> to open your parachute', 3);
  }
  openChute() {
    if (this.chute) return;
    if (!this._chuteMesh) this._chuteMesh = buildChute();
    this.root.add(this._chuteMesh);
    this.chute = true;
    this.skydive = null;
    this.airAccel = 5;
    this.game.audio?.play('swoosh', 1);
  }
  closeChute() {
    if (!this.chute) return;
    this.root.remove(this._chuteMesh);
    this.chute = false;
    this.airAccel = 0;
  }

  update(dt) {
    if (this.skydive) {
      const s = this.skydive;
      s.t += dt;
      if (this.grounded || this.dead || this.swimming || this.vehicle || this.ragdolling) this.skydive = null;
      else {
        // free fall: air drag bleeds off the aircraft's speed, terminal velocity ~55 m/s
        this.vel.x *= Math.exp(-dt * 0.9); this.vel.z *= Math.exp(-dt * 0.9);
        this.vel.y = Math.max(this.vel.y, -55);
        if (s.auto && s.t > 1.6) this.openChute();
      }
    }
    if (this.chute) {
      if (this.grounded || this.swimming || this.dead || this.vehicle || this.ragdolling) this.closeChute();
      else if (this.vel.y < -4.5) this.vel.y = damp(this.vel.y, -4.5, 5, dt);
    }
    super.update(dt);
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
    if (!this.game.freeroam?.active) w.clip--;
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
    if (this.game.freeroam?.active) { /* unlimited */ } else if (w.clip > 0) w.clip--; else w.ammo--;
    this.fireCooldown = 1.0;
    this.yaw = rig.forwardYaw;
    const a = this.anim.play('throw');
    const dir = rig.lookDir(new THREE.Vector3());
    a.onHit = () => this.game.combat.throwGrenade(this, dir);
    if (w.clip + w.ammo <= 0) setTimeout(() => { delete this.weapons.grenade; this.switchTo('fist'); }, 800);
  }

}

// Striped ram-air canopy with rigging lines, hung from the shoulders (built once, reused)
function buildChute() {
  const g = new THREE.Group();
  const geo = new THREE.SphereGeometry(3.2, 24, 6, 0, Math.PI * 2, 0, 0.8);
  const pos = geo.attributes.position;
  const col = new Float32Array(pos.count * 3);
  for (let i = 0; i < pos.count; i++) {
    const a = Math.atan2(pos.getZ(i), pos.getX(i));
    const k = Math.floor((a + Math.PI) / (Math.PI * 2) * 12) % 2;
    const c = k ? [0.86, 0.12, 0.1] : [0.95, 0.94, 0.9];
    col.set(c, i * 3);
  }
  geo.setAttribute('color', new THREE.BufferAttribute(col, 3));
  const canopy = new THREE.Mesh(geo, std({ color: 0xffffff, vertexColors: true, roughness: 0.85, side: THREE.DoubleSide }, { key: 'chute' }));
  canopy.scale.set(1.3, 0.5, 0.85);
  canopy.position.y = 3.9;
  canopy.castShadow = true;
  g.add(canopy);
  const rimY = 3.9 + Math.cos(0.8) * 3.2 * 0.5, rimR = Math.sin(0.8) * 3.2;
  const lineMat = std({ color: 0x222222, roughness: 0.9 }, { key: 'chuteline' });
  for (let k = 0; k < 8; k++) {
    const a = k / 8 * Math.PI * 2;
    const top = new THREE.Vector3(Math.cos(a) * rimR * 1.3, rimY, Math.sin(a) * rimR * 0.85);
    const bot = new THREE.Vector3(Math.cos(a) > 0 ? 0.2 : -0.2, 1.45, 0);
    const d = new THREE.Vector3().subVectors(top, bot);
    const line = new THREE.Mesh(new THREE.CylinderGeometry(0.012, 0.012, d.length(), 3), lineMat);
    line.position.copy(bot).addScaledVector(d, 0.5);
    line.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), d.normalize());
    g.add(line);
  }
  return g;
}
