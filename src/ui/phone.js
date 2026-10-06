// The phone (GTA V's iFruit): press I (or D-pad up on foot) and it slides up in the bottom right corner.
// Arrow keys / D-pad to move, Enter / A to pick, Backspace / B to go back. While it's out you can still walk.
//
//   Contacts   Downtown Cab Co. (a cab comes to you), Benny's (your last car delivered to the kerb),
//              Lester (the cops lose your file, for a price), Merryweather (three armed contractors follow
//              you), Pegasus (free roam: a helicopter dropped off nearby)
//   Cheats     GTA V-style codes: PAINKILLER, TURTLE, TOOLUP, LAWYERUP, FUGITIVE, SKYFALL, BUZZOFF, COMET,
//              OFFROAD, HOPTOIT, CATCHME, HIGHEX, POWERUP, SLOWMO, MAKEITRAIN
//   Snapmatic  photo mode (the HUD goes; press I or Backspace to come back)
//   Map, Weather
import { randomAppearance } from '../entities/humanoid.js';
import { VEHICLES } from '../entities/vehicledefs.js';
import { WEAPONS } from '../game/weapondefs.js';
import { GP } from '../core/input.js';
import { RNG } from '../core/utils.js';

const money = (n) => '$' + Math.round(n).toLocaleString('en-US');

const h = (tag, cls, parent, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html != null) e.innerHTML = html; if (parent) parent.appendChild(e); return e; };
const CHEAT_TIME = 300; // seconds a timed cheat lasts outside free roam

const APPS = [
  { id: 'contacts', name: 'Contacts', icon: '☎', color: '#2fa84f' },
  { id: 'cheats', name: 'Cheats', icon: '#', color: '#b33a3a' },
  { id: 'camera', name: 'Snapmatic', icon: '◉', color: '#e08a1e' },
  { id: 'map', name: 'Map', icon: '⌖', color: '#2c79d6' },
  { id: 'weather', name: 'Weather', icon: '☀', color: '#d9b82b' },
  { id: 'stats', name: 'Stats', icon: '▤', color: '#7a4cc2' },
];

export const CHEATS = [
  ['PAINKILLER', 'Invincibility'],
  ['TURTLE', 'Max health and armour'],
  ['TOOLUP', 'Every weapon, plenty of ammo'],
  ['LAWYERUP', 'Lose your wanted level'],
  ['FUGITIVE', 'Raise your wanted level'],
  ['SKYFALL', 'Skydive from 400 m'],
  ['BUZZOFF', 'Spawn a Warhawk gunship'],
  ['COMET', 'Spawn a Zenith supercar'],
  ['OFFROAD', 'Spawn a Trailblazer dirt bike'],
  ['HOPTOIT', 'Super jump'],
  ['CATCHME', 'Run twice as fast'],
  ['HIGHEX', 'Explosive bullets'],
  ['POWERUP', 'Refill the special ability'],
  ['SLOWMO', 'Slow motion on / off'],
  ['MAKEITRAIN', 'Change the weather'],
];

export class Phone {
  constructor(game) {
    this.game = game;
    this.open = false;
    this.photo = false;
    this.screen = 'home';
    this.sel = 0;
    this.items = [];
    this.timed = {};      // cheat -> game time it runs out (outside free roam)
    this.lastCar = null;  // the last road vehicle you drove: { type, color }
    this.pending = [];    // phone calls being acted on: { t, fn }
    this.mercs = [];
    this.delivered = null;
    this.el = h('div', 'phone', document.body);
    const frame = h('div', 'phone-frame', this.el);
    this.statusEl = h('div', 'phone-status', frame);
    this.titleEl = h('div', 'phone-title', frame);
    this.body = h('div', 'phone-body', frame);
    this.foot = h('div', 'phone-foot', frame);
    this.photoHint = h('div', 'photo-hint', document.body, '<b>Snapmatic</b> · HUD hidden for photos · press <b>I</b> or <b>Backspace</b> to put the camera away');
    // wasted or busted: the contractors leave and any call in progress is dropped
    const off = () => { this.reset(true); };
    game.events.on('playerDied', off);
    game.events.on('busted', off);
  }

