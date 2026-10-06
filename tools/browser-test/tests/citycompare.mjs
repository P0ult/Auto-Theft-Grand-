// Numbers for the Unreal port's native "city" and "rail" tests (Unreal/AutoTheftGrand/Tools/simtest.cpp):
// traffic and pedestrians populated round the street by the safehouse, then 20 s of play; and both trains
// respawned at their stations, then 200 s of timetable.
export default async function (page, shot, ev) {
  const r = await ev(() => {
    const g = window.__game;
    g.admin.cheats.god = true;
    const home = g.map.landmarks.home;
    // the north-south street nearest home, as simtest's ToStreet (citymap.js XS)
    const xs = [-830, -730, -630, -535, -440, -345, -255, -165, -75, 15, 105, 195, 285, 380, 480, 580, 680, 775, 870];
    let x = xs[0], bd = Infinity;
    for (const v of xs) { const d = Math.abs(v - home.x); if (d < bd) { bd = d; x = v; } }
    for (const p of [...g.peds.list]) g.peds.remove(p);
    for (const v of [...g.traffic.cars]) g.traffic._despawn(v);
    g.player.setPosition(x + 2, undefined, home.z);
    g.traffic.populate(Math.floor(g.traffic.maxCars * 0.7));
    g.peds.populate(Math.floor(g.peds.maxPeds * 0.6));
    const cars = g.traffic.cars.filter((v) => !v.removed).map((v) => [v, v.pos.x, v.pos.z]);
    const peds = g.peds.list.map((p) => [p, p.pos.x, p.pos.z]);
    const out = { cars: cars.length, peds: peds.length };
    window.__step(20, 1 / 30, false);
    let moved = 0, total = 0, dist = 0;
    for (const [v, x0, z0] of cars) { if (v.removed) continue; total++; const d = Math.hypot(v.pos.x - x0, v.pos.z - z0); dist += d; if (d > 20) moved++; }
    let walked = 0, ptotal = 0;
    const states = {};
    for (const [p, x0, z0] of peds) { if (p.removed) continue; ptotal++; if (Math.hypot(p.pos.x - x0, p.pos.z - z0) > 3) walked++; states[p.state] = (states[p.state] || 0) + 1; }
    Object.assign(out, { moved, total, meanDist: total ? dist / total : 0, walked, ptotal, states, carsNow: g.traffic.cars.length, pedsNow: g.peds.list.length });
    return out;
  });
  console.log('city', JSON.stringify(r));
  const t = await ev(() => {
    const g = window.__game, rs = g.rail;
    for (const tr of rs.trains) g.vehicles.remove(tr);
    rs.train = null; rs.freight = null; rs.tokens = { W: null, E: null };
    const t0 = g.time, out = { arrivals: [] };
    g.events.on('trainArrived', (tr, st) => out.arrivals.push([tr.freight ? 'freight' : 'passenger', st.name, +(g.time - t0).toFixed(1)]));
    window.__step(1 / 30, 1 / 30, false);
    const pass = rs.train, fr = rs.freight;
    out.start = { pass: pass.s, fr: fr.s, wagons: fr.cars.length };
    let vmax = 0, crossing = false;
    for (let i = 0; i < 30 * 200; i++) { window.__step(1 / 30, 1 / 30, false); vmax = Math.max(vmax, Math.abs(pass.v)); if (rs.crossings.some((c) => c.active)) crossing = true; }
    Object.assign(out, { pass: pass.s, fr: fr.s, vmax, crossing });
    return out;
  });
  console.log('rail', JSON.stringify(t));
}
