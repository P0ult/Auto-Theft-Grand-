export default async function (p, shot, ev) {
  const r = await ev(async () => {
    const g = window.__game; g.env.envInterval = 1e9; g.env.setTime(15.5);
    g.police.enabled = false; g.heists.enabled = false;
    g.player.setPosition(-2960, undefined, 200);
    for (let i = 0; i < 6; i++) { window.__step(0.1, 1 / 30, false); await new Promise((r) => setTimeout(r, 0)); }
    g.player.root.visible = false;
    for (const v of [...g.vehicles.list]) g.vehicles.remove(v);
    const ids = ['stockade', 'enforcer', 'sheriff', 'buffalo', 'baller', 'tempest'];
    ids.forEach((id, i) => g.vehicles.spawn(id, -2990 + i * 9, 330, Math.PI * 0.75));
    window.__step(0.5, 1 / 30, false);
    const st = g.vehicles.list.find((v) => v.type === 'stockade');
    window.__st = st;
    return ids.join(' ');
  });
  console.log(r);
  const view = async (name, pos, look, fov = 40) => {
    await ev(async ([pos, look, fov]) => {
      const g = window.__game, T = window.THREE;
      const gy = g.map.groundHeight(look[0], look[2]);
      g.rig.setCinematic(new T.Vector3(pos[0], gy + pos[1], pos[2]), new T.Vector3(look[0], gy + look[1], look[2]), fov);
      g.camera.position.set(pos[0], gy + pos[1], pos[2]); g.rig.fov = fov;
      window.__step(0.1, 1 / 30, true);
    }, [pos, look, fov]);
    await shot(name);
  };
  await view('row', [-2967, 9, 305], [-2967, 0.5, 333], 55);
  for (const [i, id] of [[0, 'stockade'], [1, 'enforcer'], [2, 'sheriff'], [5, 'tempest']]) {
    const x = -2990 + i * 9, z = 330;
    await view('f_' + id, [x + 4.6, 1.7, z - 5], [x, 0.9, z], 45);
    await view('r_' + id, [x - 5, 1.8, z + 4.6], [x, 1.0, z], 45);
  }
  await ev(() => { for (const d of window.__st.model.rearDoors) { d.open = 1; d.pivot.rotation.y = -d.side * 1.85; } });
  await view('r_stockade_open', [-2990 - 5, 1.8, 330 + 4.6], [-2990, 1.0, 330], 45);
}
