export default async function (p, shot, ev) {
  const tap = (code) => `window.__press('${code}'); window.__step(1/30, 1/30, false); window.__release('${code}');`;
  const r = await ev(() => {
    const g = window.__game, pl = g.player, P = g.phone;
    const tap = (c) => { window.__press(c); window.__step(1 / 30, 1 / 30, false); window.__release(c); };
    g.env.envInterval = 1e9; g.env.setTime(15);
    pl.setPosition(105, undefined, 60);
    window.__step(1, 1 / 30, false);
    tap('KeyI');
    const opened = P.open, screen = P.screen, n = P.items.length;
    const x0 = pl.pos.x, z0 = pl.pos.z;
    // arrows must not walk the player
    window.__press('ArrowDown'); window.__step(0.3, 1 / 30, false); window.__release('ArrowDown');
    const moved = Math.hypot(pl.pos.x - x0, pl.pos.z - z0);
    const selAfterDown = P.sel;
    window.__step(0.3, 1 / 30, true);
    return { opened, screen, n, moved: +moved.toFixed(2), selAfterDown };
  });
  console.log('open', JSON.stringify(r));
  await shot('phone_home');
  const r2 = await ev(() => {
    const g = window.__game, P = g.phone;
    const tap = (c) => { window.__press(c); window.__step(1 / 30, 1 / 30, false); window.__release(c); };
    // back to Contacts (index 0) and open it
    P.sel = 0; P._render();
    tap('Enter');
    const screen = P.screen, items = P.items.map((i) => i.name);
    window.__step(0.3, 1 / 30, true);
    return { screen, items };
  });
  console.log('contacts', JSON.stringify(r2));
  await shot('phone_contacts');
  // Lester while wanted
  const r3 = await ev(() => {
    const g = window.__game, P = g.phone;
    const tap = (c) => { window.__press(c); window.__step(1 / 30, 1 / 30, false); window.__release(c); };
    g.admin.cheats.god = true;
    g.police.setLevel(2);
    window.__step(0.5, 1 / 30, false);
    P._build(); P._render();
    P.sel = P.items.findIndex((i) => i.name === 'Lester');
    tap('Enter');
    const closed = !P.open;
    const lvl0 = g.police.level;
    window.__step(8, 1 / 30, false);
    return { closed, lvl0, lvl1: g.police.level };
  });
  console.log('lester', JSON.stringify(r3));
  // Merryweather + Benny
  const r4 = await ev(() => {
    const g = window.__game, P = g.phone, pl = g.player;
    P.lastCar = { type: 'zenith', color: 0xd01010 };
    P._merryweather();
    P._mechanic();
    window.__step(8, 1 / 30, false);
    const mercs = P.mercs.map((m) => ({ d: +Math.hypot(m.pos.x - pl.pos.x, m.pos.z - pl.pos.z).toFixed(1), st: m.state, w: m.weapon }));
    const v = P.delivered;
    return { mercs, car: v && { type: v.type, d: +Math.hypot(v.pos.x - pl.pos.x, v.pos.z - pl.pos.z).toFixed(1), blip: !!P.deliveredBlip } };
  });
  console.log('services', JSON.stringify(r4));
  await ev(() => { window.__step(6, 1 / 30, false); window.__step(0.2, 1 / 30, true); });
  await shot('phone_mercs');
  // cheats: free roam toggles admin cheats
  const r5 = await ev(() => {
    const g = window.__game, P = g.phone, pl = g.player;
    g.admin.cheats.god = false;
    P.cheat('HOPTOIT');
    const sj = !!g.admin.cheats.superJump;
    P.cheat('HOPTOIT');
    const sj2 = !!g.admin.cheats.superJump;
    P.cheat('FUGITIVE');
    const lvl = g.police.level;
    P.cheat('LAWYERUP');
    const lvl2 = g.police.level;
    P.cheat('SLOWMO'); const fx = g.fxScale; P.cheat('SLOWMO'); const fx2 = g.fxScale;
    // story-mode style timed cheat
    const fr = g.freeroam.active; g.freeroam.active = false;
    P.cheat('PAINKILLER');
    window.__step(0.2, 1 / 30, false);
    const inv = pl.invincible, on = !!g.cheatsOn.god;
    P.timed = {};
    window.__step(0.2, 1 / 30, false);
    const inv2 = pl.invincible;
    g.freeroam.active = fr;
    return { sj, sj2, lvl, lvl2, fx, fx2, inv, on, inv2 };
  });
  console.log('cheats', JSON.stringify(r5));
  // Snapmatic
  const r6 = await ev(() => {
    const g = window.__game, P = g.phone;
    const tap = (c) => { window.__press(c); window.__step(1 / 30, 1 / 30, false); window.__release(c); };
    tap('KeyI');
    P.sel = P.items.findIndex((i) => i.id === 'camera');
    tap('Enter');
    const photo = P.photo && document.body.classList.contains('photo-mode');
    window.__step(0.3, 1 / 30, true);
    return { photo };
  });
  console.log('photo', JSON.stringify(r6));
  await shot('phone_photo');
  const r7 = await ev(() => {
    const g = window.__game, P = g.phone;
    const tap = (c) => { window.__press(c); window.__step(1 / 30, 1 / 30, false); window.__release(c); };
    tap('Backspace');
    const off = !P.photo && !document.body.classList.contains('photo-mode');
    // Escape closes the phone without pausing
    tap('KeyI'); tap('Escape');
    return { off, open: P.open, menu: g.hud.menuOpen };
  });
  console.log('close', JSON.stringify(r7));
}
