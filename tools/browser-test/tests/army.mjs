export default async function (p, shot, ev) {
  const st = () => ev(() => {
    const g = window.__game, A = g.army, pl = g.player, pp = pl.vehicle ? pl.vehicle.pos : pl.pos;
    const units = A.units.map((u) => ({ k: u.kind, d: +Math.hypot(u.veh.pos.x - pp.x, u.veh.pos.z - pp.z).toFixed(0), alt: +(u.veh.pos.y - g.map.groundHeight(u.veh.pos.x, u.veh.pos.z)).toFixed(0), spd: +u.veh.speedAbs.toFixed(1), wreck: u.veh.isWrecked, unl: u.unloaded, leave: !!u.leaving, drv: !!u.veh.driver }));
    return { t: +g.time.toFixed(1), lvl: g.police.level, active: A.active, units, troops: A.troops.filter((s) => !s.dead).length, attacking: A.troops.filter((s) => s.state === 'attack').length, hp: Math.round(pl.health), dead: pl.dead };
  });
  await ev(() => {
    const g = window.__game, pl = g.player;
    g.env.envInterval = 1e9; g.env.setTime(13);
    g.admin.cheats.god = true;
    pl.setPosition(105, undefined, 60);
    window.__step(1, 1 / 30, false);
    g.peds.populate?.(10); g.traffic.populate?.(10);
    g.police.setLevel(5); g.police._updateLevel();
    window.__step(0.5, 1 / 30, false);
  });
  for (let i = 0; i < 12; i++) {
    await ev(() => { const g = window.__game; g.player.health = 100; g.police.raise(5); window.__step(3, 1 / 30, false); });
    console.log(JSON.stringify(await st()));
  }
  await ev(() => window.__step(0.1));
  await shot('army_a');
  // look at the gunship
  await ev(() => { const g = window.__game, u = g.army.units.find((q) => q.kind === 'heli'); if (u) { const pl = g.player; const dx = u.veh.pos.x - pl.pos.x, dz = u.veh.pos.z - pl.pos.z; g.rig.yaw = Math.atan2(dx, dz) + Math.PI; g.rig.pitch = 0.35; } window.__step(0.2); });
  await shot('army_heli');
  await ev(() => { const g = window.__game, u = g.army.units.find((q) => q.kind === 'tank'); if (u) { const pl = g.player; const dx = u.veh.pos.x - pl.pos.x, dz = u.veh.pos.z - pl.pos.z; g.rig.yaw = Math.atan2(dx, dz) + Math.PI; g.rig.pitch = 0.05; } window.__step(0.2); });
  await shot('army_tank');
  // drop to 2 stars: they should pull out
  await ev(() => { const g = window.__game; g.police.setLevel(2); window.__step(4, 1 / 30, false); });
  console.log('after', JSON.stringify(await st()));
  await ev(() => { const g = window.__game; g.police.reset(); window.__step(1, 1 / 30, false); });
  console.log('reset', JSON.stringify(await st()));
}
