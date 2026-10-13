// A browser player for multiplayer checks: open the game with ?manual&autostart=multi&room=CODE, name the player
// "Browser", stand at the safehouse for about two minutes (stepping the game), say hello twice, and log what it
// sees of the room. The Unreal game's Tools/tests/online-cross.txt joins the same room meanwhile.
export default async function (p, shot, ev) {
  await ev(() => { window.__game.net.name = 'Browser'; });
  for (let i = 0; i < 600; i++) {
    const r = await ev((i) => {
      window.__step(0.2, 1 / 30, false);
      const g = window.__game, n = g.net;
      return { t: i, st: n.status, me: [Math.round(g.player.pos.x), Math.round(g.player.pos.z)], peers: [...n.peers.values()].map((P) => ({ name: P.name, p: P.st.p, hp: P.st.hp })), npcPeds: n.npc.pedList.length, npcCars: g.vehicles.list.filter((v) => v.npcRemote && !v.removed).length, chat: n.chat.map((m) => (m.name ? m.name + ': ' : '') + m.text) };
    }, i);
    if (i % 50 === 0) console.log(JSON.stringify(r));
    if (i === 60 || i === 300) await ev(() => window.__game.net.say('hello from the browser'));
    await p.waitForTimeout(200);
  }
  await ev(() => { window.__step(0.1, 1 / 30, true); });
  await shot('mp');
}
