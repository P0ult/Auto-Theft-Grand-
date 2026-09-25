// DOM + canvas HUD: stats cluster, radar, messages, subtitles, pause menu & map, shop, overlays.
import * as THREE from 'three';
import { buildMapImage, MAP_EXTENT, drawMapLayers } from './mapimage.js';
import { WEAPONS, WEAPON_ORDER } from '../game/weapondefs.js';
import { route } from '../game/gps.js';
import { formatMoney, clamp } from '../core/utils.js';
import { VEHICLES } from '../entities/vehicledefs.js';

const h = (tag, cls, parent, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html != null) e.innerHTML = html; if (parent) parent.appendChild(e); return e; };

export class HUD {
  constructor(game) {
    this.game = game;
    const root = h('div', 'hud', document.body);
    this.root = root;
    // top-right cluster
    const tr = h('div', 'hud-tr', root);
    this.clock = h('div', 'hud-clock', tr, '12:00');
    const row = h('div', 'hud-row', tr);
    this.weaponBox = h('div', 'hud-weapon', row);
    this.weaponCanvas = h('canvas', '', this.weaponBox); this.weaponCanvas.width = 96; this.weaponCanvas.height = 96;
    this.ammo = h('div', 'hud-ammo', this.weaponBox);
    const bars = h('div', 'hud-bars', row);
    this.armorBar = h('div', 'hud-bar armor', bars, '<div></div>');
    this.healthBar = h('div', 'hud-bar health', bars, '<div></div>');
    this.money = h('div', 'hud-money', tr, '$00000000');
    this.stars = h('div', 'hud-stars', tr);
    this.starEls = [];
    for (let i = 0; i < 5; i++) this.starEls.push(h('span', 'star', this.stars, '★'));
    this.moneyPop = h('div', 'hud-money-pop', tr);
    // radar
    this.radarWrap = h('div', 'hud-radar', root);
    this.radar = h('canvas', '', this.radarWrap);
    this.radar.width = 230; this.radar.height = 230;
    this.rctx = this.radar.getContext('2d');
    // messages
    this.helpBox = h('div', 'hud-help', root);
    this.bigMsg = h('div', 'hud-big', root);
    this.subMsg = h('div', 'hud-sub', root);
    this.subtitles = h('div', 'hud-subtitles', root);
    this.zone = h('div', 'hud-zone', root);
    this.vehName = h('div', 'hud-vehname', root);
    this.radioName = h('div', 'hud-radio', root);
    this.objectiveEl = h('div', 'hud-objective', root);
    this.timerEl = h('div', 'hud-timer', root);
    this.counterEl = h('div', 'hud-counter', root);
    this.barEl = h('div', 'hud-progress', root, '<span></span><div><i></i></div>');
    this.crosshair = h('div', 'hud-crosshair', root, '<i></i><i></i><i></i><i></i>');
    this.speechLayer = h('div', 'hud-speech-layer', root);
    this.letterTop = h('div', 'letterbox top', document.body);
    this.letterBot = h('div', 'letterbox bottom', document.body);
    this.fadeEl = h('div', 'hud-fade', document.body);
    this.shard = h('div', 'hud-shard', document.body);
    this.shardText = h('span', '', this.shard, 'wasted');
    this.damageFlash = 0;
    this.overlay = h('div', 'hud-overlay', document.body);
    this.fpsEl = h('div', 'hud-fps', root);
    // speedometer (shown in vehicles)
    this.speedoWrap = h('div', 'hud-speedo', root);
    this.speedo = h('canvas', '', this.speedoWrap);
    this.speedo.width = 220; this.speedo.height = 220;
    this.sctx = this.speedo.getContext('2d');
    this.speedoNeedle = 0;
    this.interactEl = h('div', 'hud-interact', root);
    this.timers = { help: 0, big: 0, sub: 0, subs: 0, zone: 0, veh: 0, radio: 0, money: 0 };
    this.lastZone = '';
    this.lastVeh = null;
    this.speeches = [];
    this.mapImg = buildMapImage(game.map, 2048);
    this.route = null;
    this.routeTimer = 0;
    this.waypoint = null;
    this.radarRange = 130;
    this.paused = false;
    this.menuOpen = null;
    this.subtitleQueue = [];
    this.showFps = false;
    this.fpsAcc = 0; this.fpsN = 0;
    this._lastWeapon = null;
  }

  // ------------------------------------------------------------------ message API
  help(text, dur = 5) { this.helpBox.innerHTML = text; this.helpBox.classList.add('show'); this.timers.help = dur; }
  bigMessage(text, style = 'title', dur = 4, sub = '') {
    this.bigMsg.className = 'hud-big show ' + style;
    this.bigMsg.innerHTML = `<div>${text}</div>${sub ? `<small>${sub}</small>` : ''}`;
    this.timers.big = dur;
  }
  subtitle(text, speaker = '', dur = 4) {
    this.subtitles.innerHTML = speaker ? `<b style="color:${speakerColor(speaker)}">${speaker}:</b> ${text}` : text;
    this.subtitles.classList.add('show');
    this.timers.subs = dur;
  }
  objective(text, dur = 7) {
    this.subtitles.innerHTML = text;
    this.subtitles.classList.add('show');
    this.timers.subs = dur;
    this.objectiveEl.innerHTML = text;
    this.currentObjective = text;
  }
  clearObjective() { this.objectiveEl.innerHTML = ''; this.currentObjective = ''; }
  setTimer(sec) { if (sec == null) { this.timerEl.style.display = 'none'; return; } this.timerEl.style.display = 'block'; const m = Math.floor(Math.max(0, sec) / 60), s = Math.floor(Math.max(0, sec) % 60); this.timerEl.textContent = `${m}:${String(s).padStart(2, '0')}`; this.timerEl.classList.toggle('urgent', sec < 10); }
  setCounter(label, value) { if (label == null) { this.counterEl.style.display = 'none'; return; } this.counterEl.style.display = 'block'; this.counterEl.innerHTML = `${label} <b>${value}</b>`; }
  setBar(label, v, color = '#e63946') { if (label == null) { this.barEl.style.display = 'none'; return; } this.barEl.style.display = 'block'; this.barEl.querySelector('span').textContent = label; const i = this.barEl.querySelector('i'); i.style.width = `${clamp(v, 0, 1) * 100}%`; i.style.background = color; }
  showRadio(st) { this.radioName.innerHTML = st.style === 'off' ? 'Radio Off' : `${st.name}<small>${st.genre}</small>`; this.radioName.classList.add('show'); this.timers.radio = 3; }
  moneyFlash(amount) { this.moneyPop.textContent = (amount >= 0 ? '+' : '-') + formatMoney(Math.abs(amount)).replace('$0000', '$').replace(/^\$0+/, '$'); this.moneyPop.classList.add('show'); this.timers.money = 2; }
  interact(text) { if (text) { this.interactEl.innerHTML = text; this.interactEl.style.display = 'block'; } else this.interactEl.style.display = 'none'; }
  speech(ped, text) {
    if (this.speeches.length > 5) { const s = this.speeches.shift(); s.el.remove(); }
    const el = h('div', 'speech', this.speechLayer, text);
    this.speeches.push({ ped, el, t: 3 });
  }
  letterbox(on) { document.body.classList.toggle('cinematic', on); }
  fade(dur = 0.5, mid = null) {
    this.fadeEl.style.transition = `opacity ${dur}s`;
    this.fadeEl.style.opacity = 1;
    setTimeout(() => { mid?.(); this.fadeEl.style.opacity = 0; }, dur * 1000 + 150);
  }
  fadeTo(v, dur = 0.5) { this.fadeEl.style.transition = `opacity ${dur}s`; this.fadeEl.style.opacity = v; }
  damage(amount) { this.damageFlash = Math.min(1, this.damageFlash + amount / 40); }

