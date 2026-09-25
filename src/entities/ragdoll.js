// Verlet particle ragdoll mapped back onto the humanoid skeleton.
import * as THREE from 'three';
import { B } from './humanoid.js';

const P_DEF = [
  // [bone, localOffset]
  [B.hips, [0, 0, 0]], [B.chest, [0, 0, 0]], [B.neck, [0, 0, 0]], [B.head, [0, 0.2, 0]],
  [B.lUpperArm, [0, 0, 0]], [B.lForearm, [0, 0, 0]], [B.lHand, [0, -0.08, 0]],
  [B.rUpperArm, [0, 0, 0]], [B.rForearm, [0, 0, 0]], [B.rHand, [0, -0.08, 0]],
  [B.lThigh, [0, 0, 0]], [B.lShin, [0, 0, 0]], [B.lFoot, [0, 0, 0]], [B.lFoot, [0, -0.04, 0.18]],
  [B.rThigh, [0, 0, 0]], [B.rShin, [0, 0, 0]], [B.rFoot, [0, 0, 0]], [B.rFoot, [0, -0.04, 0.18]],
];
const RADIUS = [0.12, 0.13, 0.07, 0.1, 0.06, 0.05, 0.05, 0.06, 0.05, 0.05, 0.07, 0.06, 0.05, 0.04, 0.07, 0.06, 0.05, 0.04];
const LINKS = [
  [0, 1], [1, 2], [2, 3], [1, 4], [1, 7], [4, 7], [2, 4], [2, 7], [0, 10], [0, 14], [10, 14], [4, 10], [7, 14], [4, 14], [7, 10], [1, 10], [1, 14], [1, 3], [4, 3], [7, 3],
  [4, 5], [5, 6], [7, 8], [8, 9],
  [10, 11], [11, 12], [12, 13], [14, 15], [15, 16], [16, 17], [11, 13], [15, 17],
];
const MINS = [[4, 6, 0.9], [7, 9, 0.9], [10, 12, 0.72], [14, 16, 0.72], [3, 0, 0.9], [6, 1, 0.5], [9, 1, 0.5], [12, 14, 0.3], [16, 10, 0.3], [11, 15, 0.12]];

const _v = new THREE.Vector3(), _a = new THREE.Vector3(), _b = new THREE.Vector3(), _c = new THREE.Vector3();
const _m = new THREE.Matrix4(), _q = new THREE.Quaternion(), _qi = new THREE.Quaternion();
const DOWN = new THREE.Vector3(0, -1, 0);
const TOE_REST = new THREE.Vector3(0, -0.04, 0.18).normalize();

export class Ragdoll {
  constructor(char, world) {
    this.char = char;
    this.world = world; // {map, collision}
    this.n = P_DEF.length;
    this.pos = new Float32Array(this.n * 3);
    this.prev = new Float32Array(this.n * 3);
    this.rest = [];
    this.mins = [];
    this.active = false;
    this.sleepT = 0;
    this.settled = false;
    this.time = 0;
    this.worldQ = [];
    for (let i = 0; i < 17; i++) this.worldQ.push(new THREE.Quaternion());
  }

  start(vel, impulse = null, impulsePoint = -1) {
    const bones = this.char.bones;
    this.char.root.updateMatrixWorld(true);
    for (let i = 0; i < this.n; i++) {
      const [bi, off] = P_DEF[i];
      _v.set(off[0], off[1], off[2]).applyMatrix4(bones[bi].matrixWorld);
      this.pos[i * 3] = _v.x; this.pos[i * 3 + 1] = _v.y; this.pos[i * 3 + 2] = _v.z;
    }
    // prev positions encode initial velocity (dt ~ 1/60)
    const h = 1 / 60;
    for (let i = 0; i < this.n; i++) {
      let vx = vel.x, vy = vel.y, vz = vel.z;
      if (impulse) {
        const w = impulsePoint < 0 ? 1 : Math.max(0.25, 1 - this._dist(i, impulsePoint) * 0.9);
        vx += impulse.x * w; vy += impulse.y * w; vz += impulse.z * w;
      }
      // slight random spin for variety
      vx += (Math.random() - 0.5) * 0.4; vz += (Math.random() - 0.5) * 0.4;
      this.prev[i * 3] = this.pos[i * 3] - vx * h;
      this.prev[i * 3 + 1] = this.pos[i * 3 + 1] - vy * h;
      this.prev[i * 3 + 2] = this.pos[i * 3 + 2] - vz * h;
    }
    this.rest = LINKS.map(([a, b]) => this._dist(a, b));
    this.mins = MINS.map(([a, b, f]) => [a, b, this._dist(a, b) * f]);
    this.active = true; this.settled = false; this.sleepT = 0; this.time = 0;
  }

