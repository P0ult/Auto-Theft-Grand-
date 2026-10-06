// Third-person orbit camera (on foot), over-the-shoulder aim camera, vehicle chase camera, cinematic shots.
import * as THREE from 'three';
import { clamp, damp, dampAngle, wrapAngle, lerp } from '../core/utils.js';

const _v = new THREE.Vector3(), _t = new THREE.Vector3(), _d = new THREE.Vector3();
const _f = new THREE.Vector3(), _u = new THREE.Vector3(), _o = new THREE.Vector3(), _c = new THREE.Vector3();

export class CameraRig {
  constructor(camera, game) {
    this.cam = camera;
    this.game = game;
    this.yaw = Math.PI;
    this.pitch = -0.15;
    this.dist = 4.3;
    this.curDist = 4.3;
    this.pivot = new THREE.Vector3();
    this.pos = new THREE.Vector3();
    this.mode = 'foot';
    this.aimBlend = 0;
    this.shake = 0;
    this.fovBase = 62;
    this.fov = 62;
    this.scopeBlend = 0;
    this.scopeFov = 16;
    this.lastLookInput = 0;
    this.vehYawOffset = 0;
    this.vehPitch = -0.12;
    this.lookBehind = false;
    this.cine = null;
    this.vehDist = 7.5;
    this.vehicleCamIndex = 0;
    this.time = 0;
    this.cineHeld = false; // hold X / pad B in a vehicle: the cinematic camera
    this.cv = null;
  }

  addShake(a) { this.shake = Math.min(1.2, this.shake + a); }

  get forwardYaw() { return this.yaw + Math.PI; } // direction the camera looks (horizontal)

  lookDir(out = new THREE.Vector3()) { return this.cam.getWorldDirection(out); }

  setCinematic(pos, target, fov = 50) {
    this.cine = { pos: pos.clone(), target: target.clone(), fov };
  }
  clearCinematic() { this.cine = null; }

