export default async function (p, shot, ev) {
  const r = await ev(async () => {
    const g = window.__game, pl = g.player;
    const log = [];
    const orig = pl.takeDamage.bind(pl);
    pl.takeDamage = (a, info = {}) => { log.push([g.gameplay.state, +a.toFixed(1), info.type || '?', info.source ? (info.source.isPlayer ? 'self' : (info.source.brain || '') + ':' + (info.source.gang || info.source.def?.id || '')) : '-', Math.round(pl.health), g.missions.active?.def?.id || '-']); return orig(a, info); };
    for (let i = 0; i < 100; i++) window.__step(0.1, 1 / 30, false);
    const m0 = g.missions.active?.def?.id;
    pl.takeDamage(1000, { type: 'bullet' });
    for (let i = 0; i < 90; i++) { window.__step(0.1, 1 / 30, false); await new Promise((r) => setTimeout(r, 30)); }
    const st = g.gameplay.state;
    if (g.gameplay.state !== 'playing') { g.gameplay.respawn('wasted'); await new Promise((r) => setTimeout(r, 1500)); }
    const t0 = log.length, series = [];
    for (let i = 0; i < 300; i++) { window.__step(0.1, 1 / 30, false); if (i % 20 === 19) series.push(Math.round(pl.health)); if (i % 10 === 0) await new Promise((r) => setTimeout(r, 20)); }
    pl.takeDamage = orig;
    return { m0, mission: g.missions.active?.def?.id || null, stBefore: st, state: g.gameplay.state, series, after: log.slice(t0).slice(0, 10), nAfter: log.length - t0, pos: [Math.round(pl.pos.x), Math.round(pl.pos.z)], attackers: g.peds.list.filter((q) => q.threat === pl && !q.dead).map((q) => q.brain + ':' + q.gang + ':' + q.state).slice(0, 6), wanted: g.police.level };
  });
  console.log(JSON.stringify(r));
}