  // the cheats running now (admin.js reads this outside free roam)
  activeCheats() {
    const t = this.game.time, out = {};
    let any = false;
    for (const [k, until] of Object.entries(this.timed)) { if (until > t) { out[k] = true; any = true; } else delete this.timed[k]; }
    return any ? out : null;
  }

  get canOpen() {
    const g = this.game, p = g.player;
    return g.gameplay?.state === 'playing' && !g.cutscene && !g.hud?.menuOpen && !p.dead && g.input.enabled && !g.weaponWheel?.open;
  }

  // before the player's controls read the keys: the phone takes the arrows, Enter and Backspace (and the D-pad,
  // A and B) while it's out, so you can still walk with WASD / the left stick
  earlyInput() {
    const g = this.game, input = g.input, p = g.player;
    const take = (code) => { const hit = input.pressed.has(code); input.pressed.delete(code); input.keys.delete(code); return hit; };
    const pad = (i) => { const hit = input.gpHit(i); input.mask.add(i); return hit; };
    if (this.photo) {
      const out = take('KeyI') | take('Backspace') | take('Escape') | pad(GP.B) | pad(GP.UP);
      if (out || !this.canOpen) this._photo(false);
      return;
    }
    if (!this.open) {
      // D-pad up takes it out on foot, and in vehicles without hydraulics
      const padOpen = (!p.vehicle || !p.vehicle.def.hydraulics) && input.gpHit(GP.UP);
      if ((input.keyHit('KeyI') || padOpen) && this.canOpen) { take('KeyI'); if (padOpen) input.mask.add(GP.UP); this.show(); }
      return;
    }
    if (take('KeyI') | take('Escape')) { this.hide(); return; }
    if (!this.canOpen) { this.hide(); return; }
    const up = take('ArrowUp') | pad(GP.UP), down = take('ArrowDown') | pad(GP.DOWN);
    const left = take('ArrowLeft') | pad(GP.LEFT), right = take('ArrowRight') | pad(GP.RIGHT);
    const ok = take('Enter') | pad(GP.A), back = take('Backspace') | pad(GP.B);
    const n = this.items.length;
    const cols = this.screen === 'home' ? 3 : 1;
    if (n) {
      if (up) this.sel = (this.sel - cols + n) % n;
      if (down) this.sel = (this.sel + cols) % n;
      if (left && cols > 1) this.sel = (this.sel - 1 + n) % n;
      if (right && cols > 1) this.sel = (this.sel + 1) % n;
      if (up || down || left || right) { g.audio?.play('ui', 0.5); this._render(); }
      if (ok) { g.audio?.play('ui', 1); this.items[this.sel]?.go?.(); }
    }
    if (back) { if (this.screen === 'home') this.hide(); else this._go('home'); }
  }

  show() {
    this.open = true;
    this.el.classList.add('show');
    document.body.classList.add('phone-open');
    this.game.audio?.play('ui', 1);
    this._go('home');
  }

  hide() {
    this.open = false;
    this.el.classList.remove('show');
    document.body.classList.remove('phone-open');
  }

  _go(screen) {
    this.screen = screen;
    this.sel = 0;
    this._build();
    this._render();
  }

