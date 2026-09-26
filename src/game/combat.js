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
    this.missileGeo = new THREE.CylinderGeometry(0.1, 0.1, 2.4, 8).rotateX(Math.PI / 2);
    this.missileMat = new THREE.MeshStandardMaterial({ color: 0xe8e8e2, roughness: 0.4, metalness: 0.3 });
    this.shellGeo = new THREE.CylinderGeometry(0.07, 0.07, 0.9, 6).rotateX(Math.PI / 2);
    this.shellMat = new THREE.MeshBasicMaterial({ color: new THREE.Color(4, 2.6, 1.2) });
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
      if (i === 0) game.net?.onShot(shooter, def, muzzle, end);
      if (hit) { anyHit = true; this.applyHit(hit, def, shooter, d); }
    }
    game.effects.muzzleFlash(muzzle, base, def.id === 'shotgun');
    game.audio?.playAt(def.sound, muzzle, shooter.isPlayer ? 1 : 0.8, { gun: true });
    game.events.emit('gunshot', shooter, muzzle, def);
    return anyHit;
  }

  applyHit(hit, def, shooter, dir) {
    const game = this.game;
    const ch = game.cheatsOn;
    if (shooter?.isPlayer && ch && (ch.explosive || ch.oneHit)) {
      if (ch.explosive && hit.point) this.explosion(hit.point.clone(), 3.2, 80, shooter);
      if (ch.oneHit) def = { ...def, damage: def.damage * 10 };
    }
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
      v.damage(def.damage * 0.9 * (v.def.bulletMul ?? 1), shooter);
      game.effects.impact(hit.point, hit.normal, 'metal');
      game.audio?.playAt('bulletmetal', hit.point, 0.5);
      // tires & fuel: small chance to ignite when already damaged
      if (v.health < 250 && Math.random() < 0.05 && !v.def.tank) v.health = 0;
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
      // NPC melee is toned down so a group can't flatten the player in a second or two
      if (!attacker.isPlayer) dmg *= c.isPlayer ? 0.42 : 0.8;
      if (c.ragdolling) dmg *= 1.3;
      const strong = act === 'kick' || def.id === 'bat' || (act === 'cross' && Math.random() < 0.25);
      const imp = new THREE.Vector3(fx * (strong ? 3.5 : 1.5), strong ? 1.5 : 0.5, fz * (strong ? 3.5 : 1.5));
      const hp = c.chestPos;
      c.takeDamage(dmg, { part: act === 'cross' && Math.random() < 0.3 && attacker.isPlayer ? 'head' : 'torso', headMul: 1.5, source: attacker, type: 'melee', impulse: imp, knockdown: strong && !c.isPlayer, hitPoint: hp });
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
    if (this.visualOnly) return; // another player's vehicle blowing up: their client deals the damage
    game.net?.onExplosion(pos, radius, source);
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
      v.damage(damage * k * 4.5 * (v.def.blastMul ?? 1), source);
      if (v.def.kind) continue; // aircraft & tanks don't get tossed around
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

  // Vehicle-mounted guns (jet cannon, helicopter minigun): hitscan from a muzzle with tracers.
  vehicleGun(shooter, muzzle, dir, def) {
    const game = this.game;
    const d = dir.clone();
    const sp = def.spread || 0;
    d.x += rand(-sp, sp); d.y += rand(-sp, sp); d.z += rand(-sp, sp);
    d.normalize();
    const hit = this.raycast(muzzle.x, muzzle.y, muzzle.z, d.x, d.y, d.z, def.range, shooter);
    const end = hit ? hit.point : muzzle.clone().addScaledVector(d, def.range);
    if (Math.random() < 0.7) game.effects.tracers.add(muzzle, end);
    if (hit) this.applyHit(hit, def, shooter || { isPlayer: false, damageMul: 1 }, d);
    game.effects.muzzleFlash(muzzle, d, false);
    game.audio?.playAt(def.sound || 'smg', muzzle, shooter?.isPlayer ? 0.9 : 0.7, { gun: true });
    game.events.emit('gunshot', shooter, muzzle, def);
  }

  // Rockets (straight), missiles (homing, accelerating) and tank shells (fast, slight drop).
  fireProjectile(shooter, kind, pos, dir, opts = {}) {
    const game = this.game;
    const geo = kind === 'shell' ? this.shellGeo : kind === 'missile' ? this.missileGeo : this.rocketGeo;
    const mat = kind === 'shell' ? this.shellMat : kind === 'missile' ? this.missileMat : this.rocketMat;
    const mesh = new THREE.Mesh(geo, mat);
    mesh.position.copy(pos);
    if (kind === 'rocket') mesh.scale.setScalar(1.8);
    game.scene.add(mesh);
    const vel = dir.clone().multiplyScalar(opts.speed ?? 80);
    if (opts.inherit) vel.addScaledVector(opts.inherit, 0.9);
    this.projectiles.push({
      type: 'rocket', kind, mesh, pos: pos.clone(), vel, t: 0, owner: shooter,
      radius: opts.radius ?? 8, damage: opts.damage ?? 220, gravity: opts.gravity ?? 0, life: opts.life ?? 5,
      target: opts.target || null, turn: opts.turn ?? 0, accel: opts.accel ?? 0, maxSpeed: opts.maxSpeed ?? 0,
    });
    game.effects.muzzleFlash(pos, dir, true);
    if (kind !== 'shell') game.audio?.playAt('rpg', pos, 0.9);
    game.events.emit('gunshot', shooter, pos, WEAPONS.rpg);
  }

  // Best homing target in a narrow cone ahead: the police helicopter, occupied vehicles, aircraft.
  lockTarget(from, dir, exclude = null, maxDist = 1000) {
    const game = this.game;
    let best = null, bs = -Infinity;
    const consider = (obj, p) => {
      const dx = p.x - from.x, dy = p.y - from.y, dz = p.z - from.z;
      const d = Math.hypot(dx, dy, dz);
      if (d < 15 || d > maxDist) return;
      const c = (dx * dir.x + dy * dir.y + dz * dir.z) / d;
      if (c < 0.97) return;
      const score = c * 2 - d / maxDist;
      if (score > bs) { bs = score; best = obj; }
    };
    const heli = game.police?.heli;
    if (heli && !heli.down && !heli.done) consider(heli, heli.pos);
    for (const v of game.vehicles.list) {
      if (v === exclude || v.removed || v.isWrecked) continue;
      if (!v.driver && !v.def.aircraft) continue;
      consider(v, v.cg ? v.cg() : v.pos);
    }
    return best;
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
        // homing: steer toward the locked target, speed up to the motor's max
        const tg = pr.target;
        let near = false;
        if (tg && !tg.removed && !tg.exploded && !tg.done && !tg.down) {
          const tp = tg.cg ? tg.cg() : tg.def ? tg.pos.clone().setY(tg.pos.y + tg.def.H / 2) : tg.pos.clone();
          const want = tp.sub(pr.pos);
          near = want.length() < 3.5;
          want.normalize();
          const sp = pr.vel.length();
          const cur = pr.vel.clone().divideScalar(sp);
          const ang = cur.angleTo(want);
          if (ang > 1e-4) cur.lerp(want, Math.min(1, (pr.turn * dt) / ang)).normalize();
          pr.vel.copy(cur).multiplyScalar(sp);
        }
        if (pr.accel) { const sp = pr.vel.length(); pr.vel.multiplyScalar(Math.min(pr.maxSpeed || sp, sp + pr.accel * dt) / sp); }
        if (pr.gravity) pr.vel.y -= pr.gravity * dt;
        const step = pr.vel.length() * dt;
        const d = pr.vel.clone().normalize();
        const hit = this.raycast(pr.pos.x, pr.pos.y, pr.pos.z, d.x, d.y, d.z, step + 0.3, pr.owner);
        if (hit || near || pr.t > (pr.life ?? 5)) {
          const at = hit ? hit.point : pr.pos;
          pr.mesh.parent?.remove(pr.mesh);
          this.projectiles.splice(i, 1);
          this.explosion(at, pr.radius ?? 8, pr.damage ?? 220, pr.owner);
          const hv = hit && hit.kind === 'vehicle' ? hit.obj : near && tg?.def ? tg : null;
          if (hv) { if (hv.def.tank) hv.damage(900, pr.owner); else hv.health = Math.min(hv.health, -1); if (hv.def.aircraft) hv.explode(); }
          if (hit && hit.kind === 'heli') hit.obj.hit(9999);
          else if (near && tg && tg.hit) tg.hit(9999);
          continue;
        }
        pr.pos.addScaledVector(pr.vel, dt);
        pr.mesh.position.copy(pr.pos);
        pr.mesh.lookAt(pr.pos.x + d.x, pr.pos.y + d.y, pr.pos.z + d.z);
        if (pr.kind === 'shell') continue;
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
