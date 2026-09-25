// Combat: hitscan firing with spread/pellets, melee hits, explosions, rockets and grenades.
import * as THREE from 'three';
import { WEAPONS } from './weapondefs.js';
import { rayOBBYaw, raySphere, clamp, rand } from '../core/utils.js';

const _n = new THREE.Vector3();

export class Combat {
  constructor(game) {
    this.game = game;
    this.projectiles = [];
    this.rocketGeo = new THREE.CylinderGeometry(0.05, 0.05, 0.6, 8).rotateX(Math.PI / 2);
    this.rocketMat = new THREE.MeshStandardMaterial({ color: 0x3e4a2f, roughness: 0.6 });
    this.grenadeGeo = new THREE.SphereGeometry(0.06, 8, 6);
    this.grenadeMat = new THREE.MeshStandardMaterial({ color: 0x3b4a2a, roughness: 0.7 });
  }

  // Cast a ray against characters, vehicles and the static world. Returns nearest hit.
  raycast(ox, oy, oz, dx, dy, dz, maxT, exclude = null) {
    const game = this.game;
    let best = null;
    const stat = game.collision.raycast(ox, oy, oz, dx, dy, dz, maxT);
    if (stat) best = { t: stat.t, kind: 'static', obj: stat.obj, normal: new THREE.Vector3(stat.nx, stat.ny, stat.nz) };
    const lim = best ? best.t : maxT;
    for (const c of game.allCharacters()) {
      if (c === exclude || c.removed) continue;
      if (exclude && exclude.vehicle && c.vehicle === exclude.vehicle) continue;
      const cx = c.ragdolling ? c.ragdoll.pos[0] : c.pos.x, cz = c.ragdolling ? c.ragdoll.pos[2] : c.pos.z;
      // quick reject by distance from ray
      const lx = cx - ox, lz = cz - oz;
      const along = lx * dx + lz * dz;
      if (along < -1 || along > lim + 1) continue;
      const px = ox + dx * along - cx, pz = oz + dz * along - cz;
      if (px * px + pz * pz > 4) continue;
      const h = c.rayHit(ox, oy, oz, dx, dy, dz, best ? best.t : maxT);
      if (h && (!best || h.t < best.t)) best = { t: h.t, kind: 'char', obj: c, part: h.part, particle: h.particle };
    }
    for (const v of game.vehicles.list) {
      if (v.removed) continue;
      if (exclude && exclude.vehicle === v) continue;
      const hy = v.def.H / 2;
      const t = rayOBBYaw(ox, oy, oz, dx, dy, dz, v.pos.x, v.pos.y + hy + 0.05, v.pos.z, v.yaw, v.hx, hy, v.hz, _n);
      if (t >= 0 && t < (best ? best.t : maxT)) {
        // occupants visible through windows: check them first
        let occ = null;
        for (const o of v.occupants) {
          if (!o || o === exclude) continue;
          const h = o.rayHit(ox, oy, oz, dx, dy, dz, maxT);
          if (h && h.t < t + 1.5) { occ = { t: h.t, kind: 'char', obj: o, part: h.part }; break; }
        }
        if (occ && Math.random() < 0.55) best = occ;
        else best = { t, kind: 'vehicle', obj: v, normal: _n.clone() };
      }
    }
    const heli = game.police?.heli;
    if (heli && !heli.done && !heli.down) {
      const t = raySphere(ox, oy, oz, dx, dy, dz, heli.pos.x, heli.pos.y, heli.pos.z, 2.6);
      if (t >= 0 && t < (best ? best.t : maxT)) best = { t, kind: 'heli', obj: heli, normal: new THREE.Vector3(-dx, -dy, -dz) };
    }
    if (best) best.point = new THREE.Vector3(ox + dx * best.t, oy + dy * best.t, oz + dz * best.t);
    return best;
  }