  // ------------------------------------------------------------------ screens
  _build() {
    const g = this.game, free = !!g.freeroam?.active;
    const s = this.screen;
    this.items = [];
    if (s === 'home') this.items = APPS.map((a) => ({ ...a, go: () => this._app(a.id) }));
    else if (s === 'contacts') {
      this.items = [
        { name: 'Downtown Cab Co.', sub: 'A cab comes to you', go: () => this._call('Downtown Cab Co.', () => this._cab()) },
        { name: 'Benny\'s Motorworks', sub: this.lastCar ? `Deliver your ${VEHICLES[this.lastCar.type].name}${free ? '' : ' · $200'}` : `Deliver a car${free ? '' : ' · $200'}`, go: () => this._call('Benny', () => this._mechanic()) },
        { name: 'Lester', sub: g.police?.level ? `Lose your ${g.police.level}-star wanted level${free ? '' : ` · ${money(this._lesterPrice())}`}` : 'Makes the cops lose your file', go: () => this._call('Lester', () => this._lester()) },
        { name: 'Merryweather Security', sub: `Three armed contractors${free ? '' : ' · $1,000'}`, go: () => this._call('Merryweather', () => this._merryweather()) },
      ];
      if (free) this.items.push({ name: 'Pegasus Concierge', sub: 'A Skylark helicopter dropped off nearby', go: () => this._call('Pegasus', () => this._pegasus()) });
    } else if (s === 'cheats') {
      this.items = CHEATS.map(([code, what]) => ({ name: code, sub: what, go: () => { this.cheat(code); } }));
    } else if (s === 'weather') {
      const env = g.env;
      if (free) this.items = ['clear', 'cloudy', 'rain', 'storm', 'fog'].map((w) => ({ name: w[0].toUpperCase() + w.slice(1), sub: env.weather === w ? 'now' : '', go: () => { g.admin?.setWeather(w); this._build(); this._render(); } }));
      else this.items = [{ name: `Forecast: ${env.weather}`, sub: 'Change the weather in free roam, or with the MAKEITRAIN cheat', go: () => {} }];
    } else if (s === 'stats') {
      const st = g.stats;
      this.items = [
        ['Kills', st.kills], ['Headshots', st.headshots], ['Cars stolen', st.carsStolen], ['Highest wanted level', `${st.maxWanted} ★`],
        ['Missions passed', st.missions], ['Best drift', `${Math.floor(st.bestDrift)} m`], ['Times wasted', st.wasted], ['Times busted', st.busted],
      ].map(([n, v]) => ({ name: n, sub: String(v), go: () => {} }));
    }
  }

  _render() {
    const g = this.game;
    this.statusEl.innerHTML = `<span>${g.env.timeString}</span><span>iFruit</span><span>▮▮▮▯ ▰</span>`;
    const titles = { home: '', contacts: 'Contacts', cheats: 'Cheats', weather: 'Weather', stats: 'Stats' };
    this.titleEl.textContent = titles[this.screen] || '';
    this.titleEl.style.display = this.screen === 'home' ? 'none' : '';
    this.body.innerHTML = '';
    this.body.className = 'phone-body ' + (this.screen === 'home' ? 'grid' : 'list');
    if (this.screen === 'home') {
      h('div', 'phone-clock', this.body, `${g.env.timeString}<small>${g.player.money != null ? money(g.player.money) : ''}</small>`);
      this.items.forEach((a, i) => {
        const e = h('div', 'phone-app' + (i === this.sel ? ' sel' : ''), this.body, `<i style="background:${a.color}">${a.icon}</i><span>${a.name}</span>`);
        e.onclick = () => { this.sel = i; a.go(); };
      });
    } else {
      this.items.forEach((it, i) => {
        const e = h('div', 'phone-item' + (i === this.sel ? ' sel' : ''), this.body, `<b>${it.name}</b>${it.sub ? `<small>${it.sub}</small>` : ''}`);
        e.onclick = () => { this.sel = i; it.go(); };
        if (i === this.sel) requestAnimationFrame(() => e.scrollIntoView?.({ block: 'nearest' }));
      });
    }
    const pad = g.input.lastDevice === 'gamepad';
    this.foot.innerHTML = pad ? '<span>Ⓐ Select</span><span>Ⓑ Back</span>' : '<span>↵ Select</span><span>⌫ Back</span><span>I Close</span>';
  }

  _app(id) {
    const g = this.game;
    if (id === 'camera') { this.hide(); this._photo(true); return; }
    if (id === 'map') { this.hide(); g.hud?.openPause(g.freeroam?.active ? 'teleport' : 'map'); return; }
    this._go(id);
  }

  _photo(on) {
    this.photo = on;
    document.body.classList.toggle('photo-mode', on);
    this.photoHint.classList.toggle('show', on);
    if (on) this.game.audio?.play('ui', 1);
  }

  // ------------------------------------------------------------------ calls
  _call(who, fn) {
    const g = this.game;
    this.hide();
    g.hud?.help(`Calling <b>${who}</b>…`, 2);
    this.pending.push({ t: g.time + 1.6, fn });
  }

