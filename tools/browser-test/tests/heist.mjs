export default async function (p, shot, ev) {
  const r = await ev(() => {
    const g = window.__game, pl = g.player, H = g.heists;
    g.env.envInterval = 1e9; g.env.setTime(15);
    g.admin.cheats.god = true;
    pl.setPosition(105, undefined, 60);
    window.__step(1, 1 / 30, false);
    H.timer = 0;
    window.__step(0.2, 1 / 30, false);
    const h = H.van;
    if (!h) return 'no van';
    const v = h.v;
    // bring it close: put the van on the road behind us, stopped, then look at its back
    window.__step(3, 1 / 30, false);
    return { type: v.type, d: Math.hypot(v.pos.x - pl.pos.x, v.pos.z - pl.pos.z).toFixed(0), guards: h.guards.map((q) => q.weapon + (q.vehicle ? '@van' : '')), state: h.state, blips: H.blipList().length, spd: v.speedAbs.toFixed(1) };
  });
  console.log('spawn', JSON.stringify(r));
  const r2 = await ev(() => {
    const g = window.__game, pl = g.player, H = g.heists, h = H.van, v = h.v;
    // teleport the player behind the van, facing it
    const fx = Math.sin(v.yaw), fz = Math.cos(v.yaw);
    const x = v.pos.x - fx * 9, z = v.pos.z - fz * 9;
    pl.setPosition(x, v.pos.y + 0.5, z); pl.yaw = v.yaw;
    if (v.ai) v.ai.wait = 99;
    v.vel.set(0, 0, 0);
    window.__step(0.6, 1 / 30, true);
    return { d: Math.hypot(v.pos.x - pl.pos.x, v.pos.z - pl.pos.z).toFixed(1) };
  });
  console.log('behind', JSON.stringify(r2));
  await shot('van_rear');
  const r3 = await ev(() => {
    const g = window.__game, pl = g.player, H = g.heists, h = H.van, v = h.v;
    const fx = Math.sin(v.yaw), fz = Math.cos(v.yaw);
    const pt = { x: v.pos.x - fx * (v.def.L / 2 - 0.05), y: v.pos.y + 1.3, z: v.pos.z - fz * (v.def.L / 2 - 0.05) };
    const hp0 = v.health;
    for (let i = 0; i < 4; i++) { g.events.emit('vehicleShot', v, pl, pt); window.__step(0.1, 1 / 30, false); }
    const st = h.state, cash = h.cash.length, wanted = g.police.level;
    window.__step(1.2, 1 / 30, true);
    return { st, cash, wanted, guards: h.guards.map((q) => q.state + (q.vehicle ? '@van' : '')), doors: v.model.rearDoors.map((d) => d.open.toFixed(2)) };
  });
  console.log('open', JSON.stringify(r3));
  await shot('van_open');
  const r4 = await ev(() => {
    const g = window.__game, pl = g.player, H = g.heists, h = H.van;
    for (const c of h.cash) { pl.setPosition(c.pk.pos.x, c.pk.pos.y + 0.1, c.pk.pos.z); window.__step(0.2, 1 / 30, false); }
    window.__step(0.3, 1 / 30, false);
    return { taken: h.taken, done: h.done, robbed: H.robbed, big: document.querySelector('.hud-big')?.textContent };
  });
  console.log('cash', JSON.stringify(r4));
}
