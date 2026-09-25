// Weapon definitions + procedural weapon models.
import * as THREE from 'three';
import { GeoBuilder, mat4 } from '../world/geom.js';
import { std } from '../render/materials.js';

export const WEAPONS = {
  fist: { id: 'fist', name: 'Fists', slot: 0, type: 'melee', hold: 'none', damage: 9, range: 1.25, icon: 'fist' },
  knife: { id: 'knife', name: 'Knife', slot: 1, type: 'melee', hold: 'knife', damage: 40, range: 1.45, price: 150, icon: 'knife' },
  bat: { id: 'bat', name: 'Baseball Bat', slot: 1, type: 'melee', hold: 'bat', damage: 30, range: 1.9, price: 100, icon: 'bat' },
  pistol: { id: 'pistol', name: '9mm Pistol', slot: 2, type: 'gun', hold: 'pistol', damage: 26, rate: 0.22, clip: 17, spread: 0.012, range: 140, auto: false, price: 400, ammoPrice: 60, ammoPack: 34, pellets: 1, recoil: 0.9, sound: 'pistol', icon: 'pistol', shake: 0.25 },
  smg: { id: 'smg', name: 'Micro SMG', slot: 3, type: 'gun', hold: 'smg', damage: 17, rate: 0.07, clip: 32, spread: 0.035, range: 110, auto: true, price: 1200, ammoPrice: 120, ammoPack: 96, pellets: 1, recoil: 0.35, sound: 'smg', icon: 'smg', shake: 0.15 },
  shotgun: { id: 'shotgun', name: 'Pump Shotgun', slot: 4, type: 'gun', hold: 'shotgun', damage: 14, rate: 0.85, clip: 7, spread: 0.075, range: 60, auto: false, price: 1500, ammoPrice: 150, ammoPack: 14, pellets: 9, recoil: 1.2, sound: 'shotgun', icon: 'shotgun', shake: 0.6 },
  rifle: { id: 'rifle', name: 'Assault Rifle', slot: 5, type: 'gun', hold: 'rifle', damage: 32, rate: 0.105, clip: 30, spread: 0.016, range: 220, auto: true, price: 3500, ammoPrice: 200, ammoPack: 90, pellets: 1, recoil: 0.45, sound: 'rifle', icon: 'rifle', shake: 0.25 },
  rpg: { id: 'rpg', name: 'Rocket Launcher', slot: 6, type: 'launcher', hold: 'rpg', damage: 200, rate: 1.6, clip: 1, spread: 0.004, range: 300, auto: false, price: 8000, ammoPrice: 800, ammoPack: 4, pellets: 1, recoil: 1.4, sound: 'rpg', icon: 'rpg', shake: 1 },
  grenade: { id: 'grenade', name: 'Grenades', slot: 7, type: 'thrown', hold: 'none', damage: 150, rate: 1.0, clip: 1, price: 600, ammoPrice: 300, ammoPack: 4, icon: 'grenade' },
};
export const WEAPON_ORDER = ['fist', 'knife', 'bat', 'pistol', 'smg', 'shotgun', 'rifle', 'rpg', 'grenade'];

let mat = null;
function weaponMaterial() {
  if (!mat) mat = std({ vertexColors: true, roughness: 0.45, metalness: 0.65 }, { key: 'weapon' });
  return mat;
}