  _pay(amount) {
    const g = this.game, p = g.player;
    if (g.freeroam?.active || amount <= 0) return true;
    if (p.money < amount) return false;
    p.money -= amount;
    g.audio?.play('cash');
    return true;
  }

  _cab() {
    const g = this.game;
    if (g.player.vehicle) { g.hud?.subtitle('You\'re already driving, pal. Call us when you\'re on foot.', 'Downtown Cab Co.', 4); return; }
    if (!g.taxi) return;
    g.hud?.subtitle('Downtown Cab Co., where to? We\'ll send someone right over.', 'Dispatcher', 3.5);
    g.taxi.hailCab();
  }

  _lesterPrice() { return 500 * (this.game.police?.level || 1); }

  _lester() {
    const g = this.game, pol = g.police;
    if (!pol || pol.level === 0) { g.hud?.subtitle('You\'re not wanted. Why are you calling me? Don\'t answer that.', 'Lester', 4); return; }
    if (g.missions?.active && g.missions.maxWanted != null) { g.hud?.subtitle('Not while you\'re on a job. They\'ll trace it straight back to me.', 'Lester', 4); return; }
    if (!this._pay(this._lesterPrice())) { g.hud?.subtitle(`Clean records cost money. ${money(this._lesterPrice())}. Call me back.`, 'Lester', 4); return; }
    g.hud?.subtitle('Give me a minute. I\'m in the dispatch system now…', 'Lester', 3.5);
    this.pending.push({ t: g.time + 5, fn: () => { if (g.police.level > 0) { g.police.clear(); g.hud?.subtitle('Done. As far as the LSPD knows, you were never there.', 'Lester', 4); } } });
  }

  _mechanic() {
    const g = this.game, p = g.player;
    if (g.missions?.active) { g.hud?.subtitle('I don\'t deliver into the middle of a job, homie.', 'Benny', 4); return; }
    const type = this.lastCar?.type || 'kestrel';
    if (!this._pay(200)) { g.hud?.subtitle('Delivery\'s two hundred, homie. Come back with the cash.', 'Benny', 4); return; }
    g.hud?.subtitle(`One ${VEHICLES[type].name}, coming right up. Give me a sec.`, 'Benny', 3.5);
    this.pending.push({ t: g.time + 6, fn: () => {
      const v = this._deliver(type, this.lastCar?.color);
      if (!v) { g.hud?.subtitle('Can\'t find a street out there to leave it on. Here\'s your money back.', 'Benny', 4); if (!g.freeroam?.active) p.money += 200; return; }
      g.hud?.subtitle('Your ride\'s out front. The blue blip. Try to bring it back in one piece.', 'Benny', 4.5);
    } });
  }

  // a car left at the kerb near the player, clear of traffic
  _deliver(type, color) {
    const g = this.game, p = g.player, d = VEHICLES[type];
    const pp = p.vehicle ? p.vehicle.pos : p.pos;
    for (const [ahead, side] of [[16, 0], [26, 0], [-16, 0], [0, 16], [0, -16], [36, 10], [-30, -10]]) {
      const fx = Math.sin(p.yaw), fz = Math.cos(p.yaw);
      const sp = g.freeroam?.roadSpot(pp.x + fx * ahead - fz * side, pp.z + fz * ahead + fx * side, false);
      if (!sp || Math.hypot(sp.x - pp.x, sp.z - pp.z) > 70 || Math.hypot(sp.x - pp.x, sp.z - pp.z) < 5) continue;
      // nudge towards the kerb, and make sure nothing's parked there already
      const x = sp.x - Math.cos(sp.yaw) * 0.8, z = sp.z + Math.sin(sp.yaw) * 0.8;
      if (g.vehicles.list.some((o) => !o.removed && Math.hypot(o.pos.x - x, o.pos.z - z) < (o.def.L + d.L) / 2 + 1)) continue;
      if (this.delivered && !this.delivered.removed && !this.delivered.driver) g.vehicles.remove(this.delivered);
      const v = g.vehicles.spawn(type, x, z, sp.yaw, { persistent: true, parked: true, color, y: sp.y });
      v.ownedByPlayer = true;
      this.delivered = v;
      if (this.deliveredBlip) g.blips.delete(this.deliveredBlip);
      this.deliveredBlip = { x, z, icon: 'car', color: 0x5fb2ff };
      g.blips.add(this.deliveredBlip);
      return v;
    }
    return null;
  }

