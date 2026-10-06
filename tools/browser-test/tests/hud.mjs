export default async function (p, shot, ev) {
  await ev(() => {
    const g = window.__game, pl = g.player;
    g.env.envInterval = 1e9; g.env.setTime(16);
    pl.setPosition(105, undefined, 60);
    pl.armor = 60;
    window.__step(2, 1 / 30, false);
    g.peds.populate?.(20); g.traffic.populate?.(15);
    window.__step(1, 1 / 30, false);
  });
  await ev(() => window.__step(0.1));
  await shot('hud_plain');
  await ev(() => { const g = window.__game; g.police.setLevel(3); window.__step(4, 1 / 30, false); window.__step(0.05); });
  await shot('hud_wanted');
  await ev(() => { const g = window.__game; g.police.lastSeen = g.time - 5; g.police.seen = false; g.police.update(0.01); g.police.lastSeen = g.time - 5; window.__step(0.05); });
  await shot('hud_search');
  console.log(await ev(() => { const g = window.__game; return JSON.stringify({ flash: g.police.flash, lvl: g.police.level, cops: g.police.cops.length, modern: g.hud.modern }); }));
  await ev(() => { const g = window.__game; g.settings.hudStyle = 'classic'; g.hud.applyStyle(); window.__step(0.05); });
  await shot('hud_classic');
}