  setWaypoint(x, z) {
    if (this.waypoint && this.waypoint.x === x && this.waypoint.z === z) { this.waypoint = null; this.game.blips.delete(this.waypointBlip); this.route = null; return; }
    if (this.waypointBlip) this.game.blips.delete(this.waypointBlip);
    this.waypoint = { x, z };
    this.waypointBlip = { x, z, color: 0xd96cff, icon: 'waypoint' };
    this.game.blips.add(this.waypointBlip);
    this.routeTimer = 0;
  }
  routeTo(target) { this.gpsTarget = target; this.routeTimer = 0; }

  // ------------------------------------------------------------------ overlays
  showCredits() {
    const g = this.game;
    const c = h('div', 'credits', document.body);
    c.innerHTML = `<div class="roll">
      <h1>AUTO THEFT GRAND</h1><p>Los Soles</p>
      <h3>STARRING</h3><p>Andre "Dre" Castillo<br>Big Lou · Marisol Castillo · Deacon<br>Detective Frank Voss · Officer Ruiz<br>Rico · Maddox · Salazar · Chino</p>
      <h3>THE CITY</h3><p>Cedar Row · Downtown · Market District · Rosewood<br>El Corona · Port Morena · Santa Luz Beach · Vistawood Hills</p>
      <h3>MADE WITH</h3><p>Three.js · WebGL 2 · Web Audio<br>Procedural everything: every building, car, person, song and sunset is generated in your browser.</p>
      <h3>IN MEMORY OF</h3><p>Tino Castillo</p>
      <h3>THANK YOU FOR PLAYING</h3><p>The city is yours. Keep exploring — there are still hidden packages to find.</p>
    </div>`;
    const done = () => { c.remove(); g.paused = false; };
    c.onclick = done;
    setTimeout(done, 42000);
  }

  // GTA V style "shard": dark band across the screen with the word in red, the rest of the HUD hidden
  showWasted(kind) {
    this.shardText.textContent = kind === 'busted' ? 'busted' : 'wasted';
    this.shard.className = 'hud-shard ' + kind;
    void this.shard.offsetWidth; // restart the CSS animation
    this.shard.classList.add('show');
  }
  deathMode(on) {
    document.body.classList.toggle('dead', on);
    if (!on) this.shard.className = 'hud-shard';
  }

  openShop() {
    const game = this.game;
    if (game.player.vehicle) return;
    game.paused = true;
    game.input.exitLock();
    const items = ['bat', 'knife', 'pistol', 'smg', 'shotgun', 'rifle', 'rpg', 'grenade'];
    const o = this.overlay;
    const render = () => {
      const p = game.player;
      o.innerHTML = '';
      o.className = 'hud-overlay show';
      const panel = h('div', 'menu-panel shop', o);
      h('h2', '', panel, 'GUN BARN <small>Est. 1979 — No questions asked</small>');
      h('div', 'shop-money', panel, `Cash: <b>${formatMoney(p.money)}</b>`);
      const list = h('div', 'shop-list', panel);
      for (const id of items) {
        const d = WEAPONS[id];
        const owned = !!p.weapons[id];
        const row = h('div', 'shop-item', list);
        const cv = h('canvas', '', row); cv.width = 64; cv.height = 64; drawWeaponIcon(cv.getContext('2d'), id, 64);
        h('div', 'shop-name', row, `${d.name}<small>${d.type === 'melee' ? 'Melee' : d.type === 'thrown' ? 'Explosive' : `Dmg ${d.damage}${d.pellets > 1 ? '×' + d.pellets : ''} · ${d.auto ? 'Automatic' : 'Semi'} · Clip ${d.clip}`}</small>`);
        const buy = h('button', 'btn', row, owned && d.type !== 'melee' ? `Ammo $${d.ammoPrice}` : owned ? 'Owned' : `Buy $${d.price}`);
        if (owned && d.type === 'melee') buy.disabled = true;
        buy.onclick = () => {
          const cost = owned ? d.ammoPrice : d.price;
          if (p.money < cost) { game.audio?.play('locked'); buy.textContent = 'Not enough cash'; return; }
          p.money -= cost;
          if (owned) p.giveWeapon(id, d.ammoPack); else { p.giveWeapon(id, d.type === 'melee' ? 0 : d.ammoPack); p.switchTo(id); }
          game.audio?.play('cash');
          game.events.emit('purchase', id);
          render();
        };
      }
      const armorRow = h('div', 'shop-item', list);
      h('div', 'shop-name', armorRow, 'Body Armor<small>Absorbs 80% of incoming damage</small>');
      const ab = h('button', 'btn', armorRow, p.armor >= 100 ? 'Full' : 'Buy $200');
      ab.onclick = () => { if (p.armor >= 100) return; if (p.money < 200) { ab.textContent = 'Not enough cash'; return; } p.money -= 200; p.armor = 100; game.audio?.play('cash'); render(); };
      const close = h('button', 'btn primary', panel, 'Leave shop (Esc)');
      close.onclick = () => this.closeOverlay();
    };
    render();
    this.menuOpen = 'shop';
  }