  update(dt, input, player) {
    this.time += dt;
    const cam = this.cam;
    let [dx, dy] = input && input.enabled ? input.lookDelta() : [0, 0];
    // finer look through a scope
    if (this.scopeBlend > 0.01) { const k = lerp(1, this.fov / 60, this.scopeBlend); dx *= k; dy *= k; }
    const moved = Math.abs(dx) + Math.abs(dy) > 0.0005;
    if (moved) this.lastLookInput = this.time;

    cam.up.set(0, 1, 0);
    if (this.cine) {
      cam.position.lerp(this.cine.pos, 1 - Math.exp(-dt * 3));
      cam.lookAt(this.cine.target);
      this.fov = damp(this.fov, this.cine.fov, 4, dt);
      this._finish(dt);
      return;
    }

    const veh = player.vehicle;
    if (veh && (veh.def.kind === 'plane' || veh.def.kind === 'jet') && !player.dead) { this._flightCam(dt, veh); return; }
    const cineOn = !!veh && this.cineHeld && !veh.def.tank && !player.dead;
    if (cineOn !== !!this._cineBars) { this._cineBars = cineOn; document.body.classList.toggle('cine-cam', cineOn); if (!cineOn) this.cv = null; }
    if (cineOn) { this._vehCine(dt, veh); return; }
    this.flightOff = null;
    if (veh && veh.def.tank) {
      // tank: free world-space orbit (the turret follows where you look)
      this.yaw = wrapAngle(this.yaw - dx);
      this.pitch = clamp(this.pitch - dy, -0.8, 0.28);
      const size = veh.def.camDist || 12;
      this.dist = [size, size * 1.4, size * 0.62][this.vehicleCamIndex % 3];
      this.pivot.set(veh.pos.x, veh.pos.y + (veh.def.camHeight || 3), veh.pos.z);
      this.fovBase = 60;
    } else if (veh) {
      // vehicle chase camera
      const vyaw = veh.yaw;
      const speed = veh.speed;
      if (moved) { this.vehYawOffset = wrapAngle(this.vehYawOffset - dx); this.vehPitch = clamp(this.vehPitch - dy, -0.9, 0.35); }
      else if (this.time - this.lastLookInput > 1.2 && Math.abs(speed) > 2) {
        this.vehYawOffset = dampAngle(this.vehYawOffset, 0, 2.5, dt);
        this.vehPitch = damp(this.vehPitch, -0.12, 2, dt);
      }
      // follow velocity direction a bit when drifting
      const velYaw = Math.hypot(veh.vel.x, veh.vel.z) > 3 ? Math.atan2(veh.vel.x, veh.vel.z) : vyaw;
      const drift = wrapAngle(velYaw - vyaw);
      let baseYaw = vyaw + clamp(drift, -0.6, 0.6) * 0.45 + (speed < -1 ? 0 : 0);
      if (this.lookBehind) baseYaw += Math.PI;
      const targetYaw = baseYaw + this.vehYawOffset + Math.PI; // camera sits behind
      this.yaw = dampAngle(this.yaw, targetYaw, moved ? 30 : 6, dt);
      this.pitch = damp(this.pitch, this.vehPitch, 8, dt);
      const size = veh.def.camDist || 7.5;
      const dists = [size, size * 1.45, size * 0.6];
      this.dist = dists[this.vehicleCamIndex % dists.length];
      this.pivot.set(veh.pos.x, veh.pos.y + (veh.def.camHeight || 1.6), veh.pos.z);
      this.fovBase = 64 + clamp(Math.abs(speed) / 45, 0, 1) * 14 + (this.boostFov || 0);
    } else {
      // on-foot orbit
      this.yaw = wrapAngle(this.yaw - dx);
      this.pitch = clamp(this.pitch - dy, -1.35, 0.9);
      const aiming = player.aiming && !player.dead;
      this.aimBlend = damp(this.aimBlend, aiming ? 1 : 0, 12, dt);
      // sniper scope: the camera moves into the eye and zooms
      const scoped = aiming && !!player.weaponDef?.scope && !player.swimming;
      this.scopeBlend = damp(this.scopeBlend, scoped ? 1 : 0, scoped ? 16 : 22, dt);
      const hide = this.scopeBlend > 0.55;
      if (hide !== !!this._scopeHid) { this._scopeHid = hide; player.root.visible = !hide; }
      const hy = player.swimming ? 0.6 : player.crouching ? 1.15 : 1.62;
      const tp = player.ragdolling ? player.ragdoll.center : player.pos;
      const py = player.ragdolling ? tp.y + 0.6 : tp.y + hy;
      _t.set(tp.x, py, tp.z);
      this.pivot.lerp(_t, player.ragdolling ? 1 - Math.exp(-dt * 5) : 1);
      if (!player.ragdolling) this.pivot.copy(_t);
      this.dist = player.chute ? 9 : lerp(lerp(4.3, 2.0, this.aimBlend), 0.02, this.scopeBlend);
      if (player.chute) this.pivot.y += 2.4;
      this.fovBase = lerp(lerp(64, 48, this.aimBlend) + (player.sprinting ? 4 : 0), this.scopeFov, this.scopeBlend * this.scopeBlend);
    }

    // desired camera position
    const cp = Math.cos(this.pitch), sp = Math.sin(this.pitch);
    _d.set(Math.sin(this.yaw) * cp, -sp, Math.cos(this.yaw) * cp); // from pivot toward camera
    // over-the-shoulder offset (to the right of view direction)
    const side = veh ? 0 : lerp(lerp(0.35, 0.62, this.aimBlend), 0, this.scopeBlend);
    const rx = -Math.cos(this.yaw), rz = Math.sin(this.yaw);
    const piv = _v.copy(this.pivot);
    piv.x += rx * side * -1; piv.z += rz * side * -1;
    if (!veh) piv.y += lerp(0.05, 0.12, this.aimBlend);
    // collision
    const col = this.game.collision;
    let want = this.dist;
    const hit = col.raycast(piv.x, piv.y, piv.z, _d.x, _d.y, _d.z, want + 0.3, { ignoreProps: true, ignoreSoft: true });
    if (hit) want = Math.max(0.35, hit.t - 0.3);
    this.curDist = want < this.curDist ? want : damp(this.curDist, want, 4, dt);
    this.pos.copy(piv).addScaledVector(_d, this.curDist);
    const gh = this.game.map.groundHeight(this.pos.x, this.pos.z);
    if (this.pos.y < gh + 0.25) this.pos.y = gh + 0.25;
    cam.position.copy(this.pos);
    // look at a point ahead of pivot
    _t.copy(piv).addScaledVector(_d, -10);
    cam.lookAt(_t);
    this.fov = damp(this.fov, this.fovBase, 6, dt);
    this._finish(dt);
  }

