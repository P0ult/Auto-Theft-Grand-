// Pickups (cash, weapons, health, armor, hidden packages), glowing 3D markers, and world services:
// Gun Barn shop, Spray Shack, Big Bun Burgers, safehouse save point.
import * as THREE from 'three';
import { WEAPONS, createWeaponMesh } from './weapondefs.js';
import { rand, randInt, dist2, clamp, RNG } from '../core/utils.js';
import { CITY } from '../world/citymap.js';

const markerMat = (color) => new THREE.ShaderMaterial({
  uniforms: { uColor: { value: new THREE.Color(color) }, uTime: { value: 0 } },
  vertexShader: 'varying vec2 vUv; void main(){ vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.0); }',
  fragmentShader: `uniform vec3 uColor; uniform float uTime; varying vec2 vUv;
    void main(){ float a = pow(1.0 - vUv.y, 1.6) * (0.55 + 0.25 * sin(uTime * 3.0 + vUv.y * 10.0)); gl_FragColor = vec4(uColor * 1.6 * a, 1.0); }`,
  transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, side: THREE.DoubleSide,
});

export class Marker {
  constructor(game, x, z, opts = {}) {
    this.game = game;
    this.pos = new THREE.Vector3(x, opts.y ?? game.map.groundHeight(x, z), z);
    this.radius = opts.radius ?? 1.5;
    this.color = opts.color ?? 0xffd23f;
    this.group = new THREE.Group();
    this.group.position.copy(this.pos);
    const h = opts.height ?? (this.radius > 3 ? 3 : 1.8);
    const cyl = new THREE.Mesh(new THREE.CylinderGeometry(this.radius, this.radius, h, 32, 1, true), markerMat(this.color));
    cyl.position.y = h / 2;
    cyl.renderOrder = 8;
    this.cyl = cyl;
    this.group.add(cyl);
    if (opts.arrow !== false) {
      const arrow = new THREE.Mesh(new THREE.ConeGeometry(0.35, 0.7, 4), new THREE.MeshBasicMaterial({ color: new THREE.Color(this.color).multiplyScalar(2.5) }));
      arrow.rotation.x = Math.PI;
      arrow.position.y = h + 1.2;
      this.arrow = arrow;
      this.group.add(arrow);
    }
    game.scene.add(this.group);
    this.blip = opts.blip !== false ? { x, z, color: this.color, icon: opts.icon || 'dot', label: opts.label, marker: true } : null;
    if (this.blip) game.blips.add(this.blip);
    this.onEnter = opts.onEnter || null;
    this.vehicleOnly = !!opts.vehicleOnly;
    this.footOnly = !!opts.footOnly;
    this.inside = false;
    this.removed = false;
  }
  setPos(x, z) {
    this.pos.set(x, this.game.map.groundHeight(x, z), z);
    this.group.position.copy(this.pos);
    if (this.blip) { this.blip.x = x; this.blip.z = z; }
  }
  update(dt, t) {
    this.cyl.material.uniforms.uTime.value = t;
    if (this.arrow) { this.arrow.position.y = (this.cyl.geometry.parameters.height) + 1.2 + Math.sin(t * 3) * 0.2; this.arrow.rotation.y += dt * 2; }
    const pl = this.game.player;
    const p = pl.vehicle ? pl.vehicle.pos : pl.pos;
    const ok = (!this.vehicleOnly || pl.vehicle) && (!this.footOnly || !pl.vehicle) && !pl.dead;
    const inside = ok && dist2(p.x, p.z, this.pos.x, this.pos.z) < (this.radius + (pl.vehicle ? 0.8 : 0)) ** 2 && Math.abs(p.y - this.pos.y) < 4;
    if (inside && !this.inside) { this.inside = true; this.onEnter?.(this); }
    else if (!inside) this.inside = false;
    // hide when very far
    this.group.visible = dist2(p.x, p.z, this.pos.x, this.pos.z) < 400 * 400;
  }
  remove() {
    if (this.removed) return;
    this.removed = true;
    this.group.parent?.remove(this.group);
    if (this.blip) this.game.blips.delete(this.blip);
  }
}

