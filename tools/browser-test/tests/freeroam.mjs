export default async function (p, shot, ev) {
  const st = () => ev(() => { const g = window.__game; const v = g.player.vehicle; return { pos: g.player.pos.toArray().map((x) => +x.toFixed(1)), hp: g.player.health, veh: v?.def.name, spd: v ? +v.speed.toFixed(1) : 0, wanted: g.police.level, cops: g.police.cops.length, pcars: g.police.cars.length, heli: !!g.police.heli, peds: g.peds.list.length, cars: g.vehicles.list.length, kills: g.stats.kills, hours: g.env.timeString }; });
  await ev(() => window.__step(2));
  await shot('a_foot');
  console.log('foot', JSON.stringify(await st()));
  // shoot at the nearest ped
  const r = await ev(() => {
    const g = window.__game; const P = window.THREE;
    let best = null, bd = 1e9;
    for (const ped of g.peds.list) { if (ped.vehicle || ped.dead) continue; const d = ped.pos.distanceTo(g.player.pos); if (d < bd && d > 4) { bd = d; best = ped; } }
    if (!best) return 'no ped';
    // point camera at ped
    const dx = best.pos.x - g.player.pos.x, dz = best.pos.z - g.player.pos.z;
    const yaw = Math.atan2(dx, dz);
    g.rig.yaw = yaw + Math.PI; g.rig.pitch = 0.02;
    g.player.yaw = yaw;
    g.player.switchTo('rifle');
    window.__step(0.3, 1/30, false);
    for (let i = 0; i < 6; i++) { g.input.mouse.right = true; g.input.mouse.left = true; window.__step(0.12, 1/30, false); }
    g.input.mouse.left = false;
    window.__step(0.1);
    return { target: best.id, dist: bd.toFixed(1), dead: best.dead, hp: best.health };
  });
  console.log('shoot', JSON.stringify(r));
  await shot('b_shoot');
  await ev(() => { const g = window.__game; g.input.mouse.right = false; window.__step(2); });
  await shot('c_after_shoot');
  console.log('after', JSON.stringify(await st()));
  // spawn car & drive
  await ev(() => { const g = window.__game; const h = g.map.landmarks.home; const car = g.vehicles.spawn('brawler', h.x + 10, h.z + 18.6, Math.PI / 2); g.vehicles.enter(g.player, car, 0, { force: true }); window.__step(4, 1/30, false); });
  console.log('incar', JSON.stringify(await st()));
  await ev(() => { window.__press('KeyW'); window.__step(3, 1/30, false); window.__press('KeyA'); window.__press('Space'); window.__step(0.8, 1/30, false); window.__release('Space'); window.__step(0.6); });
  await shot('d_drift');
  console.log('drift', JSON.stringify(await st()));
  await ev(() => { window.__release('KeyA'); window.__step(2); window.__release('KeyW'); window.__step(1); });
  await shot('e_drive');
  // wanted level
  await ev(() => { const g = window.__game; g.police.setLevel(3); window.__step(12, 1/30, false); window.__step(0.1); });
  console.log('wanted', JSON.stringify(await st()));
  await shot('f_wanted');
  // blow up a car with rpg on foot
  await ev(() => { const g = window.__game; g.vehicles.exit(g.player); window.__step(2, 1/30, false); const P = window.THREE; const car = g.vehicles.spawn('meridian', g.player.pos.x + 12, g.player.pos.z, 0); g.combat.explosion(car.pos.clone().setY(car.pos.y + 1), 8, 220, g.player); car.health = -1; window.__step(0.3, 1/30, false); g.rig.yaw = -Math.PI/2 + Math.PI; window.__step(0.3); });
  await shot('g_boom');
  await ev(() => window.__step(5));
  await shot('h_fire');
  console.log('end', JSON.stringify(await st()));
}
