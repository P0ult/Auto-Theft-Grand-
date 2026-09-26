// Bootstrap: loading screen -> live title screen (city flyover) -> new game / continue.
import * as THREE from 'three';
import { Game } from './game/game.js';
import { PedManager } from './game/peds.js';
import { Traffic } from './game/traffic.js';
import { Police } from './game/police.js';
import { Combat } from './game/combat.js';
import { Effects } from './game/effects.js';
import { Audio } from './game/audio.js';
import { Pickups } from './game/pickups.js';
import { Military } from './game/military.js';
import { RailSystem } from './game/railsystem.js';
import { Missions } from './game/missions.js';
import { STORY } from './game/story.js';
import { SaveSystem } from './game/save.js';
import { Gameplay, FREE_ROAM_KIT } from './game/gameplay.js';
import { FreeRoam } from './game/freeroam.js';
import { TaxiSystem } from './game/taxi.js';
import { NetSystem } from './net/net.js';
import { HUD } from './ui/hud.js';

const params = new URLSearchParams(location.search);
const TIPS = [
  'Tap Space while turning to kick the tail out. Hold throttle to keep the drift alive.',
  'Losing the cops? Break line of sight and wait for the stars to stop flashing — or visit a Spray Shack.',
  'Headshots are instant kills on most enemies.',
  'Hidden packages are scattered across Los Soles. Find them all for a special reward at your safehouse.',
  'Walk into the green marker at your safehouse to save the game.',
  'Big Bun Burgers restores your health for $10.',
  'Press N in a car to cycle radio stations. Radio Los Soles plays West Coast classics.',
  'Lowriders have hydraulics — press G to bounce.',
  'Gun Barn in the Market District sells weapons, ammo and body armor.',
  'In free roam, press T to teleport to any town, station, airfield or landmark.',
  'Press H on the pavement to whistle for a cab. In the back, Space skips the trip.',
  'Driving a cab? Press J to pick up fares for cash.',
  'Catch the Sol Line at Union Station: ride it to Fern Creek and Dry Wells, or climb into the cab and drive.',
];

function el(tag, cls, parent, html) { const e = document.createElement(tag); if (cls) e.className = cls; if (html != null) e.innerHTML = html; if (parent) parent.appendChild(e); return e; }

async function boot() {
  document.body.classList.add('menu');
  const loading = el('div', 'loading', document.body);
  el('div', 'art', loading, 'Auto Theft Grand');
  const bar = el('div', 'bar', loading, '<i></i>');
  const msg = el('div', 'msg', loading, 'Starting up');
  el('div', 'tip', loading, TIPS[Math.floor(Math.random() * TIPS.length)]);
  const setP = (p, m) => { bar.firstChild.style.width = `${Math.round(p * 100)}%`; if (m) msg.textContent = m.toUpperCase(); };

  const settings = SaveSystem.loadSettings();
  if (params.get('q')) settings.quality = params.get('q');
  const game = new Game(document.getElementById('game'), { settings });
  window.__game = game; window.THREE = THREE;
  try {
    await game.init(setP);
  } catch (e) {
    console.error(e);
    msg.innerHTML = `Could not start WebGL: ${e.message}. Try a recent Chrome, Edge or Firefox with hardware acceleration enabled.`;
    return;
  }
  setP(0.8, 'Hiring pedestrians');
  await tick();
  game.addSystem('effects', new Effects(game));
  game.addSystem('combat', new Combat(game));
  game.addSystem('peds', new PedManager(game));
  game.addSystem('traffic', new Traffic(game));
  game.addSystem('police', new Police(game));
  game.addSystem('pickups', new Pickups(game));
  game.addSystem('military', new Military(game));
  game.addSystem('rail', new RailSystem(game));
  setP(0.88, 'Writing the story');
  await tick();
  game.addSystem('missions', new Missions(game, STORY));
  game.addSystem('audio', new Audio(game));
  game.save = new SaveSystem(game);
  game.addSystem('gameplay', new Gameplay(game));
  game.addSystem('freeroam', new FreeRoam(game));
  game.addSystem('taxi', new TaxiSystem(game));
  game.hud = new HUD(game);
  game.net = new NetSystem(game);
  game.net.detect();
  game.pickups.refreshPackages();
  setP(0.93, 'Compiling shaders');
  game.input.enabled = false;
  await warmup(game);
  game.env.setTime(18.35);
  game.env.timeScale = 0.25;
  setP(1, 'Ready');
  await tick();
  loading.style.transition = 'opacity 0.8s';
  loading.style.opacity = 0;
  setTimeout(() => loading.remove(), 900);
  if (params.has('manual')) {
    // test mode: advance the simulation deterministically from the console / automation
    window.__step = (sec, dt = 1 / 30, render = true) => {
      const n = Math.max(1, Math.round(sec / dt));
      for (let i = 0; i < n; i++) {
        game.input.pollGamepad();
        if (!game.paused) game.update(dt * game.timeScale, dt);
        game.net?.tick(dt);
        game.hud.update(dt);
        game.input.endFrame();
      }
      if (render) game.render(dt);
    };
    window.__press = (code) => { game.input.keys.add(code); game.input.pressed.add(code); };
    window.__release = (code) => { game.input.keys.delete(code); };
  } else game.start();
  window.__ready = true;

  if (params.get('autostart') === 'new') return startGame(game, false, null);
  if (params.get('autostart') === 'free') return startGame(game, false, null, true);
  if (params.get('autostart') === 'multi') { startGame(game, false, null, true); if (params.get('room') != null) game.net.connect(params.get('room')); return; }
  showTitle(game);
}