class Pickup {
  constructor(game, kind, pos, data = {}) {
    this.game = game;
    this.kind = kind;
    this.data = data;
    this.pos = pos.clone();
    this.pos.y = game.map.groundHeight(pos.x, pos.z);
    const g = new THREE.Group();
    g.position.copy(this.pos);
    let mesh;
    const mat = (c, e = 0.6) => new THREE.MeshStandardMaterial({ color: c, emissive: new THREE.Color(c).multiplyScalar(e), roughness: 0.4, metalness: 0.2 });
    if (kind === 'money') {
      mesh = new THREE.Group();
      for (let i = 0; i < 3; i++) { const b = new THREE.Mesh(new THREE.BoxGeometry(0.28, 0.05, 0.14), mat(0x3a9d23, 0.5)); b.position.y = i * 0.055; b.rotation.y = i * 0.3; mesh.add(b); }
    } else if (kind === 'health') {
      mesh = new THREE.Group();
      mesh.add(new THREE.Mesh(new THREE.BoxGeometry(0.5, 0.16, 0.16), mat(0xff2a2a, 1)));
      mesh.add(new THREE.Mesh(new THREE.BoxGeometry(0.16, 0.5, 0.16), mat(0xff2a2a, 1)));
    } else if (kind === 'armor') {
      mesh = new THREE.Mesh(new THREE.BoxGeometry(0.45, 0.55, 0.18), mat(0x2a6cff, 0.8));
    } else if (kind === 'package') {
      mesh = new THREE.Mesh(new THREE.BoxGeometry(0.4, 0.4, 0.4), mat(0xe8c170, 0.6));
    } else if (kind === 'weapon') {
      mesh = createWeaponMesh(data.weapon) || new THREE.Mesh(new THREE.BoxGeometry(0.3, 0.1, 0.1), mat(0x777777));
      mesh.scale.setScalar(1.6);
      mesh.rotation.x = 0;
    }
    mesh.position.y = 0.7;
    g.add(mesh);
    const glowCol = { money: 0x44ff44, health: 0xff4444, armor: 0x4488ff, weapon: 0xffcc44, package: 0xffddaa }[kind];
    const glow = new THREE.Mesh(new THREE.CircleGeometry(0.6, 20), new THREE.MeshBasicMaterial({ color: new THREE.Color(glowCol).multiplyScalar(1.5), transparent: true, opacity: 0.5, blending: THREE.AdditiveBlending, depthWrite: false }));
    glow.rotation.x = -Math.PI / 2; glow.position.y = 0.05;
    g.add(glow);
    this.mesh = mesh;
    this.group = g;
    game.scene.add(g);
    this.t = Math.random() * 6;
    this.life = data.life ?? Infinity;
    this.respawn = data.respawn ?? 0;
    this.hiddenUntil = 0;
    if (data.blip) { this.blip = { x: pos.x, z: pos.z, color: glowCol, icon: kind, small: true }; game.blips.add(this.blip); }
  }
  update(dt) {
    this.t += dt;
    this.mesh.rotation.y += dt * 2;
    this.mesh.position.y = 0.7 + Math.sin(this.t * 2.5) * 0.1;
    this.life -= dt;
  }
  remove() { this.group.parent?.remove(this.group); if (this.blip) this.game.blips.delete(this.blip); }
}

export class Pickups {
  constructor(game) {
    this.game = game;
    this.list = [];
    this.markers = new Set();
    this.services = [];
    this.collectedPackages = new Set();
    this.t = 0;
    this._setupWorld();
  }

  addMarker(x, z, opts) { const m = new Marker(this.game, x, z, opts); this.markers.add(m); return m; }
  removeMarker(m) { if (!m) return; m.remove(); this.markers.delete(m); }

  dropMoney(pos, amount) { if (amount > 0) this.list.push(new Pickup(this.game, 'money', pos.clone().add(new THREE.Vector3(rand(-0.4, 0.4), 0, rand(-0.4, 0.4))), { amount, life: 40 })); }
  dropWeapon(pos, weapon, ammo) { if (!WEAPONS[weapon] || weapon === 'fist') return; this.list.push(new Pickup(this.game, 'weapon', pos.clone().add(new THREE.Vector3(rand(-0.6, 0.6), 0, rand(-0.6, 0.6))), { weapon, ammo, life: 45 })); }
  spawn(kind, x, z, data = {}) { const p = new Pickup(this.game, kind, new THREE.Vector3(x, 0, z), data); this.list.push(p); return p; }

