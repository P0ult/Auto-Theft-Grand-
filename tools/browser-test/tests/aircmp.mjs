// Flies a Skipper off the airfield runway, a Skylark off the grass, drives a Mammoth or rides a skateboard down the
// runway, or drives a speedboat off Santa Luz (MODE=plane, heli, tank, skate or boat) and prints its state every 0.5 s. The native simtest "aircmp <mode>"
// prints the same lines.
export default async function (p, shot, ev) {
  const lines = await ev((mode) => {
    const g = window.__game, THREE = window.THREE;
    const out = [];
    g.env.envInterval = 1e9; g.env.setTime(13);
    g.admin.cheats.god = true;
    const af = g.map.landmarks.airfield;
    const at = mode === 'heli' ? [af.x, af.z] : mode === 'boat' ? [-200, 900] : [af.x, af.z - 260];
    g.env.setWeather?.('clear', true);
    g.player.setPosition(at[0] + 4, undefined, at[1]);
    window.__step(0.5, 1 / 30, false);
    const v = g.vehicles.spawn(mode === 'plane' ? 'skipper' : mode === 'heli' ? 'skylark' : mode === 'skate' ? 'skateboard' : mode === 'boat' ? 'speedboat' : 'mammoth', at[0], at[1], 0);
    g.vehicles.seatNow(g.player, v, 0);
    const st = (t) => {
      let s = `${t.toFixed(1)} x ${v.pos.x.toFixed(2)} y ${v.pos.y.toFixed(2)} z ${v.pos.z.toFixed(2)} yaw ${v.yaw.toFixed(3)} fwd ${(v.forwardSpeed ?? v.speed).toFixed(2)} hp ${v.health.toFixed(0)} ex ${v.exploded ? 1 : 0}`;
      if (mode === 'plane') { const f = new THREE.Vector3(0, 0, 1).applyQuaternion(v.quat); s += ` alt ${v.altitude.toFixed(2)} pitch ${Math.asin(f.y).toFixed(3)} gr ${v.grounded ? 1 : 0} gear ${v.gearK.toFixed(2)} spool ${v.spool.toFixed(3)}`; }
      if (mode === 'heli') s += ` alt ${v.altitude.toFixed(2)} tilt ${v.tiltP.toFixed(3)} ${v.tiltR.toFixed(3)} gr ${v.grounded ? 1 : 0} spool ${v.spool.toFixed(3)}`;
      if (mode === 'tank') s += ` tracks ${v.trackL.toFixed(2)} ${v.trackR.toFixed(2)} body ${v.bodyPitch.toFixed(4)} ${v.bodyRoll.toFixed(4)}`;
      if (mode === 'boat') s += ` pitch ${v.pitch.toFixed(4)} roll ${v.roll.toFixed(4)} float ${v.floating ? 1 : 0} t ${g.time.toFixed(2)}`;
      if (mode === 'skate') s += ` air ${v.airborne ? 1 : 0} flip ${v.flip.toFixed(3)} push ${v.pushK.toFixed(3)} crouch ${v.crouch.toFixed(3)} lean ${v.lean.toFixed(3)} on ${v.driver ? 1 : 0}`;
      out.push(s);
    };
    let t = 0;
    const phase = (keys, secs) => { for (const k of keys) window.__press(k); for (let i = 0; i < secs * 2; i++) { window.__step(0.5, 1 / 30, false); t += 0.5; st(t); } for (const k of keys) window.__release(k); };
    if (mode === 'plane') {
      window.__press('KeyW');
      while (t < 30 && v.forwardSpeed < v.def.vRotate + 2) { window.__step(0.1, 1 / 30, false); t += 0.1; }
      st(t);
      phase(['ArrowDown'], 0.5); phase([], 6); phase(['KeyA'], 0.5); phase([], 4); phase(['ArrowUp'], 1); phase([], 12);
    } else if (mode === 'heli') {
      phase([], 3); phase(['Space'], 4); phase([], 6); phase(['KeyW'], 4); phase([], 3); phase(['KeyD', 'KeyW'], 2); phase(['ShiftLeft'], 3);
    } else if (mode === 'boat') {
      phase(['KeyW'], 4); phase(['KeyW', 'KeyA'], 2); phase([], 2); phase(['KeyS'], 2); phase(['KeyD', 'KeyW'], 2);
    } else if (mode === 'skate') {
      phase(['KeyW'], 3); phase(['Space'], 0.5); phase(['KeyA'], 0.5); phase([], 2); phase(['KeyW', 'KeyD'], 2); phase(['KeyS'], 1.5);
    } else {
      phase(['KeyW'], 3); phase(['KeyW', 'KeyA'], 2); phase([], 2); phase(['KeyS'], 2); phase(['KeyD'], 2);
    }
    return out;
  }, process.env.MODE || 'plane');
  for (const l of lines) console.log(l);
}
