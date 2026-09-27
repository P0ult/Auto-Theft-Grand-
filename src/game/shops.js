// Shopkeepers for the walk-in shops: a clerk behind each counter who serves you when you step up to the
// counter (the marker), greets you, and reacts when you point a gun at them — the burger bar and the
// liquor store hand over the till (and call the cops), the Gun Barn's owner pulls a shotgun. Kill the
// clerk and the shop is shut until you've been gone a while.
import * as THREE from 'three';
import { RNG, randInt, pick } from '../core/utils.js';
import { randomAppearance } from '../entities/humanoid.js';
import { Animal, BREEDS, PET_BREEDS } from '../entities/animals.js';

const CLERKS = {
  gunshop: {
    look: { female: false, shirtType: 'jacket', jacketColor: 0x4b5320, shirt: 0x2f2f2f, pants: 0x3b3326, hairStyle: 'cap', hat: 0x222222, beard: true, glasses: true, build: 1.2, height: 1.03 },
    greet: ['Welcome to the Gun Barn. Look, don\'t touch.', 'What can I do you for?', 'Second Amendment\'s open for business.'],
    threat: ['In MY store? Big mistake!', 'You picked the wrong shop, pal!'],
    hostile: true, weapon: 'shotgun', label: 'Gun Barn', color: 0xff5a36, icon: 'gun',
  },
  burger: {
    look: { shirtType: 'tee', shirt: 0xc1121f, pants: 0x222222, hairStyle: 'cap', hat: 0xc1121f },
    greet: ['Welcome to Big Bun! Can I take your order?', 'Hi! Try the Double Stack.', 'Big Bun, how can I help you?'],
    threat: ['Please! Take it! Take it all!', 'I only work here!'],
    cash: [60, 240], label: 'Big Bun Burgers', color: 0xffd166, icon: 'burger',
  },
  liquor: {
    look: { female: false, shirtType: 'tank', shirt: 0xe8e8e8, pants: 0x3a3a3a, hairStyle: 'bald', beard: true, build: 1.15 },
    greet: ['Hey.', 'Store closes when I say it does.', 'No loitering.'],
    threat: ['Not again! Here — just go!', 'Okay, okay! It\'s all yours!'],
    cash: [150, 650], label: 'Ray\'s Liquor', color: 0x8ae3ff, icon: 'money',
  },
  petshop: {
    look: { female: true, shirtType: 'tee', shirt: 0x2e8b57, pants: 0x2b2b2b, hairStyle: 'ponytail', glasses: true },
    greet: ['Welcome to Pet Palace! Looking for a new best friend?', 'Hi! All our dogs and cats are vaccinated and chipped.', 'Every one of them needs a good home.'],
    threat: ['Please! Don\'t hurt the animals!', 'Take the money — leave the puppies alone!'],
    cash: [120, 480], color: 0x7be0a0, icon: 'paw', blip: true,
  },
  store: {
    look: { shirtType: 'tee', shirt: 0x0f7a3a, pants: 0x222222, hairStyle: 'cap', hat: 0x0f7a3a },
    greet: ['Welcome to 24/7.', 'Hot dogs are fresh. Ish.', 'Hey, how\'s it going?'],
    threat: ['Whoa, whoa! It\'s yours!', 'Minimum wage doesn\'t cover this!'],
    cash: [80, 420], color: 0x7cf29c, icon: 'cart', blip: true,
  },
  bar: {
    look: { female: false, shirtType: 'jacket', jacketColor: 0x1b1b1b, shirt: 0xe8e8e8, pants: 0x1b1b1b, hairStyle: 'short', beard: true, build: 1.1 },
    greet: ['What\'ll it be?', 'First one\'s not on the house.', 'Pull up a stool.'],
    threat: ['Easy, pal. Take the register.', 'You\'re making a big mistake.'],
    cash: [200, 700], color: 0xffa24a, icon: 'beer', blip: true,
  },
  cafe: {
    look: { female: true, shirtType: 'tee', shirt: 0x6b4226, pants: 0x222222, hairStyle: 'bun' },
    greet: ['Hi! What can I get started for you?', 'Welcome to Bean Scene.', 'Oat milk\'s back in stock!'],
    threat: ['Okay! Okay! Here!', 'Please, just take it!'],
    cash: [60, 300], color: 0xe8c39a, icon: 'cup', blip: true,
  },
};

