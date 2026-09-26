// Free roam extras: unlimited cash and ammo, and fast travel to any landmark (pause menu tab or the T key).
// Destinations snap to the nearest street (a kerbside lane when driving, the pavement on foot); an aircraft
// that is already flying arrives in the air over the destination at the same height and speed.
import { TOWNS, BASE, AIRFIELD } from '../world/worldgen.js';
import * as THREE from 'three';
import { WEAPONS } from './weapondefs.js';

export const FREE_CASH = 99999999;
const _at = [0, 0, 0, 0, 0];
const _Y = new THREE.Vector3(0, 1, 0);

export class FreeRoam {
  constructor(game) {
    this.game = game;
  }

  get active() { return !!this.game.freeRoam && !this.game.missions?.active; }

  update() {
    if (!this.active) return;
    const p = this.game.player;
    p.money = FREE_CASH;
    // magazines stay full (Player.fire / throwGrenade skip the decrement too); keep a full reserve for show
    for (const [id, w] of Object.entries(p.weapons)) {
      const def = WEAPONS[id];
      if (!def || def.type === 'melee') continue;
      if (def.type === 'thrown') { w.clip = Math.max(w.clip || 0, 1); w.ammo = Math.max(w.ammo || 0, 9); continue; }
      w.clip = def.clip; w.ammo = Math.max(w.ammo || 0, def.clip * 4);
    }
    if (p.reloading > 0) p.reloading = 0;
  }

  // ------------------------------------------------------------------ destinations
  destinations() {
    const g = this.game, L = g.map.landmarks, out = [];
    const add = (group, name, x, z, o = {}) => { if (Number.isFinite(x) && Number.isFinite(z)) out.push({ group, name, x, z, ...o }); };
    const hud = g.hud;
    if (hud?.waypoint) add('Quick', 'Map waypoint', hud.waypoint.x, hud.waypoint.z, { foot: true });
    add('Quick', 'Safehouse', L.home?.x, L.home?.z, { foot: true, yaw: Math.PI });

    const city = 'Los Soles';
    add(city, 'Hospital', L.hospital?.respawn?.x, L.hospital?.respawn?.z, { foot: true, yaw: L.hospital?.respawn?.rot });
    add(city, 'Police HQ', L.police?.respawn?.x, L.police?.respawn?.z, { foot: true, yaw: L.police?.respawn?.rot });
    add(city, 'City Plaza', L.plaza?.x, L.plaza?.z);
    add(city, 'Gun Barn', L.gunshop?.x, L.gunshop?.z);
    add(city, 'Spray Shack', L.spray?.entry?.x, L.spray?.entry?.z);
    add(city, L.mall?.name || 'Mall', L.mall?.x, L.mall?.z);
    add(city, L.park?.name || 'Park', L.park?.x, L.park?.z, { foot: true });
    add(city, L.stadium?.name || 'Stadium', L.stadium?.x, L.stadium?.z);
    add(city, 'Big Bun Burgers', L.burger?.x, L.burger?.z);
    add(city, 'The Projects', L.projects?.x, L.projects?.z);
    add(city, 'Vipers Turf', L.vipers?.x, L.vipers?.z);
    add(city, 'Hillside Mansion', L.mansion?.x, L.mansion?.z);
    add(city, L.golf?.name || 'Country Club', L.golf?.x, L.golf?.z);
    add(city, 'Los Soles Sign', L.sign?.x, L.sign?.z, { foot: true });
    add(city, 'Beach', L.beach?.x, L.beach?.z, { foot: true, exactVeh: true });
    add(city, 'Pier & Ferris Wheel', L.pierfront?.x, L.pierfront?.z);
    add(city, 'Docks', L.docksQuay?.x, L.docksQuay?.z);
    add(city, 'Warehouses', L.warehouse?.x, L.warehouse?.z);

    const towns = 'Towns';
    for (const t of Object.values(TOWNS)) add(towns, t.name, t.x, t.z);
    if (L.halePier) add(towns, 'Port Hale Pier', L.halePier.x - 6, L.halePier.z, { foot: true, y: L.halePier.y });

    const rail = g.map.roadInfo?.rail;
    for (const st of rail?.stations || []) {
      // on foot: the platform (right of increasing s), facing along it; by car: the forecourt behind the building
      const hx = st.key === 'union' ? 9 : 6, lat = 6.75 + 3 + 2 * hx + 6;
      add('Sol Line', /station/i.test(st.name) ? st.name : `${st.name} station`, st.x - st.tz * 4.6, st.z + st.tx * 4.6, {
        foot: true, y: st.y + 1.05, yaw: st.yaw,
        veh: { x: st.x - st.tz * lat + st.tx * 16, z: st.z + st.tx * lat + st.tz * 16, yaw: st.yaw },
      });
    }

    const hw = 'Airfields & military';
    add(hw, AIRFIELD.name, AIRFIELD.x - 20, AIRFIELD.z + 40, { foot: true, exactVeh: true, yaw: AIRFIELD.yaw });
    add(hw, `${BASE.name} main gate`, BASE.maxX + 60, BASE.gateZ);
    const jet = (g.map.fixedVehicles || []).find((e) => e.type === 'raptor');
    if (jet) add(hw, `${BASE.name} flight line (restricted!)`, jet.x + 14, jet.z + 4, { foot: true, exactVeh: true });
    const heli = (g.map.fixedVehicles || []).find((e) => e.type === 'skylark');
    if (heli) add(hw, 'Skylark helipad', heli.x + 9, heli.z + 3, { foot: true, exactVeh: true });
    return out;
  }

