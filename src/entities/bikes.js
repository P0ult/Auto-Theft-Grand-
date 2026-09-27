// Two-wheelers: motorbikes and bicycles. They run on the car tyre model (a narrow, light "car"), so traffic,
// collisions, carjacking and multiplayer work unchanged; on top of that a bike leans into corners (and onto
// its stand when parked), the rider sits astride with hands on the grips (pedalling on a bicycle) and a
// hard hit throws everyone off.
import * as THREE from 'three';
import { Vehicle } from './vehicle.js';
import { vehicleMaterials, bodyMaterial } from './vehiclemodels.js';
import { GeoBuilder, mat4 } from '../world/geom.js';
import { clamp, damp, wrapAngle } from '../core/utils.js';

const _up = new THREE.Vector3(0, 1, 0);

// a round tube from a to b ([x, y, z] arrays)
function tube(gb, a, b, r, seg = 8) {
  const A = new THREE.Vector3(...a), Bv = new THREE.Vector3(...b);
  const d = new THREE.Vector3().subVectors(Bv, A);
  const len = d.length();
  if (len < 1e-4) return;
  const g = new THREE.CylinderGeometry(r, r, len, seg, 1, false);
  const q = new THREE.Quaternion().setFromUnitVectors(_up, d.normalize());
  gb.addGeometry(g, new THREE.Matrix4().compose(A.clone().lerp(Bv, 0.5), q, new THREE.Vector3(1, 1, 1)));
}

// one wheel (axis along x): tyre, rim, hub and spokes (cast for motorbikes, wire for bicycles)
function wheelGeometry(R, tw, moto, knobbly) {
  const gb = new GeoBuilder();
  gb.set('color', 0.05, 0.05, 0.055);
  gb.addGeometry(new THREE.TorusGeometry(R - tw / 2, tw / 2, 8, 30), mat4(0, 0, 0, 0, Math.PI / 2, 0));
  if (knobbly) for (let k = 0; k < 22; k++) { const a = k / 22 * Math.PI * 2; gb.addGeometry(new THREE.BoxGeometry(tw * 1.05, 0.03, 0.04), new THREE.Matrix4().makeRotationX(a).multiply(mat4(0, R - 0.012, 0))); }
  const rimR = R - tw * (moto ? 0.95 : 0.9);
  gb.set('color', moto ? 0.2 : 0.75, moto ? 0.2 : 0.75, moto ? 0.22 : 0.78);
  gb.addGeometry(new THREE.TorusGeometry(rimR, moto ? 0.018 : 0.01, 5, 28), mat4(0, 0, 0, 0, Math.PI / 2, 0));
  gb.set('color', 0.55, 0.55, 0.58);
  gb.addGeometry(new THREE.CylinderGeometry(moto ? 0.06 : 0.025, moto ? 0.06 : 0.025, moto ? 0.16 : 0.1, 10), mat4(0, 0, 0, 0, 0, Math.PI / 2));
  if (moto) {
    gb.set('color', 0.18, 0.18, 0.2);
    for (let k = 0; k < 5; k++) gb.addGeometry(new THREE.BoxGeometry(0.03, rimR - 0.05, 0.035), new THREE.Matrix4().makeRotationX(k / 5 * Math.PI * 2).multiply(mat4(0, (rimR + 0.05) / 2, 0)));
    gb.set('color', 0.62, 0.62, 0.64);
    gb.addGeometry(new THREE.CylinderGeometry(R * 0.5, R * 0.5, 0.008, 20), mat4(0.07, 0, 0, 0, 0, Math.PI / 2)); // brake disc
  } else {
    gb.set('color', 0.8, 0.8, 0.82);
    for (let k = 0; k < 18; k++) { const a = k / 18 * Math.PI * 2; const s = k % 2 ? 1 : -1; tube(gb, [s * 0.025, Math.cos(a) * 0.03, Math.sin(a) * 0.03], [0, Math.cos(a + 0.2) * rimR, Math.sin(a + 0.2) * rimR], 0.0035, 3); }
  }
  return gb.build();
}