  fireWeapon(shooter, def, origin, aimDir, opts = {}) {
    const game = this.game;
    if (def.type === 'launcher') return this.fireRocket(shooter, origin, aimDir);
    const muzzle = shooter.muzzleWorld(new THREE.Vector3());
    // Figure out what the crosshair points at (from the camera ray), then shoot from the muzzle toward it
    const aimHit = opts.fromMuzzle ? null : this.raycast(origin.x, origin.y, origin.z, aimDir.x, aimDir.y, aimDir.z, def.range, shooter);
    const target = aimHit ? aimHit.point : origin.clone().addScaledVector(aimDir, def.range);
    const base = target.clone().sub(muzzle);
    const dist = base.length();
    base.normalize();
    // if the muzzle is behind a wall that the camera sees past, just use camera direction from muzzle
    const spreadMul = (opts.spreadMul ?? 1) * (shooter.isPlayer && shooter.crouching ? 0.6 : 1) * (shooter.isPlayer && shooter.vel && Math.hypot(shooter.vel.x, shooter.vel.z) > 3 ? 1.8 : 1);
    const pellets = def.pellets || 1;
    let anyHit = false;
    for (let i = 0; i < pellets; i++) {
      const d = base.clone();
      const sp = def.spread * spreadMul;
      d.x += rand(-sp, sp); d.y += rand(-sp, sp) * 0.8; d.z += rand(-sp, sp);
      d.normalize();
      const hit = this.raycast(muzzle.x, muzzle.y, muzzle.z, d.x, d.y, d.z, def.range, shooter);
      const end = hit ? hit.point : muzzle.clone().addScaledVector(d, Math.min(def.range, dist + 30));
      if (i < 3) game.effects.tracers.add(muzzle, end);
      if (hit) { anyHit = true; this.applyHit(hit, def, shooter, d); }
    }
    game.effects.muzzleFlash(muzzle, base, def.id === 'shotgun');
    game.audio?.playAt(def.sound, muzzle, shooter.isPlayer ? 1 : 0.8, { gun: true });
    game.events.emit('gunshot', shooter, muzzle, def);
    return anyHit;
  }

  applyHit(hit, def, shooter, dir) {
    const game = this.game;
    if (hit.kind === 'char') {
      const c = hit.obj;
      const wasDead = c.dead;
      const dmg = def.damage * (shooter.isPlayer ? 1 : (shooter.damageMul || 0.55));
      const imp = dir.clone().multiplyScalar(def.id === 'shotgun' ? 3 : 2.2).add(new THREE.Vector3(0, 0.6, 0));
      c.takeDamage(dmg, { part: hit.part, source: shooter, type: 'bullet', impulse: imp, hitPoint: hit.point, weapon: def.id });
      if (wasDead && c.ragdolling) c.ragdoll.push(c.ragdoll.nearestParticle(hit.point.x, hit.point.y, hit.point.z), dir.x * 3, 0.5, dir.z * 3);
      game.effects.blood(hit.point, dir, hit.part === 'head' ? 14 : 7);
      game.audio?.playAt('bulletflesh', hit.point, 0.6);
      if (c.dead && !wasDead) { game.effects.bloodPool(c.ragdolling ? new THREE.Vector3(c.ragdoll.pos[0], 0, c.ragdoll.pos[2]) : c.pos); game.events.emit('kill', shooter, c, def.id, hit.part); }
    } else if (hit.kind === 'vehicle') {
      const v = hit.obj;
      v.damage(def.damage * 0.9, shooter);
      game.effects.impact(hit.point, hit.normal, 'metal');
      game.audio?.playAt('bulletmetal', hit.point, 0.5);
      // tires & fuel: small chance to ignite when already damaged
      if (v.health < 250 && Math.random() < 0.05) v.health = 0;
      game.events.emit('vehicleShot', v, shooter);
    } else if (hit.kind === 'heli') {
      hit.obj.hit(def.damage * (shooter.isPlayer ? 1 : 0.3));
      game.effects.impact(hit.point, hit.normal, 'metal');
      game.audio?.playAt('bulletmetal', hit.point, 0.5);
      if (shooter.isPlayer) game.police.crime(0.3, hit.point, true);
    } else {
      const kind = hit.obj && hit.obj.kind === 'circle' ? 'metal' : 'concrete';
      game.effects.impact(hit.point, hit.normal, kind);
      if (Math.random() < 0.3) game.audio?.playAt('ricochet', hit.point, 0.4);
    }
  }