const STORE_STOCK = [
  { name: 'Hot dog', desc: '+20 health', price: 3, use: (p) => { p.health = Math.min(p.maxHealth, p.health + 20); } },
  { name: 'Chips & soda', desc: '+15 health, some stamina', price: 3, use: (p) => { p.health = Math.min(p.maxHealth, p.health + 15); p.stamina = Math.min(1, p.stamina + 0.4); } },
  { name: 'First-aid kit', desc: 'Full health', price: 60, use: (p) => { p.health = p.maxHealth; } },
  { name: 'Energy drink', desc: 'Full stamina, +10 health', price: 4, use: (p) => { p.stamina = 1; p.health = Math.min(p.maxHealth, p.health + 10); } },
];
const drink = (k, hp) => (p, g) => { p.health = Math.min(p.maxHealth, p.health + hp); p.drunk = Math.min(1.5, (p.drunk || 0) + k); if (p.drunk > 1) return 'The room is starting to spin...'; };
const BAR_STOCK = [
  { name: 'Beer', desc: '+5 health · a little tipsy', price: 5, use: drink(0.3, 5) },
  { name: 'Whiskey', desc: '+10 health · the room sways', price: 12, use: drink(0.6, 10) },
  { name: 'Tequila shot', desc: 'Liquid courage', price: 8, use: drink(0.45, 5) },
  { name: 'Glass of water', desc: 'Sober up', price: 1, use: (p) => { p.drunk = 0; return 'Much better.'; } },
];
const CAFE_STOCK = [
  { name: 'Espresso', desc: 'Full stamina', price: 3, use: (p) => { p.stamina = 1; p.drunk = Math.max(0, (p.drunk || 0) - 0.4); } },
  { name: 'Iced latte', desc: 'Full stamina, +10 health', price: 5, use: (p) => { p.stamina = 1; p.health = Math.min(p.maxHealth, p.health + 10); } },
  { name: 'Glazed donut', desc: '+15 health', price: 2, use: (p) => { p.health = Math.min(p.maxHealth, p.health + 15); } },
  { name: 'Club sandwich', desc: '+40 health', price: 6, use: (p) => { p.health = Math.min(p.maxHealth, p.health + 40); } },
];
const PET_BLURB = {
  lab: 'Friendly, loyal, loves the car', shepherd: 'Protective — goes for anyone who hurts you', husky: 'Tireless runner, very talkative',
  rottweiler: 'Big, brave bodyguard', pug: 'Small, snorts a lot, adorable', poodle: 'Clever and well-groomed',
  tabby: 'Independent, naps anywhere', blackcat: 'Mysterious. Lucky?', siamese: 'Chatty and curious',
};
function petStock() {
  return PET_BREEDS.map((b) => ({
    name: BREEDS[b].name, desc: PET_BLURB[b] || '', price: BREEDS[b].price || 800,
    use: (p, g) => { const a = g.pets?.adopt(b); return a ? `Meet ${a.petName}! Whistle (K) to make them stay or come.` : 'Something went wrong.'; },
  })).concat([{ name: 'Pet treats', desc: 'Heal your pet', price: 15, use: (p, g) => { const a = g.pets?.pet; if (!a || a.dead) return 'You don\'t have a pet yet.'; a.health = a.maxHealth; return `${a.petName} loves them.`; } }]);
}

const LIQUOR_STOCK = [
  { name: 'Candy bar', desc: '+10 health', price: 2, use: (p) => { p.health = Math.min(p.maxHealth, p.health + 10); } },
  { name: 'Sub sandwich', desc: '+35 health', price: 6, use: (p) => { p.health = Math.min(p.maxHealth, p.health + 35); } },
  { name: 'Energy drink', desc: 'Full stamina, +10 health', price: 4, use: (p) => { p.stamina = 1; p.health = Math.min(p.maxHealth, p.health + 10); } },
  { name: 'Scratch card', desc: 'Could be your lucky day', price: 5, use: (p, g) => {
    const r = Math.random();
    const win = r < 0.02 ? 5000 : r < 0.1 ? 250 : r < 0.3 ? 20 : 0;
    if (win) { p.money += win; g.hud?.moneyFlash?.(win); g.audio?.play('cash'); }
    return win ? `Winner! $${win}.` : 'No luck this time.';
  } },
];

export class ShopSystem {
  constructor(game) {
    this.game = game;
    this.shops = (game.map.interiors || []).filter((it) => CLERKS[it.key]).map((it) => ({ it, def: CLERKS[it.key], clerk: null, state: 'closed', respawnAt: 0, t: 0, greeted: false, robbedAt: -1e9, pets: [] }));
    for (const s of this.shops) {
      const it = s.it, d = s.def;
      s.marker = game.pickups.addMarker(it.service.x, it.service.z, { y: it.service.y + 0.06, color: d.color, radius: 0.9, height: 1.2, icon: d.icon, label: d.label || it.name, footOnly: true, arrow: false, onEnter: () => this.serve(s) });
      game.pickups.services.push(s.marker);
    }
  }