  // ------------------------------------------------------------------ travel
  teleport(d, instant = false) {
    const g = this.game;
    const go = () => {
      this.place(d);
      g.traffic?.populate(12);
      g.peds?.populate(12);
      g.hud?.help(`Teleported to <b>${d.name}</b>.`, 3);
    };
    if (instant || !g.hud) { go(); return; }
    g.hud.fadeTo(1, 0.3);
    setTimeout(() => { go(); setTimeout(() => g.hud.fadeTo(0, 0.6), 250); }, 380);
  }

  // Move the player (and whatever they're driving) to a destination.
  place(d) {
    const g = this.game, p = g.player;
    let v = p.vehicle;
    // the train can't leave its rails: step off it first
    if (v && v.def.train) { v.takeOut(p); v = null; }
    if (p.chute) p.chute = null;

    // an aircraft in flight arrives in the air over the destination
    if (v && v.def.aircraft && !v.grounded && v.altitude > 8) {
      const alt = Math.max(v.altitude, v.def.kind === 'heli' ? 60 : 140);
      const gy = Math.max(g.map.groundHeight(d.x, d.z), 0);
      v.pos.set(d.x, gy + alt, d.z);
      g.rig.flightOff = null;
      return;
    }

    let x = d.x, z = d.z, y = d.y, yaw = d.yaw ?? (g.rig.yaw - Math.PI);
    if (v && d.veh) { ({ x, z, yaw } = d.veh); y = undefined; }
    else if (v ? !d.exactVeh : !d.foot || (d.y == null && g.map.isWater(d.x, d.z))) {
      // (a waypoint dropped in the sea sends you to the nearest street instead)
      const spot = this.roadSpot(d.x, d.z, !v, v ? v.def.W || 2 : 0);
      if (spot) ({ x, z, y, yaw } = spot);
    }
    if (y == null) y = g.collision.floorHeight(x, z, g.map.groundHeight(x, z) + 1.5);

    if (v) {
      v.pos.set(x, y, z);
      v.yaw = yaw;
      v.vel.set(0, 0, 0);
      v.r = 0;
      if ('vy' in v) v.vy = 0;
      v.airborne = false;
      v.lastGroundY = y;
      if (v.quat) { v.quat.setFromAxisAngle(_Y, yaw); v.angVel?.set(0, 0, 0); v.grounded = true; v.gearDown = true; v.gearK = 1; }
      v._placeGroup?.();
      g.rig.flightOff = null;
    } else {
      p.setPosition(x, y, z);
      p.setYaw(yaw);
      p.vel.set(0, 0, 0);
      p.swimming = false;
    }
    g.rig.yaw = yaw + Math.PI;
    g.rig.vehYawOffset = 0;
  }

  // Nearest street spot: a kerbside lane for vehicles, the pavement beside it on foot. Checks that the
  // pavement is actually there (not off the edge of a bridge or inside a building).
  roadSpot(x, z, onFoot, width = 2) {
    const g = this.game, net = g.map.roads;
    if (!net) return null;
    const ok = (e) => !e.removed && e.type !== 'rail' && (!onFoot || e.T.cls < 2);
    const c = net.closest(x, z, ok, 600) || net.closest(x, z, (e) => !e.removed && e.type !== 'rail', 1500);
    if (!c) return null;
    const e = c.e;
    const s = Math.min(Math.max(c.s, 6), e.len - 6);
    net.at(e, s, _at);
    let [rx, ry, rz, tx, tz] = _at;
    // kerbside lane, travelling with the traffic (one-way edges may only run b -> a)
    const back = !(e.lanesF > 0) && e.lanesB > 0;
    if (back) { tx = -tx; tz = -tz; }
    const nl = back ? e.lanesB : e.lanesF;
    const laneOff = nl > 0 ? net.laneOffset(e, back ? 1 : 0, nl - 1) : 0;
    const yaw = Math.atan2(tx, tz);
    const lane = { x: rx - tz * laneOff, z: rz + tx * laneOff, y: ry, yaw };
    if (!onFoot) return lane;
    // pavement: just past the right-hand edge of the road
    // city streets have a pavement inside the paved width; elsewhere step onto the verge
    const w = (back ? e.wL : e.wR) ?? 4, laneEdge = e.T.off0 + e.T.laneW * Math.max(1, nl);
    const off = w - laneEdge > 2 ? (w + laneEdge) / 2 + 0.4 : w + 1.2;
    const px = rx - tz * off, pz = rz + tx * off;
    const fy = g.collision.floorHeight(px, pz, ry + 1);
    const res = g.collision.resolveCircle(px, pz, 0.45, fy, 1.8);
    if (Math.abs(fy - ry) < 1.2 && !res.hit) return { x: px, z: pz, y: fy, yaw };
    return lane;
  }
}
