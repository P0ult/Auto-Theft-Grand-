// Property: buy safehouses and businesses around the state. A "For Sale" sign (green house on the map)
// marks each one; walk up to it to see the price. Safehouses give you a place to save and patch up. Businesses
// earn money every in-game hour, piling up in the till (up to five days' worth) until you come and collect.
import { NCITY, TOWNS } from '../world/worldgen.js';

const el = (tag, cls, parent, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html != null) e.innerHTML = html; if (parent) parent.appendChild(e); return e; };
const money = (n) => '$' + Math.round(n).toLocaleString();

function propertyDefs(game) {
  const L = game.map.landmarks;
  const at = (p, dx = 0, dz = 0) => (p ? { x: p.x + dx, z: p.z + dz } : null);
  return [
    { id: 'beachhouse', name: 'Santa Luz Beach House', kind: 'safehouse', price: 45000, where: at(L.beach, 20, -40), blurb: 'Two bedrooms, a deck and the sound of the waves. Save and heal here.' },
    { id: 'carwash', name: 'Corona Car Wash', kind: 'business', price: 30000, income: 700, where: at(L.spray2, 22, 18), blurb: 'Suds, wax and no questions about where the car came from.' },
    { id: 'hotelaurelio', name: 'Hotel Aurelio', kind: 'business', price: 120000, income: 2600, where: at(L.aurelio, -40, 55), blurb: 'Sixty rooms on the Plaza. Tourists, conventions, the odd discreet guest.' },
    { id: 'harborloft', name: 'Harborside Loft', kind: 'safehouse', price: 80000, where: at(L.aurHarbor, -60, 40), blurb: 'Exposed brick, a view of the bay and a lock the Saints can\'t pick.' },
    { id: 'clamshack', name: 'The Clam Shack, Gull Bay', kind: 'business', price: 25000, income: 450, where: { x: TOWNS.gull.x + 40, z: TOWNS.gull.z + 16 }, blurb: 'Fried clams and cold beer. Surprisingly profitable.' },
    { id: 'lodge', name: 'Timberline Lodge', kind: 'both', price: 55000, income: 600, where: { x: TOWNS.timber.x + 34, z: TOWNS.timber.z - 22 }, blurb: 'A log lodge up in the pines: rooms to let, and a bed of your own.' },
    { id: 'arenabox', name: 'Aurelio Arena Skybox', kind: 'business', price: 200000, income: 4200, where: at(L.aurArena, 45, -20), blurb: 'A cut of every ticket, every beer and every bet on fight night.' },
    { id: 'desertmotel', name: 'Desert Rose Motel', kind: 'both', price: 38000, income: 520, where: { x: TOWNS.dry.x + 36, z: TOWNS.dry.z + 24 }, blurb: 'Twelve rooms, a pool that sometimes has water in it.' },
  ].filter((d) => d.where && Number.isFinite(d.where.x));
}

export class Properties {
  constructor(game) {
    this.game = game;
    this.owned = new Map(); // id -> { till }
    this.list = [];
    this.lastHours = null;
    for (const d of propertyDefs(game)) {
      const sp = game.freeroam?.roadSpot(d.where.x, d.where.z, true);
      const x = sp ? sp.x : d.where.x, z = sp ? sp.z : d.where.z;
      this.list.push({ ...d, x, z, marker: null });
    }
    this._markers();
  }

  _markers() {
    const game = this.game;
    for (const p of this.list) {
      if (p.marker) game.pickups.removeMarker(p.marker);
      const own = this.owned.has(p.id);
      p.marker = game.pickups.addMarker(p.x, p.z, { color: own ? 0x4aa3ff : 0x6cff6c, radius: 1.2, icon: 'house2', label: own ? p.name : `${p.name} — for sale`, footOnly: true, arrow: !own, onEnter: () => this.open(p) });
      if (p.marker.blip) { p.marker.blip.color = own ? 0x4aa3ff : 0x6cff6c; p.marker.blip.small = true; }
    }
  }