  _setupWorld() {
    const game = this.game;
    const L = game.map.landmarks;
    // world pickups (respawning)
    const fixed = [
      ['health', L.hospital.x - 6, L.hospital.z - 3], ['armor', L.police.x + 12, L.police.z + 1],
      ['health', L.court.x + 8, L.court.z + 12], ['weapon', L.projects.x, L.projects.z + 4, { weapon: 'bat', ammo: 1 }],
      ['weapon', L.plaza.x + 20, L.plaza.z, { weapon: 'pistol', ammo: 34 }], ['armor', L.warehouse.x, L.warehouse.z - 30],
      ['weapon', L.beach.x + 30, L.beach.z + 12, { weapon: 'knife', ammo: 1 }], ['health', L.burger.x, L.burger.z - 2],
      ['weapon', L.docksQuay.x - 40, L.docksQuay.z + 80, { weapon: 'shotgun', ammo: 14 }], ['weapon', L.mansion.x + 30, L.mansion.z - 40, { weapon: 'smg', ammo: 64 }],
    ];
    for (const [kind, x, z, d] of fixed) this.list.push(new Pickup(game, kind, new THREE.Vector3(x, 0, z), { ...(d || {}), respawn: 90 }));
    // hidden packages
    const rng = new RNG(2024);
    this.packageSpots = [];
    const blocks = game.map.blocks;
    for (let i = 0; i < 30; i++) {
      const b = blocks[rng.int(0, blocks.length - 1)];
      const x = rng.range(b.ix0 + 1, b.ix1 - 1), z = rng.range(b.iz0 + 1, b.iz1 - 1);
      const blocked = game.collision.resolveCircle(x, z, 0.8, 0.2, 1).hit;
      if (blocked) { i--; continue; }
      this.packageSpots.push({ id: i, x, z });
    }
    this.packages = [];
    // services
    const gs = L.gunshop;
    this.services.push(this.addMarker(gs.x, gs.z, { color: 0xff5a36, radius: 1.2, icon: 'gun', label: 'Gun Barn', footOnly: true, onEnter: () => game.hud?.openShop() }));
    for (const key of ['spray', 'spray2']) {
      const s = L[key];
      this.services.push(this.addMarker(s.x, s.z, { color: 0x6df0ff, radius: 3.5, height: 2.5, icon: 'spray', label: 'Spray Shack', vehicleOnly: true, arrow: false, onEnter: () => this.spray() }));
    }
    const bb = L.burger;
    this.services.push(this.addMarker(bb.x, bb.z, { color: 0xffd166, radius: 1.1, icon: 'burger', label: 'Big Bun Burgers', footOnly: true, onEnter: () => this.eat() }));
    const hm = L.home;
    this.saveMarker = this.addMarker(hm.door.x, hm.door.z + 1.2, { color: 0x6cff6c, radius: 1.0, icon: 'house', label: 'Safehouse (save)', footOnly: true, onEnter: () => game.hud?.promptSave() });
    this.services.push(this.saveMarker);
  }

  refreshPackages() {
    for (const p of this.packages) p.remove();
    this.packages = [];
    for (const s of this.packageSpots) {
      if (this.collectedPackages.has(s.id)) continue;
      const p = new Pickup(this.game, 'package', new THREE.Vector3(s.x, 0, s.z), { packageId: s.id });
      this.packages.push(p);
      this.list.push(p);
    }
  }

  spray() {
    const game = this.game;
    const v = game.player.vehicle;
    if (!v || v.isWrecked) return;
    if (game.player.money < 100) { game.hud?.help('The Spray Shack costs $100. Come back when you have the cash.'); return; }
    if (game.missions?.active?.noSpray) { game.hud?.help('Not now — you\'re on a job.'); return; }
    game.player.money -= 100;
    game.hud?.fade(0.6, () => {
      const c = v.def.colors[randInt(0, v.def.colors.length - 1)] ?? 0x888888;
      v.model.bodyMat.color.set(new THREE.Color().setHSL(Math.random(), rand(0.4, 0.8), rand(0.25, 0.55)));
      v.health = 1000; v.onFire = false; v.burnTime = 0;
      game.police.clear();
      game.audio?.play('cash');
      game.hud?.help('New paint job, fixed up and the cops won\'t recognise you. $100.');
      game.stats.sprays++;
    });
  }