function showTitle(game) {
  const t = el('div', 'title-screen transparent', document.body);
  const logo = el('div', 'title-logo', t);
  el('div', 'l1', logo, 'AUTO THEFT');
  el('div', 'l2', logo, 'GRAND');
  el('div', 'l3', logo, 'Los Soles');
  const menu = el('div', 'title-menu', t);
  const hasSave = game.save.hasSave();
  const info = game.save.info();
  const bCont = el('button', '', menu, 'Continue');
  if (!hasSave) bCont.disabled = true;
  else if (info) bCont.title = `${info.progress} missions complete`;
  const bNew = el('button', '', menu, 'New Game');
  const bFree = el('button', '', menu, 'Free Roam');
  const bMulti = el('button', '', menu, 'Multiplayer');
  const bSet = el('button', '', menu, 'Settings');
  const bCtl = el('button', '', menu, 'Controls');
  el('div', 'title-foot', t, 'WASD + Mouse · Gamepad supported · Best in Chrome/Edge with hardware acceleration · An original parody inspired by open-world crime classics');
  const go = (cont, free = false) => {
    game.audio.init();
    t.style.transition = 'opacity 0.6s';
    t.style.opacity = 0;
    setTimeout(() => t.remove(), 700);
    startGame(game, cont, null, free);
  };
  bCont.onclick = () => go(true);
  let armed = false;
  bNew.onclick = () => {
    if (hasSave && !armed) { armed = true; bNew.textContent = 'New Game? Click again'; setTimeout(() => { armed = false; bNew.textContent = 'New Game'; }, 3500); return; }
    go(false);
  };
  bFree.onclick = () => go(false, true);
  bMulti.onclick = () => { go(false, true); setTimeout(() => game.hud.openPause('online'), 250); };
  bSet.onclick = () => { game.audio.init(); game.hud.openPause('settings'); game.paused = false; };
  bCtl.onclick = () => { game.hud.openPause('controls'); game.paused = false; };
  (hasSave ? bCont : bNew).focus();
}

function startGame(game, cont, _, free = false) {
  const g = game;
  g.audio.init();
  document.body.classList.remove('menu');
  if (g.hud.menuOpen) g.hud.closeOverlay();
  const p = g.player;
  const L = g.map.landmarks;
  p.root.visible = true;
  g.rig.clearCinematic();
  g.env.timeScale = 1;
  g.gameplay.state = 'playing';
  g.input.enabled = true;
  g.input.wantLock = true;
  g.input.requestLock();
  const home = L.home;
  if (cont && g.save.load()) {
    p.setPosition(home.x, undefined, home.z);
    p.setYaw(0);
    g.rig.yaw = Math.PI;
    g.missions.refreshContacts();
    g.hud.help('Welcome back to Los Soles.', 4);
  } else if (free) {
    p.setPosition(home.x, undefined, home.z);
    for (const [w, a] of FREE_ROAM_KIT) p.giveWeapon(w, a);
    p.equip('pistol');
    g.freeRoam = true;
    g.env.setTime(17.5);
    g.missions.completed = new Set(STORY.missions.map((m) => m.id));
    g.freeroam.update();
    g.hud.help('Free roam: every weapon, unlimited cash and ammo. Press <b>T</b> to teleport anywhere. Cause some chaos!', 8);
  } else {
    p.money = 250;
    const first = STORY.missions.find((m) => m.auto && !(m.requires || []).length);
    g.missions.start(first);
  }
  g.traffic.populate(Math.floor(g.traffic.maxCars * 0.7));
  g.peds.populate(Math.floor(g.peds.maxPeds * 0.6));
  // pointer lock handling
  const clickLayer = el('div', 'click-to-play', document.body, '<div>CLICK TO PLAY</div>');
  clickLayer.onclick = () => { g.input.requestLock(); };
  g.input.onPointerLockChange = (locked) => {
    clickLayer.classList.toggle('show', !locked && !g.input.lockFailed && !g.hud.menuOpen && g.gameplay.state === 'playing');
    if (!locked && !g.hud.menuOpen && g.gameplay.state === 'playing' && !g.cutscene) { g.hud.openPause('map'); g.hud._autoPauseT = performance.now(); }
  };
  setInterval(() => { clickLayer.classList.toggle('show', !g.input.locked && !g.input.lockFailed && !g.hud.menuOpen && (g.gameplay.state === 'playing' || g.gameplay.state === 'dead')); }, 500);
}