  // Planes & jets: chase camera locked behind the nose (with lag in the aircraft's frame) that banks with it.
  _flightCam(dt, veh) {
    const cam = this.cam;
    const q = veh.quat;
    const f = _f.set(0, 0, 1).applyQuaternion(q), u = _u.set(0, 1, 0).applyQuaternion(q);
    const cg = veh.cg(_c);
    const size = veh.def.camDist || 15;
    const dist = [size, size * 1.6, size * 0.5][this.vehicleCamIndex % 3];
    const back = this.lookBehind ? -1 : 1;
    _o.copy(f).multiplyScalar(-dist * back);
    _o.y *= 0.55;
    _o.addScaledVector(u, dist * 0.17);
    _o.y += dist * 0.08;
    if (!this.flightOff) this.flightOff = _o.clone();
    this.flightOff.lerp(_o, 1 - Math.exp(-dt * 7));
    this.pos.copy(cg).add(this.flightOff);
    const gh = this.game.map.groundHeight(this.pos.x, this.pos.z);
    if (this.pos.y < gh + 1.2) this.pos.y = gh + 1.2;
    cam.position.copy(this.pos);
    cam.up.set(0, 1, 0).lerp(u, 0.4).normalize();
    _t.copy(cg).addScaledVector(f, 300 * back);
    cam.lookAt(_t);
    this.yaw = veh.yaw + Math.PI;
    this.pitch = -0.15;
    this.curDist = dist;
    const spd = Math.max(0, veh.forwardSpeed);
    this.fov = damp(this.fov, 60 + clamp(spd / 160, 0, 1) * 16, 4, dt);
    this._finish(dt);
  }

