import * as THREE from 'three';
import { Game } from './game/game.js';
import { Character } from './entities/character.js';
import { randomAppearance } from './entities/humanoid.js';

const q = new URLSearchParams(location.search);
const game = new Game(document.getElementById('game'));
await game.init((p, msg) => console.log('load', p, msg));
const home = game.map.landmarks.home;
game.player.setPosition(home.x, undefined, home.z);
game.player.setYaw(0);
game.env.setTime(parseFloat(q.get('t') || '10'));
game.env.timeScale = 0;
game.rig.yaw = parseFloat(q.get('cy') || '2.6');
game.rig.pitch = -0.1;
const v = game.vehicles.spawn(q.get('car') || 'brawler', home.x + 4, home.z + 6, Math.PI / 2);
game.testCar = v;
const peds = [];
for (let i = 0; i < 4; i++) {
  const c = new Character(game, randomAppearance());
  c.setPosition(home.x - 3 + i * 1.5, undefined, home.z + 2);
  c.setYaw(Math.PI * 0.8);
  peds.push(c);
}
game.peds = { list: peds, update(dt) { for (const p of peds) p.update(dt); } };
game.addSystem('peds', game.peds);
game.start();
window.__game = game; window.THREE = THREE;
window.__ready = true;
