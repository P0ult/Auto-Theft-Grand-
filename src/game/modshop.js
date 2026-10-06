// Customs garages: drive in and upgrade whatever you're driving. Engine stages (more power and top
// speed), armour (more health, bullets do less), nitrous (Shift for a three-second shove, three bottles),
// a respray in the colour you pick, and the old Spray Shack service (fixed up, cops lose you). Upgrades
// live on the vehicle itself; each car gets its own copy of its handling numbers.
import * as THREE from 'three';
import { NCITY } from '../world/worldgen.js';
import { clamp, rand } from '../core/utils.js';

const ENGINE = [{ force: 1, top: 1 }, { force: 1.18, top: 1.06, price: 2500 }, { force: 1.36, top: 1.12, price: 6000 }, { force: 1.6, top: 1.2, price: 12000 }];
const ARMOR = [{ hp: 1, bullet: 1 }, { hp: 1.5, bullet: 0.75, price: 3000 }, { hp: 2, bullet: 0.55, price: 7000 }, { hp: 2.8, bullet: 0.35, price: 15000 }];
const NITRO_PRICE = 4000, PAINT_PRICE = 400, RESPRAY_PRICE = 100;
const PAINTS = [0xc1121f, 0xf77f00, 0xfcbf49, 0x2a9d8f, 0x1d3557, 0x3a86ff, 0x8338ec, 0xff006e, 0x111111, 0xf1faee, 0x6c757d, 0x3a5a40];
const el = (tag, cls, parent, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html != null) e.innerHTML = html; if (parent) parent.appendChild(e); return e; };
const money = (n) => '$' + Math.round(n).toLocaleString();

export class ModShop {
  constructor(game) {
    this.game = game;
    this.garages = [];
    const L = game.map.landmarks;
    const add = (name, x, z) => {
      if (!Number.isFinite(x)) return;
      const m = game.pickups.addMarker(x, z, { color: 0xff9f1c, radius: 3.6, height: 2.5, icon: 'wrench', label: name, vehicleOnly: true, arrow: false, onEnter: () => this.open(name) });
      this.garages.push({ name, x, z, marker: m });
    };
    // the two Spray Shacks became Customs garages, and San Aurelio has its own on Mission Street
    if (L.spray) add('Los Soles Customs', L.spray.x, L.spray.z);
    if (L.spray2) add('Corona Customs', L.spray2.x, L.spray2.z);
    const sp = game.freeroam?.roadSpot(NCITY.x + 60, NCITY.z + 250, false);
    if (sp) { add('Aurelio Customs', sp.x, sp.z); L.aurCustoms = { x: sp.x, z: sp.z, name: 'Aurelio Customs' }; }
    // (the old instant spray is now a menu option)
    for (const s of game.pickups.services || []) if (s.blip?.icon === 'spray') { game.pickups.removeMarker(s); }
    game.pickups.services = (game.pickups.services || []).filter((s) => !s.removed);
    this.boostT = 0;
    this.flameT = 0;
  }

  // ---------------------------------------------------------------- per-vehicle tuning
  static base(v) { return v.baseDef || v.def; }
  static apply(v) {
    const mods = v.mods || {};
    const base = ModShop.base(v);
    if (!v.baseDef) { v.baseDef = v.def; v.def = Object.create(v.def); }
    const e = ENGINE[mods.engine || 0], a = ARMOR[mods.armor || 0];
    v.def.force = base.force * e.force;
    v.def.top = base.top * e.top;
    v.def.bulletMul = (base.bulletMul ?? 1) * a.bullet;
    const frac = v.maxHealth > 0 ? v.health / v.maxHealth : 1;
    v.maxHealth = (base.health ?? 1000) * a.hp;
    v.health = Math.max(v.health, v.maxHealth * frac);
  }
  static canMod(v) { return v && !v.isBoat && !v.def.aircraft && !v.def.bike && !v.def.pedal && !v.def.board && !v.def.train && !v.def.tank; }