  _merryweather() {
    const g = this.game, p = g.player;
    this.mercs = this.mercs.filter((m) => !m.removed && !m.dead);
    if (this.mercs.length >= 3) { g.hud?.subtitle('Your detail is already on site, sir.', 'Merryweather', 3.5); return; }
    if (!this._pay(1000)) { g.hud?.subtitle('Merryweather requires payment up front. One thousand dollars.', 'Merryweather', 4); return; }
    g.hud?.subtitle('Contractors dispatched. Three operators, inbound to your position.', 'Merryweather', 4);
    this.pending.push({ t: g.time + 4, fn: () => {
      const rng = new RNG((Math.random() * 1e9) | 0);
      const a0 = p.yaw + Math.PI;
      for (let i = this.mercs.length; i < 3; i++) {
        const a = a0 + (i - 1) * 0.5, d = 22 + i * 3;
        let x = p.pos.x + Math.sin(a) * d, z = p.pos.z + Math.cos(a) * d;
        // (walk in off the pavement rather than out of a wall)
        const sp = g.freeroam?.roadSpot(x, z, true);
        if (sp && Math.hypot(sp.x - p.pos.x, sp.z - p.pos.z) < 45) { x = sp.x + (i - 1) * 1.2; z = sp.z; }
        const look = randomAppearance(rng, { female: rng.chance(0.15), shirt: 0x24272c, shirtType: 'jacket', jacketColor: 0x1b1d21, pants: 0x2a2d33, hairStyle: 'cap', hat: 0x111214, glasses: true, shoes: 0x0d0d0d, beard: rng.chance(0.4), bandana: null, shorts: false });
        const m = g.peds.spawnPed(x, z, { appearance: look, brain: 'civilian', team: 'player', health: 200, armor: 60, persistent: true, y: g.collision.floorHeight(x, z, p.pos.y + 1.5) });
        m.giveWeapon('rifle', 900); m.equip('rifle');
        m.accuracy = 0.7; m.damageMul = 1;
        m.follow = p; m.followSlot = i % 3; m.setState('follow');
        m.merc = true; m.hireUntil = g.time + 600;
        this.mercs.push(m);
      }
    } });
  }

  _pegasus() {
    const g = this.game;
    if (!g.admin) return;
    const msg = g.admin.summon('skylark', { into: false });
    g.hud?.subtitle(/Skylark/.test(msg) ? 'Your Skylark has been delivered, sir. Enjoy your flight.' : msg, 'Pegasus', 4);
  }

