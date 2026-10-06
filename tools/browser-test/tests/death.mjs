export default async function (p, shot, ev) {
  await ev(async () => { const g = window.__game, pl = g.player; pl.protectUntil = 0; pl.takeDamage(999, { type: 'fall' }); for (let i = 0; i < 80; i++) window.__step(0.1, 1 / 30, false); });
  await new Promise((r) => setTimeout(r, 2500));
  const r = await ev(async () => { for (let i = 0; i < 30; i++) { window.__step(0.1, 1 / 30, false); await new Promise((r) => setTimeout(r, 30)); } const g = window.__game; return { state: g.gameplay.state, dead: g.player.dead, hp: g.player.health, boost: g.post.composite.uniforms.uDeathBoost.value, death: g.post.composite.uniforms.uDeath.value }; });
  console.log(JSON.stringify(r));
}