  eat() {
    const game = this.game;
    const p = game.player;
    if (p.money < 10) { game.hud?.help('A Big Bun Combo is $10.'); return; }
    if (p.health >= p.maxHealth) { game.hud?.help('You\'re not hungry right now.'); return; }
    p.money -= 10;
    p.health = p.maxHealth;
    game.audio?.play('pickup');
    game.hud?.help('Big Bun Double Stack. Health restored. $10.');
  }

  update(dt) {
    const game = this.game;
    this.t += dt;
    for (const m of this.markers) m.update(dt, this.t);
    const pl = game.player;
    const pp = pl.vehicle ? pl.vehicle.pos : pl.pos;
    for (let i = this.list.length - 1; i >= 0; i--) {
      const p = this.list[i];
      if (p.hiddenUntil > this.t) continue;
      if (!p.group.visible && p.hiddenUntil) { p.group.visible = true; }
      p.update(dt);
      if (p.life <= 0) { p.remove(); this.list.splice(i, 1); continue; }
      if (pl.dead) continue;
      const r = pl.vehicle ? 2.2 : 1.1;
      if (dist2(p.pos.x, p.pos.z, pp.x, pp.z) < r * r && Math.abs(pp.y - p.pos.y) < 2.5) {
        if (this.collect(p)) {
          if (p.respawn) { p.hiddenUntil = this.t + p.respawn; p.group.visible = false; }
          else { p.remove(); this.list.splice(i, 1); }
        }
      }
    }
  }

  collect(p) {
    const game = this.game;
    const pl = game.player;
    switch (p.kind) {
      case 'money': pl.money += p.data.amount; game.audio?.play('cash'); game.hud?.moneyFlash(p.data.amount); return true;
      case 'health': if (pl.health >= pl.maxHealth) return false; pl.health = pl.maxHealth; game.audio?.play('pickup'); return true;
      case 'armor': if (pl.armor >= 100) return false; pl.armor = 100; game.audio?.play('pickup'); return true;
      case 'weapon': {
        if (pl.vehicle) return false;
        const def = WEAPONS[p.data.weapon];
        const had = !!pl.weapons[p.data.weapon];
        pl.giveWeapon(p.data.weapon, def.type === 'melee' ? 0 : p.data.ammo);
        if (!had && (pl.weapon === 'fist' || def.slot > (WEAPONS[pl.weapon]?.slot ?? 0))) pl.switchTo(p.data.weapon);
        game.audio?.play('pickup');
        game.hud?.help(`${def.name}${def.type === 'melee' ? '' : ` +${p.data.ammo} rounds`}`, 2);
        return true;
      }
      case 'package': {
        this.collectedPackages.add(p.data.packageId);
        const n = this.collectedPackages.size;
        pl.money += 100;
        game.audio?.play('passed', 0.6);
        game.hud?.bigMessage(`HIDDEN PACKAGE ${n} OF ${this.packageSpots.length}`, 'hint', 3);
        if (n === 10) { game.hud?.help('10 packages: a Micro SMG will now spawn at your safehouse.'); }
        if (n === 20) { game.hud?.help('20 packages: an Assault Rifle will now spawn at your safehouse.'); }
        if (n === 30) { game.hud?.help('All packages found! A Rocket Launcher waits at your safehouse. You\'re a legend.'); pl.money += 50000; }
        this.spawnSafehouseRewards();
        return true;
      }
    }
    return false;
  }

  spawnSafehouseRewards() {
    const n = this.collectedPackages.size;
    const h = this.game.map.landmarks.home;
    if (this._rewardPickups) for (const p of this._rewardPickups) { p.remove(); const i = this.list.indexOf(p); if (i >= 0) this.list.splice(i, 1); }
    this._rewardPickups = [];
    const add = (w, ammo, dx) => { const p = new Pickup(this.game, 'weapon', new THREE.Vector3(h.x + dx, 0, h.z + 2), { weapon: w, ammo, respawn: 60 }); this.list.push(p); this._rewardPickups.push(p); };
    if (n >= 10) add('smg', 96, -2);
    if (n >= 20) add('rifle', 90, 0);
    if (n >= 30) add('rpg', 5, 2);
  }
}