  push(i, vx, vy, vz) {
    const h = 1 / 60;
    for (let k = 0; k < this.n; k++) {
      const w = Math.max(0.15, 1 - this._dist(k, i) * 1.2);
      this.prev[k * 3] -= vx * h * w; this.prev[k * 3 + 1] -= vy * h * w; this.prev[k * 3 + 2] -= vz * h * w;
    }
    this.settled = false; this.sleepT = 0;
  }

  _dist(a, b) { const p = this.pos; return Math.hypot(p[a * 3] - p[b * 3], p[a * 3 + 1] - p[b * 3 + 1], p[a * 3 + 2] - p[b * 3 + 2]); }

  nearestParticle(x, y, z) {
    let best = 0, bd = Infinity;
    for (let i = 0; i < this.n; i++) { const d = (this.pos[i * 3] - x) ** 2 + (this.pos[i * 3 + 1] - y) ** 2 + (this.pos[i * 3 + 2] - z) ** 2; if (d < bd) { bd = d; best = i; } }
    return best;
  }

  get center() { return _c.set(this.pos[0], this.pos[1], this.pos[2]); }

  update(dt) {
    if (!this.active) return;
    this.time += dt;
    if (this.settled) return;
    const steps = 2;
    const h = Math.min(dt, 1 / 30) / steps;
    const p = this.pos, q = this.prev, n = this.n;
    const map = this.world.map, col = this.world.collision;
    let maxV = 0;
    for (let s = 0; s < steps; s++) {
      for (let i = 0; i < n; i++) {
        const k = i * 3;
        const x = p[k], y = p[k + 1], z = p[k + 2];
        let vx = (x - q[k]) * 0.995, vy = (y - q[k + 1]) * 0.995, vz = (z - q[k + 2]) * 0.995;
        q[k] = x; q[k + 1] = y; q[k + 2] = z;
        p[k] = x + vx; p[k + 1] = y + vy - 9.81 * h * h; p[k + 2] = z + vz;
      }
      for (let it = 0; it < 8; it++) {
        for (let c = 0; c < LINKS.length; c++) {
          const [a, b] = LINKS[c];
          const ka = a * 3, kb = b * 3;
          const dx = p[kb] - p[ka], dy = p[kb + 1] - p[ka + 1], dz = p[kb + 2] - p[ka + 2];
          const d = Math.sqrt(dx * dx + dy * dy + dz * dz) || 1e-6;
          const diff = (d - this.rest[c]) / d * 0.5;
          p[ka] += dx * diff; p[ka + 1] += dy * diff; p[ka + 2] += dz * diff;
          p[kb] -= dx * diff; p[kb + 1] -= dy * diff; p[kb + 2] -= dz * diff;
        }
        for (const [a, b, mn] of this.mins) {
          const ka = a * 3, kb = b * 3;
          const dx = p[kb] - p[ka], dy = p[kb + 1] - p[ka + 1], dz = p[kb + 2] - p[ka + 2];
          const d = Math.sqrt(dx * dx + dy * dy + dz * dz) || 1e-6;
          if (d < mn) {
            const diff = (d - mn) / d * 0.5;
            p[ka] += dx * diff; p[ka + 1] += dy * diff; p[ka + 2] += dz * diff;
            p[kb] -= dx * diff; p[kb + 1] -= dy * diff; p[kb + 2] -= dz * diff;
          }
        }
        // collisions
        for (let i = 0; i < n; i++) {
          const k = i * 3;
          const r = RADIUS[i];
          const gh = col.floorHeight(p[k], p[k + 2], q[k + 1]) + r;
          if (p[k + 1] < gh) {
            p[k + 1] = gh;
            // friction
            const fr = 0.6;
            q[k] = p[k] - (p[k] - q[k]) * (1 - fr);
            q[k + 2] = p[k + 2] - (p[k + 2] - q[k + 2]) * (1 - fr);
            if (q[k + 1] < p[k + 1]) q[k + 1] = p[k + 1] + (p[k + 1] - q[k + 1]) * 0.1;
          }
          if (it === 7 && (i === 0 || i === 1 || i === 3 || i === 6 || i === 9 || i === 12 || i === 16)) {
            const res = col.resolveCircle(p[k], p[k + 2], r + 0.05, p[k + 1] - 0.1, 0.2);
            if (res.hit) { p[k] = res.x; p[k + 2] = res.z; }
          }
        }
      }
      for (let i = 0; i < n * 3; i++) maxV = Math.max(maxV, Math.abs(p[i] - q[i]) / h);
    }
    if (maxV < 0.35 && this.time > 0.6) { this.sleepT += dt; if (this.sleepT > 0.8) this.settled = true; }
    else this.sleepT = 0;
  }