  promptSave() {
    const game = this.game;
    if (game.missions?.active) { this.help('You can\'t save during a mission.'); return; }
    game.paused = true;
    game.input.exitLock();
    const o = this.overlay;
    o.innerHTML = ''; o.className = 'hud-overlay show';
    const panel = h('div', 'menu-panel small', o);
    h('h2', '', panel, 'Safehouse');
    h('p', '', panel, 'Home sweet home. Save your progress? Saving also advances the clock by six hours and restores your health.');
    const b1 = h('button', 'btn primary', panel, 'Save game');
    const b2 = h('button', 'btn', panel, 'Cancel');
    b1.onclick = () => { game.save?.save(); game.player.health = game.player.maxHealth; game.env.setTime(game.env.hours + 6); this.closeOverlay(); this.help('Game saved.'); };
    b2.onclick = () => this.closeOverlay();
    this.menuOpen = 'save';
  }

  closeOverlay() {
    this.overlay.className = 'hud-overlay';
    this.overlay.innerHTML = '';
    this.menuOpen = null;
    this.game.paused = false;
    if (this.game.gameplay?.state === 'playing') this.game.input.requestLock();
  }

  togglePause() {
    if (this.menuOpen === 'pause') { this.closeOverlay(); return; }
    if (this.menuOpen) { this.closeOverlay(); return; }
    this.openPause('map');
  }

  openPause(tab = 'map') {
    const game = this.game;
    game.paused = true;
    game.input.exitLock();
    this.menuOpen = 'pause';
    const o = this.overlay;
    o.innerHTML = ''; o.className = 'hud-overlay show pause';
    const panel = h('div', 'menu-panel pause', o);
    const head = h('div', 'pause-head', panel);
    h('h1', 'logo-small', head, 'AUTO THEFT <span>GRAND</span>');
    const tabs = h('div', 'tabs', head);
    const body = h('div', 'pause-body', panel);
    const T = { map: 'Map', brief: 'Brief', stats: 'Stats', settings: 'Settings', controls: 'Controls' };
    const show = (t) => {
      tabs.querySelectorAll('button').forEach((b) => b.classList.toggle('active', b.dataset.t === t));
      body.innerHTML = '';
      this['_tab_' + t](body);
    };
    for (const k in T) { const b = h('button', 'tab', tabs, T[k]); b.dataset.t = k; b.onclick = () => { game.audio?.play('ui'); show(k); }; }
    const foot = h('div', 'pause-foot', panel);
    const resume = h('button', 'btn primary', foot, 'Resume (Esc)'); resume.onclick = () => this.closeOverlay();
    const save = h('button', 'btn', foot, 'Save game'); save.onclick = () => { if (game.missions?.active) { save.textContent = 'Can\'t save during a mission'; return; } game.save?.save(); save.textContent = 'Saved ✓'; };
    const quit = h('button', 'btn', foot, 'Quit to title'); quit.onclick = () => { location.reload(); };
    show(tab);
  }

  _tab_map(body) {
    const game = this.game;
    const wrap = h('div', 'bigmap', body);
    const cv = h('canvas', '', wrap);
    const W = Math.min(window.innerWidth - 80, 1100), H = Math.min(window.innerHeight - 220, 700);
    cv.width = W; cv.height = H;
    const ctx = cv.getContext('2d');
    const img = this.mapImg;
    const p = game.player.vehicle ? game.player.vehicle.pos : game.player.pos;
    const view = this._mapView || { cx: p.x, cz: p.z, zoom: 1.6 };
    this._mapView = view;
    const legend = h('div', 'map-legend', wrap, '<b>Right-click / double-click</b> to set a waypoint · <b>Wheel</b> to zoom · <b>Drag</b> to pan<br>' +
      '<i style="background:#ffd23f"></i>Mission <i style="background:#ff5a36"></i>Gun Barn <i style="background:#6df0ff"></i>Spray Shack <i style="background:#6cff6c"></i>Safehouse <i style="background:#ffd166"></i>Food <i style="background:#d96cff"></i>Waypoint');
    const toWorld = (mx, my) => [view.cx + (mx - W / 2) / (img.sx * view.zoom), view.cz + (my - H / 2) / (img.sz * view.zoom)];
    const draw = () => {
      ctx.fillStyle = '#0d2233'; ctx.fillRect(0, 0, W, H);
      const s = view.zoom;
      const [ox, oy] = img.toPx(view.cx, view.cz);
      ctx.save();
      ctx.translate(W / 2, H / 2); ctx.scale(s, s); ctx.translate(-ox, -oy);
      drawMapLayers(ctx, img);
      ctx.restore();
      const w2s = (x, z) => { const [px, py] = img.toPx(x, z); return [(px - ox) * s + W / 2, (py - oy) * s + H / 2]; };
      ctx.font = 'bold 14px "Bebas Neue", Impact, sans-serif'; ctx.textAlign = 'center';
      for (const L of img.labels) {
        if (!L.big && view.zoom < 1.2) continue;
        ctx.font = L.big ? 'bold 18px "Bebas Neue", Impact, sans-serif' : 'bold 14px "Bebas Neue", Impact, sans-serif';
        const [x, y] = w2s(L.x, L.z); ctx.fillStyle = 'rgba(0,0,0,0.6)'; ctx.fillText(L.name.toUpperCase(), x + 1, y + 1); ctx.fillStyle = L.big ? 'rgba(255,236,170,0.95)' : 'rgba(255,255,255,0.85)'; ctx.fillText(L.name.toUpperCase(), x, y);
      }
      if (this.route) { ctx.strokeStyle = '#d96cff'; ctx.lineWidth = 3; ctx.beginPath(); this.route.forEach(([x, z], i) => { const [sx, sy] = w2s(x, z); if (i) ctx.lineTo(sx, sy); else ctx.moveTo(sx, sy); }); ctx.stroke(); }
      for (const b of game.blips) { const [x, y] = w2s(b.x, b.z); drawBlip(ctx, x, y, b, 1.3); }
      const [px, py] = w2s(p.x, p.z);
      drawPlayerArrow(ctx, px, py, (game.player.vehicle ? game.player.vehicle.yaw : game.player.yaw), 1.4);
    };
    draw();
    let drag = null;
    cv.onmousedown = (e) => { drag = [e.offsetX, e.offsetY, view.cx, view.cz]; };
    cv.onmousemove = (e) => { if (drag) { view.cx = drag[2] - (e.offsetX - drag[0]) / (img.sx * view.zoom); view.cz = drag[3] - (e.offsetY - drag[1]) / (img.sz * view.zoom); draw(); } };
    window.addEventListener('mouseup', () => { drag = null; }, { once: true });
    cv.onwheel = (e) => { e.preventDefault(); view.zoom = clamp(view.zoom * (e.deltaY > 0 ? 0.85 : 1.18), 0.3, 14); draw(); };
    const setWP = (e) => { e.preventDefault(); const [x, z] = toWorld(e.offsetX, e.offsetY); this.setWaypoint(x, z); this.routeTimer = 0; this._updateRoute(true); draw(); };
    cv.oncontextmenu = setWP; cv.ondblclick = setWP;
  }