export function buildBikeModel(def, color) {
  const M = vehicleMaterials();
  const group = new THREE.Group(), bodyGroup = new THREE.Group();
  group.add(bodyGroup);
  const paint = new GeoBuilder(), trim = new GeoBuilder();
  const R = def.wheelR, zf = def.wheelbase / 2, zr = -def.wheelbase / 2;
  const moto = def.bike === 'moto';
  const dirt = !!def.offroad;
  const dark = (gb, k = 0.12) => gb.set('color', k, k, k + 0.01);
  const chrome = (gb) => gb.set('color', 0.72, 0.72, 0.75);
  paint.set('color', 1, 1, 1);
  // steering head (top of the fork) and where the fork meets the axle, relative to the front axle
  const head = moto ? [0, (dirt ? 1.02 : 0.95) - R, -0.2] : [0, 0.93 - R, -0.1];
  let seat, pillion = null, crank = null, feet;
  const headMesh = new THREE.Mesh(new THREE.BoxGeometry(moto ? 0.16 : 0.05, moto ? 0.1 : 0.04, 0.03), M.headOff);
  const tailMesh = new THREE.Mesh(new THREE.BoxGeometry(moto ? 0.14 : 0.05, 0.05, 0.02), M.tailOff);
  let glass;

  if (moto) {
    const hy = R + head[1], hz = zf + head[2]; // steering head in bike space
    const pivY = dirt ? 0.5 : 0.45, pivZ = -0.16;
    dark(trim, 0.16);
    for (const x of [-0.09, 0.09]) {
      tube(trim, [x, hy - 0.05, hz - 0.02], [x, pivY + 0.08, pivZ], 0.035);             // frame spars
      tube(trim, [x, pivY, pivZ], [x * 1.1, R, zr], 0.03);                             // swingarm
    }
    tube(trim, [0, hy, hz], [0, dirt ? 0.95 : 0.88, -0.45], 0.028);                     // subframe
    // engine & exhaust
    trim.set('color', 0.3, 0.3, 0.32);
    trim.box(-0.15, dirt ? 0.3 : 0.22, -0.12, 0.15, dirt ? 0.62 : 0.6, 0.32);
    trim.set('color', 0.22, 0.22, 0.24);
    trim.box(-0.11, 0.6, 0.12, 0.11, 0.74, 0.34);
    chrome(trim);
    if (dirt) { tube(trim, [0.12, 0.45, 0.25], [0.16, 0.72, -0.2], 0.035); tube(trim, [0.16, 0.72, -0.2], [0.17, 0.8, -0.7], 0.05); }
    else { tube(trim, [0.1, 0.25, 0.25], [0.14, 0.3, -0.2], 0.035); tube(trim, [0.14, 0.3, -0.2], [0.17, 0.52, -0.72], 0.06); }
    // tank, seat, tail
    if (dirt) {
      paint.box(-0.15, 0.78, 0.02, 0.15, 0.98, 0.38, { top: true });
      paint.box(-0.2, 0.62, 0.2, 0.2, 0.9, 0.36);                                          // radiator shrouds
      dark(trim, 0.08); trim.box(-0.12, 0.92, -0.62, 0.12, 1.0, 0.18, { top: true });  // long flat seat
      paint.box(-0.1, 0.9, -0.95, 0.1, 0.96, -0.55);                                     // rear fender
      seat = new THREE.Vector3(0, 1.0, -0.22);
      pillion = null;
    } else {
      paint.box(-0.17, 0.78, 0.02, 0.17, 1.02, 0.46, { top: true });
      paint.box(-0.19, 0.5, 0.18, 0.19, 0.78, 0.5);                                        // side fairings
      dark(trim, 0.07); trim.box(-0.14, 0.86, -0.48, 0.14, 0.94, 0.04, { top: true });
      dark(trim, 0.09); trim.box(-0.12, 0.93, -0.72, 0.12, 0.99, -0.46, { top: true });
      paint.box(-0.12, 0.82, -0.86, 0.12, 0.93, -0.46);                                    // tail cowl
      paint.box(-0.2, 0.55, 0.46, 0.2, 1.06, 0.78, { top: true });                          // front fairing
      seat = new THREE.Vector3(0, 0.94, -0.2);
      pillion = new THREE.Vector3(0, 1.0, -0.58);
      glass = new THREE.Mesh(new THREE.BoxGeometry(0.3, 0.2, 0.012), M.glass);
      glass.position.set(0, 1.12, 0.7); glass.rotation.x = -0.75;
      bodyGroup.add(glass);
    }
    headMesh.position.set(0, dirt ? 0.95 : 0.84, dirt ? zf - 0.12 : 0.795);
    tailMesh.position.set(0, dirt ? 0.93 : 0.88, dirt ? -0.96 : -0.87);
    feet = [[0.2, -0.5, -0.02], [-0.2, -0.5, -0.02]];
  } else {
    // diamond frame bicycle
    const hy = R + head[1], hz = zf + head[2];
    const bbY = 0.3, bbZ = -0.02, stY = 0.86, stZ = -0.2;
    tube(paint, [0, hy, hz], [0, bbY, bbZ], 0.02);                  // down tube
    tube(paint, [0, bbY, bbZ], [0, stY, stZ], 0.018);               // seat tube
    tube(paint, [0, stY - 0.03, stZ + 0.02], [0, hy + 0.02, hz], 0.017); // top tube
    for (const x of [-0.05, 0.05]) {
      tube(paint, [0, bbY, bbZ], [x, R, zr], 0.012);                // chain stays
      tube(paint, [0, stY - 0.05, stZ], [x, R, zr], 0.011);         // seat stays
    }
    chrome(trim); tube(trim, [0, stY, stZ], [0, 0.95, -0.24], 0.012); // seat post
    dark(trim, 0.06); trim.box(-0.07, 0.95, -0.38, 0.07, 0.99, -0.12, { top: true }); // saddle
    // cranks & pedals turn together
    crank = new THREE.Group(); crank.position.set(0, bbY, bbZ);
    const cg = new GeoBuilder();
    chrome(cg); cg.addGeometry(new THREE.CylinderGeometry(0.09, 0.09, 0.01, 18), mat4(0.06, 0, 0, 0, 0, Math.PI / 2)); // chainring
    for (const s of [1, -1]) {
      cg.set('color', 0.3, 0.3, 0.32);
      cg.box(s * 0.08 - 0.012, -0.015, 0, s * 0.08 + 0.012, 0.015, s * 0.17);
      dark(cg, 0.1); cg.box(s * 0.08 + (s > 0 ? 0 : -0.1), -0.012, s * 0.17 - 0.035, s * 0.08 + (s > 0 ? 0.1 : 0), 0.012, s * 0.17 + 0.035);
    }
    const crankMesh = new THREE.Mesh(cg.build(), M.trim); crankMesh.castShadow = true;
    crank.add(crankMesh);
    bodyGroup.add(crank);
    seat = new THREE.Vector3(0, 0.98, -0.26);
    headMesh.position.set(0, 0.86, zf + 0.02);
    tailMesh.position.set(0, 0.9, -0.42);
    feet = null; // pedals: see feetFor()
  }
  if (!glass) { glass = new THREE.Mesh(new THREE.BoxGeometry(0.01, 0.01, 0.01), M.glass); glass.visible = false; bodyGroup.add(glass); }

  const bodyMat = bodyMaterial(color);
  const body = new THREE.Mesh(paint.build(), bodyMat); body.castShadow = true; bodyGroup.add(body);
  const trimMesh = new THREE.Mesh(trim.build(), M.trim); trimMesh.castShadow = true; bodyGroup.add(trimMesh);
  bodyGroup.add(headMesh, tailMesh);

  // wheels: the front one hangs in the fork, which turns with the bars
  const tw = moto ? (dirt ? 0.12 : 0.15) : 0.035;
  const wGeo = wheelGeometry(R, tw, moto, dirt);
  const wheels = [];
  for (const [z, front] of [[zf, true], [zr, false]]) {
    const pivot = new THREE.Group(); pivot.position.set(0, R, z);
    const spin = new THREE.Group();
    const m = new THREE.Mesh(wGeo, M.wheel); m.castShadow = true;
    spin.add(m); pivot.add(spin);
    group.add(pivot);
    wheels.push({ pivot, spin, x: 0, z, front, baseY: R, comp: 0 });
    if (front) {
      const fork = new GeoBuilder();
      if (moto) {
        chrome(fork);
        for (const x of [-0.1, 0.1]) tube(fork, [x, 0, 0], [x, head[1], head[2]], 0.028);
        dark(fork, 0.1);
        fork.box(-0.12, head[1] - 0.04, head[2] - 0.05, 0.12, head[1] + 0.02, head[2] + 0.05); // triple clamp
        tube(fork, [-0.34, head[1] + 0.08, head[2] - 0.08], [0.34, head[1] + 0.08, head[2] - 0.08], 0.016); // bars
        if (dirt) { fork.set('color', 0.9, 0.9, 0.9); fork.box(-0.08, R + 0.05, -0.25, 0.08, R + 0.09, 0.2); } // high fender
        else { fork.set('color', 0.15, 0.15, 0.16); fork.box(-0.08, R * 0.7, -0.25, 0.08, R + 0.06, 0.2); }
      } else {
        chrome(fork);
        for (const x of [-0.035, 0.035]) tube(fork, [x, 0, 0], [x * 0.3, head[1], head[2]], 0.011);
        tube(fork, [0, head[1], head[2]], [0, head[1] + 0.12, head[2] - 0.04], 0.013); // stem
        dark(fork, 0.15);
        tube(fork, [-0.28, head[1] + 0.12, head[2] - 0.06], [0.28, head[1] + 0.12, head[2] - 0.06], 0.012); // bars
      }
      const fm = new THREE.Mesh(fork.build(), M.trim); fm.castShadow = true;
      pivot.add(fm);
    }
  }
  const seats = [seat];
  if (pillion) seats.push(pillion);
  return {
    group, bodyGroup, body, bodyMat, glass, head: headMesh, tail: tailMesh, wheels, crank, feet,
    door: { pivot: new THREE.Group(), mesh: headMesh, open: 0 },
    seatHip: 0.04,
    seats,
    doorPos: new THREE.Vector3(0.8, 0, -0.1),
    trim: trimMesh,
    grips: moto ? [head[1] + R + 0.08, zf + head[2] - 0.08] : [head[1] + R + 0.12, zf + head[2] - 0.06],
  };
}