  _pv(i, out) { return out.set(this.pos[i * 3], this.pos[i * 3 + 1], this.pos[i * 3 + 2]); }

  _frame(xAxis, yAxis, out) {
    const Y = yAxis.normalize();
    const X = xAxis.addScaledVector(Y, -xAxis.dot(Y)).normalize();
    const Z = _c.crossVectors(X, Y).normalize();
    _m.makeBasis(X, Y, Z);
    return out.setFromRotationMatrix(_m);
  }

  _limb(parentQ, from, to, restDir, out) {
    _a.copy(this._pv(to, _b)).sub(this._pv(from, _v)).normalize();
    _qi.copy(parentQ).invert();
    _a.applyQuaternion(_qi);
    return out.setFromUnitVectors(restDir, _a);
  }

  apply() {
    const bones = this.char.bones;
    const root = this.char.root;
    const W = this.worldQ;
    const rootQ = root.getWorldQuaternion(new THREE.Quaternion());
    const tmp1 = new THREE.Vector3(), tmp2 = new THREE.Vector3();
    // hips
    this._frame(this._pv(10, tmp1).sub(this._pv(14, _v)), this._pv(1, tmp2).sub(this._pv(0, _b)), W[B.hips]);
    // chest
    this._frame(this._pv(4, tmp1).sub(this._pv(7, _v)), this._pv(2, tmp2).sub(this._pv(1, _b)), W[B.chest]);
    W[B.spine].slerpQuaternions(W[B.hips], W[B.chest], 0.5);
    this._frame(this._pv(4, tmp1).sub(this._pv(7, _v)), this._pv(3, tmp2).sub(this._pv(2, _b)), W[B.neck]);
    W[B.head].copy(W[B.neck]);
    // local rotations for torso chain
    const setLocal = (bi, parentWorld) => { bones[bi].quaternion.copy(_qi.copy(parentWorld).invert().multiply(W[bi])); };
    setLocal(B.hips, rootQ);
    setLocal(B.spine, W[B.hips]);
    setLocal(B.chest, W[B.spine]);
    setLocal(B.neck, W[B.chest]);
    bones[B.head].quaternion.identity();
    // limbs
    const limb = (bi, parentBi, from, to, rest = DOWN) => {
      this._limb(W[parentBi], from, to, rest, bones[bi].quaternion);
      W[bi].copy(W[parentBi]).multiply(bones[bi].quaternion);
    };
    limb(B.lUpperArm, B.chest, 4, 5); limb(B.lForearm, B.lUpperArm, 5, 6); bones[B.lHand].quaternion.identity();
    limb(B.rUpperArm, B.chest, 7, 8); limb(B.rForearm, B.rUpperArm, 8, 9); bones[B.rHand].quaternion.identity();
    limb(B.lThigh, B.hips, 10, 11); limb(B.lShin, B.lThigh, 11, 12); limb(B.lFoot, B.lShin, 12, 13, TOE_REST);
    limb(B.rThigh, B.hips, 14, 15); limb(B.rShin, B.rThigh, 15, 16); limb(B.rFoot, B.rShin, 16, 17, TOE_REST);
    // hips position (convert world -> mesh local)
    root.updateMatrixWorld(true);
    const inv = _m.copy(this.char.mesh.matrixWorld).invert();
    // hip particle is at the hips bone origin
    bones[B.hips].position.copy(this._pv(0, tmp1).applyMatrix4(inv));
  }
}
