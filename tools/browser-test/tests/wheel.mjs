export default async function (p, shot, ev) {
  const r = await ev(() => {
    const g = window.__game, pl = g.player;
    g.env.envInterval = 1e9; g.env.setTime(15);
    pl.setPosition(105, undefined, 60);
    window.__step(1, 1 / 30, false);
    const before = pl.weapon;
    window.__press('Tab');
    const t0 = g.time;
    window.__step(0.5, 1 / 30, false);
    const slowDt = g.time - t0;
    // point the cursor to the right (3 o'clock)
    g.input.mouse.dx = 80; g.input.mouse.dy = 0;
    window.__step(0.1, 1 / 30, true);
    const sel = g.weaponWheel.list[g.weaponWheel.sel];
    return { before, open: g.weaponWheel.open, list: g.weaponWheel.list, sel, slowDt: +slowDt.toFixed(3), fx: g.fxScale };
  });
  console.log(JSON.stringify(r));
  await shot('wheel');
  const r2 = await ev(() => {
    const g = window.__game, pl = g.player;
    window.__release('Tab');
    window.__step(0.2, 1 / 30, false);
    return { weapon: pl.weapon, open: g.weaponWheel.open, fx: g.fxScale };
  });
  console.log(JSON.stringify(r2));
}