  // GTA V's cinematic camera: cuts between a roadside camera the car blasts past, a low tracking shot
  // alongside, a high chase and a head-on shot looking back at the car.
  _vehCine(dt, veh) {
    const cam = this.cam, g = this.game;
    const st = this.cv || (this.cv = { shot: -1, t: 0, len: 0, n: 0, anchor: new THREE.Vector3(), side: 1 });
    st.t += dt;
    const sp = Math.hypot(veh.vel.x, veh.vel.z);
    const hy = sp > 2 ? Math.atan2(veh.vel.x, veh.vel.z) : veh.yaw;
    const fx = Math.sin(hy), fz = Math.cos(hy), rx = -fz, rz = fx;
    const h = Math.max(1, (veh.def.camHeight || 1.6) * 0.55);
    const size = Math.max(4.5, veh.def.L || 4.5);
    const tgt = _t.set(veh.pos.x, veh.pos.y + h, veh.pos.z);
    const col = g.collision;
    // next shot
    let cut = st.shot < 0 || st.t > st.len;
    if (st.shot === 0) {
      const along = (veh.pos.x - st.anchor.x) * fx + (veh.pos.z - st.anchor.z) * fz;
      if (along > 22 + size || Math.hypot(veh.pos.x - st.anchor.x, veh.pos.z - st.anchor.z) > 90) cut = true;
    }
    if (cut) {
      st.n++;
      const order = sp > 6 ? [0, 1, 2, 3] : [1, 2, 3];
      st.shot = order[st.n % order.length];
      st.t = 0; st.len = 4 + Math.random() * 2.5; st.side = Math.random() < 0.5 ? 1 : -1;
      if (st.shot === 0) {
        // a roadside camera up ahead, a little to one side
        // (a few tries: the other side of the road, closer in; no clear view at all and it's the tracking shot)
        let ok = false;
        for (const [k, side] of [[1, st.side], [1, -st.side], [0.6, st.side], [0.6, -st.side]]) {
          const ahead = clamp(sp * 2.4, 16, 55) * k, off = (5 + Math.random() * 4) * k;
          st.anchor.set(veh.pos.x + fx * ahead + rx * off * side, 0, veh.pos.z + fz * ahead + rz * off * side);
          st.anchor.y = Math.max(g.map.groundHeight(st.anchor.x, st.anchor.z), veh.pos.y - 2) + 1.1 + Math.random() * 1.6;
          // (to just short of the car: the ray would otherwise stop on the car itself)
          _d.subVectors(st.anchor, tgt).normalize();
          if (col.lineOfSight(st.anchor.x, st.anchor.y, st.anchor.z, tgt.x + _d.x * 3.5, tgt.y + 0.4 + _d.y * 3.5, tgt.z + _d.z * 3.5)) { ok = true; break; }
        }
        st.len = 9;
        if (!ok) { st.shot = 1; st.len = 4.5; }
      }
      st.fresh = true;
    }
    const want = _o;
    let look = _c.copy(tgt), fov = 45, follow = 7;
    switch (st.shot) {
      case 0: want.copy(st.anchor); fov = 36; follow = 0; break;
      case 1: want.set(veh.pos.x + rx * (size * 1.1) * st.side + fx * size * 0.35, tgt.y + 0.1, veh.pos.z + rz * (size * 1.1) * st.side + fz * size * 0.35); look.addScaledVector(_f.set(fx, 0, fz), size * 0.3); fov = 44; follow = 9; break;
      case 2: want.set(veh.pos.x - fx * size * 3.4 + rx * 3 * st.side, tgt.y + size * 1.9, veh.pos.z - fz * size * 3.4 + rz * 3 * st.side); look.addScaledVector(_f.set(fx, 0, fz), size); fov = 40; follow = 4; break;
      default: want.set(veh.pos.x + fx * size * 2.8 + rx * 1.4 * st.side, tgt.y + 0.45, veh.pos.z + fz * size * 2.8 + rz * 1.4 * st.side); fov = 46; follow = 12; break;
    }
    // keep a moving camera out of walls (pull in towards the car)
    if (st.shot !== 0) {
      _d.subVectors(want, tgt); const L = _d.length(); _d.divideScalar(L || 1);
      const hit = col.raycast(tgt.x, tgt.y, tgt.z, _d.x, _d.y, _d.z, L + 0.3, { ignoreProps: true, ignoreSoft: true });
      if (hit) want.copy(tgt).addScaledVector(_d, Math.max(1.5, hit.t - 0.4));
    }
    if (st.fresh || follow === 0) { this.pos.copy(want); st.fresh = false; }
    else this.pos.lerp(want, 1 - Math.exp(-dt * follow));
    const gh = g.map.groundHeight(this.pos.x, this.pos.z);
    if (this.pos.y < gh + 0.35) this.pos.y = gh + 0.35;
    cam.position.copy(this.pos);
    cam.lookAt(look);
    this.fov = fov;
    // the chase camera picks up from behind the car when you let go
    this.yaw = veh.yaw + Math.PI;
    this._finish(dt);
  }

  _finish(dt) {
    const cam = this.cam;
    if (this.shake > 0.001) {
      const s = this.shake * this.shake * 0.08;
      const t = this.time * 40;
      cam.rotation.x += (Math.sin(t * 1.3) + Math.sin(t * 2.7)) * s;
      cam.rotation.y += (Math.sin(t * 1.7 + 3) + Math.sin(t * 3.1)) * s;
      cam.rotation.z += Math.sin(t * 2.1 + 1) * s * 0.5;
      this.shake = Math.max(0, this.shake - dt * 1.8);
    }
    if (Math.abs(cam.fov - this.fov) > 0.01) { cam.fov = this.fov; cam.updateProjectionMatrix(); }
  }
}