  // ------------------------------------------------------------------ cheats
  cheat(code) {
    const g = this.game, p = g.player, free = !!g.freeroam?.active && !!g.admin?.allowed;
    const say = (t) => { g.hud?.help(`Cheat activated: <b>${code}</b> · ${t}`, 3.5); g.audio?.play('pickup'); };
    const timed = (key, label) => {
      if (free) { const on = !g.admin.cheats[key]; g.admin.cheats[key] = on; say(`${label} ${on ? 'on' : 'off'}`); return; }
      if (this.timed[key] > g.time) { delete this.timed[key]; say(`${label} off`); return; }
      this.timed[key] = g.time + CHEAT_TIME; say(`${label} for five minutes`);
    };
    const spawnNear = (type) => {
      if (g.missions?.active) { g.hud?.help('Not during a mission.', 3); return false; }
      if (g.admin?.canSpawn) { const m = g.admin.summon(type, { into: false }); return !!m; }
      const d = VEHICLES[type], a = p.yaw;
      const x = p.pos.x + Math.sin(a) * (d.L / 2 + 4), z = p.pos.z + Math.cos(a) * (d.L / 2 + 4);
      const v = g.vehicles.spawn(type, x, z, p.yaw, { persistent: true, y: d.kind === 'heli' ? Math.max(g.map.groundHeight(x, z), 0) + 60 : undefined });
      if (d.kind === 'heli') { v.spool = 1; v.grounded = false; }
      return true;
    };
    this.hide();
    if (g.net?.online) { g.hud?.help('Cheats are switched off online.', 3); return; }
    switch (code) {
      case 'PAINKILLER': timed('god', 'Invincibility'); break;
      case 'TURTLE': p.health = p.maxHealth; p.armor = 100; say('Max health and armour'); break;
      case 'TOOLUP': for (const [id, d] of Object.entries(WEAPONS)) { if (d.vehicleOnly || d.hidden) continue; p.giveWeapon(id, (d.clip || 1) * 8); } say('Weapons'); break;
      case 'LAWYERUP': g.police?.clear(); say('Wanted level cleared'); break;
      case 'FUGITIVE': if (g.police) { g.police.setLevel(Math.min(5, g.police.level + 1)); say(`Wanted level ${g.police.level}`); } break;
      case 'SKYFALL': g.admin?.skydive(); say('Skydive'); break;
      case 'BUZZOFF': if (spawnNear('warhawk')) say('Warhawk gunship'); break;
      case 'COMET': if (spawnNear('zenith')) say('Zenith'); break;
      case 'OFFROAD': if (spawnNear('trail')) say('Trailblazer'); break;
      case 'HOPTOIT': timed('superJump', 'Super jump'); break;
      case 'CATCHME': timed('superRun', 'Fast run'); break;
      case 'HIGHEX': timed('explosive', 'Explosive bullets'); break;
      case 'POWERUP': if (g.special) g.special.meter = 1; say('Special ability recharged'); break;
      case 'SLOWMO': {
        const on = !this._slow; this._slow = on;
        if (on) g.slowmo.cheat = 0.5; else delete g.slowmo.cheat;
        say(`Slow motion ${on ? 'on' : 'off'}`); break;
      }
      case 'MAKEITRAIN': {
        const order = ['clear', 'cloudy', 'rain', 'storm', 'fog'];
        const next = order[(order.indexOf(g.env.weather) + 1) % order.length];
        g.env.setWeather(next, true);
        say(`Weather: ${next}`); break;
      }
      default: g.hud?.help('Unknown number.', 2);
    }
  }

  // ------------------------------------------------------------------ per frame
  update(dt) {
    const g = this.game, p = g.player;
    // the last road vehicle you drove (for Benny)
    const v = p.vehicle;
    if (v && p.seat === 0 && !v.def.kind && !v.def.train && !v.def.police && !v.def.military && !v.missionTag && v.type !== 'taxi') this.lastCar = { type: v.type, color: v.color };
    // calls being acted on
    for (let i = this.pending.length - 1; i >= 0; i--) if (g.time >= this.pending[i].t) { const c = this.pending.splice(i, 1)[0]; c.fn(); }
    // Benny's delivery: the blip follows the car until you get in
    const dv = this.delivered;
    if (this.deliveredBlip) {
      if (!dv || dv.removed || dv.isWrecked || dv.driver?.isPlayer) { g.blips.delete(this.deliveredBlip); this.deliveredBlip = null; }
      else { this.deliveredBlip.x = dv.pos.x; this.deliveredBlip.z = dv.pos.z; }
    }
    // Merryweather contractors: ten minutes on the clock, or until they're left behind
    for (const m of this.mercs) {
      if (m.removed) continue;
      const far = Math.hypot(m.pos.x - p.pos.x, m.pos.z - p.pos.z) > 300;
      if (g.time > m.hireUntil || far || (m.dead && far)) g.peds.remove(m);
    }
    this.mercs = this.mercs.filter((m) => !m.removed);
    if (this.open && Math.floor(g.time) !== this._lastClock) { this._lastClock = Math.floor(g.time); this.statusEl.innerHTML = `<span>${g.env.timeString}</span><span>iFruit</span><span>▮▮▮▯ ▰</span>`; }
  }

  reset(keepCheats = false) {
    for (const m of this.mercs) if (!m.removed) { m.persistent = false; m.follow = null; m.setState?.('wander'); }
    this.mercs = [];
    if (!keepCheats) this.timed = {};
    this.pending = [];
    if (this._slow) { this._slow = false; delete this.game.slowmo.cheat; }
    this.hide();
    this._photo(false);
  }
}
