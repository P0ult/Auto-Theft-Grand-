// GTA V's weapon wheel: hold Tab (or LB on a pad, on foot) and the game slows right down while a ring of every
// weapon you carry fans out; point at one with the mouse or the right stick and let go to draw it. A quick
// tap of LB still flicks back to the previous weapon. In a car the wheel offers what you can shoot from a seat.
import { WEAPONS, WEAPON_ORDER } from '../game/weapondefs.js';
import { drawWeaponIcon } from './hud.js';
import { GP } from '../core/input.js';

const SLOW = 0.18;     // game speed while the wheel is open
const HOLD = 0.22;     // seconds LB has to be held to open the wheel
const S = 460;         // canvas size (px)

export class WeaponWheel {
  constructor(game) {
    this.game = game;
    this.open = false;
    this.sel = -1;
    this.cx = 0; this.cy = 0; // virtual cursor
    this.padT = 0;
    this.list = [];
    this.el = document.createElement('div');
    this.el.className = 'weapon-wheel';
    this.canvas = document.createElement('canvas');
    this.canvas.width = S; this.canvas.height = S;
    this.el.appendChild(this.canvas);
    this.label = document.createElement('div');
    this.label.className = 'ww-label';
    this.el.appendChild(this.label);
    document.body.appendChild(this.el);
    this.ctx = this.canvas.getContext('2d');
    this.icons = {};
  }

  _owned() {
    const p = this.game.player;
    if (p.vehicle) return WEAPON_ORDER.filter((id) => p.carWeaponOk(id) && p.weapons[id].clip + p.weapons[id].ammo > 0);
    return WEAPON_ORDER.filter((id) => p.weapons[id] && (WEAPONS[id].type === 'melee' || p.weapons[id].clip + p.weapons[id].ammo > 0));
  }

  get allowed() {
    const g = this.game, p = g.player;
    return g.gameplay?.state === 'playing' && !g.cutscene && !g.hud?.menuOpen && !p.dead && !p.ragdolling && g.input.enabled && !g.phone?.open;
  }

  // runs before the camera reads the mouse, so the look stops while you're choosing
  preUpdate() {
    const g = this.game, input = g.input, p = g.player;
    const padLB = !p.vehicle && input.gpDown(GP.LB);
    if (padLB) this.padT += input.frameDt; else if (!this.open) {
      if (this.padT > 0 && this.padT < HOLD && this.allowed) p.cycleWeapon(-1); // (a tap)
      this.padT = 0;
    }
    const want = this.allowed && (input.key('Tab') || (padLB && this.padT >= HOLD));
    if (want && !this.open) this._show();
    else if (!want && this.open) this._hide(true);
    if (!this.open) return;
    // the cursor: mouse movement or the right stick
    this.cx += input.mouse.dx; this.cy += input.mouse.dy;
    const L = Math.hypot(this.cx, this.cy);
    if (L > 90) { this.cx *= 90 / L; this.cy *= 90 / L; }
    if (Math.hypot(input.gp.rx, input.gp.ry) > 0.5) { this.cx = input.gp.rx * 90; this.cy = input.gp.ry * 90; }
    input.mouse.dx = 0; input.mouse.dy = 0; input.gp.rx = 0; input.gp.ry = 0;
    if (Math.hypot(this.cx, this.cy) > 28 && this.list.length) {
      const n = this.list.length;
      const a = Math.atan2(this.cy, this.cx) + Math.PI / 2; // 0 at the top, clockwise
      this.sel = ((Math.round(a / (Math.PI * 2 / n)) % n) + n) % n;
    }
    this._draw();
  }

  _show() {
    const g = this.game, p = g.player;
    this.list = this._owned();
    if (!this.list.length) return;
    this.open = true;
    this.sel = Math.max(0, this.list.indexOf(p.weapon));
    this.cx = 0; this.cy = 0;
    g.slowmo.wheel = SLOW;
    this.el.classList.add('show');
    document.body.classList.add('wheel-open');
    g.audio?.play('ui', 0.8);
    this._draw();
  }

  _hide(apply) {
    const g = this.game, p = g.player;
    this.open = false;
    delete g.slowmo.wheel;
    this.el.classList.remove('show');
    document.body.classList.remove('wheel-open');
    this.padT = 0;
    const id = this.list[this.sel];
    if (apply && id && id !== p.weapon) p.switchTo(id);
  }

  _icon(id) {
    if (!this.icons[id]) {
      const c = document.createElement('canvas'); c.width = 96; c.height = 96;
      drawWeaponIcon(c.getContext('2d'), id, 96);
      this.icons[id] = c;
    }
    return this.icons[id];
  }

  _draw() {
    const ctx = this.ctx, g = this.game, p = g.player;
    const n = this.list.length, C = S / 2, R0 = 92, R1 = 214;
    ctx.clearRect(0, 0, S, S);
    const step = Math.PI * 2 / n;
    for (let i = 0; i < n; i++) {
      const mid = -Math.PI / 2 + i * step, a0 = mid - step / 2 + 0.012, a1 = mid + step / 2 - 0.012;
      const on = i === this.sel;
      ctx.beginPath();
      ctx.arc(C, C, R1, a0, a1);
      ctx.arc(C, C, R0, a1, a0, true);
      ctx.closePath();
      ctx.fillStyle = on ? 'rgba(245,245,245,0.92)' : 'rgba(8,10,14,0.72)';
      ctx.fill();
      ctx.strokeStyle = on ? '#fff' : 'rgba(255,255,255,0.18)'; ctx.lineWidth = on ? 3 : 1.5; ctx.stroke();
      // icon (dark on the highlighted slot)
      const r = (R0 + R1) / 2, x = C + Math.cos(mid) * r, y = C + Math.sin(mid) * r;
      const sz = Math.min(84, (R1 - R0) * 0.82);
      ctx.save();
      if (on) ctx.filter = 'invert(1)';
      ctx.drawImage(this._icon(this.list[i]), x - sz / 2, y - sz / 2, sz, sz);
      ctx.restore();
      if (this.list[i] === p.weapon) { ctx.fillStyle = on ? '#2a6' : '#7dff8a'; ctx.beginPath(); ctx.arc(C + Math.cos(mid) * (R1 - 14), C + Math.sin(mid) * (R1 - 14), 4, 0, Math.PI * 2); ctx.fill(); }
    }
    // centre: name and ammo of the highlighted weapon
    ctx.beginPath(); ctx.arc(C, C, R0 - 8, 0, Math.PI * 2); ctx.fillStyle = 'rgba(0,0,0,0.6)'; ctx.fill();
    const id = this.list[this.sel];
    if (id) {
      const d = WEAPONS[id], w = p.weapons[id];
      const ammo = d.type === 'melee' ? '' : g.freeroam?.active ? '∞' : d.type === 'thrown' ? `${(w.clip || 0) + (w.ammo || 0)}` : `${w.clip} / ${w.ammo}`;
      this.label.innerHTML = `<b>${d.name}</b>${ammo ? `<span>${ammo}</span>` : ''}`;
    }
  }
}
