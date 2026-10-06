export default async function (p, shot, ev) {
  const ids = await ev(() => window.__game.missions.story.missions.map((m) => m.id));
  const only = process.env.ONLY ? process.env.ONLY.split(',') : null;
  for (const id of ids) {
    if (only && !only.includes(id)) continue;
    const res = await ev(async (id) => {
      const g = window.__game;
      const M = g.missions;
      g.env.envInterval = 1e9; g.env.updateEnvMap = () => {};
      const def = M.story.missions.find((m) => m.id === id);
      // reset world state
      if (M.active) M.abortActive();
      window.__step(0.2, 1 / 30, false);
      g.police.reset();
      g.player.dead = false; g.player.health = 100; g.gameplay.state = 'playing'; g.timeScale = 1;
      if (g.player.vehicle) g.player.vehicle.takeOut(g.player);
      M.completed = new Set(def.requires || []);
      const start = def.start ? def.start(g.map.landmarks) : g.map.landmarks.home;
      g.player.setPosition(start.x, undefined, start.z);
      let outcome = null, reason = '';
      const offP = g.events.on('missionPassed', (mid) => { if (mid === id) outcome = 'PASSED'; });
      const offF = g.events.on('missionFailed', (mid) => { if (mid === id) { outcome = 'FAILED'; reason = (document.querySelector('.hud-big small')?.textContent || '') + ' dead=' + g.player.dead; } });
      M.start(def);
      await new Promise((r) => setTimeout(r, 0));
      const log = [];
      let simT = 0;
      let lastObj = '';
      for (let it = 0; it < 1400 && !outcome; it++) {
        const ctx = M.active;
        if (!ctx && !outcome) { window.__step(0.3, 1 / 30, false); simT += 0.3; await new Promise((r) => setTimeout(r, 0)); continue; }
        const p = g.player;
        const obj = document.querySelector('.hud-subtitles')?.textContent || '';
        if (obj !== lastObj) { lastObj = obj; if (log.length < 40) log.push(`${simT.toFixed(0)}s: ${obj.slice(0, 70)}`); }
        if (g.cutscene) { window.__press('Space'); window.__step(0.1, 1 / 30, false); window.__release('Space'); await new Promise((r) => setTimeout(r, 0)); window.__step(0.2, 1 / 30, false); simT += 0.3; await new Promise((r) => setTimeout(r, 0)); continue; }
        if (id === 'toolingup' && !p.weapons.pistol) p.giveWeapon('pistol', 34);
        if (id === 'pacificstar') { p.invincible = true; p.health = 100; }
        if (id === 'pacificstar' && g.shipRaid?.spawned && !g.shipRaid.safeDone) { const S = g.shipRaid.S.safe; if (p.vehicle) p.vehicle.takeOut(p); p.setPosition(S.x, S.y, S.z - 0.6); }
        if (g.police.level > 0) g.police.clear();
        // kill enemies
        for (const e of ctx.peds) if (e.missionEnemy && !e.dead) { e.takeDamage(9999, { source: p }); }
        // red blipped targets
        for (const b of ctx.blips) {
          const ent = b.entity;
          if (!ent || ent.removed) continue;
          if (b.color === 0xff3030) {
            if (ent.isWrecked === false && ent.def) { ent.health = -1; ent.explode(); }
            else if (ent.takeDamage && !ent.dead && !(id === 'tail')) ent.takeDamage(9999, { source: p });
          }
        }
        // cars to get into
        const carBlip = ctx.blips.find((b) => b.entity && b.entity.def && b.color === 0x4aa3ff && g.blips.has(b));
        if (carBlip && p.vehicle !== carBlip.entity && id !== 'tail') {
          const v = carBlip.entity;
          if (p.vehicle) p.vehicle.takeOut(p);
          if (!v.isWrecked) { if (v.occupants[0] && v.occupants[0] !== p) v.takeOut(v.occupants[0]); v.putIn(p, 0); }
        }
        if (id === 'tail') {
          const d = ctx.blips.find((b) => b.entity && b.entity.def);
          if (d && d.entity) {
            const dv = d.entity;
            const tv = p.vehicle || null;
            if (!tv) { const car = g.vehicles.spawn('meridian', dv.pos.x - 40, dv.pos.z, 0); car.putIn(p, 0); }
            else { tv.pos.set(dv.pos.x - Math.sin(dv.yaw) * 40, dv.pos.y, dv.pos.z - Math.cos(dv.yaw) * 40); tv.vel.set(0, 0, 0); tv.yaw = dv.yaw; }
          }
        }
        // the train mission: pull straight into Union Station once aboard
        if (id === 'solline' && g.rail?.train && p.vehicle === g.rail.train) {
          const tr = g.rail.train, un = g.map.roadInfo.rail.stations.find((s) => s.key === 'union');
          tr.s = un.s + tr.len / 2; tr.v = 0; tr._place();
        }
        // flying rings: put the aircraft in the ring
        if (ctx.currentRing && p.vehicle) {
          const v = p.vehicle, c = ctx.currentRing.c;
          v.pos.set(c.x, c.y - (v.cgY || 0), c.z); v.grounded = false;
          window.__step(0.05, 1 / 30, false); simT += 0.05; await new Promise((r) => setTimeout(r, 0)); continue;
        }
        // markers: go to the newest active marker
        const mk = [...ctx.markers].reverse().find((m) => !m.removed);
        if (mk && id !== 'tail') {
          if (mk.vehicleOnly && !p.vehicle) { const car = g.vehicles.spawn('meridian', p.pos.x + 3, p.pos.z, 0); g.vehicles.list.includes(car); car.putIn(p, 0); }
          if (mk.footOnly && p.vehicle) p.vehicle.takeOut(p);
          if (p.vehicle) { p.vehicle.pos.set(mk.pos.x, mk.pos.y, mk.pos.z); p.vehicle.vel.set(0, 0, 0); }
          else p.setPosition(mk.pos.x, mk.pos.y, mk.pos.z);
          // followers along for the ride
          for (const f of ctx.peds) if (f.state === 'follow' && !f.dead && p.vehicle && !f.vehicle) { const s = [1, 2, 3].find((k) => !p.vehicle.occupants[k]); if (s) p.vehicle.putIn(f, s); }
        }
        window.__step(0.5, 1 / 30, false);
        simT += 0.5;
        await new Promise((r) => setTimeout(r, 0));
      }
      offP(); offF();
      if (!outcome) { outcome = 'TIMEOUT'; reason = lastObj; }
      return { id, outcome, reason, simT: simT.toFixed(0), log: log.slice(-8) };
    }, id);
    console.log(JSON.stringify(res));
  }
}