  _tab_brief(body) {
    const m = this.game.missions;
    const box = h('div', 'brief', body);
    h('h3', '', box, m?.active ? `Current mission: ${m.active.title}` : 'No active mission');
    h('p', '', box, this.currentObjective || (m?.active ? '' : 'Look for the <b style="color:#ffd23f">yellow letter blips</b> on your radar to start the next story mission.'));
    if (m) {
      h('h3', '', box, 'Story so far');
      const list = h('ol', 'story-log', box);
      for (const e of m.log.slice(-14)) h('li', '', list, e);
    }
  }

  _tab_stats(body) {
    const g = this.game, s = g.stats;
    const box = h('div', 'stats', body);
    const pct = g.missions ? Math.round((g.missions.completedCount / g.missions.total) * 100) : 0;
    const rows = [
      ['Story progress', `${pct}% (${g.missions?.completedCount ?? 0}/${g.missions?.total ?? 0} missions)`],
      ['Cash', formatMoney(g.player.money)], ['Time played', fmtTime(s.playTime)], ['People killed', s.kills], ['Cops killed', s.copKills],
      ['Headshots', s.headshots], ['Vehicles stolen', s.carsStolen], ['Vehicles destroyed', s.carsDestroyed], ['Pedestrians run over', s.runOver],
      ['Times wasted', s.wasted], ['Times busted', s.busted], ['Highest wanted level', '★'.repeat(s.maxWanted) || '—'], ['Longest drift', `${s.bestDrift.toFixed(0)} m`],
      ['Hidden packages', `${g.pickups?.collectedPackages.size ?? 0} / 30`], ['Distance driven', `${(s.driven / 1000).toFixed(1)} km`], ['Distance on foot', `${(s.walked / 1000).toFixed(1)} km`],
    ];
    const t = h('table', '', box);
    for (const [k, v] of rows) { const r = h('tr', '', t); h('td', '', r, k); h('td', '', r, String(v)); }
  }

  _tab_settings(body) {
    const g = this.game;
    const box = h('div', 'settings', body);
    const row = (label, el) => { const r = h('label', 'set-row', box); h('span', '', r, label); r.appendChild(el); return r; };
    const q = document.createElement('select');
    for (const k of ['low', 'medium', 'high', 'ultra']) { const o = h('option', '', q, k[0].toUpperCase() + k.slice(1)); o.value = k; if (k === g.settings.quality) o.selected = true; }
    q.onchange = () => { g.applyQuality(q.value); g.save?.saveSettings(); };
    row('Graphics quality', q);
    const slider = (val, min, max, step, on) => { const s = document.createElement('input'); s.type = 'range'; s.min = min; s.max = max; s.step = step; s.value = val; s.oninput = () => on(parseFloat(s.value)); return s; };
    row('Master volume', slider(g.settings.volume, 0, 1, 0.05, (v) => { g.settings.volume = v; g.audio?.setVolume(v); g.save?.saveSettings(); }));
    row('Radio volume', slider(g.settings.music, 0, 1, 0.05, (v) => { g.settings.music = v; g.audio?.setMusic(v); g.save?.saveSettings(); }));
    row('Mouse sensitivity', slider(g.settings.sensitivity, 0.2, 3, 0.05, (v) => { g.settings.sensitivity = v; g.input.sensitivity = v; g.save?.saveSettings(); }));
    const inv = document.createElement('input'); inv.type = 'checkbox'; inv.checked = g.settings.invertY; inv.onchange = () => { g.settings.invertY = inv.checked; g.input.invertY = inv.checked; g.save?.saveSettings(); };
    row('Invert mouse Y', inv);
    const fps = document.createElement('input'); fps.type = 'checkbox'; fps.checked = this.showFps; fps.onchange = () => { this.showFps = fps.checked; };
    row('Show FPS', fps);
    const ts = document.createElement('select');
    for (const [k, v] of [['1 min / sec (default)', 1], ['2 min / sec', 2], ['Slow (0.5)', 0.5], ['Freeze time', 0]]) { const o = h('option', '', ts, k); o.value = v; if (v === g.env.timeScale) o.selected = true; }
    ts.onchange = () => { g.env.timeScale = parseFloat(ts.value); };
    row('Day/night speed', ts);
  }