  // ------------------------------------------------------------------ melee
  meleeHit(attacker, act) {
    const game = this.game;
    const def = WEAPONS[attacker.weapon] || WEAPONS.fist;
    const range = def.range + (act === 'kick' ? 0.3 : 0);
    const fx = Math.sin(attacker.yaw), fz = Math.cos(attacker.yaw);
    let hitAny = false;
    for (const c of game.allCharacters()) {
      if (c === attacker || c.dead || c.vehicle) continue;
      const dx = c.pos.x - attacker.pos.x, dz = c.pos.z - attacker.pos.z;
      const d = Math.hypot(dx, dz);
      if (d > range + 0.35 || Math.abs(c.pos.y - attacker.pos.y) > 1.2) continue;
      const dot = (dx * fx + dz * fz) / (d || 1);
      if (dot < 0.45 && d > 0.6) continue;
      let dmg = def.damage;
      if (act === 'kick') dmg *= 1.6;
      if (act === 'cross') dmg *= 1.2;
      if (!attacker.isPlayer) dmg *= 0.8;
      if (c.ragdolling) dmg *= 1.3;
      const strong = act === 'kick' || def.id === 'bat' || (act === 'cross' && Math.random() < 0.25);
      const imp = new THREE.Vector3(fx * (strong ? 3.5 : 1.5), strong ? 1.5 : 0.5, fz * (strong ? 3.5 : 1.5));
      const hp = c.chestPos;
      c.takeDamage(dmg, { part: act === 'cross' && Math.random() < 0.3 ? 'head' : 'torso', headMul: 1.5, source: attacker, type: 'melee', impulse: imp, knockdown: strong && !c.isPlayer, hitPoint: hp });
      if (def.id === 'knife') game.effects.blood(hp, new THREE.Vector3(fx, 0.2, fz), 10);
      else if (def.id === 'bat' && Math.random() < 0.5) game.effects.blood(hp, new THREE.Vector3(fx, 0.3, fz), 4);
      game.audio?.playAt(def.id === 'knife' ? 'stab' : def.id === 'bat' ? 'bat' : 'punch', hp, 0.9);
      game.events.emit('melee', attacker, c, def.id);
      if (c.dead) game.events.emit('kill', attacker, c, def.id, 'torso');
      hitAny = true;
      if (!strong) break;
    }
    // hitting a car with a bat dents it
    if (!hitAny && def.id === 'bat') {
      for (const v of game.vehicles.list) {
        const [lx, lz] = v.worldToLocal(attacker.pos.x + fx * 1.2, attacker.pos.z + fz * 1.2);
        if (Math.abs(lx) < v.hx + 0.2 && Math.abs(lz) < v.hz + 0.2) { v.damage(15, attacker); v.dent(attacker.pos.x + fx * 1.2, v.pos.y + 0.8, attacker.pos.z + fz * 1.2, 12); game.audio?.playAt('metalhit', v.pos, 0.8); break; }
      }
    }
    return hitAny;
  }

  // ------------------------------------------------------------------ explosions & projectiles
  explosion(pos, radius, damage, source = null, excludeVehicle = null) {
    const game = this.game;
    game.effects.explosion(pos, radius * 0.75);
    game.audio?.playAt('explosion', pos, 1);
    const pd = game.player.vehicle ? game.player.vehicle.pos : game.player.pos;
    const dp = pos.distanceTo(pd);
    game.rig.addShake(clamp(1.2 - dp / 60, 0, 1.2));
    for (const c of game.allCharacters()) {
      if (c.removed) continue;
      const cp = c.ragdolling ? c.ragdoll.center : c.pos;
      const d = cp.distanceTo(pos);
      if (d > radius) continue;
      const k = 1 - d / radius;
      const dir = new THREE.Vector3().subVectors(cp, pos).setY(0).normalize();
      const imp = dir.multiplyScalar(6 + k * 10).add(new THREE.Vector3(0, 4 + k * 7, 0));
      if (c.vehicle) { c.takeDamage(damage * k * 0.4, { type: 'explosion', source }); continue; }
      const wasDead = c.dead;
      c.takeDamage(damage * k, { type: 'explosion', source, impulse: imp, knockdown: true });
      if (c.ragdolling) c.ragdoll.push(0, imp.x, imp.y, imp.z);
      if (c.dead && !wasDead) game.events.emit('kill', source, c, 'explosion', 'torso');
    }
    for (const v of game.vehicles.list) {
      if (v === excludeVehicle || v.exploded) continue;
      const d = v.pos.distanceTo(pos);
      if (d > radius * 1.3) continue;
      const k = 1 - d / (radius * 1.3);
      v.damage(damage * k * 4.5, source);
      const dir = new THREE.Vector3().subVectors(v.pos, pos).setY(0).normalize();
      v.vel.addScaledVector(dir, k * 9 * 1500 / v.mass);
      v.r += (Math.random() - 0.5) * k * 3;
      if (k > 0.4) { v.airborne = true; v.vy = Math.max(v.vy, k * 7); }
      v.dent(pos.x, pos.y, pos.z, 30 * k);
    }
    // props
    for (const col of game.city.propColliders) {
      if (col.broken || !col.breakable) continue;
      const d = Math.hypot(col.x - pos.x, col.z - pos.z);
      if (d < radius * 0.8) {
        game.city.breakProp(col);
        const dir = new THREE.Vector3(col.x - pos.x, 0, col.z - pos.z).normalize().multiplyScalar(12);
        game.effects.propDebris(col, dir);
      }
    }
    game.events.emit('explosion', pos, radius, source);
  }

