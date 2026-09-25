// Third-person orbit camera (on foot), over-the-shoulder aim camera, vehicle chase camera, cinematic shots.
import * as THREE from 'three';
import { clamp, damp, dampAngle, wrapAngle, lerp } from '../core/utils.js';

const _v = new THREE.Vector3(), _t = new THREE.Vector3(), _d = new THREE.Vector3();

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
    this.lastLookInput = 0;
    this.vehYawOffset = 0;
    this.vehPitch = -0.12;
    this.lookBehind = false;
    this.cine = null;
    this.vehDist = 7.5;
    this.vehicleCamIndex = 0;
    this.time = 0;
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
    const moved = Math.abs(dx) + Math.abs(dy) > 0.0005;
    if (moved) this.lastLookInput = this.time;

    if (this.cine) {
      cam.position.lerp(this.cine.pos, 1 - Math.exp(-dt * 3));
      cam.lookAt(this.cine.target);
      this.fov = damp(this.fov, this.cine.fov, 4, dt);
      this._finish(dt);
      return;
    }

    const veh = player.vehicle;
    if (veh) {
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
      this.fovBase = 64 + clamp(Math.abs(speed) / 45, 0, 1) * 14;
    } else {
      // on-foot orbit
      this.yaw = wrapAngle(this.yaw - dx);
      this.pitch = clamp(this.pitch - dy, -1.35, 0.9);
      const aiming = player.aiming && !player.dead;
      this.aimBlend = damp(this.aimBlend, aiming ? 1 : 0, 12, dt);
      const hy = player.swimming ? 0.6 : player.crouching ? 1.15 : 1.62;
      const tp = player.ragdolling ? player.ragdoll.center : player.pos;
      const py = player.ragdolling ? tp.y + 0.6 : tp.y + hy;
      _t.set(tp.x, py, tp.z);
      this.pivot.lerp(_t, player.ragdolling ? 1 - Math.exp(-dt * 5) : 1);
      if (!player.ragdolling) this.pivot.copy(_t);
      this.dist = lerp(4.3, 2.0, this.aimBlend);
      this.fovBase = lerp(64, 48, this.aimBlend) + (player.sprinting ? 4 : 0);
    }

    // desired camera position
    const cp = Math.cos(this.pitch), sp = Math.sin(this.pitch);
    _d.set(Math.sin(this.yaw) * cp, -sp, Math.cos(this.yaw) * cp); // from pivot toward camera
    // over-the-shoulder offset (to the right of view direction)
    const side = veh ? 0 : lerp(0.35, 0.62, this.aimBlend);
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
