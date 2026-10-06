export default async function (p, shot, ev) {
  const r = await ev(() => {
    const g = window.__game, pl = g.player, S = g.special;
    g.env.envInterval = 1e9; g.env.setTime(15);
    pl.setPosition(105, undefined, 60);
    window.__step(1, 1 / 30, false);
    const m0 = S.meter;
    window.__press('CapsLock');
    window.__step(1 / 30, 1 / 30, false);
    window.__release('CapsLock');
    const on = S.active, fx = g.fxScale;
    const t0 = g.time;
    window.__step(1, 1 / 30, false);
    const gameDt = g.time - t0;
    const m1 = S.meter;
    window.__step(0.5, 1 / 30, true);
    return { m0, on, fx, gameDt: +gameDt.toFixed(2), m1: +m1.toFixed(2), desat: g.post.composite.uniforms.uDesat.value.toFixed(2) };
  });
  console.log(JSON.stringify(r));
  await shot('special');
  const r2 = await ev(() => {
    const g = window.__game, S = g.special;
    window.__press('CapsLock'); window.__step(1 / 30, 1 / 30, false); window.__release('CapsLock');
    const off = !S.active;
    window.__step(1, 1 / 30, false);
    return { off, fx: g.fxScale, meter: +S.meter.toFixed(2), desat: g.post.composite.uniforms.uDesat.value.toFixed(2) };
  });
  console.log(JSON.stringify(r2));
}
