# Porting plan: browser game → Unreal Engine 5.8

The port is done in phases, and each phase leaves a game you can play. The rule throughout is to port the
JavaScript's behaviour rather than approximate it. The world generator matches the original number for
number, so the Unreal world has the same streets, buildings and parking spots as the browser one.

## How it is built

The game logic is plain C++ under `Private/Sim`, with no Unreal code, ported line by line from the
JavaScript: the game loop and its systems, input, the collision world (`collision.js`), characters, the
animator, ragdolls, vehicles and the camera rig. It builds and runs in a native test runner
(`Tools/simtest.cpp`), and its numbers can be checked against the browser game run in node: the car
physics comes out identical to 4 decimals (`Tools/vehcompare.mjs`). Unreal's own physics and collision are
not used. The Unreal side (`Private/Game`) draws the simulation's state, feeds it the keyboard, mouse and
pad, and will play its sounds.

## How the systems map

| Browser (src/…) | Unreal | Notes |
|---|---|---|
| world/worldgen.js, roadnet.js, roadlayout.js, northcity.js, railway.js, citymap.js, countryside.js | `Gen/*` (plain C++) | Bit-exact port: Mulberry32, float32 storage where JS used Float32Array, stable sorts, V8's `Math.hypot`. `Tools/gentest` diffs it against `Tools/dumpworld.mjs`. |
| world/terrainmesh.js, roadmesh.js, city.js, props.js, vegetation.js | `Gen/TerrainMesh.cpp`, `RoadMesh.cpp`, `CityMesh.cpp`, `PropMesh.cpp` | Same geometry. The four per-vertex channels carry what the shaders need (see `ATGMaterials.h`). |
| world/shaders.js, render/materials.js | `ATGMaterials` | GLSL → HLSL custom nodes, generated as assets in the editor. No screen-space derivatives (ray-tracing safe). |
| entities/humanoid.js | `Sim/Humanoid`, `ATGHumanMesh`, `ATGPerson` | The smooth-skinned mesh, identical to the JavaScript's (`Tools/humantest.cpp`), as a skeletal mesh built at runtime on a poseable component that copies the simulation's 17 bones. |
| world/environment.js, render/sky.js | `ATGWorld` sky | SkyAtmosphere, a sun and a moon as atmosphere lights, a real-time sky light, height fog, the same clock and sun path. |
| entities/vehicledefs.js, vehiclemodels.js, loft.js | `Gen/VehicleDefs`, `Gen/VehicleModels`, `Gen/Loft` | The whole catalogue; car models identical to the JavaScript's (`Tools/carstest.cpp`). |
| world/collision.js (+ the colliders city.js, roadmesh.js and vegetation.js add) | `Sim/Collision`, `Gen/RoadMesh` | The same boxes, oriented boxes, circles and decks, in the same order (`Tools/coltest.cpp`). |
| game/game.js, core/input.js, core/events.js | `Sim/Game`, `Sim/Input`, `Sim/Events` | The frame order and slow motion as in the browser game. |
| entities/character.js, animator.js, ragdoll.js, game/player.js | `Sim/Character`, `Sim/Animator`, `Sim/Ragdoll`, `Sim/Player` | All of it; drawn with the skinned humanoid (`ATGPerson`). |
| entities/vehicle.js, game/vehicles.js | `Sim/Vehicle`, `Sim/Vehicles` | All of it: physics, tumbling, damage, dents, lost panels, fire, explosions, enter / exit / carjack sequences, parked cars. Drawn by `ATGCar`. |
| game/camera.js | `Sim/Camera` | All the cameras, including the cinematic and flight cameras. |
| world/environment.js | `Sim/Env` | The clock and the weather; `ATGWorld` lights the sky from it. |
| game/traffic.js | `Sim/Traffic` | Lane following, traffic lights (the shaders' clock is the simulation's, so the lamps and the cars agree), give way, roundabouts, merges, queues, passing broken-down cars, panic at gunfire, spawning and despawning. |
| game/peds.js | `Sim/Peds` | Spawning on the walk graph, wandering, fleeing, gangs on their turf, followers and guards. Cops think in the police system, people with a crime on in the street-crime system. |
| game/pickups.js | `Sim/Pickups`, `ATGPickups` | Health, armour, cash and weapon pickups (the world's respawn after 90 s; drops last 40 to 45 s), the 30 hidden packages and the safehouse rewards, the markers (the cylinder's shader evaluated per ring of vertices), the Spray Shack (paint, repairs, loses the police) and the save point (the save menu comes with save and load). Big Bun's `eat` is there for the shops. |
| ui/weaponwheel.js | `Sim/WeaponWheel`, `ATGHudModern.cpp` (DrawWheel) | All of it: Tab or a held LB opens it at 0.18 speed, the mouse or right stick picks, letting go draws the weapon, a quick LB tap goes back one weapon; in a car only what you can shoot from a seat. The ring, the icons (inverted on the highlighted slot), the name and ammo, the dimmed screen. |
| game/special.js | `Sim/Special`, `ATGHudModern.cpp` (the bar), `ATGWorld::SetFringe` | All of it: the meter (a trickle, speed, drifts, jumps, kills and headshots), Caps Lock / Z / both sticks, 0.38 on foot and 0.5 at the wheel with 1.25 grip, the real-time drain, the drained colour. The fringing maps postfx.js's uChroma onto Unreal's scene fringe by eye. |
| game/heists.js | `Sim/Heists`, `ATGCar` (the rear doors) | All of it: a Stockade with two armed guards every 150 to 260 s, the first sighting, alerts (shots, rams, a guard down, a carjack), the run for it, four shots in the back doors or a blast, the four cash bags, the guards out fighting, two stars, the robbed message, the clean-up. |
| ui/phone.js | `Sim/Phone` (with freeroam.js's `roadSpot`), `ATGHudModern.cpp` (DrawPhone) | The phone and its keys (it takes the arrows, Enter and Backspace while out; you still walk), the home screen, Contacts (Lester, Benny's delivery, Merryweather's contractors), the fifteen cheat codes with their five-minute timers, Snapmatic, the map, weather and stats. Still to come with their systems: the cab (taxis), Pegasus and free roam's cheat switches (the admin tools). The phone's glyphs are drawn as small shapes and words (the engine font lacks them). |
| game/npccrime.js | `Sim/NpcCrime`, `ATGHUD` (stars over suspects, speech bubbles) | All of it: jaywalkers, reckless drivers, hit-and-runs, road rage, muggers and car thieves staged near you (more at night and in the rough districts), witnesses and phoned-in reports, stars on the culprit, dispatch, the responding unit (pull-overs, tickets, chases, tackles, the ride in the back) and the radar dots. |
| game/roadblocks.js | `Sim/Roadblocks`, `ATGPoliceView` (spike strips) | All of it: from three stars a line of cruisers (the Sheriff's out of town, the Enforcer at four stars) across the road 90 to 165 m ahead of a driving suspect, cops in cover behind, the stinger that bursts tyres (sparks while you drive on the rims), the dispatch call, the radar squares, the clean-up. The strip now lies on the road surface; in both games it used to sit at the terrain's height, a few centimetres under the road. |
| game/police.js | `Sim/Police`, `ATGPoliceView` | All of it: heat and the six thresholds, witnesses, evading (10 + 5 x stars seconds out of sight), patrol cars, pursuit drivers that route along the roads and then ram, cops on foot who shoot or come to arrest you, roadblock cops holding their line, BUSTED, and the helicopter with its searchlight and its sniper at four stars. The helicopter's rotor sound comes with the audio. |
| game/railsystem.js, entities/train.js | `Sim/Rail`, `Sim/Train`, `Gen/TrainModels` | The timetable, the single-track sections and the Fern Creek passing loop, level crossings, boarding, driving from the cab. The trains run to the metre as in the browser game. |
| game/gameplay.js | `Sim/Gameplay` | Stats, WASTED and BUSTED (slow motion, the death camera, the respawn at the hospital or the police station, the bill), drift and stunt bonuses, smoke and fire on damaged cars, skid marks, first-time hints. |
| game/combat.js | `Sim/Combat`, `ATGEffects` (projectiles) | Hitscan with spread and pellets (aimed from the camera, fired from the muzzle), hits on people, vehicles and the world, melee with its combos and the bat denting cars, explosions sized to the vehicle, rockets, homing missiles, tank shells, grenades, molotovs and their burning pools, people on fire, shooting down the police helicopter (and locking on to it). The animals join the ray with the wildlife. |
| game/effects.js | `Sim/Effects`, `ATGEffects`, `ATGMaterials` (FxAlpha, FxAdd) | The three particle pools, decals, skid marks, tracers, flash lights, explosions with their fire, flying wreckage, smoke columns and secondary blasts, prop and panel debris, broken props restored far away. The canvas textures (smoke, soft dot, the decal atlas) are drawn per pixel in the materials. Boat wakes and rain come with boats and the weather. |
| ui/hud.js (messages and overlays) | `Sim/Hud` (`HudModel`), `ATGHUD` | Help, big messages, subtitles, objectives, the bar, money pops, dispatch, the fade, the damage flash, the shard: the state and timers in the simulation, drawn by `ATGHUD`. |
| ui/hud.js (the modern layout) | `ATGHudModern.cpp`, `ATGPainter` | The minimap (`_drawRadar`: the map turned with the camera, the city layer clipped in, police flashes and search cones, blips and their icons, north, the player arrow), the health, armour and special bars, the stars, cash, weapon icon and ammo, the zone and vehicle names, the speedometer (`_drawSpeedo`: dial, red zone, gear, damage bar, the altimeter for aircraft). Still to come: the GPS route, the classic round radar, the browser game's fonts. |
| main.js (the systems) | `Sim/Setup` | Registers the systems in main.js order and populates the streets. |
| ui/mapimage.js, hud.js | `Gen/MapImage.cpp`, `ATGHUD` | The map is drawn by a small software rasteriser. The HUD is a first cut. |

## Phases

**Status.** Phase 1 builds and runs on UE 5.8.3 (Visual Studio 2026). The game logic has moved into the
simulation layer (above): walking, getting in and out, driving, crashes and parked cars run there and are
drawn by Unreal. Phase 2 is done: traffic, people, traffic lights, trains and the skinned humanoid. Phase 3
is under way: WASTED and BUSTED, the effects, combat, the police and the pickups are done here; shops are on a second branch
(`oc-phase3`, worked by a second agent and merged here once checked).

**Phase 1: the world and driving (done).**
- World generation.
- Every static mesh: terrain with streamed detail, roads, bridges, the railway, city ground, buildings,
  props, trees, water.
- Materials, the day and night cycle, street lamps.
- Walking, parked cars, getting in and out, the full car physics.
- Radar and map.

**Phase 2: a living city.**
- Traffic on the lane graph (traffic.js). Done.
- Pedestrians and gangs on the walk graph (peds.js). Done.
- Traffic lights. Done: the lamps read the simulation's clock (`SimTime` in the parameter collection).
- Trains on the timetable (railsystem.js, train.js). Done.
- A skinned humanoid with the procedural animator, and ragdolls. Done.

**Phase 3: action.**
- Weapons, melee, damage and effects (combat.js, effects.js). Done.
- Pickups (done) and shops (the clerks need the interiors' furniture and the shop menus: with the interiors).
- Street crime (npccrime.js). Done.
- Wanted level and police (police.js). Done.
- Car damage, fire and explosions; vehicle tumbling (vehicle.js `_tumble`). Done (with vehicle.js in phase 1).
- WASTED and BUSTED screens. Done (the stinger plays once audio is ported). Replace the WASTED sound clip
  with your own before sharing the game.

**Phase 3b: the GTA V layer** (added to the browser game after phase 1).
- The modern HUD: a rectangular minimap with health, armour and special bars, and the police's vision cones
  while they search.
- The weapon wheel (weaponwheel.js) and the special ability (special.js), both with slow motion. Done.
- The phone (phone.js): contacts, timed cheats, photo mode. Done (the cab and Pegasus wait for taxis and the admin tools).
- Roadblocks and spike strips (roadblocks.js). Done. The army at five stars (army.js).
- Armoured van heists (heists.js), the cinematic car camera, and the new vehicles: Stockade, Enforcer,
  Sheriff SUV, Buffalo S, Baller, Tempest. Done (the camera and the cars came with phase 1).

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

## Known gaps

- Shops are solid boxes for now (their shells' colliders are replaced by a solid box). The interiors, and
  the colliders of their furniture, come in phase 6.
- The Santa Luz pier is a plain deck; the landmarks' own colliders come with them (phase 6).
- Billboards (and their colliders) are not built yet.
- The people's material takes each part's roughness from humanoidMaterial, but not yet its fine fabric,
  hair and denim patterns, or the metalness of buckles and badges.
- Motorbikes are left out of the traffic and the parked cars until bikes.js is ported (phase 4).
- The radar and the map show blips as plain squares until the HUD is ported (phase 3b).
- Online, the trains follow the host's timetable; that comes with multiplayer (phase 6).
- No sounds yet: the simulation asks for them, nothing plays them (phase 6, audio).
- The HUD uses the engine's default font, scaled up; the browser game's fonts (Anton for the shard) come
  with the phase 3b HUD.
- The WASTED look approximates postfx.js with Unreal's post-process (saturation, gain, vignette); the radial
  blur of the death effect is not there yet.
- Headlight beams on the road are not drawn yet.
- Smoke and decals are lit by a grey stand-in for effects.js's sky-coloured light.