export class Bike extends Vehicle {
  setup() { this.lean = 0; this._yawPrev = this.yaw; this.pedalPhase = 0; this._throwT = 0; }
  buildModel(def, color) { return buildBikeModel(def, color); }
  // get on from whichever side you're standing
  doorFor(seat, char) {
    const side = char ? (this.worldToLocal(char.pos.x, char.pos.z)[0] >= 0 ? 1 : -1) : 1;
    return this.localToWorld(0.85 * side, 0, seat === 0 ? -0.15 : -0.55, new THREE.Vector3());
  }
  nearestDoor(pos) {
    const side = this.worldToLocal(pos.x, pos.z)[0] >= 0 ? 1 : -1;
    const p = this.localToWorld(0.85 * side, 0, -0.15, new THREE.Vector3());
    p.seat = 0;
    return p;
  }

  // where the rider's feet go (hip-relative, bike axes): pegs, or the pedals going round
  feetFor(seat) {
    const m = this.model;
    if (m.feet) return seat === 0 ? m.feet : [[0.22, -0.4, -0.1], [-0.22, -0.4, -0.1]];
    const s = m.seats[0], hipY = s.y + m.seatHip, a = this.pedalPhase;
    const cy = 0.3 - hipY, cz = -0.02 - s.z;
    return [[0.13, cy + Math.sin(a) * 0.17, cz + Math.cos(a) * 0.17], [-0.13, cy - Math.sin(a) * 0.17, cz - Math.cos(a) * 0.17]];
  }
  // the grips, hip-relative
  gripsFor() { const m = this.model, s = m.seats[0]; return [m.grips[0] - (s.y + m.seatHip), m.grips[1] - s.z]; }