  open(name) {
    const game = this.game, v = game.player.vehicle;
    if (!v || v.isWrecked || v.driver !== game.player) return;
    if (!ModShop.canMod(v)) { game.hud?.help('Customs only works on cars and trucks.', 3); return; }
    if (game.missions?.active?.noSpray && game.police.level > 0) { game.hud?.help('Not now — you\'re on a job.', 3); return; }
    v.input.throttle = 0; v.input.brake = 1; v.vel.multiplyScalar(0.2);
    const hud = game.hud;
    game.paused = true;
    game.input.exitLock();
    hud.menuOpen = 'shop';
    const o = hud.overlay;
    const p = game.player;
    const render = () => {
      v.mods = v.mods || {};
      o.innerHTML = '';
      o.className = 'hud-overlay show';
      const panel = el('div', 'menu-panel shop', o);
      el('h2', '', panel, `${name.toUpperCase()} <small>${v.def.name} — make it yours</small>`);
      el('div', 'shop-money', panel, `Cash: <b>${money(p.money)}</b>`);
      const list = el('div', 'shop-list', panel);
      const row = (title, sub, label, price, ok, fn) => {
        const r = el('div', 'shop-item', list);
        el('div', 'shop-name', r, `${title}<small>${sub}</small>`);
        const b = el('button', 'btn', r, label);
        if (!ok) { b.disabled = true; return; }
        b.onclick = () => {
          if (p.money < price) { game.audio?.play('locked'); b.textContent = 'Not enough cash'; return; }
          p.money -= price;
          fn();
          game.audio?.play('cash');
          game.events.emit('purchase', 'mod');
          render();
        };
      };
      const eng = v.mods.engine || 0, arm = v.mods.armor || 0;
      const nextE = ENGINE[eng + 1], nextA = ARMOR[arm + 1];
      row(`Engine — Stage ${eng}`, nextE ? `Stage ${eng + 1}: +${Math.round((nextE.force - 1) * 100)}% power, +${Math.round((nextE.top - 1) * 100)}% top speed` : 'Fully tuned', nextE ? `Stage ${eng + 1} ${money(nextE.price)}` : 'Maxed', nextE?.price || 0, !!nextE, () => { v.mods.engine = eng + 1; ModShop.apply(v); });
      row(`Armour — Level ${arm}`, nextA ? `Level ${arm + 1}: ${nextA.hp}× body strength, bullets do ${Math.round(nextA.bullet * 100)}%` : 'Built like a tank', nextA ? `Level ${arm + 1} ${money(nextA.price)}` : 'Maxed', nextA?.price || 0, !!nextA, () => { v.mods.armor = arm + 1; ModShop.apply(v); v.health = v.maxHealth; });
      const nb = v.mods.nitro || 0;
      row('Nitrous', nb ? `${nb} of 3 bottles left — hold <b>Shift</b> (A on a pad) to fire one` : 'Three bottles, three seconds each. <b>Shift</b> to boost.', nb >= 3 ? 'Full' : nb ? `Refill ${money(NITRO_PRICE / 2)}` : `Fit ${money(NITRO_PRICE)}`, nb ? NITRO_PRICE / 2 : NITRO_PRICE, nb < 3, () => { v.mods.nitro = 3; });
      // paint swatches
      const pr = el('div', 'shop-item', list);
      el('div', 'shop-name', pr, `Paint<small>${money(PAINT_PRICE)} a coat</small>`);
      const sw = el('div', 'swatches', pr);
      for (const c of PAINTS) {
        const b = el('button', 'swatch', sw);
        b.style.background = '#' + c.toString(16).padStart(6, '0');
        b.onclick = () => {
          if (p.money < PAINT_PRICE) { game.audio?.play('locked'); return; }
          p.money -= PAINT_PRICE;
          v.model.bodyMat?.color.setHex(c);
          v.mods.paint = c;
          game.audio?.play('cash');
          render();
        };
      }
      row('Respray &amp; repair', game.police.level > 0 ? 'Fix it up and the cops won\'t recognise you' : 'Fix it up, good as new', `Respray ${money(RESPRAY_PRICE)}`, RESPRAY_PRICE, true, () => {
        v.model.bodyMat?.color.set(new THREE.Color().setHSL(Math.random(), rand(0.4, 0.8), rand(0.25, 0.55)));
        v.health = v.maxHealth; v.onFire = false; v.burnTime = 0; v.flat = false;
        v.undent?.();
        if (!game.missions?.active?.noSpray) game.police.clear();
        game.stats.sprays++;
      });
      const close = el('button', 'btn primary', panel, 'Drive out (Esc)');
      close.onclick = () => hud.closeOverlay();
    };
    render();
  }

  // ---------------------------------------------------------------- nitrous
  update(dt) {
    const game = this.game, p = game.player, v = p.vehicle;
    if (game.rig.boostFov && this.boostT <= 0) game.rig.boostFov = Math.max(0, game.rig.boostFov - dt * 20);
    if (!v || v.driver !== p || (!v.mods?.nitro && this.boostT <= 0)) { this.boostT = 0; return; }
    const input = game.input;
    if (this.boostT <= 0 && v.mods.nitro > 0 && input.hit('nitro') && !game.paused && v.speedAbs > 2) {
      v.mods.nitro--;
      this.boostT = 3;
      game.audio?.playAt('swoosh', v.pos, 1);
      game.hud?.help(`NITROUS! ${v.mods.nitro} bottle${v.mods.nitro === 1 ? '' : 's'} left`, 1.5);
    }
    if (this.boostT > 0) {
      this.boostT -= dt;
      const fx = Math.sin(v.yaw), fz = Math.cos(v.yaw);
      const top = v.def.top * 1.4;
      const along = v.vel.x * fx + v.vel.z * fz;
      if (along < top && !v.airborne) { v.vel.x += fx * 13 * dt; v.vel.z += fz * 13 * dt; }
      game.rig.boostFov = Math.min(10, (game.rig.boostFov || 0) + dt * 40);
      // blue flame out of the exhaust
      this.flameT -= dt;
      if (this.flameT <= 0) {
        this.flameT = 0.02;
        const back = -(v.def.L || 4.5) / 2 - 0.2;
        const pos = new THREE.Vector3(v.pos.x + fx * back, v.pos.y + 0.45, v.pos.z + fz * back);
        game.effects.addPool.spawn({ x: pos.x, y: pos.y, z: pos.z, vx: -fx * 6 + rand(-0.5, 0.5), vy: rand(0, 0.4), vz: -fz * 6 + rand(-0.5, 0.5), age: 0, life: 0.18, size0: 0.5, size1: 0.15, rot: 0, spin: 0, grav: 0, drag: 1, alpha: 1, fadeIn: 0.01, fadePow: 1, color: [0.6, 1.4, 4.5], color1: [1.5, 0.6, 3], floor: null });
      }
    }
  }
}

export const MOD_TABLES = { ENGINE, ARMOR };