  // after a respawn: a clerk who was shooting at you goes back behind the counter
  calmDown() {
    for (const s of this.shops) if (s.state === 'hostile' && s.clerk && !s.clerk.dead) { this.game.peds.remove(s.clerk); s.clerk = null; s.state = 'closed'; }
  }

  shopAt(x, z) { return this.shops.find((s) => s.it.inside(x, z)) || null; }

  // the player stepped up to the counter
  serve(s) {
    const g = this.game, p = g.player;
    const c = s.clerk;
    if (!c || c.dead || c.removed || s.state !== 'calm') {
      g.hud?.help(s.state === 'robbed' ? 'The clerk is cowering behind the counter. Nobody\'s serving now.' : 'There\'s nobody behind the counter.', 3);
      return;
    }
    c.faceTowards?.(p.pos.x, p.pos.z, 0);
    const nm = s.it.name.toUpperCase();
    if (s.it.key === 'gunshop') g.hud?.openShop();
    else if (s.it.key === 'burger') g.pickups.eat();
    else if (s.it.key === 'liquor') g.hud?.openStore?.('RAY\'S LIQUOR', 'Snacks, smokes & scratch cards', LIQUOR_STOCK);
    else if (s.it.key === 'store') g.hud?.openStore?.(nm, 'Open all day, every day', STORE_STOCK.concat(LIQUOR_STOCK.slice(3)));
    else if (s.it.key === 'bar') g.hud?.openStore?.(nm, 'Cold beer, strong spirits', BAR_STOCK);
    else if (s.it.key === 'cafe') g.hud?.openStore?.(nm, 'Coffee & snacks', CAFE_STOCK);
    else if (s.it.key === 'petshop') g.hud?.openStore?.(nm, g.pets?.pet && !g.pets.pet.dead ? `Adopting a new friend replaces ${g.pets.pet.petName}` : 'Dogs & cats looking for a home', petStock());
  }

  _spawnClerk(s) {
    const g = this.game, it = s.it;
    const rng = new RNG(it.key.length * 131 + 7);
    const app = randomAppearance(rng, s.def.look);
    const c = g.peds.spawnPed(it.clerk.x, it.clerk.z, { appearance: app, brain: 'script', persistent: true, yaw: it.clerk.yaw, y: it.clerk.y });
    c.shopClerk = true;
    c.health = c.maxHealth = 80;
    c.scriptThink = (dt) => this._think(s, dt);
    s.clerk = c; s.state = 'calm'; s.t = 0; s.greeted = false;
    if (it.key === 'petshop' && !s.pets.length) this._stockKennels(s);
    return c;
  }

  // dogs and cats in the kennels at Pet Palace
  _stockKennels(s) {
    const g = this.game, it = s.it, F = it.F;
    const breeds = ['lab', 'pug', 'husky', 'tabby', 'shepherd', 'siamese', 'poodle', 'blackcat'];
    it.furniture.filter((q) => q.kind === 'pen').forEach((pen, i) => {
      for (let k = 0; k < (i % 2 ? 1 : 2); k++) {
        const u = (pen.u0 + pen.u1) / 2 + (k ? 0.45 : -0.3), w = (pen.w0 + pen.w1) / 2 + (k ? 0.5 : -0.3);
        const a = new Animal(g, breeds[(i * 2 + k) % breeds.length], F.x(u, w), F.z(u, w), { y: F.y + 0.09, yaw: F.yawR + (k ? 0.6 : -0.4) });
        a.state = (i + k) % 3 ? 'sit' : 'idle'; a.display = true;
        a.root.rotation.set(0, a.yaw, 0);
        s.pets.push(a);
      }
    });
  }