const cache = {};
// Model space: barrel along +Z, top along +Y, origin at the grip.
export function weaponGeometry(id) {
  if (cache[id] !== undefined) return cache[id];
  const gb = new GeoBuilder({ position: 3, normal: 3, uv: 2, color: 3 });
  const c = (hex) => { const cc = new THREE.Color(hex); gb.set('color', cc.r, cc.g, cc.b); };
  const box = (w, h, d, x, y, z, rx = 0) => gb.addGeometry(new THREE.BoxGeometry(w, h, d), mat4(x, y, z, rx));
  const cyl = (r, l, x, y, z, s = 8) => gb.addGeometry(new THREE.CylinderGeometry(r, r, l, s), mat4(x, y, z, Math.PI / 2));
  const black = 0x1a1a1a, gun = 0x2a2c30, wood = 0x6b3e1f;
  switch (id) {
    case 'knife':
      c(0x222222); box(0.025, 0.03, 0.11, 0, 0, 0);
      c(0xd8d8d8); box(0.006, 0.028, 0.18, 0, 0.005, 0.14);
      break;
    case 'bat':
      c(0xb5874e);
      gb.addGeometry(new THREE.CylinderGeometry(0.035, 0.018, 0.85, 10), mat4(0, 0, 0.36, Math.PI / 2));
      c(0x222222); cyl(0.02, 0.12, 0, 0, -0.02);
      break;
    case 'pistol':
      c(gun); box(0.03, 0.035, 0.19, 0, 0.055, 0.05);
      c(black); box(0.028, 0.11, 0.045, 0, 0, -0.005, -0.25);
      c(black); box(0.01, 0.03, 0.04, 0, 0.005, 0.03);
      break;
    case 'smg':
      c(gun); box(0.04, 0.06, 0.26, 0, 0.05, 0.06);
      c(black); box(0.03, 0.1, 0.04, 0, -0.02, 0.0, -0.1);
      c(black); box(0.025, 0.14, 0.03, 0, -0.03, 0.09);
      c(gun); cyl(0.012, 0.08, 0, 0.06, 0.22);
      break;
    case 'shotgun':
      c(gun); cyl(0.016, 0.62, 0, 0.07, 0.34);
      c(gun); cyl(0.014, 0.5, 0, 0.045, 0.3);
      c(wood); box(0.04, 0.05, 0.14, 0, 0.045, 0.34);
      c(gun); box(0.045, 0.06, 0.16, 0, 0.055, 0.05);
      c(wood); box(0.035, 0.09, 0.05, 0, -0.01, 0.0, -0.3);
      c(wood); box(0.04, 0.08, 0.3, 0, 0.03, -0.18, 0.12);
      break;
    case 'rifle':
      c(gun); box(0.045, 0.07, 0.34, 0, 0.06, 0.1);
      c(gun); cyl(0.013, 0.34, 0, 0.075, 0.43);
      c(wood); box(0.05, 0.055, 0.2, 0, 0.055, 0.32);
      c(black); box(0.03, 0.17, 0.06, 0, -0.04, 0.16, 0.35);
      c(black); box(0.035, 0.1, 0.04, 0, -0.01, 0.0, -0.2);
      c(wood); box(0.04, 0.09, 0.28, 0, 0.04, -0.2, 0.1);
      c(black); box(0.01, 0.04, 0.01, 0, 0.12, 0.5);
      break;
    case 'rpg':
      c(0x3e4a2f); cyl(0.055, 1.05, 0, 0.1, 0.15, 12);
      c(black); box(0.03, 0.12, 0.05, 0, 0.0, 0.0, -0.2);
      c(black); box(0.03, 0.1, 0.04, 0, 0.02, 0.3);
      c(0x5a6b45); gb.addGeometry(new THREE.ConeGeometry(0.07, 0.22, 10), mat4(0, 0.1, 0.78, Math.PI / 2));
      break;
    case 'grenade':
      c(0x3b4a2a); gb.addGeometry(new THREE.SphereGeometry(0.045, 10, 8), mat4(0, 0, 0));
      c(0x888888); box(0.015, 0.04, 0.02, 0, 0.05, 0);
      break;
    default:
      cache[id] = null;
      return null;
  }
  cache[id] = gb.build();
  return cache[id];
}

export function createWeaponMesh(id) {
  const g = weaponGeometry(id);
  if (!g) return null;
  const m = new THREE.Mesh(g, weaponMaterial());
  m.castShadow = true;
  return m;
}

// muzzle offsets in model space
export const MUZZLE = {
  pistol: [0, 0.06, 0.16], smg: [0, 0.06, 0.27], shotgun: [0, 0.07, 0.66], rifle: [0, 0.075, 0.61], rpg: [0, 0.1, 0.75],
};