  _tab_controls(body) {
    const box = h('div', 'controls', body);
    box.innerHTML = `<div class="ctl-cols"><div><h3>On foot</h3><table>
      <tr><td>WASD</td><td>Move</td></tr><tr><td>Mouse</td><td>Look</td></tr><tr><td>Shift</td><td>Sprint</td></tr><tr><td>Space</td><td>Jump</td></tr>
      <tr><td>C / Ctrl</td><td>Crouch</td></tr><tr><td>Left mouse</td><td>Punch / fire</td></tr><tr><td>Right mouse</td><td>Aim</td></tr><tr><td>R</td><td>Reload</td></tr>
      <tr><td>Q / E, wheel, 1-9</td><td>Switch weapon</td></tr><tr><td>F / Enter</td><td>Enter / steal vehicle</td></tr></table></div>
      <div><h3>In a vehicle</h3><table><tr><td>W / S</td><td>Accelerate / brake-reverse</td></tr><tr><td>A / D</td><td>Steer</td></tr><tr><td>Space</td><td>Handbrake (drift!)</td></tr>
      <tr><td>H</td><td>Horn (Shift+H: siren in police cars)</td></tr><tr><td>N</td><td>Next radio station</td></tr><tr><td>V</td><td>Change camera</td></tr><tr><td>B</td><td>Look behind</td></tr>
      <tr><td>Right mouse + left mouse</td><td>Drive-by (pistol / SMG)</td></tr><tr><td>G</td><td>Hydraulics (lowriders)</td></tr><tr><td>F</td><td>Exit (bail out when fast)</td></tr></table>
      <h3>General</h3><table><tr><td>Esc / P</td><td>Pause, map & settings</td></tr><tr><td>M</td><td>Map</td></tr><tr><td>Space / Enter</td><td>Skip cutscene line</td></tr></table>
      <p class="muted">Gamepad supported (standard layout): sticks, RT/LT to drive, RB handbrake, Y enter vehicle, A sprint.</p></div></div>`;
  }

  // ------------------------------------------------------------------ update
  update(dt) {
    const game = this.game;
    const p = game.player;
    const input = game.input;
    if (input.hit('pause')) { if (!(this._autoPauseT && performance.now() - this._autoPauseT < 450)) this.togglePause(); }
    else if (input.hit('map') && !this.menuOpen) this.openPause('map');
    if (this.menuOpen) { this._tick(dt); return; }
    if (input.hit('radio') && p.vehicle) game.audio?.radio?.next();

    this.clock.textContent = game.env.timeString;
    // health / armor
    const hp = clamp(p.health / p.maxHealth, 0, 1);
    this.healthBar.firstChild.style.width = `${hp * 100}%`;
    this.healthBar.classList.toggle('low', hp < 0.25 && Math.floor(game.time * 3) % 2 === 0);
    this.armorBar.style.visibility = p.armor > 0 ? 'visible' : 'hidden';
    this.armorBar.firstChild.style.width = `${clamp(p.armor, 0, 100)}%`;
    this.money.textContent = formatMoney(p.money);
    // weapon
    const w = p.weapons[p.weapon];
    const def = WEAPONS[p.weapon];
    if (this._lastWeapon !== p.weapon) {
      this._lastWeapon = p.weapon;
      const c = this.weaponCanvas.getContext('2d');
      c.clearRect(0, 0, 96, 96);
      drawWeaponIcon(c, p.weapon, 96);
    }
    this.ammo.textContent = def.type === 'melee' ? '' : def.type === 'thrown' ? `${(w.clip || 0) + (w.ammo || 0)}` : `${w.ammo}-${w.clip}`;
    // wanted
    const police = game.police;
    const lvl = police ? police.level : 0;
    const flash = police?.flash && Math.floor(game.time * 4) % 2 === 0;
    this.starEls.forEach((s, i) => { s.className = 'star' + (i < lvl ? (flash ? ' on flash' : ' on') : ''); });
    // crosshair
    const aimVisible = (p.aiming && (def.type === 'gun' || def.type === 'launcher') && !p.dead) || (p.vehicle && p.aiming);
    this.crosshair.style.display = aimVisible ? 'block' : 'none';
    this.crosshair.classList.toggle('wide', def.id === 'shotgun' || def.id === 'smg');
    // zone name
    const zp = p.vehicle ? p.vehicle.pos : p.pos;
    const zone = game.map.zoneName(zp.x, zp.z);
    if (zone !== this.lastZone) { this.lastZone = zone; this.zone.textContent = zone; this.zone.classList.add('show'); this.timers.zone = 3.5; }
    if (p.vehicle !== this.lastVeh) { this.lastVeh = p.vehicle; if (p.vehicle) { this.vehName.textContent = p.vehicle.def.name; this.vehName.classList.add('show'); this.timers.veh = 3; if (game.audio?.radio) this.showRadio(game.audio.radio.current); } }
    // speech bubbles
    const cam = game.camera;
    const v = new THREE.Vector3();
    for (let i = this.speeches.length - 1; i >= 0; i--) {
      const s = this.speeches[i];
      s.t -= dt;
      if (s.t <= 0 || s.ped.removed) { s.el.remove(); this.speeches.splice(i, 1); continue; }
      const hp2 = s.ped.ragdolling ? s.ped.ragdoll.center : s.ped.pos;
      v.set(hp2.x, hp2.y + 2.1, hp2.z).project(cam);
      const d = cam.position.distanceTo(hp2);
      if (v.z > 1 || d > 35) { s.el.style.display = 'none'; continue; }
      s.el.style.display = 'block';
      s.el.style.left = `${(v.x * 0.5 + 0.5) * window.innerWidth}px`;
      s.el.style.top = `${(-v.y * 0.5 + 0.5) * window.innerHeight}px`;
      s.el.style.opacity = Math.min(1, s.t * 2);
    }
    // damage vignette
    this.damageFlash = Math.max(0, this.damageFlash - dt * 1.5);
    game.post.composite.uniforms.uDamage.value = Math.max(this.damageFlash, hp < 0.2 && !p.dead ? 0.35 + Math.sin(game.time * 4) * 0.1 : 0);
    this._tick(dt);
    this._drawRadar(dt);
    this._drawSpeedo(dt);
    // fps
    if (this.showFps) { this.fpsAcc += dt; this.fpsN++; if (this.fpsAcc > 0.5) { this.fpsEl.textContent = `${Math.round(this.fpsN / this.fpsAcc)} FPS · ${game.renderer.info.render.calls} draws`; this.fpsAcc = 0; this.fpsN = 0; } this.fpsEl.style.display = 'block'; }
    else this.fpsEl.style.display = 'none';
  }

  _tick(dt) {
    const T = this.timers;
    const dec = (k, el) => { if (T[k] > 0) { T[k] -= dt; if (T[k] <= 0) el.classList.remove('show'); } };
    dec('help', this.helpBox); dec('big', this.bigMsg); dec('subs', this.subtitles); dec('zone', this.zone); dec('veh', this.vehName); dec('radio', this.radioName); dec('money', this.moneyPop);
  }