  open(p) {
    const game = this.game, pl = game.player, hud = game.hud;
    if (pl.vehicle || game.missions?.active) return;
    game.paused = true; game.input.exitLock(); hud.menuOpen = 'shop';
    const o = hud.overlay;
    const render = () => {
      o.innerHTML = ''; o.className = 'hud-overlay show';
      const panel = el('div', 'menu-panel shop', o);
      const own = this.owned.get(p.id);
      const kind = p.kind === 'safehouse' ? 'Safehouse' : p.kind === 'business' ? 'Business' : 'Safehouse & business';
      el('h2', '', panel, `${p.name.toUpperCase()} <small>${kind}${p.income ? ` · earns ${money(p.income)} a day` : ''}</small>`);
      el('div', 'shop-money', panel, `${p.blurb}<br>Cash: <b>${money(pl.money)}</b>`);
      const list = el('div', 'shop-list', panel);
      if (!own) {
        const r = el('div', 'shop-item', list);
        el('div', 'shop-name', r, `Buy the deeds<small>${p.kind !== 'business' ? 'Save, heal and restock here. ' : ''}${p.income ? `Income piles up here — up to five days' worth.` : ''}</small>`);
        const b = el('button', 'btn primary', r, `Buy ${money(p.price)}`);
        b.onclick = () => {
          if (pl.money < p.price) { game.audio?.play('locked'); b.textContent = 'Not enough cash'; return; }
          pl.money -= p.price;
          this.owned.set(p.id, { till: 0 });
          game.audio?.play('cash');
          hud.bigMessage('PROPERTY BOUGHT', 'passed', 3, p.name);
          game.stats.properties = this.owned.size;
          game.events.emit('propertyBought', p.id);
          this._markers();
          game.save?.save(true);
          render();
        };
      } else {
        if (p.income) {
          const r = el('div', 'shop-item', list);
          el('div', 'shop-name', r, `The till<small>${money(own.till)} waiting · ${money(p.income)} a day</small>`);
          const b = el('button', 'btn primary', r, own.till >= 1 ? `Collect ${money(own.till)}` : 'Empty');
          if (own.till < 1) b.disabled = true;
          b.onclick = () => { pl.money += Math.floor(own.till); hud.moneyFlash?.(Math.floor(own.till)); own.till = 0; game.audio?.play('cash'); render(); };
        }
        if (p.kind !== 'business') {
          const r = el('div', 'shop-item', list);
          el('div', 'shop-name', r, 'Rest here<small>Save your game, patch yourself up and sleep six hours.</small>');
          const b = el('button', 'btn', r, 'Save & rest');
          b.onclick = () => { pl.health = pl.maxHealth; pl.armor = Math.max(pl.armor, 50); game.env.setTime(game.env.hours + 6); const ok = game.save?.save(); hud.closeOverlay(); hud.help(ok === false ? 'Rested. (Free roam doesn\'t save.)' : 'Game saved. Health restored.', 3); };
        }
      }
      const close = el('button', 'btn', panel, 'Leave (Esc)');
      close.onclick = () => hud.closeOverlay();
    };
    render();
  }

  update() {
    const env = this.game.env;
    const h = env.hours;
    if (this.lastHours == null) { this.lastHours = h; return; }
    let dh = h - this.lastHours;
    if (dh < 0) dh += 24;
    this.lastHours = h;
    if (dh <= 0 || dh > 12 || !this.owned.size) return;
    for (const p of this.list) {
      const own = this.owned.get(p.id);
      if (!own || !p.income) continue;
      own.till = Math.min(p.income * 5, own.till + p.income * dh / 24);
    }
  }

  serialize() { return [...this.owned].map(([id, o]) => ({ id, till: Math.floor(o.till) })); }
  load(data) {
    this.owned = new Map();
    for (const o of data || []) if (this.list.some((p) => p.id === o.id)) this.owned.set(o.id, { till: o.till || 0 });
    this._markers();
  }
}