  update(dt) {
    super.update(dt);
    if (this.removed) return;
    if (this.def.offroad) this._surface = 1; // knobbly tyres don't mind the dirt
    if (this.def.pedal && this.model.crank) {
      // the cranks turn with the back wheel while pedalling, and freewheel when coasting
      if (this.driver && this.input.throttle > 0.05 && this.speed > -0.5) this.pedalPhase += Math.max(this.speed, 2) * dt / 0.62;
      this.model.crank.rotation.x = this.pedalPhase;
    }
    this._throwT -= dt;
  }

  _updateVisual(dt) {
    super._updateVisual(dt);
    if (!this.model?.bodyGroup) return;
    // lean into the turn: tan(lean) = v * yawRate / g (from the heading change, so it works for stand-ins too)
    const yr = dt > 0 ? wrapAngle(this.yaw - (this._yawPrev ?? this.yaw)) / dt : 0;
    this._yawPrev = this.yaw;
    let tgt = 0;
    const ridden = !!this.driver && !this.isWrecked;
    if (ridden && !this.airborne) tgt = clamp(Math.atan2(this.speed * yr, 9.81), -0.8, 0.8);
    else if (!ridden && this.speedAbs < 1 && !this.airborne) tgt = this.isWrecked ? 1.35 : 0.2; // on its side / its stand
    this.lean = damp(this.lean || 0, tgt, ridden ? 7 : 3, dt);
    this.group.rotation.z = this.groundRoll - this.lean;
    this.model.bodyGroup.rotation.z = 0;
    for (const w of this.model.wheels) if (w.front) w.pivot.rotation.y = this.steerAngle * 0.8;
  }

  onCrash(impact) {
    if (impact > 6.5) this.throwRiders(impact);
  }

  // off you come
  throwRiders(impact) {
    if (this._throwT > 0) return;
    this._throwT = 1;
    const vx = this.vel.x, vz = this.vel.z;
    for (const o of [...this.occupants]) {
      if (!o || o.remote) continue;
      const p = this.localToWorld(0, 0, 0.6);
      this.takeOut(o, p);
      o.pos.y = this.pos.y + 1.0;
      const imp = new THREE.Vector3(vx * 0.55, 2.2 + impact * 0.12, vz * 0.55);
      o.vel.copy(imp);
      o.knockDown(imp);
      o.takeDamage(Math.min(40, impact * 2), { type: 'fall' });
      this.game.events?.emit('exitedVehicle', o, this);
      if (o.isPlayer) this.game.rig?.addShake(0.6);
    }
    this.vel.multiplyScalar(0.6);
  }
}
