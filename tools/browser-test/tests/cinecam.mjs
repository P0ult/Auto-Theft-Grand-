export default async function (p, shot, ev) {
  await ev(() => {
    const g = window.__game, pl = g.player, A = g.admin;
    g.env.envInterval = 1e9; g.env.setTime(17.5); window.__forced = 0;
    A.cheats.god = true; A.cheats.vehGod = true;
    const net = g.map.roads;
    let best = null;
    for (const e of net.edges) { if (e.removed || e.type === 'rail' || e.type === 'ramp' || e.len < 320) continue; if (!best || e.len > best.len) best = e; }
    const at = net.at(best, 40);
    const yaw = Math.atan2(at[3], at[4]);
    const off = net.laneOffset(best, 0, 0);
    pl.setPosition(at[0] - at[4] * off, at[1] + 1, at[2] + at[3] * off); pl.yaw = yaw;
    window.__step(0.5, 1 / 30, false);
    A.summon('tempest');
    const v = pl.vehicle;
    v.yaw = yaw; v.vel.set(Math.sin(yaw) * 30, 0, Math.cos(yaw) * 30);
    window.__press('KeyW'); window.__press('KeyX');
  });
  const shots = [];
  for (let i = 0; i < 4; i++) {
    const r = await ev(() => { const g = window.__game; window.__step(1.4, 1 / 30, false); window.__step(0.15, 1 / 30, true); return { shot: g.rig.cv?.shot, bars: document.body.classList.contains('cine-cam'), spd: g.player.vehicle.speedAbs.toFixed(0) }; });
    shots.push(r);
    await shot('cine' + i);
    await ev(() => { const g = window.__game; if (g.rig.cv) { g.rig.cv.t = 99; g.rig.cv.n = [3, 0, 1, 2][window.__forced++ % 4] - 1 + 4; } });
  }
  console.log(JSON.stringify(shots));
  const r = await ev(() => { window.__release('KeyX'); window.__step(0.3, 1 / 30, false); return document.body.classList.contains('cine-cam'); });
  console.log('released bars:', r);
}