function tick() { return new Promise((r) => setTimeout(r, 0)); }

// Spawn one of everything in front of the camera, precompile all shader programs and render a couple
// of frames (for shadow-map variants), then clean up. Avoids hitches when things first appear.
async function warmup(game) {
  const cam = game.camera;
  const home = game.map.landmarks.home;
  const base = new THREE.Vector3(home.x, 0, home.z + 30);
  cam.position.set(base.x, 6, base.z - 18);
  cam.lookAt(base.x, 1, base.z);
  cam.updateMatrixWorld();
  const temp = [];
  const ids = Object.keys(await import('./entities/vehicledefs.js').then((m) => m.VEHICLES));
  ids.forEach((id, i) => { const v = game.vehicles.spawn(id, base.x - 20 + i * 4, base.z + (i % 2) * 6, 0); v.explodedPreview = false; temp.push(['v', v]); });
  const ped = game.peds.spawnPed(base.x, base.z - 4, {});
  ped.giveWeapon('rifle', 10); ped.equip('rifle');
  const cop = game.police.spawnCop(base.x + 2, base.z - 4);
  temp.push(['p', ped], ['p', cop]);
  const mk = game.pickups.addMarker(base.x - 3, base.z - 6, { radius: 1.2 });
  const pk = ['money', 'health', 'armor', 'package'].map((k, i) => game.pickups.spawn(k, base.x - 6 + i, base.z - 8, { life: 0.5 }));
  game.pickups.spawn('weapon', base.x + 4, base.z - 8, { weapon: 'smg', ammo: 1, life: 0.5 });
  game.effects.explosion(new THREE.Vector3(base.x, 20, base.z + 8), 3);
  game.effects.blood(new THREE.Vector3(base.x, 1, base.z - 2), new THREE.Vector3(0, 1, 0), 4);
  game.effects.tracers.add(new THREE.Vector3(base.x, 1, base.z), new THREE.Vector3(base.x + 5, 1, base.z + 5));
  game.effects.impact(new THREE.Vector3(base.x, 0.2, base.z), new THREE.Vector3(0, 1, 0));
  for (const w of ['pistol', 'smg', 'shotgun', 'rpg', 'knife', 'bat', 'grenade']) { const m = (await import('./game/weapondefs.js')).createWeaponMesh(w); if (m) { m.position.set(base.x + Math.random() * 4, 1, base.z - 5); game.scene.add(m); temp.push(['m', m]); } }
  game.env.update(0, base);
  try { if (game.renderer.compileAsync) await game.renderer.compileAsync(game.scene, cam); else game.renderer.compile(game.scene, cam); } catch (e) { console.warn(e); }
  game.render(0.016);
  await tick();
  // wrecked car + night headlight variants
  { const vm = await import('./entities/vehiclemodels.js'); temp[1][1].model.body.material = vm.vehicleMaterials().burnt; }
  game.env.setTime(22);
  game.render(0.016);
  await tick();
  game.env.setTime(12);
  game.render(0.016);
  for (const [k, o] of temp) {
    if (k === 'v') { for (const oc of o.occupants) if (oc) game.peds.remove(oc); game.vehicles.remove(o); }
    else if (k === 'p') game.peds.remove(o);
    else o.parent?.remove(o);
  }
  game.pickups.removeMarker(mk);
  game.effects.emitters.length = 0;
  game.police.reset();
}

boot();