  _updateRoute(force = false) {
    const game = this.game;
    this.routeTimer -= 1;
    const tgt = this.gpsTarget || this.waypoint;
    if (!tgt) { this.route = null; return; }
    const p = game.player.vehicle ? game.player.vehicle.pos : game.player.pos;
    if (Math.hypot(tgt.x - p.x, tgt.z - p.z) < 15) { if (tgt === this.waypoint) { this.setWaypoint(tgt.x, tgt.z); } this.route = null; return; }
    this.route = route(game.map.roads, p.x, p.z, tgt.x, tgt.z);
  }

  // Analog speedometer (mph) with gear + damage readout; airspeed / altitude / throttle for aircraft.
  _drawSpeedo(dt) {
    const p = this.game.player;
    const v = p.vehicle && p.seat === 0 ? p.vehicle : null;
    this.speedoWrap.classList.toggle('show', !!v && !p.dead);
    if (this._inVeh !== !!v) { this._inVeh = !!v; document.body.classList.toggle('in-vehicle', !!v); }
    if (!v) return;
    const ctx = this.sctx, S = this.speedo.width, C = S / 2, R = S / 2 - 12;
    const air = !!v.def.aircraft;
    const mph = Math.abs(v.forwardSpeed ?? v.speed) * 2.23694;
    const max = air ? (v.def.maxDial || 400) : v.def.tank ? 60 : 160;
    this.speedoNeedle += (Math.min(mph, max * 1.03) - this.speedoNeedle) * Math.min(1, dt * 10);
    const a0 = Math.PI * 0.75, a1 = Math.PI * 2.25;
    const ang = (val) => a0 + (a1 - a0) * clamp(val / max, 0, 1.03);
    ctx.clearRect(0, 0, S, S);
    // face
    const bg = ctx.createRadialGradient(C, C * 0.8, R * 0.1, C, C, R);
    bg.addColorStop(0, 'rgba(28,30,36,0.92)'); bg.addColorStop(1, 'rgba(6,7,9,0.92)');
    ctx.fillStyle = bg; ctx.beginPath(); ctx.arc(C, C, R, 0, Math.PI * 2); ctx.fill();
    ctx.lineWidth = 4; ctx.strokeStyle = '#000'; ctx.stroke();
    ctx.lineWidth = 1.5; ctx.strokeStyle = 'rgba(232,182,76,0.8)'; ctx.beginPath(); ctx.arc(C, C, R - 4, 0, Math.PI * 2); ctx.stroke();
    // red zone
    ctx.lineWidth = 7; ctx.strokeStyle = 'rgba(220,40,30,0.85)';
    ctx.beginPath(); ctx.arc(C, C, R - 13, ang(max * 0.85), ang(max)); ctx.stroke();
    // ticks + numbers
    const major = max <= 60 ? 10 : max <= 200 ? 20 : 50, minor = major / (max <= 60 ? 2 : 4);
    ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    ctx.font = 'bold 17px "Bebas Neue", Impact, sans-serif';
    for (let val = 0; val <= max + 0.01; val += minor) {
      const a = ang(val), big = Math.abs(val / major - Math.round(val / major)) < 1e-6;
      const r0 = R - (big ? 24 : 18), r1 = R - 9;
      ctx.strokeStyle = big ? '#f4f4f4' : 'rgba(244,244,244,0.55)'; ctx.lineWidth = big ? 3 : 1.5;
      ctx.beginPath(); ctx.moveTo(C + Math.cos(a) * r0, C + Math.sin(a) * r0); ctx.lineTo(C + Math.cos(a) * r1, C + Math.sin(a) * r1); ctx.stroke();
      if (big) { ctx.fillStyle = '#e9e9e9'; ctx.fillText(String(Math.round(val)), C + Math.cos(a) * (R - 38), C + Math.sin(a) * (R - 38)); }
    }
    // digital readout
    ctx.fillStyle = '#fff';
    ctx.font = '38px "Bebas Neue", Impact, sans-serif';
    ctx.fillText(String(Math.round(mph)).padStart(air ? 3 : 2, '0'), C, C + R * 0.42);
    ctx.font = '14px "Bebas Neue", Impact, sans-serif'; ctx.fillStyle = '#e8b64c';
    ctx.fillText(air ? 'MPH · AIRSPEED' : 'MPH', C, C + R * 0.62);
    // gear / altitude
    ctx.font = '22px "Bebas Neue", Impact, sans-serif';
    if (air) {
      const alt = Math.max(0, v.altitude ?? 0);
      ctx.fillStyle = '#9fe3ff'; ctx.fillText(`ALT ${Math.round(alt * 3.281)} FT`, C, C - R * 0.34);
      // throttle arc
      ctx.lineWidth = 5; ctx.strokeStyle = 'rgba(255,255,255,0.15)';
      ctx.beginPath(); ctx.arc(C, C, R * 0.28, Math.PI * 0.8, Math.PI * 2.2); ctx.stroke();
      ctx.strokeStyle = '#6cff8a';
      ctx.beginPath(); ctx.arc(C, C, R * 0.28, Math.PI * 0.8, Math.PI * 0.8 + Math.PI * 1.4 * clamp(v.throttle ?? 0, 0, 1)); ctx.stroke();
    } else {
      const sp = v.speed;
      const gear = sp < -0.5 ? 'R' : mph < 1 ? 'N' : String(Math.min(6, 1 + Math.floor(mph / (max / 6.2))));
      ctx.fillStyle = gear === 'R' ? '#ff6b5a' : '#fff';
      ctx.fillText(gear, C, C - R * 0.34);
    }
    // damage bar
    const hp = clamp(v.health / (v.maxHealth || 1000), 0, 1);
    ctx.fillStyle = 'rgba(255,255,255,0.12)'; ctx.fillRect(C - 34, C + R * 0.74, 68, 5);
    ctx.fillStyle = hp > 0.5 ? '#6cd46c' : hp > 0.25 ? '#ffb13b' : '#ff4a3a'; ctx.fillRect(C - 34, C + R * 0.74, 68 * hp, 5);
    // needle
    const na = ang(this.speedoNeedle);
    ctx.save(); ctx.translate(C, C); ctx.rotate(na);
    ctx.shadowColor = 'rgba(255,120,40,0.8)'; ctx.shadowBlur = 8;
    ctx.fillStyle = '#ff7a1a';
    ctx.beginPath(); ctx.moveTo(-10, -3); ctx.lineTo(R - 16, -1); ctx.lineTo(R - 16, 1); ctx.lineTo(-10, 3); ctx.closePath(); ctx.fill();
    ctx.restore();
    ctx.fillStyle = '#111'; ctx.strokeStyle = '#e8b64c'; ctx.lineWidth = 2;
    ctx.beginPath(); ctx.arc(C, C, 8, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
  }

  _drawRadar(dt) {
    const game = this.game;
    const ctx = this.rctx;
    const S = this.radar.width, R = S / 2 - 8, C = S / 2;
    const p = game.player;
    const pos = p.vehicle ? p.vehicle.pos : p.ragdolling ? p.ragdoll.center : p.pos;
    const speed = p.vehicle ? p.vehicle.speedAbs : 0;
    const targetRange = p.vehicle ? 150 + clamp(speed * 4, 0, 130) : 110;
    this.radarRange += (targetRange - this.radarRange) * Math.min(1, dt * 2);
    const scale = R / this.radarRange; // px per meter
    const fy = game.rig.forwardYaw;
    const alpha = Math.atan2(Math.cos(fy), Math.sin(fy));
    const theta = -Math.PI / 2 - alpha;
    this.routeTimer -= dt;
    if (this.routeTimer <= 0) { this.routeTimer = 1.0; this._updateRoute(); }
    ctx.clearRect(0, 0, S, S);
    ctx.save();
    ctx.beginPath(); ctx.arc(C, C, R, 0, Math.PI * 2); ctx.clip();
    ctx.fillStyle = '#16303f'; ctx.fillRect(0, 0, S, S);
    const img = this.mapImg;
    const [px, py] = img.toPx(pos.x, pos.z);
    ctx.translate(C, C);
    ctx.rotate(theta);
    ctx.scale(scale / img.sx, scale / img.sz);
    ctx.translate(-px, -py);
    ctx.globalAlpha = 0.92;
    drawMapLayers(ctx, img);
    ctx.globalAlpha = 1;
    if (this.route) {
      ctx.strokeStyle = '#d96cff'; ctx.lineWidth = 5 * img.sx / scale; ctx.lineJoin = 'round';
      ctx.beginPath();
      this.route.forEach(([x, z], i) => { const [mx, my] = img.toPx(x, z); if (i) ctx.lineTo(mx, my); else ctx.moveTo(mx, my); });
      ctx.stroke();
    }
    ctx.restore();
    // vignette ring
    const grd = ctx.createRadialGradient(C, C, R * 0.7, C, C, R);
    grd.addColorStop(0, 'rgba(0,0,0,0)'); grd.addColorStop(1, 'rgba(0,0,0,0.35)');
    ctx.fillStyle = grd; ctx.beginPath(); ctx.arc(C, C, R, 0, Math.PI * 2); ctx.fill();
    // blips
    const cs = Math.cos(theta), sn = Math.sin(theta);
    const toRadar = (x, z) => {
      const dx = (x - pos.x) * scale, dz = (z - pos.z) * scale;
      return [dx * cs - dz * sn, dx * sn + dz * cs];
    };
    const drawList = [];
    for (const b of game.blips) drawList.push(b);
    // cops & enemies
    if (game.police && game.police.level > 0) for (const c of game.police.cops) if (!c.dead) drawList.push({ x: c.vehicle ? c.vehicle.pos.x : c.pos.x, z: c.vehicle ? c.vehicle.pos.z : c.pos.z, color: Math.floor(game.time * 4) % 2 ? 0x3355ff : 0xff3333, icon: 'dot', small: true, noEdge: true });
    for (const b of drawList) {
      let [rx, ry] = toRadar(b.x, b.z);
      const d = Math.hypot(rx, ry);
      if (d > R - 7) { if (b.noEdge) continue; rx *= (R - 7) / d; ry *= (R - 7) / d; }
      drawBlip(ctx, C + rx, C + ry, b, b.small ? 0.8 : 1);
    }
    // north indicator
    const [nx, ny] = [Math.sin(theta) * 0, 0];
    const na = theta - Math.PI / 2;
    const nX = C + Math.cos(-Math.PI / 2 + theta) * (R - 2), nY = C + Math.sin(-Math.PI / 2 + theta) * (R - 2);
    ctx.fillStyle = '#fff'; ctx.strokeStyle = '#000'; ctx.lineWidth = 3;
    ctx.font = 'bold 15px "Bebas Neue", Impact, sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    ctx.beginPath(); ctx.arc(nX, nY, 9, 0, Math.PI * 2); ctx.fillStyle = 'rgba(0,0,0,0.7)'; ctx.fill();
    ctx.fillStyle = '#fff'; ctx.fillText('N', nX, nY + 1);
    // player arrow
    const heading = p.vehicle ? p.vehicle.yaw : p.yaw;
    drawPlayerArrow(ctx, C, C, heading - fy, 1, true);
    // ring
    ctx.strokeStyle = 'rgba(0,0,0,0.85)'; ctx.lineWidth = 5; ctx.beginPath(); ctx.arc(C, C, R, 0, Math.PI * 2); ctx.stroke();
    ctx.strokeStyle = 'rgba(255,255,255,0.25)'; ctx.lineWidth = 1.5; ctx.beginPath(); ctx.arc(C, C, R - 3, 0, Math.PI * 2); ctx.stroke();
  }
}

// ---------------------------------------------------------------------------------- drawing helpers
const SPEAKER_COLORS = {};
function speakerColor(name) {
  if (!SPEAKER_COLORS[name]) { let hsh = 0; for (const c of name) hsh = (hsh * 31 + c.charCodeAt(0)) >>> 0; SPEAKER_COLORS[name] = `hsl(${hsh % 360},70%,70%)`; }
  if (name === 'Dre') return '#8fd3ff';
  return SPEAKER_COLORS[name];
}

function fmtTime(s) { const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60); return `${h}h ${m}m`; }