  _think(s, dt) {
    const g = this.game, c = s.clerk, it = s.it, p = g.player;
    s.t += dt;
    c.stop?.();
    c.animState.handsUp = false; c.animState.cower = false; c.crouching = false; c.animState.talking = false;
    // stay behind the counter
    const dx = it.clerk.x - c.pos.x, dz = it.clerk.z - c.pos.z;
    if (dx * dx + dz * dz > 0.25 && s.state !== 'robbed') { c.goTo?.(it.clerk.x, it.clerk.z, 1.4, dt, 0.3); return; }
    const inside = !p.vehicle && it.inside(p.pos.x, p.pos.z);
    if (s.state === 'handsup') {
      c.animState.handsUp = true;
      c.faceTowards(p.pos.x, p.pos.z, dt, 6);
      if (s.t > 2.6) {
        // the till: cash on the counter
        const amt = randInt(s.def.cash[0], s.def.cash[1]);
        g.pickups.dropMoney(new THREE.Vector3(it.service.x, it.service.y, it.service.z), amt);
        g.police?.crime(4, c.pos, true);
        g.events.emit('shopRobbed', it.key, amt);
        s.state = 'robbed'; s.t = 0; s.robbedAt = g.time;
      }
      return;
    }
    if (s.state === 'robbed') {
      c.animState.cower = true; c.crouching = true;
      if (s.t > 40 && !inside) { s.state = 'calm'; s.t = 0; s.greeted = false; }
      return;
    }
    // calm: face the customer when they're in the shop, the door otherwise
    if (inside && Math.hypot(p.pos.x - c.pos.x, p.pos.z - c.pos.z) < 12) c.faceTowards(p.pos.x, p.pos.z, dt, 4);
    else c.yaw = it.clerk.yaw;
    if (inside && !s.greeted) { s.greeted = true; c.say?.(pick(s.def.greet)); c.animState.talking = true; }
    if (!inside && Math.hypot(p.pos.x - it.center.x, p.pos.z - it.center.z) > 25) s.greeted = false;
    // a gun in my face?
    if (inside && this._threatened(c)) {
      c.say?.(pick(s.def.threat));
      if (s.def.hostile) {
        c.brain = 'civilian';
        c.giveWeapon(s.def.weapon, 60); c.equip(s.def.weapon);
        c.threat = p; c.threatPos?.copy?.(p.pos); c.setState('attack');
        c.accuracy = 0.55;
        g.police?.crime(2, c.pos, true);
        s.state = 'hostile';
      } else { s.state = 'handsup'; s.t = 0; }
    }
  }

  _threatened(c) {
    const g = this.game, p = g.player;
    if (!p.aiming || p.vehicle || p.dead) return false;
    const def = p.weaponDef;
    if (!def || (def.type !== 'gun' && def.type !== 'launcher')) return false;
    const d = Math.hypot(c.pos.x - p.pos.x, c.pos.z - p.pos.z);
    if (d > 16) return false;
    const cam = g.camera.position, dir = g.rig.lookDir(this._v || (this._v = new THREE.Vector3()));
    const tx = c.pos.x - cam.x, ty = c.pos.y + 1.2 - cam.y, tz = c.pos.z - cam.z, tl = Math.hypot(tx, ty, tz) || 1;
    return (dir.x * tx + dir.y * ty + dir.z * tz) / tl > 0.95;
  }

  update(dt) {
    const g = this.game, p = g.player;
    const pp = p.vehicle ? p.vehicle.pos : p.pos;
    // a few drinks: the world sways (walks it off over a minute or two)
    if (p.drunk > 0) {
      p.drunk = Math.max(0, p.drunk - dt * 0.012);
      const k = Math.min(1, p.drunk), t = g.time;
      g.rig.yaw += Math.sin(t * 0.9) * 0.0035 * k + Math.sin(t * 2.3) * 0.0012 * k;
      g.rig.pitch = (g.rig.pitch || 0) + Math.sin(t * 1.3) * 0.0015 * k;
    }
    for (const s of this.shops) {
      const it = s.it;
      const d = Math.hypot(pp.x - it.center.x, pp.z - it.center.z);
      if (s.pets.length) {
        if (d > 110) { for (const a of s.pets) a.remove(); s.pets = []; }
        else if (d < 60) for (const a of s.pets) { a.t += dt; a.happy = Math.hypot(p.pos.x - a.pos.x, p.pos.z - a.pos.z) < 4; a._animate(dt); if (a.happy && Math.random() < dt * 0.15) g.audio?.playAt(a.kind === 'dog' ? 'bark' : 'meow', a.pos, 0.5); }
      }
      const c = s.clerk;
      if (c && (c.removed || c.dead)) {
        if (s.state !== 'dead') {
          s.state = 'dead';
          s.respawnAt = g.time + 60;
          if (!c.removed) g.pickups.dropMoney(c.pos.clone(), randInt(40, 160));
        }
        // the body goes once you're out of sight; a new clerk turns up later
        if (d > 70 && !c.removed) g.peds.remove(c);
        if (d > 70 && g.time > s.respawnAt) { s.clerk = null; s.state = 'closed'; }
        continue;
      }
      if (!c && d < 85 && s.state !== 'dead') this._spawnClerk(s);
      else if (c && d > 130) { g.peds.remove(c); s.clerk = null; s.state = 'closed'; }
      if (s.clerk && s.state === 'hostile' && d > 60) { g.peds.remove(s.clerk); s.clerk = null; s.state = 'closed'; }
      s.marker.group.visible = s.state === 'calm';
    }
  }
}
