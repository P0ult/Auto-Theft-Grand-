export default async function (p, shot, ev) {
  const r = await ev(() => {
    const g = window.__game, pl = g.player, A = g.admin;
    g.env.envInterval = 1e9; g.env.setTime(14);
    A.cheats.god = true; A.cheats.vehGod = true;
    // a long straight: sample a few places, keep the one with the longest edge
    const net = g.map.roads;
    let best = null;
    for (const e of net.edges) { if (e.removed || e.type === 'rail' || e.type === 'ramp' || e.type === 'freeway' || e.len < 320) continue; if (!best || e.len > best.len) best = e; }
    const at = net.at(best, 40);
    const yaw = Math.atan2(at[3], at[4]);
    const off = net.laneOffset(best, 0, 0);
    const x = at[0] - at[4] * off, z = at[2] + at[3] * off;
    pl.setPosition(x, at[1] + 1, z); pl.yaw = yaw;
    window.__step(0.5, 1 / 30, false);
    A.summon('buffalo');
    const v = pl.vehicle;
    v.yaw = yaw; v.vel.set(Math.sin(yaw) * 22, 0, Math.cos(yaw) * 22);
    g.police.setLevel(4);
    g.roadblocks.timer = 0;
    window.__press('KeyW');
    let n = 0;
    for (let i = 0; i < 30 && !g.roadblocks.blocks.length; i++) { window.__step(0.1, 1 / 30, false); n++; }
    window.__release('KeyW');
    const b = g.roadblocks.blocks[0];
    return { edge: best.len, type: best.type, steps: n, spd: v.speedAbs.toFixed(1), blocks: g.roadblocks.blocks.length,
      b: b && { d: Math.hypot(b.x - v.pos.x, b.z - v.pos.z).toFixed(0), cars: b.cars.map((c) => c.type), cops: b.cops.length, spike: !!b.spike, w: (b.wU + b.wD).toFixed(1) } };
  });
  console.log('spawn', JSON.stringify(r));
  // look at it: park 35 m short, facing it
  const r2 = await ev(() => {
    const g = window.__game, pl = g.player, b = g.roadblocks.blocks[0];
    if (!b) return null;
    const v = pl.vehicle;
    const x = b.x - b.tx * 30, z = b.z - b.tz * 30;
    v.pos.set(x, b.y + 0.5, z); v.yaw = Math.atan2(b.tx, b.tz); v.vel.set(0, 0, 0);
    v._placeGroup?.();
    window.__step(1.5, 1 / 30, true); const vv = pl.vehicle; window.__dbg = { d: Math.hypot(vv.pos.x - b.x, vv.pos.z - b.z).toFixed(1), yaws: b.cars.map((c) => ((c.yaw - Math.atan2(b.tx, b.tz)) % 6.283).toFixed(2)) };
    return { dbg: window.__dbg, copStates: b.cops.map((c) => c.state + (c.dead ? '/dead' : '')), hp: g.player.health };
  });
  console.log('view', JSON.stringify(r2));
  await shot('roadblock');
  // drive over the stinger
  const r3 = await ev(() => {
    const g = window.__game, pl = g.player, b = g.roadblocks.blocks[0];
    if (!b?.spike) return 'no spike';
    const v = pl.vehicle;
    const s = b.spike;
    const x = s.x - b.tx * 20, z = s.z - b.tz * 20;
    v.pos.set(x, b.y + 0.5, z); v.yaw = Math.atan2(b.tx, b.tz); v.vel.set(b.tx * 15, 0, b.tz * 15);
    window.__press('KeyW');
    let t = 0;
    while (!v.flat && t < 3) { window.__step(0.1, 1 / 30, false); t += 0.1; }
    window.__release('KeyW');
    const flat = v.flat;
    // top speed on flats
    window.__press('KeyW'); window.__step(0.1, 1 / 30, false); window.__release('KeyW');
    return { flat, t: t.toFixed(1) };
  });
  console.log('spike', JSON.stringify(r3));
  await ev(() => { window.__step(0.4, 1 / 30, true); });
  await shot('spiked');
  // clearing up: lose the stars and drive away
  const r4 = await ev(() => {
    const g = window.__game, pl = g.player;
    g.police.clear();
    const v = pl.vehicle;
    v.pos.x += 400; v.flat = false; v._placeGroup?.();
    window.__step(1, 1 / 30, false);
    return { blocks: g.roadblocks.blocks.length };
  });
  console.log('cleanup', JSON.stringify(r4));
}
