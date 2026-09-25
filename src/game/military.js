// Fort Carver and the other hardware spots: streams the parked aircraft / tanks placed by the world builder
// (respawning them a while after they're taken or destroyed), garrisons the base with soldiers, and enforces the
// restricted zone: a warning on entry, then the army opens fire and the cops send a three-star response.
import { BASE } from '../world/worldgen.js';
import { RNG, dist2, rand } from '../core/utils.js';
import { randomAppearance } from '../entities/humanoid.js';

// [x, z, facing yaw, patrol-to x, z]
const POSTS = [
  [-3834, -4048, Math.PI / 2], [-3834, -4074, Math.PI / 2],                 // main gate
  [-4880, -4200, Math.PI], [-4700, -4195, Math.PI, -4450, -4195], [-4560, -4205, Math.PI], // flight line
  [-4680, -4055, Math.PI], [-4600, -3995, 0],                                // helipads
  [-4095, -4205, Math.PI], [-4160, -4100, -Math.PI / 2, -4160, -4200],       // tank yard
  [-4290, -4196, Math.PI], [-4200, -3984, Math.PI],                          // tower, HQ
  [-5010, -3984, Math.PI, -4800, -3984], [-5150, -4470, Math.PI / 2], [-4005, -4470, -Math.PI / 2], // barracks, runway ends
  [-5250, -4700, 0, -4400, -4700],                                           // south fence patrol
];

const SPAWN_R = 1100, DESPAWN_R = 1500;

export class Military {
  constructor(game) {
    this.game = game;
    this.fixed = (game.map.fixedVehicles || []).map((e) => ({ ...e, veh: null, timer: 0, old: null }));
    this.soldiers = [];
    this.garrisoned = false;
    this.inside = false;
    this.alerted = false;
    this.grace = 0;
    this.t = 0;
    this.raiseT = 0;
    // map icons for the hardware (radar shows them only when close)
    const icon = { skipper: 'plane', hercules: 'plane', raptor: 'jet', warhawk: 'heli', skylark: 'heli', mammoth: 'tank' };
    const seen = new Set();
    for (const e of this.fixed) {
      const ic = icon[e.type];
      const key = ic + Math.round(e.x / 150) + ',' + Math.round(e.z / 150);
      if (!ic || seen.has(key)) continue;
      seen.add(key);
      game.blips.add({ x: e.x, z: e.z, icon: ic, color: 0x9fe3ff, small: true, noEdge: true });
    }
    const ev = game.events;
    // stealing military hardware or firing inside the fence puts the base on alert immediately
    ev.on('enteredVehicle', (c, v) => { if (c.isPlayer && v.def.military && this.inBase(v.pos)) this.alert('Military hardware stolen!'); });
    ev.on('gunshot', (s, pos) => { if (s?.isPlayer && this.inBase(pos)) this.alert(); });
    ev.on('explosion', (pos, r, src) => { if (src?.isPlayer && this.inBase(pos)) this.alert(); });
    ev.on('kill', (killer, victim) => { if (killer?.isPlayer && victim.gang === 'army') this.alert(); });
  }

  inBase(p, margin = 0) { return p.x > BASE.minX - margin && p.x < BASE.maxX + margin && p.z > BASE.minZ - margin && p.z < BASE.maxZ + margin; }

  alert(msg) {
    const g = this.game;
    if (!this.alerted) {
      this.alerted = true;
      g.hud?.bigMessage('ARMY ALERTED', 'failed', 2.5, msg || 'Fort Carver is under lockdown');
    }
    g.peds.gangAggro.army = true;
    if (!g.player.dead) g.police?.raise(3);
  }

  update(dt) {
    const g = this.game, p = g.player;
    if (g.gameplay?.state === 'menu') return;
    const pos = p.vehicle ? p.vehicle.pos : p.pos;
    // restricted zone
    const high = pos.y - g.map.groundHeight(pos.x, pos.z) > 220;
    const inside = this.inBase(pos) && !high && !p.dead;
    if (inside && !this.inside) {
      this.grace = this.alerted ? 0 : 8;
      if (!this.alerted) {
        g.hud?.bigMessage('RESTRICTED AREA', 'failed', 3, 'Fort Carver · military personnel only');
        g.hud?.help('You are trespassing on a military base. Leave now or the army will open fire.', 6);
      }
    }
    this.inside = inside;
    if (inside) {
      this.grace -= dt;
      if (this.grace <= 0) this.alert();
      if (g.peds.gangAggro.army && !this.alerted) this.alert();
      // while inside and on alert, the heat doesn't cool off
      this.raiseT -= dt;
      if (this.alerted && this.raiseT <= 0) { this.raiseT = 2; g.police?.raise(3); }
    } else if (this.alerted && (g.police?.level ?? 0) === 0) {
      // lockdown lifted once the wanted level is gone
      this.alerted = false;
      g.peds.gangAggro.army = false;
      for (const s of this.soldiers) if (!s.dead && s.state === 'attack') { s.threat = null; s.setState('guard'); }
    }
    if (p.dead) { this.inside = false; }

    this.t -= dt;
    if (this.t > 0) return;
    this.t = 0.5;
    this._streamVehicles(pos);
    this._garrison(pos);
  }

