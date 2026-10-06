// Drives a car with fixed inputs in the browser game's own physics (no renderer) and prints its state, to
// compare with `simtest.exe vehcompare` (the C++ port). Same world collision: the map's colliders and the
// road network's walls and decks.
//   node --import ./three-hook.mjs vehcompare.mjs > jsveh.txt
import { CityMap } from '../../../src/world/citymap.js';
import { CollisionWorld } from '../../../src/world/collision.js';
import { RoadMeshes } from '../../../src/world/roadmesh.js';
import { Vehicle } from '../../../src/entities/vehicle.js';
import { XS } from '../../../src/world/citymap.js';
const map = new CityMap();
const collision = new CollisionWorld(map);
new RoadMeshes({ add() {} }, map, collision);
const game = { map, collision, time: 0, scene: { add() {} }, events: { emit() {} }, gravity: 1 };
const home = map.landmarks.home;
const x0 = XS[map.nearestX(home.x)] + 2;
const scripts = {
  straight: (f) => ({ throttle: 1, steer: 0, brake: 0, handbrake: false }),
  turn: (f) => ({ throttle: 1, steer: f > 45 ? 0.6 : 0, brake: 0, handbrake: false }),
  drift: (f) => ({ throttle: 1, steer: f > 50 ? 1 : 0, brake: 0, handbrake: f > 50 && f < 60 }),
};
const out = [];
for (const [name, script] of Object.entries(scripts)) {
  const v = new Vehicle(game, 'kestrel', { x: x0, z: home.z - 120, yaw: 0, color: 0xffffff });
  v.occupants[0] = { isPlayer: false };
  for (let f = 0; f < 120; f++) {
    Object.assign(v.input, script(f));
    v.update(1 / 30);
    game.time += 1 / 30;
    if (f % 10 === 9) out.push(`${name} ${f + 1} ${v.pos.x.toFixed(4)} ${v.pos.y.toFixed(4)} ${v.pos.z.toFixed(4)} ${v.yaw.toFixed(5)} ${v.vel.x.toFixed(4)} ${v.vel.z.toFixed(4)} ${v.health.toFixed(2)}`);
  }
}
console.log(out.join('\n'));