  fireRocket(shooter, origin, aimDir) {
    const game = this.game;
    const muzzle = shooter.muzzleWorld(new THREE.Vector3());
    const aimHit = this.raycast(origin.x, origin.y, origin.z, aimDir.x, aimDir.y, aimDir.z, 300, shooter);
    const target = aimHit ? aimHit.point : origin.clone().addScaledVector(aimDir, 300);
    const dir = target.sub(muzzle).normalize();
    const mesh = new THREE.Mesh(this.rocketGeo, this.rocketMat);
    mesh.position.copy(muzzle);
    mesh.lookAt(muzzle.clone().add(dir));
    game.scene.add(mesh);
    this.projectiles.push({ type: 'rocket', mesh, pos: muzzle.clone(), vel: dir.multiplyScalar(55), t: 0, owner: shooter });
    game.effects.muzzleFlash(muzzle, dir, true);
    game.audio?.playAt('rpg', muzzle, 1);
    game.events.emit('gunshot', shooter, muzzle, WEAPONS.rpg);
  }

  throwGrenade(thrower, dir) {
    const game = this.game;
    const p = thrower.pos.clone().add(new THREE.Vector3(0, 1.7, 0)).addScaledVector(thrower.forward, 0.4);
    const mesh = new THREE.Mesh(this.grenadeGeo, this.grenadeMat);
    mesh.position.copy(p);
    mesh.castShadow = true;
    game.scene.add(mesh);
    const v = dir.clone().setY(Math.max(dir.y, 0) + 0.35).normalize().multiplyScalar(17);
    this.projectiles.push({ type: 'grenade', mesh, pos: p, vel: v, t: 0, owner: thrower });
    game.audio?.playAt('swoosh', p, 0.6);
  }

  update(dt) {
    const game = this.game;
    for (let i = this.projectiles.length - 1; i >= 0; i--) {
      const pr = this.projectiles[i];
      pr.t += dt;
      if (pr.type === 'rocket') {
        const step = pr.vel.length() * dt;
        const d = pr.vel.clone().normalize();
        const hit = this.raycast(pr.pos.x, pr.pos.y, pr.pos.z, d.x, d.y, d.z, step + 0.3, pr.owner);
        if (hit || pr.t > 5) {
          const at = hit ? hit.point : pr.pos;
          pr.mesh.parent?.remove(pr.mesh);
          this.projectiles.splice(i, 1);
          this.explosion(at, 8, 220, pr.owner);
          if (hit && hit.kind === 'vehicle') hit.obj.health = Math.min(hit.obj.health, -1);
          if (hit && hit.kind === 'heli') hit.obj.hit(9999);
          continue;
        }
        pr.pos.addScaledVector(pr.vel, dt);
        pr.mesh.position.copy(pr.pos);
        game.effects.alphaPool.spawn({ x: pr.pos.x, y: pr.pos.y, z: pr.pos.z, vx: rand(-0.3, 0.3), vy: rand(0, 0.5), vz: rand(-0.3, 0.3), age: 0, life: 1.6, size0: 0.3, size1: 1.8, rot: Math.random() * 6, spin: 0.5, grav: 0, drag: 1, alpha: 0.5, fadeIn: 0.05, fadePow: 1.5, color: [0.8, 0.8, 0.8], color1: null, floor: null });
        game.effects.addPool.spawn({ x: pr.pos.x, y: pr.pos.y, z: pr.pos.z, vx: 0, vy: 0, vz: 0, age: 0, life: 0.08, size0: 0.5, size1: 0.3, rot: 0, spin: 0, grav: 0, drag: 0, alpha: 1, fadeIn: 0.01, fadePow: 1, color: [6, 3, 1], color1: null, floor: null });
      } else if (pr.type === 'grenade') {
        pr.vel.y -= 16 * dt;
        pr.pos.addScaledVector(pr.vel, dt);
        const gh = game.map.groundHeight(pr.pos.x, pr.pos.z) + 0.06;
        if (pr.pos.y < gh) { pr.pos.y = gh; pr.vel.y = Math.abs(pr.vel.y) * 0.35; pr.vel.x *= 0.6; pr.vel.z *= 0.6; if (Math.abs(pr.vel.y) > 1.5) game.audio?.playAt('clink', pr.pos, 0.5); }
        const res = game.collision.resolveCircle(pr.pos.x, pr.pos.z, 0.08, pr.pos.y, 0.1);
        if (res.hit) { pr.vel.x *= -0.4; pr.vel.z *= -0.4; pr.pos.x = res.x; pr.pos.z = res.z; }
        pr.mesh.position.copy(pr.pos);
        pr.mesh.rotation.x += dt * 10;
        if (pr.t > 3) {
          pr.mesh.parent?.remove(pr.mesh);
          this.projectiles.splice(i, 1);
          this.explosion(pr.pos.clone().setY(pr.pos.y + 0.3), 7, 170, pr.owner);
        }
      }
    }
  }
}