  // ------------------------------------------------------------------ parked aircraft & armour
  _streamVehicles(pos) {
    const g = this.game;
    for (const e of this.fixed) {
      const d2 = dist2(e.x, e.z, pos.x, pos.z);
      const v = e.veh;
      if (v) {
        const moved = dist2(v.pos.x, v.pos.z, e.x, e.z) > 25 * 25;
        if (v.removed || v.isWrecked || moved || v.driver) {
          if (v.driver || moved || v.isWrecked || v.removed) {
            // taken or destroyed: leave it to the world and respawn a fresh one later
            e.veh = null; e.timer = e.respawn || 180; e.old = v.removed ? null : v;
            v.persistent = !!v.driver?.isPlayer;
          }
          continue;
        }
        if (d2 > DESPAWN_R * DESPAWN_R) { g.vehicles.remove(v); e.veh = null; e.timer = 0; }
        continue;
      }
      e.timer -= 0.5;
      if (e.timer > 0 || d2 > SPAWN_R * SPAWN_R) continue;
      if (e.old && !e.old.removed) {
        // tidy up the last one if it was abandoned somewhere far from the player
        const o = e.old;
        if (!o.driver && dist2(o.pos.x, o.pos.z, pos.x, pos.z) > 400 * 400) { g.vehicles.remove(o); e.old = null; }
      }
      // don't pop in right in front of the player, and don't spawn into something parked on the spot
      if (e.respawned && d2 < 160 * 160) continue;
      if (g.vehicles.list.some((o) => !o.removed && dist2(o.pos.x, o.pos.z, e.x, e.z) < 14 * 14 && Math.abs(o.pos.y - e.y) < 5)) continue;
      e.veh = g.vehicles.spawn(e.type, e.x, e.z, e.yaw, { y: e.y, persistent: true, parked: true });
      e.respawned = true;
    }
  }

  // ------------------------------------------------------------------ soldiers
  _garrison(pos) {
    const g = this.game;
    const cx = (BASE.minX + BASE.maxX) / 2, cz = (BASE.minZ + BASE.maxZ) / 2;
    const d = Math.hypot(pos.x - cx, pos.z - cz);
    if (!this.garrisoned && d < 900) {
      this.garrisoned = true;
      const rng = new RNG(4242);
      POSTS.forEach((post, i) => {
        if (this.soldiers[i] && !this.soldiers[i].removed) return;
        this.soldiers[i] = this._spawnSoldier(post, rng);
      });
    } else if (this.garrisoned && d > 1300) {
      this.garrisoned = false;
      for (const s of this.soldiers) if (s && !s.removed) g.peds.remove(s);
      this.soldiers = [];
    }
    if (!this.garrisoned) return;
    // patrols walk back and forth between their two points
    for (const s of this.soldiers) {
      if (!s || s.dead || s.removed || !s.patrol) continue;
      if (s.state === 'guard' && s.stateTime > s.patrolWait) {
        s.patrolLeg = 1 - s.patrolLeg;
        const [x, z] = s.patrolLeg ? s.patrol[1] : s.patrol[0];
        s.targetPos.set(x, 0, z);
        s.gotoSpeed = 1.5; s.afterGoto = 'guard';
        s.setState('goto');
        s.patrolWait = rand(4, 10);
      }
    }
  }

  _spawnSoldier(post, rng) {
    const g = this.game;
    const [x, z, face, px, pz] = post;
    const app = randomAppearance(rng, {
      female: rng.chance(0.15), shirt: 0x5b6b3a, shirtType: 'long', pants: 0x4d5a33, jacketColor: 0x5b6b3a,
      hairStyle: 'cap', hat: 0x46542c, shoes: 0x2a2620, glasses: false, bandana: null, beard: false, shorts: false,
    });
    const s = g.peds.spawnPed(x, z, { appearance: app, brain: 'gang', gang: 'army', state: 'guard', weapon: 'rifle', health: 140, persistent: true, yaw: face });
    s.guardFace = face;
    s.accuracy = 0.62;
    s.damageMul = 0.6;
    s.soldier = true;
    if (px != null) { s.patrol = [[x, z], [px, pz]]; s.patrolLeg = 0; s.patrolWait = rand(2, 8); }
    return s;
  }
}
