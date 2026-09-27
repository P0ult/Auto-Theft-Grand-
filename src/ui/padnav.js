// Menus with a controller: the D-pad / left stick moves a highlight between the buttons of whatever menu is
// on screen (the pause menu, shops, the title screen...), A presses it, B backs out, LB / RB switch tabs.
// Sliders and drop-downs are changed with left / right.
import { GP } from '../core/input.js';

const FOCUSABLE = 'button, select, input[type=range], input[type=checkbox], input[type=text], [data-nav]';

export class PadNav {
  constructor(game) {
    this.game = game;
    this.cur = null;
    this.repeatT = 0;
    this.lastDir = null;
  }

  // the menu that currently has the screen, if any
  _root() {
    const hud = this.game.hud;
    if (hud?.consoleOpen) return null;
    const ov = hud?.overlay;
    if (ov && ov.classList.contains('show')) return ov;
    const t = document.querySelector('.title-screen');
    return t && t.getBoundingClientRect().width > 0 ? t : null;
  }

  _items(root) {
    return [...root.querySelectorAll(FOCUSABLE)].filter((e) => {
      if (e.disabled) return false;
      const r = e.getBoundingClientRect();
      return r.width > 0 && r.height > 0 && getComputedStyle(e).visibility !== 'hidden';
    });
  }

  _dir() {
    const input = this.game.input, g = input.gp;
    let d = null;
    if (input.gpDown(GP.UP) || g.ly < -0.6) d = 'up';
    else if (input.gpDown(GP.DOWN) || g.ly > 0.6) d = 'down';
    else if (input.gpDown(GP.LEFT) || g.lx < -0.6) d = 'left';
    else if (input.gpDown(GP.RIGHT) || g.lx > 0.6) d = 'right';
    const dt = Math.min(0.1, input.frameDt || 1 / 60);
    if (!d) { this.lastDir = null; return null; }
    if (d !== this.lastDir) { this.lastDir = d; this.repeatT = 0.38; return d; }
    this.repeatT -= dt;
    if (this.repeatT <= 0) { this.repeatT = 0.11; return d; }
    return null;
  }

  _setCur(el) {
    if (this.cur === el) return;
    this.cur?.classList.remove('pad-focus');
    this.cur = el;
    if (!el) return;
    el.classList.add('pad-focus');
    try { el.focus({ preventScroll: true }); } catch { /* not focusable */ }
    el.scrollIntoView?.({ block: 'nearest', inline: 'nearest' });
  }

  // nearest item in a direction from the current one (screen space)
  _move(items, dir) {
    if (!this.cur || !items.includes(this.cur)) { this._setCur(items[0]); return; }
    const a = this.cur.getBoundingClientRect(), ax = a.left + a.width / 2, ay = a.top + a.height / 2;
    let best = null, bs = Infinity;
    for (const e of items) {
      if (e === this.cur) continue;
      const r = e.getBoundingClientRect(), x = r.left + r.width / 2, y = r.top + r.height / 2;
      const dx = x - ax, dy = y - ay;
      const main = dir === 'up' ? -dy : dir === 'down' ? dy : dir === 'left' ? -dx : dx;
      const side = dir === 'up' || dir === 'down' ? Math.abs(dx) : Math.abs(dy);
      if (main <= 2) continue;
      const s = main + side * 2.2;
      if (s < bs) { bs = s; best = e; }
    }
    if (best) { this._setCur(best); this.game.audio?.play('ui'); }
  }

  _adjust(el, dir) {
    const k = dir === 'left' ? -1 : 1;
    if (el.tagName === 'SELECT') {
      const n = el.options.length;
      if (!n) return false;
      el.selectedIndex = (el.selectedIndex + k + n) % n;
    } else if (el.type === 'range') {
      const step = parseFloat(el.step) || (parseFloat(el.max) - parseFloat(el.min)) / 20 || 1;
      el.value = String(Math.min(parseFloat(el.max), Math.max(parseFloat(el.min), parseFloat(el.value) + k * step)));
      el.dispatchEvent(new Event('input', { bubbles: true }));
    } else return false;
    el.dispatchEvent(new Event('change', { bubbles: true }));
    return true;
  }

  update() {
    const input = this.game.input;
    if (!input.gp.connected) { if (this.cur) this._setCur(null); return; }
    const root = this._root();
    if (!root) { if (this.cur) this._setCur(null); this.lastDir = null; return; }
    if (this.cur && !root.contains(this.cur)) this.cur = null;
    const items = this._items(root);
    if (!items.length) return;
    const dir = this._dir();
    if (dir) {
      if ((dir === 'left' || dir === 'right') && this.cur && this._adjust(this.cur, dir)) { /* changed a value */ }
      else this._move(items, dir);
    } else if ((!this.cur || !items.includes(this.cur)) && input.lastDevice === 'gamepad') {
      // something to press straight away: the focused button, else the first item
      this._setCur(items.includes(document.activeElement) ? document.activeElement : items[0]);
    }
    if (input.gpHit(GP.A) && this.cur) {
      const el = this.cur;
      if (el.type === 'checkbox') { el.checked = !el.checked; el.dispatchEvent(new Event('change', { bubbles: true })); }
      else el.click();
      this.game.audio?.play('ui');
    }
    // LB / RB: previous / next tab
    const tabs = [...root.querySelectorAll('.tabs button')];
    if (tabs.length && (input.gpHit(GP.LB) || input.gpHit(GP.RB))) {
      const i = Math.max(0, tabs.findIndex((t) => t.classList.contains('active')));
      const n = tabs[(i + (input.gpHit(GP.RB) ? 1 : tabs.length - 1)) % tabs.length];
      n.click();
      this.cur = null;
    }
    if (input.gpHit(GP.B)) this._back(root);
  }

  _back(root) {
    const hud = this.game.hud;
    if (root === hud?.overlay) { hud.closeOverlay(); this._setCur(null); return; }
    const back = [...root.querySelectorAll('button')].find((b) => /^(back|cancel|close|leave)\b/i.test(b.textContent.trim()) && b.offsetParent !== null);
    if (back) { back.click(); this.cur = null; }
  }
}
