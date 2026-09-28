# Porting plan: browser game → Unreal Engine 5.8

The port is done in phases, and each phase leaves a game you can play. The rule throughout is to port the
JavaScript's behaviour rather than approximate it. The world generator matches the original number for
number, so the Unreal world has the same streets, buildings and parking spots as the browser one.

## How the systems map

| Browser (src/…) | Unreal | Notes |
|---|---|---|
| world/worldgen.js, roadnet.js, roadlayout.js, northcity.js, railway.js, citymap.js, countryside.js | `Gen/*` (plain C++) | Bit-exact port: Mulberry32, float32 storage where JS used Float32Array, stable sorts, V8's `Math.hypot`. `Tools/gentest` diffs it against `Tools/dumpworld.mjs`. |
| world/terrainmesh.js, roadmesh.js, city.js, props.js, vegetation.js | `Gen/TerrainMesh.cpp`, `RoadMesh.cpp`, `CityMesh.cpp`, `PropMesh.cpp` | Same geometry. The four per-vertex channels carry what the shaders need (see `ATGMaterials.h`). |
| world/shaders.js, render/materials.js | `ATGMaterials` | GLSL → HLSL custom nodes, generated as assets in the editor. No screen-space derivatives (ray-tracing safe). |
| entities/vehiclemodels.js, humanoid.js | `Gen/Models.cpp` | Lofted car bodies; a segmented person (not skinned yet). |
| world/environment.js, render/sky.js | `ATGWorld` sky | SkyAtmosphere, a sun and a moon as atmosphere lights, a real-time sky light, height fog, the same clock and sun path. |
| world/collision.js | Unreal collision + `ATGWorld` circles | World geometry collides (hidden boxes for walls, prisms for posts and trunks, fine terrain near the player). Cars use traces plus the original circle and box tests. |
| entities/vehicle.js | `ATGCar` | `_step`, `_afterPhysics` and `_resolveStatic` ported. Tumbling, damage visuals and fire are not ported yet. |
| game/player.js, camera.js, core/input.js | `ATGCharacter`, `ATGPlayerController` | Character movement for walking; the camera rig is ported as is. |
| game/vehicles.js (parked cars) | `ATGGameMode` | Same spots, same odds, same car choices. |
| ui/mapimage.js, hud.js | `Gen/MapImage.cpp`, `ATGHUD` | The map is drawn by a small software rasteriser. The HUD is a first cut. |

## Phases

**Phase 1: the world and driving (done).**
- World generation.
- Every static mesh: terrain with streamed detail, roads, bridges, the railway, city ground, buildings,
  props, trees, water.
- Materials, the day and night cycle, street lamps.
- Walking, parked cars, getting in and out, the full car physics.
- Radar and map.

**Phase 2: a living city.**
- Traffic on the lane graph (traffic.js).
- Pedestrians and gangs on the walk graph (peds.js).
- Traffic lights: the signal phase is already in the prop instances' custom data.
- Trains on the timetable (railsystem.js, train.js).
- A skinned humanoid with the procedural animator, and ragdolls.

**Phase 3: action.**
- Weapons, melee, damage and effects (combat.js, effects.js).
- Pickups and shops.
- Wanted level and police (police.js).
- Car damage, fire and explosions; vehicle tumbling (vehicle.js `_tumble`).
- WASTED and BUSTED screens. Replace the WASTED sound clip with your own before sharing the game.

**Phase 4: the rest of the vehicles.**
- Motorbikes and bicycles, boats and police boats, planes, jets, helicopters, tanks (Fort Carver)
- Skateboards and the skatepark.

**Phase 5: story and features.**
- The mission engine and both storylines (missions.js, story.js, story_north.js).
- The ship raid.
- Taxis, races, vigilante, Customs garages, property.
- Free roam and admin tools.
- Save and load.

**Phase 6: the finish.**
- Walk-in interiors with their furniture and shopkeepers.
- Landmarks: the pier's rides, the Ferris wheel.
- Chain-link fences, billboards, rain and puddles.
- Audio and the radio stations.
- Wildlife and pets.
- Multiplayer, likely with Unreal's own replication.

## Known gaps in phase 1

- Shops are solid boxes for now. Their interiors come in phase 6.
- The Santa Luz pier is a plain deck.
- There is no swimming yet. Deep water sends you back to the safehouse.
- Cars can't flip over yet. Hard landings and crashes cost health, and a wrecked engine stops the car.