export function drawPlayerArrow(ctx, x, y, relYaw, scale = 1, radar = false) {
  // radar: relYaw 0 = pointing up. Map: relYaw is world yaw with +z down on the map.
  ctx.save();
  ctx.translate(x, y);
  ctx.rotate(radar ? -relYaw : Math.PI - relYaw);
  ctx.scale(scale, scale);
  ctx.beginPath(); ctx.moveTo(0, -10); ctx.lineTo(7, 8); ctx.lineTo(0, 4); ctx.lineTo(-7, 8); ctx.closePath();
  ctx.fillStyle = '#fff'; ctx.strokeStyle = '#000'; ctx.lineWidth = 2;
  ctx.fill(); ctx.stroke();
  ctx.restore();
}

export function drawBlip(ctx, x, y, b, scale = 1) {
  const col = '#' + new THREE.Color(b.color ?? 0xffffff).getHexString();
  ctx.save();
  ctx.translate(x, y);
  ctx.scale(scale, scale);
  ctx.lineWidth = 2; ctx.strokeStyle = '#000';
  if (b.letter) {
    ctx.fillStyle = col; ctx.beginPath(); ctx.arc(0, 0, 9, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
    ctx.fillStyle = '#111'; ctx.font = 'bold 13px Arial'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillText(b.letter, 0, 1);
  } else if (b.icon && b.icon !== 'dot' && ICONS[b.icon]) {
    ctx.fillStyle = '#111'; ctx.beginPath(); ctx.roundRect ? ctx.roundRect(-9, -9, 18, 18, 4) : ctx.rect(-9, -9, 18, 18); ctx.fill();
    ctx.strokeStyle = col; ctx.lineWidth = 1.5; ctx.stroke();
    ctx.fillStyle = col; ctx.strokeStyle = col; ICONS[b.icon](ctx);
  } else {
    ctx.fillStyle = col;
    ctx.beginPath();
    const r = b.small ? 4 : 6;
    if (b.square) ctx.rect(-r, -r, r * 2, r * 2); else ctx.arc(0, 0, r, 0, Math.PI * 2);
    ctx.fill(); ctx.stroke();
  }
  ctx.restore();
}

const ICONS = {
  gun: (c) => { c.fillRect(-6, -3, 11, 3); c.fillRect(-5, 0, 3, 5); },
  spray: (c) => { c.fillRect(-3, -4, 6, 9); c.fillRect(-1, -7, 2, 3); },
  burger: (c) => { c.beginPath(); c.arc(0, 0, 5, Math.PI, 0); c.fill(); c.fillRect(-5, 1, 10, 3); },
  house: (c) => { c.beginPath(); c.moveTo(-6, 0); c.lineTo(0, -6); c.lineTo(6, 0); c.fill(); c.fillRect(-4, 0, 8, 5); },
  waypoint: (c) => { c.beginPath(); c.moveTo(0, 6); c.lineTo(-5, -2); c.arc(0, -2, 5, Math.PI, 0); c.closePath(); c.fill(); },
  money: (c) => { c.font = 'bold 12px Arial'; c.textAlign = 'center'; c.textBaseline = 'middle'; c.fillText('$', 0, 1); },
  health: (c) => { c.fillRect(-5, -1.5, 10, 3); c.fillRect(-1.5, -5, 3, 10); },
  armor: (c) => { c.fillRect(-4, -5, 8, 10); },
  weapon: (c) => { c.fillRect(-6, -2, 11, 3); c.fillRect(-5, 1, 3, 4); },
  car: (c) => { c.fillRect(-6, -2, 12, 5); c.fillRect(-3, -5, 6, 3); },
  target: (c) => { c.beginPath(); c.arc(0, 0, 5, 0, Math.PI * 2); c.stroke(); c.fillRect(-1, -1, 2, 2); },
  skull: (c) => { c.beginPath(); c.arc(0, -1, 5, 0, Math.PI * 2); c.fill(); c.fillRect(-3, 3, 6, 3); },
  flag: (c) => { c.fillRect(-4, -6, 2, 12); c.fillRect(-2, -6, 7, 5); },
};

export function drawWeaponIcon(c, id, size) {
  c.save();
  c.scale(size / 96, size / 96);
  c.fillStyle = '#f5f5f5'; c.strokeStyle = '#000'; c.lineWidth = 3;
  const P = (pts) => { c.beginPath(); pts.forEach(([x, y], i) => (i ? c.lineTo(x, y) : c.moveTo(x, y))); c.closePath(); c.fill(); c.stroke(); };
  switch (id) {
    case 'fist':
      c.beginPath(); c.roundRect ? c.roundRect(26, 30, 44, 34, 10) : c.rect(26, 30, 44, 34); c.fill(); c.stroke();
      for (let i = 0; i < 4; i++) { c.beginPath(); c.moveTo(30 + i * 10, 30); c.lineTo(30 + i * 10, 44); c.stroke(); }
      c.beginPath(); c.roundRect ? c.roundRect(20, 44, 16, 22, 6) : c.rect(20, 44, 16, 22); c.fill(); c.stroke();
      break;
    case 'knife': P([[14, 54], [44, 50], [82, 40], [44, 58], [14, 60]]); c.fillStyle = '#6b3e1f'; P([[10, 52], [30, 52], [30, 62], [10, 62]]); break;
    case 'bat': c.fillStyle = '#c69c6d'; P([[10, 60], [78, 30], [86, 36], [18, 66]]); break;
    case 'pistol': P([[18, 36], [76, 36], [76, 48], [40, 48], [36, 70], [22, 70], [24, 48], [18, 48]]); break;
    case 'smg': P([[12, 36], [80, 36], [80, 46], [52, 46], [52, 72], [44, 72], [44, 46], [36, 46], [32, 62], [22, 62], [24, 46], [12, 46]]); break;
    case 'shotgun': P([[4, 44], [90, 38], [90, 46], [40, 50], [34, 62], [20, 64], [22, 52], [4, 54]]); break;
    case 'rifle': P([[4, 46], [30, 40], [88, 38], [88, 44], [62, 46], [58, 66], [50, 66], [50, 48], [36, 50], [30, 60], [20, 60], [22, 52], [4, 56]]); break;
    case 'rpg': c.fillStyle = '#9aa77e'; P([[6, 42], [78, 38], [90, 42], [78, 46], [6, 50]]); c.fillStyle = '#f5f5f5'; P([[36, 50], [44, 50], [44, 64], [36, 64]]); break;
    case 'grenade': c.beginPath(); c.arc(48, 54, 18, 0, Math.PI * 2); c.fill(); c.stroke(); P([[42, 30], [54, 30], [54, 38], [42, 38]]); break;
    default: break;
  }
  c.restore();
}
